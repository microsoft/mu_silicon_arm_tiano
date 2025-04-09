/** @file SmmuDxe.c

    This file contains functions for the SMMU driver.

    This driver consumes a SMMU_CONFIG Hob structure defined by the platform to configure the SMMU hardware.
    Initializes the SmmuV3 hardware to enable stage 2 translation and dma remapping.
    Installs the IORT to describe the SMMU configuration to the OS.
    Implements the IoMmu protocol to provide a generic interface for mapping host memory to device memory.

    Copyright (c) Microsoft Corporation.
    SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/IoMmu.h>
#include <Guid/SmmuConfig.h>
#include "IoMmu.h"
#include "SmmuV3.h"

// Global IOMMU/SMMU instance
IOMMU_CONFIG  *mIoMmu;

/**
  Calculate and update the checksum of an ACPI table.

  @param [in, out]  Buffer    Pointer to the ACPI table buffer.
  @param [in]       Size      Size of the ACPI table buffer.

  @retval EFI_SUCCESS            Success.
  @retval EFI_INVALID_PARAMETER  Invalid parameter.
**/
STATIC
EFI_STATUS
AcpiPlatformChecksum (
  IN OUT UINT8  *Buffer,
  IN UINTN      Size
  )
{
  UINTN  ChecksumOffset;

  if ((Buffer == NULL) || (Size == 0)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Parameters\n", __func__));
    return EFI_INVALID_PARAMETER;
  }

  ChecksumOffset = OFFSET_OF (EFI_ACPI_DESCRIPTION_HEADER, Checksum);

  // Set checksum field to 0 since it is used as part of the calculation
  Buffer[ChecksumOffset] = 0;

  Buffer[ChecksumOffset] = CalculateCheckSum8 (Buffer, Size);

  return EFI_SUCCESS;
}

/**
  Add the IORT ACPI table.

  @param [in]  AcpiTableProtocol    Pointer to the ACPI Table Protocol.
  @param [in]  IortData             Pointer to the IORT.
  @param [in]  IortSize             Size of the IORT table.

  @retval EFI_SUCCESS               Success.
  @retval EFI_OUT_OF_RESOURCES      Out of resources.
  @retval EFI_INVALID_PARAMETER     Invalid parameter.
**/
EFI_STATUS
AddIortTable (
  IN EFI_ACPI_TABLE_PROTOCOL  *AcpiTable,
  IN VOID                     *IortData,
  IN UINT32                   IortSize
  )
{
  EFI_STATUS            Status;
  UINTN                 TableHandle;
  EFI_PHYSICAL_ADDRESS  PageAddress;

  if ((AcpiTable == NULL) || (IortData == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Parameters\n", __func__));
    return EFI_INVALID_PARAMETER;
  }

  Status = gBS->AllocatePages (
                  AllocateAnyPages,
                  EfiACPIReclaimMemory,
                  EFI_SIZE_TO_PAGES (IortSize),
                  &PageAddress
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate pages for IORT table\n", __func__));
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem ((VOID *)(UINTN)PageAddress, EFI_SIZE_TO_PAGES (IortSize) * EFI_PAGE_SIZE);
  CopyMem ((VOID *)(UINTN)PageAddress, IortData, IortSize);

  Status = AcpiPlatformChecksum ((UINT8 *)(UINTN)PageAddress, IortSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to calculate checksum for IORT table\n", __func__));
    return Status;
  }

  Status = AcpiTable->InstallAcpiTable (
                        AcpiTable,
                        (EFI_ACPI_COMMON_HEADER *)(UINTN)PageAddress,
                        IortSize,
                        &TableHandle
                        );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to install IORT table\n", __func__));
  }

  return Status;
}

/**
  Initialize a page table. Only initializes the root page table.
  UpdateMapping() will allocate entries on the fly as needed.

  @retval A pointer to the initialized page table, or NULL on failure.
**/
STATIC
PAGE_TABLE *
PageTableInit (
  VOID
  )
{
  PAGE_TABLE  *PageTable;

  PageTable = (PAGE_TABLE *)AllocatePages (1);
  if (PageTable == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate page table\n", __func__));
    return NULL;
  }

  ZeroMem (PageTable, EFI_PAGE_SIZE);

  return PageTable;
}

/**
  Recursivley deinitialize and free a page table for all previously
  allocated entries, given its level and pointer.

  @param [in]  Level      The level of the page table to deinitialize.
  @param [in]  PageTable  The page table to deinitialize.
**/
STATIC
VOID
PageTableDeInit (
  IN UINT8       Level,
  IN PAGE_TABLE  *PageTable
  )
{
  UINTN  Index;

  if ((Level >= PAGE_TABLE_DEPTH) || (PageTable == NULL)) {
    return;
  }

  for (Index = 0; Index < PAGE_TABLE_SIZE; Index++) {
    PAGE_TABLE_ENTRY  Entry             = PageTable->Entries[Index];
    PAGE_TABLE        *PageTableAddress = (PAGE_TABLE *)((UINTN)Entry & ~PAGE_TABLE_BLOCK_OFFSET);

    if (Entry != 0) {
      PageTableDeInit (Level + 1, PageTableAddress);
    }
  }

  FreePages (PageTable, EFI_SIZE_TO_PAGES (sizeof (PAGE_TABLE)));
}

/**
  Allocate an event queue for SMMUv3.

  @param [in]   SmmuInfo       Pointer to the SMMU_INFO structure.
  @param [out]  QueueLog2Size  Pointer to store the log2 size of the queue.
  @param [out]  EventQueueBase Pointer to store the base address of the allocated event queue.

  @retval EFI_SUCCESS          The event queue was allocated successfully.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.
  @retval EFI_OUT_OF_RESOURCES  Allocation failed due to insufficient resources.
**/
STATIC
EFI_STATUS
SmmuV3AllocateEventQueue (
  IN  SMMU_INFO  *SmmuInfo,
  OUT UINT32     *QueueLog2Size,
  OUT VOID       **EventQueueBase
  )
{
  UINT32       QueueSize;
  SMMUV3_IDR1  Idr1;
  UINT32       Pages;

  if ((SmmuInfo == NULL) || (QueueLog2Size == NULL) || (EventQueueBase == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Parameters\n", __func__));
    return EFI_INVALID_PARAMETER;
  }

  Idr1.AsUINT32 = SmmuV3ReadRegister32 (SmmuInfo->SmmuBase, SMMU_IDR1);

  *QueueLog2Size  = MIN (Idr1.EventQs, SMMUV3_EVENT_QUEUE_LOG2ENTRIES);
  QueueSize       = SMMUV3_EVENT_QUEUE_SIZE_FROM_LOG2 (*QueueLog2Size);
  Pages           = EFI_SIZE_TO_PAGES (QueueSize);
  *EventQueueBase = AllocatePages (Pages);

  if (*EventQueueBase == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Allocation failed\n", __func__));
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (*EventQueueBase, EFI_PAGES_TO_SIZE (Pages));
  return EFI_SUCCESS;
}

/**
  Allocate a command queue for SMMUv3.

  @param [in]   SmmuInfo       Pointer to the SMMU_INFO structure.
  @param [out]  QueueLog2Size  Pointer to store the log2 size of the queue.
  @param [out]  CmdQueueBase   Pointer to store the base address of the allocated command queue.

  @retval EFI_SUCCESS          The command queue was allocated successfully.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.
  @retval EFI_OUT_OF_RESOURCES  Allocation failed due to insufficient resources.
**/
STATIC
EFI_STATUS
SmmuV3AllocateCommandQueue (
  IN  SMMU_INFO  *SmmuInfo,
  OUT UINT32     *QueueLog2Size,
  OUT VOID       **CmdQueueBase
  )
{
  UINT32       QueueSize;
  SMMUV3_IDR1  Idr1;
  UINT32       Pages;

  if ((SmmuInfo == NULL) || (QueueLog2Size == NULL) || (CmdQueueBase == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Parameters\n", __func__));
    return EFI_INVALID_PARAMETER;
  }

  Idr1.AsUINT32 = SmmuV3ReadRegister32 (SmmuInfo->SmmuBase, SMMU_IDR1);

  *QueueLog2Size = MIN (Idr1.CmdQs, SMMUV3_COMMAND_QUEUE_LOG2ENTRIES);
  QueueSize      = SMMUV3_COMMAND_QUEUE_SIZE_FROM_LOG2 (*QueueLog2Size);
  Pages          = EFI_SIZE_TO_PAGES (QueueSize);
  *CmdQueueBase  = AllocatePages (Pages);

  if (*CmdQueueBase == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Allocation failed\n", __func__));
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (*CmdQueueBase, EFI_PAGES_TO_SIZE (Pages));
  return EFI_SUCCESS;
}

/**
  Free a previously allocated queue.

  @param [in]  QueuePtr    Pointer to the queue to free.
**/
STATIC
VOID
SmmuV3FreeQueue (
  IN VOID    *QueuePtr,
  IN UINT32  Log2Size
  )
{
  UINT32  Size;

  if (QueuePtr == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid parameters. QueuePtr == NULL\n", __func__));
  } else {
    Size = SMMUV3_COMMAND_QUEUE_SIZE_FROM_LOG2 (Log2Size);
    FreePages ((VOID *)QueuePtr, EFI_SIZE_TO_PAGES (Size));
  }
}

/**
  Build the stream table for SMMUv3.

  @param [in]  SmmuInfo       Pointer to the SMMU_INFO structure.
  @param [in]  StreamId       Stream ID.
  @param [out] StreamEntry    Pointer to the stream table entry.

  @retval EFI_SUCCESS         Success.
  @retval EFI_INVALID_PARAMETER  Invalid parameter.
**/
STATIC
EFI_STATUS
SmmuV3BuildStreamTableEntry (
  IN SMMU_INFO                   *SmmuInfo,
  IN UINT32                      StreamId,
  OUT SMMUV3_STREAM_TABLE_ENTRY  *StreamEntry
  )
{
  EFI_STATUS   Status;
  UINT32       OutputAddressWidth;
  UINT32       InputSize;
  SMMUV3_IDR0  Idr0;
  SMMUV3_IDR1  Idr1;
  SMMUV3_IDR5  Idr5;
  UINT8        IortCohac;
  UINT32       CCA;
  UINT8        CPM;
  UINT8        DACS;
  UINT64       S2Sl0;

  if ((SmmuInfo == NULL) || (SmmuInfo->StreamEntryConfig == NULL) || (StreamEntry == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Parameters\n", __func__));
    return EFI_INVALID_PARAMETER;
  }

  IortCohac = SmmuInfo->Flags & EFI_ACPI_IORT_SMMUv3_FLAG_COHAC_OVERRIDE;                              // Cohac override flag
  CCA       = SmmuInfo->StreamEntryConfig[StreamId].CacheCoherentAttribute;                            // Cache Coherent Attribute
  CPM       = SmmuInfo->StreamEntryConfig[StreamId].MemoryAccessFlags & SMMUV3_STREAM_TABLE_ENTRY_CPM; // Coherent Path to Memory

  // Device attributes are Cacheable and Inner-Shareable
  DACS = (SmmuInfo->StreamEntryConfig[StreamId].MemoryAccessFlags & SMMUV3_STREAM_TABLE_ENTRY_DACS) >> 1;      // Shift by 1 to isolate DACS bit.

  ZeroMem ((VOID *)StreamEntry, sizeof (SMMUV3_STREAM_TABLE_ENTRY));

  Idr0.AsUINT32 = SmmuV3ReadRegister32 (SmmuInfo->SmmuBase, SMMU_IDR0);
  Idr1.AsUINT32 = SmmuV3ReadRegister32 (SmmuInfo->SmmuBase, SMMU_IDR1);
  Idr5.AsUINT32 = SmmuV3ReadRegister32 (SmmuInfo->SmmuBase, SMMU_IDR5);

  StreamEntry->Config = SMMUV3_STREAM_TABLE_ENTRY_CONFIG_STAGE_2_TRANSLATE_STAGE_1_BYPASS;
  StreamEntry->Eats   = SMMUV3_STREAM_TABLE_ENTRY_EATS_NOT_SUPPORTED;
  StreamEntry->S2Vmid = SMMUV3_STREAM_TABLE_ENTRY_S2VMID;             // Choose a non-zero value
  StreamEntry->S2Tg   = SMMUV3_STREAM_TABLE_ENTRY_S2TG_4KB;
  StreamEntry->S2Aa64 = 1;                                                                                // AArch64 S2 translation tables
  StreamEntry->S2Ttb  = (UINT64)(UINTN)SmmuInfo->PageTableRoot >> SMMUV3_STREAM_TABLE_ENTRY_S2TTB_OFFSET; // Page table root address
  if ((Idr0.S1p == 1) && (Idr0.S2p == 1)) {
    StreamEntry->S2Ptw = SMMUV3_STREAM_TABLE_ENTRY_S2PTW;
  }

  //
  // Set the maximum output address width. Per SMMUv3.2 spec (sections 5.2 and
  // 3.4.1), the maximum input address width with AArch64 format is given by
  // SMMU_IDR5.OAS field and capped at:
  // - 48 bits in SMMUv3.0,
  // - 52 bits in SMMUv3.1+. However, an address greater than 48 bits can
  //   only be output from stage 2 when a 64KB translation granule is in use
  //   for that translation table, which is not currently supported (only 4KB
  //   granules).
  //
  //  Thus the maximum input address width is restricted to 48-bits even if
  //  it is advertised to be larger.
  //
  OutputAddressWidth = SmmuV3DecodeAddressWidth (Idr5.Oas);

  if (OutputAddressWidth < SMMUV3_STREAM_TABLE_ENTRY_OUTPUT_ADDRESS_MAX) {
    StreamEntry->S2Ps = SmmuV3EncodeAddressWidth (OutputAddressWidth);
  } else {
    DEBUG ((DEBUG_INFO, "%a: Advertised OutputAddressWidth >= 48. Capping the width to 48 per the SMMU spec.\n", __func__));
    StreamEntry->S2Ps  = SmmuV3EncodeAddressWidth (SMMUV3_STREAM_TABLE_ENTRY_OUTPUT_ADDRESS_MAX);
    OutputAddressWidth = SMMUV3_STREAM_TABLE_ENTRY_OUTPUT_ADDRESS_MAX;
  }

  Status = SmmuV3SetTranslationStartingLevel (SmmuInfo, OutputAddressWidth, &S2Sl0);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to set translation starting level\n", __func__));
    return Status;
  }

  // S2SL0      Meaning
  // <https://developer.arm.com/documentation/ddi0595/2021-03/AArch64-Registers/VTCR-EL2--Virtualization-Translation-Control-Register?lang=en#fieldset_0-7_6-1>
  // Starting level of the stage 2 translation lookup, controlled by VTCR_EL2. The meaning of this field depends on the value of VTCR_EL2.TG0.
  // 0x2:
  // If VTCR_EL2.TG0 is 0b00 (4KB granule):
  // If FEAT_LPA2 is not implemented, start at level 0.
  // If FEAT_LPA2 is implemented and VTCR_EL2.SL2 is 0b0, start at level 0.
  // If FEAT_LPA2 is implemented, the combination of VTCR_EL2.SL0 == 10 and VTCR_EL2.SL2 == 1 is reserved.
  // If VTCR_EL2.TG0 is 0b10 (16KB granule) or 0b01 (64KB granule), start at level 1.
  //
  StreamEntry->S2Sl0 = S2Sl0;

  InputSize           = OutputAddressWidth;
  StreamEntry->S2T0Sz = 64 - InputSize;

  /**
    If Platform configures cohac ovveride, coherent translation table walks,
    then update the attributes as:
    - Inner/Outer cacheability -> Write-back-cacheable (WBC),
              Read-Allocate (RA), Write-Allocate (WA)
    - Shareability -> Inner-shareable.

    Otherwise, the default attributes (set above) apply:
    - Inner/Outer cacheability -> Non-cacheable (0x0),
    - Shareability -> Non-shareable (0x0).
  **/
  if (IortCohac != 0) {
    StreamEntry->S2Ir0 = ARM64_RGNCACHEATTR_WRITEBACK_WRITEALLOCATE;
    StreamEntry->S2Or0 = ARM64_RGNCACHEATTR_WRITEBACK_WRITEALLOCATE;
    StreamEntry->S2Sh0 = ARM64_SHATTR_INNER_SHAREABLE;
  } else {
    StreamEntry->S2Ir0 = ARM64_RGNCACHEATTR_NONCACHEABLE;
    StreamEntry->S2Or0 = ARM64_RGNCACHEATTR_NONCACHEABLE;
    StreamEntry->S2Sh0 = ARM64_SHATTR_OUTER_SHAREABLE;
  }

  StreamEntry->S2Rs = SMMUV3_STREAM_TABLE_ENTRY_S2RS_RECORD_FAULTS;   // record faults

  if (Idr1.AttrTypesOvr != 0) {
    StreamEntry->ShCfg = SMMUV3_STREAM_TABLE_ENTRY_SHCFG_INCOMING_SHAREABILITY; // incoming shareability attribute
  }

  // If the device requires memory attribute overrides, then hard-code it to
  // Inner+Outer write-back cached and Inner-shareable (IWB-OWB-ISH) as
  // given by the IORT spec.
  if ((Idr1.AttrTypesOvr != 0) && ((CCA == 1) && (CPM == 1) && (DACS == 0))) {
    StreamEntry->Mtcfg   = SMMUV3_STREAM_TABLE_ENTRY_MTCFG;
    StreamEntry->MemAttr = SMMUV3_STREAM_TABLE_ENTRY_MEMATTR_INNER_OUTTER_WRITEBACK_CACHED; // Inner+Outer write-back cached
    StreamEntry->ShCfg   = SMMUV3_STREAM_TABLE_ENTRY_SHCFG_INNER_SHAREABLE;                 // Inner shareable
  }

  StreamEntry->Valid = SMMUV3_STREAM_TABLE_ENTRY_VALID;

  return Status;
}

/**
  Allocate a linear stream table for SMMUv3.

  For allocating a 2-level or linear stream table, the stream table alignment
  requirements per SMMUv3 spec:
  - For 2-level table, the table needs to be aligned to the larger of L1
    table size or 64 bytes.
  - For linear table, the table needs to be aligned to its size.

  This function uses the linear stream table.

  @param [in]  SmmuInfo       Pointer to the SMMU_INFO structure.
  @param [out] Log2Size       Pointer to store the log2 size of the stream table.
  @param [out] Size           Pointer to store the size of the stream table.

  @retval Pointer to the allocated stream table, or NULL on failure.
**/
STATIC
SMMUV3_STREAM_TABLE_ENTRY *
SmmuV3AllocateStreamTable (
  IN SMMU_INFO  *SmmuInfo,
  OUT UINT32    *Log2Size,
  OUT UINT32    *Size
  )
{
  UINT32  MaxStreamId;
  UINT32  SidMsb;
  UINT32  Alignment;
  UINTN   Pages;
  VOID    *AllocatedAddress;

  if ((SmmuInfo == NULL) || (Log2Size == NULL) || (Size == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Parameters\n", __func__));
    return NULL;
  }

  // The max stream id is calculated as the output base + the number of stream ids
  MaxStreamId      = SmmuInfo->StreamTableEntryMax;
  SidMsb           = HighBitSet32 (MaxStreamId);
  *Log2Size        = SidMsb + 1;
  *Size            = SMMUV3_LINEAR_STREAM_TABLE_SIZE_FROM_LOG2 (*Log2Size);
  *Size            = ALIGN_VALUE (*Size, EFI_PAGE_SIZE);
  Alignment        = *Size; // Aligned to the size of the table, linear stream table
  Pages            = EFI_SIZE_TO_PAGES (*Size);
  AllocatedAddress = AllocateAlignedPages (Pages, Alignment);

  ZeroMem (AllocatedAddress, *Size);
  return (SMMUV3_STREAM_TABLE_ENTRY *)AllocatedAddress;
}

/**
  Free the allocated stream table for SMMUv3.

  @param [in] StreamTablePtr  Pointer to the stream table entry.
  @param [in] Size            Size of the stream table.
**/
STATIC
VOID
SmmuV3FreeStreamTable (
  IN VOID    *StreamTablePtr,
  IN UINT32  Size
  )
{
  UINTN  Pages;

  if ((StreamTablePtr == NULL) || (Size == 0)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Parameters\n", __func__));
    return;
  }

  Pages = EFI_SIZE_TO_PAGES (Size);
  FreeAlignedPages ((VOID *)StreamTablePtr, Pages);
}

/**
  Configure the SMMUv3 based on the provided configuration per the SmmuV3 specification.
  Main configuration function for smmu hardware. Creates and enables a stream table, page table,
  event queue, and command queue. Enables stage 2 translation and dma remapping.

  <https://developer.arm.com/documentation/109242/0100/Programming-the-SMMU/Minimum-configuration>
  <https://developer.arm.com/documentation/ihi0070/latest/>

  @param [in] SmmuInfo        Pointer to the SMMU_INFO structure.
  @param [in] PageTableRoot   Pointer to the page table root.

  @retval EFI_SUCCESS            Success.
  @retval EFI_INVALID_PARAMETER  Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES   Out of resources.
  @retval EFI_TIMEOUT            Timeout.
  @retval EFI_DEVICE_ERROR       Device error.
  @retval Others                 Failure.
**/
STATIC
EFI_STATUS
SmmuV3Configure (
  IN SMMU_INFO   *SmmuInfo,
  IN PAGE_TABLE  *PageTableRoot
  )
{
  EFI_STATUS                 Status;
  UINT32                     Index;
  UINT32                     StreamTableLog2Size;
  UINT32                     StreamTableSize;
  UINT32                     CommandQueueLog2Size;
  UINT32                     EventQueueLog2Size;
  UINT8                      ReadWriteAllocationHint;
  SMMUV3_STRTAB_BASE         StrTabBase;
  SMMUV3_STRTAB_BASE_CFG     StrTabBaseCfg;
  SMMUV3_STREAM_TABLE_ENTRY  *StreamTablePtr;
  SMMUV3_CMDQ_BASE           CommandQueueBase;
  SMMUV3_EVENTQ_BASE         EventQueueBase;
  SMMUV3_CR0                 Cr0;
  SMMUV3_CR1                 Cr1;
  SMMUV3_CR2                 Cr2;
  SMMUV3_IDR0                Idr0;
  SMMUV3_CMD_GENERIC         Command;
  SMMUV3_GERROR              GError;
  VOID                       *CommandQueue;
  VOID                       *EventQueue;

  if ((SmmuInfo == NULL) || (PageTableRoot == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Parameters\n", __func__));
    return EFI_INVALID_PARAMETER;
  }

  // Set ReadWriteAllocationHint based on the COHAC_OVERRIDE flag.
  // These hints are applied to the allocated Stream Table, Command Queue, and Event Queue.
  if ((SmmuInfo->Flags & EFI_ACPI_IORT_SMMUv3_FLAG_COHAC_OVERRIDE) != 0) {
    ReadWriteAllocationHint = 0x1;
  } else {
    ReadWriteAllocationHint = 0x0;
  }

  // Disable SMMU before configuring
  Status = SmmuV3DisableTranslation (SmmuInfo->SmmuBase);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Error disabling translation\n", __func__));
    goto End;
  }

  Status = SmmuV3DisableInterrupts (SmmuInfo->SmmuBase, TRUE);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Error disabling interrupts\n", __func__));
    goto End;
  }

  // Allocate Linear Stream Table
  StreamTablePtr = SmmuV3AllocateStreamTable (SmmuInfo, &StreamTableLog2Size, &StreamTableSize);
  if (StreamTablePtr == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Error allocating stream table\n", __func__));
    Status = EFI_OUT_OF_RESOURCES;
    goto End;
  }

  SmmuInfo->StreamTable         = StreamTablePtr;
  SmmuInfo->StreamTableSize     = StreamTableSize;
  SmmuInfo->StreamTableLog2Size = StreamTableLog2Size;

  SmmuInfo->PageTableRoot = PageTableRoot;
  if (SmmuInfo->PageTableRoot == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Error initializing Page Table\n", __func__));
    Status = EFI_OUT_OF_RESOURCES;
    goto End;
  }

  // Load default STE values
  for (Index = 0; Index < SmmuInfo->StreamTableEntryMax; Index++) {
    Status = SmmuV3BuildStreamTableEntry (SmmuInfo, Index, &StreamTablePtr[Index]);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Error building stream table\n", __func__));
      goto End;
    }
  }

  Status = SmmuV3AllocateCommandQueue (SmmuInfo, &CommandQueueLog2Size, &CommandQueue);
  if (EFI_ERROR (Status) || (CommandQueue == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Error allocating SMMU Command Queue\n", __func__));
    goto End;
  }

  Status = SmmuV3AllocateEventQueue (SmmuInfo, &EventQueueLog2Size, &EventQueue);
  if (EFI_ERROR (Status) || (EventQueue == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Error allocating SMMU Event Queue\n", __func__));
    goto End;
  }

  SmmuInfo->CommandQueue         = CommandQueue;
  SmmuInfo->CommandQueueLog2Size = CommandQueueLog2Size;
  SmmuInfo->EventQueue           = EventQueue;
  SmmuInfo->EventQueueLog2Size   = EventQueueLog2Size;

  // Configure Stream Table Base
  StrTabBaseCfg.AsUINT32 = 0;
  StrTabBaseCfg.Fmt      = SMMUV3_STR_TAB_BASE_CFG_FMT_LINEAR; // Linear format
  StrTabBaseCfg.Log2Size = StreamTableLog2Size;

  SmmuV3WriteRegister32 (SmmuInfo->SmmuBase, SMMU_STRTAB_BASE_CFG, StrTabBaseCfg.AsUINT32);

  StrTabBase.AsUINT64 = 0;
  StrTabBase.Ra       = ReadWriteAllocationHint;
  StrTabBase.Addr     = ((UINT64)(UINTN)SmmuInfo->StreamTable) >> SMMUV3_STR_TAB_BASE_ADDR_OFFSET;
  SmmuV3WriteRegister64 (SmmuInfo->SmmuBase, SMMU_STRTAB_BASE, StrTabBase.AsUINT64);

  // Configure Command Queue Base
  CommandQueueBase.AsUINT64 = 0;
  CommandQueueBase.Log2Size = SmmuInfo->CommandQueueLog2Size;
  CommandQueueBase.Addr     = ((UINT64)(UINTN)SmmuInfo->CommandQueue) >> SMMUV3_STR_TAB_BASE_CMDQ_OFFSET;
  CommandQueueBase.Ra       = ReadWriteAllocationHint;
  SmmuV3WriteRegister64 (SmmuInfo->SmmuBase, SMMU_CMDQ_BASE, CommandQueueBase.AsUINT64);
  SmmuV3WriteRegister32 (SmmuInfo->SmmuBase, SMMU_CMDQ_PROD, 0);
  SmmuV3WriteRegister32 (SmmuInfo->SmmuBase, SMMU_CMDQ_CONS, 0);

  // Configure Event Queue Base
  EventQueueBase.AsUINT64 = 0;
  EventQueueBase.Log2Size = SmmuInfo->EventQueueLog2Size;
  EventQueueBase.Addr     = ((UINT64)(UINTN)SmmuInfo->EventQueue) >> SMMUV3_STR_TAB_BASE_EVENTQ_OFFSET;
  EventQueueBase.Wa       = ReadWriteAllocationHint;
  SmmuV3WriteRegister64 (SmmuInfo->SmmuBase, SMMU_EVENTQ_BASE, EventQueueBase.AsUINT64);
  SmmuV3WriteRegister32 (SmmuInfo->SmmuBase + SMMUV3_PAGE_1_OFFSET, SMMU_EVENTQ_PROD, 0);
  SmmuV3WriteRegister32 (SmmuInfo->SmmuBase + SMMUV3_PAGE_1_OFFSET, SMMU_EVENTQ_CONS, 0);

  // Enable GError and event interrupts
  Status = SmmuV3EnableInterrupts (SmmuInfo->SmmuBase);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Error enabling interrupts\n", __func__));
    goto End;
  }

  // Configure CR1
  Cr1.AsUINT32  = SmmuV3ReadRegister32 (SmmuInfo->SmmuBase, SMMU_CR1);
  Cr1.AsUINT32 &= ~SMMUV3_CR1_VALID_MASK;
  if ((SmmuInfo->Flags & EFI_ACPI_IORT_SMMUv3_FLAG_COHAC_OVERRIDE) != 0) {
    Cr1.QueueIc = ARM64_RGNCACHEATTR_WRITEBACK_WRITEALLOCATE; // WBC
    Cr1.QueueOc = ARM64_RGNCACHEATTR_WRITEBACK_WRITEALLOCATE; // WBC
    Cr1.QueueSh = ARM64_SHATTR_INNER_SHAREABLE;               // Inner-shareable
  }

  SmmuV3WriteRegister32 (SmmuInfo->SmmuBase, SMMU_CR1, Cr1.AsUINT32);

  // Configure CR2
  Cr2.AsUINT32  = SmmuV3ReadRegister32 (SmmuInfo->SmmuBase, SMMU_CR2);
  Cr2.AsUINT32 &= ~SMMUV3_CR2_VALID_MASK;
  Cr2.E2h       = SMMUV3_CR2_E2H;
  Cr2.RecInvSid = SMMUV3_CR2_REC_INV_SID;   // Record C_BAD_STREAMID for invalid input streams.

  //
  // If broadcast TLB maintenance (BTM) is not enabled, then configure
  // private TLB maintenance (PTM). Per SMMU spec (section 6.3.12), the PTM bit is
  // only valid when BTM is indicated as supported.
  //
  Idr0.AsUINT32 = SmmuV3ReadRegister32 (SmmuInfo->SmmuBase, SMMU_IDR0);
  if (Idr0.Btm == 1) {
    Cr2.Ptm = SMMUV3_CR2_PTM;     // Private TLB maintenance.
  }

  SmmuV3WriteRegister32 (SmmuInfo->SmmuBase, SMMU_CR2, Cr2.AsUINT32);

  // Configure CR0 part1
  ArmDataSynchronizationBarrier ();  // DSB

  Cr0.AsUINT32 = SmmuV3ReadRegister32 (SmmuInfo->SmmuBase, SMMU_CR0);
  Cr0.EventQEn = SMMUV3_CR0_EVENTQ_EN;
  Cr0.CmdQEn   = SMMUV3_CR0_CMDQ_EN;

  SmmuV3WriteRegister32 (SmmuInfo->SmmuBase, SMMU_CR0, Cr0.AsUINT32);
  Status = SmmuV3Poll (SmmuInfo->SmmuBase, SMMU_CR0ACK, 0xC, 0xC);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Error polling register: 0x%lx\n", __func__, SmmuInfo->SmmuBase + SMMU_CR0ACK));
    goto End;
  }

  //
  // Invalidate all cached configuration and TLB entries
  //
  SMMUV3_BUILD_CMD_CFGI_ALL (&Command);
  Status = SmmuV3SendCommand (SmmuInfo, &Command);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Error sending command.\n", __func__));
    goto End;
  }

  SMMUV3_BUILD_CMD_TLBI_NSNH_ALL (&Command);
  Status = SmmuV3SendCommand (SmmuInfo, &Command);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Error sending command.\n", __func__));
    goto End;
  }

  SMMUV3_BUILD_CMD_TLBI_EL2_ALL (&Command);
  Status = SmmuV3SendCommand (SmmuInfo, &Command);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Error sending command.\n", __func__));
    goto End;
  }

  // Issue a CMD_SYNC command to guarantee that any previously issued TLB
  // invalidations (CMD_TLBI_*) are completed (SMMUv3.2 spec section 4.6.3).
  SMMUV3_BUILD_CMD_SYNC_NO_INTERRUPT (&Command);
  Status = SmmuV3SendCommand (SmmuInfo, &Command);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Error sending command.\n", __func__));
    goto End;
  }

  // Configure CR0 part2
  Cr0.AsUINT32 = SmmuV3ReadRegister32 (SmmuInfo->SmmuBase, SMMU_CR0);
  ArmDataSynchronizationBarrier ();  // DSB

  Cr0.AsUINT32  = Cr0.AsUINT32 & ~SMMUV3_CR0_VALID_MASK;
  Cr0.SmmuEn    = SMMUV3_CR0_SMMU_EN;
  Cr0.EventQEn  = SMMUV3_CR0_EVENTQ_EN;
  Cr0.CmdQEn    = SMMUV3_CR0_CMDQ_EN;
  Cr0.PriQEn    = SMMUV3_CR0_PRIQ_EN_DISABLED;
  Cr0.Vmw       = SMMUV3_CR0_VMW_DISABLED; // Disable VMID wildcard matching.
  Idr0.AsUINT32 = SmmuV3ReadRegister32 (SmmuInfo->SmmuBase, SMMU_IDR0);
  if (Idr0.Ats != 0) {
    Cr0.AtsChk = SMMUV3_CR0_ATS_CHK_DISABLE;     // disable bypass for ATS translated traffic.
  }

  SmmuV3WriteRegister32 (SmmuInfo->SmmuBase, SMMU_CR0, Cr0.AsUINT32);
  Status = SmmuV3Poll (SmmuInfo->SmmuBase, SMMU_CR0ACK, SMMUV3_CR0_SMMU_EN_MASK, SMMUV3_CR0_SMMU_EN_MASK);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Error polling register: 0x%lx\n", __func__, SmmuInfo->SmmuBase + SMMU_CR0ACK));
    goto End;
  }

  ArmDataSynchronizationBarrier ();  // DSB

  GError.AsUINT32 = SmmuV3ReadRegister32 (SmmuInfo->SmmuBase, SMMU_GERROR);
  if (GError.AsUINT32 != 0) {
    DEBUG ((DEBUG_ERROR, "%a: Globar SMMU Error detected: 0x%lx\n", __func__, GError.AsUINT32));
    Status = EFI_DEVICE_ERROR;
  }

End:
  // Only logs errors if errors are found
  SmmuV3LogErrors (SmmuInfo);
  return Status;
}

/**
  Retrieve the SMMU configuration data from the HOB.

  @return Pointer to the SMMU_CONFIG structure, or NULL if not found.
**/
SMMU_CONFIG *
GetSmmuConfigHobData (
  VOID
  )
{
  VOID  *GuidHob;

  GuidHob = GetFirstGuidHob (&gSmmuConfigHobGuid);

  if (GuidHob != NULL) {
    return (SMMU_CONFIG *)GET_GUID_HOB_DATA (GuidHob);
  }

  return NULL;
}

/**
  Check if the SMMU_CONFIG structure is compatible with the current driver version.
  Backwards compatibility is currently not supported.

  @param [in] SmmuConfig  Pointer to the SMMU_CONFIG structure.

  @retval EFI_SUCCESS               Success.
  @retval EFI_INVALID_PARAMETER     Invalid parameter.
  @retval EFI_INCOMPATIBLE_VERSION  Incompatible version.
**/
EFI_STATUS
CheckSmmuConfigVersion (
  IN SMMU_CONFIG  *SmmuConfig
  )
{
  if (SmmuConfig == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: SMMU_CONFIG structure is NULL\n", __func__));
    return EFI_INVALID_PARAMETER;
  }

  if ((SmmuConfig->VersionMajor == CURRENT_SMMU_CONFIG_VERSION_MAJOR) && (SmmuConfig->VersionMinor == CURRENT_SMMU_CONFIG_VERSION_MINOR)) {
    return EFI_SUCCESS;
  }

  DEBUG ((
    DEBUG_ERROR,
    "%a: SMMU_CONFIG version mismatch. Expected: %u.%u Got: %u.%u\n",
    __func__,
    CURRENT_SMMU_CONFIG_VERSION_MAJOR,
    CURRENT_SMMU_CONFIG_VERSION_MINOR,
    SmmuConfig->VersionMajor,
    SmmuConfig->VersionMinor
    ));
  return EFI_INCOMPATIBLE_VERSION;
}

/**
  Initialize the IOMMU_CONFIG structure.


  @retval Pointer to the allocated IOMMU_CONFIG structure, or NULL on failure.
**/
EFI_STATUS
IoMmuConfigInit (
  OUT IOMMU_CONFIG  **IoMmu
  )
{
  *IoMmu = (IOMMU_CONFIG *)AllocateZeroPool (sizeof (IOMMU_CONFIG));
  if (*IoMmu == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate IOMMU_CONFIG structure\n", __func__));
    return EFI_OUT_OF_RESOURCES;
  }

  return EFI_SUCCESS;
}

/**
  Deinitialize and free the SMMU_INFO structure and everything inside.
  Also disables SMMU translation and sets global abort.

  @param [in]  Smmu    Pointer to the SMMU_INFO structure to deinitialize.
**/
STATIC
VOID
IoMmuDeInit (
  IN IOMMU_CONFIG  *IoMmu
  )
{
  EFI_STATUS  Status;
  UINT32      SmmuIndex;

  if (IoMmu == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: SMMU_INFO structure is NULL\n", __func__));
    return;
  }

  for (SmmuIndex = 0; SmmuIndex < IoMmu->SmmuCount; SmmuIndex++) {
    Status = SmmuV3DisableTranslation (IoMmu->SmmuInfo[SmmuIndex].SmmuBase);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to disable SMMUv3 translation 0x%llx\n", __func__, IoMmu->SmmuInfo[SmmuIndex].SmmuBase));
    }

    Status = SmmuV3GlobalAbort (IoMmu->SmmuInfo[SmmuIndex].SmmuBase);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to global abort SMMUv3 0x%llx\n", __func__, IoMmu->SmmuInfo[SmmuIndex].SmmuBase));
    }

    if (IoMmu->SmmuInfo[SmmuIndex].PageTableRoot != NULL) {
      PageTableDeInit (0, IoMmu->SmmuInfo[SmmuIndex].PageTableRoot);
      IoMmu->SmmuInfo->PageTableRoot = NULL;
    }

    if (IoMmu->SmmuInfo[SmmuIndex].StreamEntryConfig != NULL) {
      FreePool (IoMmu->SmmuInfo[SmmuIndex].StreamEntryConfig);
      IoMmu->SmmuInfo[SmmuIndex].StreamEntryConfig = NULL;
    }

    if (IoMmu->SmmuInfo[SmmuIndex].StreamTable != NULL) {
      SmmuV3FreeStreamTable (IoMmu->SmmuInfo[SmmuIndex].StreamTable, IoMmu->SmmuInfo[SmmuIndex].StreamTableSize);
      IoMmu->SmmuInfo[SmmuIndex].StreamTable = NULL;
    }

    if (IoMmu->SmmuInfo[SmmuIndex].CommandQueue != NULL) {
      SmmuV3FreeQueue (IoMmu->SmmuInfo[SmmuIndex].CommandQueue, IoMmu->SmmuInfo[SmmuIndex].CommandQueueLog2Size);
      IoMmu->SmmuInfo[SmmuIndex].CommandQueue = NULL;
    }

    if (IoMmu->SmmuInfo[SmmuIndex].EventQueue != NULL) {
      SmmuV3FreeQueue (IoMmu->SmmuInfo[SmmuIndex].EventQueue, IoMmu->SmmuInfo[SmmuIndex].EventQueueLog2Size);
      IoMmu->SmmuInfo[SmmuIndex].EventQueue = NULL;
    }
  }

  FreePool (IoMmu->SmmuInfo);
  FreePool (IoMmu);
}

/**
  Disable SMMU translation and set SMMU to global bypass during ExitBootServices.

  @param [in] Event    The event that triggered this notification function.
  @param [in] Context  Pointer to the notification function's context.
**/
STATIC
VOID
SmmuV3ExitBootServices (
  IN      EFI_EVENT  Event,
  IN      VOID       *Context
  )
{
  EFI_STATUS  Status;
  EFI_TPL     OldTpl;
  UINT32      SmmuIndex;

  if (Event == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Event\n", __func__));
    ASSERT (Event != NULL);
    return;
  }

  if ((mIoMmu == NULL) || (mIoMmu->SmmuInfo == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: IOMMU_CONFIG/SMMU_INFO structure is NULL\n", __func__));
    ASSERT (mIoMmu != NULL);
    ASSERT (mIoMmu->SmmuInfo != NULL);
    return;
  }

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);

  for (SmmuIndex = 0; SmmuIndex < mIoMmu->SmmuCount; SmmuIndex++) {
    Status = SmmuV3DisableTranslation (mIoMmu->SmmuInfo[SmmuIndex].SmmuBase);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to disable smmu translation.\n", __func__));
      ASSERT_EFI_ERROR (Status);
    }

    Status = SmmuV3SetGlobalBypass (mIoMmu->SmmuInfo[SmmuIndex].SmmuBase);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to set global bypass.\n", __func__));
      ASSERT_EFI_ERROR (Status);
    }
  }

  gBS->RestoreTPL (OldTpl);
  gBS->CloseEvent (Event);
}

/**
  Entrypoint for SmmuDxe driver.
  Configures IORT, and SMMUv3 hardware based on the configuration data from gSmmuConfigHobGuid HOB.
  Uses a linear stream table and stage 2 translation for dma remapping.
  Initializes IoMmu Protocol.

  @param [in] ImageHandle    The firmware allocated handle for the EFI image.
  @param [in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS               The entry point is executed successfully.
  @retval EFI_OUT_OF_RESOURCES      Not enough resources to initialize the driver.
  @retval EFI_NOT_FOUND             The SMMU configuration data is not found.
  @retval EFI_INVALID_PARAMETER     Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES      Out of resources.
  @retval EFI_TIMEOUT               Timeout.
  @retval EFI_DEVICE_ERROR          Device error.
  @retval EFI_INCOMPATIBLE_VERSION  Incompatible version.
  @retval Others                    Some error occurs when executing this entry point.
**/
EFI_STATUS
InitializeSmmuDxe (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS               Status;
  EFI_EVENT                Event;
  UINT32                   SmmuIndex;
  EFI_ACPI_TABLE_PROTOCOL  *AcpiTable;
  // SMMU_CONFIG              *SmmuConfig;
  PAGE_TABLE               *PageTableRoot;
  VOID                     *IortData;
  EFI_ACPI_COMMON_HEADER   *IortHeader;

  // Get SMMU configuration data from HOB
  // SmmuConfig = GetSmmuConfigHobData ();
  // if (SmmuConfig == NULL) {
  //   DEBUG ((DEBUG_ERROR, "%a: Failed to get SMMU config data from gSmmuConfigHobGuid\n", __func__));
  //   return EFI_NOT_FOUND;
  // }

  // Check SMMU_CONFIG version, return error if incompatible. Backwards compatibility not supported.
  // Status = CheckSmmuConfigVersion (SmmuConfig);
  // if (EFI_ERROR (Status)) {
  //   DEBUG ((DEBUG_ERROR, "%a: SMMU_CONFIG version check failed\n", __func__));
  //   return Status;
  // }

  // Check if ACPI Table Protocol has been installed
  Status = gBS->LocateProtocol (
                  &gEfiAcpiTableProtocolGuid,
                  NULL,
                  (VOID **)&AcpiTable
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to locate ACPI Table Protocol\n", __func__));
    return Status;
  }

  // Create an event callback to disable SMMUv3 translation and set global abort during ExitBootServices
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  SmmuV3ExitBootServices,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &Event
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to create ExitBootServices event\n", __func__));
    return Status;
  }

  Status = IoMmuConfigInit (&mIoMmu);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to initialize IoMmu Config\n", __func__));
    return Status;
  }

  IortHeader = NULL;
  IortHeader = EfiLocateNextAcpiTable (EFI_ACPI_6_0_IO_REMAPPING_TABLE_SIGNATURE, IortHeader);
  if (IortHeader == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to locate IORT table\n", __func__));
    return EFI_NOT_FOUND;
  }

  // Cast the void* to the IORT structure
  IortData = (EFI_ACPI_6_0_IO_REMAPPING_TABLE *)IortHeader;
  // IortData = (VOID *)((UINT8 *)SmmuConfig + SmmuConfig->IortOffset);

  Status = SmmuV3ParseIort (IortData, &mIoMmu->SmmuInfo, &mIoMmu->SmmuCount);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to parse IORT for SMMU\n", __func__));
    return EFI_NOT_FOUND;
  }

  DEBUG ((DEBUG_VERBOSE, "%a: Found %u SMMUs\n", __func__, mIoMmu->SmmuCount));

  // Add IORT Table
  // Status = AddIortTable (AcpiTable, IortData, SmmuConfig->IortSize);
  // if (EFI_ERROR (Status)) {
  //   DEBUG ((DEBUG_ERROR, "%a: Failed to add IORT table\n", __func__));
  //   goto Error;
  // }

  // Global Page Table until TODO: IoMmu Protocol V2 is implemented
  PageTableRoot = PageTableInit ();
  if (PageTableRoot == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to initialize Page Table\n", __func__));
    Status = EFI_OUT_OF_RESOURCES;
    goto Error;
  }

  // Configure SMMUv3 hardware
  for (SmmuIndex = 0; SmmuIndex < mIoMmu->SmmuCount; SmmuIndex++) {
    Status = SmmuV3Configure (&mIoMmu->SmmuInfo[SmmuIndex], PageTableRoot);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to configure SMMUv3 hardware\n", __func__));
      goto Error;
    }
  }

  // Initialize IoMmu Protocol
  Status = IoMmuInit ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to intall IoMmuProtocol\n", __func__));
    goto Error;
  }

  DEBUG ((DEBUG_INFO, "%a: Status = %llx\n", __func__, Status));

  return Status;

Error:
  IoMmuDeInit (mIoMmu);
  mIoMmu = NULL;
  return Status;
}

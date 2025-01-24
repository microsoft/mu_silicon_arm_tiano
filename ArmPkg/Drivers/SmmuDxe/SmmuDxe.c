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
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/IoMmu.h>
#include <Guid/SmmuConfig.h>
#include "IoMmu.h"
#include "SmmuV3.h"

// Global SMMU instance
SMMU_INFO  *mSmmu;

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
  @param [in]  SmmuConfig           Pointer to the SMMU configuration.

  @retval EFI_SUCCESS               Success.
  @retval EFI_OUT_OF_RESOURCES      Out of resources.
  @retval EFI_INVALID_PARAMETER     Invalid parameter.
**/
STATIC
EFI_STATUS
AddIortTable (
  IN EFI_ACPI_TABLE_PROTOCOL  *AcpiTable,
  IN SMMU_CONFIG              *SmmuConfig
  )
{
  EFI_STATUS            Status;
  UINTN                 TableHandle;
  UINT32                TableSize;
  EFI_PHYSICAL_ADDRESS  PageAddress;
  UINT8                 *New;

  if ((AcpiTable == NULL) || (SmmuConfig == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Parameters\n", __func__));
    return EFI_INVALID_PARAMETER;
  }

  // Calculate the new table size based on the number of nodes in SMMU_CONFIG struct
  TableSize = sizeof (SmmuConfig->Config.Iort) +
              sizeof (SmmuConfig->Config.ItsNode) +
              sizeof (SmmuConfig->Config.SmmuNode) +
              sizeof (SmmuConfig->Config.RcNode);

  Status = gBS->AllocatePages (
                  AllocateAnyPages,
                  EfiACPIReclaimMemory,
                  EFI_SIZE_TO_PAGES (TableSize),
                  &PageAddress
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate pages for IORT table\n", __func__));
    return EFI_OUT_OF_RESOURCES;
  }

  New = (UINT8 *)(UINTN)PageAddress;
  ZeroMem (New, TableSize);

  // Add the ACPI Description table header
  CopyMem (New, &SmmuConfig->Config.Iort, sizeof (SmmuConfig->Config.Iort));
  ((EFI_ACPI_DESCRIPTION_HEADER *)New)->Length = TableSize;
  New                                         += sizeof (SmmuConfig->Config.Iort);

  // ITS Node
  CopyMem (New, &SmmuConfig->Config.ItsNode, sizeof (SmmuConfig->Config.ItsNode));
  New += sizeof (SmmuConfig->Config.ItsNode);

  // SMMUv3 Node
  CopyMem (New, &SmmuConfig->Config.SmmuNode, sizeof (SmmuConfig->Config.SmmuNode));
  New += sizeof (SmmuConfig->Config.SmmuNode);

  // RC Node
  CopyMem (New, &SmmuConfig->Config.RcNode, sizeof (SmmuConfig->Config.RcNode));
  New += sizeof (SmmuConfig->Config.RcNode);

  Status = AcpiPlatformChecksum ((UINT8 *)(UINTN)PageAddress, TableSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to calculate checksum for IORT table\n", __func__));
    return Status;
  }

  Status = AcpiTable->InstallAcpiTable (
                        AcpiTable,
                        (EFI_ACPI_COMMON_HEADER *)(UINTN)PageAddress,
                        TableSize,
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

  @retval Pointer to the allocated event queue, or NULL on failure.
**/
STATIC
VOID *
SmmuV3AllocateEventQueue (
  IN SMMU_INFO  *SmmuInfo,
  OUT UINT32    *QueueLog2Size
  )
{
  UINT32       QueueSize;
  SMMUV3_IDR1  Idr1;

  if ((SmmuInfo == NULL) || (QueueLog2Size == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Parameters\n", __func__));
    return NULL;
  }

  Idr1.AsUINT32 = SmmuV3ReadRegister32 (SmmuInfo->SmmuBase, SMMU_IDR1);

  *QueueLog2Size = MIN (Idr1.EventQs, SMMUV3_EVENT_QUEUE_LOG2ENTRIES);
  QueueSize      = SMMUV3_EVENT_QUEUE_SIZE_FROM_LOG2 (*QueueLog2Size);
  return AllocateZeroPool (QueueSize);
}

/**
  Allocate a command queue for SMMUv3.

  @param [in]   SmmuInfo       Pointer to the SMMU_INFO structure.
  @param [out]  QueueLog2Size  Pointer to store the log2 size of the queue.

  @retval Pointer to the allocated command queue, or NULL on failure.
**/
STATIC
VOID *
SmmuV3AllocateCommandQueue (
  IN  SMMU_INFO  *SmmuInfo,
  OUT UINT32     *QueueLog2Size
  )
{
  UINT32       QueueSize;
  SMMUV3_IDR1  Idr1;

  if ((SmmuInfo == NULL) || (QueueLog2Size == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Parameters\n", __func__));
    return NULL;
  }

  Idr1.AsUINT32 = SmmuV3ReadRegister32 (SmmuInfo->SmmuBase, SMMU_IDR1);

  *QueueLog2Size = MIN (Idr1.CmdQs, SMMUV3_COMMAND_QUEUE_LOG2ENTRIES);
  QueueSize      = SMMUV3_COMMAND_QUEUE_SIZE_FROM_LOG2 (*QueueLog2Size);
  return AllocateZeroPool (QueueSize);
}

/**
  Free a previously allocated queue.

  @param [in]  QueuePtr    Pointer to the queue to free.
**/
STATIC
VOID
SmmuV3FreeQueue (
  IN VOID  *QueuePtr
  )
{
  if (QueuePtr == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid parameters. QueuePtr == NULL\n", __func__));
  } else {
    FreePool (QueuePtr);
  }
}

/**
  Build the stream table for SMMUv3.

  @param [in]  SmmuInfo       Pointer to the SMMU_INFO structure.
  @param [in]  SmmuConfig     Pointer to the SMMU configuration.
  @param [out] StreamEntry    Pointer to the stream table entry.

  @retval EFI_SUCCESS         Success.
  @retval EFI_INVALID_PARAMETER  Invalid parameter.
**/
STATIC
EFI_STATUS
SmmuV3BuildStreamTable (
  IN SMMU_INFO                   *SmmuInfo,
  IN SMMU_CONFIG                 *SmmuConfig,
  OUT SMMUV3_STREAM_TABLE_ENTRY  *StreamEntry
  )
{
  UINT32       OutputAddressWidth;
  UINT32       InputSize;
  SMMUV3_IDR0  Idr0;
  SMMUV3_IDR1  Idr1;
  SMMUV3_IDR5  Idr5;
  UINT8        IortCohac;
  UINT32       CCA;
  UINT8        CPM;
  UINT8        DACS;

  if ((SmmuInfo == NULL) || (SmmuConfig == NULL) || (StreamEntry == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Parameters\n", __func__));
    return EFI_INVALID_PARAMETER;
  }

  IortCohac = SmmuConfig->Config.SmmuNode.SmmuNode.Flags & EFI_ACPI_IORT_SMMUv3_FLAG_COHAC_OVERRIDE; // Cohac override flag
  CCA       = SmmuConfig->Config.RcNode.RcNode.CacheCoherent;                                        // Cache Coherent Attribute
  CPM       = SmmuConfig->Config.RcNode.RcNode.MemoryAccessFlags & SMMUV3_STREAM_TABLE_ENTRY_CPM;    // Coherent Path to Memory

  // Device attributes are Cacheable and Inner-Shareable
  DACS = (SmmuConfig->Config.RcNode.RcNode.MemoryAccessFlags & SMMUV3_STREAM_TABLE_ENTRY_DACS) >> 1;      // Shift by 1 to isolate DACS bit.

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

  StreamEntry->S2Sl0 = SMMUV3_STREAM_TABLE_ENTRY_S2SL0; // 0x2: Start at level 0

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
    StreamEntry->S2Ps = SmmuV3EncodeAddressWidth (SMMUV3_STREAM_TABLE_ENTRY_OUTPUT_ADDRESS_MAX);
  }

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

  return EFI_SUCCESS;
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
  @param [in]  SmmuConfig     Pointer to the SMMU configuration.
  @param [out] Log2Size       Pointer to store the log2 size of the stream table.
  @param [out] Size           Pointer to store the size of the stream table.

  @retval Pointer to the allocated stream table, or NULL on failure.
**/
STATIC
SMMUV3_STREAM_TABLE_ENTRY *
SmmuV3AllocateStreamTable (
  IN SMMU_INFO    *SmmuInfo,
  IN SMMU_CONFIG  *SmmuConfig,
  OUT UINT32      *Log2Size,
  OUT UINT32      *Size
  )
{
  UINT32  MaxStreamId;
  UINT32  SidMsb;
  UINT32  Alignment;
  UINTN   Pages;
  VOID    *AllocatedAddress;

  if ((SmmuInfo == NULL) || (SmmuConfig == NULL) || (Log2Size == NULL) || (Size == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Parameters\n", __func__));
    return NULL;
  }

  // The max stream id is calculated as the output base + the number of stream ids
  MaxStreamId      = SmmuConfig->Config.RcNode.RcIdMap.OutputBase + SmmuConfig->Config.RcNode.RcIdMap.NumIds;
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

  @param [in] SmmuInfo    Pointer to the SMMU_INFO structure.
  @param [in] SmmuConfig  Pointer to the SMMU configuration.

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
  IN SMMU_INFO    *SmmuInfo,
  IN SMMU_CONFIG  *SmmuConfig
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
  SMMUV3_STREAM_TABLE_ENTRY  TemplateStreamEntry;
  SMMUV3_CR0                 Cr0;
  SMMUV3_CR1                 Cr1;
  SMMUV3_CR2                 Cr2;
  SMMUV3_IDR0                Idr0;
  SMMUV3_CMD_GENERIC         Command;
  SMMUV3_GERROR              GError;
  VOID                       *CommandQueue;
  VOID                       *EventQueue;

  if ((SmmuInfo == NULL) || (SmmuConfig == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Parameters\n", __func__));
    return EFI_INVALID_PARAMETER;
  }

  // Set ReadWriteAllocationHint based on the COHAC_OVERRIDE flag.
  // These hints are applied to the allocated Stream Table, Command Queue, and Event Queue.
  if ((SmmuConfig->Config.SmmuNode.SmmuNode.Flags & EFI_ACPI_IORT_SMMUv3_FLAG_COHAC_OVERRIDE) != 0) {
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
  StreamTablePtr = SmmuV3AllocateStreamTable (SmmuInfo, SmmuConfig, &StreamTableLog2Size, &StreamTableSize);
  if (StreamTablePtr == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Error allocating stream table\n", __func__));
    Status = EFI_OUT_OF_RESOURCES;
    goto End;
  }

  SmmuInfo->StreamTable         = StreamTablePtr;
  SmmuInfo->StreamTableSize     = StreamTableSize;
  SmmuInfo->StreamTableLog2Size = StreamTableLog2Size;

  SmmuInfo->PageTableRoot = PageTableInit ();
  if (SmmuInfo->PageTableRoot == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Error initializing Page Table\n", __func__));
    Status = EFI_OUT_OF_RESOURCES;
    goto End;
  }

  // Build default STE template
  Status = SmmuV3BuildStreamTable (SmmuInfo, SmmuConfig, &TemplateStreamEntry);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Error building stream table\n", __func__));
    goto End;
  }

  // Load default STE values
  for (Index = 0; Index < SMMUV3_COUNT_FROM_LOG2 (StreamTableLog2Size); Index++) {
    CopyMem (&StreamTablePtr[Index], &TemplateStreamEntry, sizeof (SMMUV3_STREAM_TABLE_ENTRY));
  }

  CommandQueue = SmmuV3AllocateCommandQueue (SmmuInfo, &CommandQueueLog2Size);
  EventQueue   = SmmuV3AllocateEventQueue (SmmuInfo, &EventQueueLog2Size);
  if ((CommandQueue == NULL) || (EventQueue == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Error allocating SMMU Queues\n", __func__));
    Status = EFI_OUT_OF_RESOURCES;
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
  if ((SmmuConfig->Config.SmmuNode.SmmuNode.Flags & EFI_ACPI_IORT_SMMUv3_FLAG_COHAC_OVERRIDE) != 0) {
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
STATIC
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
STATIC
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
  Initialize the SMMU_INFO structure.

  @param [in] SmmuBase The base address of the SMMU.

  @retval Pointer to the allocated SMMU_INFO structure, or NULL on failure.
**/
STATIC
SMMU_INFO *
SmmuInit (
  IN UINT64  SmmuBase
  )
{
  SMMU_INFO  *SmmuInfo;

  if (SmmuBase == 0) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid SMMU base address\n", __func__));
    return NULL;
  }

  SmmuInfo           = (SMMU_INFO *)AllocateZeroPool (sizeof (SMMU_INFO));
  SmmuInfo->SmmuBase = SmmuBase;
  return SmmuInfo;
}

/**
  Deinitialize and free the SMMU_INFO structure and everything inside.
  Also disables SMMU translation and sets global abort.

  @param [in]  Smmu    Pointer to the SMMU_INFO structure to deinitialize.
**/
STATIC
VOID
SmmuDeInit (
  IN SMMU_INFO  *SmmuInfo
  )
{
  EFI_STATUS  Status;

  if (SmmuInfo == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: SMMU_INFO structure is NULL\n", __func__));
    return;
  }

  Status = SmmuV3DisableTranslation (SmmuInfo->SmmuBase);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to disable SMMUv3 translation\n", __func__));
  }

  Status = SmmuV3GlobalAbort (SmmuInfo->SmmuBase);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to global abort SMMUv3\n", __func__));
  }

  if (SmmuInfo->PageTableRoot != NULL) {
    PageTableDeInit (0, SmmuInfo->PageTableRoot);
    SmmuInfo->PageTableRoot = NULL;
  }

  if (SmmuInfo->StreamTable != NULL) {
    SmmuV3FreeStreamTable (SmmuInfo->StreamTable, SmmuInfo->StreamTableSize);
    SmmuInfo->StreamTable = NULL;
  }

  if (SmmuInfo->CommandQueue != NULL) {
    SmmuV3FreeQueue (SmmuInfo->CommandQueue);
    SmmuInfo->CommandQueue = NULL;
  }

  if (SmmuInfo->EventQueue != NULL) {
    SmmuV3FreeQueue (SmmuInfo->EventQueue);
    SmmuInfo->EventQueue = NULL;
  }

  FreePool (SmmuInfo);
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

  if (Event == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid Event\n", __func__));
    ASSERT (Event != NULL);
    return;
  }

  if (mSmmu == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: SMMU_INFO structure is NULL\n", __func__));
    ASSERT (mSmmu != NULL);
    return;
  }

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);

  Status = SmmuV3DisableTranslation (mSmmu->SmmuBase);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to disable smmu translation.\n", __func__));
    ASSERT_EFI_ERROR (Status);
  }

  Status = SmmuV3SetGlobalBypass (mSmmu->SmmuBase);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to set global bypass.\n", __func__));
    ASSERT_EFI_ERROR (Status);
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
  EFI_ACPI_TABLE_PROTOCOL  *AcpiTable;
  SMMU_CONFIG              *SmmuConfig;

  // Get SMMU configuration data from HOB
  SmmuConfig = GetSmmuConfigHobData ();
  if (SmmuConfig == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to get SMMU config data from gSmmuConfigHobGuid\n", __func__));
    return EFI_NOT_FOUND;
  }

  // Check SMMU_CONFIG version, return error if incompatible. Backwards compatibility not supported.
  Status = CheckSmmuConfigVersion (SmmuConfig);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: SMMU_CONFIG version check failed\n", __func__));
    return Status;
  }

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

  // Get SMMUv3 base address from Smmu Config HOB
  mSmmu = SmmuInit (SmmuConfig->Config.SmmuNode.SmmuNode.Base);
  if (mSmmu == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate SMMU_INFO structure\n", __func__));
    return EFI_OUT_OF_RESOURCES;
  }

  // Add IORT Table
  Status = AddIortTable (AcpiTable, SmmuConfig);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to add IORT table\n", __func__));
    goto Error;
  }

  // Configure SMMUv3 hardware
  Status = SmmuV3Configure (mSmmu, SmmuConfig);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to configure SMMUV3 hardware\n", __func__));
    goto Error;
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
  SmmuDeInit (mSmmu);
  mSmmu = NULL;
  return Status;
}

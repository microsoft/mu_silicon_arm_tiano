/** @file IoMmu.c

    This file contains functions for the IoMmu protocol for use with the SMMU driver.
    This driver provides a generic interface for mapping host memory to device memory.
    Maintains a 4-level (0-3) page table for mapping virtual addresses to physical addresses.
    The mapping is identity mapped.

    Copyright (c) Microsoft Corporation.
    SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Protocol/IoMmu.h>
#include "IoMmu.h"
#include "SmmuV3.h"

/**
  IOMMU Mapping structure used to store the mapping information.
  Used to pass between IoMmuMap, IoMmuUnmap and IoMmuSetAttribute.
**/
typedef struct IOMMU_MAP_INFO {
  UINTN     NumberOfBytes;
  UINT64    VirtualAddress;
  UINT64    PhysicalAddress;
} IOMMU_MAP_INFO;

/**
  Update the flags of a page table entry per Arm Architecture Reference Manual for A profile.
  <https://developer.arm.com/documentation/102105/ka-07>

  The bottom 12 bits of a PAGE_TABLE_ENTRY, such as R/W, Access Flags, Valid flags, can be set or cleared. Only allows clearing of R/W bits

  @param [in]  Table                  Pointer to the page table.
  @param [in]  SetReadWriteFlagsOnly  Boolean to indicate if only R/W flags should be set.
  @param [in]  Flags                  Flags such as Read/Write Flags to set or clear. Only allows clearing of R/W bits. 12 bits or less.
  @param [in]  Index                  Index of the entry to update. <= PAGE_TABLE_SIZE

  @retval EFI_SUCCESS            Success.
  @retval EFI_INVALID_PARAMETER  Invalid parameter.
**/
STATIC
EFI_STATUS
UpdateFlags (
  IN PAGE_TABLE  *Table,
  IN BOOLEAN     SetReadWriteFlagsOnly,
  IN UINT16      Flags,
  IN UINT32      Index
  )
{
  EFI_STATUS  Status;

  if ((Table == NULL) || ((Flags & ~PAGE_TABLE_BLOCK_OFFSET) != 0) || (Index >= PAGE_TABLE_SIZE)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid parameter.\n", __func__));
    Status = EFI_INVALID_PARAMETER;
    ASSERT_EFI_ERROR (Status);
    return Status;
  }

  Status = EFI_SUCCESS;

  // This boolean is used to explicity update the R/W bits in the page table entry.
  // Allows clearing the R/W bits without affecting the other bits in the entry.
  if (SetReadWriteFlagsOnly) {
    if (Flags != 0) {
      // Set R/W bits in page table entry
      Table->Entries[Index] |= Flags;
    } else {
      // Clear R/W bits in page table entry
      Table->Entries[Index] &= ~(PAGE_TABLE_READ_BIT | PAGE_TABLE_WRITE_BIT);
    }
  } else {
    // Set R/W bits in page table entry
    Table->Entries[Index] |= Flags;
  }

  return Status;
}

/**
  Update the mapping of a virtual address to a physical address in the page table.

  Iterates through the page table levels to find the leaf entry for the given virtual address and
  validates entries along the way as needed. The leaf entry is then updated with the physical address along
  with appropriate flags and valid bit set. The option SetFlagsOnly allows traversal of the page table while
  only updating the flags of the entry, allowing clearing of flag bits as well.

  @param [in]  Root                       Pointer to the root page table.
  @param [in]  VirtualAddress             Virtual address to map.
  @param [in]  PhysicalAddress            Physical address to map to.
  @param [in]  Flags                      Flags to set for the mapping. 12 bit or less.
  @param [in]  Valid                      Boolean to indicate if the entry is valid.
  @param [in]  SetReadWriteFlagsOnly      Boolean to indicate if only flags should be set.

  @retval EFI_SUCCESS            Success.
  @retval EFI_INVALID_PARAMETER  Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES   Out of resources.
**/
STATIC
EFI_STATUS
UpdateMapping (
  IN PAGE_TABLE  *Root,
  IN UINT64      VirtualAddress,
  IN UINT64      PhysicalAddress,
  IN UINT16      Flags,
  IN BOOLEAN     Valid,
  IN BOOLEAN     SetReadWriteFlagsOnly
  )
{
  EFI_STATUS  Status;
  UINT8       Level;
  UINT32      Index;
  PAGE_TABLE  *Current;

  // Flags must be 12 bits or less
  if ((Root == NULL) || ((Flags & ~PAGE_TABLE_BLOCK_OFFSET) != 0) || (PhysicalAddress == 0)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid parameter.\n", __func__));
    Status = EFI_INVALID_PARAMETER;
    goto Error;
  }

  Current = Root;

  // Traverse the page table to the leaf level
  for (Level = 0; Level < PAGE_TABLE_DEPTH - 1; Level++) {
    Index = PAGE_TABLE_INDEX (VirtualAddress, Level);

    if (Current->Entries[Index] == 0) {
      PAGE_TABLE  *NewPage = (PAGE_TABLE *)AllocatePages (1);
      if (NewPage == NULL) {
        DEBUG ((DEBUG_ERROR, "%a: Failed allocating page.\n", __func__));
        Status = EFI_OUT_OF_RESOURCES;
        goto Error;
      }

      ZeroMem ((VOID *)NewPage, EFI_PAGE_SIZE);

      Current->Entries[Index] = (PAGE_TABLE_ENTRY)(UINTN)NewPage;
    }

    if (!SetReadWriteFlagsOnly) {
      if (Valid) {
        Current->Entries[Index] |= PAGE_TABLE_ENTRY_VALID_BIT; // valid entry
      }
    }

    Status = UpdateFlags (Current, SetReadWriteFlagsOnly, Flags, Index);
    if (EFI_ERROR (Status)) {
      goto FlagError;
    }

    Current = (PAGE_TABLE *)((UINTN)Current->Entries[Index] & ~PAGE_TABLE_BLOCK_OFFSET);
  }

  // leaf level
  if (Current != 0) {
    Index = PAGE_TABLE_INDEX (VirtualAddress, Level);

    if (Valid && ((Current->Entries[Index] & PAGE_TABLE_ENTRY_VALID_BIT) != 0)) {
      DEBUG ((DEBUG_VERBOSE, "%a: Page already mapped. VirtualAddress = 0x%llx PhysicalAddress=0x%llx\n", __func__, VirtualAddress, PhysicalAddress));
    }

    if (!SetReadWriteFlagsOnly) {
      if (Valid) {
        Current->Entries[Index]  = (PhysicalAddress & ~PAGE_TABLE_BLOCK_OFFSET); // Assign PA
        Current->Entries[Index] |= PAGE_TABLE_ENTRY_VALID_BIT;                   // valid entry
      } else {
        Current->Entries[Index] &= ~PAGE_TABLE_ENTRY_VALID_BIT; // only invalidate leaf entry
      }
    }

    Status = UpdateFlags (Current, SetReadWriteFlagsOnly, Flags, Index);
    if (EFI_ERROR (Status)) {
      goto FlagError;
    }
  }

  return Status;

FlagError:
  DEBUG ((DEBUG_ERROR, "%a: Failed to update flags.\n", __func__));
Error:
  ASSERT_EFI_ERROR (Status);
  return Status;
}

/**
  Update the page table mapping with the given physical address and flags.

  @param [in]  Root                       Pointer to the root page table.
  @param [in]  PhysicalAddress            Physical address to map.
  @param [in]  Bytes                      Number of bytes to map.
  @param [in]  Flags                      Flags to set for the mapping. 12 bits or less.
  @param [in]  Valid                      Boolean to indicate if the entry is valid.
  @param [in]  SetReadWriteFlagsOnly      Boolean to indicate if only R/W flags should be set.

  @retval EFI_SUCCESS            Success.
  @retval EFI_INVALID_PARAMETER  Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES   Out of resources.
**/
STATIC
EFI_STATUS
UpdatePageTable (
  IN PAGE_TABLE  *Root,
  IN UINT64      PhysicalAddress,
  IN UINT64      Bytes,
  IN UINT16      Flags,
  IN BOOLEAN     Valid,
  IN BOOLEAN     SetReadWriteFlagsOnly
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  PhysicalAddressEnd;
  EFI_PHYSICAL_ADDRESS  CurPhysicalAddress;

  if ((Root == NULL) || ((Flags & ~PAGE_TABLE_BLOCK_OFFSET) != 0) || (PhysicalAddress == 0) || (Bytes == 0)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid parameter\n", __func__));
    Status = EFI_INVALID_PARAMETER;
    goto Error;
  }

  CurPhysicalAddress = PhysicalAddress;
  PhysicalAddressEnd = ALIGN_VALUE (PhysicalAddress + Bytes, EFI_PAGE_SIZE);

  while (CurPhysicalAddress < PhysicalAddressEnd) {
    Status = UpdateMapping (Root, CurPhysicalAddress, CurPhysicalAddress, Flags, Valid, SetReadWriteFlagsOnly);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Failed to update page table mapping\n", __func__));
      goto Error;
    }

    CurPhysicalAddress += EFI_PAGE_SIZE;
  }

  return Status;

Error:
  ASSERT_EFI_ERROR (Status);
  return Status;
}

/**
  Map a host address to a device address using the Page Table.
  Currently, this function only supports identity mapping.

  @param [in]      This            Pointer to the IOMMU protocol instance.
  @param [in]      Operation       The type of IOMMU operation.
  @param [in]      HostAddress     The host address to map.
  @param [in, out] NumberOfBytes   On input, the number of bytes to map. On output, the number of bytes mapped.
  @param [out]     DeviceAddress   The resulting device address.
  @param [out]     Mapping         A handle to the mapping.

  @retval EFI_SUCCESS              Success.
  @retval EFI_INVALID_PARAMETER    Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES     Out of resources.
**/
EFI_STATUS
EFIAPI
IoMmuMap (
  IN     EDKII_IOMMU_PROTOCOL   *This,
  IN     EDKII_IOMMU_OPERATION  Operation,
  IN     VOID                   *HostAddress,
  IN OUT UINTN                  *NumberOfBytes,
  OUT    EFI_PHYSICAL_ADDRESS   *DeviceAddress,
  OUT    VOID                   **Mapping
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  PhysicalAddress;
  IOMMU_MAP_INFO        *MapInfo;
  UINT16                Flags;

  if ((This == NULL) ||
      (HostAddress == NULL) ||
      (NumberOfBytes == NULL) ||
      (*NumberOfBytes == 0) ||
      (DeviceAddress == NULL) ||
      (Mapping == NULL))
  {
    DEBUG ((DEBUG_ERROR, "%a: Invalid parameter\n", __func__));
    Status = EFI_INVALID_PARAMETER;
    goto Error;
  }

  // Arm Architecture Reference Manual Armv8, for Armv8-A architecture profile:
  // The VMSAv8-64 translation table format descriptors.
  // Bit #10 AF = 1, Table/Page Descriptors for levels 0-3 so set bit #1 to 0b'1 for each entry
  Flags = PAGE_TABLE_ACCESS_FLAG | PAGE_TABLE_DESCRIPTOR;

  PhysicalAddress = (EFI_PHYSICAL_ADDRESS)(UINTN)HostAddress;
  Status          = UpdatePageTable (mSmmu->PageTableRoot, PhysicalAddress, *NumberOfBytes, Flags, TRUE, FALSE);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to update page table.\n", __func__));
    goto Error;
  }

  *DeviceAddress = PhysicalAddress; // Identity mapping

  // Allocate and fill the IOMMU_MAP_INFO structure with mapped information
  MapInfo                  = (IOMMU_MAP_INFO *)AllocateZeroPool (sizeof (IOMMU_MAP_INFO));
  MapInfo->NumberOfBytes   = *NumberOfBytes;
  MapInfo->VirtualAddress  = *DeviceAddress;
  MapInfo->PhysicalAddress = PhysicalAddress;
  *Mapping                 = MapInfo;

  // Only prints errors if Event Queue is not empty and GError != 0
  SmmuV3LogErrors (mSmmu);
  return Status;

Error:
  SmmuV3LogErrors (mSmmu);
  ASSERT_EFI_ERROR (Status);
  return Status;
}

/**
  Unmap a device address in the Page Table, invalidate the TLB with TLBI operation.

  @param [in]  This      Pointer to the IOMMU protocol instance.
  @param [in]  Mapping   The mapping to unmap.

  @retval EFI_SUCCESS            Success.
  @retval EFI_INVALID_PARAMETER  Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES   Out of resources.
  @retval EFI_TIMEOUT            Timeout.
**/
EFI_STATUS
EFIAPI
IoMmuUnmap (
  IN  EDKII_IOMMU_PROTOCOL  *This,
  IN  VOID                  *Mapping
  )
{
  EFI_STATUS          Status;
  SMMUV3_CMD_GENERIC  Command;
  IOMMU_MAP_INFO      *MapInfo;

  if ((This == NULL) || (Mapping == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid parameter\n", __func__));
    Status = EFI_INVALID_PARAMETER;
    goto Error;
  }

  MapInfo = (IOMMU_MAP_INFO *)Mapping;

  Status = UpdatePageTable (mSmmu->PageTableRoot, MapInfo->PhysicalAddress, MapInfo->NumberOfBytes, 0, FALSE, FALSE);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to update page table.\n", __func__));
    goto Error;
  }

  // Invalidate TLBI Command
  SMMUV3_BUILD_CMD_TLBI_NSNH_ALL (&Command);
  Status = SmmuV3SendCommand (mSmmu, &Command);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: CMD_TLBI_NSNH_ALL failed.\n", __func__));
    goto Error;
  }

  SMMUV3_BUILD_CMD_TLBI_EL2_ALL (&Command);
  Status = SmmuV3SendCommand (mSmmu, &Command);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: CMD_TLBI_EL2_ALL failed.\n", __func__));
    goto Error;
  }

  // Issue a CMD_SYNC command to guarantee that any previously issued TLB
  // invalidations (CMD_TLBI_*) are completed (SMMUv3.2 spec section 4.6.3).
  SMMUV3_BUILD_CMD_SYNC_NO_INTERRUPT (&Command);
  Status = SmmuV3SendCommand (mSmmu, &Command);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: CMD_SYNC_NO_INTERRUPT failed.\n", __func__));
    goto Error;
  }

  // Free the mapping structure allocated in IoMmuMap
  if (MapInfo != NULL) {
    FreePool (MapInfo);
  }

  // Only prints errors if Event Queue is not empty and GError != 0
  SmmuV3LogErrors (mSmmu);
  return Status;

Error:
  SmmuV3LogErrors (mSmmu);
  ASSERT_EFI_ERROR (Status);
  return Status;
}

/**
  Free a buffer allocated by IoMmuAllocateBuffer.

  @param [in]  This          Pointer to the IOMMU protocol instance.
  @param [in]  Pages         The number of pages to free.
  @param [in]  HostAddress   The host address to free.

  @retval EFI_SUCCESS            The requested pages were freed.
  @retval EFI_INVALID_PARAMETER  Memory is not a page-aligned address or Pages is invalid.
  @retval EFI_NOT_FOUND          The requested memory pages were not allocated with AllocatePages().
**/
EFI_STATUS
EFIAPI
IoMmuFreeBuffer (
  IN  EDKII_IOMMU_PROTOCOL  *This,
  IN  UINTN                 Pages,
  IN  VOID                  *HostAddress
  )
{
  EFI_STATUS  Status;

  if ((This == NULL) || (HostAddress == NULL) || (Pages == 0)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid parameter\n", __func__));
    Status = EFI_INVALID_PARAMETER;
    goto Error;
  }

  Status = gBS->FreePages ((EFI_PHYSICAL_ADDRESS)(UINTN)HostAddress, Pages);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to free pages\n", __func__));
    goto Error;
  }

  return Status;

Error:
  ASSERT_EFI_ERROR (Status);
  return Status;
}

/**
  Allocate a buffer for use with the IOMMU.

  @param [in]      This          Pointer to the IOMMU protocol instance.
  @param [in]      Type          The type of allocation to perform.
  @param [in]      MemoryType    The type of memory to allocate.
  @param [in]      Pages         The number of pages to allocate.
  @param [in, out] HostAddress   On input, the desired host address. On output, the allocated host address.
  @param [in]      Attributes    The memory attributes to use for the allocation.

  @retval EFI_SUCCESS           The requested pages were allocated.
  @retval EFI_INVALID_PARAMETER 1) Type is not AllocateAnyPages or
                                AllocateMaxAddress or AllocateAddress.
                                2) MemoryType is in the range
                                EfiMaxMemoryType..0x6FFFFFFF.
                                3) Memory is NULL.
                                4) MemoryType is EfiPersistentMemory.
  @retval EFI_OUT_OF_RESOURCES  The pages could not be allocated.
  @retval EFI_NOT_FOUND         The requested pages could not be found.
**/
EFI_STATUS
EFIAPI
IoMmuAllocateBuffer (
  IN     EDKII_IOMMU_PROTOCOL  *This,
  IN     EFI_ALLOCATE_TYPE     Type,
  IN     EFI_MEMORY_TYPE       MemoryType,
  IN     UINTN                 Pages,
  IN OUT VOID                  **HostAddress,
  IN     UINT64                Attributes
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  PhysicalAddress;

  if ((This == NULL) || (Pages == 0) || (HostAddress == NULL)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid parameter\n", __func__));
    Status = EFI_INVALID_PARAMETER;
    goto Error;
  }

  Status = gBS->AllocatePages (
                  Type,
                  MemoryType,
                  Pages,
                  &PhysicalAddress
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate pages\n", __func__));
    goto Error;
  }

  *HostAddress = (VOID *)(UINTN)PhysicalAddress;

  return Status;

Error:
  ASSERT_EFI_ERROR (Status);
  return Status;
}

/**
  Set the R/W access attributes for Mapping in the Page Table.

  @param [in]  This          Pointer to the IOMMU protocol instance.
  @param [in]  DeviceHandle  The device handle to set attributes for.
  @param [in]  Mapping       The mapping to set attributes for.
  @param [in]  IoMmuAccess   The IOMMU access attributes for R/W.

  @retval EFI_SUCCESS            Success.
  @retval EFI_INVALID_PARAMETER  Invalid parameter.
  @retval EFI_OUT_OF_RESOURCES   Out of resources.
**/
EFI_STATUS
EFIAPI
IoMmuSetAttribute (
  IN EDKII_IOMMU_PROTOCOL  *This,
  IN EFI_HANDLE            DeviceHandle,
  IN VOID                  *Mapping,
  IN UINT64                IoMmuAccess
  )
{
  EFI_STATUS      Status;
  IOMMU_MAP_INFO  *MapInfo;

  if ((This == NULL) || (Mapping == NULL) || ((IoMmuAccess & ~(EDKII_IOMMU_ACCESS_READ | EDKII_IOMMU_ACCESS_WRITE)) != 0)) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid parameter\n", __func__));
    Status = EFI_INVALID_PARAMETER;
    goto Error;
  }

  MapInfo = (IOMMU_MAP_INFO *)Mapping;

  Status = UpdatePageTable (
             mSmmu->PageTableRoot,
             MapInfo->PhysicalAddress,
             MapInfo->NumberOfBytes,
             PAGE_TABLE_READ_WRITE_FROM_IOMMU_ACCESS (IoMmuAccess),
             FALSE,
             TRUE
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to update page table.\n", __func__));
    goto Error;
  }

  // Only prints errors if Event Queue is not empty and GError != 0
  SmmuV3LogErrors (mSmmu);
  return Status;

Error:
  SmmuV3LogErrors (mSmmu);
  ASSERT_EFI_ERROR (Status);
  return Status;
}

// IOMMU Protocol instance for the SMMU.
EDKII_IOMMU_PROTOCOL  SmmuIoMmu = {
  EDKII_IOMMU_PROTOCOL_REVISION,
  IoMmuSetAttribute,
  IoMmuMap,
  IoMmuUnmap,
  IoMmuAllocateBuffer,
  IoMmuFreeBuffer,
};

/**
  Installs the IOMMU Protocol on this SMMU instance.

  @retval EFI_SUCCESS           All the protocol interface was installed.
  @retval EFI_OUT_OF_RESOURCES  There was not enough memory in pool to install all the protocols.
  @retval EFI_ALREADY_STARTED   A Device Path Protocol instance was passed in that is already present in
                                the handle database.
  @retval EFI_INVALID_PARAMETER Handle is NULL.
  @retval EFI_INVALID_PARAMETER Protocol is already installed on the handle specified by Handle.
**/
EFI_STATUS
IoMmuInit (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEdkiiIoMmuProtocolGuid,
                  &SmmuIoMmu,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to install gEdkiiIoMmuProtocolGuid\n", __func__));
  }

  return Status;
}

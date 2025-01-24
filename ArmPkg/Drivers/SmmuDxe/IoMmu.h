/** @file IoMmu.h

    This file is the IoMmu header file for SMMU driver.

    Copyright (c) Microsoft Corporation.
    SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef IOMMU_H_
#define IOMMU_H_

/**
  Page Table bit definitions used by the Smmu/IoMmu for mapping
  <https://developer.arm.com/documentation/102105/ka-07>
  Section D8.3.1 VMSAv8-64 descriptor formats
**/
#define PAGE_TABLE_DEPTH            4                           // Number of levels in the page table
#define PAGE_TABLE_READ_BIT         (0x1 << 6)
#define PAGE_TABLE_WRITE_BIT        (0x1 << 7)
#define PAGE_TABLE_ENTRY_VALID_BIT  0x1
#define PAGE_TABLE_BLOCK_OFFSET     0xFFF
#define PAGE_TABLE_ACCESS_FLAG      (0x1 << 10)
#define PAGE_TABLE_DESCRIPTOR       (0x1 << 1)
#define PAGE_TABLE_INDEX(VirtualAddress, Level)               (((VirtualAddress) >> (12 + (9 * (PAGE_TABLE_DEPTH - 1 - (Level))))) & 0x1FF)
#define PAGE_TABLE_READ_WRITE_FROM_IOMMU_ACCESS(IoMmuAccess)  (IoMmuAccess << 6)

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
  );

#endif

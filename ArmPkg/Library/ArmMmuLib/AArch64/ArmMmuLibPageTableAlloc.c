/** @file ArmMmuLibPageTableAlloc.c

  Logic facilitating the allocation of page table memory from a reserved pool.

  Copyright (c) Microsoft Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/ArmPageTableMemoryAllocation.h>

PAGE_TABLE_MEM_ALLOC_PROTOCOL  *PageTableMemAllocProtocol = NULL;

/**
  Allocates pages for the page table from a reserved pool.

  @param[in]  Pages  The number of pages to allocate

  @return A pointer to the allocated buffer or NULL if allocation fails
**/
static
VOID *
AllocatePageTableMemory (
  IN UINTN  Pages
  )
{
  if (PageTableMemAllocProtocol == NULL) {
    gBS->LocateProtocol (
           &gArmPageTableMemoryAllocationProtocolGuid,
           NULL,
           (VOID **)&PageTableMemAllocProtocol
           );
  }

  if (PageTableMemAllocProtocol != NULL) {
    return PageTableMemAllocProtocol->AllocatePageTableMem (Pages);
  }

  return AllocatePages (Pages);
}

/** @file ArmMmuPeiLibPageTableAlloc.c

Logic facilitating the pre-allocation of page table memory.

Copyright (C) Microsoft Corporation. All rights reserved.
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>

#include <Library/MemoryAllocationLib.h>

/**
  Initializes the reserved pool used to allocate pages for page table memory.

  @param[in] PoolPages The number of pages to initialize for the reserve pool. The actual number of pages
                       reserved will be at least 512 - the number of pages required to describe a
                       fully split 2MB region.

  @retval EFI_SUCCESS             Reserved pool initialized successfully
  @retval EFI_INVALID_PARAMETER   PoolPages is zero
  @retval EFI_OUT_OF_RESOURCES    Unable to allocate pages
**/
EFI_STATUS
InitializePageTablePool (
  IN  UINTN  PoolPages
  )
{
  return EFI_SUCCESS;
}

/**
  Allocates pages for the page table from a reserved pool.

  @param[in]  Pages  The number of pages to allocate

  @return A pointer to the allocated buffer or NULL if allocation fails

**/
VOID *
AllocatePageTableMemory (
  IN UINTN  Pages
  )
{
  return AllocatePages (Pages);
}

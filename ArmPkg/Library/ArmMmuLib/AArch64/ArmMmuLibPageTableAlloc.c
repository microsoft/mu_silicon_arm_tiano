/** @file ArmMmuLibPageTableAlloc.c

Logic facilitating the pre-allocation of page table memory.

Copyright (C) Microsoft Corporation. All rights reserved.
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/ArmMmuLib.h>

typedef struct {
  VOID     *NextPool;
  UINTN    Offset;
  UINTN    FreePages;
} PAGE_TABLE_POOL;

PAGE_TABLE_POOL  *mPageTablePool    = NULL;
BOOLEAN          mPageTablePoolLock = FALSE;

/**
  Initializes the reserved pool used to allocate pages for page table memory.

  @param[in] PoolPages The number of pages to initialize for the reserve pool. The actual number of pages
                       reserved will be at least 512 - the number of pages required to describe a
                       fully split 2MB region.

  @retval EFI_SUCCESS             Reserved pool initialized successfully
  @retval EFI_INVALID_PARAMETER   PoolPages is zero
  @retval EFI_ABORTED             Software lock is held by another call to this function
  @retval EFI_OUT_OF_RESOURCES    Unable to allocate pages
**/
EFI_STATUS
InitializePageTablePool (
  IN  UINTN  PoolPages
  )
{
  VOID        *Buffer;
  EFI_STATUS  Status = EFI_SUCCESS;

  if (PoolPages == 0) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Avoid an infinite loop by bailing if we are nested in another call
  // to this function
  //
  if (mPageTablePoolLock) {
    return EFI_ABORTED;
  }

  mPageTablePoolLock = TRUE;
  PoolPages++;
  PoolPages = ALIGN_VALUE (PoolPages, EFI_SIZE_TO_PAGES (SIZE_2MB)); // Add one page for the header
  Buffer    = AllocateAlignedPages (PoolPages, BASE_2MB);
  if (Buffer == NULL) {
    DEBUG ((DEBUG_ERROR, "ERROR: Out of aligned pages\r\n"));
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }

  if (mPageTablePool == NULL) {
    mPageTablePool           = Buffer;
    mPageTablePool->NextPool = mPageTablePool;
  } else {
    ((PAGE_TABLE_POOL *)Buffer)->NextPool = mPageTablePool->NextPool;
    mPageTablePool->NextPool              = Buffer;
    mPageTablePool                        = Buffer;
  }

  //
  // Reserve one page for pool header.
  //
  mPageTablePool->FreePages = PoolPages - 1;
  mPageTablePool->Offset    = EFI_PAGES_TO_SIZE (1);

  // Mark the region as XN
  ArmSetMemoryAttributes (
    (PHYSICAL_ADDRESS)(UINTN)Buffer,
    EFI_PAGES_TO_SIZE (PoolPages),
    EFI_MEMORY_XP,
    EFI_MEMORY_XP | EFI_MEMORY_RP | EFI_MEMORY_RO
    );

Done:
  mPageTablePoolLock = FALSE;
  return Status;
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
  VOID  *Buffer;

  if (Pages == 0) {
    return NULL;
  }

  //
  // Renew the pool if necessary.
  //
  if ((mPageTablePool == NULL) ||
      (Pages > mPageTablePool->FreePages))
  {
    if (EFI_ERROR (InitializePageTablePool (Pages))) {
      return NULL;
    }
  }

  Buffer                     = (UINT8 *)mPageTablePool + mPageTablePool->Offset;
  mPageTablePool->Offset    += EFI_PAGES_TO_SIZE (Pages);
  mPageTablePool->FreePages -= Pages;

  return Buffer;
}

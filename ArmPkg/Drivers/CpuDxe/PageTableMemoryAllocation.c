/** @file PageTableAlloc.c

Logic facilitating the pre-allocation of page table memory.

Copyright (C) Microsoft Corporation. All rights reserved.
SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/ArmPageTableMemoryAllocation.h>

#pragma pack(1)

typedef struct {
  VOID     *NextPool;
  UINTN    Offset;
  UINTN    FreePages;
} PAGE_TABLE_POOL;

#pragma pack()

#define MIN_PAGES_AVAILABLE  5

UINTN            TotalFreePages     = 0;
PAGE_TABLE_POOL  *mPageTablePool    = NULL;
BOOLEAN          mPageTablePoolLock = FALSE;

/**
  Allocates pages for page table memory and adds them to the pool list.

  @param[in] PoolPages The number of pages to allocate. The actual number of pages
                       reserved will be at least 512 - the number of pages required to describe a
                       fully split 2MB region.

  @retval EFI_SUCCESS             More pages successfully added to the pool list
  @retval EFI_INVALID_PARAMETER   PoolPages is zero
  @retval EFI_ABORTED             Software lock is held by another call to this function
  @retval EFI_OUT_OF_RESOURCES    Unable to allocate pages
**/
EFI_STATUS
GetMorePages (
  IN  UINTN  PoolPages
  )
{
  VOID        *Buffer;
  EFI_STATUS  Status = EFI_SUCCESS;

  if (PoolPages == 0) {
    return EFI_INVALID_PARAMETER;
  }

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
  mPageTablePool->Offset    = EFI_PAGE_SIZE;
  TotalFreePages           += mPageTablePool->FreePages;

Done:
  return Status;
}

/**
  Allocate memory for the page table.

  @param[in]  NumPages          Number of pages to allocate.

  @retval A pointer to the allocated page table memory or NULL if the
          allocation failed.
**/
VOID *
AllocatePageTableMemory (
  IN UINTN  Pages
  )
{
  VOID             *Buffer;
  PAGE_TABLE_POOL  *CurrentPool;

  if (Pages == 0) {
    return NULL;
  }

  // If we are running low on free pages, allocate more while we
  // still have enough pages to accommodate a potential split. Hold
  // a lock during this because the AllocatePages() call in GetMorePages()
  // may call this function recursively. In that recursive call, it should
  // pull from the existing pool rather than allocating more pages.
  if (  ((TotalFreePages < Pages) ||
         ((TotalFreePages - Pages) <= MIN_PAGES_AVAILABLE))
     && !mPageTablePoolLock)
  {
    mPageTablePoolLock = TRUE;
    GetMorePages (1);
    mPageTablePoolLock = FALSE;
  }

  CurrentPool = mPageTablePool;
  if (CurrentPool != NULL) {
    while (CurrentPool->NextPool != mPageTablePool) {
      if (Pages <= CurrentPool->FreePages) {
        break;
      }

      if (CurrentPool->NextPool == mPageTablePool) {
        break;
      }

      CurrentPool = CurrentPool->NextPool;
    }
  }

  if ((CurrentPool == NULL) ||
      (Pages > CurrentPool->FreePages))
  {
    DEBUG ((DEBUG_INFO, "%a - Allocating page table memory.\n", __func__));
    if (EFI_ERROR (GetMorePages (Pages))) {
      return NULL;
    }

    CurrentPool = mPageTablePool;
  }

  Buffer                  = (UINT8 *)CurrentPool + CurrentPool->Offset;
  CurrentPool->Offset    += EFI_PAGES_TO_SIZE (Pages);
  CurrentPool->FreePages -= Pages;
  TotalFreePages         -= Pages;

  return Buffer;
}

PAGE_TABLE_MEM_ALLOC_PROTOCOL  mPageTableMemAllocProtocol = {
  AllocatePageTableMemory
};

/**
  Initialize the page table memory pool and produce the page table memory allocation
  protocol.

  @param[in] ImageHandle  Handle on which to install the protocol.

  @retval EFI_SUCCESS           The page table pool was initialized and protocol produced.
  @retval Others                The driver returned an error while initializing.
**/
EFI_STATUS
EFIAPI
InitializePageTableMemory (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS  Status;

  Status = GetMorePages (1);

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "ERROR: Failed to allocate initial page table pool\n"));
    return Status;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ImageHandle,
                  &gArmPageTableMemoryAllocationProtocolGuid,
                  &mPageTableMemAllocProtocol,
                  NULL
                  );

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "ERROR: Failed to install ARM page table memory allocation protocol!\n"));
    FreePages (mPageTablePool, EFI_SIZE_TO_PAGES (mPageTablePool->FreePages) + 1);
  }

  return Status;
}

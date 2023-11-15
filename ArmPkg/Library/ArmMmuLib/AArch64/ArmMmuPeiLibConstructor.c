/** @file

  Copyright (c) 2016, Linaro Limited. All rights reserved.
  Copyright (c) 2021, Arm Limited. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent
*/

#include <Base.h>

#include <Library/ArmLib.h>
#include <Library/ArmMmuLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include "ArmMmuLibInternal.h"  // MU_CHANGE: Add function pointer type

EFI_STATUS
EFIAPI
ArmMmuPeiLibConstructor (
  IN       EFI_PEI_FILE_HANDLE  FileHandle,
  IN CONST EFI_PEI_SERVICES     **PeiServices
  )
{
  // MU_CHANGE [BEGIN]: Add function pointer type
  extern UINT32                       ArmReplaceLiveTranslationEntrySize;
  ARM_REPLACE_LIVE_TRANSLATION_ENTRY  ArmReplaceLiveTranslationEntryFunc;
  VOID                                *Hob;
  // MU_CHANGE [END]: Add function pointer type

  EFI_FV_FILE_INFO  FileInfo;
  EFI_STATUS        Status;

  // MU_CHANGE [BEGIN]: Add basic function parameter validation
  if ((FileHandle == NULL) || (PeiServices == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  // MU_CHANGE [END]: Add basic function parameter validation

  Status = (*PeiServices)->FfsGetFileInfo (FileHandle, &FileInfo);
  ASSERT_EFI_ERROR (Status);

  //
  // Some platforms do not cope very well with cache maintenance being
  // performed on regions backed by NOR flash. Since the firmware image
  // can be assumed to be clean to the PoC when running XIP, even when PEI
  // is executing from DRAM, we only need to perform the cache maintenance
  // when not executing in place.
  //
  if (((UINTN)FileInfo.Buffer <= (UINTN)ArmReplaceLiveTranslationEntry) &&
      ((UINTN)FileInfo.Buffer + FileInfo.BufferSize >=
       (UINTN)ArmReplaceLiveTranslationEntry + ArmReplaceLiveTranslationEntrySize))
  {
    DEBUG ((DEBUG_INFO, "ArmMmuLib: skipping cache maintenance on XIP PEIM\n"));

    //
    // Expose the XIP version of the ArmReplaceLiveTranslationEntry() routine
    // via a HOB so we can fall back to it later when we need to split block
    // mappings in a way that adheres to break-before-make requirements.
    //
    ArmReplaceLiveTranslationEntryFunc = ArmReplaceLiveTranslationEntry;

    Hob = BuildGuidDataHob (
            &gArmMmuReplaceLiveTranslationEntryFuncGuid,
            &ArmReplaceLiveTranslationEntryFunc,
            sizeof ArmReplaceLiveTranslationEntryFunc
            );
    ASSERT (Hob != NULL);
  } else {
    DEBUG ((DEBUG_INFO, "ArmMmuLib: performing cache maintenance on shadowed PEIM\n"));
    //
    // The ArmReplaceLiveTranslationEntry () helper function may be invoked
    // with the MMU off so we have to ensure that it gets cleaned to the PoC
    //
    WriteBackDataCacheRange (
      (VOID *)(UINTN)ArmReplaceLiveTranslationEntry,
      ArmReplaceLiveTranslationEntrySize
      );
  }

  return RETURN_SUCCESS;
}

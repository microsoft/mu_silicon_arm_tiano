/** @file

  Copyright (c) 2011, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/ArmPlatformLib.h>
#include <Library/DebugLib.h>

/**
  Return the Virtual Memory Map of your platform

  This Virtual Memory Map is used by MemoryInitPei Module to initialize the MMU on your platform.

  @param[out]   VirtualMemoryMap    Array of ARM_MEMORY_REGION_DESCRIPTOR describing a Physical-to-
                                    Virtual Memory mapping. This array must be ended by a zero-filled
                                    entry

**/
VOID
ArmPlatformGetVirtualMemoryMap (
  IN ARM_MEMORY_REGION_DESCRIPTOR  **VirtualMemoryMap
  )
{
  ASSERT (0);
}

// MU_CHANGE START

/**
  Checks if the platform requires a special initial EFI memory region.

  @param[out]  EfiMemoryBase  The custom memory base, will be unchanged if FALSE is returned.
  @param[out]  EfiMemorySize  The custom memory size, will be unchanged if FALSE is returned.

  @retval   TRUE    A custom memory region was set.
  @retval   FALSE   A custom memory region was not set.
**/
BOOLEAN
EFIAPI
ArmPlatformGetPeiMemory (
  OUT UINTN   *EfiMemoryBase,
  OUT UINT32  *EfiMemorySize
  )
{
  return FALSE;
}

// MU_CHANGE END

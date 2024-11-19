/** @file
  Platform layer for the secure partition interrupt handler using FF-A.

  Copyright (c), Microsoft Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Base.h>

#include <Library/DebugLib.h>
#include <Library/PlatformFfaInterruptLib.h>

/**
  Secure Partition interrupt handler.

  @param  InterruptId  The interrupt ID.

**/
VOID
EFIAPI
SecurePartitionInterruptHandler (
  UINT32  InterruptId
  )
{
  DEBUG ((DEBUG_INFO, "%a Received interrupt ID 0x%x\n", __func__, InterruptId));
}

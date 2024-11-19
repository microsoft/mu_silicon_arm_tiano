/** @file
  Platform layer for the secure partition interrupt handler using FF-A.

  Copyright (c), Microsoft Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef PLATFORM_FF_A_INTERRUPT_LIB_H_
#define PLATFORM_FF_A_INTERRUPT_LIB_H_

/**
  Secure Partition interrupt handler.

  @param  InterruptId  The interrupt ID.

**/
VOID
EFIAPI
SecurePartitionInterruptHandler (
  UINT32  InterruptId
  );

#endif /* PLATFORM_FF_A_INTERRUPT_LIB_H_ */

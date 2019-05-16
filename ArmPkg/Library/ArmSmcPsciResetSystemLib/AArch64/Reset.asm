;/** @file
;  ResetSystemLib implementation using PSCI calls
;
;  Copyright (c) 2018, Linaro Ltd. All rights reserved.<BR>
;
;  SPDX-License-Identifier: BSD-2-Clause-Patent
;
;**/

  AREA Reset, CODE, READONLY

  EXPORT DisableMmuAndReenterPei
  IMPORT ArmDisableMmu

DisableMmuAndReenterPei
  stp   x29, x30, [sp, #-16]!
  mov   x29, sp

  bl    ArmDisableMmu

  ; no memory accesses after MMU and caches have been disabled
  ; MU_CHANGE : The Microsoft assembler does not support the movl pseudo-instruction.
  ; No platforms using the Microsoft toolchain support S3 and Capsule update anyways,
  ; so comment it out to still use this library.
  ;
  ; NOTE: This obviously means the function that uses this (EnterS3WithImmediateWake) WILL NOT WORK.
  ;
  ; movl  x0, FixedPcdGet64 (PcdFvBaseAddress)
  blr   x0

  ; never returns
  nop

  END

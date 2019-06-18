;/** @file
;  ResetSystemLib implementation using PSCI calls
;
;  Copyright (c) 2018, Linaro Ltd. All rights reserved.<BR>
;
;  This program and the accompanying materials
;  are licensed and made available under the terms and conditions of the BSD License
;  which accompanies this distribution. The full text of the license may be found at
;  http://opensource.org/licenses/bsd-license.php
;
;  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
;  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
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

//
//  Copyright (c) 2021, ARM Limited. All rights reserved.
//
//  SPDX-License-Identifier: BSD-2-Clause-Patent
//
//

#include <AsmMacroIoLibV8.h>

  .align 12
StackBase:
  .space 8192
StackEnd:

  .macro FfaMemPermSet start:req end:req perm:req
  adrp x29, \start
  add x29, x29, :lo12: \start

  adrp x30, \end
  add x30, x30, :lo12:\end

  /* x30 = end - begin */
  sub x30, x30, x29
  /* x28 = x30 >> 12 (number of pages) */
  mov x28, #12
  lsrv x28, x30, x28

  mov w0, #0x89
  movk w0, #0x8400, lsl #16
  mov x1, x29
  mov x2, x28
  mov w3, #\perm

  svc #0

  mov w1, #0x61
  movk w1, #0x8400, lsl #16
  cmp w1, w0
  b.ne .
  .endm

ASM_FUNC(_ModuleEntryPoint)
  // Stash boot information registers from the SPMC
  mov	x8, x0
  mov	x9, x1
  mov	x10, x2
  mov	x11, x3

  // Set the correct permissions on stack memory
  FfaMemPermSet StackBase StackEnd 0x5

  // Initialise SP
  adr	x0, StackEnd
  mov   sp, x0

  // Restore boot information registers from the SPMC
  mov	x0, x8
  mov	x1, x9
  mov	x2, x10
  mov	x3, x11

  // Invoke the C entrypoint
  b     ModuleEntryPoint

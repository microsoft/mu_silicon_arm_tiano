/** @file

This is a C based implementation of ArmGetPhysicalAddressBits that uses the
already existing ArmReadIdMmfr0 with the Microsoft toolchain.

Copyright (c) Microsoft Corporation.
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/ArmLib.h>

UINT8  // MU_CHANGE
EFIAPI
ArmGetPhysicalAddressBits (
  VOID
  )
{
  // Read system register ID_AA64MMFR0_EL1, valid range is bits[3:0]
  // Valid values for ARMv8.1 PARange are 0 thru 6 which map to the corresponding address widths.
  static UINT8  aw[7]                = { 32, 36, 40, 42, 44, 48, 52 };
  UINTN         regValue             = ArmReadIdMmfr0 ();
  UINT8         physicalAddressWidth = 0;

  // It is highly unlikely for the register to have an invalid value.
  // Regardless, match the assembly routine and return 0.
  if ((regValue & 0xF) < 7) {
    physicalAddressWidth = aw[regValue & 0xF];
  }

  return physicalAddressWidth;  // MU_CHANGE
}

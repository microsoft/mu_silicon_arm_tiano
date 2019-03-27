/** @file

This is a C based implementation of ArmGetPhysicalAddressBits that uses the
already existing ArmReadIdMmfr0 with the Microsoft toolchain.

Copyright (c) 2019, Microsoft Corporation.

All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

**/

#include <Uefi.h>
#include <Library/ArmLib.h>

UINTN
EFIAPI
ArmGetPhysicalAddressBits (
  VOID
  )
{
    // Read system register ID_AA64MMFR0_EL1, valid range is bits[3:0]
    // Valid values for ARMv8.1 PARange are 0 thru 6 which map to the corresponding address widths.
    static UINT8 aw[7] = { 32, 36, 40, 42, 44, 48, 52 };
    UINTN regValue = ArmReadIdMmfr0();
    UINT8 physicalAddressWidth = 0;

    // It is highly unlikely for the register to have an invalid value.
    // Regardless, match the assembly routine and return 0.
    if ((regValue & 0xF) < 7)
    {
        physicalAddressWidth = aw[regValue & 0xF];
    }

    return (UINTN) physicalAddressWidth;
}
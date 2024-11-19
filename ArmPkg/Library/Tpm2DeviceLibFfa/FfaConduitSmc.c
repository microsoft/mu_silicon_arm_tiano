/** @file
  This library provides an implementation of Tpm2DeviceLib
  using ARM64 SMC calls to request TPM service.

  The implementation is only supporting the Command Response Buffer (CRB)
  for sharing data with the TPM.

Copyright (c) 2021, Microsoft Corporation. All rights reserved. <BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <Uefi.h>
#include <IndustryStandard/ArmFfaSvc.h>

#include <Library/ArmSmcLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/Tpm2DeviceLib.h>

#include "Tpm2DeviceLibFfa.h"

EFI_STATUS
ArmCallFfaConduit (
  IN OUT FFA_CONDUIT_ARGS  *Args
  )
{
  ARM_SMC_ARGS  SmcArgs;

  if (Args == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (&SmcArgs, sizeof (ARM_SMC_ARGS));
  CopyMem (&SmcArgs, Args, sizeof (FFA_CONDUIT_ARGS));

  ArmCallSmc (&SmcArgs);

  if (SmcArgs.Arg0 == ARM_FID_FFA_ERROR) {
    return TranslateFfaReturnStatus (SmcArgs.Arg2);
  }

  CopyMem (Args, &SmcArgs, sizeof (FFA_CONDUIT_ARGS));

  return EFI_SUCCESS;
}

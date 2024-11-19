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

#include <IndustryStandard/ArmFfaSvc.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/Tpm2DeviceLib.h>
#include <IndustryStandard/ArmStdSmc.h>
#include <IndustryStandard/Tpm20.h>
#include <Library/TimerLib.h>

#include "Tpm2DeviceLibFfa.h"

TPM2_PTP_INTERFACE_TYPE  mActiveTpmInterfaceType;
UINT8                    mCRBIdleByPass;

/**
  Return cached PTP CRB interface IdleByPass state.

  @return Cached PTP CRB interface IdleByPass state.
**/
UINT8
GetCachedIdleByPass (
  VOID
  )
{
  return mCRBIdleByPass;
}

/**
  Send a command to TPM for execution and return response data.
  Used during boot only.

  @retval EFI_SUCCESS     Command was successfully sent to the TPM
                          and the response was copied to the Output buffer.
  @retval Other           Some error occurred in communication with the TPM.
**/
EFI_STATUS
EFIAPI
Tpm2SubmitCommand (
  IN UINT32      InputParameterBlockSize,
  IN UINT8       *InputParameterBlock,
  IN OUT UINT32  *OutputParameterBlockSize,
  IN UINT8       *OutputParameterBlock
  )
{
  return FfaTpm2SubmitCommand (
           InputParameterBlockSize,
           InputParameterBlock,
           OutputParameterBlockSize,
           OutputParameterBlock
           );
}

/**
  This service requests use TPM2.
  Since every communication with the TPM is blocking
  you are always good to start communicating with the TPM.

  @retval EFI_SUCCESS      Get the control of TPM2 chip.
**/
EFI_STATUS
EFIAPI
Tpm2RequestUseTpm (
  VOID
  )
{
  return FfaTpm2RequestUseTpm ();
}

/**
  This service register TPM2 device.

  @param Tpm2Device  TPM2 device

  @retval EFI_UNSUPPORTED      System does not support register this TPM2 device.
**/
EFI_STATUS
EFIAPI
Tpm2RegisterTpm2DeviceLib (
  IN TPM2_DEVICE_INTERFACE  *Tpm2Device
  )
{
  return EFI_UNSUPPORTED;
}

/**
  Check the return status from the FF-A call and returns EFI_STATUS

  @param EFI_LOAD_ERROR  FF-A status code returned in x0

  @retval EFI_SUCCESS    The entry point is executed successfully.
**/
EFI_STATUS
EFIAPI
TranslateFfaReturnStatus (
  UINTN  FfaReturnStatus
  )
{
  EFI_STATUS  Status;

  switch (FfaReturnStatus) {
    case ARM_FFA_RET_SUCCESS:
      Status = EFI_SUCCESS;
      break;

    case ARM_FFA_RET_NOT_SUPPORTED:
      Status = EFI_UNSUPPORTED;
      break;

    case ARM_FFA_RET_INVALID_PARAMETERS:
      Status = EFI_INVALID_PARAMETER;
      break;

    case ARM_FFA_RET_NO_MEMORY:
      Status = EFI_BUFFER_TOO_SMALL;
      break;

    case ARM_FFA_RET_BUSY:
      Status = EFI_WRITE_PROTECTED;
      break;

    case ARM_FFA_RET_INTERRUPTED:
      Status = EFI_MEDIA_CHANGED;
      break;

    case ARM_FFA_RET_DENIED:
      Status = EFI_ACCESS_DENIED;
      break;

    case ARM_FFA_RET_RETRY:
      Status = EFI_LOAD_ERROR;
      break;

    case ARM_FFA_RET_ABORTED:
      Status = EFI_ABORTED;
      break;

    case ARM_FFA_RET_NODATA:
      Status = EFI_NOT_FOUND;
      break;

    case ARM_FFA_RET_NOT_READY:
      Status = EFI_NOT_READY;
      break;

    default:
      Status = EFI_DEVICE_ERROR;
  }

  return Status;
}

/**
  Check that we have an address for the CRB

  @retval EFI_SUCCESS    The entry point is executed successfully.
**/
EFI_STATUS
EFIAPI
Tpm2DeviceLibFfaConstructor (
  VOID
  )
{
  EFI_STATUS  Status;

  mActiveTpmInterfaceType = 0xFF;
  mCRBIdleByPass          = 0xFF;

  // Check to see if the FF-A is actually supported.
  Status = VerifyFfaVersion ();
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (PcdGet64 (PcdTpmBaseAddress) == 0) {
    Status = EFI_NOT_STARTED;
    goto Exit;
  }

  //
  // Always cache current active TpmInterfaceType for StandaloneMm implementation
  //
  mActiveTpmInterfaceType = Tpm2GetPtpInterface ((VOID *)(UINTN)PcdGet64 (PcdTpmBaseAddress));
  if (mActiveTpmInterfaceType != Tpm2PtpInterfaceCrb) {
    Status = EFI_UNSUPPORTED;
    goto Exit;
  }

  mCRBIdleByPass = Tpm2GetIdleByPass ((VOID *)(UINTN)PcdGet64 (PcdTpmBaseAddress));

  Status = EFI_SUCCESS;

Exit:
  return Status;
}

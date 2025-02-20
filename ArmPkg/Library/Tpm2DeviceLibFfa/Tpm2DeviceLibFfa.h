/** @file
  Provides function interfaces to communicate with TPM 2.0 device

  This library helps to use TPM 2.0 device in library function API
  based on SMC using Command Response Buffer (CRB).

  The TPM2DeviceLib library is sitting at the bottom of the TPM stack in UEFI.
  It is responsible for sending and receiving commands to and from the TPM.

  This TPM library performs the following actions:

  1) Receives a TPM command from the upper TPM layers.
  2) Moves the TPM command into the Command/Response Buffer (CRB).
    a) The address of the CRB is received through:
        gEfiSecurityPkgTokenSpaceGuid.PcdTpmBaseAddress
    b) The interface to the CRB is described in:
        https://trustedcomputinggroup.org/wp-content/uploads/Mobile-Command-Response-Buffer-Interface-v2-r12-Specification_FINAL2.pdf
  3) Set the CRB start bit to indicate that a TPM command is sitting in the CRB.
  4) Execute an SMC instruction to transfer execution to the Secure Monitor in EL3.
  4) The CRB/TPM command moves through the following components:
      NS -> SP -> TPM
  5) After returning from the SMC instruction the TPM command has been processed.
  6) Check status codes etc.
  7) Read the response length from the CRB and copy the response into the output buffer delivered with the TPM command.
  8) Return back to the upper layers of the TPM stack.

  This module is only to be used during boot. This will not persist after exit boot services is called.

 Copyright (c) 2021, Microsoft Corporation. All rights reserved.<BR>
 This program and the accompanying materials
 are licensed and made available under the terms and conditions of the BSD License
 which accompanies this distribution. The full text of the license may be found at
 http://opensource.org/licenses/bsd-license.php
 THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
 WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef _TPM2_DEVICE_LIB_SMC_H_
#define _TPM2_DEVICE_LIB_SMC_H_

#include <Library/ArmSmcLib.h>
#include <Library/ArmSvcLib.h>

/**
  Check the return status from the FF-A call and returns EFI_STATUS

  @param EFI_LOAD_ERROR  FF-A status code returned in x0

  @retval EFI_SUCCESS    The entry point is executed successfully.
**/
EFI_STATUS
EFIAPI
TranslateFfaReturnStatus (
  UINTN  FfaReturnStatus
  );

/*
  This function is used to get the TPM interface version.

  @param[out] Version - Supplies the pointer to the TPM interface version.

  @retval EFI_SUCCESS           The TPM command was successfully sent to the TPM
                                and the response was copied to the Output buffer.
  @retval EFI_INVALID_PARAMETER The TPM command buffer is NULL or the TPM command
                                buffer size is 0.
  @retval EFI_DEVICE_ERROR      An error occurred in communication with the TPM.
*/
EFI_STATUS
Tpm2GetInterfaceVersion (
  OUT UINT32  *Version
  );

/*
  This function is used to get the TPM feature information.

  @param[out] FeatureInfo - Supplies the pointer to the feature information.

  @retval EFI_SUCCESS           The TPM command was successfully sent to the TPM
                                and the response was copied to the Output buffer.
  @retval EFI_INVALID_PARAMETER The TPM command buffer is NULL or the TPM command
                                buffer size is 0.
  @retval EFI_DEVICE_ERROR      An error occurred in communication with the TPM.
*/
EFI_STATUS
Tpm2GetFeatureInfo (
  OUT UINT32  *FeatureInfo
  );

EFI_STATUS
Tpm2ServiceStart (
  IN UINT64  FuncQualifier,
  IN UINT64  LocalityQualifier
  );

EFI_STATUS
Tpm2RegisterNotification (
  IN BOOLEAN  NotificationTypeQualifier,
  IN UINT16   vCpuId,
  IN UINT64   NotificationId
  );

EFI_STATUS
Tpm2UnregisterNotification (
  VOID
  );

EFI_STATUS
Tpm2FinishNotified (
  VOID
  );

/**
  Return cached PTP CRB interface IdleByPass state.

  @return Cached PTP CRB interface IdleByPass state.
**/
UINT8
GetCachedIdleByPass (
  VOID
  );

/**
  Return PTP interface type.

  @param[in] Register                Pointer to PTP register.

  @return PTP interface type.
**/
TPM2_PTP_INTERFACE_TYPE
Tpm2GetPtpInterface (
  IN VOID  *Register
  );

/**
  Return PTP CRB interface IdleByPass state.

  @param[in] Register                Pointer to PTP register.

  @return PTP CRB interface IdleByPass state.
**/
UINT8
Tpm2GetIdleByPass (
  IN VOID  *Register
  );

/**
  This service enables the sending of commands to the TPM2.

  @param[in]      InputParameterBlockSize  Size of the TPM2 input parameter block.
  @param[in]      InputParameterBlock      Pointer to the TPM2 input parameter block.
  @param[in,out]  OutputParameterBlockSize Size of the TPM2 output parameter block.
  @param[in]      OutputParameterBlock     Pointer to the TPM2 output parameter block.

  @retval EFI_SUCCESS            The command byte stream was successfully sent to the device and a response was successfully received.
  @retval EFI_DEVICE_ERROR       The command was not successfully sent to the device or a response was not successfully received from the device.
  @retval EFI_BUFFER_TOO_SMALL   The output parameter block is too small.
**/
EFI_STATUS
EFIAPI
FfaTpm2SubmitCommand (
  IN UINT32      InputParameterBlockSize,
  IN UINT8       *InputParameterBlock,
  IN OUT UINT32  *OutputParameterBlockSize,
  IN UINT8       *OutputParameterBlock
  );

/**
  This service requests use TPM2.

  @retval EFI_SUCCESS      Get the control of TPM2 chip.
  @retval EFI_NOT_FOUND    TPM2 not found.
  @retval EFI_DEVICE_ERROR Unexpected device behavior.
**/
EFI_STATUS
EFIAPI
FfaTpm2RequestUseTpm (
  VOID
  );

#endif /* _TPM2_DEVICE_LIB_SMC_H_ */

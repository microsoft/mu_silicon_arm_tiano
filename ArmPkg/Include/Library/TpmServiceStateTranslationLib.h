/** @file
  Definitions for the TPM Service State Translation Library. This library's
  main purpose is to translate the states of the TPM service's CRB states to
  the main TPM's interface states. (i.e. TPM PC CRB -> TPM FIFO) A user of the
  TPM service should only need to update this library with the proper TPM
  interface type for their device.

  Copyright (c), Microsoft Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef TPM_SST_LIB_H_
#define TPM_SST_LIB_H_

#include <IndustryStandard/TpmPtp.h>

/**
  Initiates the transition to the Idle state

  @param  Locality The locality of the TPM to set into Idle

  @retval EFI_SUCCESS  Success
  @retval EFI_TIMEOUT  Timeout

**/
EFI_STATUS
TpmSstGoIdle (
  UINT8  Locality
  );

/**
  Initiates the transition to the commandReady state

  @param  Locality  The locality of the TPM to set to commandReady

  @retval EFI_SUCCESS  Success
  @retval EFI_TIMEOUT  Timeout

**/
EFI_STATUS
TpmSstCmdReady (
  UINT8  Locality
  );

/**
  Initiates command execution

  @param  Locality        The locality of the TPM to initiate the command on
  @param  InternalTpmCrb  The internal CRB to copy command data from

  @retval EFI_SUCCESS  Success
  @retval EFI_TIMEOUT  Timeout

**/
EFI_STATUS
TpmSstStart (
  UINT8                  Locality,
  PTP_CRB_REGISTERS_PTR  InternalTpmCrb
  );

/**
  Requests access to the given locality

  @param  Locality  The locality to request access to

  @retval EFI_SUCCESS  Success
  @retval EFI_TIMEOUT  Timeout

**/
EFI_STATUS
TpmSstLocalityRequest (
  UINT8  Locality
  );

/**
  Returns if IdleBypass is supported

  @retval TRUE   Supported
  @retval FALSE  Unsupported

**/
BOOLEAN
TpmSstIsIdleBypassSupported (
  VOID
  );

/**
  Initializes the TPM Service State Translation Library

**/
VOID
TpmSstInit (
  VOID
  );

#endif /* TPM_SST_LIB_H_ */

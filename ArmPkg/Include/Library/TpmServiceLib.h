/** @file
  Definitions for the TPM Service

  Copyright (c), Microsoft Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef TPM_SERVICE_LIB_H_
#define TPM_SERVICE_LIB_H_

#include <Base.h>
#include <IndustryStandard/ArmFfaPartInfo.h>
#include <Library/ArmSvcLib.h>
#include <Library/ArmFfaLibEx.h>

/**
  Initializes the TPM service

**/
VOID
TpmServiceInit (
  VOID
  );

/**
  De-initializes the TPM service

**/
VOID
TpmServiceDeInit (
  VOID
  );

/**
  Handler for TPM service commands

  @param  Request   The incoming message
  @param  Response  The outgoing message

**/
VOID
TpmServiceHandle (
  DIRECT_MSG_ARGS_EX  *Request,
  DIRECT_MSG_ARGS_EX  *Response
  );

#endif /* TPM_SERVICE_LIB_H_ */

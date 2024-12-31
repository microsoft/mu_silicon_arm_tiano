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
#include <IndustryStandard/ArmFfaBootInfo.h>
#include <IndustryStandard/ArmFfaPartInfo.h>
#include <Guid/Tpm2ServiceFfa.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/Tpm2DeviceLib.h>

#include "Tpm2DeviceLibFfa.h"

UINT32  mMyId               = MAX_UINT32;
UINT32  mFfaTpm2PartitionId = MAX_UINT32;

/**
  Check the return status from the FF-A call and returns EFI_STATUS

  @param EFI_LOAD_ERROR  FF-A status code returned in x0

  @retval EFI_SUCCESS    The entry point is executed successfully.
**/
EFI_STATUS
EFIAPI
TranslateTpmReturnStatus (
  UINTN  TpmReturnStatus
  )
{
  EFI_STATUS  Status;

  switch (TpmReturnStatus) {
    case TPM2_FFA_SUCCESS_OK:
    case TPM2_FFA_SUCCESS_OK_RESULTS_RETURNED:
      Status = EFI_SUCCESS;
      break;

    case TPM2_FFA_ERROR_NOFUNC:
      Status = EFI_NOT_FOUND;
      break;

    case TPM2_FFA_ERROR_NOTSUP:
      Status = EFI_UNSUPPORTED;
      break;

    case TPM2_FFA_ERROR_INVARG:
      Status = EFI_INVALID_PARAMETER;
      break;

    case TPM2_FFA_ERROR_INV_CRB_CTRL_DATA:
      Status = EFI_COMPROMISED_DATA;
      break;

    case TPM2_FFA_ERROR_ALREADY:
      Status = EFI_ALREADY_STARTED;
      break;

    case TPM2_FFA_ERROR_DENIED:
      Status = EFI_ACCESS_DENIED;
      break;

    case TPM2_FFA_ERROR_NOMEM:
      Status = EFI_OUT_OF_RESOURCES;
      break;

    default:
      Status = EFI_DEVICE_ERROR;
  }

  return Status;
}

/**
  This function is used to prepare a GUID for FF-A.

  The FF-A expects a GUID to be in a specific format. This function
  manipulates the input Guid to be understood by the FF-A.

  Note: This function is symmetric, i.e., calling it twice will return the
  original GUID. Thus, it can be used to prepare and restore the GUID.

  @param  Guid - Supplies the pointer for original GUID. This is the one you
  get from VS GUID creator.

  @retval None.
**/
STATIC
VOID
FfaPrepareGuid (
  IN OUT EFI_GUID  *Guid
  )
{
  UINT32  TempData[4];

  if (Guid == NULL) {
    return;
  }

  //
  // Swap Data2 and Data3 of the input GUID.
  //

  Guid->Data2 += Guid->Data3;
  Guid->Data3  = Guid->Data2 - Guid->Data3;
  Guid->Data2  = Guid->Data2 - Guid->Data3;
  CopyMem (TempData, Guid, sizeof (EFI_GUID));

  //
  // Swap the bytes for TempData[2] and TempData[3].
  //

  TempData[2] = SwapBytes32 (TempData[2]);
  TempData[3] = SwapBytes32 (TempData[3]);
  CopyMem (Guid, TempData, sizeof (EFI_GUID));
}

/**
  Send a command to FF-A to make sure it is at least v1.2 because we need DIRECT
  REQ2 to talk to TPM services.

  @retval EFI_SUCCESS     Command was successfully sent to the TPM
                          and the response was copied to the Output buffer.
  @retval Other           Some error occurred in communication with the TPM.
**/
EFI_STATUS
VerifyFfaVersion (
  VOID
  )
{
  EFI_STATUS        Status;
  FFA_CONDUIT_ARGS  FfaConduitArgs;

  ZeroMem (&FfaConduitArgs, sizeof (FFA_CONDUIT_ARGS));
  FfaConduitArgs.Arg0 = ARM_FID_FFA_VERSION;
  FfaConduitArgs.Arg1 = ARM_FFA_CREATE_VERSION (
                          ARM_FFA_MAJOR_VERSION,
                          ARM_FFA_MINOR_VERSION
                          );

  Status = ArmCallFfaConduit (&FfaConduitArgs);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (((FfaConduitArgs.Arg0 >> ARM_FFA_MAJOR_VERSION_SHIFT) != ARM_FFA_MAJOR_VERSION) ||
      ((FfaConduitArgs.Arg0 & ARM_FFA_MINOR_VERSION_MASK) < ARM_FFA_MINOR_VERSION))
  {
    Status = EFI_UNSUPPORTED;
    goto Exit;
  }

  Status = EFI_SUCCESS;

Exit:
  return Status;
}

EFI_STATUS
GetMyId (
  OUT UINT16  *PartitionId
  )
{
  EFI_STATUS        Status;
  FFA_CONDUIT_ARGS  FfaConduitArgs;

  if (PartitionId == NULL) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  if (mMyId != MAX_UINT32) {
    *PartitionId = mMyId;
    Status       = EFI_SUCCESS;
    goto Exit;
  }

  ZeroMem (&FfaConduitArgs, sizeof (FFA_CONDUIT_ARGS));
  FfaConduitArgs.Arg0 = ARM_FID_FFA_ID_GET;

  Status = ArmCallFfaConduit (&FfaConduitArgs);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  *PartitionId = (UINT16)(FfaConduitArgs.Arg2 & MAX_UINT16);
  mMyId        = *PartitionId;

Exit:
  return Status;
}

EFI_STATUS
GetTpmServicePartitionId (
  OUT UINT32  *PartitionId
  )
{
  EFI_STATUS              Status;
  FFA_CONDUIT_ARGS        FfaConduitArgs;
  EFI_GUID                TpmServiceGuid;
  UINT16                  CurrentIndex;
  UINT16                  LastIndex;
  EFI_FFA_PART_INFO_DESC  *TpmPartInfo;

  if (PartitionId == NULL) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  if (mFfaTpm2PartitionId != MAX_UINT32) {
    *PartitionId = mFfaTpm2PartitionId;
    Status       = EFI_SUCCESS;
    goto Exit;
  }

  if (PcdGet16 (PcdTpmServiceFfaPartitionId) != 0) {
    mFfaTpm2PartitionId = PcdGet16 (PcdTpmServiceFfaPartitionId);
    *PartitionId        = mFfaTpm2PartitionId;
    Status              = EFI_SUCCESS;

    goto Exit;
  }

  CopyMem (&TpmServiceGuid, &gEfiTpm2ServiceFfaGuid, sizeof (EFI_GUID));

  ZeroMem (&FfaConduitArgs, sizeof (FFA_CONDUIT_ARGS));
  FfaConduitArgs.Arg0 = ARM_FID_FFA_PARTITION_INFO_GET_REGS;

  FfaPrepareGuid (&TpmServiceGuid);
  CopyMem ((VOID *)&FfaConduitArgs.Arg1, (VOID *)&TpmServiceGuid, sizeof (EFI_GUID));

  Status = ArmCallFfaConduit (&FfaConduitArgs);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  LastIndex    = (UINT16)(FfaConduitArgs.Arg2 & MAX_UINT16);
  CurrentIndex = (UINT16)((FfaConduitArgs.Arg2 >> 16) & MAX_UINT16);

  // Only allow one TPM service partition.
  if ((CurrentIndex != 0) || (LastIndex != 0)) {
    DEBUG ((DEBUG_ERROR, "Discovered TPM is odd... CurrentIndex: %d, LastIndex: %d.\n", CurrentIndex, LastIndex));
    Status = EFI_DEVICE_ERROR;
    goto Exit;
  }

  TpmPartInfo         = (EFI_FFA_PART_INFO_DESC *)(&FfaConduitArgs.Arg3);
  mFfaTpm2PartitionId = TpmPartInfo->PartitionId;
  *PartitionId        = mFfaTpm2PartitionId;

  Status = PcdSet16S (PcdTpmServiceFfaPartitionId, mFfaTpm2PartitionId);

Exit:
  return Status;
}

/**
  This function is used to send a FF-A command to the TPM service partition through REQ2.

  Note that only the w4-w7 will be honored by the TPM service partition, according to
  FF-A spec.

  @param[in]      TpmCommandBuffer - Supplies the pointer to the TPM command buffer.
  @param[in]      TpmCommandBufferSize - Supplies the size of the TPM command buffer.
  @param[in, out] TpmResponseBuffer - Supplies the pointer to the TPM response buffer.
  @param[in, out] TpmResponseBufferSize - Supplies the size of the TPM response buffer.

  @retval EFI_SUCCESS           The TPM command was successfully sent to the TPM
                                and the response was copied to the Output buffer.
  @retval EFI_INVALID_PARAMETER The TPM command buffer is NULL or the TPM command
                                buffer size is 0.
  @retval EFI_DEVICE_ERROR      An error occurred in communication with the TPM.
**/
EFI_STATUS
Tpm2ServiceFuncCallReq2 (
  IN FFA_CONDUIT_ARGS  *FfaConduitArgs
  )
{
  EFI_STATUS  Status;
  EFI_GUID    TpmServiceGuid;

  if (FfaConduitArgs == NULL) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  if (mMyId == MAX_UINT32) {
    Status = GetMyId ((UINT16 *)&mMyId);
    if (EFI_ERROR (Status)) {
      goto Exit;
    }
  }

  if (mFfaTpm2PartitionId == MAX_UINT32) {
    Status = GetTpmServicePartitionId (&mFfaTpm2PartitionId);
    if (EFI_ERROR (Status)) {
      goto Exit;
    }
  }

  FfaConduitArgs->Arg0 = ARM_FID_FFA_MSG_SEND_DIRECT_REQ2;
  FfaConduitArgs->Arg1 = mMyId << 16 | mFfaTpm2PartitionId;

  CopyMem (&TpmServiceGuid, &gEfiTpm2ServiceFfaGuid, sizeof (EFI_GUID));
  FfaPrepareGuid (&TpmServiceGuid);
  CopyMem ((VOID *)&FfaConduitArgs->Arg2, (VOID *)&TpmServiceGuid, sizeof (EFI_GUID));

  Status = ArmCallFfaConduit (FfaConduitArgs);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (FfaConduitArgs->Arg0 != ARM_FID_FFA_MSG_SEND_DIRECT_RESP2) {
    Status = EFI_DEVICE_ERROR;
    goto Exit;
  }

  Status = EFI_SUCCESS;

Exit:
  return Status;
}

EFI_STATUS
Tpm2GetInterfaceVersion (
  OUT UINT32  *Version
  )
{
  EFI_STATUS        Status;
  FFA_CONDUIT_ARGS  FfaConduitArgs;

  if (Version == NULL) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  ZeroMem (&FfaConduitArgs, sizeof (FFA_CONDUIT_ARGS));
  FfaConduitArgs.Arg4 = TPM2_FFA_GET_INTERFACE_VERSION;

  Status = Tpm2ServiceFuncCallReq2 (&FfaConduitArgs);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = TranslateTpmReturnStatus (FfaConduitArgs.Arg4);

  if (!EFI_ERROR (Status)) {
    *Version = FfaConduitArgs.Arg5;
  }

Exit:
  return Status;
}

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
  )
{
  EFI_STATUS        Status;
  FFA_CONDUIT_ARGS  FfaConduitArgs;

  if (FeatureInfo == NULL) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  ZeroMem (&FfaConduitArgs, sizeof (FFA_CONDUIT_ARGS));
  FfaConduitArgs.Arg4 = TPM2_FFA_GET_FEATURE_INFO;
  FfaConduitArgs.Arg5 = TPM_SERVICE_FEATURE_SUPPORT_NOTIFICATION;

  Status = Tpm2ServiceFuncCallReq2 (&FfaConduitArgs);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = TranslateTpmReturnStatus (FfaConduitArgs.Arg4);

Exit:
  return Status;
}

EFI_STATUS
Tpm2ServiceStart (
  IN UINT64  FuncQualifier,
  IN UINT64  LocalityQualifier
  )
{
  EFI_STATUS        Status;
  FFA_CONDUIT_ARGS  FfaConduitArgs;

  ZeroMem (&FfaConduitArgs, sizeof (FFA_CONDUIT_ARGS));
  FfaConduitArgs.Arg4 = TPM2_FFA_START;
  FfaConduitArgs.Arg5 = (FuncQualifier & 0xFF);
  FfaConduitArgs.Arg6 = (LocalityQualifier & 0xFF);

  Status = Tpm2ServiceFuncCallReq2 (&FfaConduitArgs);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = TranslateTpmReturnStatus (FfaConduitArgs.Arg4);

Exit:
  return Status;
}

EFI_STATUS
Tpm2RegisterNotification (
  IN BOOLEAN  NotificationTypeQualifier,
  IN UINT16   vCpuId,
  IN UINT64   NotificationId
  )
{
  EFI_STATUS        Status;
  FFA_CONDUIT_ARGS  FfaConduitArgs;

  ZeroMem (&FfaConduitArgs, sizeof (FFA_CONDUIT_ARGS));
  FfaConduitArgs.Arg4 = TPM2_FFA_REGISTER_FOR_NOTIFICATION;
  FfaConduitArgs.Arg5 = (NotificationTypeQualifier << 16 | vCpuId);
  FfaConduitArgs.Arg6 = (NotificationId & 0xFF);

  Status = Tpm2ServiceFuncCallReq2 (&FfaConduitArgs);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = TranslateTpmReturnStatus (FfaConduitArgs.Arg4);

Exit:
  return Status;
}

EFI_STATUS
Tpm2UnregisterNotification (
  VOID
  )
{
  EFI_STATUS        Status;
  FFA_CONDUIT_ARGS  FfaConduitArgs;

  ZeroMem (&FfaConduitArgs, sizeof (FFA_CONDUIT_ARGS));
  FfaConduitArgs.Arg4 = TPM2_FFA_UNREGISTER_FROM_NOTIFICATION;

  Status = Tpm2ServiceFuncCallReq2 (&FfaConduitArgs);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = TranslateTpmReturnStatus (FfaConduitArgs.Arg4);

Exit:
  return Status;
}

EFI_STATUS
Tpm2FinishNotified (
  VOID
  )
{
  EFI_STATUS        Status;
  FFA_CONDUIT_ARGS  FfaConduitArgs;

  ZeroMem (&FfaConduitArgs, sizeof (FFA_CONDUIT_ARGS));
  FfaConduitArgs.Arg4 = TPM2_FFA_FINISH_NOTIFIED;

  Status = Tpm2ServiceFuncCallReq2 (&FfaConduitArgs);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = TranslateTpmReturnStatus (FfaConduitArgs.Arg4);

Exit:
  return Status;
}

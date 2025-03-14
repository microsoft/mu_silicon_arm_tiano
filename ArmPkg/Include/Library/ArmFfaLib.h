/** @file
  Arm FF-A library Header file

  Copyright (c) 2024, Arm Limited. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Glossary:
     - FF-A - Firmware Framework for Arm A-profile
     - spmc - Secure Partition Manager Core
     - spmd - Secure Partition Manager Dispatcher

  @par Reference(s):
     - Arm Firmware Framework for Arm A-Profile [https://developer.arm.com/documentation/den0077/latest]

**/

#ifndef ARM_FFA_LIB_H_
#define ARM_FFA_LIB_H_

#include <IndustryStandard/ArmFfaSvc.h>
#include <IndustryStandard/ArmFfaBootInfo.h>
#include <IndustryStandard/ArmFfaPartInfo.h>

#include <Library/ArmSmcLib.h>

#define FFA_RXTX_MAP_INPUT_PROPERTY_DEFAULT  0x00

/** Implementation define arguments used in
 *  FFA_SEND_MSG_DIRECT_REQ/FFA_SEND_MSG_DIRECT_RESP (i.e. v1) and
 *  FFA_SEND_MSG_DIRECT_REQ2/FFA_SEND_MSG_DIRECT_RESP2 (i.e. v2)
 */
typedef struct DirectMsgArgs {
  /// Implementation define argument 0, this will be set to/from x3(v1) or x4(v2)
  UINTN    Arg0;

  /// Implementation define argument 1, this will be set to/from x4(v1) or x5(v2)
  UINTN    Arg1;

  /// Implementation define argument 2, this will be set to/from x5(v1) or x6(v2)
  UINTN    Arg2;

  /// Implementation define argument 3, this will be set to/from x6(v1) or x7(v2)
  UINTN    Arg3;

  /// Implementation define argument 4, this will be set to/from x7(v1) or x8(v2)
  UINTN    Arg4;

  // MU_CHANGE Starts: Add implementation define arguments 5-13
  /// Implementation define argument 5, this will be set to/from x9(v2)
  UINTN    Arg5;

  /// Implementation define argument 6, this will be set to/from x10(v2)
  UINTN    Arg6;

  /// Implementation define argument 7, this will be set to/from x11(v2)
  UINTN    Arg7;

  /// Implementation define argument 8, this will be set to/from x12(v2)
  UINTN    Arg8;

  /// Implementation define argument 9, this will be set to/from x13(v2)
  UINTN    Arg9;

  /// Implementation define argument 10, this will be set to/from x14(v2)
  UINTN    Arg10;

  /// Implementation define argument 11, this will be set to/from x15(v2)
  UINTN    Arg11;

  /// Implementation define argument 12, this will be set to/from x16(v2)
  UINTN    Arg12;

  /// Implementation define argument 13, this will be set to/from x17(v2)
  UINTN    Arg13;
  // MU_CHANGE Ends
} DIRECT_MSG_ARGS;

/**
  Convert EFI_GUID to UUID format.
  for example, If there is EFI_GUID named
  "378daedc-f06b-4446-8314-40ab933c87a3",

  EFI_GUID is saved in memory like:
     dc ae 8d 37
     6b f0 46 44
     83 14 40 ab
     93 3c 87 a3

  However, UUID should be saved like:
     37 8d ae dc
     f0 6b 44 46
     83 14 40 ab
     93 3c 87 a3

  FF-A and other software components (i.e. linux-kernel)
  uses below format.

  @param [in] Guid            EFI_GUID
  @param [out] Uuid           Uuid

**/
VOID
EFIAPI
ConvertEfiGuidToUuid (
  IN   EFI_GUID  *Guid,
  OUT  UINT64    *Uuid
  );

/**
  Convert UUID to EFI_GUID format, which is the inverse of ConvertEfiGuidToUuid.

  @param [in] Uuid            Uuid
  @param [out] Guid           EFI_GUID
**/
VOID
EFIAPI
ConvertUuidToEfiGuid (
  IN  UINT64    *Uuid,
  OUT EFI_GUID  *Guid
  );

/**
  Convert EFI_STATUS to FFA return code.

  @param [in] Status          edk2 status code.

  @retval ARM_FFA_RET_*       return value correspond to EFI_STATUS.
**/
UINTN
EFIAPI
EfiStatusToFfaStatus (
  IN EFI_STATUS  Status
  );

/**
  Convert FFA return code to EFI_STATUS.

  @param [in] FfaStatus          Ffa Status Code.

  @retval EFI_STATUS             return value correspond EFI_STATUS to FfaStatus

**/
EFI_STATUS
EFIAPI
FfaStatusToEfiStatus (
  IN UINTN  FfaStatus
  );

/**
  Check FF-A support or not.

  @retval TRUE                   Supported
  @retval FALSE                  Not supported

**/
BOOLEAN
EFIAPI
IsFfaSupported (
  IN VOID
  );

/**
  Get mapped Rx/Tx buffers.

  @param [out]   TxBuffer         Address of TxBuffer
  @param [out]   TxBufferSize     Size of TxBuffer
  @param [out]   RxBuffer         Address of RxBuffer
  @param [out]   RxBufferSize     Size of RxBuffer

  @retval EFI_SUCCESS
  @retval Others             Error.

**/
EFI_STATUS
EFIAPI
ArmFfaLibGetRxTxBuffers (
  OUT VOID    **TxBuffer OPTIONAL,
  OUT UINT64  *TxBufferSize OPTIONAL,
  OUT VOID    **RxBuffer OPTIONAL,
  OUT UINT64  *RxBufferSize OPTIONAL
  );

/**
  Get FF-A version

  @param [in]    RequestMajorVersion          Minimal request major version
  @param [in]    RequestMinorVersion          Minimal request minor version
  @param [out]   CurrentMajorVersion          Current major version
  @param [out]   CurrentMinorVersion          Current minor version

**/
EFI_STATUS
EFIAPI
ArmFfaLibGetVersion (
  IN  UINT16  RequestMajorVersion,
  IN  UINT16  RequestMinorVersion,
  OUT UINT16  *CurrentMajorVersion,
  OUT UINT16  *CurrentMinorVersion
  );

/**
  Get FF-A features.

  @param [in]   Id               Feature id or function id
  @param [in]   InputProperties  Input properties according to Id
  @param [out]  Property1        First Property value.
  @param [out]  Property2        Second Property value.

  @retval EFI_SUCCESS
  @retval Others                 Error

**/
EFI_STATUS
EFIAPI
ArmFfaLibGetFeatures (
  IN  UINT32  Id,
  IN  UINT32  InputProperties,
  OUT UINTN   *Property1,
  OUT UINTN   *Property2
  );

/**
  Acquire ownership of the Rx buffer.

  @param [in]  PartId    Partition Id.

  @retval EFI_SUCCESS
  @retval Others         Error

**/
EFI_STATUS
EFIAPI
ArmFfaLibRxAcquire (
  IN UINT16  PartId
  );

/**
  Release ownership of the Rx buffer.

  @param [in]  PartId    Partition Id.

  @retval EFI_SUCCESS
  @retval Others         Error

**/
EFI_STATUS
EFIAPI
ArmFfaLibRxRelease (
  IN UINT16  PartId
  );

/**
  Get Partition info.
  If This function is called to get partition descriptors
  (Flags isn't set with FFA_PART_INFO_FL_TYPE_COUNT),
  It should call ArmFfaLibRxRelease() to release Rx buffer.

  @param [in]   ServiceGuid    Service guid.
  @param [in]   Flags          If this function called to get partition desc
                               and get successfully,
                               Caller should release RX buffer by calling
                               ArmFfaLibRxRelease
  @param [out]  Count          Number of partition or partition descriptor
  @param [out]  Size           Size of Partition Info structure in Rx Buffer

  @retval EFI_SUCCESS
  @retval Others               Error
**/
EFI_STATUS
EFIAPI
ArmFfaLibPartitionInfoGet (
  IN  EFI_GUID  *ServiceGuid,
  IN  UINT32    Flags,
  OUT UINT32    *Count,
  OUT UINT32    *Size OPTIONAL
  );

/**
  Get partition or VM id.
  This function is only called in ArmFfaLibConstructor.

  @param [out]    PartId      Partition id.

  @retval EFI_SUCCESS
  @retval Others              Error

**/
EFI_STATUS
EFIAPI
ArmFfaLibPartitionIdGet (
  OUT UINT16  *PartId
  );

/**
  Get spmc or spmd partition id.

  @param [out]    SpmPartId      spmc/spmd partition id.

  @retval EFI_SUCCESS
  @retval Others              Error

**/
EFI_STATUS
EFIAPI
ArmFfaLibSpmIdGet (
  OUT UINT16  *SpmPartId
  );

/**
  Restore context which interrupted with FFA_INTERRUPT (EFI_INTERRUPT_PENDING).

  @param [in]   PartId       Partition id
  @param [in]   CpuNumber    Cpu number in partition

  @retval EFI_SUCCESS
  @retval Other              Error

**/
EFI_STATUS
EFIAPI
ArmFfaLibRun (
  IN  UINT16  PartId,
  IN  UINT16  CpuNumber
  );

/**
  Send direct message request version 1.

  @param [in]      DestPartId       Dest partition id
  @param [in]      Flags            Message flags
  @param [in, out] ImpDefArgs       Implemented defined arguments and
                                    Implemented defined return values

  @retval EFI_SUCCESS               Success
  @retval Others                    Error

**/
EFI_STATUS
EFIAPI
ArmFfaLibMsgSendDirectReq (
  IN  UINT16               DestPartId,
  IN  UINT32               Flags,
  IN  OUT DIRECT_MSG_ARGS  *ImpDefArgs
  );

/**
  Send direct message request version 2.

  @param [in]      DestPartId       Dest partition id
  @param [in]      ServiceGuid      Service guid
  @param [in, out] ImpDefArgs       Implemented defined arguments and
                                    Implemented defined return values

  @retval EFI_SUCCESS               Success
  @retval Others                    Error

**/
EFI_STATUS
EFIAPI
ArmFfaLibMsgSendDirectReq2 (
  IN  UINT16               DestPartId,
  IN  EFI_GUID             *ServiceGuid,
  IN  OUT DIRECT_MSG_ARGS  *ImpDefArgs
  );

/**
  Mapping Rx/Tx buffers.
  This function is only called in ArmFfaLibConstructor because
  Rx/Tx buffer is registered only once per partition.

  @retval EFI_SUCCESS
  @retval EFI_ALREADY_STARTED     Rx/Tx buffer already mapped in PEI phase
  @retval EFI_OUT_OF_RESOURCE     Out of memory
  @retval EFI_INVALID_PARAMETER   Invalid alignment of Rx/Tx buffer
  @retval Others                  Error

**/
EFI_STATUS
EFIAPI
ArmFfaLibRxTxMap (
  IN VOID
  );

/**
  Unmap Rx/Tx buffer.
  This function is only called in Exit boot service because
  Rx/Tx buffer is registered only once per partition.

  @retval EFI_SUCCESS
  @retval EFI_INVALID_PARAMETERS               Already unregistered
  @retval EFI_UNSUPPORTED                      Not supported

**/
EFI_STATUS
EFIAPI
ArmFfaLibRxTxUnmap (
  IN VOID
  );

#endif // ARM_FFA_LIB_H_

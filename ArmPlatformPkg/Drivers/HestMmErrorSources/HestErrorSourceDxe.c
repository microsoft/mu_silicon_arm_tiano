/** @file
  Collects and appends the HEST error source descriptors from the MM drivers.

  The drivers entry point locates the MM Communication protocol and calls into
  Standalone MM to get the HEST error sources length and count. It also
  retrieves descriptor information.
  The information is then used to build the HEST table using the HEST table
  generation protocol.

  Copyright (c) 2020, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <IndustryStandard/Acpi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/MmCommunication2.h>
#include <Protocol/HestTable.h>

#include "HestMmErrorSourceCommon.h"

#define MM_COMMUNICATE_HEADER_SIZE (OFFSET_OF (EFI_MM_COMMUNICATE_HEADER, Data))

STATIC HEST_TABLE_PROTOCOL *mHestProtocol;
STATIC EFI_MM_COMMUNICATION2_PROTOCOL *mMmCommunication2;

/**
  Retrieve the error source descriptors from Standalone MM.

  Initialize the MM comminication buffer by assigning the MM service to
  invoke as gMmHestGetErrorSourceInfoGuid. Use the MM communication
  protocol to retrieve the error source descriptors.

  @param[in]      CommBuffSize  Size of communicate buffer.
  @param[in, out] CommBuffer    The communicate buffer.

  @retval EFI_SUCCESS           MM Communicate protocol call successful.
  @retval Other                 MM Communicate protocol call failed.

**/
STATIC
EFI_STATUS
GetErrorSourceDescriptors (
  IN     UINTN                     CommBuffSize,
  IN OUT EFI_MM_COMMUNICATE_HEADER **CommBuffer
  )
{
  EFI_STATUS Status;

  //
  // Initialize the CommBuffer with MM Communicate metadata.
  //
  CopyGuid (&(*CommBuffer)->HeaderGuid, &gMmHestGetErrorSourceInfoGuid);
  (*CommBuffer)->MessageLength =
    CommBuffSize -
    sizeof ((*CommBuffer)->HeaderGuid) -
    sizeof ((*CommBuffer)->MessageLength);

  //
  // Call into the Standalone MM using the MM Communicate protocol.
  //
  Status = mMmCommunication2->Communicate (
                                mMmCommunication2,
                                (VOID *)*CommBuffer,
                                (VOID *)*CommBuffer,
                                NULL
                                );

  return Status;
}

/**
  Collect HEST error source descriptors from all Standalone MM drivers and append
  them to the HEST table.

  Use MM Communication Protocol to communicate and collect the error source
  descriptor information from Standalone MM. Check for the required buffer size
  returned by the MM driver. Allocate buffer of adequate size and call again into
  MM.

  @retval EFI_SUCCESS          Successful to collect and append the error source
                               descriptors to HEST table.
  @retval EFI_OUT_OF_RESOURCES Memory allocation failure.
  @retval Other                For any other error.

**/
STATIC
EFI_STATUS
AppendMmErrorSources (VOID)
{
  EFI_MM_COMMUNICATE_HEADER   *CommunicationHeader = NULL;
  HEST_ERROR_SOURCE_DESC_INFO *ErrorSourceDescInfo;
  EFI_STATUS                  Status;
  UINTN                       CommBufferSize;

  //
  // Find out the number of error source descriptors that StandaloneMM supports
  // and length of the buffer to be supplied to retrieve those error source
  // descriptors. Do this by providing a buffer of size that is just sufficient
  // to hold the error source descriptor info. In response to this, the error
  // source descriptor that is returned contains the actual size of the
  // communication buffer that is required to retrieve all the error sources.
  //
  CommBufferSize = MM_COMMUNICATE_HEADER_SIZE + HEST_ERROR_SOURCE_DESC_INFO_SIZE;
  CommunicationHeader = AllocatePool (CommBufferSize);
  if (CommunicationHeader == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  // Call to retrieve the length of required buffer.
  Status = GetErrorSourceDescriptors (CommBufferSize, &CommunicationHeader);
  if ((EFI_ERROR (Status)) &&
      (Status != EFI_BAD_BUFFER_SIZE)) {
    DEBUG ((DEBUG_ERROR, "MM Communicate protocol call failed, status: %r\n",
                          Status));
    FreePool (CommunicationHeader);
    return Status;
  }

  // Check for the length of Error Source descriptors.
  ErrorSourceDescInfo = (HEST_ERROR_SOURCE_DESC_INFO *)(CommunicationHeader->Data);
  if ((ErrorSourceDescInfo->ErrSourceDescSize == 0) ||
      (ErrorSourceDescInfo->ErrSourceDescCount == 0)) {
    DEBUG ((DEBUG_INFO, "HEST error source(s) not found\n"));
    FreePool (CommunicationHeader);
    return EFI_SUCCESS;
  }

  //
  // Allocate CommBuffer of required size to accomodate all the error source
  // descriptors. Required size of communication buffer =
  // MM communicate metadata. + (error source desc info struct + error source
  // descriptor size)
  //
  CommBufferSize =
    MM_COMMUNICATE_HEADER_SIZE +
    HEST_ERROR_SOURCE_DESC_INFO_SIZE +
    ErrorSourceDescInfo->ErrSourceDescSize;

  // Free old MM Communicate buffer and allocate a new buffer of required size.
  FreePool (CommunicationHeader);
  CommunicationHeader = AllocatePool (CommBufferSize);
  if (CommunicationHeader == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  // Call to get the error source descriptors.
  Status = GetErrorSourceDescriptors (CommBufferSize, &CommunicationHeader);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MM Communicate protocol failed, status: %r\n",
                        Status));
    FreePool (CommunicationHeader);
    return Status;
  }

  //
  // Retrieve the HEST error source descriptor information. Ensure that there
  // is a valid list of error source descriptors.
  //
  ErrorSourceDescInfo = (HEST_ERROR_SOURCE_DESC_INFO *)(CommunicationHeader->Data);
  if (ErrorSourceDescInfo->ErrSourceDescList == NULL) {
    DEBUG ((DEBUG_INFO, "Error source descriptor list is empty"));
    FreePool (CommunicationHeader);
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_INFO, "HEST ErrorSources: TotalCount = %d TotalLength = %d \n",
                       ErrorSourceDescInfo->ErrSourceDescCount,
                       ErrorSourceDescInfo->ErrSourceDescSize));
  //
  // Append the error source descriptors to HEST table using the HEST table
  // generation protocol.
  //
  Status = mHestProtocol->AppendErrorSourceDescriptors (
                            ErrorSourceDescInfo->ErrSourceDescList,
                            ErrorSourceDescInfo->ErrSourceDescSize,
                            ErrorSourceDescInfo->ErrSourceDescCount
                            );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Failed to append error source(s), status: %r\n",
                         Status));
  }

  FreePool (CommunicationHeader);
  return Status;
}

/**
  The Entry Point for Hest Error Source DXE driver.

  Locates the Hest Table generation and MM Communication2 protocols. Using the
  MM Communication2, the driver collects the Error Source Descriptor(s) from
  Standalone MM. It then appends those Error Source Descriptor(s) to the Hest
  table using the Hest Table generation protocol.

  @param[in] ImageHandle The firmware allocated handle for the EFI image.
  @param[in] SystemTable A pointer to the EFI System Table.

  @retval EFI_SUCCESS    The entry point is executed successfully.
  @retval Other          Some error occurred when executing this entry point.

**/
EFI_STATUS
EFIAPI
HestErrorSourceInitialize (
  IN EFI_HANDLE       ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;

  Status = gBS->LocateProtocol (
                  &gHestTableProtocolGuid,
                  NULL,
                  (VOID **)&mHestProtocol
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "Failed to locate HEST table generation protocol, status:%r\n",
      Status
      ));
    return Status;
  }

  Status = gBS->LocateProtocol (
                  &gEfiMmCommunication2ProtocolGuid,
                  NULL,
                  (VOID **)&mMmCommunication2
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "Failed to locate MMCommunication2 driver protocol, status:%r\n",
      Status
      ));
    return Status;
  }

  //
  // Append HEST error sources retrieved from StandaloneMM, if any, into the HEST
  // ACPI table.
  //
  Status = AppendMmErrorSources ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "HEST table creation faied, status:%r\n",
                         Status));
  }

  return EFI_SUCCESS;
}

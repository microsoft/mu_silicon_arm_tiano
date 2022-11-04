/** @file -- SmmVariablePei.c
  Provides interface for reading Secure System Variables during PEI.

  Copyright (c) 2006 - 2019, Intel Corporation. All rights reserved.<BR>
  Copyright (c) Microsoft Corporation.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "SmmVariablePei.h"

//
// Module globals
//
EFI_PEI_READ_ONLY_VARIABLE2_PPI  mPeiSecureVariableRead = {
  PeiSmmGetVariable,
  PeiSmmGetNextVariableName
};

EFI_PEI_PPI_DESCRIPTOR  mPeiSmmVariablePpi = {
  (EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
  &gEfiPeiReadOnlyVariable2PpiGuid,
  &mPeiSecureVariableRead
};

/**
 * Entry point of PEI Secure Variable read driver
 *
 * @param  FileHandle   Handle of the file being invoked.
 *                      Type EFI_PEI_FILE_HANDLE is defined in FfsFindNextFile().
 * @param  PeiServices  General purpose services available to every PEIM.
 *
 * @retval EFI_SUCCESS  If the interface could be successfully installed
 * @retval Others       Returned from PeiServicesInstallPpi()
 *
**/
EFI_STATUS
EFIAPI
PeiSmmVariableInitialize (
  IN       EFI_PEI_FILE_HANDLE  FileHandle,
  IN CONST EFI_PEI_SERVICES     **PeiServices
  )
{
  return PeiServicesInstallPpi (&mPeiSmmVariablePpi);
}

/**
  This service retrieves a variable's value using its name and GUID.

  This function is using the Secure Variable Store. If the Data
  buffer is too small to hold the contents of the variable, the error
  EFI_BUFFER_TOO_SMALL is returned and DataSize is set to the required buffer
  size to obtain the data.

  @param  This                  A pointer to this instance of the EFI_PEI_READ_ONLY_VARIABLE2_PPI.
  @param  VariableName          A pointer to a null-terminated string that is the variable's name.
  @param  VariableGuid          A pointer to an EFI_GUID that is the variable's GUID. The combination of
                                VariableGuid and VariableName must be unique.
  @param  Attributes            If non-NULL, on return, points to the variable's attributes.
  @param  DataSize              On entry, points to the size in bytes of the Data buffer.
                                On return, points to the size of the data returned in Data.
  @param  Data                  Points to the buffer which will hold the returned variable value.
                                May be NULL with a zero DataSize in order to determine the size of the buffer needed.

  @retval EFI_SUCCESS           The variable was read successfully.
  @retval EFI_NOT_FOUND         The variable was not found.
  @retval EFI_BUFFER_TOO_SMALL  The DataSize is too small for the resulting data.
                                DataSize is updated with the size required for
                                the specified variable.
  @retval EFI_INVALID_PARAMETER VariableName, VariableGuid, DataSize or Data is NULL.
  @retval EFI_DEVICE_ERROR      The variable could not be retrieved because of a device error.

**/
EFI_STATUS
EFIAPI
PeiSmmGetVariable (
  IN CONST  EFI_PEI_READ_ONLY_VARIABLE2_PPI *This,
  IN CONST  CHAR16 *VariableName,
  IN CONST  EFI_GUID *VariableGuid,
  OUT       UINT32 *Attributes, OPTIONAL
  IN OUT    UINTN                            *DataSize,
  OUT       VOID                             *Data OPTIONAL
  )
{
  EFI_STATUS                                Status;
  UINTN                                     MessageSize;
  EFI_MM_COMMUNICATE_HEADER                 *CommunicateHeader;
  SMM_VARIABLE_COMMUNICATE_HEADER           *SmmVarCommsHeader;
  SMM_VARIABLE_COMMUNICATE_ACCESS_VARIABLE  *SmmVarAccessHeader;
  UINT8                                     *MmCommunicateBuffer;
  UINT8                                     RequiredPages;
  EFI_PEI_MM_COMMUNICATION_PPI              *MmCommunicationPpi;

  // Check input parameters
  if ((VariableName == NULL) || (VariableGuid == NULL) || (DataSize == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (VariableName[0] == 0) {
    return EFI_NOT_FOUND;
  }

  if ((*DataSize > 0) && (Data == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = PeiServicesLocatePpi (&gEfiPeiMmCommunicationPpiGuid, 0, NULL, (VOID **)&MmCommunicationPpi);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to locate PEI MM Communication PPI: %r\n", __FUNCTION__, Status));
    return Status;
  }

  // Allocate required pages to send MM request
  RequiredPages = EFI_SIZE_TO_PAGES (
                    sizeof (EFI_MM_COMMUNICATE_HEADER) + \
                    sizeof (SMM_VARIABLE_COMMUNICATE_HEADER) + \
                    sizeof (SMM_VARIABLE_COMMUNICATE_ACCESS_VARIABLE) + \
                    StrSize (VariableName) + *DataSize
                    );
  MmCommunicateBuffer = (UINT8 *)AllocatePages (RequiredPages);

  if (MmCommunicateBuffer == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate memory: %r\n", __FUNCTION__, Status));
    return Status;
  }

  // Zero the entire Communication Buffer
  ZeroMem (MmCommunicateBuffer, (RequiredPages * EFI_PAGE_SIZE));

  CommunicateHeader = (EFI_MM_COMMUNICATE_HEADER *)MmCommunicateBuffer;

  SmmVarCommsHeader = (SMM_VARIABLE_COMMUNICATE_HEADER *)CommunicateHeader->Data;

  SmmVarAccessHeader = (SMM_VARIABLE_COMMUNICATE_ACCESS_VARIABLE *)SmmVarCommsHeader->Data;

  // Use gEfiSmmVariableProtocolGuid to request the SMM variable service in S-EL0
  CopyMem ((VOID *)&CommunicateHeader->HeaderGuid, &gEfiSmmVariableProtocolGuid, sizeof (GUID));

  // We are only supporting GetVariable
  SmmVarCommsHeader->Function = SMM_VARIABLE_FUNCTION_GET_VARIABLE;

  // Variable GUID
  CopyMem ((VOID *)&SmmVarAccessHeader->Guid, VariableGuid, sizeof (GUID));

  // Program the max amount of data we accept.
  SmmVarAccessHeader->DataSize = *DataSize;

  // Get size of the variable name
  SmmVarAccessHeader->NameSize = StrSize (VariableName);

  //
  // Program all data structure sizes
  //

  // Program the MM header size
  CommunicateHeader->MessageLength = sizeof (SMM_VARIABLE_COMMUNICATE_HEADER) - 1 + \
                                     sizeof (SMM_VARIABLE_COMMUNICATE_ACCESS_VARIABLE) - 1 + \
                                     SmmVarAccessHeader->NameSize + SmmVarAccessHeader->DataSize;

  // Size of the entire message sent to the secure world.
  MessageSize = CommunicateHeader->MessageLength + \
                sizeof (CommunicateHeader->HeaderGuid) + \
                sizeof (CommunicateHeader->MessageLength);

  CopyMem ((VOID *)&SmmVarAccessHeader->Name, VariableName, SmmVarAccessHeader->NameSize);

  // Send the MM request using MmCommunicationPeiLib
  Status = MmCommunicationPpi->Communicate (MmCommunicationPpi, CommunicateHeader, &MessageSize);

  if (EFI_ERROR (Status)) {
    // Received an error from MM interface.
    DEBUG ((DEBUG_ERROR, "%a - MM Interface Error: %r\n", __FUNCTION__, Status));
    goto Exit;
  }

  // MM request was successfully handled by the framework.
  // Set status to the Variable Service Status Code
  Status = SmmVarCommsHeader->ReturnStatus;
  if (EFI_ERROR (Status)) {
    // We received an error from Variable Service.
    // We cant do anymore so return Status
    if (Status != EFI_BUFFER_TOO_SMALL) {
      DEBUG ((DEBUG_ERROR, "%a - Variable Service Error: %r\n", __FUNCTION__, Status));
    }

    goto Exit;
  }

  Status = EFI_SUCCESS;

  // User provided buffer is too small
  if (*DataSize < SmmVarAccessHeader->DataSize) {
    Status = EFI_BUFFER_TOO_SMALL;
  }

Exit:
  // Check if we need to set Attributes
  if (Attributes != NULL) {
    *Attributes = SmmVarAccessHeader->Attributes;
  }

  *DataSize = SmmVarAccessHeader->DataSize;

  if (Status == EFI_SUCCESS) {
    CopyMem ((VOID *)Data, (UINT8 *)SmmVarAccessHeader->Name + SmmVarAccessHeader->NameSize, *DataSize);
  }

  // Free the Communication Buffer
  if (MmCommunicateBuffer != NULL) {
    FreePages (MmCommunicateBuffer, RequiredPages);
  }

  return Status;
}

/**
 * This function is not supported
 * @retval EFI_UNSUPPORTED
 *
**/
EFI_STATUS
EFIAPI
PeiSmmGetNextVariableName (
  IN CONST  EFI_PEI_READ_ONLY_VARIABLE2_PPI  *This,
  IN OUT UINTN                               *VariableNameSize,
  IN OUT CHAR16                              *VariableName,
  IN OUT EFI_GUID                            *VariableGuid
  )
{
  return EFI_UNSUPPORTED;
}

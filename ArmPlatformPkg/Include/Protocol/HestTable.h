/** @file
  Provides protocol to create and install the HEST ACPI for the
  platform.

  Copyright (c) 2020, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef HEST_TABLE_H_
#define HEST_TABLE_H_

#define HEST_TABLE_PROTOCOL_GUID \
  { \
    0x705bdcd9, 0x8c47, 0x457e, \
    { 0xad, 0x0d, 0xf7, 0x86, 0xf3, 0x4a, 0x0d, 0x63 } \
  }

/**
  Append a list of error source descriptors to the HEST table.

  Allows error source list producer module to append its error source descriptors
  into the HEST ACPI table being prepared for installation.

  @param[in] ErrorSourceDescriptorList     List of Error Source Descriptors.
  @param[in] ErrorSourceDescriptorListSize Total Size of the ErrorSourceDescriptorList.
  @param[in] ErrorSourceDescriptorCount    Total count of error source descriptors in
                                           ErrorSourceDescriptorList.

  @retval EFI_SUCCESS                      Appending the error source descriptors
                                           successful.
  @retval EFI_OUT_OF_RESOURCES             Buffer reallocation failed for the HEST
                                           table.
  @retval EFI_INVALID_PARAMETER            Null ErrorSourceDescriptorList param or
                                           ErrorSourceDescriptorListSize is 0.

**/
typedef
EFI_STATUS
(EFIAPI *APPEND_ERROR_SOURCE_DESCRIPTOR) (
  IN CONST VOID *ErrorSourceDescriptorList,
  IN UINTN      ErrorSourceDescriptorListSize,
  IN UINTN      ErrorSourceDescriptorCount
  );

/**
  Install ACPI HEST table.

  @retval EFI_SUCCESS On ACPI HEST table installation success.
  @retval Other       On ACPI HEST table installation failure.

**/
typedef
EFI_STATUS
(EFIAPI *INSTALL_HEST_TABLE) (VOID);

//
// HEST_TABLE_PROTOCOL enables creation and installation of HEST table
//
typedef struct {
  APPEND_ERROR_SOURCE_DESCRIPTOR AppendErrorSourceDescriptors;
  INSTALL_HEST_TABLE             InstallHestTable;
} HEST_TABLE_PROTOCOL;

extern EFI_GUID gHestTableProtocolGuid;
#endif  // HEST_TABLE_H_

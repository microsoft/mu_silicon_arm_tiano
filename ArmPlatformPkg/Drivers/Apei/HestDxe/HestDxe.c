/** @file
  Builds and installs the HEST ACPI table.

  This driver installs a protocol that can be used to create and install a HEST
  ACPI table. The protocol allows one or more error source producers to add the
  error source descriptors into the HEST table. It also allows the resulting HEST
  ACPI table to be installed.

  Copyright (c) 2020, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <IndustryStandard/Acpi.h>

#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/AcpiSystemDescriptionTable.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/HestTable.h>

typedef struct {
  VOID      *HestTable;       /// Pointer to HEST ACPI table.
  UINT32    CurrentTableSize; /// Current size of HEST ACPI table.
} HEST_DXE_DRIVER_DATA;

STATIC EFI_ACPI_TABLE_PROTOCOL  *mAcpiTableProtocol = NULL;
STATIC HEST_DXE_DRIVER_DATA     mHestDriverData;

/**
  Helper function to reallocate memory pool for every new error source descriptor
  added.

  @param[in]      OldTableSize  Old memory pool size.
  @param[in]      NewTableSize  Required memory pool size.
  @param[in, out] OldBuffer     Current memory buffer pointer holding the HEST
                                table data.

  @retval                       New pointer to reallocated memory pool.

**/
STATIC
VOID *
ReallocateHestTableMemory (
  IN     UINT32  OldTableSize,
  IN     UINT32  NewTableSize,
  IN OUT VOID    *OldBuffer
  )
{
  return ReallocateReservedPool (
           OldTableSize,
           NewTableSize,
           OldBuffer
           );
}

/**
  Allocate memory for the HEST ACPI table header and populate it.

  @retval EFI_SUCCESS           On successful creation of HEST header.
  @retval EFI_OUT_OF_RESOURCES  If system is out of memory recources.

**/
STATIC
EFI_STATUS
BuildHestHeader (
  VOID
  )
{
  EFI_ACPI_6_3_HARDWARE_ERROR_SOURCE_TABLE_HEADER  HestHeader;
  UINT64                                           TempOemTableId;

  //
  // Allocate memory for the HEST ACPI table header.
  //
  mHestDriverData.HestTable =
    ReallocateHestTableMemory (
      0,
      sizeof (EFI_ACPI_6_3_HARDWARE_ERROR_SOURCE_TABLE_HEADER),
      NULL
      );
  if (mHestDriverData.HestTable == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  mHestDriverData.CurrentTableSize =
    sizeof (EFI_ACPI_6_3_HARDWARE_ERROR_SOURCE_TABLE_HEADER);

  //
  // Populate the values of the HEST ACPI table header.
  //
  HestHeader.Header.Signature = EFI_ACPI_6_3_HARDWARE_ERROR_SOURCE_TABLE_SIGNATURE;
  HestHeader.Header.Revision  = EFI_ACPI_6_3_HARDWARE_ERROR_SOURCE_TABLE_REVISION;
  CopyMem (
    &HestHeader.Header.OemId,
    FixedPcdGetPtr (PcdAcpiDefaultOemId),
    sizeof (HestHeader.Header.OemId)
    );
  TempOemTableId = FixedPcdGet64 (PcdAcpiDefaultOemTableId);
  CopyMem (
    &HestHeader.Header.OemTableId,
    &TempOemTableId,
    sizeof (HestHeader.Header.OemTableId)
    );
  HestHeader.Header.OemRevision     = PcdGet32 (PcdAcpiDefaultOemRevision);
  HestHeader.Header.CreatorId       = PcdGet32 (PcdAcpiDefaultCreatorId);
  HestHeader.Header.CreatorRevision = PcdGet32 (PcdAcpiDefaultCreatorRevision);
  HestHeader.ErrorSourceCount       = 0;
  CopyMem (mHestDriverData.HestTable, &HestHeader, sizeof (HestHeader));

  return EFI_SUCCESS;
}

/**
  Append HEST error source descriptor protocol service.

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
STATIC
EFI_STATUS
EFIAPI
AppendErrorSourceDescriptor (
  IN CONST VOID  *ErrorSourceDescriptorList,
  IN UINTN       ErrorSourceDescriptorListSize,
  IN UINTN       ErrorSourceDescriptorCount
  )
{
  EFI_ACPI_6_3_HARDWARE_ERROR_SOURCE_TABLE_HEADER  *HestHeaderPtr;
  EFI_STATUS                                       Status;
  UINT32                                           NewTableSize;
  VOID                                             *ErrorDescriptorPtr;

  if (  (ErrorSourceDescriptorList == NULL)
     || (ErrorSourceDescriptorListSize == 0))
  {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Create a HEST table header if not already created.
  //
  if (mHestDriverData.HestTable == NULL) {
    Status = BuildHestHeader ();
    if (Status == EFI_OUT_OF_RESOURCES) {
      DEBUG ((
        DEBUG_ERROR,
        "HestDxe: Failed to build HEST header, status: %r\n",
        Status
        ));
      return Status;
    }
  }

  //
  // Resize the existing HEST table buffer to accommodate the incoming error source
  // descriptors.
  //
  NewTableSize = mHestDriverData.CurrentTableSize +
                 ErrorSourceDescriptorListSize;
  mHestDriverData.HestTable = ReallocateHestTableMemory (
                                mHestDriverData.CurrentTableSize,
                                NewTableSize,
                                mHestDriverData.HestTable
                                );
  if (mHestDriverData.HestTable == NULL) {
    DEBUG ((DEBUG_ERROR, "HestDxe: Failed to reallocate memory for HEST table\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Copy the incoming error source descriptors into HEST table.
  //
  ErrorDescriptorPtr = (VOID *)mHestDriverData.HestTable +
                       mHestDriverData.CurrentTableSize;
  HestHeaderPtr = (EFI_ACPI_6_3_HARDWARE_ERROR_SOURCE_TABLE_HEADER *)
                  mHestDriverData.HestTable;
  CopyMem (
    ErrorDescriptorPtr,
    ErrorSourceDescriptorList,
    ErrorSourceDescriptorListSize
    );
  mHestDriverData.CurrentTableSize = NewTableSize;
  HestHeaderPtr->Header.Length     = mHestDriverData.CurrentTableSize;
  HestHeaderPtr->ErrorSourceCount += ErrorSourceDescriptorCount;

  DEBUG ((
    DEBUG_INFO,
    "HestDxe: %d Error source descriptor(s) added \n",
    ErrorSourceDescriptorCount
    ));
  return EFI_SUCCESS;
}

/**
  Install HEST ACPI table protocol service.

  @retval EFI_SUCCESS ACPI HEST table is installed successfully.
  @retval EFI_ABORTED HEST table is NULL.
  @retval Other       Install HEST ACPI table failed.

**/
STATIC
EFI_STATUS
EFIAPI
InstallHestAcpiTable (
  VOID
  )
{
  EFI_ACPI_6_3_HARDWARE_ERROR_SOURCE_TABLE_HEADER  *HestHeader;
  EFI_STATUS                                       Status;
  UINTN                                            AcpiTableHandle;

  //
  // Check if we have valid HEST table data.
  //
  if (mHestDriverData.HestTable == NULL) {
    DEBUG ((DEBUG_ERROR, "HestDxe: No data available to generate HEST table\n"));
    return EFI_ABORTED;
  }

  //
  // Valid data for HEST table found. Update the header checksum and install the
  // HEST table.
  //
  HestHeader = (EFI_ACPI_6_3_HARDWARE_ERROR_SOURCE_TABLE_HEADER *)
               mHestDriverData.HestTable;
  HestHeader->Header.Checksum = CalculateCheckSum8 (
                                  (UINT8 *)HestHeader,
                                  HestHeader->Header.Length
                                  );

  Status = mAcpiTableProtocol->InstallAcpiTable (
                                 mAcpiTableProtocol,
                                 HestHeader,
                                 HestHeader->Header.Length,
                                 &AcpiTableHandle
                                 );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "HestDxe: HEST ACPI table installation failed, status: %r\n",
      Status
      ));
    return Status;
  }

  //
  // Free the HEST table buffer.
  //
  FreePool (mHestDriverData.HestTable);
  DEBUG ((DEBUG_INFO, "HestDxe: Installed HEST ACPI table \n"));
  return EFI_SUCCESS;
}

//
// HEST table generation protocol instance.
//
STATIC HEST_TABLE_PROTOCOL  mHestProtocol = {
  AppendErrorSourceDescriptor,
  InstallHestAcpiTable
};

/**
  The Entry Point for HEST DXE driver.

  This function installs the HEST table creation and installation protocol
  services.

  @param[in] ImageHandle Handle to the EFI image.
  @param[in] SystemTable A pointer to the EFI System Table.

  @retval EFI_SUCCESS    On successful installation of protocol services and
                         location the Acpi table protocol.
  @retval Other          On Failure to locate ACPI table protocol or install
                         of HEST table generation protocol.

**/
EFI_STATUS
EFIAPI
HestInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE  Handle = NULL;
  EFI_STATUS  Status;

  Status = gBS->LocateProtocol (
                  &gEfiAcpiTableProtocolGuid,
                  NULL,
                  (VOID **)&mAcpiTableProtocol
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "HestDxe: Failed to locate ACPI table protocol, status: %r\n",
      Status
      ));
    return Status;
  }

  Status = gBS->InstallProtocolInterface (
                  &Handle,
                  &gHestTableProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mHestProtocol
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "HestDxe: Failed to install HEST table generation protocol status: %r\n",
      Status
      ));
    return Status;
  }

  return EFI_SUCCESS;
}

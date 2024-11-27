/** @file
  Entry point to the Secure Partition when initialized during the SEC
  phase on ARM platforms

Copyright (c) 2017 - 2021, Arm Ltd. All rights reserved.<BR>
Copyright (c), Microsoft Corporation.
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiMm.h>

#include <Library/Arm/StandaloneMmCoreEntryPoint.h>

#include <PiPei.h>
#include <Guid/MmramMemoryReserve.h>
#include <Guid/MpInformation.h>

#include <libfdt.h>
#include <Library/ArmMmuLib.h>
#include <Library/ArmSvcLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/SerialPortLib.h>
#include <Library/ArmStandaloneMmMmuLib.h>
#include <Library/SecurePartitionServicesTableLib.h>
#include <Library/PcdLib.h>

#include <IndustryStandard/ArmStdSmc.h>
#include <IndustryStandard/ArmMmSvc.h>
#include <IndustryStandard/ArmFfaSvc.h>
#include <IndustryStandard/ArmFfaBootInfo.h>

#define FFA_PAGE_4K   0
#define FFA_PAGE_16K  1
#define FFA_PAGE_64K  2

// Materialize the Secure Partition Services Table
SECURE_PARTITION_SERVICES_TABLE  mSpst = {
  .FDTAddress = NULL
};

/**
  This structure is used to stage boot information required to initialize the
  standalone MM environment when FF-A is used as the interface between this
  secure partition and the SPMC. This structure supersedes
  EFI_SECURE_PARTITION_BOOT_INFO and reduces the amount of information that must
  be passed by the SPMC for SP initialization.
**/
typedef struct {
  UINT64    SpMemBase;
  UINT64    SpMemSize;
  UINT64    SpHeapBase;
  UINT64    SpHeapSize;
} SP_BOOT_INFO;

/**
  An StMM SP implements partial support for FF-A v1.0. The FF-A ABIs are used to
  get and set permissions of memory pages in collaboration with the SPMC and
  signalling completion of initialisation. The original Arm MM communication
  interface is used for communication with the Normal world. A TF-A specific
  interface is used for initialising the SP.

  With FF-A v1.1, the StMM SP uses only FF-A ABIs for initialisation and
  communication. This is subject to support for FF-A v1.1 in the SPMC. If this
  is not the case, the StMM implementation reverts to the FF-A v1.0
  behaviour. Any of this is applicable only if the feature flag PcdFfaEnable is
  TRUE.

  This function helps the caller determine whether FF-A v1.1 or v1.0 are
  available and if only FF-A ABIs can be used at runtime.
**/
STATIC
EFI_STATUS
CheckFfaCompatibility (
  BOOLEAN  *UseOnlyFfaAbis
  )
{
  UINT16      SpmcMajorVer;
  UINT16      SpmcMinorVer;
  EFI_STATUS  Status;

  Status = ArmFfaLibVersion (
             ARM_FFA_MAJOR_VERSION,
             ARM_FFA_MINOR_VERSION,
             &SpmcMajorVer,
             &SpmcMinorVer
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // If the major versions differ then all bets are off.
  if (SpmcMajorVer != ARM_FFA_MAJOR_VERSION) {
    return EFI_UNSUPPORTED;
  }

  // We advertised v1.1 as our version. If the SPMC supports it, it must return
  // the same or a compatible version. If it does not then FF-A ABIs cannot be
  // used for all communication.
  if (SpmcMinorVer >= ARM_FFA_MINOR_VERSION) {
    *UseOnlyFfaAbis = TRUE;
  } else {
    *UseOnlyFfaAbis = FALSE;
  }

  // We have validated that there is a compatible FF-A
  // implementation. So. return success.
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ReadProperty32 (
  IN  VOID    *DtbAddress,
  IN  INT32   Offset,
  IN  CHAR8   *Property,
  OUT UINT32  *Value
  )
{
  CONST UINT32  *Property32;

  Property32 =  fdt_getprop (DtbAddress, Offset, Property, NULL);
  if (Property32 == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "%s: Missing in FF-A boot information manifest\n",
      Property
      ));
    return EFI_INVALID_PARAMETER;
  }

  *Value = fdt32_to_cpu (*Property32);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ReadProperty64 (
  IN  VOID    *DtbAddress,
  IN  INT32   Offset,
  IN  CHAR8   *Property,
  OUT UINT64  *Value
  )
{
  CONST UINT64  *Property64;

  Property64 =  fdt_getprop (DtbAddress, Offset, Property, NULL);
  if (Property64 == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "%s: Missing in FF-A boot information manifest\n",
      Property
      ));
    return EFI_INVALID_PARAMETER;
  }

  *Value = fdt64_to_cpu (*Property64);

  return EFI_SUCCESS;
}

/**

  Populates FF-A boot information structure.

  This function receives the address of a DTB from which boot information defind
  by FF-A and required to initialize the standalone environment is extracted.

  @param [in, out] SpBootInfo    Pointer to a pre-allocated boot info structure to be
                                 populated.
  @param [in]      DtbAddress    Address of the Device tree from where boot
                                 information will be fetched.
**/
STATIC
EFI_STATUS
PopulateBootinformation (
  IN  OUT  SP_BOOT_INFO  *SpBootInfo,
  IN       VOID          *DtbAddress
  )
{
  INTN    Status;
  INT32   Offset;
  UINT64  MemBase;
  UINT32  EntryPointOffset;
  UINT32  PageSize;

  Offset = fdt_node_offset_by_compatible (DtbAddress, -1, "arm,ffa-manifest-1.0");

  DEBUG ((DEBUG_INFO, "Offset  = %d \n", Offset));
  if (Offset < 0) {
    DEBUG ((DEBUG_ERROR, "Missing FF-A boot information in manifest\n"));
    return EFI_NOT_FOUND;
  }

  Status = ReadProperty64 (
             DtbAddress,
             Offset,
             "load-address",
             &MemBase
             );
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  Status = ReadProperty32 (
             DtbAddress,
             Offset,
             "entrypoint-offset",
             &EntryPointOffset
             );

  SpBootInfo->SpMemBase = MemBase + EntryPointOffset;
  DEBUG ((DEBUG_INFO, "sp mem base  = 0x%llx\n", SpBootInfo->SpMemBase));

  Status = ReadProperty64 (
             DtbAddress,
             Offset,
             "image-size",
             &SpBootInfo->SpMemSize
             );
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  DEBUG ((DEBUG_INFO, "sp mem size  = 0x%llx\n", SpBootInfo->SpMemSize));

  Status = ReadProperty32 (DtbAddress, Offset, "xlat-granule", &PageSize);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  /*  EFI_PAGE_SIZE is 4KB */
  switch (PageSize) {
    case FFA_PAGE_4K:
      PageSize = EFI_PAGE_SIZE;
      break;

    case FFA_PAGE_16K:
      PageSize = 4 * EFI_PAGE_SIZE;
      break;

    case FFA_PAGE_64K:
      PageSize = 16 * EFI_PAGE_SIZE;
      break;

    default:
      DEBUG ((DEBUG_ERROR, "Invalid page type = %lu\n", PageSize));
      return EFI_INVALID_PARAMETER;
      break;
  }

  DEBUG ((DEBUG_INFO, "Page Size = 0x%lx\n", PageSize));

  DEBUG ((DEBUG_WARN, "Skip heap buffer info for non stmm secure partitions\n"));

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
GetSpManifest (
  IN  OUT     UINT64  **SpManifestAddr,
  IN          VOID    *BootInfoAddr
  )
{
  EFI_FFA_BOOT_INFO_HEADER  *FfaBootInfo;
  EFI_FFA_BOOT_INFO_DESC    *FfaBootInfoDesc;

  // Paranoid check to avoid an inadvertent NULL pointer dereference.
  if (BootInfoAddr == NULL) {
    DEBUG ((DEBUG_ERROR, "FF-A Boot information is NULL\n"));
    return EFI_INVALID_PARAMETER;
  }

  // Check boot information magic number.
  FfaBootInfo = (EFI_FFA_BOOT_INFO_HEADER *)BootInfoAddr;
  if (FfaBootInfo->Magic != FFA_BOOT_INFO_SIGNATURE) {
    DEBUG ((
      DEBUG_ERROR,
      "FfaBootInfo Magic no. is invalid 0x%ux\n",
      FfaBootInfo->Magic
      ));
    return EFI_INVALID_PARAMETER;
  }

  FfaBootInfoDesc =
    (EFI_FFA_BOOT_INFO_DESC *)((UINT8 *)BootInfoAddr +
                               FfaBootInfo->OffsetBootInfoDesc);

  if (FfaBootInfoDesc->Type ==
      (FFA_BOOT_INFO_TYPE (FFA_BOOT_INFO_TYPE_STD) |
       FFA_BOOT_INFO_TYPE_ID (FFA_BOOT_INFO_TYPE_ID_FDT)))
  {
    *SpManifestAddr = (UINT64 *)FfaBootInfoDesc->Content;
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_ERROR, "SP manifest not found \n"));
  return EFI_NOT_FOUND;
}

/**
  The entry point of Standalone MM Foundation.

  @param  [in]  SharedBufAddress  Pointer to the Buffer between SPM and SP.
  @param  [in]  SharedBufSize     Size of the shared buffer.
  @param  [in]  cookie1           Cookie 1
  @param  [in]  cookie2           Cookie 2

**/
VOID
EFIAPI
ModuleEntryPoint (
  IN VOID    *SharedBufAddress,
  IN UINT64  SharedBufSize,
  IN UINT64  cookie1,
  IN UINT64  cookie2
  )
{
  PE_COFF_LOADER_IMAGE_CONTEXT  ImageContext;
  SP_BOOT_INFO                  SpBootInfo = { 0 };
  EFI_STATUS                    Status;
  INT32                         Ret;
  UINT32                        SectionHeaderOffset;
  UINT16                        NumberOfSections;
  VOID                          *TeData;
  UINTN                         TeDataSize;
  EFI_PHYSICAL_ADDRESS          ImageBase;
  UINT64                        *DtbAddress;
  EFI_FIRMWARE_VOLUME_HEADER    *BfvAddress;
  BOOLEAN                       UseOnlyFfaAbis = FALSE;

  Status = CheckFfaCompatibility (&UseOnlyFfaAbis);
  if (EFI_ERROR (Status) || !UseOnlyFfaAbis) {
    goto finish;
  }

  // If only FF-A is used, the DTB address is passed in the Boot information
  // structure. Else, the Boot info is copied from Sharedbuffer.
  Status = GetSpManifest (&DtbAddress, SharedBufAddress);
  if (Status != EFI_SUCCESS) {
    goto finish;
  }

  // Extract boot information from the DTB
  Status = PopulateBootinformation (&SpBootInfo, (VOID *)DtbAddress);
  if (Status != EFI_SUCCESS) {
    goto finish;
  }

  // Stash the base address of the boot firmware volume
  BfvAddress = (EFI_FIRMWARE_VOLUME_HEADER *)SpBootInfo.SpMemBase;

  // Locate PE/COFF File information for the Standalone MM core module
  Status = LocateStandaloneMmCorePeCoffData (BfvAddress, &TeData, &TeDataSize);

  if (EFI_ERROR (Status)) {
    goto finish;
  }

  // Obtain the PE/COFF Section information for the Standalone MM core module
  Status = GetStandaloneMmCorePeCoffSections (
             TeData,
             &ImageContext,
             &ImageBase,
             &SectionHeaderOffset,
             &NumberOfSections
             );

  if (EFI_ERROR (Status)) {
    goto finish;
  }

  //
  // ImageBase may deviate from ImageContext.ImageAddress if we are dealing
  // with a TE image, in which case the latter points to the actual offset
  // of the image, whereas ImageBase refers to the address where the image
  // would start if the stripped PE headers were still in place. In either
  // case, we need to fix up ImageBase so it refers to the actual current
  // load address.
  //
  ImageBase += (UINTN)TeData - ImageContext.ImageAddress;

  // Update the memory access permissions of individual sections in the
  // Standalone MM core module
  Status = UpdateMmFoundationPeCoffPermissions (
             &ImageContext,
             ImageBase,
             SectionHeaderOffset,
             NumberOfSections,
             ArmSetMemoryRegionNoExec,
             ArmSetMemoryRegionReadOnly,
             ArmClearMemoryRegionReadOnly
             );

  if (EFI_ERROR (Status)) {
    goto finish;
  }

  // Now that we can update globals, initialize the SPST for other libraries
  mSpst.FDTAddress = DtbAddress;
  gSpst            = &mSpst;

  if (ImageContext.ImageAddress != (UINTN)TeData) {
    ImageContext.ImageAddress = (UINTN)TeData;
    ArmSetMemoryRegionNoExec (ImageBase, SIZE_4KB);
    ArmClearMemoryRegionReadOnly (ImageBase, SIZE_4KB);

    Status = PeCoffLoaderRelocateImage (&ImageContext);
    ASSERT_EFI_ERROR (Status);
  }

  ProcessLibraryConstructorList (NULL, NULL);

  ProcessLibraryConstructorList (NULL, NULL);

  //
  // Call the MM Core entry point
  //
  ProcessModuleEntryPointList (NULL);

finish:
  if (Status == RETURN_UNSUPPORTED) {
    Ret = -1;
  } else if (Status == RETURN_INVALID_PARAMETER) {
    Ret = -2;
  } else if (Status == EFI_NOT_FOUND) {
    Ret = -7;
  } else {
    Ret = 0;
  }
}

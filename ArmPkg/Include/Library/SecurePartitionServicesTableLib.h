/** @file
  Provides a service to retrieve a pointer to the EFI Boot Services Table.
  Only available to DXE and UEFI module types.

Copyright (c) 2006 - 2008, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef SECURE_PARTITION_SERVICES_TABLE_LIB_H_
#define SECURE_PARTITION_SERVICES_TABLE_LIB_H_

typedef struct {
  ///
  /// The table header for the EFI Boot Services Table.
  ///
  VOID    *FDTAddress;
} SECURE_PARTITION_SERVICES_TABLE;

extern SECURE_PARTITION_SERVICES_TABLE  *gSpst;

#endif

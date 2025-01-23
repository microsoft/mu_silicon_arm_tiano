/** @file
  Implementation for the TPM Service State Translation Library. This library's
  main purpose is to translate the states of the TPM service's CRB states to
  the main TPM's interface states. (i.e. TPM PC CRB -> TPM FIFO) A user of the
  TPM service should only need to update this library with the proper TPM
  interface type for their device.

  Copyright (c) 2013 - 2018, Intel Corporation. All rights reserved.<BR>
  (C) Copyright 2015 Hewlett Packard Enterprise Development LP<BR>
  Copyright (c), Microsoft Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/DebugLib.h>
#include <Library/TpmServiceStateTranslationLib.h>
#include <Library/Tpm2DebugLib.h>
#include <IndustryStandard/Tpm20.h>

/* TPM Service State Translation Library Defines */
#define INTERFACE_TYPE_MASK  (0x00F)
#define IDLE_BYPASS_MASK     (0x200)

#define LOCALITY_OFFSET  (0x1000)

#define DELAY_AMOUNT  (30)

#define PTP_TIMEOUT_MAX  (90000 * 1000) // 90s

/* TPM Service State Translation Library Variables */
STATIC BOOLEAN  mIsCrbInterface;
STATIC BOOLEAN  mIsIdleBypassSupported;

/* TPM Service State Translation Library Static Functions */

/**
  Returns the BurstCount from the ExternalFifo

  @param  ExternalFifo   The Fifo registers to read from
  @param  BurstCount     The value of the BurstCount to populate

  @retval EFI_SUCCESS  Success
  @retval EFI_TIMEOUT  Timeout

**/
STATIC
EFI_STATUS
FifoReadBurstCount (
  PTP_FIFO_REGISTERS_PTR  ExternalFifo,
  UINT16                  *BurstCount
  )
{
  UINT32  Timeout;
  UINT8   DataByte0;
  UINT8   DataByte1;

  Timeout = 0;
  do {
    DataByte0   = MmioRead8 ((UINTN)&ExternalFifo->BurstCount);
    DataByte1   = MmioRead8 ((UINTN)&ExternalFifo->BurstCount + 1);
    *BurstCount = (UINT16)((DataByte1 << 8) + DataByte0);
    if (*BurstCount != 0) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (DELAY_AMOUNT);
    Timeout += DELAY_AMOUNT;
  } while (Timeout < PTP_TIMEOUT_D);

  return EFI_TIMEOUT;
}

/**
  Determines whether the value of the provided register matches expectations.

  @param  Register   The register to validate
  @param  BitSet     Bits to check against that should be set
  @param  BitClear   Bits to check against that should be clear
  @param  Timeout    Amount of time to wait

  @retval EFI_SUCCESS  Success
  @retval EFI_TIMEOUT  Timeout

**/
STATIC
EFI_STATUS
WaitRegisterBits (
  UINT32  *Register,
  UINT32  BitSet,
  UINT32  BitClear,
  UINT32  Timeout
  )
{
  UINT32  RegRead;
  UINT32  WaitTime;

  /* Attempt to read the register based on the TPM type. */
  for (WaitTime = 0; WaitTime < Timeout; WaitTime += DELAY_AMOUNT) {
    if (mIsCrbInterface) {
      RegRead = MmioRead32 ((UINTN)Register);
    } else {
      RegRead = (UINT32)MmioRead8 ((UINTN)Register);
    }

    /* Verify the register contents. */
    if (((RegRead & BitSet) == BitSet) && ((RegRead & BitClear) == 0)) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (DELAY_AMOUNT);
  }

  return EFI_TIMEOUT;
}

/**
  Copies command data to the TPM

  @param  Locality          The locality to copy to
  @param  TpmCommandBuffer  The command buffer containing the data
  @param  CommandDataLen    The length of the command data

  @retval EFI_SUCCESS  Success
  @retval EFI_TIMEOUT  Timeout

**/
STATIC
EFI_STATUS
CopyCommandData (
  UINT8   Locality,
  UINT8   *TpmCommandBuffer,
  UINT32  CommandDataLen
  )
{
  EFI_STATUS              Status;
  PTP_CRB_REGISTERS_PTR   ExternalCrb;
  PTP_FIFO_REGISTERS_PTR  ExternalFifo;
  UINT32                  Index;
  UINT16                  BurstCount;

  /* Determine which TPM structure to access */
  if (mIsCrbInterface) {
    ExternalCrb = (PTP_CRB_REGISTERS_PTR)(UINTN)(PcdGet64 (PcdTpmBaseAddress) + (Locality * LOCALITY_OFFSET));

    /* Copy the command data to the CRB buffer. */
    for (Index = 0; Index < CommandDataLen; Index++) {
      MmioWrite8 ((UINTN)&ExternalCrb->CrbDataBuffer[Index], TpmCommandBuffer[Index]);
    }

    /* Setup the CRB buffer addresses and sizes. */
    MmioWrite32 ((UINTN)&ExternalCrb->CrbControlCommandAddressHigh, (UINT32)RShiftU64 ((UINTN)ExternalCrb->CrbDataBuffer, 32));
    MmioWrite32 ((UINTN)&ExternalCrb->CrbControlCommandAddressLow, (UINT32)(UINTN)ExternalCrb->CrbDataBuffer);
    MmioWrite32 ((UINTN)&ExternalCrb->CrbControlCommandSize, sizeof (ExternalCrb->CrbDataBuffer));

    MmioWrite64 ((UINTN)&ExternalCrb->CrbControlResponseAddrss, (UINT32)(UINTN)ExternalCrb->CrbDataBuffer);
    MmioWrite32 ((UINTN)&ExternalCrb->CrbControlResponseSize, sizeof (ExternalCrb->CrbDataBuffer));

    Status = EFI_SUCCESS;
  } else {
    ExternalFifo = (PTP_FIFO_REGISTERS_PTR)(UINTN)(PcdGet64 (PcdTpmBaseAddress) + (Locality * LOCALITY_OFFSET));

    /* Copy the command data to the FIFO depending on the burst count. */
    Index = 0;
    while (Index < CommandDataLen) {
      Status = FifoReadBurstCount (ExternalFifo, &BurstCount);
      if (EFI_ERROR (Status)) {
        break;
      }

      while (BurstCount > 0 && Index < CommandDataLen) {
        MmioWrite8 ((UINTN)&ExternalFifo->DataFifo, TpmCommandBuffer[Index]);
        Index++;
        BurstCount--;
      }
    }

    /* Check to make sure the STS_EXPECT register changed from 1 to 0. */
    Status = WaitRegisterBits (
               (UINT32 *)&ExternalFifo->Status,
               PTP_FIFO_STS_VALID,
               PTP_FIFO_STS_EXPECT,
               PTP_TIMEOUT_C
               );
  }

  return Status;
}

/**
  Initiates or starts the command execution

  @param  Locality  The locality to begin command execution for

  @retval EFI_SUCCESS  Success
  @retval EFI_TIMEOUT  Timeout

**/
STATIC
EFI_STATUS
StartCommand (
  UINT8  Locality
  )
{
  EFI_STATUS              Status;
  PTP_CRB_REGISTERS_PTR   ExternalCrb;
  PTP_FIFO_REGISTERS_PTR  ExternalFifo;

  /* Determine which TPM structure to access */
  if (mIsCrbInterface) {
    ExternalCrb = (PTP_CRB_REGISTERS_PTR)(UINTN)(PcdGet64 (PcdTpmBaseAddress) + (Locality * LOCALITY_OFFSET));

    MmioWrite32 ((UINTN)&ExternalCrb->CrbControlStart, PTP_CRB_CONTROL_START);
    Status = WaitRegisterBits (
               &ExternalCrb->CrbControlStart,
               0,
               PTP_CRB_CONTROL_START,
               PTP_TIMEOUT_MAX
               );
  } else {
    ExternalFifo = (PTP_FIFO_REGISTERS_PTR)(UINTN)(PcdGet64 (PcdTpmBaseAddress) + (Locality * LOCALITY_OFFSET));

    /* Set the tpmGo bit in the Status register. */
    MmioWrite8 ((UINTN)&ExternalFifo->Status, PTP_FIFO_STS_GO);
    Status = WaitRegisterBits (
               (UINT32 *)&ExternalFifo->Status,
               (PTP_FIFO_STS_VALID | PTP_FIFO_STS_DATA),
               0,
               PTP_TIMEOUT_MAX
               );
  }

  return Status;
}

/**
  Retrieves the response header

  @param  Locality          The locality to read from
  @param  TpmCommandBuffer  The TPM command buffer to populate
  @param  ResponseDataLen   The length of the response

  @retval EFI_SUCCESS           Success
  @retval EFI_TIMEOUT           Timeout
  @retval EFI_UNSUPPORTED       Unsupported command type
  @retval EFI_BUFFER_TOO_SMALL  Response buffer too small

**/
STATIC
EFI_STATUS
GetResponseHeader (
  UINT8   Locality,
  UINT8   *TpmCommandBuffer,
  UINT32  *ResponseDataLen
  )
{
  EFI_STATUS              Status;
  PTP_CRB_REGISTERS_PTR   ExternalCrb;
  PTP_FIFO_REGISTERS_PTR  ExternalFifo;
  UINT32                  Index;
  UINT16                  BurstCount;
  UINT32                  TpmOutSize;
  UINT16                  Data16;
  UINT32                  Data32;

  /* Determine which TPM structure to access */
  if (mIsCrbInterface) {
    ExternalCrb = (PTP_CRB_REGISTERS_PTR)(UINTN)(PcdGet64 (PcdTpmBaseAddress) + (Locality * LOCALITY_OFFSET));

    for (Index = 0; Index < sizeof (TPM2_RESPONSE_HEADER); Index++) {
      TpmCommandBuffer[Index] = MmioRead8 ((UINTN)&ExternalCrb->CrbDataBuffer[Index]);
    }

    Status = EFI_SUCCESS;
  } else {
    ExternalFifo = (PTP_FIFO_REGISTERS_PTR)(UINTN)(PcdGet64 (PcdTpmBaseAddress) + (Locality * LOCALITY_OFFSET));

    Index      = 0;
    BurstCount = 0;
    while (Index < sizeof (TPM2_RESPONSE_HEADER)) {
      Status = FifoReadBurstCount (ExternalFifo, &BurstCount);
      if (EFI_ERROR (Status)) {
        goto Exit;
      }

      while (BurstCount > 0) {
        TpmCommandBuffer[Index] = MmioRead8 ((UINTN)&ExternalFifo->DataFifo);
        Index++;
        BurstCount--;
        if (Index == sizeof (TPM2_RESPONSE_HEADER)) {
          Status = EFI_SUCCESS;
          break;
        }
      }
    }
  }

  /* TPM2 should not use RSP_COMMAND. */
  CopyMem (&Data16, TpmCommandBuffer, sizeof (UINT16));
  if (SwapBytes16 (Data16) == TPM_ST_RSP_COMMAND) {
    DEBUG ((DEBUG_ERROR, "TPM2: TPM_ST_RSP error - %x\n", TPM_ST_RSP_COMMAND));
    Status = EFI_UNSUPPORTED;
    goto Exit;
  }

  CopyMem (&Data32, (TpmCommandBuffer + 2), sizeof (UINT32));
  TpmOutSize = SwapBytes32 (Data32);
  if (*ResponseDataLen < TpmOutSize) {
    Status = EFI_BUFFER_TOO_SMALL;
    goto Exit;
  }

  *ResponseDataLen = TpmOutSize;

Exit:

  return Status;
}

/**
  Retrieves the response data

  @param  Locality          The locality to read from
  @param  TpmCommandBuffer  The TPM command buffer to populate
  @param  ResponseDataLen   The length of the response

  @retval EFI_SUCCESS  Success
  @retval EFI_TIMEOUT  Timeout

**/
STATIC
EFI_STATUS
CopyResponseData (
  UINT8   Locality,
  UINT8   *TpmCommandBuffer,
  UINT32  ResponseDataLen
  )
{
  EFI_STATUS              Status;
  PTP_CRB_REGISTERS_PTR   ExternalCrb;
  PTP_FIFO_REGISTERS_PTR  ExternalFifo;
  UINT32                  Index;
  UINT16                  BurstCount;

  /* Determine which TPM structure to access */
  if (mIsCrbInterface) {
    ExternalCrb = (PTP_CRB_REGISTERS_PTR)(UINTN)(PcdGet64 (PcdTpmBaseAddress) + (Locality * LOCALITY_OFFSET));

    for (Index = sizeof (TPM2_RESPONSE_HEADER); Index < ResponseDataLen; Index++) {
      TpmCommandBuffer[Index] = MmioRead8 ((UINTN)&ExternalCrb->CrbDataBuffer[Index]);
    }

    Status = EFI_SUCCESS;
  } else {
    ExternalFifo = (PTP_FIFO_REGISTERS_PTR)(UINTN)(PcdGet64 (PcdTpmBaseAddress) + (Locality * LOCALITY_OFFSET));

    Index = sizeof (TPM2_RESPONSE_HEADER);
    while (Index < ResponseDataLen) {
      Status = FifoReadBurstCount (ExternalFifo, &BurstCount);
      if (EFI_ERROR (Status)) {
        break;
      }

      while (BurstCount > 0) {
        TpmCommandBuffer[Index] = MmioRead8 ((UINTN)&ExternalFifo->DataFifo);
        Index++;
        BurstCount--;
        if (Index == ResponseDataLen) {
          Status = EFI_SUCCESS;
          break;
        }
      }
    }
  }

  return Status;
}

/* TPM Service State Translation Library Global Functions */

/**
  Initiates the transition to the Idle state

  @param  Locality The locality of the TPM to set into Idle

  @retval EFI_SUCCESS  Success
  @retval EFI_TIMEOUT  Timeout

**/
EFI_STATUS
TpmSstGoIdle (
  UINT8  Locality
  )
{
  EFI_STATUS              Status;
  PTP_CRB_REGISTERS_PTR   ExternalCrb;
  PTP_FIFO_REGISTERS_PTR  ExternalFifo;

  /* Determine which TPM structure to access */
  if (mIsCrbInterface) {
    ExternalCrb = (PTP_CRB_REGISTERS_PTR)(UINTN)(PcdGet64 (PcdTpmBaseAddress) + (Locality * LOCALITY_OFFSET));

    /* Set the goIdle bit in the CRB Control Request register. Wait for it to clear and then check
     * the CRB Control Area Status register to make sure the tpmIdle bit was set. */
    MmioWrite32 ((UINTN)&ExternalCrb->CrbControlRequest, PTP_CRB_CONTROL_AREA_REQUEST_GO_IDLE);
    Status = WaitRegisterBits (
               &ExternalCrb->CrbControlRequest,
               0,
               PTP_CRB_CONTROL_AREA_REQUEST_GO_IDLE,
               PTP_TIMEOUT_C
               );
    if (Status == EFI_SUCCESS) {
      Status = WaitRegisterBits (
                 &ExternalCrb->CrbControlStatus,
                 PTP_CRB_CONTROL_AREA_STATUS_TPM_IDLE,
                 0,
                 PTP_TIMEOUT_C
                 );
    }

    /* Note that there is no goIdle in the FIFO TPM implementation. Going idle is the same as commandReady. */
  } else {
    ExternalFifo = (PTP_FIFO_REGISTERS_PTR)(UINTN)(PcdGet64 (PcdTpmBaseAddress) + (Locality * LOCALITY_OFFSET));

    /* Set the commandReady bit in the Status register. Read it back and verify it is set which
     * indicates the TPM is ready. */
    MmioWrite8 ((UINTN)&ExternalFifo->Status, PTP_FIFO_STS_READY);
    Status = WaitRegisterBits (
               (UINT32 *)&ExternalFifo->Status,
               PTP_FIFO_STS_READY,
               0,
               PTP_TIMEOUT_B
               );
  }

  return Status;
}

/**
  Initiates the transition to the commandReady state

  @param  Locality  The locality of the TPM to set to commandReady

  @retval EFI_SUCCESS  Success
  @retval EFI_TIMEOUT  Timeout

**/
EFI_STATUS
TpmSstCmdReady (
  UINT8  Locality
  )
{
  EFI_STATUS              Status;
  PTP_CRB_REGISTERS_PTR   ExternalCrb;
  PTP_FIFO_REGISTERS_PTR  ExternalFifo;

  /* Determine which TPM structure to access */
  if (mIsCrbInterface) {
    ExternalCrb = (PTP_CRB_REGISTERS_PTR)(UINTN)(PcdGet64 (PcdTpmBaseAddress) + (Locality * LOCALITY_OFFSET));

    /* Set the cmdReady bit in the CRB Control Request register. Wait for it to clear and then check
     * the CRB Control Area Status register to make sure the tpmIdle bit was cleared. */
    MmioWrite32 ((UINTN)&ExternalCrb->CrbControlRequest, PTP_CRB_CONTROL_AREA_REQUEST_COMMAND_READY);
    Status = WaitRegisterBits (
               &ExternalCrb->CrbControlRequest,
               0,
               PTP_CRB_CONTROL_AREA_REQUEST_COMMAND_READY,
               PTP_TIMEOUT_C
               );
    if (Status == EFI_SUCCESS) {
      Status = WaitRegisterBits (
                 &ExternalCrb->CrbControlStatus,
                 0,
                 PTP_CRB_CONTROL_AREA_STATUS_TPM_IDLE,
                 PTP_TIMEOUT_C
                 );
    }
  } else {
    ExternalFifo = (PTP_FIFO_REGISTERS_PTR)(UINTN)(PcdGet64 (PcdTpmBaseAddress) + (Locality * LOCALITY_OFFSET));

    /* Set the commandReady bit in the Status register. Read it back and verify it is set which
     * indicates the TPM is ready. */
    MmioWrite8 ((UINTN)&ExternalFifo->Status, PTP_FIFO_STS_READY);
    Status = WaitRegisterBits (
               (UINT32 *)&ExternalFifo->Status,
               PTP_FIFO_STS_READY,
               0,
               PTP_TIMEOUT_B
               );
  }

  return Status;
}

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
  )
{
  EFI_STATUS  Status;
  UINT8       TpmCommandBuffer[sizeof (InternalTpmCrb->CrbDataBuffer)];
  UINT32      ResponseDataLen;
  UINT32      CommandDataLen;

  /* Init the local variables. */
  ResponseDataLen = InternalTpmCrb->CrbControlResponseSize;
  CommandDataLen  = InternalTpmCrb->CrbControlCommandSize;
  SetMem (TpmCommandBuffer, sizeof (InternalTpmCrb->CrbDataBuffer), 0);

  /* Copy the CRB command data to the local buffer. */
  CopyMem (TpmCommandBuffer, InternalTpmCrb->CrbDataBuffer, CommandDataLen);

  DEBUG_CODE_BEGIN ();
  DumpTpmInputBlock (CommandDataLen, TpmCommandBuffer);
  DEBUG_CODE_END ();

  /* Copy the command data. */
  Status = CopyCommandData (Locality, TpmCommandBuffer, CommandDataLen);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  /* Start command execution. */
  Status = StartCommand (Locality);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  /* Get the response header. */
  Status = GetResponseHeader (Locality, TpmCommandBuffer, &ResponseDataLen);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  /* Copy the response data. */
  Status = CopyResponseData (Locality, TpmCommandBuffer, ResponseDataLen);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  /* Copy the CRB response data from the local buffer. */
  CopyMem (InternalTpmCrb->CrbDataBuffer, TpmCommandBuffer, ResponseDataLen);

Exit:
  DEBUG_CODE_BEGIN ();
  DumpTpmOutputBlock (ResponseDataLen, TpmCommandBuffer);
  DEBUG_CODE_END ();

  return Status;
}

/**
  Requests access to the given locality

  @param  Locality  The locality to request access to

  @retval EFI_SUCCESS  Success
  @retval EFI_TIMEOUT  Timeout

**/
EFI_STATUS
TpmSstLocalityRequest (
  UINT8  Locality
  )
{
  EFI_STATUS              Status;
  PTP_CRB_REGISTERS_PTR   ExternalCrb;
  PTP_FIFO_REGISTERS_PTR  ExternalFifo;

  /* Determine which TPM structure to access */
  if (mIsCrbInterface) {
    ExternalCrb = (PTP_CRB_REGISTERS_PTR)(UINTN)(PcdGet64 (PcdTpmBaseAddress) + (Locality * LOCALITY_OFFSET));

    MmioWrite32 ((UINTN)&ExternalCrb->LocalityControl, PTP_CRB_LOCALITY_CONTROL_REQUEST_ACCESS);
    Status = WaitRegisterBits (
               &ExternalCrb->LocalityStatus,
               PTP_CRB_LOCALITY_STATUS_GRANTED,
               0,
               PTP_TIMEOUT_A
               );
  } else {
    ExternalFifo = (PTP_FIFO_REGISTERS_PTR)(UINTN)(PcdGet64 (PcdTpmBaseAddress) + (Locality * LOCALITY_OFFSET));

    MmioWrite8 ((UINTN)&ExternalFifo->Access, PTP_FIFO_ACC_RQUUSE);
    Status = WaitRegisterBits (
               (UINT32 *)&ExternalFifo->Access,
               (PTP_FIFO_ACC_ACTIVE | PTP_FIFO_VALID),
               0,
               PTP_TIMEOUT_A
               );
  }

  return Status;
}

/**
  Returns if IdleBypass is supported

  @retval TRUE   Supported
  @retval FALSE  Unsupported

**/
BOOLEAN
TpmSstIsIdleBypassSupported (
  VOID
  )
{
  return mIsIdleBypassSupported;
}

/**
  Initializes the TPM Service State Translation Library

**/
VOID
TpmSstInit (
  VOID
  )
{
  /* Note that the register we are looking at is located at the same address
   * regardless of if the TPM type is FIFO or CRB.  */
  PTP_CRB_REGISTERS_PTR  ExternalCrb;

  /* Need to determine the TPM interface type. */
  ExternalCrb = (PTP_CRB_REGISTERS_PTR)(UINTN)PcdGet64 (PcdTpmBaseAddress);
  if ((ExternalCrb->InterfaceId & INTERFACE_TYPE_MASK) == 1) {
    mIsCrbInterface = TRUE;
  } else {
    mIsCrbInterface = FALSE;
  }

  /* Need to determine if idle bypass is supported. */
  if (ExternalCrb->InterfaceId & IDLE_BYPASS_MASK) {
    mIsIdleBypassSupported = TRUE;
  } else {
    mIsIdleBypassSupported = FALSE;
  }
}

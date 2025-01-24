/** @file SmmuV3.h

    This file is the SmmuV3 header file for SMMU driver compliant with the Smmu spec:

    <https://developer.arm.com/documentation/ihi0070/latest/>

    Copyright (c) Microsoft Corporation.
    SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef SMMUV3_H_
#define SMMUV3_H_

#include <Register/SmmuV3Registers.h>
#include <Uefi/UefiBaseType.h>

// Macro to align values down. Alignment is required to be power of 2.
#define ALIGN_DOWN_BY(length, alignment) \
    ((UINT64)(length) & ~((UINT64)(alignment) - 1))

// Cacheability and Shareability attributes
#define ARM64_RGNCACHEATTR_NONCACHEABLE               0
#define ARM64_RGNCACHEATTR_WRITEBACK_WRITEALLOCATE    1
#define ARM64_RGNCACHEATTR_WRITETHROUGH               2
#define ARM64_RGNCACHEATTR_WRITEBACK_NOWRITEALLOCATE  3

#define ARM64_SHATTR_NON_SHAREABLE    0
#define ARM64_SHATTR_OUTER_SHAREABLE  2
#define ARM64_SHATTR_INNER_SHAREABLE  3

#define SMMUV3_PAGE_1_OFFSET  0x10000

// log2 size of the command queue
#define SMMUV3_COMMAND_QUEUE_LOG2ENTRIES  (8)

//
// Define the size of each entry in the command queue.
//
#define SMMUV3_COMMAND_QUEUE_ENTRY_SIZE  (sizeof(SMMUV3_CMD_GENERIC))

//
// Macros to compute command queue size given its Log2 size.
//
#define SMMUV3_COMMAND_QUEUE_SIZE_FROM_LOG2(QueueLog2Size) \
    ((UINT32)(1UL << (QueueLog2Size)) * \
        (UINT16)(SMMUV3_COMMAND_QUEUE_ENTRY_SIZE))

// log2 size of the event queue
#define SMMUV3_EVENT_QUEUE_LOG2ENTRIES  (7)

//
// Define the size of each entry in the event queue.
//
#define SMMUV3_EVENT_QUEUE_ENTRY_SIZE  (sizeof(SMMUV3_FAULT_RECORD))

//
// Macros to compute event queue size given its Log2 size.
//
#define SMMUV3_EVENT_QUEUE_SIZE_FROM_LOG2(QueueLog2Size) \
    ((UINT32)(1UL << (QueueLog2Size)) * (UINT16)(SMMUV3_EVENT_QUEUE_ENTRY_SIZE))

#define SMMUV3_COUNT_FROM_LOG2(Log2Size)  (1UL << (Log2Size))

//
// Macro to determine if a queue is empty. It is empty if the producer and
// consumer indices are equal and their wrap bits are also equal.
//
#define SMMUV3_IS_QUEUE_EMPTY(ProducerIndex, \
                              ProducerWrap, \
                              ConsumerIndex, \
                              ConsumerWrap) \
                                            \
    (((ProducerIndex) == (ConsumerIndex)) && ((ProducerWrap) == (ConsumerWrap)))

//
// Macro to determine if a queue is full. It is full if the producer and
// consumer indices are equal and their wrap bits are different.
//
#define SMMUV3_IS_QUEUE_FULL(ProducerIndex, \
                             ProducerWrap, \
                             ConsumerIndex, \
                             ConsumerWrap) \
                                           \
    (((ProducerIndex) == (ConsumerIndex)) && ((ProducerWrap) != (ConsumerWrap)))

//
// SMMUV3 Stream Table Entry bit definitions
//
#define SMMUV3_STREAM_TABLE_ENTRY_CPM                                      1     // Coherent Path to Memory
#define SMMUV3_STREAM_TABLE_ENTRY_DACS                                     2     // Device attributes are Cacheable and Inner-Shareable
#define SMMUV3_STREAM_TABLE_ENTRY_CONFIG_STAGE_2_TRANSLATE_STAGE_1_BYPASS  0x6   // Stage 2 Translate, Stage 1 Bypass
#define SMMUV3_STREAM_TABLE_ENTRY_CONFIG_STAGE_2_BYPASS_STAGE_1_BYPASS     0x4   // Stage 2 Bypass, Stage 1 Bypass
#define SMMUV3_STREAM_TABLE_ENTRY_EATS_NOT_SUPPORTED                       0     // ATS not supported
#define SMMUV3_STREAM_TABLE_ENTRY_S2VMID                                   1     // Pick non-zero value
#define SMMUV3_STREAM_TABLE_ENTRY_S2TG_4KB                                 0     // Granule size 4KB
#define SMMUV3_STREAM_TABLE_ENTRY_S2AA64                                   1     // AA64
#define SMMUV3_STREAM_TABLE_ENTRY_S2TTB_OFFSET                             4     // S2TTB offset >> 4
#define SMMUV3_STREAM_TABLE_ENTRY_S2PTW                                    1     // S2PTW bit
#define SMMUV3_STREAM_TABLE_ENTRY_S2SL0                                    2     // Encoded level of page table walk, 2 -> 4-level page table starting from 0
#define SMMUV3_STREAM_TABLE_ENTRY_OUTPUT_ADDRESS_MAX                       48    // 48 bit output address width max
#define SMMUV3_STREAM_TABLE_ENTRY_S2RS_RECORD_FAULTS                       2     // Record faults
#define SMMUV3_STREAM_TABLE_ENTRY_SHCFG_INCOMING_SHAREABILITY              1     // Incoming shareability attribute
#define SMMUV3_STREAM_TABLE_ENTRY_SHCFG_INNER_SHAREABLE                    3     // Inner shareable
#define SMMUV3_STREAM_TABLE_ENTRY_MTCFG                                    1     // MTCFG bit
#define SMMUV3_STREAM_TABLE_ENTRY_MEMATTR_INNER_OUTTER_WRITEBACK_CACHED    0xF   // Inner+Outer write-back cached
#define SMMUV3_STREAM_TABLE_ENTRY_VALID                                    1     // Entry is valid

//
// SMMUV3 Configuration bit definitions
//
#define SMMUV3_STR_TAB_BASE_CFG_FMT_LINEAR  0                // Linear Stream Table format
#define SMMUV3_STR_TAB_BASE_ADDR_OFFSET     6                // Stream Table base address offset
#define SMMUV3_STR_TAB_BASE_CMDQ_OFFSET     5                // Command queue base address offset
#define SMMUV3_STR_TAB_BASE_EVENTQ_OFFSET   5                // Event queue base address offset
#define SMMUV3_CR2_E2H                      0                // E2H bit 0
#define SMMUV3_CR2_REC_INV_SID              1                // Record C_BAD_STREAMID for invalid input streams
#define SMMUV3_CR2_PTM                      1                // PTM bit
#define SMMUV3_CR0_EVENTQ_EN                1                // Event queue enable
#define SMMUV3_CR0_CMDQ_EN                  1                // Command queue enable
#define SMMUV3_CR0_SMMU_EN                  1                // SMMU enable
#define SMMUV3_CR0_EVENTQ_EN                1                // Event queue enable
#define SMMUV3_CR0_CMDQ_EN                  1                // Command queue enable
#define SMMUV3_CR0_PRIQ_EN_DISABLED         0                // Disable PRI queue
#define SMMUV3_CR0_VMW_DISABLED             0                // Disable VMID wildcard matching
#define SMMUV3_CR0_ATS_CHK_DISABLE          1                // Disable bypass for ATS translated traffic

typedef UINT64 PAGE_TABLE_ENTRY;

#define PAGE_TABLE_SIZE  EFI_PAGE_SIZE / sizeof(PAGE_TABLE_ENTRY)  // Number of entries in a page table

typedef enum _SMMU_ADDRESS_SIZE_TYPE {
  SmmuAddressSize32Bit = 0,
  SmmuAddressSize36Bit = 1,
  SmmuAddressSize40Bit = 2,
  SmmuAddressSize42Bit = 3,
  SmmuAddressSize44Bit = 4,
  SmmuAddressSize48Bit = 5,
  SmmuAddressSize52Bit = 6,
} SMMU_ADDRESS_SIZE_TYPE;

// Page Table Structure used by SMMU
typedef struct _PAGE_TABLE {
  PAGE_TABLE_ENTRY    Entries[PAGE_TABLE_SIZE];
} PAGE_TABLE;

// General SMMU Information for a SMMU instance
typedef struct _SMMU_INFO {
  PAGE_TABLE    *PageTableRoot;
  VOID          *StreamTable;
  VOID          *CommandQueue;
  VOID          *EventQueue;
  UINT64        SmmuBase;
  UINT32        StreamTableSize;
  UINT32        CommandQueueSize;
  UINT32        EventQueueSize;
  UINT32        StreamTableLog2Size;
  UINT32        CommandQueueLog2Size;
  UINT32        EventQueueLog2Size;
} SMMU_INFO;

// SMMU instance
extern SMMU_INFO  *mSmmu;

/**
  Decode the address width from the given address size type.

  @param [in]  AddressSizeType  The address size type.

  @return The decoded address width. 0 if the address size type is invalid.
**/
UINT32
SmmuV3DecodeAddressWidth (
  IN UINT32  AddressSizeType
  );

/**
  Encode the address width to the corresponding address size type.

  @param [in]  AddressWidth  The address width.

  @return The encoded address size type. 0 if the address width is invalid.
**/
UINT8
SmmuV3EncodeAddressWidth (
  IN UINT32  AddressWidth
  );

/**
  Read a 32-bit value from the specified SMMU register.

  @param [in]  SmmuBase   The base address of the SMMU.
  @param [in]  Register   The offset of the register.

  @return The 32-bit value read from the register. 0 if the SMMU base address is invalid.
**/
UINT32
SmmuV3ReadRegister32 (
  IN UINT64  SmmuBase,
  IN UINT64  Register
  );

/**
  Read a 64-bit value from the specified SMMU register.

  @param [in]  SmmuBase   The base address of the SMMU.
  @param [in]  Register   The offset of the register.

  @return The 64-bit value read from the register. 0 if the SMMU base address is invalid.
**/
UINT64
SmmuV3ReadRegister64 (
  IN UINT64  SmmuBase,
  IN UINT64  Register
  );

/**
  Write a 32-bit value to the specified SMMU register.

  @param [in]  SmmuBase   The base address of the SMMU.
  @param [in]  Register   The offset of the register.
  @param [in]  Value      The 32-bit value to write.

  @return The 32-bit value written to the register, or 0 if the SMMU base address is invalid.
**/
UINT32
SmmuV3WriteRegister32 (
  IN UINT64  SmmuBase,
  IN UINT64  Register,
  IN UINT32  Value
  );

/**
  Write a 64-bit value to the specified SMMU register.

  @param [in]  SmmuBase   The base address of the SMMU.
  @param [in]  Register   The offset of the register.
  @param [in]  Value      The 64-bit value to write.

  @return The 64-bit value written to the register, or 0 if the SMMU base address is invalid.
**/
UINT64
SmmuV3WriteRegister64 (
  IN UINT64  SmmuBase,
  IN UINT64  Register,
  IN UINT64  Value
  );

/**
  Disable interrupts for the SMMUv3.

  @param [in]  SmmuBase          The base address of the SMMU.
  @param [in]  ClearStaleErrors  Whether to clear stale errors.

  @retval EFI_SUCCESS            Success.
  @retval EFI_INVALID_PARAMETER  Invalid Parameters.
  @retval EFI_TIMEOUT            Timeout.
**/
EFI_STATUS
SmmuV3DisableInterrupts (
  IN UINT64   SmmuBase,
  IN BOOLEAN  ClearStaleErrors
  );

/**
  Enable interrupts for the SMMUv3.

  @param [in]  SmmuBase  The base address of the SMMU.

  @retval EFI_SUCCESS            Success.
  @retval EFI_INVALID_PARAMETER  Invalid Parameters.
  @retval EFI_TIMEOUT            Timeout.
**/
EFI_STATUS
SmmuV3EnableInterrupts (
  IN UINT64  SmmuBase
  );

/**
  Disable translation for the SMMUv3.

  @param [in]  SmmuBase  The base address of the SMMU.

  @retval EFI_SUCCESS            Success.
  @retval EFI_INVALID_PARAMETER  Invalid Parameters.
  @retval EFI_TIMEOUT            Timeout.
**/
EFI_STATUS
SmmuV3DisableTranslation (
  IN UINT64  SmmuBase
  );

/**
  Set the Smmu in ABORT mode and stop DMA.

  @param [in]  SmmuReg    Base address of the SMMUv3.

  @retval EFI_SUCCESS            Success.
  @retval EFI_INVALID_PARAMETER  Invalid Parameters.
  @retval EFI_TIMEOUT            Timeout.
**/
EFI_STATUS
SmmuV3GlobalAbort (
  IN  UINT64  SmmuBase
  );

/**
  Set all streams to bypass the SMMU.

  @param [in]  SmmuReg    Base address of the SMMUv3.

  @retval EFI_SUCCESS            Success.
  @retval EFI_TIMEOUT            Timeout.
  @retval EFI_INVALID_PARAMETER  Invalid Parameters.
**/
EFI_STATUS
SmmuV3SetGlobalBypass (
  IN UINT64  SmmuBase
  );

/**
  Poll the SMMU register and test the value based on the mask.

  @param [in]  SmmuBase   Base address of the SMMU.
  @param [in]  SmmuReg    The SMMU register to poll.
  @param [in]  Mask       Mask of register bits to monitor.
  @param [in]  Value      Expected value.

  @retval EFI_SUCCESS            Success.
  @retval EFI_TIMEOUT            Timeout.
  @retval EFI_INVALID_PARAMETER  Invalid Parameters.
**/
EFI_STATUS
SmmuV3Poll (
  IN UINT64  SmmuBase,
  IN UINT64  SmmuReg,
  IN UINT32  Mask,
  IN UINT32  Value
  );

/**
  Consume the event queue for errors and retrieve the fault record.
  Clears the outputted FaultRecord if the queue is empty.

  @param [in]  SmmuInfo     Pointer to the SMMU_INFO structure.
  @param [out] FaultRecord  Pointer to the fault record structure.
  @param [out] IsEmpty      Flag to indicate if the queue is empty.

  @retval EFI_SUCCESS            Success.
  @retval EFI_TIMEOUT            Timeout.
  @retval EFI_INVALID_PARAMETER  Invalid Parameters.
**/
EFI_STATUS
SmmuV3ConsumeEventQueueForErrors (
  IN SMMU_INFO             *SmmuInfo,
  OUT SMMUV3_FAULT_RECORD  *FaultRecord,
  OUT BOOLEAN              *IsEmpty
  );

/**
  Log the errors if found from the SMMUv3. Prints Event Queue entries and GError register.
  Does nothing if no errors found.

  @param [in]  SmmuInfo  Pointer to the SMMU_INFO structure.
**/
VOID
SmmuV3LogErrors (
  IN SMMU_INFO  *SmmuInfo
  );

/**
  Send a SMMUV3_CMD_GENERIC command to the SMMUv3.

  @param [in]  SmmuInfo  Pointer to the SMMU_INFO structure.
  @param [in]  Command   Pointer to the command to send.

  @retval EFI_SUCCESS            Success.
  @retval EFI_TIMEOUT            Timeout.
  @retval EFI_INVALID_PARAMETER  Invalid Parameters.
**/
EFI_STATUS
SmmuV3SendCommand (
  IN SMMU_INFO           *SmmuInfo,
  IN SMMUV3_CMD_GENERIC  *Command
  );

#endif

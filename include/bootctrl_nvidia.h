/*
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation.  Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */

#ifndef _BOOTCTRL_NVIDIA_H_
#define _BOOTCTRL_NVIDIA_H_

#include <stdint.h>

#define BOOTCTRL_MAGIC 0x43424E00 /*magic number: '\0NBC' */
#define BOOTCTRL_SUFFIX_A           "_a"
#define BOOTCTRL_SUFFIX_B           "_b"
#define MAX_SLOTS 2
#define BOOTCTRL_VERSION 3
#define MAX_COUNT   7

/*This is just for test. Will define new slot_metadata partition */
#define BOOTCTRL_SLOTMETADATA_FILE_DEFAULT "/dev/block/by-name/SMD"

#define SMD_INFO_OFFSET_T18x 0x664
#define SMD_INFO_OFFSET_T19x 0x7DC

typedef enum {
    SOC_TYPE_UNKNOWN = 0,
    SOC_TYPE_T210,
    SOC_TYPE_T186,
    SOC_TYPE_T194,
    SOC_TYPE_T234,
    SOC_TYPE_T239
} soc_type_t;

typedef enum {
    TEGRABL_STORAGE_SDMMC_BOOT = 0,
    TEGRABL_STORAGE_SDMMC_USER,
    TEGRABL_STORAGE_SDMMC_RPMB,
    TEGRABL_STORAGE_QSPI_FLASH,
    TEGRABL_STORAGE_SATA,
    TEGRABL_STORAGE_USB_MS,
    TEGRABL_STORAGE_SDCARD,
    TEGRABL_STORAGE_UFS,
    TEGRABL_STORAGE_UFS_USER,
    TEGRABL_STORAGE_UFS_RPMB,
    TEGRABL_STORAGE_NVME,
    TEGRABL_STORAGE_MAX
} tegrabl_storage_type_t;

typedef struct slotmetadata_info {
    uint16_t device_type;
    uint16_t device_instance;
    uint32_t start_sector;
    uint32_t partition_size;
} smd_info_t;

typedef struct __attribute__((__packed__)) slot_info {
    /*
     * boot priority of slot.
     * range [0:15]
     * 15 meaning highest priortiy,
     * 0 mean that the slot is unbootable.
     */
    uint8_t priority;
    /*
     * suffix of slots.
     */
    char suffix[2];
    /*
     * retry count of booting
     * range [0:7]
     */
    uint8_t retry_count;

    /* set true if slot can boot successfully */
    uint8_t boot_successful;

} slot_info_t;

typedef struct __attribute__((__packed__)) smd_partition {
    /* Magic number  for idetification */
    uint32_t magic;
    uint16_t version;
    uint16_t num_slots;
    /*slot parameter structure */
    slot_info_t slot_info[MAX_SLOTS];
    uint32_t crc32;
} smd_partition_t;
#endif /* _BOOTCTRL_NVIDIA_H_ */

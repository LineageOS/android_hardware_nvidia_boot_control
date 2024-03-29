/*
 * Copyright (c) 2016 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef T186_NV_BOOTLOADER_PAYLOAD_UPDATER_H_
#define T186_NV_BOOTLOADER_PAYLOAD_UPDATER_H_

#include <bootctrl_nvidia.h>
#include <hardware/boot_control.h>

#include <stdio.h>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>

#define UPDATE_TYPE 0
#define BMP_TYPE 1

#define UPDATE_MAGIC_V2 "NVIDIA__BLOB__V2"
#define UPDATE_MAGIC_V3 "NVIDIA__BLOB__V3"
#define UPDATE_MAGIC_SIZE 17
#define ENTRY_LEN_WO_SPEC 56
#define IMG_SPEC_INFO_LENGTH_V2 64
#define IMG_SPEC_INFO_LENGTH_V3 128

#define PARTITION_PATH "/dev/block/by-name/"
#define BP_ENABLE_PATH "/sys/block/mmcblk0boot0/force_ro"
#define PARTITION_LEN 40

#define BMP_PATH "/postinstall/system/etc/firmware/bmp.blob"
#define BLOB_PATH "/postinstall/system/etc/firmware/bl_update_payload"

#define BMP_NAME "bootlogo"

/* Compute ceil(n/d) */
#define DIV_CEIL(n, d) (((n) + (d) - 1) / (d))

/* Round-up n to next multiple of w */
#define ROUND_UP(n, w) (DIV_CEIL(n, w) * (w))


/*
 * TODO : read those value from block device
 *
 */
#define BCT_MAX_COPIES 64
#define BR_EMMC_BLOCK_SIZE (16 * 1024)
#define BR_EMMC_PAGE_SIZE 512
#define BR_QSPI_BLOCK_SIZE (32 * 1024)
#define BR_QSPI_PAGE_SIZE (16 * 1024)

struct DependPartition {
    std::string name;
    int slot;
};

DependPartition part_dependence[] = {
    { "mb1", 0 },
    { "BCT", 0 },
    { "BCT", 1 },
    { "mb1", 1 },
};

enum PartitionType {
    kBootPartition = 0,
    kUserPartition,
    kDependPartition
};

enum BLStatus{
   kSuccess = 0,
   kBlobOpenFailed,
   kBootctrlGetFailed,
   kFsOpenFailed,
   kInternalError,
   kSlotOpenFailed,
   kStatusMax
};

class NvPayloadUpdate {
 public:
    NvPayloadUpdate();
    ~NvPayloadUpdate();

    /* UpdateDriver - main function that parses the Bootloader
     * Payload (blob) and writes to partitions in unused slots.
     * @params - payload_path, path to bootloader paylaod
     * @return - 0 on success, non-zero otherwise.
     */
    BLStatus UpdateDriver();

 private:
    struct RatchetInfo {
        uint8_t mb1_ratchet_level;
        uint8_t mts_ratchet_level;
        uint8_t rollback_ratchet_level;
        uint8_t reserved[5];
    };


    struct Header{
        char magic[UPDATE_MAGIC_SIZE];
        uint32_t hex;
        uint32_t size;
        uint32_t header_size;
        uint32_t number_of_elements;
        uint32_t type;
        uint32_t uncomp_size;
        struct RatchetInfo ratchet_info;
    };

    struct Entry{
        std::string partition;
        uint32_t pos;
        uint32_t len;
        uint32_t version;
        uint32_t op_mode;
        std::string spec_info;
        PartitionType type;
	uint8_t index;
        BLStatus (*write)(Entry*, FILE*, int);
    };

    // Updates the partitions in ota.blob
    static BLStatus OTAUpdater(const char* ota_path);

    // Updates BMP-A/BMP-B with bmp.blob
    static BLStatus BMPUpdater(const char* bmp_path);

    static std::string GetDeviceTNSpec();
    static uint8_t GetDeviceOpMode();

    // Parses header in the payload
    static void ParseHeaderInfo(unsigned char* buffer, Header* header);
    static void ParseEntryTable(char* buffer, std::vector<Entry>& entry_table,
                                Header* header);

    static bool IsDependPartition(std::string partition);
    static void GetEntryTable(std::string part, Entry *entry_t,
                              std::vector<Entry>& entry_table);

    // Writes to unused slot partitions from the payload
    static BLStatus WriteToPartition(std::vector<Entry>& entry_table, FILE* blobfile);

    static BLStatus WriteToUserPartition(Entry *entry_table,
                                         FILE* blobfile,
                                         int slot);
    static BLStatus WriteToDependPartition(std::vector<Entry>& entry_table,
                                           FILE* blobfile);

    static BLStatus WriteToBootPartition(Entry *entry_table,
                                         FILE* blobfile, int slot);

    static BLStatus EnableBootPartitionWrite(int enable);

    static BLStatus WriteToBctPartition(Entry *entry_table,
                                        FILE *blob_file,
                                        FILE *bootp,
                                        int slot);


    static int VerifiedPartition(Entry *entry_table, FILE *blob_file, int slot);

    // Log parsing of payload
    static void PrintHeader(Header* header);
    static void PrintEntryTable(std::vector<Entry>& entry_table, Header* header);
};

#endif  // T186_NV_BOOTLOADER_PAYLOAD_UPDATER_H_

/*
 * Copyright (c) 2016 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include "nv_bootloader_payload_updater.h"
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <string>
#include <iostream>
#include "gpt/gpttegra.h"

extern "C" {
}

BLStatus NvPayloadUpdate::UpdateDriver() {
    BLStatus status;

    status = OTAUpdater(BLOB_PATH);
    if (status != kSuccess) {
        LOG(ERROR) << "OTA Blob update failed. Status: "
            << static_cast<int>(status);
       return status;
    }

    status = BMPUpdater(BMP_PATH);
    if (status != kSuccess) {
        LOG(WARNING) << "BMP Blob update failed. Status: "
            << static_cast<int>(status);
       status = kSuccess;
    }

    return status;
}

uint8_t target_slot;
std::string boot_part;
std::string gpt_part;
GPTDataTegra BootGPT;
uint32_t br_block_size;
uint32_t br_page_size;

NvPayloadUpdate::NvPayloadUpdate() {
    // If suffix prop is empty, guess slot a
    std::string target_suffix = android::base::GetProperty("ro.boot.slot_suffix", "");
    // slot is the target slot, so opposite of current
    target_slot = (target_suffix.compare("_b") == 0 ? 0 : 1);

    boot_part = android::base::GetProperty("vendor.tegra.ota.boot_device", "");
    gpt_part = android::base::GetProperty("vendor.tegra.ota.gpt_device", boot_part);

    if (!gpt_part.empty()) {
        BootGPT.SetDisk(gpt_part);
        BootGPT.LoadTegraGPTData();
    }

    if (boot_part.compare("mtdblock") == 0) {
        br_block_size = BR_QSPI_BLOCK_SIZE;
	br_page_size = BR_QSPI_PAGE_SIZE;
    } else { //if (boot_part.compare("boot0") == 0)
        br_block_size = BR_EMMC_BLOCK_SIZE;
	br_page_size = BR_EMMC_PAGE_SIZE;
    }
}

NvPayloadUpdate::~NvPayloadUpdate() {
}

BLStatus NvPayloadUpdate::BMPUpdater(const char* bmp_path) {
    FILE* blob_file;
    char* buffer;
    std::string unused_path = std::string(PARTITION_PATH) + BMP_PATH;
    int bytes;
    int err;
    Header* header = new Header;
    size_t header_size = sizeof(Header);
    FILE* slot_stream;
    BLStatus status = kSuccess;

    if (target_slot)
        unused_path += "_b";

    blob_file = fopen(bmp_path, "r");
    if (!blob_file) {
         return  kBlobOpenFailed;
    }

    // Parse Header
    buffer = new char[header_size];
    bytes = fread(buffer, 1, header_size, blob_file);
    ParseHeaderInfo((unsigned char*) buffer, header);
    delete[] buffer;

    PrintHeader(header);

    err = fseek(blob_file, 0, SEEK_SET);
    buffer = new char[header->size];
    bytes = fread(buffer, 1, header->size, blob_file);

    LOG(INFO) << "Writing to " << unused_path << " for "
              << BMP_NAME;

    slot_stream = fopen(unused_path.c_str(), "rb+");
    if (!slot_stream) {
        LOG(ERROR) << "Slot could not be opened "<< BMP_NAME;
        status = kSlotOpenFailed;
        goto exit;
    }

    bytes = fwrite(buffer, 1, header->size, slot_stream);
    LOG(INFO) << "Bytes written to "<< BMP_NAME
                << ": "<< bytes;

    fclose(slot_stream);

exit:
    delete[] buffer;
    delete header;
    fclose(blob_file);

    return status;
}

BLStatus NvPayloadUpdate::OTAUpdater(const char* ota_path) {
    FILE* blob_file;
    Header* header = new Header;
    size_t header_size = sizeof(Header);
    char* buffer = new char[header_size];
    int bytes = 0;
    int err;
    BLStatus status = kSuccess;

    blob_file = fopen(ota_path, "r");
    if (!blob_file) {
        return  kBlobOpenFailed;
    }

    // Parse the header
    bytes = fread(buffer, 1, header_size, blob_file);
    ParseHeaderInfo((unsigned char*) buffer, header);
    delete[] buffer;

    PrintHeader(header);

    // Parse the entry table
    std::vector<Entry> entry_table;
    int entry_table_size = header->number_of_elements * ENTRY_LEN;
    buffer = new char[entry_table_size];
    err = fseek(blob_file, header->header_size, SEEK_SET);
    bytes = fread(buffer, 1, entry_table_size, blob_file);
    ParseEntryTable(buffer, entry_table, header);
    delete[] buffer;

    PrintEntryTable(entry_table, header);

    // Write each partition
    err = fseek(blob_file, 0, SEEK_SET);
    status = WriteToPartition(entry_table, blob_file);
    if (status) {
        LOG(ERROR) << "Writing to partitions failed.";
    }

    delete header;
    fclose(blob_file);

    return status;
}

static int OffsetOfBootPartition(std::string part, int slot, uint8_t index) {
    if ((part.compare("BCT") == 0) && slot)
        return ROUND_UP(br_block_size, BootGPT.GetBlockSize());

    return BootGPT.GetOffset(index);
}

BLStatus NvPayloadUpdate::EnableBootPartitionWrite(int enable) {
    FILE* fd;
    int bytes = 0;

    // Only needed for emmc boot partitions
    if (boot_part.find("boot0") == std::string::npos)
        return kSuccess;

    fd = fopen(BP_ENABLE_PATH, "rb+");
    if (!fd) {
        LOG(ERROR) << "BP_ENABLE_PATH could not be opened ";
        return kFsOpenFailed;
    }

    if (enable)
        bytes = fwrite("0", 1, 1, fd);
    else
        bytes = fwrite("1", 1, 1, fd);

    fclose(fd);
    return kSuccess;
}

bool NvPayloadUpdate::IsDependPartition(std::string partition) {
    unsigned int i;

    for (i = 0; i < sizeof(part_dependence)/sizeof(*part_dependence); i++) {
        if (!partition.compare(part_dependence[i].name))
            return true;
    }

    return false;
}

BLStatus NvPayloadUpdate::WriteToBctPartition(Entry *entry_table,
                                                FILE *blob_file,
                                                FILE *bootp,
                                                int slot) {
    int bytes = 0;
    unsigned pages_in_bct = 0;
    unsigned slot_size = 0;
    unsigned bct_count = 0;
    int offset = 0;
    int bin_size = entry_table->len;

    /*
     * Read update binary from blob
     */
    unsigned char* new_bct = new unsigned char[bin_size];
    fseek(blob_file, entry_table->pos, SEEK_SET);
    bytes = fread(new_bct, 1, bin_size, blob_file);

    /*
     * Read current BCT from boot partition
     */
    unsigned char *cur_bct = new unsigned char[bin_size];
    fseek(bootp, 0, SEEK_SET);
    bytes = fread(cur_bct, 1, bin_size, bootp);

    pages_in_bct = DIV_CEIL(bin_size, br_page_size);

    /*
    The term "slot" refers to a potential location
    of a BCT in a block. A slot is the smallest integral
    number of pages that can hold a BCT.Thus, every
    BCT begins at the start of a page and may span
    multiple pages. A block is a space in memory that
    can hold multiple slots of BCTs.
    */
    slot_size = pages_in_bct * br_page_size;

    /*
    The BCT search sequence followed by BootROM is:
    Block 0, Slot 0
    Block 0, Slot 1
    Block 1, Slot 0
    Block 1, Slot 1
    Block 1, Slot 2
    .....
    Block 1, Slot N
    Block 2, Slot 0
    .....
    Block 2, Slot N
    .....
    Block 63, Slot N
    Based on the search sequence, we write the
    block 0, slot 1 BCT first, followed by one BCT
    in slot 0 of subsequent blocks and lastly one BCT
    in block0, slot 0.
    */

    if (slot)
        goto block_1;

    /*
     * Write one BCT to slot 1, block 0
     * if blocksize is bigger than twice of slotsize
     */
    offset = ROUND_UP(slot_size, BootGPT.GetBlockSize());
    if (br_block_size > offset) {
        fseek(bootp, offset, SEEK_SET);
        bytes = fwrite(new_bct, 1, bin_size, bootp);

        LOG(INFO) << entry_table->partition << " write: offset = " << slot_size
            << " bytes = " << bytes;
    }

    /* Finally write to block 0, slot 0 */
    offset = 0;
    fseek(bootp, offset, SEEK_SET);
    bytes = fwrite(new_bct, 1, bin_size, bootp);

    LOG(INFO) << entry_table->partition << " write: offset = " << offset
           << " bytes = " << bytes;

    goto exit;

block_1:
    /* Fill Slot 0 for all other blocks */
    offset = ROUND_UP(br_block_size, BootGPT.GetBlockSize());
    while (offset < BootGPT.GetSize(entry_table->index)) {
        fseek(bootp, offset, SEEK_SET);
        bytes = fwrite(new_bct, 1, bin_size, bootp);

        LOG(INFO) << entry_table->partition << " write: offset = " << offset
              << " bytes = " << bytes;

        offset += ROUND_UP(br_block_size, BootGPT.GetBlockSize());
        bct_count++;
        if (bct_count >= BCT_MAX_COPIES - 1)
            break;
    }

exit:
    delete[] cur_bct;
    delete[] new_bct;

    return kSuccess;
}

BLStatus NvPayloadUpdate::WriteToBootPartition(Entry *entry_table,
                                               FILE* blob_file,
                                               int slot) {
    int bin_size = entry_table->len;
    FILE* bootp;
    int bytes = 0;
    BLStatus status = kSuccess;
    int offset = 0;

    if (boot_part.empty())
        return kFsOpenFailed;

    status = EnableBootPartitionWrite(1);
    if (status) {
        return kFsOpenFailed;
    }

    bootp = fopen(boot_part.c_str(), "rb+");
    if (!bootp) {
        LOG(ERROR) << "Boot Partition could not be opened "
            << entry_table->partition;
        status = EnableBootPartitionWrite(0);

        return kFsOpenFailed;
    }

    if (!entry_table->partition.compare("BCT")) {
        status = WriteToBctPartition(entry_table, blob_file, bootp, slot);
    } else {
        /*
         * Read update binary from blob
         */
        char* buffer = new char[bin_size];
        fseek(blob_file, entry_table->pos, SEEK_SET);
        bytes = fread(buffer, 1, bin_size, blob_file);

        offset = OffsetOfBootPartition(entry_table->partition, slot, entry_table->index);

        fseek(bootp, offset , SEEK_SET);
        bytes = fwrite(buffer, 1, bin_size, bootp);
        fflush(bootp);
        delete[] buffer;

        LOG(INFO) << entry_table->partition
            << " write: offset = " << offset << " bytes = " << bytes;

        if (VerifiedPartition(entry_table, blob_file, slot)) {
            LOG(ERROR) << "Failed to write " << entry_table->partition;
            status = kInternalError;
        }
    }

    fclose(bootp);
    EnableBootPartitionWrite(0);

    return status;
}

void NvPayloadUpdate::GetEntryTable(std::string part, Entry *entry_t,
                                    std::vector<Entry>& entry_table) {
    for (auto entry : entry_table) {
        if (!entry.partition.compare(part)) {
            *entry_t = entry;
            break;
        }
    }
}

int NvPayloadUpdate::VerifiedPartition(Entry *entry_table,
                                           FILE *blob_file,
                                           int slot) {
    FILE *fd;
    int offset;
    int bin_size = entry_table->len;
    char* source = new char[bin_size];
    char* target = new char[bin_size];
    int result;

    if (boot_part.empty())
        return kFsOpenFailed;

    fd = fopen(boot_part.c_str(), "r");

    offset = OffsetOfBootPartition(entry_table->partition, slot, entry_table->index);

    fseek(fd, offset, SEEK_SET);
    fread(source, 1, bin_size, fd);
    fclose(fd);

    fseek(blob_file, entry_table->pos, SEEK_SET);
    fread(target, 1, bin_size, blob_file);

    result = memcmp(target, source, bin_size);

    delete[] target;
    delete[] source;

    return result;
}

BLStatus NvPayloadUpdate::WriteToDependPartition(std::vector<Entry>& entry_table,
                                                 FILE* blob_file) {
    Entry entry_t;
    BLStatus status = kSuccess;
    int slot, i;
    int num_part = sizeof(part_dependence)/sizeof(*part_dependence);

    for (i = 0; i < num_part; i++) {
        slot = part_dependence[i].slot;
        GetEntryTable(part_dependence[i].name, &entry_t, entry_table);
        if (VerifiedPartition(&entry_t, blob_file, slot)) {
            status = (entry_t.write)(&entry_t, blob_file, slot);
            if (status) {
                LOG(ERROR) << entry_t.partition <<" update failed ";
                return kInternalError;
            }
        }
    }

    return status;
}

BLStatus NvPayloadUpdate::WriteToUserPartition(Entry *entry_table,
                                               FILE* blob_file,
                                               int slot) {
    std::string unused_path = std::string(PARTITION_PATH) + entry_table->partition;
    FILE* slot_stream;
    int bytes = 0;
    int part_size = entry_table->len;

    if (slot)
        unused_path += "_b";
    // Certain os facing partitions have to use _a instead of empty for slot 0
    else if (entry_table->partition.compare("kernel-dtb") == 0)
        unused_path += "_a";

    slot_stream = fopen(unused_path.c_str(), "rb+");
    if (!slot_stream) {
        LOG(ERROR) << "Slot could not be opened "<< entry_table->partition;
        return  kSlotOpenFailed;
    }

    char* buffer = new char[part_size];
    fseek(blob_file, entry_table->pos, SEEK_SET);
    bytes = fread(buffer, 1, part_size, blob_file);

    LOG(INFO) << "Writing to " << unused_path << " for "
        << entry_table->partition;

    bytes = fwrite(buffer, 1, part_size, slot_stream);
    LOG(INFO) << entry_table->partition
        << " write: bytes = " << bytes;

    delete[] buffer;
    fclose(slot_stream);

    return kSuccess;
}

BLStatus NvPayloadUpdate::WriteToPartition(std::vector<Entry>& entry_table,
                                           FILE* blob_file) {
    BLStatus status = kSuccess;

    for (auto entry : entry_table) {
        if (entry.type != kDependPartition) {
            status = (entry.write)(std::addressof(entry), blob_file, target_slot);
            if (status) {
                LOG(INFO) << entry.partition
                    << " fail to write ";
                return status;
            }
        }
    }

    status = WriteToDependPartition(entry_table, blob_file);
    if (status) {
        LOG(INFO) << "Fail to write Dependence partitions ";
    }

    return status;
}

void NvPayloadUpdate::ParseHeaderInfo(unsigned char* buffer,
                                      Header* header) {
    std::memcpy(header->magic, buffer, (sizeof(header->magic)-1));

    // blob header magic string does not have null terminator
    header->magic[sizeof(header->magic)-1] = '\0';
    buffer += sizeof(header->magic)-1;

    std::memcpy( &header->hex, buffer, sizeof(int));
    buffer += sizeof(int);

    std::memcpy( &header->size, buffer, sizeof(int));
    buffer += sizeof(int);

    std::memcpy( &header->header_size, buffer, sizeof(int));
    buffer += sizeof(int);

    std::memcpy( &header->number_of_elements, buffer, sizeof(int));
    buffer += sizeof(int);

    std::memcpy( &header->type, buffer, sizeof(int));
    buffer += sizeof(int);

    std::memcpy( &header->uncomp_size, buffer, sizeof(int));
    buffer += sizeof(int);

    if (header->type == UPDATE_TYPE) {
        std::memcpy( &header->ratchet_info, buffer, sizeof(RatchetInfo));
        buffer += sizeof(RatchetInfo);
    }
}

std::string NvPayloadUpdate::GetDeviceTNSpec() {
    std::string specid, specconfig;
    std::ifstream tnspec_id("/proc/device-tree/chosen/plugin-manager/tnspec/id");
    std::ifstream tnspec_config("/proc/device-tree/chosen/plugin-manager/tnspec/config");

    if (tnspec_id.is_open() && tnspec_config.is_open() &&
        std::getline(tnspec_id, specid) &&
        std::getline(tnspec_config, specconfig)) {
        specid.erase(std::find(specid.begin(), specid.end(), '\0'),
                     specid.end());
        specconfig.erase(std::find(specconfig.begin(), specconfig.end(), '\0'),
                         specconfig.end());
        return specid + "." + specconfig;
    }

    return "";
}

uint8_t NvPayloadUpdate::GetDeviceOpMode() {
    std::string opmode;
    std::ifstream op_mode("/sys/module/tegra_fuse/parameters/tegra_prod_mode");

    if (op_mode.is_open() && std::getline(op_mode, opmode))
        return std::stoul(opmode, nullptr, 10);

    // If detection fails, assume production mode. It's a pretty safe bet.
    return 1;
}

void NvPayloadUpdate::ParseEntryTable(char* buffer, std::vector<Entry>& entry_table,
                                      Header* header) {
    int num_entries = header->number_of_elements;
    Entry temp_entry;
    std::string tnspec = GetDeviceTNSpec();

    // Device op_mode is 0 or 1, but corresponds to 1 and 2 in a BUP entry
    uint8_t op_mode = GetDeviceOpMode() + 1;

    for (int i = 0; i < num_entries; i++) {
        temp_entry.partition.assign(buffer, PARTITION_LEN);
        temp_entry.partition.erase(std::find(temp_entry.partition.begin(),
                                             temp_entry.partition.end(), '\0'),
                                   temp_entry.partition.end());
        buffer+= (PARTITION_LEN)*sizeof(char);

        std::memcpy(&temp_entry.pos, buffer, sizeof(int32_t));
        buffer+= sizeof(int32_t);

        std::memcpy(&temp_entry.len, buffer, sizeof(int32_t));
        buffer+= sizeof(int32_t);

        std::memcpy(&temp_entry.version, buffer, sizeof(int32_t));
        buffer+= sizeof(int32_t);

        std::memcpy(&temp_entry.op_mode, buffer, sizeof(int32_t));
        buffer+= sizeof(int32_t);

        temp_entry.spec_info.assign(buffer, IMG_SPEC_INFO_LENGTH);
        temp_entry.spec_info.erase(std::find(temp_entry.spec_info.begin(),
                                             temp_entry.spec_info.end(), '\0'),
                                   temp_entry.spec_info.end());
        buffer+= (IMG_SPEC_INFO_LENGTH)*sizeof(char);

        // Conditions for a valid entry:
        // - Entry tnspec is empty
        // - Entry tnspec and device tnspec match
        // - Entry op_mode is 0
        // - Entry op_mode is 1 and device op_mode is 0
        // - Entry op_mode is 2 and device op_mode is 1
        // Otherwise, ignore the entry
        if (!((temp_entry.spec_info.empty() ||
               tnspec.compare(temp_entry.spec_info) == 0) &&
              (temp_entry.op_mode == 0 ||
               temp_entry.op_mode == op_mode)))
            continue;

        std::string part_match = temp_entry.partition;
        if ((part_match.compare("BCT")) != 0 && target_slot)
            part_match += "_b";

        if (BootGPT.MatchPartition(part_match, &temp_entry.index)) {
            temp_entry.type = (IsDependPartition(temp_entry.partition) ? 
                               kDependPartition : kBootPartition);
            temp_entry.write = WriteToBootPartition;
        } else {
            temp_entry.type = kUserPartition;
            temp_entry.write = WriteToUserPartition;
        }

	entry_table.push_back(temp_entry);
    }
}

void NvPayloadUpdate::PrintHeader(Header* header) {
    LOG(INFO) << "HEADER: MAGIC " << header->magic;
    LOG(INFO) << "HEX_VALUE " << header->hex;
    LOG(INFO) << "BLOB_SIZE " << header->size;
    LOG(INFO) << "HEADER_SIZE " << header->header_size;
    LOG(INFO) << "NUMBER_OF_ELEMENTS " << header->number_of_elements;
    LOG(INFO) << "HEADER_TYPE " << header->type;
}

void NvPayloadUpdate::PrintEntryTable(std::vector<Entry>& entry_table, Header* header) {
    LOG(INFO) << "ENTRY_TABLE:";
    LOG(INFO) << "PART  POS  LEN  VER TYPE";

    for (auto entry : entry_table) {
        LOG(INFO) << entry.partition << "  "
                << entry.pos << "  "
                << entry.len << "  "
                << entry.version << "  "
                << static_cast<int>(entry.type);
    }
}

int main() {
    NvPayloadUpdate updater;

    BLStatus status = updater.UpdateDriver();

    return status;
}

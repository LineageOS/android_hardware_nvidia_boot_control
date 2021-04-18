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
#include <base/logging.h>
#include <string>
#include <iostream>

extern "C" {
}

boot_control_module_t *g_bootctrl_module = NULL;

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

NvPayloadUpdate::NvPayloadUpdate() {
    const hw_module_t* hw_module;

    if (hw_get_module("bootctrl", &hw_module) != 0)  {
        LOG(ERROR) << "Error getting bootctrl module";
	return;
    }

    g_bootctrl_module  = reinterpret_cast<boot_control_module_t*>(
                    const_cast<hw_module_t*>(hw_module));

    g_bootctrl_module->init(g_bootctrl_module);
}

BLStatus NvPayloadUpdate::BMPUpdater(const char* bmp_path) {
    FILE* blob_file;
    char* buffer;
    char* unused_path = NULL;
    int bytes;
    int err;
    Header* header = new Header;
    size_t header_size = sizeof(Header);
    FILE* slot_stream;
    int current_slot;
    BLStatus status = kSuccess;

    if (!g_bootctrl_module) {
        LOG(ERROR) << "Error getting bootctrl module";
        return kBootctrlGetFailed;
    }

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

    current_slot =
        g_bootctrl_module->getCurrentSlot(g_bootctrl_module);
    if (current_slot < 0) {
        LOG(ERROR) << "Error getting current SLOT";
        status = kBootctrlGetFailed;
        goto exit;
    }

    unused_path = GetUnusedPartition(BMP_NAME, !current_slot);
    if (!unused_path) {
        status = kBootctrlGetFailed;
        goto exit;
    }

    LOG(INFO) << "Writing to " << unused_path << " for "
              << BMP_NAME;

    slot_stream = fopen(unused_path, "rb+");
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

static int OffsetOfBootPartition(std::string part, int slot) {
    int offset = 0;

    if (part.compare("BCT")) {
        if (slot)
            return (BCT_MAX_COPIES - 1) * BR_BLOCK_SIZE;
        else
            return 0;
    }

    offset = PART_BCT_SIZE;

    for (unsigned i = 1; i < sizeof(boot_partiton)/sizeof(Partition); i++) {
        if (!part.compare(boot_partiton[i].name)) {
            offset += slot * boot_partiton[i].part_size;
            break;
        }
        offset += 2 * boot_partiton[i].part_size;
    }

    return offset;
}

BLStatus NvPayloadUpdate::EnableBootPartitionWrite(int enable) {
    FILE* fd;
    int bytes = 0;

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

char* NvPayloadUpdate::GetUnusedPartition(std::string partition_name,
                                          int slot) {
    char* unused_path;
    const char* free_slot_name;
    int path_len;

    free_slot_name =
        (slot)? BOOTCTRL_SUFFIX_B: BOOTCTRL_SUFFIX_A;

    path_len = strlen(PARTITION_PATH) + partition_name.length()
                            + strlen(free_slot_name) + 1;

    unused_path = new char[path_len];
    snprintf(unused_path, path_len,  "%s%s%s", PARTITION_PATH,
        partition_name.c_str(), free_slot_name);

    return unused_path;
}

bool NvPayloadUpdate::IsDependPartition(std::string partition) {
    unsigned int i;

    for (i = 0; i < sizeof(part_dependence)/sizeof(*part_dependence); i++) {
        if (!partition.compare(part_dependence[i].name))
            return true;
    }

    return false;
}

bool NvPayloadUpdate::IsBootPartition(std::string partition) {
    unsigned int i;

    for (i = 0; i < sizeof(boot_partiton)/sizeof(*boot_partiton); i++) {
        if (!partition.compare(boot_partiton[i].name))
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
    //BLStatus status;
    //int err;

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

    /*
     * Update signature of current BCT
     */
    /*err = update_bct_signedsection(cur_bct, new_bct, bin_size);
    if (err) {
        LOG(ERROR) << "Update BCT signature failed, err = " << err;
        status = kInternalError;
        goto exit;
    }*/

    pages_in_bct = DIV_CEIL(bin_size, BR_PAGE_SIZE);

    /*
    The term "slot" refers to a potential location
    of a BCT in a block. A slot is the smallest integral
    number of pages that can hold a BCT.Thus, every
    BCT begins at the start of a page and may span
    multiple pages. A block is a space in memory that
    can hold multiple slots of BCTs.
    */
    slot_size = pages_in_bct * BR_PAGE_SIZE;

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
    if (BR_BLOCK_SIZE > slot_size * 2) {
        fseek(bootp, slot_size, SEEK_SET);
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
    offset = BR_BLOCK_SIZE;
    while (offset < PART_BCT_SIZE) {
        fseek(bootp, offset, SEEK_SET);
        bytes = fwrite(new_bct, 1, bin_size, bootp);

        LOG(INFO) << entry_table->partition << " write: offset = " << offset
              << " bytes = " << bytes;

        offset += BR_BLOCK_SIZE;
        bct_count++;
        if (bct_count == BCT_MAX_COPIES - 1)
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

    status = EnableBootPartitionWrite(1);
    if (status) {
        return kFsOpenFailed;
    }

    bootp = fopen(BOOT_PART_PATH, "rb+");
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

        offset = OffsetOfBootPartition(entry_table->partition, slot);

        fseek(bootp, offset , SEEK_SET);
        bytes = fwrite(buffer, 1, bin_size, bootp);
        delete[] buffer;

        LOG(INFO) << entry_table->partition
            << " write: offset = " << offset << " bytes = " << bytes;
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

int NvPayloadUpdate::VerifiedPartiton(Entry *entry_table,
                                           FILE *blob_file, int slot) {
    FILE *fd;
    int offset;
    int bin_size = entry_table->len;
    char* source = new char[bin_size];
    char* target = new char[bin_size];
    int result;

    fd = fopen(BOOT_PART_PATH, "r");

    offset = OffsetOfBootPartition(entry_table->partition, slot);

    fseek(fd, offset, SEEK_SET);
    fread(source, 1, bin_size, fd);
    fclose(fd);

    fseek(blob_file, entry_table->pos, SEEK_SET);
    fread(target, 1, bin_size, blob_file);

    result = memcmp(target, source, bin_size);
    if (result == 0) {
        LOG(INFO) << entry_table->partition
            << " slot: " << slot
            << " is updated. Skip... ";
    }

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
        if (VerifiedPartiton(&entry_t, blob_file, slot)) {
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
    char* unused_path;
    FILE* slot_stream;
    int bytes = 0;
    int part_size = entry_table->len;

    unused_path = GetUnusedPartition(entry_table->partition, slot);
    if (!unused_path) {
        return kBootctrlGetFailed;
    }

    slot_stream = fopen(unused_path, "rb+");
    if (!slot_stream) {
        LOG(ERROR) << "Slot could not be opened "<< entry_table->partition;
        return  kSlotOpenFailed;
    }

    char* buffer = new char[part_size];
    fseek(blob_file, entry_table->pos, SEEK_SET);
    bytes = fread(buffer, 1, part_size, blob_file);

    /*if (!strcmp(entry_table->partition, KERNEL_DTB_NAME) &&
            is_dtb_valid(reinterpret_cast<void*>(buffer), KERNEL_DTB, slot)) {
        goto exit;
    } else if (!strcmp(entry_table->partition, BL_DTB_NAME) &&
            is_dtb_valid(reinterpret_cast<void*>(buffer), BL_DTB, slot)) {
        goto exit;
    }*/

    LOG(INFO) << "Writing to " << unused_path << " for "
        << entry_table->partition;

    bytes = fwrite(buffer, 1, part_size, slot_stream);
    LOG(INFO) << entry_table->partition
        << " write: bytes = " << bytes;

//exit:
    delete[] buffer;
    fclose(slot_stream);

    return kSuccess;
}

BLStatus NvPayloadUpdate::WriteToPartition(std::vector<Entry>& entry_table,
                                           FILE* blob_file) {
    BLStatus status = kSuccess;
    int current_slot;

    if (!g_bootctrl_module) {
        LOG(ERROR) << "Error getting bootctrl module";
        return kBootctrlGetFailed;
    }

    current_slot = g_bootctrl_module->getCurrentSlot(g_bootctrl_module);
    if (current_slot < 0) {
        LOG(ERROR) << "Error getting current SLOT";
        return kBootctrlGetFailed;
    }

    for (auto entry : entry_table) {
        if (entry.type != kDependPartition) {
            status = (entry.write)(std::addressof(entry), blob_file,
                        !current_slot);
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

        if (IsDependPartition(temp_entry.partition)) {
            temp_entry.type = kDependPartition;
            temp_entry.write = WriteToBootPartition;
        } else if (IsBootPartition(temp_entry.partition)) {
            temp_entry.type = kBootPartition;
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

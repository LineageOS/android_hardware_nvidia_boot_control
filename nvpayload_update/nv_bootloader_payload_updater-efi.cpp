/*
 * Copyright (c) 2016 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include "nv_bootloader_payload_updater-efi.h"
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <string>
#include <iostream>

extern "C" {
#include <efivar/efivar.h>
}

namespace fs = std::filesystem;

BLStatus NvPayloadUpdate::UpdateDriver() {
    BLStatus status;

    status = OTAUpdater(BLOB_PATH);
    if (status != kSuccess) {
        LOG(ERROR) << "Kernel Blob update failed. Status: "
            << static_cast<int>(status);
       return status;
    }

    status = FMPSetup();
    if (status != kSuccess) {
        LOG(ERROR) << "Capsule setup failed. Status: "
            << static_cast<int>(status);
       return status;
    }
    return status;
}

uint8_t target_slot;

NvPayloadUpdate::NvPayloadUpdate() {
    // If suffix prop is empty, guess slot a
    std::string target_suffix = android::base::GetProperty("ro.boot.slot_suffix", "");
    // slot is the target slot, so opposite of current
    target_slot = (target_suffix.compare("_b") == 0 ? 0 : 1);
}

NvPayloadUpdate::~NvPayloadUpdate() {
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

    uint8_t entry_len = ENTRY_LEN_WO_SPEC;
    if (strncmp(header->magic, UPDATE_MAGIC_V2, UPDATE_MAGIC_SIZE) == 0)
        entry_len += IMG_SPEC_INFO_LENGTH_V2;
    else if (strncmp(header->magic, UPDATE_MAGIC_V3, UPDATE_MAGIC_SIZE) == 0)
        entry_len += IMG_SPEC_INFO_LENGTH_V3;
    else
        return  kBlobOpenFailed;

    // Parse the entry table
    std::vector<Entry> entry_table;
    int entry_table_size = header->number_of_elements * entry_len;
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

void NvPayloadUpdate::GetEntryTable(std::string part, Entry *entry_t,
                                    std::vector<Entry>& entry_table) {
    for (auto entry : entry_table) {
        if (!entry.partition.compare(part)) {
            *entry_t = entry;
            break;
        }
    }
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
    else
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
        status = (entry.write)(std::addressof(entry), blob_file, target_slot);
        if (status) {
            LOG(INFO) << entry.partition
                << " fail to write ";
            return status;
        }
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
    size_t tnspec_size = 0;
    char *tnspec = NULL;
    uint32_t attributes;
    if (efi_get_variable(EFI_NVIDIA_PUBLIC_VARIABLE_GUID, "TegraPlatformSpec",
		    (uint8_t **)&tnspec, &tnspec_size, &attributes) < 0)
        return "";

    std::string ret(tnspec);
    free(tnspec);
    return ret;
}

uint8_t NvPayloadUpdate::GetDeviceOpMode() {
    // This is not exposed on efi devices, assume prod mode
    // Nothing being written by this process cares anyways
    return 1;
}

void NvPayloadUpdate::ParseEntryTable(char* buffer, std::vector<Entry>& entry_table,
                                      Header* header) {
    int num_entries = header->number_of_elements;
    Entry temp_entry;
    std::string tnspec = GetDeviceTNSpec();

    // Device op_mode is 0 or 1, but corresponds to 1 and 2 in a BUP entry
    uint8_t op_mode = GetDeviceOpMode() + 1;

    // Check spec length
    uint8_t spec_len = IMG_SPEC_INFO_LENGTH_V2;
    if (strncmp(header->magic, UPDATE_MAGIC_V3, UPDATE_MAGIC_SIZE) == 0)
        spec_len = IMG_SPEC_INFO_LENGTH_V3;

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

        temp_entry.spec_info.assign(buffer, spec_len);
        temp_entry.spec_info.erase(std::find(temp_entry.spec_info.begin(),
                                             temp_entry.spec_info.end(), '\0'),
                                   temp_entry.spec_info.end());
        buffer+= (spec_len)*sizeof(char);

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

        temp_entry.type = kUserPartition;
        temp_entry.write = WriteToUserPartition;

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

BLStatus NvPayloadUpdate::FMPSetup() {
    // Check if esp partition mounted
    if (!fs::is_directory("/mnt/vendor/esp/EFI"))
        return kFsOpenFailed;

    // Set OS Indications for capsule update
    uint64_t indication = 0;
    if (efi_get_variable_exists(EFI_GLOBAL_GUID, "OsIndications") >= 0) {
        size_t osind_size = 0;
        if (efi_get_variable_size(EFI_GLOBAL_GUID, "OsIndications", &osind_size) < 0 ||
            osind_size != sizeof(uint64_t))
            return kBootctrlGetFailed;

        uint64_t *osind = NULL;
        uint32_t attributes;
        if (efi_get_variable(EFI_GLOBAL_GUID, "OsIndications", (uint8_t **)&osind,
                    &osind_size, &attributes) < 0)
            return kBootctrlGetFailed;

        indication = *osind;
        free (osind);
    }

    indication |= 0x0004;
    if (efi_set_variable(EFI_GLOBAL_GUID, "OsIndications", (uint8_t *)&indication,
                         sizeof(indication),
                         EFI_VARIABLE_NON_VOLATILE |
                         EFI_VARIABLE_BOOTSERVICE_ACCESS |
                         EFI_VARIABLE_RUNTIME_ACCESS,
                         0644) < 0) {
        return kBootctrlGetFailed;
    }

    // Nvidia's capsule update will fail if a capsule exists while the efivar
    // to switch boot chains exists, cancelling both. So delete this efivar.
    efi_del_variable(EFI_NVIDIA_PUBLIC_VARIABLE_GUID, "BootChainFwNext");

    // Copy capsule update and efi launcher into place
    fs::current_path("/mnt/vendor/esp/EFI");
    fs::create_directory("UpdateCapsule");
    fs::copy(CAPSULE_PATH, "UpdateCapsule/TEGRA_BL.Cap");
    fs::copy(LAUNCHER_PATH, "BOOT/BOOTAA64.efi", fs::copy_options::overwrite_existing);

    return kSuccess;
}

int main() {
    NvPayloadUpdate updater;

    BLStatus status = updater.UpdateDriver();

    return status;
}

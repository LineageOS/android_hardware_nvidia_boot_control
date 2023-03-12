/*
 * Copyright (C) 2021 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "BootControl-smd.h"

#include <fstream>
#include <zlib.h>

namespace android {
namespace hardware {
namespace boot {
namespace V1_0 {
namespace implementation {

bool BootControl::validateSlotMetadata() {
    ssize_t crc_size = sizeof(smd_partition_t) - sizeof(uint32_t);
    smd_partition_t smd_partition, smd_backup;

    if (smd_info.start_sector != 0) {
        // SMD is on a device that does not have an accessible partition table
        std::ifstream smd(smd_device, std::ios::binary);
        if(!smd.is_open())
            return false;

        // Seek to primary smd and read
        smd.seekg(smd_info.start_sector * 512);
        if (smd.fail())
            return false;

        smd.read((char*)&smd_partition, sizeof(smd_partition_t));
        if (smd.fail())
            return false;

        // Seek to backup smd and read
        smd.seekg(smd_info.start_sector * 512 + smd_info.partition_size);
        if (smd.fail())
            return false;

        smd.read((char*)&smd_backup, sizeof(smd_partition_t));
        if (smd.fail())
            return false;
    } else {
        // SMD is on a device that does have an accessible partition table
        // Open primary smd and read
        std::ifstream smd(smd_device, std::ios::binary);
        if(!smd.is_open())
            return false;

        smd.read((char*)&smd_partition, sizeof(smd_partition_t));
        if (smd.fail())
            return false;

        // Open backup smd and read
        std::ifstream smd_b(smd_device + "_b", std::ios::binary);
        if(!smd_b.is_open())
            return false;

        smd_b.read((char*)&smd_backup, sizeof(smd_partition_t));
        if (smd_b.fail())
            return false;
    }

    uint32_t crc   = crc32(0, (const unsigned char*)&smd_partition, crc_size);
    uint32_t crc_b = crc32(0, (const unsigned char*)&smd_backup,    crc_size);

    if (smd_partition.crc32 == crc && smd_backup.crc32 == crc_b &&
        crc == crc_b) {
        // Everything checks out
        return true;
    } else if (smd_partition.crc32 == crc) {
        // Either backup is corrupt or primary and backup do not match
        return false; // TODO: write primary to backup
    } else if (smd_backup.crc32 == crc) {
        // Primary is corrupt
        return false; // TODO: write backup to primary
    } else {
        // Both are corrupt, can't do anything
        return false;
    }
}

bool BootControl::readSlotMetadata(smd_partition_t *smd_partition) {
    // Only read primary smd, no need to also read backup
    std::ifstream smd(smd_device, std::ios::binary);
    if(!smd.is_open())
        return false;

    smd.seekg(smd_info.start_sector * 512);
    if (smd.fail())
        return false;

    smd.read((char*)smd_partition, sizeof(smd_partition_t));
    if (smd.fail())
        return false;

    return true;
}

bool BootControl::writeSlotMetadata(smd_partition_t *smd_partition) {
    ssize_t crc_size = sizeof(smd_partition_t) - sizeof(uint32_t);

    if (smd_info.device_type == TEGRABL_STORAGE_SDMMC_BOOT) {
        std::ofstream boot_lock("/sys/block/mmcblk0boot0/force_ro");
        if (boot_lock.is_open())
            boot_lock.write("0", 1);
    }

    smd_partition->crc32 = crc32(0, (const unsigned char*)smd_partition, crc_size);

    if (smd_info.start_sector != 0) {
        // SMD is on a device that does not have an accessible partition table
        std::ofstream smd(smd_device, std::ios::binary);
        if(!smd.is_open())
            return false;

        // Seek to primary smd and write
        smd.seekp(smd_info.start_sector * 512);
        if (smd.fail())
            return false;

        smd.write((char*)smd_partition, sizeof(smd_partition_t));
        smd.flush();
        if (smd.fail())
            return false;

        // Seek to backup smd and write
        smd.seekp(smd_info.start_sector * 512 + smd_info.partition_size);
        if (smd.fail())
            return false;

        smd.write((char*)smd_partition, sizeof(smd_partition_t));
        smd.flush();
        if (smd.fail())
            return false;
    } else {
        // SMD is on a device that does have an accessible partition table
        // Open primary smd and write
        std::ofstream smd(smd_device, std::ios::binary);
        if(!smd.is_open())
            return false;

        smd.write((char*)smd_partition, sizeof(smd_partition_t));
        smd.flush();
        if (smd.fail())
            return false;

        // Open backup smd and write
        std::ofstream smd_b(smd_device + "_b", std::ios::binary);
        if(!smd_b.is_open())
            return false;

        smd_b.write((char*)smd_partition, sizeof(smd_partition_t));
        smd_b.flush();
        if (smd_b.fail())
            return false;
    }

    if (smd_info.device_type == TEGRABL_STORAGE_SDMMC_BOOT) {
        std::ofstream boot_lock("/sys/block/mmcblk0boot0/force_ro");
        if (boot_lock.is_open())
            boot_lock.write("1", 1);
    }

    // Read back to validate successful write
    return validateSlotMetadata();
}

// Methods from ::android::hardware::boot::V1_0::IBootControl follow.
Return<uint32_t> BootControl::getNumberSlots() {
    smd_partition_t smd_partition;

    if (!readSlotMetadata(&smd_partition))
        return -EIO;

    return smd_partition.num_slots;
}

Return<uint32_t> BootControl::getCurrentSlot() {
    smd_partition_t smd_partition;
    std::string slot_suffix = GetProperty("ro.boot.slot_suffix", "");

    if (!readSlotMetadata(&smd_partition))
        return -EIO;

    for (uint8_t i = 0 ; i < smd_partition.num_slots; i++) {
        if (slot_suffix.compare(0, 2, smd_partition.slot_info[i].suffix, 2) == 0)
            return i;
    }

    return -EINVAL;
}

Return<void> BootControl::markBootSuccessful(markBootSuccessful_cb _hidl_cb) {
    smd_partition_t smd_partition;
    int slot = getCurrentSlot();

    if (!readSlotMetadata(&smd_partition)) {
        _hidl_cb(CommandResult{false, "Failed to read metadata"});
        return Void();
    }

    smd_partition.slot_info[slot].boot_successful = 1;
    smd_partition.slot_info[slot].retry_count = MAX_COUNT;

    if (!writeSlotMetadata(&smd_partition)) {
        _hidl_cb(CommandResult{false, "Failed to write metadata"});
        return Void();
    }

    _hidl_cb(CommandResult{true, ""});
    return Void();
}

Return<void> BootControl::setActiveBootSlot(uint32_t slot, setActiveBootSlot_cb _hidl_cb) {
    smd_partition_t smd_partition;
    int slot_s = getCurrentSlot();

    if (!readSlotMetadata(&smd_partition)) {
        _hidl_cb(CommandResult{false, "Failed to read metadata"});
        return Void();
    }

    if (slot >= smd_partition.num_slots) {
        _hidl_cb(CommandResult{false, "Requested slot is larger than available slots"});
        return Void();
    }

    /*
     * Set the target slot priority to max value 15.
     * and reset the retry count to 7.
     */
    smd_partition.slot_info[slot].priority = 15;
    smd_partition.slot_info[slot].boot_successful = 0;
    smd_partition.slot_info[slot].retry_count = MAX_COUNT;

    /*
     * Since we use target slot to boot,
     * lower source slot priority.
     */
    smd_partition.slot_info[slot_s].priority = 14;

    if (!writeSlotMetadata(&smd_partition)) {
        _hidl_cb(CommandResult{false, "Failed to write metadata"});
        return Void();
    }

    _hidl_cb(CommandResult{true, ""});
    return Void();
}

Return<void> BootControl::setSlotAsUnbootable(uint32_t slot, setSlotAsUnbootable_cb _hidl_cb) {
    smd_partition_t smd_partition;

    if (!readSlotMetadata(&smd_partition)) {
        _hidl_cb(CommandResult{false, "Failed to read metadata"});
        return Void();
    }

    if (slot >= smd_partition.num_slots) {
        _hidl_cb(CommandResult{false, "Requested slot is larger than available slots"});
        return Void();
    }

    /*
     * As this slot is unbootable, set all of value to zero
     * so boot-loader does not rollback to this slot.
     */
    smd_partition.slot_info[slot].priority = 0;
    smd_partition.slot_info[slot].boot_successful = 0;
    smd_partition.slot_info[slot].retry_count = 0;

    if (!writeSlotMetadata(&smd_partition)) {
        _hidl_cb(CommandResult{false, "Failed to write metadata"});
        return Void();
    }

    _hidl_cb(CommandResult{true, ""});
    return Void();
}

Return<BoolResult> BootControl::isSlotBootable(uint32_t slot) {
    smd_partition_t smd_partition;

    if (!readSlotMetadata(&smd_partition))
        return BoolResult::FALSE;

    if (slot >= smd_partition.num_slots)
        return BoolResult::INVALID_SLOT;

    return static_cast<BoolResult>(smd_partition.slot_info[slot].priority != 0);
}

Return<BoolResult> BootControl::isSlotMarkedSuccessful(uint32_t slot) {
    smd_partition_t smd_partition;

    if (!readSlotMetadata(&smd_partition))
        return BoolResult::FALSE;

    if (slot >= smd_partition.num_slots)
        return BoolResult::INVALID_SLOT;

    return static_cast<BoolResult>(smd_partition.slot_info[slot].boot_successful);
}

Return<void> BootControl::getSuffix(uint32_t slot, getSuffix_cb _hidl_cb) {
    smd_partition_t smd_partition;

    if (!readSlotMetadata(&smd_partition)) {
        _hidl_cb(NULL);
        return Void();
    }

    if (slot >= smd_partition.num_slots) {
        _hidl_cb(NULL);
        return Void();
    }

    _hidl_cb(smd_partition.slot_info[slot].suffix);

    return Void();
}

soc_type_t BootControl::getSocType()
{
    soc_type_t soc_type = SOC_TYPE_UNKNOWN;

    std::ifstream file("/proc/device-tree/compatible", std::ios::binary);
    if (!file.is_open())
        return SOC_TYPE_UNKNOWN;

    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0);
    std::vector<char> compatible(size, '\0');
    file.read(compatible.data(), size);

    std::string search_type = "nvidia,tegra210";
    if (std::search(compatible.begin(), compatible.end(), search_type.begin(),
                    search_type.end()) !=
        compatible.end()) {
        soc_type = SOC_TYPE_T210;
    }

    search_type = "nvidia,tegra186";
    if (std::search(compatible.begin(), compatible.end(), search_type.begin(),
                    search_type.end()) !=
        compatible.end()) {
        soc_type = SOC_TYPE_T186;
    }

    search_type = "nvidia,tegra194";
    if (std::search(compatible.begin(), compatible.end(), search_type.begin(),
                    search_type.end()) !=
        compatible.end()) {
        soc_type = SOC_TYPE_T194;
    }

    return soc_type;
}

BootControl::BootControl() {
    int32_t smd_info_offset = 0;
    soc_type_t soc_type = getSocType();
    smd_info_t smd_user = { TEGRABL_STORAGE_SDMMC_USER, 3, 0, 4096 };

    switch (soc_type) {
        case SOC_TYPE_T186:
            smd_info_offset = SMD_INFO_OFFSET_T18x;
            break;

        case SOC_TYPE_T194:
            smd_info_offset = SMD_INFO_OFFSET_T19x;
            break;

        // Cannot read bct on t210, attempt to use a userspace visible SMD
        case SOC_TYPE_T210:
        // If soc is unknown, attempt to use a userspace visible SMD
        case SOC_TYPE_UNKNOWN:
            smd_device = BOOTCTRL_SLOTMETADATA_FILE_DEFAULT;
            smd_info = smd_user;
            break;
    }

    if (smd_info_offset) {
        smd_device = GetProperty("vendor.tegra.ota.boot_device", BOOTCTRL_SLOTMETADATA_FILE_DEFAULT);

        if (smd_device.compare(BOOTCTRL_SLOTMETADATA_FILE_DEFAULT) == 0) {
            smd_info = smd_user;
            return;
        }

        std::ifstream smd_info_fd(smd_device, std::ios::binary);
        if (!smd_info_fd.is_open())
            return;

        smd_info_fd.seekg(smd_info_offset);
        smd_info_fd.read((char*)&smd_info, sizeof(smd_info_t));

        switch (smd_info.device_type) {
            case TEGRABL_STORAGE_SDMMC_USER:
            case TEGRABL_STORAGE_SATA:
            case TEGRABL_STORAGE_USB_MS:
            case TEGRABL_STORAGE_SDCARD:
            case TEGRABL_STORAGE_UFS_USER:
            case TEGRABL_STORAGE_NVME:
                smd_device = BOOTCTRL_SLOTMETADATA_FILE_DEFAULT;
                smd_info.start_sector = 0;
                break;
        }
    }
}

IBootControl* HIDL_FETCH_IBootControl(const char* /* hal */) {
    return new BootControl();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace boot
}  // namespace hardware
}  // namespace android

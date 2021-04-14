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

#include "bootctrl_nvidia.h"

#include <android-base/properties.h>
#include <hardware/boot_control.h>
#include <fstream>
#include <vector>
#include <zlib.h>

using ::android::base::GetProperty;

std::string smd_device;
smd_info_t smd_info;

bool accessSlotMetadata(smd_partition_t *smd_partition, bool writed) {
    ssize_t size = sizeof(smd_partition_t);
    ssize_t crc_size = size - sizeof(uint32_t);
    char *buf = (char*)smd_partition;

    std::fstream smd(smd_device, std::ios::binary | std::ios::in | std::ios::out);
    if(!smd.is_open()) {
        printf("Fail to open metadata file\n");
        return false;
    }
    smd.seekg(smd_info.start_sector * 512);
    if (smd.fail()) {
        printf("Error seeking to metadata offset\n");
        return false;
    }

    /* Read/Write slot_medata */
    if (writed) {
        smd_partition->crc32 = crc32(0, (const unsigned char*)buf, crc_size);
        smd.write(buf, size);
        smd.flush();
    } else {
        smd.read(buf, size);
    }

    if (smd.fail()) {
        printf("Fail to %s slot metadata \n", (writed)?("write"):("read"));
        return false;
    }

    return true;
}

unsigned getNumberSlots(boot_control_module_t *module __unused) {
    smd_partition_t smd_partition;

    if (!accessSlotMetadata(&smd_partition, false))
        return -EIO;

    return smd_partition.num_slots;
}

unsigned getCurrentSlot(boot_control_module_t *module __unused) {
    smd_partition_t smd_partition;
    std::string slot_suffix = GetProperty("ro.boot.slot_suffix", "");

    if (!accessSlotMetadata(&smd_partition, false))
        return -EIO;

    for (uint8_t i = 0 ; i < smd_partition.num_slots; i++) {
        if (slot_suffix.compare(0, 2, smd_partition.slot_info[i].suffix, 2) == 0)
            return i;
    }

    return -EINVAL;
}

int markBootSuccessful(boot_control_module_t *module) {
    smd_partition_t smd_partition;
    int slot = getCurrentSlot(module);

    if (!accessSlotMetadata(&smd_partition, false))
        return -EIO;

    smd_partition.slot_info[slot].boot_successful = 1;
    smd_partition.slot_info[slot].retry_count = MAX_COUNT;

    return (accessSlotMetadata(&smd_partition, true) ? 0 : -EIO);
}

int setActiveBootSlot(boot_control_module_t *module, unsigned slot) {
    smd_partition_t smd_partition;
    int slot_s = getCurrentSlot(module);

    if (!accessSlotMetadata(&smd_partition, false))
        return -EIO;

    if (slot >= smd_partition.num_slots)
        return -EINVAL;

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

    return (accessSlotMetadata(&smd_partition, true) ? 0 : -EIO);
}

int setSlotAsUnbootable(boot_control_module_t *module __unused, unsigned slot) {
    smd_partition_t smd_partition;

    if (!accessSlotMetadata(&smd_partition, false))
        return -EIO;

    if (slot >= smd_partition.num_slots)
        return -EINVAL;

    /*
     * As this slot is unbootable, set all of value to zero
     * so boot-loader does not rollback to this slot.
     */
    smd_partition.slot_info[slot].priority = 0;
    smd_partition.slot_info[slot].boot_successful = 0;
    smd_partition.slot_info[slot].retry_count = 0;

    return (accessSlotMetadata(&smd_partition, true) ? 0 : -EIO);
}

int isSlotBootable(boot_control_module_t *module __unused, unsigned slot) {
    smd_partition_t smd_partition;

    if (!accessSlotMetadata(&smd_partition, false))
        return -EIO;

    if (slot >= smd_partition.num_slots)
        return -EINVAL;

    return (smd_partition.slot_info[slot].priority != 0 ? 1 : 0);
}

int isSlotMarkedSuccessful(boot_control_module_t *module __unused, unsigned slot) {
    smd_partition_t smd_partition;

    if (!accessSlotMetadata(&smd_partition, false))
        return -EIO;

    if (slot >= smd_partition.num_slots)
        return -EINVAL;

    return (smd_partition.slot_info[slot].boot_successful ? 1 : 0);
}

char suffix[2];
const char* getSuffix(boot_control_module_t *module __unused, unsigned slot) {
    smd_partition_t smd_partition;

    if (!accessSlotMetadata(&smd_partition, false))
        return NULL;

    if (slot >= smd_partition.num_slots)
        return NULL;

    strncpy(suffix, smd_partition.slot_info[slot].suffix, 2);

    return suffix;
}

soc_type_t getSocType()
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

void BootControl(boot_control_module_t *module __unused) {
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

boot_control_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag                 = HARDWARE_MODULE_TAG,
        .module_api_version  = BOOT_CONTROL_MODULE_API_VERSION_0_1,
        .hal_api_version     = HARDWARE_HAL_API_VERSION,
        .id                  = BOOT_CONTROL_HARDWARE_MODULE_ID,
        .name                = "Nvidia Boot Control HAL",
        .author              = "Nvidia Corporation",
    },
    .init                   = BootControl,
    .getNumberSlots         = getNumberSlots,
    .getCurrentSlot         = getCurrentSlot,
    .markBootSuccessful     = markBootSuccessful,
    .setActiveBootSlot      = setActiveBootSlot,
    .setSlotAsUnbootable    = setSlotAsUnbootable,
    .isSlotBootable         = isSlotBootable,
    .getSuffix              = getSuffix,
    .isSlotMarkedSuccessful = isSlotMarkedSuccessful,
};

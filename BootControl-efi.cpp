/*
 * Copyright (C) 2023 The LineageOS Project
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

#include "BootControl-efi.h"

#include <fstream>

extern "C" {
#include <efivar/efivar.h>
}

#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>

static const char *slot_suffixes[2] = {
  BOOTCTRL_SUFFIX_A,
  BOOTCTRL_SUFFIX_B
};

namespace android {
namespace hardware {
namespace boot {
namespace V1_0 {
namespace implementation {

uint16_t BootControl::initializeDevMem() {
    uint16_t value = 0; 
    uint32_t *retrymax = NULL;
    size_t retrymax_size = 0;
    uint32_t attributes;
    if (efi_get_variable(EFI_NVIDIA_PUBLIC_VARIABLE_GUID, "RootfsRetryCountMax",
		    (uint8_t **)&retrymax, &retrymax_size, &attributes) < 0)
        return 0xFFFF;

    if (retrymax_size != 4) {
        if (retrymax != NULL)
            free (retrymax);
        return 0xFFFF;
    }

    value |= (*retrymax & 0x0003) << 2;
    value |= (*retrymax & 0x0003) << 4;

    free (retrymax);

    if (!writeDevMem(value))
        return 0xFFFF;

    return value;
}

uint16_t BootControl::readDevMem() {
    void *map_base;
    unsigned page_size, offset_in_page;
    int fd;
    uint32_t value;

    fd = open("/dev/mem", (O_RDONLY | O_SYNC));
    if (fd < 0)
        return 0xFFFF;

    page_size = getpagesize();
    offset_in_page = dev_mem_addr & (page_size - 1);

    map_base = mmap(NULL, page_size, PROT_READ, MAP_SHARED, fd,
        dev_mem_addr & ~(off_t)(page_size - 1));
    if (map_base == MAP_FAILED) {
        close(fd);
        return 0xFFFF;
    }

    value = *(volatile uint32_t*)((char*)map_base + offset_in_page);

    munmap(map_base, page_size);
    close(fd);

    if ((value & 0x0000FFFF) != 0x0000FACE)
        return initializeDevMem();

    return (uint16_t)(value >> 16);
}

bool BootControl::writeDevMem(uint16_t value) {
    void *map_base;
    unsigned page_size, offset_in_page;
    int fd;

    fd = open("/dev/mem", (O_RDWR | O_SYNC));
    if (fd < 0)
        return false;

    page_size = getpagesize();
    offset_in_page = dev_mem_addr & (page_size - 1);

    map_base = mmap(NULL, page_size, PROT_WRITE, MAP_SHARED, fd,
        dev_mem_addr & ~(off_t)(page_size - 1));
    if (map_base == MAP_FAILED) {
        close(fd);
        return false;
    }

    *(volatile uint32_t*)((char*)map_base + offset_in_page) =
        ((uint32_t)value << 16) | 0x0000FACE;

    munmap(map_base, page_size);
    close(fd);

    return true;
}

// Methods from ::android::hardware::boot::V1_0::IBootControl follow.
Return<uint32_t> BootControl::getNumberSlots() {
    return MAX_SLOTS;
}

Return<uint32_t> BootControl::getCurrentSlot() {
    uint32_t *slot = NULL;
    size_t slot_size = 0;
    uint32_t attributes;
    uint32_t retval;

    if (efi_get_variable(EFI_NVIDIA_PUBLIC_VARIABLE_GUID, "BootChainFwCurrent",
		    (uint8_t **)&slot, &slot_size, &attributes) < 0)
        return -EIO;

    if (slot_size != 4) {
        if (slot != NULL)
            free (slot);
        return -EINVAL;
    }

    retval = *slot;
    free (slot);
    return retval;

}

bool BootControl::setBootSuccessful(uint32_t slot) {
    uint16_t value = readDevMem(); 
    if (value == 0xFFFF)
        return false;

    uint32_t *retrymax = NULL;
    size_t retrymax_size = 0;
    uint32_t attributes;
    if (efi_get_variable(EFI_NVIDIA_PUBLIC_VARIABLE_GUID, "RootfsRetryCountMax",
		    (uint8_t **)&retrymax, &retrymax_size, &attributes) < 0)
        return false;
    
    if (retrymax_size != 4) {
        if (retrymax != NULL)
            free (retrymax);
        return false;
    }

    value &= (slot ? 0xFFF3 : 0xFFCF);
    value |= (*retrymax & 0x0003) << (slot ? 2 : 4);

    free (retrymax);

    if (!writeDevMem(value))
        return false;

    uint32_t bootsuccess = 0;
    std::string rootfsslot("RootfsStatusSlot");
    rootfsslot += (slot ? "B" : "A");
    if (efi_set_variable(EFI_NVIDIA_PUBLIC_VARIABLE_GUID, rootfsslot.c_str(),
                         (uint8_t *)&bootsuccess, sizeof(bootsuccess),
                         EFI_VARIABLE_NON_VOLATILE |
                         EFI_VARIABLE_BOOTSERVICE_ACCESS |
                         EFI_VARIABLE_RUNTIME_ACCESS,
                         0644) < 0)
        return false;

    return true;
}

Return<void> BootControl::markBootSuccessful(markBootSuccessful_cb _hidl_cb) {
    uint32_t slot = getCurrentSlot();
    if (slot < 0 || slot > getNumberSlots()) {
        _hidl_cb(CommandResult{false, "Failed to get current slot"});
        return Void();
    }

    if (!setBootSuccessful(slot)) {
        _hidl_cb(CommandResult{false, "Failed to set devmem or efivars"});
        return Void();
    }
    
    _hidl_cb(CommandResult{true, ""});
    return Void();
}

Return<void> BootControl::setActiveBootSlot(uint32_t slot, setActiveBootSlot_cb _hidl_cb) {
    if (efi_set_variable(EFI_NVIDIA_PUBLIC_VARIABLE_GUID, "BootChainFwNext",
                         (uint8_t *)&slot, sizeof(slot),
                         EFI_VARIABLE_NON_VOLATILE |
                         EFI_VARIABLE_BOOTSERVICE_ACCESS |
                         EFI_VARIABLE_RUNTIME_ACCESS,
                         0644) < 0) {
        _hidl_cb(CommandResult{false, "Failed to set boot chain next"});
        return Void();
    }

    if (!setBootSuccessful(slot)) {
        _hidl_cb(CommandResult{false, "Failed to set devmem or efivars"});
        return Void();
    }

    _hidl_cb(CommandResult{true, ""});
    return Void();
}

Return<void> BootControl::setSlotAsUnbootable(uint32_t slot, setSlotAsUnbootable_cb _hidl_cb) {
    uint32_t bootfail = 0x000000FF;
    std::string rootfsslot("RootfsStatusSlot");
    rootfsslot += (slot ? "B" : "A");
    if (efi_set_variable(EFI_NVIDIA_PUBLIC_VARIABLE_GUID, rootfsslot.c_str(),
                         (uint8_t *)&bootfail, sizeof(bootfail),
                         EFI_VARIABLE_NON_VOLATILE |
                         EFI_VARIABLE_BOOTSERVICE_ACCESS |
                         EFI_VARIABLE_RUNTIME_ACCESS,
                         0644) < 0) {
        _hidl_cb(CommandResult{false, "Failed to set rootfs status"});
        return Void();
    }

    _hidl_cb(CommandResult{true, ""});
    return Void();
}

Return<BoolResult> BootControl::isSlotBootable(uint32_t slot) {
    if (slot >= getNumberSlots())
        return BoolResult::INVALID_SLOT;

    uint16_t value = readDevMem(); 
    if (value == 0xFFFF)
        return BoolResult::FALSE;

    return static_cast<BoolResult>((value & (slot ? 0x000C : 0x0030)) != 0);
}

Return<BoolResult> BootControl::isSlotMarkedSuccessful(uint32_t slot) {
    uint32_t *status = NULL;
    size_t status_size = 0;
    uint32_t attributes;
    bool retval = false;

    if (slot >= getNumberSlots())
        return BoolResult::INVALID_SLOT;

    std::string rootfsslot("RootfsStatusSlot");
    rootfsslot += (slot ? "B" : "A");
    if (efi_get_variable(EFI_NVIDIA_PUBLIC_VARIABLE_GUID, rootfsslot.c_str(),
		    (uint8_t **)&status, &status_size, &attributes) < 0)
        return BoolResult::FALSE;
    
    if (status_size != 4) {
        if (status != NULL)
            free (status);
        return BoolResult::FALSE;
    }

    retval = (*status == 0);
    free (status);

    return static_cast<BoolResult>(retval);
}

Return<void> BootControl::getSuffix(uint32_t slot, getSuffix_cb _hidl_cb) {
    if (slot >= getNumberSlots()) {
        _hidl_cb(NULL);
        return Void();
    }

    _hidl_cb(slot_suffixes[slot]);

    return Void();
}

soc_type_t BootControl::getSocType()
{
    std::ifstream file("/proc/device-tree/compatible", std::ios::binary);
    if (!file.is_open())
        return SOC_TYPE_UNKNOWN;

    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0);
    std::vector<char> compatible(size, '\0');
    file.read(compatible.data(), size);

    std::string search_type = "nvidia,tegra194";
    if (std::search(compatible.begin(), compatible.end(), search_type.begin(),
                    search_type.end()) !=
        compatible.end()) {
        return SOC_TYPE_T194;
    }

    search_type = "nvidia,tegra234";
    if (std::search(compatible.begin(), compatible.end(), search_type.begin(),
                    search_type.end()) !=
        compatible.end()) {
        return SOC_TYPE_T234;
    }

    search_type = "nvidia,tegra239";
    if (std::search(compatible.begin(), compatible.end(), search_type.begin(),
                    search_type.end()) !=
        compatible.end()) {
        return SOC_TYPE_T239;
    }

    // Less likely socs
    search_type = "nvidia,tegra210";
    if (std::search(compatible.begin(), compatible.end(), search_type.begin(),
                    search_type.end()) !=
        compatible.end()) {
        return SOC_TYPE_T210;
    }

    search_type = "nvidia,tegra186";
    if (std::search(compatible.begin(), compatible.end(), search_type.begin(),
                    search_type.end()) !=
        compatible.end()) {
        return SOC_TYPE_T186;
    }

    return SOC_TYPE_UNKNOWN;
}

BootControl::BootControl() {
    switch (getSocType()) {
        case SOC_TYPE_T186:
            dev_mem_addr = DEV_MEM_ADDR_T18x;
            break;

        case SOC_TYPE_T194:
            dev_mem_addr = DEV_MEM_ADDR_T19x;
            break;

        case SOC_TYPE_T234:
        case SOC_TYPE_T239:
            dev_mem_addr = DEV_MEM_ADDR_T23x;
            break;

        case SOC_TYPE_T210:
        case SOC_TYPE_UNKNOWN:
            dev_mem_addr = 0;
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

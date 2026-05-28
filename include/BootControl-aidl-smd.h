/*
 * Copyright (C) 2024 The LineageOS Project
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

#pragma once

#include <android-base/properties.h>
#include <aidl/android/hardware/boot/BnBootControl.h>

#include "bootctrl_nvidia.h"

namespace aidl::android::hardware::boot {

using ::android::base::GetProperty;

class BootControl final : public BnBootControl {
  public:
    BootControl();

    ::ndk::ScopedAStatus getActiveBootSlot(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getNumberSlots(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus getCurrentSlot(int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus markBootSuccessful() override;
    ::ndk::ScopedAStatus setActiveBootSlot(int32_t slot) override;
    ::ndk::ScopedAStatus setSlotAsUnbootable(int32_t slot) override;
    ::ndk::ScopedAStatus isSlotBootable(int32_t slot, bool* _aidl_return) override;
    ::ndk::ScopedAStatus isSlotMarkedSuccessful(int32_t slot, bool* _aidl_return) override;
    ::ndk::ScopedAStatus getSuffix(int32_t slot, std::string* _aidl_return) override;
    ::ndk::ScopedAStatus getSnapshotMergeStatus(
            ::aidl::android::hardware::boot::MergeStatus* _aidl_return) override;
    ::ndk::ScopedAStatus setSnapshotMergeStatus(
            ::aidl::android::hardware::boot::MergeStatus in_status) override;

  private:
    std::string smd_device;
    smd_info_t smd_info;

    bool setBootSuccessful(int32_t slot);
    bool readSlotMetadata(smd_partition_t *smd_partition);
    bool writeSlotMetadata(smd_partition_t *smd_partition);
    bool validateSlotMetadata();
    soc_type_t getSocType();
};

}  // namespace aidl::android::hardware::boot

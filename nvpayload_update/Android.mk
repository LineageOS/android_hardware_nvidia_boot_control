# Copyright (C) 2016 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

common_cflags := \
    -Wa,--noexecstack \
    -Wall \
    -Werror \
    -Wextra \
    -Wformat=2 \
    -Wno-psabi \
    -Wno-unused-parameter \
    -ffunction-sections \
    -fstack-protector-strong \
    -fvisibility=hidden
common_cppflags := \
    -Wnon-virtual-dtor \
    -fno-strict-aliasing \
    -std=gnu++11

include $(CLEAR_VARS)
LOCAL_C_INCLUDES := \
    external/gptfdisk \
    $(LOCAL_PATH)/../include
LOCAL_SRC_FILES := \
    nv_bootloader_payload_updater.cpp \
    gpt/gpttegra.cpp
LOCAL_CFLAGS := $(common_cflags)
LOCAL_CFLAGS += -Wno-sign-compare
LOCAL_CPPFLAGS := $(common_cppflags)
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_STATIC_LIBRARIES := libchrome liblog libbase libext2_uuid libsgdisk
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE := nv_bootloader_payload_updater
include $(BUILD_EXECUTABLE)

# Copyright (C) 2015 The Android Open Source Project
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

include $(CLEAR_VARS)

LOCAL_MODULE := bootctrl.tegra
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES = $(LOCAL_PATH)/include
LOCAL_HEADER_LIBRARIES := libhardware_headers libbase_headers
LOCAL_SRC_FILES := BootControl.cpp

LOCAL_SHARED_LIBRARIES := libcutils libz

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := bootctrl.tegra
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES = $(LOCAL_PATH)/include external/zlib
LOCAL_HEADER_LIBRARIES := libhardware_headers libbase_headers
LOCAL_SRC_FILES := BootControl.cpp

include $(BUILD_STATIC_LIBRARY)

include $(call all-makefiles-under,$(LOCAL_PATH))

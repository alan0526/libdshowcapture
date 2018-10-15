/*
 *  Copyright (C) 2014 Hugh Bailey <obs.jim@gmail.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 */

#include "../dshowcapture.hpp"
#include "dshow-base.hpp"
#include "dshow-enum.hpp"
#include "device.hpp"
#include "dshow-device-defs.hpp"
#include "log.hpp"
#include <iostream>
#include <atlconv.h>
//#include "regedit.hpp"

#include <vector>

#include <SetupAPI.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <Devpkey.h>
#include <locale>
#include <algorithm>

#pragma comment(lib, "uuid.lib")
#pragma comment (lib, "setupapi.lib")

namespace DShow {

#define GUID_CAMERA_STRING L"{65e8773d-8f56-11d0-a3b9-00a0c9223196}"

std::vector<std::string> split_string(const std::string& src_str, const std::string& delimStr, bool repeated_char_ignored = false) {
    std::vector<std::string> strings;

	auto process_str = src_str;
	replace_if(process_str.begin(), process_str.end(), [&](const char& c) {
	    if (delimStr.find(c) != std::string::npos) { return true; }
	    else { return false; }
    }, delimStr.at(0));

	size_t pos = process_str.find(delimStr.at(0));
    std::string addedString = "";
	while (pos != std::string::npos) {
		addedString = process_str.substr(0, pos);
		if (!addedString.empty() || !repeated_char_ignored) {
			strings.push_back(addedString);
		}
		process_str.erase(process_str.begin(), process_str.begin() + pos + 1);
		pos = process_str.find(delimStr.at(0));
	}

	addedString = process_str;
	if (!addedString.empty() || !repeated_char_ignored) {
		strings.push_back(addedString);
	}

	return strings;
}

Device::Device(InitGraph initialize) : context(new HDevice) {
    if (initialize == InitGraph::True)
        context->CreateGraph();
}

Device::~Device() {
    delete context;
}

bool Device::Valid() const {
    return context->initialized;
}

bool Device::ResetGraph() {
    /* cheap and easy way to clear all the filters */
    delete context;
    context = new HDevice;

    return context->CreateGraph();
}

void Device::ShutdownGraph() {
    delete context;
    context = new HDevice;
}

bool Device::SetVideoConfig(VideoConfig* config) {
    return context->SetVideoConfig(config);
}

bool Device::SetAudioConfig(AudioConfig* config) {
    return context->SetAudioConfig(config);
}

bool Device::ConnectFilters() {
    return context->ConnectFilters();
}

Result Device::Start() {
    return context->Start();
}

void Device::Stop() {
    context->Stop();
}

bool Device::GetVideoConfig(VideoConfig& config) const {
    if (context->videoCapture == nullptr)
        return false;

    config = context->videoConfig;
    return true;
}

bool Device::GetAudioConfig(AudioConfig& config) const {
    if (context->audioCapture == nullptr)
        return false;

    config = context->audioConfig;
    return true;
}

bool Device::GetVideoDeviceId(DeviceId& id) const {
    if (context->videoCapture == nullptr)
        return false;

    id = context->videoConfig;
    return true;
}

bool Device::GetAudioDeviceId(DeviceId& id) const {
    if (context->audioCapture == nullptr)
        return false;

    id = context->audioConfig;
    return true;
}

static void OpenPropertyPages(HWND hwnd, IUnknown* propertyObject) {
    if (!propertyObject)
        return;

    ComQIPtr<ISpecifyPropertyPages> pages(propertyObject);
    CAUUID cauuid;

    if (pages != nullptr) {
        if (SUCCEEDED(pages->GetPages(&cauuid)) && cauuid.cElems) {
            OleCreatePropertyFrame(hwnd, 0, 0, nullptr, 1,
                                   (LPUNKNOWN*)&propertyObject,
                                   cauuid.cElems, cauuid.pElems,
                                   0, 0, nullptr);
            CoTaskMemFree(cauuid.pElems);
        }
    }
}

void Device::OpenDialog(void* hwnd, DialogType type) const {
    ComPtr<IUnknown> ptr;
    HRESULT hr;

    if (type == DialogType::ConfigVideo) {
        ptr = context->videoFilter;
    }
    else if (type == DialogType::ConfigCrossbar ||
        type == DialogType::ConfigCrossbar2) {
        hr = context->builder->FindInterface(nullptr, nullptr,
                                             context->videoFilter, IID_IAMCrossbar,
                                             (void**)&ptr);
        if (FAILED(hr)) {
            WarningHR(L"Failed to find crossbar", hr);
            return;
        }

        if (ptr != nullptr && type == DialogType::ConfigCrossbar2) {
            ComQIPtr<IAMCrossbar> xbar(ptr);
            ComQIPtr<IBaseFilter> filter(xbar);

            hr = context->builder->FindInterface(
                &LOOK_UPSTREAM_ONLY,
                nullptr, filter, IID_IAMCrossbar,
                (void**)&ptr);
            if (FAILED(hr)) {
                WarningHR(L"Failed to find crossbar2", hr);
                return;
            }
        }
    }
    else if (type == DialogType::ConfigAudio) {
        ptr = context->audioFilter;
    }

    if (!ptr) {
        Warning(L"Could not find filter to open dialog type: %d with",
                (int)type);
        return;
    }

    OpenPropertyPages((HWND)hwnd, ptr);
}

static void EnumEncodedVideo(vector<VideoDevice>& devices,
                             const wchar_t* deviceName, const wchar_t* devicePath,
                             const EncodedDevice& info) {
    VideoDevice device;
    VideoInfo caps;

    device.name = deviceName;
    device.path = devicePath;
    device.audioAttached = true;
    device.separateAudioFilter = false;

    caps.minCX = caps.maxCX = info.width;
    caps.minCY = caps.maxCY = info.height;
    caps.granularityCX = caps.granularityCY = 1;
    caps.minInterval = caps.maxInterval = info.frameInterval;
    caps.format = info.videoFormat;

    device.caps.push_back(caps);
    devices.push_back(device);
}

static void EnumExceptionVideoDevice(vector<VideoDevice>& devices,
                                     IBaseFilter* filter,
                                     const wchar_t* deviceName,
                                     const wchar_t* devicePath) {
    ComPtr<IPin> pin;

    if (GetPinByName(filter, PINDIR_OUTPUT, L"656", &pin))
        EnumEncodedVideo(devices, deviceName, devicePath, HD_PVR2);

    else if (GetPinByName(filter, PINDIR_OUTPUT, L"TS Out", &pin))
        EnumEncodedVideo(devices, deviceName, devicePath, Roxio);
}

static bool EnumVideoDevice(vector<VideoDevice>& devices,
                            IBaseFilter* filter,
                            const wchar_t* deviceName,
                            const wchar_t* devicePath) {
    ComPtr<IPin> pin;
    ComPtr<IPin> audioPin;
    ComPtr<IBaseFilter> audioFilter;
    VideoDevice info;

    if (wcsstr(deviceName, L"C875") != nullptr ||
        wcsstr(deviceName, L"Prif Streambox") != nullptr ||
        wcsstr(deviceName, L"C835") != nullptr) {
        EnumEncodedVideo(devices, deviceName, devicePath, AV_LGP);
        return true;

    }
    if (wcsstr(deviceName, L"Hauppauge HD PVR Capture") != nullptr) {
        EnumEncodedVideo(devices, deviceName, devicePath, HD_PVR1);
        return true;
    }

    bool success = GetFilterPin(filter, MEDIATYPE_Video,
                                PIN_CATEGORY_CAPTURE, PINDIR_OUTPUT, &pin);

    /* if this device has no standard capture pin, see if it's an
     * encoded device, and get its information if so (all encoded devices
     * are exception devices pretty much) */
    if (!success) {
        EnumExceptionVideoDevice(devices, filter, deviceName,
                                 devicePath);
        return true;
    }

    if (!EnumVideoCaps(pin, info.caps))
        return true;

    info.audioAttached = GetFilterPin(filter, MEDIATYPE_Audio,
                                      PIN_CATEGORY_CAPTURE, PINDIR_OUTPUT, &audioPin);

    // Fallback: Find a corresponding audio filter for the same device
    if (!info.audioAttached) {
        info.separateAudioFilter = GetDeviceAudioFilter(devicePath, &audioFilter);
        info.audioAttached = info.separateAudioFilter;
    }

    info.name = deviceName;
    if (devicePath)
        info.path = devicePath;

    devices.push_back(info);
    return true;
}

std::wstring get_location_info(const std::wstring& device_path) {
    USES_CONVERSION;
    std::wstring ret;

    HDEVINFO DeviceInfoSet = INVALID_HANDLE_VALUE;
    SP_DEVINFO_DATA DeviceInfoData;
    WCHAR DeviceInstanceID[2048] = { 0 };
    WCHAR DeviceLocationInfo[2048] = { 0 };
    DEVPROPTYPE PropertyType;

    GUID guid;
    CLSIDFromString(GUID_CAMERA_STRING, &guid);

    DeviceInfoSet = SetupDiGetClassDevs(&guid,
                                        nullptr,
                                        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (DeviceInfoSet == INVALID_HANDLE_VALUE) {
        goto Exit;
    }

    ZeroMemory(&DeviceInfoData, sizeof(DeviceInfoData));
    DeviceInfoData.cbSize = sizeof(DeviceInfoData);

    for (DWORD Index = 0;
         SetupDiEnumDeviceInfo(DeviceInfoSet, Index, &DeviceInfoData);
         Index++) {
        if (!SetupDiGetDeviceProperty(DeviceInfoSet,
            &DeviceInfoData,
            &DEVPKEY_Device_InstanceId,
            &PropertyType,
            reinterpret_cast<PBYTE>(DeviceInstanceID),
            sizeof(DeviceInstanceID),
            nullptr,
            0)) {
            continue;
        }

        auto device_instance_id = std::wstring(DeviceInstanceID);
        std::transform(device_instance_id.begin(), device_instance_id.end(), device_instance_id.begin(), ::tolower);

        const auto device_path_a = std::string(W2A(device_path.c_str()));
        const auto device_instance_id_a = std::string(W2A(device_instance_id.c_str()));

        const auto device_paths = split_string(device_path_a, "#");
        const auto device_instance_ids = split_string(device_instance_id_a, "\\");

        if(device_paths.size() >= 3 && device_instance_ids.size() >= 3) {
            if(device_paths[2] != device_instance_ids[2]) {
                continue;;
            }
        }

        if (!SetupDiGetDeviceProperty(DeviceInfoSet,
                                      &DeviceInfoData,
                                      &DEVPKEY_Device_LocationInfo,
                                      &PropertyType,
                                      reinterpret_cast<PBYTE>(DeviceLocationInfo),
                                      sizeof(DeviceLocationInfo),
                                      nullptr,
                                      0)) {
            continue;
        }

        ret = std::wstring(DeviceLocationInfo);
        break;
    }

    if (GetLastError() != ERROR_NO_MORE_ITEMS) {
    }

Exit:
    if (DeviceInfoSet != INVALID_HANDLE_VALUE) {
        SetupDiDestroyDeviceInfoList(DeviceInfoSet);
    }


    return ret;
}

bool Device::EnumVideoDevices(vector<VideoDevice>& devices) {
    devices.clear();
    const auto ret = EnumDevices(CLSID_VideoInputDeviceCategory,
                       EnumDeviceCallback(EnumVideoDevice), &devices);

    for (auto& device : devices) {
        device.location = get_location_info(device.path);
    }

    return ret;
}

static bool EnumAudioDevice(vector<AudioDevice>& devices,
                            IBaseFilter* filter,
                            const wchar_t* deviceName,
                            const wchar_t* devicePath) {
    ComPtr<IPin> pin;
    AudioDevice info;

    bool success = GetFilterPin(filter, MEDIATYPE_Audio,
                                PIN_CATEGORY_CAPTURE, PINDIR_OUTPUT, &pin);
    if (!success)
        return true;

    if (!EnumAudioCaps(pin, info.caps))
        return true;

    info.name = deviceName;
    if (devicePath)
        info.path = devicePath;

    devices.push_back(info);
    return true;
}

bool Device::EnumAudioDevices(vector<AudioDevice>& devices) {
    devices.clear();
    return EnumDevices(CLSID_AudioInputDeviceCategory,
                       EnumDeviceCallback(EnumAudioDevice), &devices);
}

}; /* namespace DShow */

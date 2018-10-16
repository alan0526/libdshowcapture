#include "pch.h"
#include <iostream>
#include <dshow.h>
#include "../../../dshowcapture.hpp"

#pragma comment(lib, "../Debug/dshowcapture.lib")

using namespace DShow;

void frame_callback(const VideoConfig &config, unsigned char *data, size_t size, long long startTime, long long stopTime) {
    std::cout << "get frame, size: (" << config.cx << "," << config.cy << 
        "), format: " << int(config.format) << 
        ", size: " << size <<
        std::endl;
}

int main() {
    CoInitialize(nullptr);
   
    auto device = Device();

    device.ResetGraph();

    VideoConfig vc;
    vc.useDefaultConfig = false;
    vc.cx = 1280;
    vc.cy = 720;
    vc.format = VideoFormat::RGB24;
    vc.path = L"";
    vc.location = L"0000.0014.0000.007.000.000.000.000.000";
    vc.callback = frame_callback;

    auto ret = device.Start(&vc);
    if (ret != Result::Success) {
        std::cout << "start device1 failed" << std::endl;
        CoUninitialize();
        return 0;
    }

    for(auto i = 0; i < 10; i++) {
        Sleep(100);
    }

    device.Stop();

    ////////////////////////////////////////////////////////////////////////////////////////////////////

    device.ResetGraph();

    vc.cx = 640;
    vc.cy = 480;
    vc.format = VideoFormat::RGB24;
    vc.path = L"";
    vc.location = L"0000.0014.0000.002.002.000.000.000.000";

    ret = device.Start(&vc);
    if (ret != Result::Success) {
        std::cout << "start device2 failed" << std::endl;
        CoUninitialize();
        return 0;
    }

    for (auto i = 0; i < 10; i++) {
        Sleep(100);
    }

    device.Stop();

    CoUninitialize();

    std::cout << "press enter to continue..." << std::endl;
    getchar();

    return 0;
}


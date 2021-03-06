#include "pch.h"
#include <iostream>
#include <dshow.h>
#include "../../../dshowcapture.hpp"

#ifdef _DEBUG
#pragma comment(lib, "../Debug/dshowcaptured.lib")
#else
#pragma comment(lib, "../Release/dshowcapture.lib")

#endif

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

    // const auto location = L"0000.0014.0000.011.000.000.000.000.000";
    const auto location = L"0000.0014.0000.002.002.000.000.000.000";

    VideoConfig vc;
    vc.useDefaultConfig = false;
    vc.cx = 1280;
    vc.cy = 720;
    vc.internalFormat = VideoFormat::Any;
    vc.format = VideoFormat::RGB24;
    vc.location = location;
    vc.callback = frame_callback;

    auto ret = device.Start(vc);
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
    vc.location = location;

    ret = device.Start(vc);
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


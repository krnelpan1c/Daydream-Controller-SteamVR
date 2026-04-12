#pragma once
#include "DaydreamPacket.h"
#include <functional>
#include <string>

class DaydreamBLEHandler {
public:
    DaydreamBLEHandler();
    ~DaydreamBLEHandler();

    void Start(std::function<void(const DaydreamData&)> onDataReceived, uint64_t bluetoothAddress = 0, std::wstring knownDeviceId = L"");
    void Stop();

private:
    std::function<void(const DaydreamData&)> m_onDataReceived;
    bool m_running;
    
    struct Impl;
    Impl* m_impl;
};

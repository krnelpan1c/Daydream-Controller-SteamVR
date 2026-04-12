#pragma once
#include "DaydreamPacket.h"
#include <functional>

class DaydreamBLEHandler {
public:
    DaydreamBLEHandler();
    ~DaydreamBLEHandler();

    void Start(std::function<void(const DaydreamData&)> onDataReceived);
    void Stop();

private:
    std::function<void(const DaydreamData&)> m_onDataReceived;
    bool m_running;
    
    struct Impl;
    Impl* m_impl;
};

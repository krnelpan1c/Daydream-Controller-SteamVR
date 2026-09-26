#pragma once
#include "DaydreamPacket.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

class DaydreamBLEHandler {
public:
    DaydreamBLEHandler();
    ~DaydreamBLEHandler();

    void Start(std::function<void(const DaydreamData&)> onDataReceived, uint64_t bluetoothAddress = 0, std::wstring knownDeviceId = L"");
    void Stop();

    // Wakes the connect loop so it attempts a connection right away. Call this when an
    // advertisement from the controller is seen, i.e. the controller is awake and connectable.
    void NotifyAdvertisement();

    uint64_t GetBluetoothAddress() const { return m_address; }
    bool IsSubscribed() const { return m_subscribed; }

private:
    void ConnectLoop(uint64_t bluetoothAddress, std::wstring knownDeviceId);
    bool TrySubscribe();

    std::function<void(const DaydreamData&)> m_onDataReceived;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_subscribed{false};
    std::atomic<uint64_t> m_address{0};
    std::thread m_thread;

    struct Impl;
    Impl* m_impl;
};

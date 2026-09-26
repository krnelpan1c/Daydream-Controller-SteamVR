#include "DaydreamBLEHandler.h"
#include <windows.h>
#define DriverLog(...)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <mutex>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Devices::Enumeration;

// How long to wait between connection attempts when no advertisement has been seen.
// An advertisement (NotifyAdvertisement) or a connection status change skips the wait.
static const DWORD kRetryIntervalMs = 2000;

struct DaydreamBLEHandler::Impl {
    BluetoothLEDevice m_device{ nullptr };
    GattSession m_session{ nullptr };
    GattCharacteristic m_characteristic{ nullptr };
    winrt::event_token m_valueChangedToken{};
    winrt::event_token m_connectionStatusToken{};
    HANDLE m_wakeEvent{ nullptr };

    // The async operation the connect thread is currently blocked on, so Stop() can cancel it
    // instead of waiting for a Bluetooth timeout.
    std::mutex m_opMutex;
    IAsyncInfo m_pendingOp{ nullptr };

    template <typename Op>
    auto Await(Op const& op, std::atomic<bool> const& running) {
        {
            std::lock_guard<std::mutex> lock(m_opMutex);
            m_pendingOp = op;
        }
        if (!running) op.Cancel();
        struct Clear {
            Impl* impl;
            ~Clear() { std::lock_guard<std::mutex> lock(impl->m_opMutex); impl->m_pendingOp = nullptr; }
        } clear{ this };
        return op.get();
    }

    void CancelPending() {
        std::lock_guard<std::mutex> lock(m_opMutex);
        if (m_pendingOp) {
            try { m_pendingOp.Cancel(); } catch (...) {}
        }
    }
};

DaydreamBLEHandler::DaydreamBLEHandler() : m_impl(new Impl()) {
    m_impl->m_wakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
}

DaydreamBLEHandler::~DaydreamBLEHandler() {
    Stop();
    CloseHandle(m_impl->m_wakeEvent);
    delete m_impl;
}

void DaydreamBLEHandler::Start(std::function<void(const DaydreamData&)> onDataReceived, uint64_t bluetoothAddress, std::wstring knownDeviceId) {
    if (m_running) return;
    m_onDataReceived = onDataReceived;
    m_running = true;
    m_address = bluetoothAddress;
    m_thread = std::thread(&DaydreamBLEHandler::ConnectLoop, this, bluetoothAddress, knownDeviceId);
}

void DaydreamBLEHandler::NotifyAdvertisement() {
    if (!m_subscribed) SetEvent(m_impl->m_wakeEvent);
}

void DaydreamBLEHandler::ConnectLoop(uint64_t bluetoothAddress, std::wstring knownDeviceId) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // Resolve the device object. For a paired device this comes from the system cache and
    // succeeds even while the controller is asleep; for an unpaired one it needs to be in range.
    while (m_running && !m_impl->m_device) {
        try {
            if (bluetoothAddress != 0) {
                m_impl->m_device = m_impl->Await(BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddress), m_running);
            } else if (!knownDeviceId.empty()) {
                m_impl->m_device = m_impl->Await(BluetoothLEDevice::FromIdAsync(knownDeviceId), m_running);
            } else {
                DriverLog("DaydreamBLEHandler: No device address or id given.\n");
                m_running = false;
                break;
            }
        } catch (winrt::hresult_error const& ex) {
            DriverLog("DaydreamBLEHandler Exception: %ls\n", ex.message().c_str());
        }
        if (!m_impl->m_device) WaitForSingleObject(m_impl->m_wakeEvent, kRetryIntervalMs);
    }

    if (m_impl->m_device) {
        m_address = m_impl->m_device.BluetoothAddress();

        // React immediately to the link coming up or going down instead of waiting for the next retry.
        m_impl->m_connectionStatusToken = m_impl->m_device.ConnectionStatusChanged([this](BluetoothLEDevice const& device, IInspectable const&) {
            if (device.ConnectionStatus() == BluetoothConnectionStatus::Disconnected) {
                DriverLog("DaydreamBLEHandler: Controller disconnected.\n");
                m_subscribed = false;
            }
            SetEvent(m_impl->m_wakeEvent);
        });
    }

    while (m_running && m_impl->m_device) {
        if (!m_impl->m_session) {
            try {
                // Keeps Windows auto-reconnecting to the controller in the background.
                m_impl->m_session = m_impl->Await(GattSession::FromDeviceIdAsync(m_impl->m_device.BluetoothDeviceId()), m_running);
                if (m_impl->m_session) m_impl->m_session.MaintainConnection(true);
            } catch (...) {
                DriverLog("DaydreamBLEHandler: Failed to initialize GattSession. Connection might drop.\n");
            }
        }

        if (!m_subscribed && TrySubscribe()) {
            m_subscribed = true;
            DriverLog("DaydreamBLEHandler: Connected and subscribed successfully!\n");
        }

        // Once subscribed, only wake on a connection status change or Stop(). Otherwise retry
        // periodically, or right away when the controller is seen advertising.
        WaitForSingleObject(m_impl->m_wakeEvent, m_subscribed ? INFINITE : kRetryIntervalMs);
    }

    winrt::uninit_apartment();
}

bool DaydreamBLEHandler::TrySubscribe() {
    try {
        if (!m_impl->m_characteristic) {
            guid serviceId = winrt::guid("0000fe55-0000-1000-8000-00805f9b34fb");
            auto servicesResult = m_impl->Await(m_impl->m_device.GetGattServicesForUuidAsync(serviceId, BluetoothCacheMode::Cached), m_running);
            if (servicesResult.Status() != GattCommunicationStatus::Success || servicesResult.Services().Size() == 0) {
                servicesResult = m_impl->Await(m_impl->m_device.GetGattServicesForUuidAsync(serviceId, BluetoothCacheMode::Uncached), m_running);
            }
            if (servicesResult.Status() != GattCommunicationStatus::Success || servicesResult.Services().Size() == 0) {
                DriverLog("DaydreamBLEHandler: Failed to get GATT Service.\n");
                return false;
            }

            auto service = servicesResult.Services().GetAt(0);
            guid charId = winrt::guid("00000001-1000-1000-8000-00805f9b34fb");
            auto charResult = m_impl->Await(service.GetCharacteristicsForUuidAsync(charId, BluetoothCacheMode::Cached), m_running);
            if (charResult.Status() != GattCommunicationStatus::Success || charResult.Characteristics().Size() == 0) {
                charResult = m_impl->Await(service.GetCharacteristicsForUuidAsync(charId, BluetoothCacheMode::Uncached), m_running);
            }
            if (charResult.Status() != GattCommunicationStatus::Success || charResult.Characteristics().Size() == 0) {
                DriverLog("DaydreamBLEHandler: Failed to get GATT Characteristic.\n");
                return false;
            }

            m_impl->m_characteristic = charResult.Characteristics().GetAt(0);
            m_impl->m_valueChangedToken = m_impl->m_characteristic.ValueChanged([this](GattCharacteristic const&, GattValueChangedEventArgs const& args) {
                auto reader = Windows::Storage::Streams::DataReader::FromBuffer(args.CharacteristicValue());
                std::vector<uint8_t> data(reader.UnconsumedBufferLength());
                reader.ReadBytes(data);

                if (m_onDataReceived) {
                    m_onDataReceived(DaydreamPacketParser::Parse(data.data(), data.size()));
                }
            });
        }

        // This write needs a live link, so it doubles as an explicit (foreground) connection
        // request, which Windows services much faster than the background auto-connect.
        auto status = m_impl->Await(m_impl->m_characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
            GattClientCharacteristicConfigurationDescriptorValue::Notify), m_running);
        if (status != GattCommunicationStatus::Success) {
            DriverLog("DaydreamBLEHandler: Failed to subscribe to characteristic notifications.\n");
            return false;
        }
        return true;
    } catch (winrt::hresult_error const& ex) {
        DriverLog("DaydreamBLEHandler Exception: %ls\n", ex.message().c_str());
        return false;
    }
}

void DaydreamBLEHandler::Stop() {
    if (!m_running.exchange(false)) return;

    SetEvent(m_impl->m_wakeEvent);
    m_impl->CancelPending();
    if (m_thread.joinable()) m_thread.join();
    bool wasSubscribed = m_subscribed.exchange(false);

    if (m_impl->m_characteristic) {
        try {
            m_impl->m_characteristic.ValueChanged(m_impl->m_valueChangedToken);
            // Skip when the link is down; the write would just block until the connection times out.
            if (wasSubscribed) {
                m_impl->m_characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
                    GattClientCharacteristicConfigurationDescriptorValue::None).get();
            }
        } catch(...) {}
        m_impl->m_characteristic = nullptr;
    }

    if (m_impl->m_device) {
        try { m_impl->m_device.ConnectionStatusChanged(m_impl->m_connectionStatusToken); } catch(...) {}
        m_impl->m_device.Close();
        m_impl->m_device = nullptr;
    }

    if (m_impl->m_session) {
        try { m_impl->m_session.MaintainConnection(false); } catch(...) {}
        m_impl->m_session.Close();
        m_impl->m_session = nullptr;
    }
}

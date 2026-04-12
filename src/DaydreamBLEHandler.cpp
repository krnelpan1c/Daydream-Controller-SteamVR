#include "DaydreamBLEHandler.h"
#include <windows.h>
#define DriverLog(...)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <thread>
#include <atomic>

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Devices::Enumeration;

struct DaydreamBLEHandler::Impl {
    BluetoothLEDevice m_device{ nullptr };
    GattSession m_session{ nullptr };
    GattCharacteristic m_characteristic{ nullptr };
    winrt::event_token m_valueChangedToken;
    std::atomic<bool> m_isConnecting{false};
};

DaydreamBLEHandler::DaydreamBLEHandler() : m_impl(new Impl()), m_running(false) {
    winrt::init_apartment();
}

DaydreamBLEHandler::~DaydreamBLEHandler() {
    Stop();
    delete m_impl;
    winrt::uninit_apartment();
}

void DaydreamBLEHandler::Start(std::function<void(const DaydreamData&)> onDataReceived, uint64_t bluetoothAddress, std::wstring knownDeviceId) {
    if (m_running) return;
    m_onDataReceived = onDataReceived;
    m_running = true;
    m_impl->m_isConnecting = true;

    std::thread([this, bluetoothAddress, knownDeviceId]() {
        try {
            if (bluetoothAddress != 0) {
                m_impl->m_device = BluetoothLEDevice::FromBluetoothAddressAsync(bluetoothAddress).get();
            } else if (!knownDeviceId.empty()) {
                m_impl->m_device = BluetoothLEDevice::FromIdAsync(knownDeviceId).get();
            } else {
                winrt::hstring selector = L"System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\" AND System.Devices.Aep.IsPaired:=System.StructuredQueryType.Boolean#True";
                auto devicesInfo = DeviceInformation::FindAllAsync(selector, { L"System.Devices.Aep.DeviceAddress" }, DeviceInformationKind::AssociationEndpoint).get();
                
                winrt::hstring deviceId;
                for (auto&& info : devicesInfo) {
                    std::wstring_view nameView = info.Name();
                    if (nameView.find(L"Daydream") != std::wstring_view::npos || nameView.find(L"daydream") != std::wstring_view::npos) {
                        deviceId = info.Id();
                        break;
                    }
                }

                if (deviceId.empty()) {
                    DriverLog("DaydreamBLEHandler: No Daydream controller found.\n");
                    m_impl->m_isConnecting = false;
                    return;
                }

                m_impl->m_device = BluetoothLEDevice::FromIdAsync(deviceId).get();
            }
            
            if (!m_impl->m_device) {
                DriverLog("DaydreamBLEHandler: Failed to connect to device.\n");
                m_impl->m_isConnecting = false;
                return;
            }

            try {
                m_impl->m_session = GattSession::FromDeviceIdAsync(m_impl->m_device.BluetoothDeviceId()).get();
                if (m_impl->m_session) {
                    m_impl->m_session.MaintainConnection(true);
                }
            } catch (...) {
                DriverLog("DaydreamBLEHandler: Failed to initialize GattSession. Connection might drop.\n");
            }

            guid serviceId = winrt::guid("0000fe55-0000-1000-8000-00805f9b34fb");
            auto servicesResult = m_impl->m_device.GetGattServicesForUuidAsync(serviceId, BluetoothCacheMode::Cached).get();
            
            if (servicesResult.Status() != GattCommunicationStatus::Success || servicesResult.Services().Size() == 0) {
                DriverLog("DaydreamBLEHandler: Failed to get GATT Service.\n");
                m_impl->m_isConnecting = false;
                return;
            }

            auto service = servicesResult.Services().GetAt(0);
            guid charId = winrt::guid("00000001-1000-1000-8000-00805f9b34fb");
            auto charResult = service.GetCharacteristicsForUuidAsync(charId, BluetoothCacheMode::Cached).get();

            if (charResult.Status() != GattCommunicationStatus::Success || charResult.Characteristics().Size() == 0) {
                DriverLog("DaydreamBLEHandler: Failed to get GATT Characteristic.\n");
                m_impl->m_isConnecting = false;
                return;
            }

            m_impl->m_characteristic = charResult.Characteristics().GetAt(0);
            
            auto status = m_impl->m_characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

            if (status == GattCommunicationStatus::Success) {
                m_impl->m_valueChangedToken = m_impl->m_characteristic.ValueChanged([this](GattCharacteristic const&, GattValueChangedEventArgs const& args) {
                    auto reader = Windows::Storage::Streams::DataReader::FromBuffer(args.CharacteristicValue());
                    std::vector<uint8_t> data(reader.UnconsumedBufferLength());
                    reader.ReadBytes(data);
                    
                    if (m_onDataReceived) {
                        m_onDataReceived(DaydreamPacketParser::Parse(data.data(), data.size()));
                    }
                });
                DriverLog("DaydreamBLEHandler: Connected and subscribed successfully!\n");
            } else {
                DriverLog("DaydreamBLEHandler: Failed to subscribe to characteristic notifications.\n");
            }

            m_impl->m_isConnecting = false;

        } catch (winrt::hresult_error const& ex) {
            DriverLog("DaydreamBLEHandler Exception: %ls\n", ex.message().c_str());
            m_impl->m_isConnecting = false;
        }
    }).detach();
}

void DaydreamBLEHandler::Stop() {
    if (!m_running) return;
    m_running = false;
    
    if (m_impl->m_characteristic) {
        try {
            m_impl->m_characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
                GattClientCharacteristicConfigurationDescriptorValue::None).get();
            m_impl->m_characteristic.ValueChanged(m_impl->m_valueChangedToken);
            m_impl->m_characteristic = nullptr;
        } catch(...) {}
    }
    
    if (m_impl->m_device) {
        m_impl->m_device.Close();
        m_impl->m_device = nullptr;
    }
    
    if (m_impl->m_session) {
        try { m_impl->m_session.MaintainConnection(false); } catch(...) {}
        m_impl->m_session.Close();
        m_impl->m_session = nullptr;
    }
}

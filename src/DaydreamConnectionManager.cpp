#include "DaydreamConnectionManager.h"
#include "PendingAsync.h"
#include "driver_log.h"
#include <windows.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Enumeration;

static const uint64_t kPairRetryCooldownMs = 5000;

struct DaydreamConnectionManager::Impl {
  BluetoothLEAdvertisementWatcher m_watcher{nullptr};
  winrt::event_token m_receivedToken{};
  PendingAsync m_pending;
};

static bool IsDaydreamName(std::wstring_view name) {
  return name.find(L"Daydream") != std::wstring_view::npos || name.find(L"daydream") != std::wstring_view::npos;
}

static bool IsDaydreamAdvertisement(BluetoothLEAdvertisementReceivedEventArgs const &args) {
  if (IsDaydreamName(args.Advertisement().LocalName())) return true;
  for (auto &&uuid : args.Advertisement().ServiceUuids()) {
    if (uuid == winrt::guid("0000fe55-0000-1000-8000-00805f9b34fb") || uuid == winrt::guid("0000fef5-0000-1000-8000-00805f9b34fb")) return true;
  }
  return false;
}

DaydreamConnectionManager::DaydreamConnectionManager() : m_impl(new Impl()) {}

DaydreamConnectionManager::~DaydreamConnectionManager() {
  Stop();
  delete m_impl;
}

void DaydreamConnectionManager::Start(DataCallback onData) {
  if (m_running) return;
  m_onData = onData;
  m_running = true;
  m_worker = std::thread(&DaydreamConnectionManager::WorkerLoop, this);
}

void DaydreamConnectionManager::Stop() {
  if (!m_running.exchange(false)) return;

  m_pairCv.notify_all();
  m_impl->m_pending.Cancel();
  if (m_worker.joinable()) m_worker.join();

  std::lock_guard<std::mutex> lock(m_handlersMutex);
  for (auto &handler : m_handlers) handler->Stop();
  m_handlers.clear();
}

void DaydreamConnectionManager::AddHandler(const std::string &deviceId, uint64_t address, const std::wstring &knownDeviceId) {
  auto handler = std::make_unique<DaydreamBLEHandler>();
  handler->Start([this, deviceId](const DaydreamData &data) { m_onData(deviceId, data); }, address, knownDeviceId);
  std::lock_guard<std::mutex> lock(m_handlersMutex);
  m_handlers.push_back(std::move(handler));
}

void DaydreamConnectionManager::WorkerLoop() {
  winrt::init_apartment(winrt::apartment_type::multi_threaded);

  try {
    // Start a connect loop for every already-paired controller. Each loop keeps retrying until
    // its controller wakes up, rather than giving up after one attempt.
    auto selector = L"System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\" AND System.Devices.Aep.IsPaired:=System.StructuredQueryType.Boolean#True";
    auto devicesInfo = m_impl->m_pending.Await(DeviceInformation::FindAllAsync(selector, {L"System.Devices.Aep.DeviceAddress"}, DeviceInformationKind::AssociationEndpoint), m_running);

    for (auto &&info : devicesInfo) {
      if (IsDaydreamName(info.Name())) {
        DriverLog("DaydreamConnectionManager: Found paired controller %s\n", winrt::to_string(info.Id()).c_str());
        AddHandler(winrt::to_string(info.Id()), 0, std::wstring(info.Id()));
      }
    }

    m_impl->m_watcher = BluetoothLEAdvertisementWatcher();
    m_impl->m_watcher.ScanningMode(BluetoothLEScanningMode::Active);
    m_impl->m_receivedToken = m_impl->m_watcher.Received([this](BluetoothLEAdvertisementWatcher const &, BluetoothLEAdvertisementReceivedEventArgs const &args) {
      OnAdvertisement(args.BluetoothAddress(), IsDaydreamAdvertisement(args));
    });
    m_impl->m_watcher.Start();
  } catch (winrt::hresult_error const &ex) {
    DriverLog("DaydreamConnectionManager Exception: %ls\n", ex.message().c_str());
  }

  // Pair newly discovered controllers one at a time, off the watcher's callback thread.
  while (m_running) {
    uint64_t address;
    {
      std::unique_lock<std::mutex> lock(m_pairMutex);
      m_pairCv.wait(lock, [this] { return !m_running || !m_pairQueue.empty(); });
      if (!m_running) break;
      address = m_pairQueue.front();
      m_pairQueue.pop_front();
    }
    PairAndConnect(address);
  }

  if (m_impl->m_watcher) {
    try {
      m_impl->m_watcher.Stop();
      m_impl->m_watcher.Received(m_impl->m_receivedToken);
    } catch (...) {}
    m_impl->m_watcher = nullptr;
  }

  winrt::uninit_apartment();
}

void DaydreamConnectionManager::OnAdvertisement(uint64_t address, bool isDaydream) {
  {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    if (!m_running) return;
    // A known controller is advertising, so it's awake and connectable right now.
    // Directed reconnect advertisements carry no name or UUIDs, so match on address.
    for (auto &h : m_handlers) {
      if (h->GetBluetoothAddress() == address) { h->NotifyAdvertisement(); return; }
    }
    if (!isDaydream) return;
    // Unknown address (e.g. the controller changed its address): wake any loop still waiting.
    for (auto &h : m_handlers) {
      if (!h->IsSubscribed()) h->NotifyAdvertisement();
    }
  }

  uint64_t now = GetTickCount64();
  {
    std::lock_guard<std::mutex> lock(m_pairMutex);
    auto it = m_pairAttempts.find(address);
    if (it != m_pairAttempts.end() && now - it->second < kPairRetryCooldownMs) return;
    m_pairAttempts[address] = now;
    m_pairQueue.push_back(address);
  }
  m_pairCv.notify_one();
}

void DaydreamConnectionManager::PairAndConnect(uint64_t address) {
  try {
    auto device = m_impl->m_pending.Await(BluetoothLEDevice::FromBluetoothAddressAsync(address), m_running);
    if (!device || !device.DeviceInformation().Pairing().CanPair()) return;

    // Windows' default pairing flow fails with the Daydream controller, so pair through the
    // custom flow with ConfirmOnly and accept the request ourselves.
    auto custom = device.DeviceInformation().Pairing().Custom();
    auto token = custom.PairingRequested([](DeviceInformationCustomPairing const &, DevicePairingRequestedEventArgs const &args) { args.Accept(); });
    DevicePairingResult result{nullptr};
    try {
      result = m_impl->m_pending.Await(custom.PairAsync(DevicePairingKinds::ConfirmOnly), m_running);
    } catch (...) {
      custom.PairingRequested(token);
      throw;
    }
    custom.PairingRequested(token);

    if (result.Status() != DevicePairingResultStatus::Paired && result.Status() != DevicePairingResultStatus::AlreadyPaired) {
      DriverLog("DaydreamConnectionManager: Pairing failed with status %d\n", (int)result.Status());
      return;
    }

    {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      for (auto &h : m_handlers) {
        if (h->GetBluetoothAddress() == address) return;
      }
    }
    std::string deviceId = winrt::to_string(device.DeviceInformation().Id());
    DriverLog("DaydreamConnectionManager: Paired new controller %s\n", deviceId.c_str());
    AddHandler(deviceId, address, L"");
  } catch (winrt::hresult_error const &ex) {
    DriverLog("DaydreamConnectionManager Exception: %ls\n", ex.message().c_str());
  }
}

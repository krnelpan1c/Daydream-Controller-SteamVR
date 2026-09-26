#pragma once
#include "DaydreamBLEHandler.h"
#include "DaydreamPacket.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Finds, pairs and keeps connected every Daydream controller in range. Data from each controller
// is reported with that controller's device ID, which is what settings.ini stores for hand assignment.
class DaydreamConnectionManager {
public:
  using DataCallback = std::function<void(const std::string &deviceId, const DaydreamData &data)>;

  DaydreamConnectionManager();
  ~DaydreamConnectionManager();

  void Start(DataCallback onData);
  void Stop();

private:
  void WorkerLoop();
  void OnAdvertisement(uint64_t address, bool isDaydream);
  void PairAndConnect(uint64_t address);
  void AddHandler(const std::string &deviceId, uint64_t address, const std::wstring &knownDeviceId);

  DataCallback m_onData;
  std::atomic<bool> m_running{false};
  std::thread m_worker;

  std::mutex m_handlersMutex;
  std::vector<std::unique_ptr<DaydreamBLEHandler>> m_handlers;

  // Addresses queued for pairing, and when each was last queued, so repeated advertisements
  // from the same controller don't pile up pairing attempts.
  std::mutex m_pairMutex;
  std::condition_variable m_pairCv;
  std::deque<uint64_t> m_pairQueue;
  std::map<uint64_t, uint64_t> m_pairAttempts;

  struct Impl;
  Impl *m_impl;
};

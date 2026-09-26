#pragma once
#include "CDaydreamController.h"
#include "DaydreamConnectionManager.h"
#include <memory>
#include <mutex>
#include <openvr_driver.h>
#include <string>
#include <thread>

class CDaydreamProvider : public vr::IServerTrackedDeviceProvider {
public:
  virtual vr::EVRInitError Init(vr::IVRDriverContext *pDriverContext) override;
  virtual void Cleanup() override;
  virtual const char *const *GetInterfaceVersions() override;
  virtual void RunFrame() override;
  virtual bool ShouldBlockStandbyMode() override;
  virtual void EnterStandby() override;
  virtual void LeaveStandby() override;

private:
  void OnControllerData(const std::string &deviceId, const DaydreamData &data);
  void LoadSettings();
  void SettingsWatchLoop();

  std::unique_ptr<CDaydreamController> m_left;
  std::unique_ptr<CDaydreamController> m_right;
  DaydreamConnectionManager m_connections;

  std::mutex m_settingsMutex;
  std::string m_leftId;
  std::string m_rightId;
  std::string m_capture;

  std::thread m_settingsThread;
  void *m_settingsStopEvent = nullptr;
};

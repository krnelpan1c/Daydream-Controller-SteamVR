#pragma once
#include "DaydreamPacket.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <openvr_driver.h>
#include <string>

class CDaydreamController : public vr::ITrackedDeviceServerDriver {
public:
  CDaydreamController();
  virtual ~CDaydreamController();

  bool IsRegistered() const { return m_isRegistered; }
  void SetRegistered(bool val) { m_isRegistered = val; }
  bool IsConnected() const { return m_isConnected; }

  virtual vr::EVRInitError Activate(uint32_t unObjectId) override;
  virtual void Deactivate() override;
  virtual void EnterStandby() override;
  virtual void *GetComponent(const char *pchComponentNameAndVersion) override;
  virtual void DebugRequest(const char *pchRequest, char *pchResponseBuffer,
                            uint32_t unResponseBufferSize) override;
  virtual vr::DriverPose_t GetPose() override;

  void RunFrame();
  std::string GetSerialNumber() const { return m_serialNumber; }

private:
  void HandleData(const DaydreamData &data);
  void UpdatePose(const DaydreamData &data);
  void HandleMediaKeys(const DaydreamData &data);

  vr::TrackedDeviceIndex_t m_unObjectId;
  vr::PropertyContainerHandle_t m_ulPropertyContainer;

  std::string m_serialNumber;
  std::string m_modelNumber;

  std::mutex m_poseMutex;
  vr::DriverPose_t m_pose;

  vr::VRInputComponentHandle_t m_compClick;
  vr::VRInputComponentHandle_t m_compTriggerValue;
  vr::VRInputComponentHandle_t m_compTouch;
  vr::VRInputComponentHandle_t m_compApp;
  vr::VRInputComponentHandle_t m_compHome;
  vr::VRInputComponentHandle_t m_compTouchX;
  vr::VRInputComponentHandle_t m_compTouchY;

  void *m_hPipe;
  std::atomic<bool> m_pipeRunning;
  std::atomic<bool> m_isConnected;
  bool m_isRegistered;
  void StartPipeClient();

  bool m_lastVolUp;
  bool m_lastVolDown;
  bool m_lastHome;

  std::chrono::steady_clock::time_point m_homeDownTime;
  bool m_recenterTriggered;
  bool m_wantsRecenter;
  float m_yawOffset;
  float m_lastHeadYaw;
  int m_handRole;
};

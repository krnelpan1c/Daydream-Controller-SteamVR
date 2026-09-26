#pragma once
#include "DaydreamPacket.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <openvr_driver.h>
#include <string>

// Target each Daydream button is mapped to (see DaydreamSettings::Target).
struct ButtonMappings {
  int click;
  int app;
  int home;
  int volUp;
  int volDown;
};

class CDaydreamController : public vr::ITrackedDeviceServerDriver {
public:
  CDaydreamController(int handRole);
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

  // Called from the Bluetooth notification thread with each packet from this hand's controller.
  void HandleData(const DaydreamData &data);
  void SetMappings(const ButtonMappings &mappings);

private:
  void UpdatePose(const DaydreamData &data);
  void HandleMediaKeys(const DaydreamData &data);

  vr::TrackedDeviceIndex_t m_unObjectId;
  vr::PropertyContainerHandle_t m_ulPropertyContainer;

  std::string m_serialNumber;
  std::string m_modelNumber;

  std::mutex m_poseMutex;
  vr::DriverPose_t m_pose;

  vr::VRInputComponentHandle_t m_compClick = vr::k_ulInvalidInputComponentHandle; // trigger click
  vr::VRInputComponentHandle_t m_compTriggerValue = vr::k_ulInvalidInputComponentHandle;
  vr::VRInputComponentHandle_t m_compGrip = vr::k_ulInvalidInputComponentHandle;
  vr::VRInputComponentHandle_t m_compTrackpadClick = vr::k_ulInvalidInputComponentHandle;
  vr::VRInputComponentHandle_t m_compTouch = vr::k_ulInvalidInputComponentHandle;
  vr::VRInputComponentHandle_t m_compApp = vr::k_ulInvalidInputComponentHandle;
  vr::VRInputComponentHandle_t m_compHome = vr::k_ulInvalidInputComponentHandle;
  vr::VRInputComponentHandle_t m_compTouchX = vr::k_ulInvalidInputComponentHandle;
  vr::VRInputComponentHandle_t m_compTouchY = vr::k_ulInvalidInputComponentHandle;

  std::atomic<bool> m_isConnected;
  bool m_isRegistered;

  bool m_lastVolUp;
  bool m_lastVolDown;
  bool m_lastHome;

  std::chrono::steady_clock::time_point m_homeDownTime;
  std::chrono::steady_clock::time_point m_lastDataTime;
  bool m_recenterTriggered;
  bool m_wantsRecenter;
  float m_yawOffset;
  float m_lastHeadYaw;
  float m_torsoYaw;
  bool m_torsoYawValid;
  int m_handRole;
  float m_lastTouchX;
  float m_lastTouchY;

  std::atomic<int> m_mapClick;
  std::atomic<int> m_mapApp;
  std::atomic<int> m_mapHome;
  std::atomic<int> m_mapVolUp;
  std::atomic<int> m_mapVolDown;
  
  bool isTargetActive(int target, const DaydreamData &data);
};

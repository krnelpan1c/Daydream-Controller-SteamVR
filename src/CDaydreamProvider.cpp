#include "CDaydreamProvider.h"
#include "DaydreamSettings.h"
#include "driver_log.h"
#include <windows.h>

vr::EVRInitError CDaydreamProvider::Init(vr::IVRDriverContext *pDriverContext) {
  VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);
  g_pDriverLog = vr::VRDriverLog();
  DriverLog("CDaydreamProvider::Init - Initializing Daydream Provider\n");

  m_left = std::make_unique<CDaydreamController>(vr::TrackedControllerRole_LeftHand);
  m_right = std::make_unique<CDaydreamController>(vr::TrackedControllerRole_RightHand);

  LoadSettings();
  m_settingsStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
  m_settingsThread = std::thread(&CDaydreamProvider::SettingsWatchLoop, this);

  m_connections.Start([this](const std::string &deviceId, const DaydreamData &data) { OnControllerData(deviceId, data); });

  return vr::VRInitError_None;
}

void CDaydreamProvider::Cleanup() {
  DriverLog("CDaydreamProvider::Cleanup\n");
  m_connections.Stop();

  if (m_settingsStopEvent) {
    SetEvent(m_settingsStopEvent);
    if (m_settingsThread.joinable()) m_settingsThread.join();
    CloseHandle(m_settingsStopEvent);
    m_settingsStopEvent = nullptr;
  }

  m_left.reset();
  m_right.reset();
}

const char *const *CDaydreamProvider::GetInterfaceVersions() {
  return vr::k_InterfaceVersions;
}

void CDaydreamProvider::RunFrame() {
  for (CDaydreamController *controller : {m_left.get(), m_right.get()}) {
    if (!controller) continue;
    if (!controller->IsRegistered() && controller->IsConnected()) {
      vr::VRServerDriverHost()->TrackedDeviceAdded(
          controller->GetSerialNumber().c_str(),
          vr::TrackedDeviceClass_Controller, controller);
      controller->SetRegistered(true);
    }
    controller->RunFrame();
  }
}

bool CDaydreamProvider::ShouldBlockStandbyMode() { return false; }
void CDaydreamProvider::EnterStandby() {}
void CDaydreamProvider::LeaveStandby() {}

void CDaydreamProvider::OnControllerData(const std::string &deviceId, const DaydreamData &data) {
  CDaydreamController *target = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_settingsMutex);

    // The config app asked us to assign the next controller that presses a button to a hand.
    bool anyButton = data.click || data.app || data.home || data.volDown || data.volUp;
    if (!m_capture.empty() && anyButton) {
      if (m_capture == DaydreamSettings::kCaptureLeft) {
        m_leftId = deviceId;
        if (m_rightId == deviceId) m_rightId = "";
      } else if (m_capture == DaydreamSettings::kCaptureRight) {
        m_rightId = deviceId;
        if (m_leftId == deviceId) m_leftId = "";
      }
      m_capture.clear();
      DaydreamSettings::Write(DaydreamSettings::kLeftId, m_leftId);
      DaydreamSettings::Write(DaydreamSettings::kRightId, m_rightId);
      DaydreamSettings::Write(DaydreamSettings::kCapture, "");
      DriverLog("CDaydreamProvider: Assigned controller %s\n", deviceId.c_str());
      return;
    }

    if (deviceId == m_leftId) target = m_left.get();
    else if (deviceId == m_rightId) target = m_right.get();
  }

  if (target) target->HandleData(data);
}

void CDaydreamProvider::LoadSettings() {
  {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    m_leftId = DaydreamSettings::ReadString(DaydreamSettings::kLeftId);
    m_rightId = DaydreamSettings::ReadString(DaydreamSettings::kRightId);
    m_capture = DaydreamSettings::ReadString(DaydreamSettings::kCapture);
  }

  ButtonMappings mappings;
  mappings.click = DaydreamSettings::ReadInt(DaydreamSettings::kMapClick, DaydreamSettings::kDefaultMapClick);
  mappings.app = DaydreamSettings::ReadInt(DaydreamSettings::kMapApp, DaydreamSettings::kDefaultMapApp);
  mappings.home = DaydreamSettings::ReadInt(DaydreamSettings::kMapHome, DaydreamSettings::kDefaultMapHome);
  mappings.volUp = DaydreamSettings::ReadInt(DaydreamSettings::kMapVolUp, DaydreamSettings::kDefaultMapVolUp);
  mappings.volDown = DaydreamSettings::ReadInt(DaydreamSettings::kMapVolDown, DaydreamSettings::kDefaultMapVolDown);
  m_left->SetMappings(mappings);
  m_right->SetMappings(mappings);
}

// Reloads settings.ini whenever it changes, so assignments and button mappings made in the
// config app apply without restarting SteamVR.
void CDaydreamProvider::SettingsWatchLoop() {
  HANDLE change = FindFirstChangeNotificationA(DaydreamSettings::GetDirectory().c_str(), FALSE,
                                               FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
  if (change == INVALID_HANDLE_VALUE) {
    DriverLog("CDaydreamProvider: Failed to watch settings directory. Settings changes need a SteamVR restart.\n");
    return;
  }

  HANDLE handles[2] = {m_settingsStopEvent, change};
  while (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
    Sleep(50); // Let the writer finish before reading.
    LoadSettings();
    if (!FindNextChangeNotification(change)) break;
  }
  FindCloseChangeNotification(change);
}

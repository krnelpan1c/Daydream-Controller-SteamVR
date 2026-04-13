#include "CDaydreamProvider.h"
#include "driver_log.h"

vr::EVRInitError CDaydreamProvider::Init(vr::IVRDriverContext *pDriverContext) {
  VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);
  g_pDriverLog = vr::VRDriverLog();
  DriverLog("CDaydreamProvider::Init - Initializing Daydream Provider\n");

  m_controllers.push_back(std::make_unique<CDaydreamController>());

  return vr::VRInitError_None;
}

void CDaydreamProvider::Cleanup() {
  DriverLog("CDaydreamProvider::Cleanup\n");
  m_controllers.clear();
}

const char *const *CDaydreamProvider::GetInterfaceVersions() {
  return vr::k_InterfaceVersions;
}

void CDaydreamProvider::RunFrame() {
  for (auto &controller : m_controllers) {
    if (!controller->IsRegistered() && controller->IsConnected()) {
      vr::VRServerDriverHost()->TrackedDeviceAdded(
          controller->GetSerialNumber().c_str(),
          vr::TrackedDeviceClass_Controller, controller.get());
      controller->SetRegistered(true);
    }
    controller->RunFrame();
  }
}

bool CDaydreamProvider::ShouldBlockStandbyMode() { return false; }
void CDaydreamProvider::EnterStandby() {}
void CDaydreamProvider::LeaveStandby() {}

#pragma once
#include "CDaydreamController.h"
#include <memory>
#include <openvr_driver.h>
#include <vector>

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
  std::vector<std::unique_ptr<CDaydreamController>> m_controllers;
};

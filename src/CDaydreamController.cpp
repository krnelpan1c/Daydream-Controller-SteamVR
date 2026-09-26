#include "CDaydreamController.h"
#include "DaydreamSettings.h"
#include "driver_log.h"
#include <cmath>
#include <windows.h>

inline vr::HmdQuaternion_t EulerToQuaternion(double yaw, double pitch,
                                             double roll) {
  double cy = cos(yaw * 0.5);
  double sy = sin(yaw * 0.5);
  double cp = cos(pitch * 0.5);
  double sp = sin(pitch * 0.5);
  double cr = cos(roll * 0.5);
  double sr = sin(roll * 0.5);

  vr::HmdQuaternion_t q;
  q.w = cr * cp * cy + sr * sp * sy;
  q.x = sr * cp * cy - cr * sp * sy;
  q.y = cr * sp * cy + sr * cp * sy;
  q.z = cr * cp * sy - sr * sp * cy;

  return q;
}

CDaydreamController::CDaydreamController(int handRole)
    : m_unObjectId(vr::k_unTrackedDeviceIndexInvalid),
      m_ulPropertyContainer(vr::k_ulInvalidPropertyContainer),
      m_isConnected(false), m_isRegistered(false),
      m_mapClick(DaydreamSettings::kDefaultMapClick),
      m_mapApp(DaydreamSettings::kDefaultMapApp),
      m_mapHome(DaydreamSettings::kDefaultMapHome),
      m_mapVolUp(DaydreamSettings::kDefaultMapVolUp),
      m_mapVolDown(DaydreamSettings::kDefaultMapVolDown) {
  m_handRole = handRole;
  if (m_handRole == vr::TrackedControllerRole_LeftHand) {
    m_serialNumber = "DD_REMOTE_LEFT";
  } else {
    m_serialNumber = "DD_REMOTE_RIGHT";
  }
  m_modelNumber = "Daydream Controller";

  m_pose = {0};
  m_pose.poseTimeOffset = 0;
  m_pose.poseIsValid = true;
  m_pose.deviceIsConnected = false;
  m_pose.qWorldFromDriverRotation = {1, 0, 0, 0};
  m_pose.qDriverFromHeadRotation = {1, 0, 0, 0};
  m_pose.qRotation = {1, 0, 0, 0};
  m_pose.vecPosition[0] = 0.0;
  m_pose.vecPosition[1] = 1.0;
  m_pose.vecPosition[2] = -0.5;
  m_pose.result = vr::TrackingResult_Running_OK;

  m_lastVolUp = false;
  m_lastVolDown = false;
  m_lastHome = false;
  m_recenterTriggered = false;
  m_wantsRecenter = false;
  m_yawOffset = 0.0f;
  m_lastHeadYaw = 0.0f;
  m_torsoYaw = 0.0f;
  m_torsoYawValid = false;
  m_lastTouchX = 0.0f;
  m_lastTouchY = 0.0f;
  m_lastDataTime = std::chrono::steady_clock::now();
}

CDaydreamController::~CDaydreamController() {}

void CDaydreamController::SetMappings(const ButtonMappings &mappings) {
  m_mapClick = mappings.click;
  m_mapApp = mappings.app;
  m_mapHome = mappings.home;
  m_mapVolUp = mappings.volUp;
  m_mapVolDown = mappings.volDown;
}

vr::EVRInitError CDaydreamController::Activate(uint32_t unObjectId) {
  m_unObjectId = unObjectId;
  m_ulPropertyContainer =
      vr::VRProperties()->TrackedDeviceToPropertyContainer(m_unObjectId);

  vr::VRProperties()->SetStringProperty(m_ulPropertyContainer,
                                        vr::Prop_ModelNumber_String,
                                        m_modelNumber.c_str());
  vr::VRProperties()->SetStringProperty(m_ulPropertyContainer,
                                        vr::Prop_RenderModelName_String,
                                        "vr_controller_vive_1_5");
  vr::VRProperties()->SetStringProperty(
      m_ulPropertyContainer, vr::Prop_InputProfilePath_String,
      "{daydream}/input/daydream_profile.json");
  vr::VRProperties()->SetInt32Property(m_ulPropertyContainer,
                                       vr::Prop_DeviceClass_Int32,
                                       vr::TrackedDeviceClass_Controller);
  vr::VRProperties()->SetInt32Property(
      m_ulPropertyContainer, vr::Prop_ControllerRoleHint_Int32, m_handRole);

  vr::VRDriverInput()->CreateBooleanComponent(
      m_ulPropertyContainer, "/input/trigger/click", &m_compClick);
  vr::VRDriverInput()->CreateScalarComponent(
      m_ulPropertyContainer, "/input/trigger/value", &m_compTriggerValue,
      vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
  vr::VRDriverInput()->CreateBooleanComponent(
      m_ulPropertyContainer, "/input/grip/click", &m_compGrip);
  vr::VRDriverInput()->CreateBooleanComponent(
      m_ulPropertyContainer, "/input/trackpad/click", &m_compTrackpadClick);
  vr::VRDriverInput()->CreateBooleanComponent(
      m_ulPropertyContainer, "/input/trackpad/touch", &m_compTouch);
  vr::VRDriverInput()->CreateBooleanComponent(
      m_ulPropertyContainer, "/input/application_menu/click", &m_compApp);
  vr::VRDriverInput()->CreateBooleanComponent(
      m_ulPropertyContainer, "/input/system/click", &m_compHome);
  vr::VRDriverInput()->CreateScalarComponent(
      m_ulPropertyContainer, "/input/trackpad/x", &m_compTouchX,
      vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
  vr::VRDriverInput()->CreateScalarComponent(
      m_ulPropertyContainer, "/input/trackpad/y", &m_compTouchY,
      vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);

  return vr::VRInitError_None;
}

void CDaydreamController::Deactivate() {
  m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
}

void CDaydreamController::EnterStandby() {}

void *
CDaydreamController::GetComponent(const char *pchComponentNameAndVersion) {
  return nullptr;
}

void CDaydreamController::DebugRequest(const char *pchRequest,
                                       char *pchResponseBuffer,
                                       uint32_t unResponseBufferSize) {
  if (unResponseBufferSize >= 1)
    pchResponseBuffer[0] = 0;
}

vr::DriverPose_t CDaydreamController::GetPose() {
  std::lock_guard<std::mutex> lock(m_poseMutex);
  return m_pose;
}

void CDaydreamController::RunFrame() {
  if (m_unObjectId != vr::k_unTrackedDeviceIndexInvalid) {
    if (m_isConnected) {
      auto now = std::chrono::steady_clock::now();
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastDataTime).count() > 1000) {
        m_isConnected = false;
        std::lock_guard<std::mutex> lock(m_poseMutex);
        m_pose.deviceIsConnected = false;
        DriverLog("CDaydreamController: Data timeout. Disconnecting device.\n");
      }
    }
    vr::VRServerDriverHost()->TrackedDevicePoseUpdated(
        m_unObjectId, GetPose(), sizeof(vr::DriverPose_t));
  }
}

bool CDaydreamController::isTargetActive(int target, const DaydreamData &data) {
  bool active = false;
  if (m_mapClick == target && data.click) active = true;
  if (m_mapApp == target && data.app) active = true;
  if (m_mapHome == target && data.home) active = true;
  if (m_mapVolUp == target && data.volUp) active = true;
  if (m_mapVolDown == target && data.volDown) active = true;
  return active;
}

void CDaydreamController::HandleData(const DaydreamData &data) {
  UpdatePose(data);
  m_lastDataTime = std::chrono::steady_clock::now();

  if (!m_isConnected) {
     m_isConnected = true;
     std::lock_guard<std::mutex> lock(m_poseMutex);
     m_pose.deviceIsConnected = true;
     DriverLog("CDaydreamController: Data flowing. Device Connected.\n");
  }

  bool homePressed = isTargetActive(4, data); // System (Home)

  if (homePressed && !m_lastHome) {
    m_homeDownTime = std::chrono::steady_clock::now();
    m_recenterTriggered = false;
  }

  if (homePressed && !m_recenterTriggered) {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                              m_homeDownTime)
            .count() > 1000) {
      std::lock_guard<std::mutex> lock(m_poseMutex);
      m_wantsRecenter = true;
      m_recenterTriggered = true;
      DriverLog("CDaydreamController: Recentered Controller!\n");
    }
  }

  m_lastHome = homePressed;

  bool clickActive = isTargetActive(0, data);
  bool triggerActive = isTargetActive(1, data);
  bool gripActive = isTargetActive(2, data);
  bool appActive = isTargetActive(3, data);

  vr::VRDriverInput()->UpdateBooleanComponent(m_compTrackpadClick, clickActive, 0);
  vr::VRDriverInput()->UpdateBooleanComponent(m_compClick, triggerActive, 0);
  vr::VRDriverInput()->UpdateScalarComponent(m_compTriggerValue,
                                             triggerActive ? 1.0f : 0.0f, 0);
  vr::VRDriverInput()->UpdateBooleanComponent(m_compGrip, gripActive, 0);
  vr::VRDriverInput()->UpdateBooleanComponent(m_compTouch, data.touched, 0);
  vr::VRDriverInput()->UpdateBooleanComponent(m_compApp, appActive, 0);
  vr::VRDriverInput()->UpdateBooleanComponent(m_compHome, homePressed, 0);

  if (data.touched) {
    m_lastTouchX = data.touchX;
    m_lastTouchY = data.touchY;
  }

  vr::VRDriverInput()->UpdateScalarComponent(m_compTouchX, m_lastTouchX, 0);
  vr::VRDriverInput()->UpdateScalarComponent(m_compTouchY, m_lastTouchY, 0);

  HandleMediaKeys(data);
}

void CDaydreamController::UpdatePose(const DaydreamData &data) {
  std::lock_guard<std::mutex> lock(m_poseMutex);

  float x = data.oriX;
  float y = data.oriY;
  float z = data.oriZ;

  float angle = sqrt(x * x + y * y + z * z);
  vr::HmdQuaternion_t q_sensor;
  if (angle > 0.00001f) {
    float factor = sin(angle / 2.0f) / angle;
    q_sensor.x = x * factor;
    q_sensor.y = y * factor;
    q_sensor.z = z * factor;
    q_sensor.w = cos(angle / 2.0f);
  } else {
    q_sensor.w = 1.0f;
    q_sensor.x = 0.0f;
    q_sensor.y = 0.0f;
    q_sensor.z = 0.0f;
  }

  vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
  vr::VRServerDriverHost()->GetRawTrackedDevicePoses(
      0, poses, vr::k_unMaxTrackedDeviceCount);

  float handX = 0.0f, handY = 1.0f, handZ = -0.5f;

  bool bPoseIsValid = poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid;
  if (bPoseIsValid) {
    auto &mat = poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
    float forwardX = -mat.m[0][2];
    float forwardZ = -mat.m[2][2];
    m_lastHeadYaw = atan2(forwardX, forwardZ);
  }

  float fX = -2.0f * (q_sensor.x * q_sensor.z + q_sensor.w * q_sensor.y);
  float fZ =
      -(1.0f - 2.0f * (q_sensor.x * q_sensor.x + q_sensor.y * q_sensor.y));
  float yaw_sensor = atan2(fX, fZ);

  if (m_wantsRecenter) {
    m_yawOffset = m_lastHeadYaw - yaw_sensor;
    m_torsoYaw = m_lastHeadYaw;
    m_torsoYawValid = true;
    m_wantsRecenter = false;
  }

  float cy = cos(m_yawOffset / 2.0f);
  float sy = sin(m_yawOffset / 2.0f);
  vr::HmdQuaternion_t q;
  q.w = cy * q_sensor.w - sy * q_sensor.y;
  q.x = cy * q_sensor.x + sy * q_sensor.z;
  q.y = cy * q_sensor.y + sy * q_sensor.w;
  q.z = cy * q_sensor.z - sy * q_sensor.x;

  // Controller forward (-Z) in world space.
  float dirX = -2.0f * (q.x * q.z + q.w * q.y);
  float dirY = -2.0f * (q.y * q.z - q.w * q.x);
  float dirZ = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));

  // The torso faces its own direction, independent of head yaw, so looking around doesn't
  // swing the arm. It only turns when the controller points further than kTorsoDeadzone to
  // either side, like turning in a chair. Skip when pointing near straight up/down, where
  // the controller's yaw is unstable.
  const float kTorsoDeadzone = 45.0f * (float)M_PI / 180.0f;
  if (sqrt(dirX * dirX + dirZ * dirZ) > 0.3f) {
    float controllerYaw = atan2(dirX, dirZ);
    if (!m_torsoYawValid) {
      m_torsoYaw = controllerYaw;
      m_torsoYawValid = true;
    }
    float diff = remainderf(controllerYaw - m_torsoYaw, 2.0f * (float)M_PI);
    if (diff > kTorsoDeadzone) m_torsoYaw += diff - kTorsoDeadzone;
    else if (diff < -kTorsoDeadzone) m_torsoYaw += diff + kTorsoDeadzone;
  }

  if (bPoseIsValid) {
    auto &mat = poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;

    // Pivot around the neck rather than the HMD: the eyes sit in front of and above the neck,
    // so the HMD itself moves in an arc whenever the head turns. (Head space: +Y up, +Z back.)
    const float kNeckDown = 0.075f;
    const float kNeckBack = 0.08f;
    float neckX = mat.m[0][3] - mat.m[0][1] * kNeckDown + mat.m[0][2] * kNeckBack;
    float neckY = mat.m[1][3] - mat.m[1][1] * kNeckDown + mat.m[1][2] * kNeckBack;
    float neckZ = mat.m[2][3] - mat.m[2][1] * kNeckDown + mat.m[2][2] * kNeckBack;

    float sign = (m_handRole == vr::TrackedControllerRole_LeftHand) ? -1.0f : 1.0f;

    // Torso forward and right vectors from the torso yaw.
    float fx = sin(m_torsoYaw);
    float fz = cos(m_torsoYaw);
    float rx = -fz;
    float rz = fx;

    float shoulderX = neckX + rx * (0.15f * sign) + fx * 0.13f;
    float shoulderY = neckY - 0.225f;
    float shoulderZ = neckZ + rz * (0.15f * sign) + fz * 0.13f;

    float armLength = 0.25f;

    handX = shoulderX + dirX * armLength;
    handY = shoulderY + dirY * armLength;
    handZ = shoulderZ + dirZ * armLength;
  }

  m_pose.qRotation = q;
  m_pose.vecPosition[0] = handX;
  m_pose.vecPosition[1] = handY;
  m_pose.vecPosition[2] = handZ;
  m_pose.result = vr::TrackingResult_Running_OK;
}

void CDaydreamController::HandleMediaKeys(const DaydreamData &data) {
  bool volUpActive = isTargetActive(5, data);
  bool volDownActive = isTargetActive(6, data);

  if (volUpActive && !m_lastVolUp) {
    INPUT inputs[1] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_VOLUME_UP;
    SendInput(1, inputs, sizeof(INPUT));
  }
  m_lastVolUp = volUpActive;

  if (volDownActive && !m_lastVolDown) {
    INPUT inputs[1] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_VOLUME_DOWN;
    SendInput(1, inputs, sizeof(INPUT));
  }
  m_lastVolDown = volDownActive;
}

#include "CDaydreamController.h"
#include "driver_log.h"
#include <windows.h>
#include <cmath>

inline vr::HmdQuaternion_t EulerToQuaternion(double yaw, double pitch, double roll)
{
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

CDaydreamController::CDaydreamController()
    : m_unObjectId(vr::k_unTrackedDeviceIndexInvalid), m_ulPropertyContainer(vr::k_ulInvalidPropertyContainer)
{
    m_serialNumber = "DD_REMOTE_001";
    m_modelNumber = "Daydream Controller";

    m_pose = {0};
    m_pose.poseTimeOffset = 0;
    m_pose.poseIsValid = true;
    m_pose.deviceIsConnected = true;
    m_pose.qWorldFromDriverRotation = { 1, 0, 0, 0 };
    m_pose.qDriverFromHeadRotation = { 1, 0, 0, 0 };
    m_pose.qRotation = { 1, 0, 0, 0 };
    m_pose.vecPosition[0] = 0.0;
    m_pose.vecPosition[1] = 1.0;
    m_pose.vecPosition[2] = -0.5;

    m_lastVolUp = false;
    m_lastVolDown = false;
    m_lastHome = false;
    m_recenterTriggered = false;
    m_yawOffset = 0.0f;
}

CDaydreamController::~CDaydreamController()
{
}

vr::EVRInitError CDaydreamController::Activate(uint32_t unObjectId)
{
    m_unObjectId = unObjectId;
    m_ulPropertyContainer = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_unObjectId);

    vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_ModelNumber_String, m_modelNumber.c_str());
    vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_RenderModelName_String, "vr_controller_vive_1_5");
    vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_InputProfilePath_String, "{daydream}/input/daydream_profile.json");
    vr::VRProperties()->SetInt32Property(m_ulPropertyContainer, vr::Prop_DeviceClass_Int32, vr::TrackedDeviceClass_Controller);
    vr::VRProperties()->SetInt32Property(m_ulPropertyContainer, vr::Prop_ControllerRoleHint_Int32, vr::TrackedControllerRole_RightHand);

    vr::VRDriverInput()->CreateBooleanComponent(m_ulPropertyContainer, "/input/trackpad/click", &m_compClick);
    vr::VRDriverInput()->CreateBooleanComponent(m_ulPropertyContainer, "/input/trackpad/touch", &m_compTouch);
    vr::VRDriverInput()->CreateBooleanComponent(m_ulPropertyContainer, "/input/application_menu/click", &m_compApp);
    vr::VRDriverInput()->CreateBooleanComponent(m_ulPropertyContainer, "/input/system/click", &m_compHome);
    vr::VRDriverInput()->CreateScalarComponent(m_ulPropertyContainer, "/input/trackpad/x", &m_compTouchX, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
    vr::VRDriverInput()->CreateScalarComponent(m_ulPropertyContainer, "/input/trackpad/y", &m_compTouchY, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);

    m_bleHandler.Start([this](const DaydreamData& data) {
        HandleData(data);
    });

    return vr::VRInitError_None;
}

void CDaydreamController::Deactivate()
{
    m_bleHandler.Stop();
    m_unObjectId = vr::k_unTrackedDeviceIndexInvalid;
}

void CDaydreamController::EnterStandby()
{
}

void* CDaydreamController::GetComponent(const char* pchComponentNameAndVersion)
{
    return nullptr;
}

void CDaydreamController::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize)
{
    if (unResponseBufferSize >= 1)
        pchResponseBuffer[0] = 0;
}

vr::DriverPose_t CDaydreamController::GetPose()
{
    std::lock_guard<std::mutex> lock(m_poseMutex);
    return m_pose;
}

void CDaydreamController::RunFrame()
{
    if (m_unObjectId != vr::k_unTrackedDeviceIndexInvalid)
    {
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_unObjectId, GetPose(), sizeof(vr::DriverPose_t));
    }
}

void CDaydreamController::HandleData(const DaydreamData& data)
{
    UpdatePose(data);
    
    bool homePressed = data.home;
    if (homePressed && !m_lastHome) {
        m_homeDownTime = std::chrono::steady_clock::now();
        m_recenterTriggered = false;
    }
    
    if (homePressed && !m_recenterTriggered) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_homeDownTime).count() > 1000) {
            std::lock_guard<std::mutex> lock(m_poseMutex);
            m_yawOffset = data.oriY; 
            m_recenterTriggered = true;
            DriverLog("CDaydreamController: Recentered Controller!\n");
        }
    }
    
    m_lastHome = homePressed;

    vr::VRDriverInput()->UpdateBooleanComponent(m_compClick, data.click, 0);
    vr::VRDriverInput()->UpdateBooleanComponent(m_compTouch, std::abs(data.touchX) > 0.05f || std::abs(data.touchY) > 0.05f || data.click, 0);
    vr::VRDriverInput()->UpdateBooleanComponent(m_compApp, data.app, 0);
    vr::VRDriverInput()->UpdateBooleanComponent(m_compHome, homePressed, 0);
    vr::VRDriverInput()->UpdateScalarComponent(m_compTouchX, data.touchX, 0);
    vr::VRDriverInput()->UpdateScalarComponent(m_compTouchY, data.touchY, 0);

    HandleMediaKeys(data);
}

void CDaydreamController::UpdatePose(const DaydreamData& data)
{
    std::lock_guard<std::mutex> lock(m_poseMutex);
    float yaw = data.oriY - m_yawOffset;
    m_pose.qRotation = EulerToQuaternion(yaw, data.oriZ, data.oriX);
    m_pose.result = vr::TrackingResult_Running_OK;
}

void CDaydreamController::HandleMediaKeys(const DaydreamData& data)
{
    if (data.volUp && !m_lastVolUp) {
        INPUT inputs[1] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_VOLUME_UP;
        SendInput(1, inputs, sizeof(INPUT));
    }
    m_lastVolUp = data.volUp;

    if (data.volDown && !m_lastVolDown) {
        INPUT inputs[1] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_VOLUME_DOWN;
        SendInput(1, inputs, sizeof(INPUT));
    }
    m_lastVolDown = data.volDown;
}

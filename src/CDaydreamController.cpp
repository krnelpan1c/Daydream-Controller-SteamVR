#include "CDaydreamController.h"
#include "driver_log.h"
#include <windows.h>
#include <cmath>
#include <thread>

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
    : m_unObjectId(vr::k_unTrackedDeviceIndexInvalid), m_ulPropertyContainer(vr::k_ulInvalidPropertyContainer), m_pipeRunning(false), m_hPipe(INVALID_HANDLE_VALUE)
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
    m_pose.result = vr::TrackingResult_Running_OK;

    m_lastVolUp = false;
    m_lastVolDown = false;
    m_lastHome = false;
    m_recenterTriggered = false;
    m_wantsRecenter = false;
    m_yawOffset = 0.0f;
    m_lastHeadYaw = 0.0f;
    m_handRole = vr::TrackedControllerRole_RightHand;
}

CDaydreamController::~CDaydreamController()
{
}

vr::EVRInitError CDaydreamController::Activate(uint32_t unObjectId)
{
    m_unObjectId = unObjectId;
    m_ulPropertyContainer = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_unObjectId);

    vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_ModelNumber_String, m_modelNumber.c_str());
    char path[MAX_PATH];
    ExpandEnvironmentStringsA("%LOCALAPPDATA%\\DaydreamSteamVR\\settings.ini", path, MAX_PATH);
    m_handRole = GetPrivateProfileIntA("Settings", "HandRole", vr::TrackedControllerRole_RightHand, path);

    vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_RenderModelName_String, "vr_controller_vive_1_5");
    vr::VRProperties()->SetStringProperty(m_ulPropertyContainer, vr::Prop_InputProfilePath_String, "{daydream}/input/daydream_profile.json");
    vr::VRProperties()->SetInt32Property(m_ulPropertyContainer, vr::Prop_DeviceClass_Int32, vr::TrackedDeviceClass_Controller);
    vr::VRProperties()->SetInt32Property(m_ulPropertyContainer, vr::Prop_ControllerRoleHint_Int32, m_handRole);

    vr::VRDriverInput()->CreateBooleanComponent(m_ulPropertyContainer, "/input/trigger/click", &m_compClick);
    vr::VRDriverInput()->CreateScalarComponent(m_ulPropertyContainer, "/input/trigger/value", &m_compTriggerValue, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
    vr::VRDriverInput()->CreateBooleanComponent(m_ulPropertyContainer, "/input/trackpad/touch", &m_compTouch);
    vr::VRDriverInput()->CreateBooleanComponent(m_ulPropertyContainer, "/input/application_menu/click", &m_compApp);
    vr::VRDriverInput()->CreateBooleanComponent(m_ulPropertyContainer, "/input/system/click", &m_compHome);
    vr::VRDriverInput()->CreateScalarComponent(m_ulPropertyContainer, "/input/trackpad/x", &m_compTouchX, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
    vr::VRDriverInput()->CreateScalarComponent(m_ulPropertyContainer, "/input/trackpad/y", &m_compTouchY, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);

    StartPipeClient();

    return vr::VRInitError_None;
}

void CDaydreamController::Deactivate()
{
    m_pipeRunning = false;
    if (m_hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hPipe);
        m_hPipe = INVALID_HANDLE_VALUE;
    }
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

void CDaydreamController::StartPipeClient()
{
    m_pipeRunning = true;
    std::thread([this]() {
        while (m_pipeRunning) {
            m_hPipe = CreateFileA("\\\\.\\pipe\\DaydreamSteamVR", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (m_hPipe != INVALID_HANDLE_VALUE) {
                DWORD mode = PIPE_READMODE_MESSAGE;
                SetNamedPipeHandleState(m_hPipe, &mode, NULL, NULL);
                DaydreamData data;
                DWORD bytesRead = 0;
                while (m_pipeRunning && ReadFile(m_hPipe, &data, sizeof(DaydreamData), &bytesRead, NULL) && bytesRead == sizeof(DaydreamData)) {
                    HandleData(data);
                }
                CloseHandle(m_hPipe);
                m_hPipe = INVALID_HANDLE_VALUE;
            } else {
                Sleep(200); // Wait for app to connect
            }
        }
    }).detach();
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
            m_wantsRecenter = true;
            m_recenterTriggered = true;
            DriverLog("CDaydreamController: Recentered Controller!\n");
        }
    }
    
    m_lastHome = homePressed;

    vr::VRDriverInput()->UpdateBooleanComponent(m_compClick, data.click, 0);
    vr::VRDriverInput()->UpdateScalarComponent(m_compTriggerValue, data.click ? 1.0f : 0.0f, 0);
    vr::VRDriverInput()->UpdateBooleanComponent(m_compTouch, data.touched, 0);
    vr::VRDriverInput()->UpdateBooleanComponent(m_compApp, data.app, 0);
    vr::VRDriverInput()->UpdateBooleanComponent(m_compHome, homePressed, 0);
    vr::VRDriverInput()->UpdateScalarComponent(m_compTouchX, data.touchX, 0);
    vr::VRDriverInput()->UpdateScalarComponent(m_compTouchY, data.touchY, 0);

    HandleMediaKeys(data);
}

void CDaydreamController::UpdatePose(const DaydreamData& data)
{
    std::lock_guard<std::mutex> lock(m_poseMutex);
    
    float x = data.oriX;
    float y = data.oriY;
    float z = data.oriZ;

    float angle = sqrt(x*x + y*y + z*z);
    vr::HmdQuaternion_t q_sensor;
    if (angle > 0.00001f) {
        float factor = sin(angle / 2.0f) / angle;
        q_sensor.x = x * factor;
        q_sensor.y = y * factor;
        q_sensor.z = z * factor;
        q_sensor.w = cos(angle / 2.0f);
    } else {
        q_sensor.w = 1.0f; q_sensor.x = 0.0f; q_sensor.y = 0.0f; q_sensor.z = 0.0f;
    }

    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
    vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0, poses, vr::k_unMaxTrackedDeviceCount);

    float handX = 0.0f, handY = 1.0f, handZ = -0.5f; 
    
    bool bPoseIsValid = poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid;
    if (bPoseIsValid) {
        auto& mat = poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
        float forwardX = -mat.m[0][2];
        float forwardZ = -mat.m[2][2];
        m_lastHeadYaw = atan2(forwardX, forwardZ);
    }
    
    float fX = -2.0f * (q_sensor.x * q_sensor.z + q_sensor.w * q_sensor.y);
    float fZ = -(1.0f - 2.0f * (q_sensor.x * q_sensor.x + q_sensor.y * q_sensor.y));
    float yaw_sensor = atan2(fX, fZ);

    if (m_wantsRecenter) {
        m_yawOffset = m_lastHeadYaw - yaw_sensor;
        m_wantsRecenter = false;
    }

    float cy = cos(m_yawOffset / 2.0f);
    float sy = sin(m_yawOffset / 2.0f);
    vr::HmdQuaternion_t q;
    q.w = cy * q_sensor.w - sy * q_sensor.y;
    q.x = cy * q_sensor.x + sy * q_sensor.z;
    q.y = cy * q_sensor.y + sy * q_sensor.w;
    q.z = cy * q_sensor.z - sy * q_sensor.x;

    if (bPoseIsValid) {
        auto& mat = poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
        float headX = mat.m[0][3];
        float headY = mat.m[1][3];
        float headZ = mat.m[2][3];
        
        float sign = (m_handRole == vr::TrackedControllerRole_LeftHand) ? -1.0f : 1.0f;
        float shoulderOffsetX = cos(m_lastHeadYaw) * (0.15f * sign) + sin(m_lastHeadYaw) * 0.05f;
        float shoulderOffsetY = -0.25f;
        float shoulderOffsetZ = -sin(m_lastHeadYaw) * (0.15f * sign) + cos(m_lastHeadYaw) * 0.05f;
        
        float shoulderX = headX + shoulderOffsetX;
        float shoulderY = headY + shoulderOffsetY;
        float shoulderZ = headZ + shoulderOffsetZ;
        
        float dirX = -2.0f * (q.x * q.z + q.w * q.y);
        float dirY = -2.0f * (q.y * q.z - q.w * q.x);
        float dirZ = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));
        
        float armLength = 0.35f;
        
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

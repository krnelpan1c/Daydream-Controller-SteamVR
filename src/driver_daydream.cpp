#include <openvr_driver.h>
#include <string.h>
#include "CDaydreamProvider.h"

CDaydreamProvider g_serverDriverProvider;

extern "C" __declspec(dllexport) void *HmdDriverFactory(const char *pInterfaceName, int *pReturnCode)
{
    if (0 == strcmp(vr::IServerTrackedDeviceProvider_Version, pInterfaceName))
    {
        return &g_serverDriverProvider;
    }
    
    if (pReturnCode)
        *pReturnCode = vr::VRInitError_Init_InterfaceNotFound;

    return nullptr;
}

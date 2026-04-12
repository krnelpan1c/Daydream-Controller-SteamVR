#pragma once
#include <openvr_driver.h>
#include <string>

extern vr::IVRDriverLog *g_pDriverLog;

void DriverLog(const char *pchFormat, ...);
void DebugDriverLog(const char *pchFormat, ...);

#include "driver_log.h"
#include <stdio.h>
#include <stdarg.h>

vr::IVRDriverLog *g_pDriverLog = nullptr;

void DriverLog(const char *pchFormat, ...)
{
    if (g_pDriverLog)
    {
        va_list args;
        va_start(args, pchFormat);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), pchFormat, args);
        va_end(args);
        g_pDriverLog->Log(buffer);
    }
}

void DebugDriverLog(const char *pchFormat, ...)
{
#if defined(_DEBUG)
    if (g_pDriverLog)
    {
        va_list args;
        va_start(args, pchFormat);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), pchFormat, args);
        va_end(args);
        g_pDriverLog->Log(buffer);
    }
#endif
}

#pragma once
#include <windows.h>
#include <string>

// settings.ini is shared between the config app (which edits it) and the driver (which watches
// it and reloads on change). Both sides go through these names so they stay in sync.
namespace DaydreamSettings {

constexpr const char *kSection = "Settings";
constexpr const char *kLeftId = "LeftID";
constexpr const char *kRightId = "RightID";
constexpr const char *kMapClick = "MapClick";
constexpr const char *kMapApp = "MapApp";
constexpr const char *kMapHome = "MapHome";
constexpr const char *kMapVolUp = "MapVolUp";
constexpr const char *kMapVolDown = "MapVolDown";

// Set by the config app to ask the driver to assign the next controller that presses a button
// to a hand. The driver writes the controller's ID to LeftID/RightID and clears this key.
constexpr const char *kCapture = "Capture";
constexpr const char *kCaptureLeft = "Left";
constexpr const char *kCaptureRight = "Right";

// Button mapping targets, indexes into the config app's dropdowns.
enum Target {
  Target_TrackpadClick = 0,
  Target_Trigger = 1,
  Target_Grip = 2,
  Target_AppMenu = 3,
  Target_System = 4,
  Target_VolumeUp = 5,
  Target_VolumeDown = 6,
  Target_Unmapped = 7,
};

constexpr int kDefaultMapClick = Target_Trigger;
constexpr int kDefaultMapApp = Target_AppMenu;
constexpr int kDefaultMapHome = Target_System;
constexpr int kDefaultMapVolUp = Target_VolumeUp;
constexpr int kDefaultMapVolDown = Target_VolumeDown;

inline std::string GetDirectory() {
  char path[MAX_PATH];
  ExpandEnvironmentStringsA("%LOCALAPPDATA%\\DaydreamSteamVR", path, MAX_PATH);
  CreateDirectoryA(path, NULL);
  return path;
}

inline std::string GetPath() { return GetDirectory() + "\\settings.ini"; }

inline std::string ReadString(const char *key) {
  char buf[256];
  GetPrivateProfileStringA(kSection, key, "", buf, sizeof(buf), GetPath().c_str());
  return buf;
}

inline int ReadInt(const char *key, int defaultValue) {
  return GetPrivateProfileIntA(kSection, key, defaultValue, GetPath().c_str());
}

inline void Write(const char *key, const std::string &value) {
  WritePrivateProfileStringA(kSection, key, value.c_str(), GetPath().c_str());
}

} // namespace DaydreamSettings

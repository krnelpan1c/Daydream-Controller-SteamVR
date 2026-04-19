// clang-format off
#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
// clang-format on
#include "DaydreamBLEHandler.h"
#include "DaydreamPacket.h"
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <memory>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

using namespace winrt;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;

#define IDC_BTN_INSTALL 1001
#define IDC_BTN_SETTINGS 1002
#define IDC_LABEL_LEFT_STATUS 1006
#define IDC_LABEL_RIGHT_STATUS 1007

#define IDC_COMBO_CLICK 2001
#define IDC_COMBO_APP 2002
#define IDC_COMBO_HOME 2003
#define IDC_COMBO_VOLUP 2004
#define IDC_COMBO_VOLDOWN 2005
#define IDC_BTN_ASSIGN_LEFT 2006
#define IDC_BTN_ASSIGN_RIGHT 2007
#define IDC_BTN_SAVE_SETTINGS 2008
#define IDC_BTN_CLEAR_ASSIGN 2009

#define WM_TRAYICON (WM_APP + 1)

HWND hInstallBtn, hSettingsBtn, hStatusLeft, hStatusRight;
NOTIFYICONDATAA g_nid = {};

HANDLE g_hPipeLeft = INVALID_HANDLE_VALUE;
HANDLE g_hPipeRight = INVALID_HANDLE_VALUE;
std::atomic<bool> g_pipeConnectedLeft{false};
std::atomic<bool> g_pipeConnectedRight{false};

std::vector<std::unique_ptr<DaydreamBLEHandler>> g_bleHandlers;
std::mutex g_handlersMutex;

std::string g_leftId = "";
std::string g_rightId = "";

std::atomic<bool> g_captureLeft{false};
std::atomic<bool> g_captureRight{false};

std::atomic<uint64_t> g_leftLastDataTime{0};
std::atomic<uint64_t> g_rightLastDataTime{0};

int g_mapClick = 1;
int g_mapApp = 3;
int g_mapHome = 4;
int g_mapVolUp = 5;
int g_mapVolDown = 6;

HWND g_hSettingsHwnd = NULL;

const char* targetNames[] = {"Trackpad Click", "Trigger", "Grip", "Application Menu", "System", "Volume Up", "Volume Down", "Unmapped"};

std::string GetSettingsPath() {
  char path[MAX_PATH];
  ExpandEnvironmentStringsA("%LOCALAPPDATA%\\DaydreamSteamVR", path, MAX_PATH);
  CreateDirectoryA(path, NULL);
  return std::string(path) + "\\settings.ini";
}

void LoadSettings() {
  std::string path = GetSettingsPath();
  char buf[256];
  GetPrivateProfileStringA("Settings", "LeftID", "", buf, 256, path.c_str());
  g_leftId = buf;
  GetPrivateProfileStringA("Settings", "RightID", "", buf, 256, path.c_str());
  g_rightId = buf;

  g_mapClick = GetPrivateProfileIntA("Settings", "MapClick", 1, path.c_str());
  g_mapApp = GetPrivateProfileIntA("Settings", "MapApp", 3, path.c_str());
  g_mapHome = GetPrivateProfileIntA("Settings", "MapHome", 4, path.c_str());
  g_mapVolUp = GetPrivateProfileIntA("Settings", "MapVolUp", 5, path.c_str());
  g_mapVolDown = GetPrivateProfileIntA("Settings", "MapVolDown", 6, path.c_str());
}

void SaveSettings() {
  std::string path = GetSettingsPath();
  WritePrivateProfileStringA("Settings", "LeftID", g_leftId.c_str(), path.c_str());
  WritePrivateProfileStringA("Settings", "RightID", g_rightId.c_str(), path.c_str());

  WritePrivateProfileStringA("Settings", "MapClick", std::to_string(g_mapClick).c_str(), path.c_str());
  WritePrivateProfileStringA("Settings", "MapApp", std::to_string(g_mapApp).c_str(), path.c_str());
  WritePrivateProfileStringA("Settings", "MapHome", std::to_string(g_mapHome).c_str(), path.c_str());
  WritePrivateProfileStringA("Settings", "MapVolUp", std::to_string(g_mapVolUp).c_str(), path.c_str());
  WritePrivateProfileStringA("Settings", "MapVolDown", std::to_string(g_mapVolDown).c_str(), path.c_str());
}

void UpdateStatus(HWND hwnd, const char *text) {
  OutputDebugStringA((std::string(text) + "\n").c_str());
}

void SendDataToLeftPipe(const DaydreamData &data) {
  if (g_pipeConnectedLeft && g_hPipeLeft != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    if (!WriteFile(g_hPipeLeft, &data, sizeof(DaydreamData), &written, NULL)) g_pipeConnectedLeft = false;
  }
}

void SendDataToRightPipe(const DaydreamData &data) {
  if (g_pipeConnectedRight && g_hPipeRight != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    if (!WriteFile(g_hPipeRight, &data, sizeof(DaydreamData), &written, NULL)) g_pipeConnectedRight = false;
  }
}

void RouteData(std::string deviceId, const DaydreamData &data) {
  if (data.click || data.app || data.home || data.volDown || data.volUp) {
    if (g_captureLeft) {
      g_leftId = deviceId;
      if (g_rightId == deviceId) g_rightId = "";
      SaveSettings();
      g_captureLeft = false;
      UpdateStatus(NULL, ("Assigned to Left Hand: " + deviceId).c_str());
      if (g_hSettingsHwnd) SetWindowTextA(GetDlgItem(g_hSettingsHwnd, IDC_BTN_ASSIGN_LEFT), "Assign Left Controller");
      return;
    } else if (g_captureRight) {
      g_rightId = deviceId;
      if (g_leftId == deviceId) g_leftId = "";
      SaveSettings();
      g_captureRight = false;
      UpdateStatus(NULL, ("Assigned to Right Hand: " + deviceId).c_str());
      if (g_hSettingsHwnd) SetWindowTextA(GetDlgItem(g_hSettingsHwnd, IDC_BTN_ASSIGN_RIGHT), "Assign Right Controller");
      return;
    }
  }

  if (deviceId == g_leftId) {
    g_leftLastDataTime = GetTickCount64();
    SendDataToLeftPipe(data);
  } else if (deviceId == g_rightId) {
    g_rightLastDataTime = GetTickCount64();
    SendDataToRightPipe(data);
  }
}

void StartNamedPipeServers() {
  std::thread([]() {
    g_hPipeLeft = CreateNamedPipeA("\\\\.\\pipe\\DaydreamSteamVR_Left", PIPE_ACCESS_OUTBOUND, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, sizeof(DaydreamData), sizeof(DaydreamData), 0, NULL);
    while (true) {
      if (g_hPipeLeft != INVALID_HANDLE_VALUE) {
        if (ConnectNamedPipe(g_hPipeLeft, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED)) {
          g_pipeConnectedLeft = true;
          while (g_pipeConnectedLeft) Sleep(100);
          DisconnectNamedPipe(g_hPipeLeft);
        }
      } else Sleep(1000);
    }
  }).detach();

  std::thread([]() {
    g_hPipeRight = CreateNamedPipeA("\\\\.\\pipe\\DaydreamSteamVR_Right", PIPE_ACCESS_OUTBOUND, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, sizeof(DaydreamData), sizeof(DaydreamData), 0, NULL);
    while (true) {
      if (g_hPipeRight != INVALID_HANDLE_VALUE) {
        if (ConnectNamedPipe(g_hPipeRight, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED)) {
          g_pipeConnectedRight = true;
          while (g_pipeConnectedRight) Sleep(100);
          DisconnectNamedPipe(g_hPipeRight);
        }
      } else Sleep(1000);
    }
  }).detach();
}

bool IsProcessRunning(const wchar_t *name) {
  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  PROCESSENTRY32W pe = {sizeof(PROCESSENTRY32W)};
  if (Process32FirstW(hSnap, &pe)) {
    do {
      if (_wcsicmp(pe.szExeFile, name) == 0) {
        CloseHandle(hSnap);
        return true;
      }
    } while (Process32NextW(hSnap, &pe));
  }
  CloseHandle(hSnap);
  return false;
}

void StartProcessMonitor(HWND hwnd) {
  std::thread([hwnd]() {
    bool steamVRWasRunning = false;
    while (true) {
      bool running = IsProcessRunning(L"vrserver.exe");
      if (running) {
        steamVRWasRunning = true;
      } else if (steamVRWasRunning && !running) {
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        break;
      }
      Sleep(2000);
    }
  }).detach();
}

void InstallDriver(HWND hwnd) {
  char exePath[MAX_PATH];
  GetModuleFileNameA(NULL, exePath, MAX_PATH);
  std::filesystem::path p(exePath);
  std::string driverPath = p.parent_path().parent_path().parent_path().string();

  HKEY hKey;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
    char steamPath[MAX_PATH];
    DWORD size = MAX_PATH;
    if (RegQueryValueExA(hKey, "InstallPath", NULL, NULL, (LPBYTE)steamPath, &size) == ERROR_SUCCESS) {
      std::string vrpathreg = std::string(steamPath) + "\\steamapps\\common\\SteamVR\\bin\\win64\\vrpathreg.exe";
      if (std::filesystem::exists(vrpathreg)) {
        std::string cmd = "\"" + vrpathreg + "\" adddriver \"" + driverPath + "\"";
        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
          WaitForSingleObject(pi.hProcess, INFINITE);
          CloseHandle(pi.hProcess);
          CloseHandle(pi.hThread);
          UpdateStatus(hwnd, "SteamVR Driver Installed Successfully!");
          RegCloseKey(hKey);
          return;
        }
      }
    }
    RegCloseKey(hKey);
  }
  UpdateStatus(hwnd, "Failed to run vrpathreg.exe. Is SteamVR installed?");
}

void ConnectControllersBackground(HWND hwnd) {
  UpdateStatus(hwnd, "Scanning for Daydream controllers (Make sure white LED is pulsing to pair)...");
  std::thread([hwnd]() {
    try {
      winrt::init_apartment();

      auto selector = L"System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\" AND System.Devices.Aep.IsPaired:=System.StructuredQueryType.Boolean#True";
      auto devicesInfo = DeviceInformation::FindAllAsync(selector, {L"System.Devices.Aep.DeviceAddress"}, DeviceInformationKind::AssociationEndpoint).get();
      
      for (auto &&info : devicesInfo) {
        std::wstring_view nameView = info.Name();
        if (nameView.find(L"Daydream") != std::wstring_view::npos || nameView.find(L"daydream") != std::wstring_view::npos) {
          std::string devIdStr = winrt::to_string(info.Id());
          auto handler = std::make_unique<DaydreamBLEHandler>();
          handler->Start([devIdStr](const DaydreamData &data) { RouteData(devIdStr, data); }, 0, std::wstring(info.Id()));
          std::lock_guard<std::mutex> lock(g_handlersMutex);
          g_bleHandlers.push_back(std::move(handler));
        }
      }

      BluetoothLEAdvertisementWatcher watcher;
      watcher.ScanningMode(BluetoothLEScanningMode::Active);
      watcher.Received([hwnd](BluetoothLEAdvertisementWatcher const &, BluetoothLEAdvertisementReceivedEventArgs const &args) {
          bool isDaydream = false;
          std::wstring_view nameView = args.Advertisement().LocalName();
          if (nameView.find(L"Daydream") != std::wstring_view::npos || nameView.find(L"daydream") != std::wstring_view::npos) isDaydream = true;
          else {
              for (auto &&uuid : args.Advertisement().ServiceUuids()) {
                  if (uuid == winrt::guid("0000fe55-0000-1000-8000-00805f9b34fb") || uuid == winrt::guid("0000fef5-0000-1000-8000-00805f9b34fb")) { isDaydream = true; break; }
              }
          }
          if (isDaydream) {
              uint64_t addr = args.BluetoothAddress();
              try {
                  auto device = BluetoothLEDevice::FromBluetoothAddressAsync(addr).get();
                  if (device && device.DeviceInformation().Pairing().CanPair()) {
                      auto custom = device.DeviceInformation().Pairing().Custom();
                      auto token = std::make_shared<winrt::event_token>();
                      *token = custom.PairingRequested([token, custom](DeviceInformationCustomPairing const &, DevicePairingRequestedEventArgs const &args) { args.Accept(); });
                      auto result = custom.PairAsync(DevicePairingKinds::ConfirmOnly).get();
                      custom.PairingRequested(*token);

                      if (result.Status() == DevicePairingResultStatus::Paired || result.Status() == DevicePairingResultStatus::AlreadyPaired) {
                          std::string devIdStr = winrt::to_string(device.DeviceInformation().Id());
                          auto handler = std::make_unique<DaydreamBLEHandler>();
                          handler->Start([devIdStr](const DaydreamData &data) { RouteData(devIdStr, data); }, addr, L"");
                          std::lock_guard<std::mutex> lock(g_handlersMutex);
                          g_bleHandlers.push_back(std::move(handler));
                          UpdateStatus(hwnd, "New controller paired and connected.");
                      }
                  }
              } catch (...) {}
          }
      });
      watcher.Start();
      while(true) Sleep(10000); 
    } catch (...) {}
    winrt::uninit_apartment();
  }).detach();
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE: {
      CreateWindowA("STATIC", "Press a button on controller to assign:", WS_VISIBLE|WS_CHILD, 20, 20, 300, 20, hwnd, NULL, NULL, NULL);
      CreateWindowA("BUTTON", "Assign Left Controller", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON, 20, 50, 200, 30, hwnd, (HMENU)IDC_BTN_ASSIGN_LEFT, NULL, NULL);
      CreateWindowA("BUTTON", "Assign Right Controller", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON, 230, 50, 200, 30, hwnd, (HMENU)IDC_BTN_ASSIGN_RIGHT, NULL, NULL);
      CreateWindowA("BUTTON", "Clear Assignments", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON, 150, 85, 150, 30, hwnd, (HMENU)IDC_BTN_CLEAR_ASSIGN, NULL, NULL);
      
      CreateWindowA("STATIC", "Button Mappings (Requires SteamVR Restart):", WS_VISIBLE|WS_CHILD, 20, 130, 400, 20, hwnd, NULL, NULL, NULL);
      
      const char* lbls[] = {"Touchpad Click:", "App Button:", "Home Button:", "Volume Up:", "Volume Down:"};
      int ids[] = {IDC_COMBO_CLICK, IDC_COMBO_APP, IDC_COMBO_HOME, IDC_COMBO_VOLUP, IDC_COMBO_VOLDOWN};
      int* vals[] = {&g_mapClick, &g_mapApp, &g_mapHome, &g_mapVolUp, &g_mapVolDown};

      for(int i=0; i<5; ++i) {
        CreateWindowA("STATIC", lbls[i], WS_VISIBLE|WS_CHILD, 20, 170 + i*40, 120, 20, hwnd, NULL, NULL, NULL);
        HWND hCombo = CreateWindowA("COMBOBOX", "", CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE, 150, 165 + i*40, 200, 200, hwnd, (HMENU)(INT_PTR)ids[i], NULL, NULL);
        for(int j=0; j<8; ++j) SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)targetNames[j]);
        SendMessageA(hCombo, CB_SETCURSEL, *vals[i], 0);
      }

      CreateWindowA("BUTTON", "Save & Close", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON, 150, 380, 150, 30, hwnd, (HMENU)IDC_BTN_SAVE_SETTINGS, NULL, NULL);
      g_hSettingsHwnd = hwnd;
      break;
    }
    case WM_COMMAND: {
      if (LOWORD(wParam) == IDC_BTN_ASSIGN_LEFT) {
        g_captureLeft = true; g_captureRight = false;
        SetWindowTextA(GetDlgItem(hwnd, IDC_BTN_ASSIGN_LEFT), "Listening...");
        SetWindowTextA(GetDlgItem(hwnd, IDC_BTN_ASSIGN_RIGHT), "Assign Right Controller");
      } else if (LOWORD(wParam) == IDC_BTN_ASSIGN_RIGHT) {
        g_captureRight = true; g_captureLeft = false;
        SetWindowTextA(GetDlgItem(hwnd, IDC_BTN_ASSIGN_RIGHT), "Listening...");
        SetWindowTextA(GetDlgItem(hwnd, IDC_BTN_ASSIGN_LEFT), "Assign Left Controller");
      } else if (LOWORD(wParam) == IDC_BTN_CLEAR_ASSIGN) {
        g_leftId = "";
        g_rightId = "";
        SaveSettings();
        SetWindowTextA(GetDlgItem(hwnd, IDC_BTN_ASSIGN_LEFT), "Assign Left Controller");
        SetWindowTextA(GetDlgItem(hwnd, IDC_BTN_ASSIGN_RIGHT), "Assign Right Controller");
      } else if (LOWORD(wParam) == IDC_BTN_SAVE_SETTINGS) {
        g_mapClick = SendMessage(GetDlgItem(hwnd, IDC_COMBO_CLICK), CB_GETCURSEL, 0, 0);
        g_mapApp = SendMessage(GetDlgItem(hwnd, IDC_COMBO_APP), CB_GETCURSEL, 0, 0);
        g_mapHome = SendMessage(GetDlgItem(hwnd, IDC_COMBO_HOME), CB_GETCURSEL, 0, 0);
        g_mapVolUp = SendMessage(GetDlgItem(hwnd, IDC_COMBO_VOLUP), CB_GETCURSEL, 0, 0);
        g_mapVolDown = SendMessage(GetDlgItem(hwnd, IDC_COMBO_VOLDOWN), CB_GETCURSEL, 0, 0);
        SaveSettings();
        DestroyWindow(hwnd);
      }
      break;
    }
    case WM_DESTROY: g_hSettingsHwnd = NULL; break;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

void OpenSettingsWindow(HWND parent) {
  if (g_hSettingsHwnd) { SetForegroundWindow(g_hSettingsHwnd); return; }
  WNDCLASSA wc = {0};
  wc.lpfnWndProc = SettingsWndProc;
  wc.hInstance = GetModuleHandle(NULL);
  wc.lpszClassName = "DaydreamSettingsWin";
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
  RegisterClassA(&wc);
  HWND hwnd = CreateWindowExA(0, "DaydreamSettingsWin", "Controller Settings", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 480, 480, parent, NULL, GetModuleHandle(NULL), NULL);
  ShowWindow(hwnd, SW_SHOW);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
  case WM_CREATE:
    CreateWindowA("BUTTON", "Install SteamVR Driver", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 20, 20, 200, 30, hwnd, (HMENU)IDC_BTN_INSTALL, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
    CreateWindowA("BUTTON", "Controller Settings", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 20, 60, 200, 30, hwnd, (HMENU)IDC_BTN_SETTINGS, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
    
    CreateWindowA("BUTTON", "Controller Status:", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, 240, 10, 220, 80, hwnd, NULL, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
    hStatusLeft = CreateWindowA("STATIC", "Left: Unassigned", WS_VISIBLE | WS_CHILD, 250, 35, 200, 20, hwnd, (HMENU)IDC_LABEL_LEFT_STATUS, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
    hStatusRight = CreateWindowA("STATIC", "Right: Unassigned", WS_VISIBLE | WS_CHILD, 250, 60, 200, 20, hwnd, (HMENU)IDC_LABEL_RIGHT_STATUS, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
    
    SetTimer(hwnd, 1, 1000, NULL);

    g_nid.cbSize = sizeof(NOTIFYICONDATAA);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strcpy(g_nid.szTip, "Daydream VR Manager");
    Shell_NotifyIconA(NIM_ADD, &g_nid);

    LoadSettings();
    StartNamedPipeServers();
    StartProcessMonitor(hwnd);
    ConnectControllersBackground(hwnd);
    break;

  case WM_TIMER: {
    if (wParam == 1) {
      std::string lText = "Left: Unassigned";
      if (!g_leftId.empty()) {
        lText = (GetTickCount64() - g_leftLastDataTime < 2000) ? "Left: Connected" : "Left: Disconnected";
      }
      SetWindowTextA(hStatusLeft, lText.c_str());

      std::string rText = "Right: Unassigned";
      if (!g_rightId.empty()) {
        rText = (GetTickCount64() - g_rightLastDataTime < 2000) ? "Right: Connected" : "Right: Disconnected";
      }
      SetWindowTextA(hStatusRight, rText.c_str());
    }
    break;
  }

  case WM_SIZE:
    if (wParam == SIZE_MINIMIZED) ShowWindow(hwnd, SW_HIDE);
    break;

  case WM_TRAYICON:
    if (lParam == WM_LBUTTONDBLCLK) { ShowWindow(hwnd, SW_RESTORE); SetForegroundWindow(hwnd); }
    break;

  case WM_COMMAND:
    if (LOWORD(wParam) == IDC_BTN_INSTALL) InstallDriver(hwnd);
    else if (LOWORD(wParam) == IDC_BTN_SETTINGS) OpenSettingsWindow(hwnd);
    break;

  case WM_DESTROY:
    for (auto& handler : g_bleHandlers) handler->Stop();
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
    PostQuitMessage(0);
    return 0;

  default:
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
  return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  
  const char CLASS_NAME[] = "DaydreamAppWindow";
  WNDCLASSA wc = {};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = CLASS_NAME;
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
  RegisterClassA(&wc);
  HWND hwnd = CreateWindowExA(0, CLASS_NAME, "Daydream Controller Manager", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 500, 180, NULL, NULL, hInstance, NULL);
  if (hwnd == NULL) return 0;
  ShowWindow(hwnd, nCmdShow);
  MSG msg = {};
  while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
  return 0;
}

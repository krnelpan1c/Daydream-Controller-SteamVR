// clang-format off
#include <windows.h>
// clang-format on
#include "DaydreamSettings.h"
#include "app_resource.h"
#include <filesystem>
#include <string>

#define IDC_BTN_INSTALL 1001
#define IDC_LABEL_STATUS 1002
#define IDC_LABEL_LEFT_ASSIGNED 1003
#define IDC_LABEL_RIGHT_ASSIGNED 1004

#define IDC_COMBO_CLICK 2001
#define IDC_COMBO_APP 2002
#define IDC_COMBO_HOME 2003
#define IDC_COMBO_VOLUP 2004
#define IDC_COMBO_VOLDOWN 2005
#define IDC_BTN_ASSIGN_LEFT 2006
#define IDC_BTN_ASSIGN_RIGHT 2007
#define IDC_BTN_SAVE_SETTINGS 2008
#define IDC_BTN_CLEAR_ASSIGN 2009

#define TIMER_CAPTURE_POLL 1

// Hand the driver is currently waiting to assign, as written to settings.ini ("" when not capturing).
std::string g_capture = "";

const char* targetNames[] = {"Trackpad Click", "Trigger", "Grip", "Application Menu", "System", "Volume Up", "Volume Down", "Unmapped"};

void SetStatus(HWND hwnd, const char *text) {
  SetWindowTextA(GetDlgItem(hwnd, IDC_LABEL_STATUS), text);
}

void RefreshAssignments(HWND hwnd) {
  bool left = !DaydreamSettings::ReadString(DaydreamSettings::kLeftId).empty();
  bool right = !DaydreamSettings::ReadString(DaydreamSettings::kRightId).empty();
  SetWindowTextA(GetDlgItem(hwnd, IDC_LABEL_LEFT_ASSIGNED), left ? "Assigned" : "Not assigned");
  SetWindowTextA(GetDlgItem(hwnd, IDC_LABEL_RIGHT_ASSIGNED), right ? "Assigned" : "Not assigned");
  SetWindowTextA(GetDlgItem(hwnd, IDC_BTN_ASSIGN_LEFT), g_capture == DaydreamSettings::kCaptureLeft ? "Listening... (click to cancel)" : "Assign Left Controller");
  SetWindowTextA(GetDlgItem(hwnd, IDC_BTN_ASSIGN_RIGHT), g_capture == DaydreamSettings::kCaptureRight ? "Listening... (click to cancel)" : "Assign Right Controller");
}

// Asks the driver to assign the next controller that presses a button to the given hand, or
// cancels a pending request when hand is "".
void SetCapture(HWND hwnd, const std::string &hand) {
  g_capture = hand;
  DaydreamSettings::Write(DaydreamSettings::kCapture, hand);
  if (!hand.empty()) SetStatus(hwnd, "Press a button on the controller to assign it.");
  else SetStatus(hwnd, "");
  RefreshAssignments(hwnd);
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
          SetStatus(hwnd, "SteamVR driver installed successfully!");
          RegCloseKey(hKey);
          return;
        }
      }
    }
    RegCloseKey(hKey);
  }
  SetStatus(hwnd, "Failed to run vrpathreg.exe. Is SteamVR installed?");
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
  case WM_CREATE: {
    CreateWindowA("BUTTON", "Install SteamVR Driver", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON, 20, 20, 200, 30, hwnd, (HMENU)IDC_BTN_INSTALL, NULL, NULL);

    CreateWindowA("STATIC", "Controller assignment (SteamVR must be running):", WS_VISIBLE|WS_CHILD, 20, 70, 400, 20, hwnd, NULL, NULL, NULL);
    CreateWindowA("BUTTON", "Assign Left Controller", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON, 20, 95, 200, 30, hwnd, (HMENU)IDC_BTN_ASSIGN_LEFT, NULL, NULL);
    CreateWindowA("STATIC", "", WS_VISIBLE|WS_CHILD, 230, 102, 200, 20, hwnd, (HMENU)IDC_LABEL_LEFT_ASSIGNED, NULL, NULL);
    CreateWindowA("BUTTON", "Assign Right Controller", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON, 20, 130, 200, 30, hwnd, (HMENU)IDC_BTN_ASSIGN_RIGHT, NULL, NULL);
    CreateWindowA("STATIC", "", WS_VISIBLE|WS_CHILD, 230, 137, 200, 20, hwnd, (HMENU)IDC_LABEL_RIGHT_ASSIGNED, NULL, NULL);
    CreateWindowA("BUTTON", "Clear Assignments", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON, 20, 165, 200, 30, hwnd, (HMENU)IDC_BTN_CLEAR_ASSIGN, NULL, NULL);

    CreateWindowA("STATIC", "Button Mappings:", WS_VISIBLE|WS_CHILD, 20, 215, 400, 20, hwnd, NULL, NULL, NULL);

    const char* lbls[] = {"Touchpad Click:", "App Button:", "Home Button:", "Volume Up:", "Volume Down:"};
    int ids[] = {IDC_COMBO_CLICK, IDC_COMBO_APP, IDC_COMBO_HOME, IDC_COMBO_VOLUP, IDC_COMBO_VOLDOWN};
    int vals[] = {
      DaydreamSettings::ReadInt(DaydreamSettings::kMapClick, DaydreamSettings::kDefaultMapClick),
      DaydreamSettings::ReadInt(DaydreamSettings::kMapApp, DaydreamSettings::kDefaultMapApp),
      DaydreamSettings::ReadInt(DaydreamSettings::kMapHome, DaydreamSettings::kDefaultMapHome),
      DaydreamSettings::ReadInt(DaydreamSettings::kMapVolUp, DaydreamSettings::kDefaultMapVolUp),
      DaydreamSettings::ReadInt(DaydreamSettings::kMapVolDown, DaydreamSettings::kDefaultMapVolDown),
    };

    for(int i=0; i<5; ++i) {
      CreateWindowA("STATIC", lbls[i], WS_VISIBLE|WS_CHILD, 20, 250 + i*40, 120, 20, hwnd, NULL, NULL, NULL);
      HWND hCombo = CreateWindowA("COMBOBOX", "", CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE, 150, 245 + i*40, 200, 200, hwnd, (HMENU)(INT_PTR)ids[i], NULL, NULL);
      for(int j=0; j<8; ++j) SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)targetNames[j]);
      SendMessageA(hCombo, CB_SETCURSEL, vals[i], 0);
    }

    CreateWindowA("BUTTON", "Save Mappings", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON, 150, 455, 150, 30, hwnd, (HMENU)IDC_BTN_SAVE_SETTINGS, NULL, NULL);
    CreateWindowA("STATIC", "", WS_VISIBLE|WS_CHILD, 20, 500, 430, 20, hwnd, (HMENU)IDC_LABEL_STATUS, NULL, NULL);

    // Drop any capture request left over from a previous run.
    SetCapture(hwnd, "");
    SetTimer(hwnd, TIMER_CAPTURE_POLL, 500, NULL);
    break;
  }

  case WM_TIMER:
    // The driver clears the capture request once it has assigned a controller.
    if (wParam == TIMER_CAPTURE_POLL && !g_capture.empty() && DaydreamSettings::ReadString(DaydreamSettings::kCapture).empty()) {
      std::string hand = g_capture;
      g_capture = "";
      SetStatus(hwnd, (hand + " controller assigned.").c_str());
      RefreshAssignments(hwnd);
    }
    break;

  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case IDC_BTN_INSTALL:
      InstallDriver(hwnd);
      break;
    case IDC_BTN_ASSIGN_LEFT:
      SetCapture(hwnd, g_capture == DaydreamSettings::kCaptureLeft ? "" : DaydreamSettings::kCaptureLeft);
      break;
    case IDC_BTN_ASSIGN_RIGHT:
      SetCapture(hwnd, g_capture == DaydreamSettings::kCaptureRight ? "" : DaydreamSettings::kCaptureRight);
      break;
    case IDC_BTN_CLEAR_ASSIGN:
      SetCapture(hwnd, "");
      DaydreamSettings::Write(DaydreamSettings::kLeftId, "");
      DaydreamSettings::Write(DaydreamSettings::kRightId, "");
      RefreshAssignments(hwnd);
      SetStatus(hwnd, "Assignments cleared.");
      break;
    case IDC_BTN_SAVE_SETTINGS:
      DaydreamSettings::Write(DaydreamSettings::kMapClick, std::to_string(SendMessage(GetDlgItem(hwnd, IDC_COMBO_CLICK), CB_GETCURSEL, 0, 0)));
      DaydreamSettings::Write(DaydreamSettings::kMapApp, std::to_string(SendMessage(GetDlgItem(hwnd, IDC_COMBO_APP), CB_GETCURSEL, 0, 0)));
      DaydreamSettings::Write(DaydreamSettings::kMapHome, std::to_string(SendMessage(GetDlgItem(hwnd, IDC_COMBO_HOME), CB_GETCURSEL, 0, 0)));
      DaydreamSettings::Write(DaydreamSettings::kMapVolUp, std::to_string(SendMessage(GetDlgItem(hwnd, IDC_COMBO_VOLUP), CB_GETCURSEL, 0, 0)));
      DaydreamSettings::Write(DaydreamSettings::kMapVolDown, std::to_string(SendMessage(GetDlgItem(hwnd, IDC_COMBO_VOLDOWN), CB_GETCURSEL, 0, 0)));
      SetStatus(hwnd, "Mappings saved.");
      break;
    }
    break;

  case WM_DESTROY:
    if (!g_capture.empty()) DaydreamSettings::Write(DaydreamSettings::kCapture, "");
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
  const char CLASS_NAME[] = "DaydreamAppWindow";
  WNDCLASSA wc = {};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = CLASS_NAME;
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
  RegisterClassA(&wc);
  HWND hwnd = CreateWindowExA(0, CLASS_NAME, "Daydream Controller Settings", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 480, 570, NULL, NULL, hInstance, NULL);
  if (hwnd == NULL) return 0;
  // Class icon alone gets scaled down for the title bar; set a proper small icon too.
  HICON hSmallIcon = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
  SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);
  ShowWindow(hwnd, nCmdShow);
  MSG msg = {};
  while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
  return 0;
}

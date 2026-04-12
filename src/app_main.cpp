#include <windows.h>
#include <commctrl.h>
#include <string>
#include <thread>
#include <filesystem>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <tlhelp32.h>
#include "DaydreamBLEHandler.h"
#include "DaydreamPacket.h"

using namespace winrt;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;

#define IDC_BTN_INSTALL 1001
#define IDC_BTN_PAIR 1002
#define IDC_RADIO_RIGHT 1003
#define IDC_RADIO_LEFT 1004
#define IDC_LABEL_STATUS 1005
#define WM_TRAYICON (WM_APP + 1)

HWND hInstallBtn, hPairBtn, hRightRadio, hLeftRadio, hStatusLabel;
BluetoothLEAdvertisementWatcher g_advWatcher{nullptr};
winrt::event_token g_advToken;
bool g_pairing = false;
NOTIFYICONDATAA g_nid = {};

HANDLE g_hPipe = INVALID_HANDLE_VALUE;
std::atomic<bool> g_pipeConnected{false};
DaydreamBLEHandler g_bleHandler;

std::string GetSettingsPath() {
    char path[MAX_PATH];
    ExpandEnvironmentStringsA("%LOCALAPPDATA%\\DaydreamSteamVR", path, MAX_PATH);
    CreateDirectoryA(path, NULL);
    return std::string(path) + "\\settings.ini";
}

int GetHandRole() {
    return GetPrivateProfileIntA("Settings", "HandRole", 1, GetSettingsPath().c_str()); 
}

void SetHandRole(int role) {
    std::string val = std::to_string(role);
    WritePrivateProfileStringA("Settings", "HandRole", val.c_str(), GetSettingsPath().c_str());
}

void UpdateStatus(HWND hwnd, const char* text) {
    SetWindowTextA(hStatusLabel, text);
}

void SendDataToPipe(const DaydreamData& data) {
    if (g_pipeConnected && g_hPipe != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        BOOL result = WriteFile(g_hPipe, &data, sizeof(DaydreamData), &written, NULL);
        if (!result) {
            g_pipeConnected = false;
        }
    }
}

void StartNamedPipeServer() {
    std::thread([]() {
        g_hPipe = CreateNamedPipeA("\\\\.\\pipe\\DaydreamSteamVR",
            PIPE_ACCESS_OUTBOUND, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, sizeof(DaydreamData), sizeof(DaydreamData), 0, NULL);
        
        while (true) {
            if (g_hPipe != INVALID_HANDLE_VALUE) {
                if (ConnectNamedPipe(g_hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED)) {
                    g_pipeConnected = true;
                    while(g_pipeConnected) Sleep(100);
                    DisconnectNamedPipe(g_hPipe);
                }
            } else {
                Sleep(1000);
            }
        }
    }).detach();
}

bool IsProcessRunning(const wchar_t* name) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
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
                
                STARTUPINFOA si = { sizeof(si) };
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

void StartBluetoothHandler(HWND hwnd, uint64_t addr = 0, std::wstring deviceId = L"") {
    g_bleHandler.Start([](const DaydreamData& data) {
        SendDataToPipe(data);
    }, addr, deviceId);
}

void ConnectController(HWND hwnd) {
    if (g_pairing) return;
    g_pairing = true;
    UpdateStatus(hwnd, "Searching for Daydream controller... (Make sure it's probing white)");

    std::thread([hwnd]() {
        try {
            winrt::init_apartment();
            
            // 1. Check if already paired
            winrt::hstring selector = L"System.Devices.Aep.ProtocolId:=\"{bb7bb05e-5972-42b5-94fc-76eaa7084d49}\" AND System.Devices.Aep.IsPaired:=System.StructuredQueryType.Boolean#True";
            auto devicesInfo = DeviceInformation::FindAllAsync(selector, { L"System.Devices.Aep.DeviceAddress" }, DeviceInformationKind::AssociationEndpoint).get();
            for (auto&& info : devicesInfo) {
                std::wstring_view nameView = info.Name();
                if (nameView.find(L"Daydream") != std::wstring_view::npos || nameView.find(L"daydream") != std::wstring_view::npos) {
                    UpdateStatus(hwnd, "Controller is paired & connecting! You can launch SteamVR.");
                    StartBluetoothHandler(hwnd, 0, std::wstring(info.Id()));
                    g_pairing = false;
                    winrt::uninit_apartment();
                    return;
                }
            }

            // 2. Scan for Advertisements (Unpaired mode)
            g_advWatcher = BluetoothLEAdvertisementWatcher();
            g_advWatcher.ScanningMode(BluetoothLEScanningMode::Active);
            
            g_advToken = g_advWatcher.Received([hwnd](BluetoothLEAdvertisementWatcher const&, BluetoothLEAdvertisementReceivedEventArgs const& args) {
                bool isDaydream = false;
                std::wstring_view nameView = args.Advertisement().LocalName();
                
                if (nameView.find(L"Daydream") != std::wstring_view::npos || nameView.find(L"daydream") != std::wstring_view::npos) {
                    isDaydream = true;
                } else {
                    for (auto&& uuid : args.Advertisement().ServiceUuids()) {
                        if (uuid == winrt::guid("0000fe55-0000-1000-8000-00805f9b34fb") || 
                            uuid == winrt::guid("0000fef5-0000-1000-8000-00805f9b34fb")) {
                            isDaydream = true;
                            break;
                        }
                    }
                }

                if (isDaydream) {
                    g_advWatcher.Stop();
                    UpdateStatus(hwnd, "Found pairing mode controller! Pairing...");
                    
                    uint64_t addr = args.BluetoothAddress();
                    std::thread([hwnd, addr]() {
                        try {
                            winrt::init_apartment();
                            auto device = BluetoothLEDevice::FromBluetoothAddressAsync(addr).get();
                            if (device) {
                                if (device.DeviceInformation().Pairing().CanPair()) {
                                    
                                    // Custom pairing to fix Code 19
                                    auto custom = device.DeviceInformation().Pairing().Custom();
                                    std::shared_ptr<winrt::event_token> token = std::make_shared<winrt::event_token>();
                                    *token = custom.PairingRequested([token, custom](DeviceInformationCustomPairing const&, DevicePairingRequestedEventArgs const& args) {
                                        args.Accept();
                                    });
                                    
                                    auto result = custom.PairAsync(DevicePairingKinds::ConfirmOnly).get();
                                    custom.PairingRequested(*token); // Cleanup
                                    
                                    if (result.Status() == DevicePairingResultStatus::Paired || result.Status() == DevicePairingResultStatus::AlreadyPaired) {
                                        UpdateStatus(hwnd, "Successfully paired to the controller! Connecting...");
                                        StartBluetoothHandler(hwnd, addr, L"");
                                    } else {
                                        std::string errStr = "Pairing failed. Code: " + std::to_string((int)result.Status());
                                        UpdateStatus(hwnd, errStr.c_str());
                                    }
                                } else {
                                    UpdateStatus(hwnd, "Controller found, but Windows says it cannot be paired.");
                                }
                            }
                        } catch(...) {
                            UpdateStatus(hwnd, "Pairing exception occurred.");
                        }
                        g_pairing = false;
                        winrt::uninit_apartment();
                    }).detach();
                }
            });
            
            g_advWatcher.Start();
            
            Sleep(15000);
            if (g_pairing) {
                g_advWatcher.Stop();
                g_pairing = false;
                UpdateStatus(hwnd, "Pairing timeout (15s). Controller not found.");
            }
        } catch(...) {
            UpdateStatus(hwnd, "Error starting BLE adv watcher.");
            g_pairing = false;
        }
        winrt::uninit_apartment();
    }).detach();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            CreateWindowA("BUTTON", "Install SteamVR Driver", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                20, 20, 200, 30, hwnd, (HMENU)IDC_BTN_INSTALL, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
            CreateWindowA("BUTTON", "Connect Controller", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                20, 60, 200, 30, hwnd, (HMENU)IDC_BTN_PAIR, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
            
            CreateWindowA("BUTTON", "Hand (Needs SteamVR Restart):", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
                240, 10, 220, 80, hwnd, NULL, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
            hRightRadio = CreateWindowA("BUTTON", "Right", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
                250, 35, 100, 20, hwnd, (HMENU)IDC_RADIO_RIGHT, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
            hLeftRadio = CreateWindowA("BUTTON", "Left", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
                250, 60, 100, 20, hwnd, (HMENU)IDC_RADIO_LEFT, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);

            if (GetHandRole() == 1) SendMessage(hRightRadio, BM_SETCHECK, BST_CHECKED, 0);
            else SendMessage(hLeftRadio, BM_SETCHECK, BST_CHECKED, 0);

            hStatusLabel = CreateWindowA("STATIC", "Ready.", WS_VISIBLE | WS_CHILD,
                20, 110, 440, 20, hwnd, (HMENU)IDC_LABEL_STATUS, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
            
            g_nid.cbSize = sizeof(NOTIFYICONDATAA);
            g_nid.hWnd = hwnd;
            g_nid.uID = 1;
            g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_nid.uCallbackMessage = WM_TRAYICON;
            g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            strcpy(g_nid.szTip, "Daydream VR Manager");
            Shell_NotifyIconA(NIM_ADD, &g_nid);
            
            StartNamedPipeServer();
            StartProcessMonitor(hwnd);
            break;
            
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) {
                ShowWindow(hwnd, SW_HIDE);
            }
            break;
            
        case WM_TRAYICON:
            if (lParam == WM_LBUTTONDBLCLK) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            }
            break;
            
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_BTN_INSTALL) {
                InstallDriver(hwnd);
            } else if (LOWORD(wParam) == IDC_BTN_PAIR) {
                ConnectController(hwnd);
            } else if (LOWORD(wParam) == IDC_RADIO_RIGHT) {
                SetHandRole(1);
            } else if (LOWORD(wParam) == IDC_RADIO_LEFT) {
                SetHandRole(2);
            }
            break;
            
        case WM_DESTROY:
            g_bleHandler.Stop();
            Shell_NotifyIconA(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
            
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    const char CLASS_NAME[] = "DaydreamAppWindow";
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    
    RegisterClassA(&wc);
    
    HWND hwnd = CreateWindowExA(0, CLASS_NAME, "Daydream Controller SteamVR Application",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 180, NULL, NULL, hInstance, NULL);
        
    if (hwnd == NULL) return 0;
    
    ShowWindow(hwnd, nCmdShow);
    
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return 0;
}

#include <Windows.h>
#include <thread>
#include <atomic>
#include "Renderer.h"
#include "WindowManager.h"
#include <vector>
#include <imgui.h>
#include <algorithm>
#include <string>
#include <shellapi.h> 
#include <shlwapi.h>

// tray
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_AUTOSTART 1002

NOTIFYICONDATA nid = {};
HMENU hTrayMenu = nullptr;
HWND hTrayWindow = nullptr;
bool autoStartEnabled = false;



bool showOverlay = false;
bool wasShiftPressed = false;
char searchBuffer[256] = "";
std::atomic<bool> triggerGui = false;
HHOOK hookHandle = nullptr;
int selectedIndex = 0;

// kbd hook shift + b detection
// should add custom binds
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;

        if (wParam == WM_KEYDOWN && p->vkCode == 'B')
        {
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                triggerGui = true;
            }
        }
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

bool IsAutoStartEnabled()
{
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    char value[260];
    DWORD size = sizeof(value);
    bool result = RegQueryValueExA(hKey, "BetterAltTab", nullptr, nullptr, (LPBYTE)value, &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return result;
}

void SetAutoStart(bool enable)
{
    HKEY hKey;
    RegCreateKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);

    if (enable)
    {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        RegSetValueExA(hKey, "BetterAltTab", 0, REG_SZ, (BYTE*)path, (DWORD)strlen(path));
    }
    else
    {
        RegDeleteValueA(hKey, "BetterAltTab");
    }

    RegCloseKey(hKey);
    autoStartEnabled = enable;
}

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);

            if (hTrayMenu) DestroyMenu(hTrayMenu);
            hTrayMenu = CreatePopupMenu();

            AppendMenu(hTrayMenu, MF_STRING | (autoStartEnabled ? MF_CHECKED : 0), ID_TRAY_AUTOSTART, L"Start on boot");
            AppendMenu(hTrayMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenu(hTrayMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

            SetForegroundWindow(hwnd); // important!
            TrackPopupMenu(hTrayMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            PostMessage(hwnd, WM_NULL, 0, 0);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_TRAY_EXIT:
            PostQuitMessage(0);
            break;
        case ID_TRAY_AUTOSTART:
            SetAutoStart(!autoStartEnabled);
            break;
        }
        break;

    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    Renderer renderer;
    if (!renderer.Init(hInstance)) return -1;

    // Set up tray icon
    autoStartEnabled = IsAutoStartEnabled();

    WNDCLASS wcTray = {};
    wcTray.lpfnWndProc = TrayWndProc;
    wcTray.hInstance = hInstance;
    wcTray.lpszClassName = L"BetterAltTabTrayWindow";
    RegisterClass(&wcTray);

    hTrayWindow = CreateWindowEx(0, wcTray.lpszClassName, L"",
        0,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, hInstance, nullptr);
    LONG exStyle = GetWindowLong(hTrayWindow, GWL_EXSTYLE);
    SetWindowLong(hTrayWindow, GWL_EXSTYLE, exStyle | WS_EX_TOOLWINDOW);


    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hTrayWindow;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, _countof(nid.szTip), L"BetterAltTab");


    Shell_NotifyIcon(NIM_ADD, &nid);


    WindowManager wm;
    wm.RefreshWindows(renderer.GetDevice());

    hookHandle = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);

    HWND selectedHwnd = nullptr;

    while (renderer.BeginFrame())
    {
        bool shiftDown = GetAsyncKeyState(VK_SHIFT) & 0x8000;

        if (triggerGui)
        {
            HWND realForeground = GetForegroundWindow();
            showOverlay = true;
            wm.RefreshWindows(renderer.GetDevice());
            searchBuffer[0] = '\0';
            selectedIndex = 0;
            selectedHwnd = nullptr;

            ShowWindow(renderer.GetHwnd(), SW_SHOW);
            SetActiveWindow(renderer.GetHwnd());
            SetFocus(renderer.GetHwnd());

            DWORD thisThread = GetCurrentThreadId();
            DWORD fgThread = GetWindowThreadProcessId(realForeground, nullptr);
            AttachThreadInput(fgThread, thisThread, TRUE);
            SetForegroundWindow(renderer.GetHwnd());
            SetFocus(renderer.GetHwnd());
            AttachThreadInput(fgThread, thisThread, FALSE);

            MONITORINFO mi = { sizeof(MONITORINFO) };
            HMONITOR monitor = MonitorFromWindow(realForeground, MONITOR_DEFAULTTONEAREST);
            GetMonitorInfo(monitor, &mi);

            RECT rc = mi.rcWork;
            int width = 600;
            int height = 400;
            int x = rc.left + (rc.right - rc.left - width) / 2;
            int y = rc.top + (rc.bottom - rc.top - height) / 2;

            SetWindowPos(renderer.GetHwnd(), HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);

            triggerGui = false;
        }

        if (showOverlay && shiftDown)
        {
            ImVec2 displaySize = ImGui::GetIO().DisplaySize;
            ImVec2 winSize = ImVec2(600, 400);
            ImVec2 center = ImVec2((displaySize.x - winSize.x) / 2, (displaySize.y - winSize.y) / 2);
            ImGui::SetNextWindowPos(center, ImGuiCond_Always);
            ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);
            ImGui::Begin("BetterAT", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));

            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##SearchInput", "Search...", searchBuffer, sizeof(searchBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                selectedIndex = 0;
            }

            ImGui::PopStyleColor();

            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                selectedIndex--;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                selectedIndex++;
            }

            if (!ImGui::IsItemActive() && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(0)) {
                ImGui::SetKeyboardFocusHere(-1);
            }

            std::wstring searchW = std::wstring(searchBuffer, searchBuffer + strlen(searchBuffer));
            std::transform(searchW.begin(), searchW.end(), searchW.begin(), ::towlower);

            int index = 0;
            int visibleCount = 0;
            HWND hoveredHwnd = nullptr;
            for (const auto& w : wm.windows)
            {
                std::wstring lowerTitle = w.title;
                std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::towlower);

                if (searchW.empty() || lowerTitle.find(searchW) != std::wstring::npos)
                {
                    if (visibleCount == selectedIndex) selectedHwnd = w.hwnd;

                    if (w.icon) ImGui::Image(w.icon, ImVec2(16, 16));
                    ImGui::SameLine();

                    // handle ms click
                    std::string label = std::string(w.title.begin(), w.title.end()) + "##" + std::to_string(reinterpret_cast<uintptr_t>(w.hwnd));
                    if (ImGui::Selectable(label.c_str(), visibleCount == selectedIndex)) {
                        selectedHwnd = w.hwnd;

                        WINDOWPLACEMENT placement = { sizeof(WINDOWPLACEMENT) };
                        GetWindowPlacement(selectedHwnd, &placement);

                        if (placement.showCmd == SW_SHOWMINIMIZED) {
                            ShowWindow(selectedHwnd, SW_RESTORE);
                        }
                        else {
                            ShowWindow(selectedHwnd, SW_SHOW);
                        }

                        SetWindowPos(selectedHwnd, HWND_TOP, 0, 0, 0, 0,
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                        SetForegroundWindow(selectedHwnd);

                        showOverlay = false;
                        ShowWindow(renderer.GetHwnd(), SW_HIDE);
                    }

                    if (ImGui::IsItemHovered()) {
                        selectedIndex = visibleCount;
                        selectedHwnd = w.hwnd;
                    }
                    visibleCount++;
                }
                ++index;
            }

            ImGui::End();
        }
        // on shift key release, execute selected/searched window
        if (showOverlay && !shiftDown && wasShiftPressed)
        {
            std::string cmd(searchBuffer);
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

            if (cmd.starts_with("r:")) {
                std::string toRun = cmd.substr(2);
                if (!toRun.empty()) {
                    ShellExecuteA(nullptr, "open", toRun.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
            }
            else if (selectedHwnd)
            {
                WINDOWPLACEMENT placement = { sizeof(WINDOWPLACEMENT) };
                GetWindowPlacement(selectedHwnd, &placement);

                if (placement.showCmd == SW_SHOWMINIMIZED) {
                    ShowWindow(selectedHwnd, SW_RESTORE);
                }
                else {
                    ShowWindow(selectedHwnd, SW_SHOW);
                }

                SetWindowPos(selectedHwnd, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

                SetForegroundWindow(selectedHwnd);
            }

            showOverlay = false;
            ShowWindow(renderer.GetHwnd(), SW_HIDE);
        }

        wasShiftPressed = shiftDown;
        renderer.EndFrame();

    }

    UnhookWindowsHookEx(hookHandle);
    renderer.Shutdown();
    return 0;
}
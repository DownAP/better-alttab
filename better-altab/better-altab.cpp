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
#include <map>
#include <unordered_map>

// tray
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_AUTOSTART 1002

NOTIFYICONDATA nid = {};
HMENU hTrayMenu = nullptr;
HWND hTrayWindow = nullptr;
bool autoStartEnabled = false;

//map, not the happiest decision 
static std::map<UINT, HWND> g_assignedWindows;
std::unordered_map<HWND, UINT> hwndToVk;

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
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        UINT vk = p->vkCode;

        if (ctrl && shift) {
            auto it = g_assignedWindows.find(vk);
            if (it != g_assignedWindows.end()) {
                HWND h = it->second;

                HWND fg = GetForegroundWindow();
                DWORD fgThread = GetWindowThreadProcessId(fg, nullptr);
                DWORD thisThread = GetCurrentThreadId();
                AttachThreadInput(fgThread, thisThread, TRUE);

                WINDOWPLACEMENT pl{ sizeof(pl) };
                GetWindowPlacement(h, &pl);
                if (pl.showCmd == SW_SHOWMINIMIZED)
                    ShowWindow(h, SW_RESTORE);
                else
                    ShowWindow(h, SW_SHOW);

                SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                SetForegroundWindow(h);
                AttachThreadInput(fgThread, thisThread, FALSE);
                return 1;
            }
        }

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

            SetForegroundWindow(hwnd);
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

    hwndToVk.reserve(g_assignedWindows.size());
    for (auto& kv : g_assignedWindows)
        hwndToVk[kv.second] = kv.first;

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

            hwndToVk.clear();
            hwndToVk.reserve(g_assignedWindows.size());
            for (auto& kv : g_assignedWindows)
                hwndToVk[kv.second] = kv.first;

            std::string cmd(searchBuffer);
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

            bool assignMode = cmd.size() >= 2 && cmd[0] == ':' && cmd[1] == 'a';
            bool unassignMode = cmd.size() >= 2 && cmd[0] == ':' && cmd[1] == 'u';

            UINT assignVk = 0;
            if (assignMode && cmd.size() >= 3) {
                SHORT vks = VkKeyScanA(cmd[2]);
                if (vks != -1) assignVk = LOBYTE(vks);
            }

            std::wstring filter;
            if (!assignMode && !unassignMode && !cmd.empty()) {
                filter = std::wstring(cmd.begin(), cmd.end());
                std::transform(filter.begin(), filter.end(),
                    filter.begin(), ::towlower);
            }

            hwndToVk.clear();
            hwndToVk.reserve(g_assignedWindows.size());
            for (auto& kv : g_assignedWindows)
                hwndToVk[kv.second] = kv.first;

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

                if (unassignMode)
                {
                    if (hwndToVk.find(w.hwnd) == hwndToVk.end())
                        continue;
                }
                else if (assignMode)
                {
                    //
                }
                // normal  mode
                else
                {
                    if (!filter.empty() &&
                        lowerTitle.find(filter) == std::wstring::npos)
                        continue;
                }
                    
                    if (visibleCount == selectedIndex) selectedHwnd = w.hwnd;

                    UINT boundVk = 0;
                    auto it = hwndToVk.find(w.hwnd);
                    if (it != hwndToVk.end())
                        boundVk = it->second;

                    // label build
                    std::string title = std::string(w.title.begin(), w.title.end());
                    std::string id = "##" + std::to_string((uintptr_t)w.hwnd);
                    std::string label;
                    if (boundVk)
                    {
                        //  VK > scan code > name
                        UINT scan = MapVirtualKeyA(boundVk, MAPVK_VK_TO_VSC);
                        char name[32] = { 0 };
                        GetKeyNameTextA(scan << 16, name, sizeof(name));

                        label = std::string("[") + name + "] " + title + id;
                    }
                    else
                    {
                        label = title + id;
                    }

                    if (w.icon) ImGui::Image(w.icon, ImVec2(16, 16));
                    ImGui::SameLine();

                    // handle ms click
                    if (ImGui::Selectable(label.c_str(), visibleCount == selectedIndex)) {
                        selectedHwnd = w.hwnd;

                        // handle bind on ms click
                        std::string cmd(searchBuffer);
                        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);


                        bool assignMode = cmd.size() >= 2 && cmd[0] == ':' && cmd[1] == 'a';
                        bool unassignMode = cmd.size() >= 2 && cmd[0] == ':' && cmd[1] == 'u';

                        UINT assignVk = 0;
                        if (assignMode && cmd.size() >= 3) {
                            SHORT vks = VkKeyScanA(cmd[2]);
                            if (vks != -1) assignVk = LOBYTE(vks);
                        }

                        if (assignMode && selectedHwnd)
                        {
                            // bind
                            g_assignedWindows[assignVk] = selectedHwnd;
                        }

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
                
                ++index;
            }

            ImGui::End();
        }
        // on shift key release, execute selected/searched window
        if (showOverlay && !shiftDown && wasShiftPressed)
        {
            std::string cmd(searchBuffer);
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);


            bool assignMode = cmd.size() >= 2 && cmd[0] == ':' && cmd[1] == 'a';
            bool unassignMode = cmd.size() >= 2 && cmd[0] == ':' && cmd[1] == 'u';

            UINT assignVk = 0;
            if (assignMode && cmd.size() >= 3) {
                SHORT vks = VkKeyScanA(cmd[2]);
                if (vks != -1) assignVk = LOBYTE(vks);
            }

            if (assignMode && selectedHwnd)
            {
                // bind
                g_assignedWindows[assignVk] = selectedHwnd;
            }
            else if (unassignMode && selectedHwnd)
            {
                for (auto it = g_assignedWindows.begin(); it != g_assignedWindows.end(); ++it)
                {
                    if (it->second == selectedHwnd)
                    {
                        g_assignedWindows.erase(it);
                        break;
                    }
                }
            }
            else if (cmd.starts_with("r:")) {
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
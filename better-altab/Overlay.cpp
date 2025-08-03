#include "Overlay.h"
#include <imgui.h>
#include <thread>
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <shellapi.h>
#include "IconUtils.h"

static bool icontains(const std::wstring& hay, const std::wstring& needle) {
    std::wstring h = hay, n = needle;
    std::transform(h.begin(), h.end(), h.begin(), ::towlower);
    std::transform(n.begin(), n.end(), n.begin(), ::towlower);
    return h.find(n) != std::wstring::npos;
}

namespace Overlay {

    static Renderer* g_renderer = nullptr;
    static WindowManager* g_wm = nullptr;

    static bool showOverlay = false;
    static bool wasShiftPressed = false;
    static char searchBuffer[256] = "";
    static std::atomic<bool> triggerGui = false;
    static HHOOK hookHandle = nullptr;
    static int selectedIndex = 0;

    static std::map<UINT, HWND> g_assignedWindows;
    static std::unordered_map<HWND, UINT> hwndToVk;

    static bool searchApps = false;
    static std::string  lastAppQuery;
    static bool         lastSearchApps = false;
    static std::vector<const AppEntry*> filtered;

    // track currently highlighted window in the overlay
    static HWND hoveredHwnd = nullptr;

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

    void Init(Renderer* renderer, WindowManager* wm) {
        g_renderer = renderer;
        g_wm = wm;
        hookHandle = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    }

    void Shutdown() {
        if (hookHandle) {
            UnhookWindowsHookEx(hookHandle);
            hookHandle = nullptr;
        }
    }

    void ProcessFrame() {
        bool shiftDown = GetAsyncKeyState(VK_SHIFT) & 0x8000;

        if (triggerGui)
        {
            HWND realForeground = GetForegroundWindow();
            showOverlay = true;
            g_wm->RefreshWindows(g_renderer->GetDevice());
            searchBuffer[0] = '\0';
            selectedIndex = 0;

            ShowWindow(g_renderer->GetHwnd(), SW_SHOW);
            SetActiveWindow(g_renderer->GetHwnd());
            SetFocus(g_renderer->GetHwnd());

            DWORD thisThread = GetCurrentThreadId();
            DWORD fgThread = GetWindowThreadProcessId(realForeground, nullptr);
            AttachThreadInput(fgThread, thisThread, TRUE);
            SetForegroundWindow(g_renderer->GetHwnd());
            SetFocus(g_renderer->GetHwnd());
            AttachThreadInput(fgThread, thisThread, FALSE);

            MONITORINFO mi = { sizeof(MONITORINFO) };
            HMONITOR monitor = MonitorFromWindow(realForeground, MONITOR_DEFAULTTONEAREST);
            GetMonitorInfo(monitor, &mi);

            RECT rc = mi.rcWork;
            int width = 600;
            int height = 400;
            int x = rc.left + (rc.right - rc.left - width) / 2;
            int y = rc.top + (rc.bottom - rc.top - height) / 2;

            SetWindowPos(g_renderer->GetHwnd(), HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);

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

            searchApps = cmd.rfind("s:", 0) == 0;
            std::string thisQuery = searchApps ? cmd.substr(2) : "";

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
                std::transform(filter.begin(), filter.end(), filter.begin(), ::towlower);
            }

            hwndToVk.clear();
            hwndToVk.reserve(g_assignedWindows.size());
            for (auto& kv : g_assignedWindows)
                hwndToVk[kv.second] = kv.first;

            if (searchApps != lastSearchApps || thisQuery != lastAppQuery) {
                lastSearchApps = searchApps;
                lastAppQuery = thisQuery;
                filtered.clear();

                std::string ascii = cmd.size() > 2 ? cmd.substr(2) : "";
                std::wstring q(ascii.begin(), ascii.end());
                std::transform(q.begin(), q.end(), q.begin(), ::towlower);

                for (auto& e : AppManager::GetAllApps()) {
                    if (ascii.empty() || icontains(e.name, q))
                        filtered.push_back(&e);
                }
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

            int visibleCount = 0;

            if (searchApps)
            {
                if (filtered.empty()) {
                    selectedIndex = 0;
                }
                else {
                    if (selectedIndex < 0) selectedIndex = 0;
                    if (selectedIndex >= (int)filtered.size()) selectedIndex = (int)filtered.size() - 1;
                }

                for (int i = 0; i < (int)filtered.size(); ++i) {
                    auto* e = filtered[i];
                    if (e->icon) { ImGui::Image(e->icon, { 16,16 }); ImGui::SameLine(); }
                    std::string label = std::string(e->name.begin(), e->name.end()) + "##app" + std::to_string(i);

                    bool isSelected = (i == selectedIndex);
                    if (ImGui::Selectable(label.c_str(), isSelected)) {
                        ShellExecuteW(nullptr, L"open", e->target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        showOverlay = false;
                    }
                    if (isSelected) ImGui::SetScrollHereY();
                    ++visibleCount;
                }
            }
            else
            {
                std::vector<const WindowEntry*> visible;
                visible.reserve(g_wm->windows.size());

                for (const auto& w : g_wm->windows)
                {
                    std::wstring lowerTitle = w.title;
                    std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::towlower);

                    if (unassignMode)
                    {
                        if (hwndToVk.find(w.hwnd) == hwndToVk.end())
                            continue;
                    }
                    else if (!assignMode)
                    {
                        if (!filter.empty() &&
                            lowerTitle.find(filter) == std::wstring::npos)
                            continue;
                    }

                    visible.push_back(&w);
                }

                visibleCount = (int)visible.size();
                if (visibleCount > 0)
                {
                    if (selectedIndex < 0) selectedIndex = 0;
                    if (selectedIndex >= visibleCount) selectedIndex = visibleCount - 1;
                    hoveredHwnd = visible[selectedIndex]->hwnd;
                }
                else
                {
                    selectedIndex = 0;
                    hoveredHwnd = nullptr;
                }

                for (int i = 0; i < visibleCount; ++i)
                {
                    const WindowEntry* w = visible[i];

                    UINT boundVk = 0;
                    auto it = hwndToVk.find(w->hwnd);
                    if (it != hwndToVk.end())
                        boundVk = it->second;

                    std::string title = std::string(w->title.begin(), w->title.end());
                    std::string id = "##" + std::to_string((uintptr_t)w->hwnd);
                    std::string label;
                    if (boundVk)
                    {
                        UINT scan = MapVirtualKeyA(boundVk, MAPVK_VK_TO_VSC);
                        char name[32] = { 0 };
                        GetKeyNameTextA(scan << 16, name, sizeof(name));

                        label = std::string("[") + name + "] " + title + id;
                    }
                    else
                    {
                        label = title + id;
                    }

                    if (w->icon) ImGui::Image(w->icon, ImVec2(16, 16));
                    ImGui::SameLine();

                    bool isSelected = (i == selectedIndex);
                    if (ImGui::Selectable(label.c_str(), isSelected)) {
                        hoveredHwnd = w->hwnd;

                        std::string cmd(searchBuffer);
                        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

                        bool assignMode = cmd.size() >= 2 && cmd[0] == ':' && cmd[1] == 'a';
                        bool unassignMode = cmd.size() >= 2 && cmd[0] == ':' && cmd[1] == 'u';

                        UINT assignVk = 0;
                        if (assignMode && cmd.size() >= 3) {
                            SHORT vks = VkKeyScanA(cmd[2]);
                            if (vks != -1) assignVk = LOBYTE(vks);
                        }

                        if (assignMode && hoveredHwnd)
                        {
                            g_assignedWindows[assignVk] = hoveredHwnd;
                        }

                        WINDOWPLACEMENT placement = { sizeof(WINDOWPLACEMENT) };
                        GetWindowPlacement(hoveredHwnd, &placement);

                        if (placement.showCmd == SW_SHOWMINIMIZED) {
                            ShowWindow(hoveredHwnd, SW_RESTORE);
                        }
                        else {
                            ShowWindow(hoveredHwnd, SW_SHOW);
                        }

                        SetWindowPos(hoveredHwnd, HWND_TOP, 0, 0, 0, 0,
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                        SetForegroundWindow(hoveredHwnd);

                        showOverlay = false;
                        ShowWindow(g_renderer->GetHwnd(), SW_HIDE);
                    }

                    if (isSelected) {
                        ImGui::SetScrollHereY();
                        hoveredHwnd = w->hwnd;
                    }

                    if (ImGui::IsItemHovered()) {
                        selectedIndex = i;
                        hoveredHwnd = w->hwnd;
                    }
                }
            }

            ImGui::End();
        }
        if (showOverlay && !shiftDown && wasShiftPressed)
        {
            std::string cmd(searchBuffer);
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

            searchApps = (cmd.rfind("s:", 0) == 0);
            bool assignMode = cmd.size() >= 2 && cmd[0] == ':' && cmd[1] == 'a';
            bool unassignMode = cmd.size() >= 2 && cmd[0] == ':' && cmd[1] == 'u';

            UINT assignVk = 0;
            if (assignMode && cmd.size() >= 3) {
                SHORT vks = VkKeyScanA(cmd[2]);
                if (vks != -1) assignVk = LOBYTE(vks);
            }

            if (searchApps && selectedIndex >= 0 && selectedIndex < (int)filtered.size()) {
                ShellExecuteW(nullptr, L"open",
                    filtered[selectedIndex]->target.c_str(),
                    nullptr, nullptr, SW_SHOWNORMAL);
            }
            if (assignMode && hoveredHwnd)
            {
                g_assignedWindows[assignVk] = hoveredHwnd;
            }
            else if (unassignMode && hoveredHwnd)
            {
                for (auto it = g_assignedWindows.begin(); it != g_assignedWindows.end(); ++it)
                {
                    if (it->second == hoveredHwnd)
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
            else if (hoveredHwnd)
            {
                WINDOWPLACEMENT placement = { sizeof(WINDOWPLACEMENT) };
                GetWindowPlacement(hoveredHwnd, &placement);

                if (placement.showCmd == SW_SHOWMINIMIZED) {
                    ShowWindow(hoveredHwnd, SW_RESTORE);
                }
                else {
                    ShowWindow(hoveredHwnd, SW_SHOW);
                }

                SetWindowPos(hoveredHwnd, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

                SetForegroundWindow(hoveredHwnd);
            }

            showOverlay = false;
            ShowWindow(g_renderer->GetHwnd(), SW_HIDE);
        }

        wasShiftPressed = shiftDown;
    }
}
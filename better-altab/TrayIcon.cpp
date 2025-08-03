#include "TrayIcon.h"
#include <shellapi.h>
#include "Renderer.h"

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_AUTOSTART 1002
#define ID_TRAY_APPEARANCE_ACCENT 1003
#define ID_TRAY_APPEARANCE_DARK 1004

extern Renderer* g_renderer;

namespace {
    NOTIFYICONDATA nid = {};
    HMENU hTrayMenu = nullptr;
    HWND hTrayWindow = nullptr;
    bool autoStartEnabled = false;
    bool accentTheme = false;

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

        if (enable) {
            char path[MAX_PATH];
            GetModuleFileNameA(NULL, path, MAX_PATH);
            RegSetValueExA(hKey, "BetterAltTab", 0, REG_SZ, (BYTE*)path, (DWORD)strlen(path));
        }
        else {
            RegDeleteValueA(hKey, "BetterAltTab");
        }

        RegCloseKey(hKey);
        autoStartEnabled = enable;
    }

    bool IsAccentThemeEnabled() 
    {
        HKEY hKey;
        DWORD value = 0, size = sizeof(value);
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\BetterAltTab", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExW(hKey, L"Theme", nullptr, nullptr, (LPBYTE)&value, &size);
            RegCloseKey(hKey);
        }
        return value == 1;
    }

    void SetAccentTheme(bool enable) 
    {
        HKEY hKey;
        RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\BetterAltTab", 0, nullptr, 0,
            KEY_WRITE, nullptr, &hKey, nullptr);
        DWORD value = enable ? 1 : 0;
        RegSetValueExW(hKey, L"Theme", 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
        RegCloseKey(hKey);
        accentTheme = enable;
        if (g_renderer) g_renderer->ApplyTheme(enable ? Theme::Accent : Theme::Dark);
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

                HMENU hAppearance = CreatePopupMenu();
                AppendMenu(hAppearance, MF_STRING | (accentTheme ? MF_CHECKED : 0), ID_TRAY_APPEARANCE_ACCENT, L"Accent");
                AppendMenu(hAppearance, MF_STRING | (!accentTheme ? MF_CHECKED : 0), ID_TRAY_APPEARANCE_DARK, L"Default");
                AppendMenu(hTrayMenu, MF_POPUP, (UINT_PTR)hAppearance, L"Appearance");
                //
                AppendMenu(hTrayMenu, MF_SEPARATOR, 0, nullptr);

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
            case ID_TRAY_APPEARANCE_ACCENT:
                SetAccentTheme(true);
                break;
            case ID_TRAY_APPEARANCE_DARK:
                SetAccentTheme(false);
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
}

namespace TrayIcon {

    bool Init(HINSTANCE hInstance) 
    {
        autoStartEnabled = IsAutoStartEnabled();
        accentTheme = IsAccentThemeEnabled();
        if (g_renderer) g_renderer->ApplyTheme(accentTheme ? Theme::Accent : Theme::Dark);

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
        return true;
    }

    void Shutdown() 
    {
        Shell_NotifyIcon(NIM_DELETE, &nid);
        if (hTrayWindow) DestroyWindow(hTrayWindow);
        if (hTrayMenu) DestroyMenu(hTrayMenu);
    }

}

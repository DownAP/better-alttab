#include "WindowManager.h"
#include "IconUtils.h"

static ID3D11Device* g_device = nullptr;
static std::vector<WindowEntry>* g_list = nullptr;

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM)
{
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER)) return TRUE;
    if (GetWindowLong(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    if (GetWindowTextLengthW(hwnd) == 0) return TRUE;

    wchar_t title[256];
    GetWindowTextW(hwnd, title, 256);
    HICON icon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL2, 0);
    if (!icon) icon = (HICON)GetClassLongPtr(hwnd, GCLP_HICONSM);

    WindowEntry entry{ hwnd, title };
    if (icon)
        entry.icon = IconUtils::CreateTextureFromIcon(g_device, icon);

    g_list->push_back(entry);
    return TRUE;
}

void WindowManager::RefreshWindows(ID3D11Device* device)
{
    windows.clear();
    g_device = device;
    g_list = &windows;
    EnumWindows(EnumProc, 0);
}

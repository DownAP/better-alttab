#pragma once
#include <vector>
#include <string>
#include <Windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

struct WindowEntry {
    HWND hwnd;
    std::wstring title;
    ID3D11ShaderResourceView* icon = nullptr;
};

class WindowManager {
public:
    std::vector<WindowEntry> windows;
    void RefreshWindows(ID3D11Device* device);
};

#pragma once
#include <Windows.h>
#include <d3d11.h>

enum class Theme {
    Accent,
    Dark
};

class Renderer {
public:
    bool Init(HINSTANCE hInstance);
    bool BeginFrame();
    void EndFrame();
    void Shutdown();
    ID3D11Device* GetDevice() const { return g_pd3dDevice; }
    HWND GetHwnd() const { return hwnd; }
    void Resize(int width, int height);
    void ApplyTheme(Theme theme);

private:
    HWND hwnd = nullptr;
    ID3D11Device* g_pd3dDevice = nullptr;
    ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
    IDXGISwapChain* g_pSwapChain = nullptr;
    ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
    Theme currentTheme = Theme::Dark;

    bool CreateDeviceD3D(HWND hWnd);
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();
};

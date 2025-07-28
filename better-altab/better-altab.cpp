#include <Windows.h>
#include "Renderer.h"
#include "WindowManager.h"
#include "AppManager.h"
#include "TrayIcon.h"
#include "Overlay.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    Renderer renderer;
    if (!renderer.Init(hInstance)) return -1;

    AppManager::LoadAllApps(renderer.GetDevice());
    TrayIcon::Init(hInstance);

    WindowManager wm;
    wm.RefreshWindows(renderer.GetDevice());

    Overlay::Init(&renderer, &wm);

    while (renderer.BeginFrame())
    {
        Overlay::ProcessFrame();
        renderer.EndFrame();
    }

    Overlay::Shutdown();
    TrayIcon::Shutdown();
    renderer.Shutdown();
    return 0;
}

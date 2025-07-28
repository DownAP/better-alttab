#pragma once
#include <Windows.h>
#include <atomic>
#include "Renderer.h"
#include "WindowManager.h"
#include "AppManager.h"

namespace Overlay {
    void Init(Renderer* renderer, WindowManager* wm);
    void Shutdown();
    void ProcessFrame();
}

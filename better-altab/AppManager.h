#pragma once
#include <vector>
#include <string>
#include <Windows.h>
#include <d3d11.h>

struct AppEntry {
    std::wstring name;
    std::wstring target;
    ID3D11ShaderResourceView* icon = nullptr;
};

namespace AppManager {
    void LoadAllApps(ID3D11Device* device);
    const std::vector<AppEntry>& GetAllApps();
    bool icontains(const std::wstring& hay, const std::wstring& needle);
}

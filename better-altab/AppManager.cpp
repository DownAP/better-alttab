#include "AppManager.h"
#include "IconUtils.h"
#include <filesystem>
#include <algorithm>
#include <vector>
#include <shellapi.h>

namespace {
    std::vector<AppEntry> g_allApps;
    bool g_appsLoaded = false;

    std::vector<std::wstring> EnumerateFiles(const std::wstring& root) {
        std::vector<std::wstring> out;
        try {
            for (auto const& entry : std::filesystem::recursive_directory_iterator(root)) {
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension().wstring();
                if (_wcsicmp(ext.c_str(), L".lnk") == 0)
                    out.push_back(entry.path().wstring());
            }
        }
        catch (std::exception&) {
            // skip
        }
        return out;
    }

    std::wstring GetShortcutDisplayName(const std::wstring& lnkPath) {
        std::filesystem::path p(lnkPath);
        return p.stem().native();
    }

    std::wstring ResolveShortcutTarget(const std::wstring& lnkPath) {
        return lnkPath;
    }

    

    std::wstring GetEnvVar(const wchar_t* name) {
        size_t requiredSize = 0;
        errno_t err = _wgetenv_s(&requiredSize, nullptr, 0, name);
        if (err != 0 || requiredSize == 0) return L"";
        std::vector<wchar_t> buf(requiredSize);
        err = _wgetenv_s(&requiredSize, buf.data(), buf.size(), name);
        if (err != 0) return L"";
        return std::wstring(buf.data());
    }
}

namespace AppManager {

    void LoadAllApps(ID3D11Device* device) {
        if (g_appsLoaded) return;
        g_appsLoaded = true;

        std::wstring appData = GetEnvVar(L"APPDATA");
        std::wstring progData = GetEnvVar(L"PROGRAMDATA");
        std::vector<std::wstring> roots;
        if (!appData.empty())  roots.push_back(appData + L"\\Microsoft\\Windows\\Start Menu\\Programs");
        if (!progData.empty()) roots.push_back(progData + L"\\Microsoft\\Windows\\Start Menu\\Programs");

        for (auto& root : roots) {
            for (auto& lnk : EnumerateFiles(root)) {
                AppEntry e;
                e.name = GetShortcutDisplayName(lnk);
                e.target = ResolveShortcutTarget(lnk);

                SHFILEINFOW sfi = {};
                if (SHGetFileInfoW(e.target.c_str(), FILE_ATTRIBUTE_NORMAL,
                    &sfi, sizeof(sfi),
                    SHGFI_ICON | SHGFI_SMALLICON))
                {
                    e.icon = IconUtils::CreateTextureFromIcon(device, sfi.hIcon);
                    DestroyIcon(sfi.hIcon);
                }
                g_allApps.push_back(e);
            }
        }
    }

    const std::vector<AppEntry>& GetAllApps() {
        return g_allApps;
    }

    bool icontains(const std::wstring& hay, const std::wstring& needle) {
        auto lower = [](std::wstring s) {
            std::transform(s.begin(), s.end(), s.begin(), ::towlower);
            return s;
            };
        return lower(hay).find(lower(needle)) != std::wstring::npos;
    }

}

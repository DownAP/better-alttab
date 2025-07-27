#pragma once
#include <Windows.h>
#include <d3d11.h>

namespace IconUtils {
    ID3D11ShaderResourceView* CreateTextureFromIcon(ID3D11Device* device, HICON icon);
}

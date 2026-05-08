#pragma once
#include "pch.h"
#include <imgui.h>

namespace MiniCAD
{
    class ImGuiLayer
    {
    public:
        bool Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
        void Shutdown();

        void Begin();
        void End();

    private:
        HWND                  m_hwnd = nullptr;
        ID3D11Device*         m_device = nullptr;
        ID3D11DeviceContext*  m_context = nullptr; 
    };
}
#pragma once
#include <vector>
#include <DirectXMath.h>
#include "Render/D3D11/Shader.h"  // Vertex_P3_C4
#include "Editor/Context/ViewState.h"

namespace MiniCAD
{
    class Cursor
    {
    public:
        std::vector<Vertex_P3_C4> BuildCursor(const ViewState  state, float width, float height)
        {
            std::vector<Vertex_P3_C4> verts; 

            float half = 5.0f;
            float x = state.MouseX;
            float y = state.MouseY;

            verts.push_back({ XMFLOAT3(0,          y, 0), XMFLOAT4(1,1,1,1) });
            verts.push_back({ XMFLOAT3(width,      y, 0), XMFLOAT4(1,1,1,1) });
            verts.push_back({ XMFLOAT3(x,     height, 0), XMFLOAT4(1,1,1,1) });
            verts.push_back({ XMFLOAT3(x,          0, 0), XMFLOAT4(1,1,1,1) });
             
            if (!state.Selection.Active&& state.ShowCurrorBox)   // 正在框选,不渲染中间的方框
            {
                verts.push_back({ XMFLOAT3(x - half, y - half, 0), XMFLOAT4(1,1,1,1) });
                verts.push_back({ XMFLOAT3(x + half, y - half, 0), XMFLOAT4(1,1,1,1) });
                verts.push_back({ XMFLOAT3(x + half, y - half, 0), XMFLOAT4(1,1,1,1) });
                verts.push_back({ XMFLOAT3(x + half, y + half, 0), XMFLOAT4(1,1,1,1) });
                verts.push_back({ XMFLOAT3(x + half, y + half, 0), XMFLOAT4(1,1,1,1) });
                verts.push_back({ XMFLOAT3(x - half, y + half, 0), XMFLOAT4(1,1,1,1) });
                verts.push_back({ XMFLOAT3(x - half, y + half, 0), XMFLOAT4(1,1,1,1) });
                verts.push_back({ XMFLOAT3(x - half, y - half, 0), XMFLOAT4(1,1,1,1) });

            } 

            return verts;
        }

    };
}
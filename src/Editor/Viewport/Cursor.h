#pragma once
#include <vector> 
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
            float x    = state.MouseX;
            float y    = state.MouseY;

            verts.push_back({ { 0.f,        y,  0.f }, {1.f,1.f,1.f,1.f} });
            verts.push_back({ { width,      y,  0.f }, {1.f,1.f,1.f,1.f} });
            verts.push_back({ { x,     height,  0.f }, {1.f,1.f,1.f,1.f} });
            verts.push_back({ { x,          0,  0.f }, {1.f,1.f,1.f,1.f} });
             
            if (!state.Selection.Active&& state.ShowCurrorBox)   // 正在框选,不渲染中间的方框
            {
                verts.push_back({ { x - half, y - half, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x + half, y - half, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x + half, y - half, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x + half, y + half, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x + half, y + half, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x - half, y + half, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x - half, y + half, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x - half, y - half, 0.f }, {1.f,1.f,1.f,1.f} }); 
            } 

            return verts;
        }

    };
}
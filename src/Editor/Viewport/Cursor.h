#pragma once
#include <vector> 
#include "Render/D3D11/Shader.h"  // Vertex_P3_C4
#include "Editor/Context/ViewState.h"
namespace MiniCAD
{
    class Cursor
    {
    public:
        /// <summary>
		/// 构建光标的顶点数据, 默认十字线长度为 60 像素，中心方框边长为 12 像素
        /// </summary> 
        std::vector<Vertex_P3_C4> BuildCursor(const ViewState state, float width, float height,float half = 6.0f, float crossSize = 60.f)
        {
            std::vector<Vertex_P3_C4> verts; 
            float x    = state.MouseX;
            float y    = state.MouseY;

            // 计算十字线的实际端点
            float left   = (crossSize > 0.f) ? std::max(0.f,    x - crossSize) : 0.f;
            float right  = (crossSize > 0.f) ? std::min(width,  x + crossSize) : width;
            float top    = (crossSize > 0.f) ? std::max(0.f,    y - crossSize) : 0.f;
            float bottom = (crossSize > 0.f) ? std::min(height, y + crossSize) : height;

            if (!state.Selection.Active && state.ShowCurrorBox)  // 正在框选，不渲染中间的方框
            {
                // 十字线：以方框边缘为端点，向四边延伸（中间留出方框空隙）
                verts.push_back({ { left,         y,        0.f }, {1.f,1.f,1.f,1.f} });  // 左
                verts.push_back({ { x - half,     y,        0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x + half,     y,        0.f }, {1.f,1.f,1.f,1.f} });  // 右
                verts.push_back({ { right,        y,        0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x,            top,      0.f }, {1.f,1.f,1.f,1.f} });  // 上
                verts.push_back({ { x,            y - half, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x,            y + half, 0.f }, {1.f,1.f,1.f,1.f} });  // 下
                verts.push_back({ { x,            bottom,   0.f }, {1.f,1.f,1.f,1.f} });
                // 中间方框：以鼠标为中心，边长 12 像素
                verts.push_back({ { x - half, y - half, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x + half, y - half, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x + half, y - half, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x + half, y + half, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x + half, y + half, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x - half, y + half, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x - half, y + half, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x - half, y - half, 0.f }, {1.f,1.f,1.f,1.f} });
            }
            else
            {
                verts.push_back({ { left,  y,      0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { right, y,      0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x,     top,    0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x,     bottom, 0.f }, {1.f,1.f,1.f,1.f} });
            }
            return verts;
        }
    };
}
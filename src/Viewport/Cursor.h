#pragma once
#include <vector>
#include <algorithm>
#include "Render/VertexTypes.hpp"
#include "Viewport/ViewState.h"
namespace MiniCAD
{
    class Cursor
    {
    public:
        // 十字线每边的长度（像素）：CursorSize 是整条线占视口长边的百分比，≥ 100 时铺满视口（返回负数）
        static float CrossArm(float cursorSize, float width, float height)
        {
            if (cursorSize >= 100.0f)
                return -1.0f;
            return std::max(cursorSize, 0.0f) * 0.01f * std::max(width, height) * 0.5f;
        }

        /// <summary>
		/// 构建光标的顶点数据：十字线长度按 state.CursorSize，中心方框半边长按 state.PickBox
        /// </summary>
        std::vector<Vertex_P3_C4> BuildCursor(const ViewState state, float width, float height)
        {
            std::vector<Vertex_P3_C4> verts;
            const float x    = state.MouseX;
            const float y    = state.MouseY;
            const bool  box  = !state.Selection.Active && state.ShowCurrorBox && state.PickBox > 0.0f;   // 正在框选，不渲染中间的方框
            const float half = box ? state.PickBox : 0.0f;
            float arm = CrossArm(state.CursorSize, width, height);
            if (!box && arm >= 0.0f)
                arm = std::max(arm, 5.0f);      // 没有方框时（画图命令里）至少留个小十字，光标不会消失

            const float left   = arm < 0.f ? 0.f    : std::max(0.f,    x - arm);
            const float right  = arm < 0.f ? width  : std::min(width,  x + arm);
            const float top    = arm < 0.f ? 0.f    : std::max(0.f,    y - arm);
            const float bottom = arm < 0.f ? height : std::min(height, y + arm);
            auto add = [&](float x0, float y0, float x1, float y1)
            {
                verts.push_back({ { x0, y0, 0.f }, {1.f,1.f,1.f,1.f} });
                verts.push_back({ { x1, y1, 0.f }, {1.f,1.f,1.f,1.f} });
            };

            // 十字线：以方框边缘为端点，向四边延伸（中间留出方框空隙）；短于方框时不画
            if (arm < 0.f || arm > half)
            {
                if (left < x - half)   add(left, y, x - half, y);
                if (right > x + half)  add(x + half, y, right, y);
                if (top < y - half)    add(x, top, x, y - half);
                if (bottom > y + half) add(x, y + half, x, bottom);
            }
            if (box)
            {
                add(x - half, y - half, x + half, y - half);
                add(x + half, y - half, x + half, y + half);
                add(x + half, y + half, x - half, y + half);
                add(x - half, y + half, x - half, y - half);
            }
            return verts;
        }
    };
}

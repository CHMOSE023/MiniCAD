#pragma once
#include "Core/UIContext.h"
#include "Paint/DrawList.h"
#include "Render/IRenderBackend.h"
#include <algorithm>
#include <vector>

namespace MiniGUI::Test
{
    // 空渲染后端：只分配纹理 ID，不做任何绘制，用于在没有 GPU 的环境下驱动 UIContext
    class NullBackend : public IRenderBackend
    {
    public:
        TextureId CreateTexture(int, int, TextureFormat, const void*) override { return m_next++; }
        void      UpdateTexture(TextureId, const RectI&, const void*, int) override {}
        void      DestroyTexture(TextureId) override {}
        void      Render(const DrawData& data) override
        {
            ++renderCount;
            lastVertexCount = 0;
            lastColors.clear();
            for (const DrawList* l : data.lists)
            {
                lastVertexCount += l->GetVertices().size();
                for (const DrawVert& v : l->GetVertices())
                    lastColors.push_back(v.color);
            }
        }

        int    renderCount     = 0;
        size_t lastVertexCount = 0;     // 最近一帧的顶点数（验证列表只绘制可见行）
        std::vector<Color32> lastColors; // 最近一帧所有顶点的颜色（验证主题切换）

        bool Drew(Color32 c) const { return std::find(lastColors.begin(), lastColors.end(), c) != lastColors.end(); }

    private:
        TextureId m_next = 1;
    };

    // 测试夹具：一个 UIContext + 指定尺寸的显示区域
    struct TestUI
    {
        NullBackend backend;
        UIContext   ui{ &backend };
        uint32_t    now  = 100000;     // 假时钟（毫秒），测试里手动推进
        uint32_t    tick = 0;          // 连击判定用的消息时间

        explicit TestUI(Vec2 size, float scale = 1.0f)
        {
            ui.SetClock([this] { return now; });
            ui.SetDisplaySize(size, scale);
        }

        template<typename T = Node, typename... Args>
        T* Add(Args&&... args) { return ui.GetRoot()->AddChild<T>(std::forward<Args>(args)...); }

        void Layout() { ui.Update(); }

        // 推进时间并执行到期的定时器
        void Advance(uint32_t ms) { now += ms; ui.Tick(); }

        // 一次完整的单击（每次间隔足够长，不会被判定为双击）
        void Click(Vec2 p, MouseButton b = MouseButton::Left, uint8_t mods = 0)
        {
            tick += 5000;
            ui.PointerMove(p, mods);
            ui.PointerDown(p, b, mods, tick);
            ui.PointerUp(p, b, mods);
            ui.Update();
        }

        void DoubleClick(Vec2 p)
        {
            tick += 5000;
            ui.PointerMove(p, 0);
            ui.PointerDown(p, MouseButton::Left, 0, tick);
            ui.PointerUp(p, MouseButton::Left, 0);
            ui.PointerDown(p, MouseButton::Left, 0, tick + 100);
            ui.PointerUp(p, MouseButton::Left, 0);
            ui.Update();
        }

        void Key(MiniGUI::Key k, uint8_t mods = 0) { ui.KeyDown(k, mods); ui.KeyUp(k, mods); ui.Update(); }
    };

    inline Vec2 CenterOf(const Node* n) { return n->GetScreenBounds().Center(); }

    inline LayoutStyle Fixed(float w, float h)
    {
        LayoutStyle s;
        s.width  = w;
        s.height = h;
        return s;
    }
}

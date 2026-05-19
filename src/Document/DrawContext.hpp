#pragma once
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Color4.hpp"
#include "Render/VertexTypes.hpp"
#include "Editor/Overlay/Overlay.h"
#include <imgui.h>
#include <vector>
#include <string>
#include <cmath>

namespace MiniCAD
{
    class DrawContext : public IDrawSink
    {
    public:
        using ObjectID = uint64_t;

        DrawContext(std::vector<Vertex_P3_C4>&    sceneVertices,
                    std::vector<Vertex_P3_C4_UV>& textVertices,
                    Overlay&                       overlay)
            : m_verts(sceneVertices)
            , m_textVerts(textVertices)
            , m_overlay(overlay)
        {
        }

        void DrawLine(const Math::Point3& a, const Math::Point3& b,
                      const Math::Color4& color, bool isOverlay) override
        {
            if (isOverlay)
            {
                m_overlay.AddLine(a, b, color);
            }
            else
            {
                m_verts.push_back(ToVertex(a, color));
                m_verts.push_back(ToVertex(b, color));
            }
        }

        void EmitText(const Math::Point3& pos, const std::string& utf8Text,
                      float height, float rotation,
                      const Math::Color4& color) override
        {
            if (utf8Text.empty()) return;

            // 用 128px 的 baked font：DX11 backend 支持 RendererHasTextures，字形按需加载。
            // 从大尺寸向下采样，比从 16px UI 字体向上拉伸清晰得多。
            constexpr float kBakeSize = 128.f;
            ImFontBaked* font = ImGui::GetFont()->GetFontBaked(kBakeSize);
            if (!font || font->Size <= 0.f) return;

            const float scale = height / font->Size;
            const float cosR  = cosf(rotation);
            const float sinR  = sinf(rotation);

            Math::Float4 c4 = {
                static_cast<float>(color.r),
                static_cast<float>(color.g),
                static_cast<float>(color.b),
                static_cast<float>(color.a)
            };

            float curX = 0.f;
            const char* p   = utf8Text.c_str();
            const char* end = p + utf8Text.size();

            while (p < end)
            {
                unsigned int cp;
                p = DecodeUtf8(p, end, cp);

                const ImFontGlyph* g = font->FindGlyph(static_cast<ImWchar>(cp));
                if (!g) { curX += font->FallbackAdvanceX * scale; continue; }

                // 字形四角（局部坐标，Y-down ImGui 空间）
                float lx0 = curX + g->X0 * scale;
                float ly0 =        g->Y0 * scale;
                float lx1 = curX + g->X1 * scale;
                float ly1 =        g->Y1 * scale;

                // 旋转+翻转Y到世界坐标（Y-up）
                auto xform = [&](float lx, float ly) -> Math::Float3
                {
                    float wx =  lx;
                    float wy = -ly; // 翻转 Y：屏幕向下 → 世界向上
                    return {
                        static_cast<float>(pos.x) + wx * cosR - wy * sinR,
                        static_cast<float>(pos.y) + wx * sinR + wy * cosR,
                        static_cast<float>(pos.z)
                    };
                };

                Math::Float3 tl = xform(lx0, ly0); // 左上
                Math::Float3 tr = xform(lx1, ly0); // 右上
                Math::Float3 br = xform(lx1, ly1); // 右下
                Math::Float3 bl = xform(lx0, ly1); // 左下

                // triangle 1: tl → tr → br
                m_textVerts.push_back({ tl, c4, { g->U0, g->V0 } });
                m_textVerts.push_back({ tr, c4, { g->U1, g->V0 } });
                m_textVerts.push_back({ br, c4, { g->U1, g->V1 } });
                // triangle 2: tl → br → bl
                m_textVerts.push_back({ tl, c4, { g->U0, g->V0 } });
                m_textVerts.push_back({ br, c4, { g->U1, g->V1 } });
                m_textVerts.push_back({ bl, c4, { g->U0, g->V1 } });

                curX += g->AdvanceX * scale;
            }
        }

    private:
        // UTF-8 解码，返回下一个字符位置，写入 codepoint
        static const char* DecodeUtf8(const char* p, const char* end, unsigned int& cp)
        {
            auto u = [](const char c) { return static_cast<unsigned char>(c); };
            unsigned char c0 = u(*p);

            if (c0 < 0x80) { cp = c0; return p + 1; }
            if (c0 < 0xC0) { cp = 0xFFFD; return p + 1; }

            if (c0 < 0xE0 && p + 1 < end)
            {
                cp = ((c0 & 0x1F) << 6) | (u(p[1]) & 0x3F);
                return p + 2;
            }
            if (c0 < 0xF0 && p + 2 < end)
            {
                cp = ((c0 & 0x0F) << 12) | ((u(p[1]) & 0x3F) << 6) | (u(p[2]) & 0x3F);
                return p + 3;
            }
            if (p + 3 < end)
            {
                cp = ((c0 & 0x07) << 18) | ((u(p[1]) & 0x3F) << 12) | ((u(p[2]) & 0x3F) << 6) | (u(p[3]) & 0x3F);
                return p + 4;
            }
            cp = 0xFFFD;
            return p + 1;
        }

        Vertex_P3_C4 ToVertex(const Math::Point3& pt, const Math::Color4& color) const
        {
            return {
                { static_cast<float>(pt.x), static_cast<float>(pt.y), static_cast<float>(pt.z) },
                { static_cast<float>(color.r), static_cast<float>(color.g),
                  static_cast<float>(color.b), static_cast<float>(color.a) }
            };
        }

        std::vector<Vertex_P3_C4>&    m_verts;
        std::vector<Vertex_P3_C4_UV>& m_textVerts;
        Overlay&                       m_overlay;
    };
}

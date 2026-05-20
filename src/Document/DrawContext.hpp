#pragma once
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Color4.hpp"
#include "Render/VertexTypes.hpp"
#include "Editor/Overlay/Overlay.h"
#include "Text/Font/IFont.h"
#include "Text/Layout/TextLayoutEngine.h"
#include <vector>
#include <string>
#include <functional>
#include <cmath>

namespace MiniCAD
{
    // 字形数据（归一化到字高=1的坐标系，供纹理路径使用）
    struct GlyphInfo
    {
        float X0, Y0, X1, Y1; // 字形边界（单位：字高）
        float U0, V0, U1, V1; // 纹理 UV
        float AdvanceX;        // 水平步进（单位：字高）
    };

    // 纹理字形查询回调（ImGui / WebFontAtlas 路径）
    using GlyphProvider = std::function<bool(uint32_t cp, GlyphInfo& out, float& fallbackAdvance)>;

    // 矢量字体解析回调：styleId → IFont*（由 DocumentManager 注入，找不到返回 nullptr）
    using FontResolver = std::function<IFont*(uint32_t styleId)>;

    class DrawContext : public IDrawSink
    {
    public:
        DrawContext(std::vector<Vertex_P3_C4>&    sceneVertices,
                    std::vector<Vertex_P3_C4_UV>& textVertices,
                    Overlay&                       overlay,
                    GlyphProvider                  glyphProvider = nullptr,
                    FontResolver                   fontResolver  = nullptr)
            : m_verts(sceneVertices)
            , m_textVerts(textVertices)
            , m_overlay(overlay)
            , m_glyphProvider(std::move(glyphProvider))
            , m_fontResolver(std::move(fontResolver))
        {
        }

        // --- 几何线段 ---

        void DrawLine(const Math::Point3& a, const Math::Point3& b,
                      const Math::Color4& color, bool isOverlay) override
        {
            if (isOverlay)
                m_overlay.AddLine(a, b, color);
            else
            {
                m_verts.push_back(ToVertex(a, color));
                m_verts.push_back(ToVertex(b, color));
            }
        }

        // --- 纹理文字（ImGui / WebFontAtlas 路径）---

        void EmitText(const Math::Point3& pos, const std::string& utf8Text,
                      float height, float rotation,
                      const Math::Color4& color) override
        {
            if (utf8Text.empty() || !m_glyphProvider) return;

            const float cosR = cosf(rotation);
            const float sinR = sinf(rotation);

            Math::Float4 c4 = {
                static_cast<float>(color.r), static_cast<float>(color.g),
                static_cast<float>(color.b), static_cast<float>(color.a)
            };

            float curX = 0.f;
            const char* p   = utf8Text.c_str();
            const char* end = p + utf8Text.size();

            while (p < end)
            {
                unsigned int cp;
                p = DecodeUtf8(p, end, cp);

                GlyphInfo g;
                float fallback = 0.f;
                if (!m_glyphProvider(cp, g, fallback)) { curX += fallback; continue; }

                float lx0 = (curX + g.X0) * height, ly0 = g.Y0 * height;
                float lx1 = (curX + g.X1) * height, ly1 = g.Y1 * height;

                auto xform = [&](float lx, float ly) -> Math::Float3 {
                    float wx =  lx, wy = -ly;
                    return {
                        static_cast<float>(pos.x) + wx * cosR - wy * sinR,
                        static_cast<float>(pos.y) + wx * sinR + wy * cosR,
                        static_cast<float>(pos.z)
                    };
                };

                Math::Float3 tl = xform(lx0, ly0), tr = xform(lx1, ly0);
                Math::Float3 br = xform(lx1, ly1), bl = xform(lx0, ly1);

                m_textVerts.push_back({ tl, c4, { g.U0, g.V0 } });
                m_textVerts.push_back({ tr, c4, { g.U1, g.V0 } });
                m_textVerts.push_back({ br, c4, { g.U1, g.V1 } });
                m_textVerts.push_back({ tl, c4, { g.U0, g.V0 } });
                m_textVerts.push_back({ br, c4, { g.U1, g.V1 } });
                m_textVerts.push_back({ bl, c4, { g.U0, g.V1 } });

                curX += g.AdvanceX;
            }
        }

        // --- 矢量多行文字（SHX / TTF 路径）---
        // 字形线段直接输出到 m_verts（场景几何），不走纹理。

        void EmitMText(const Math::Point3& pos,
                       const std::string&  utf8Text,
                       uint32_t            styleId,
                       double              height,
                       double              rotation,
                       double              boxWidth,
                       const Math::Color4& color) override
        {
            if (utf8Text.empty() || !m_fontResolver) return;

            IFont* font = m_fontResolver(styleId);
            if (!font) return;

            TextLayoutEngine layout;
            auto result = layout.Layout(utf8Text, font, height, 1.0, rotation, boxWidth);

            const double cosR = std::cos(rotation);
            const double sinR = std::sin(rotation);

            Math::Float4 c4 = 
            {
                static_cast<float>(color.r), static_cast<float>(color.g),
                static_cast<float>(color.b), static_cast<float>(color.a)
            };

            for (const auto& inst : result.m_glyphs)
            {
                auto xform = [&](const Math::Point3& lp) -> Math::Float3
                {
                    double x = lp.x * inst.m_scale + inst.m_position.x;
                    double y = lp.y * inst.m_scale + inst.m_position.y;
                    return {
                        static_cast<float>(pos.x + x * cosR - y * sinR),
                        static_cast<float>(pos.y + x * sinR + y * cosR),
                        static_cast<float>(pos.z)
                    };
                };

                for (const auto& seg : inst.m_glyph.Lines)
                {
                    m_verts.push_back({ xform(seg.Start), c4 });
                    m_verts.push_back({ xform(seg.End),   c4 });
                }
            }
        }

    private:
        static const char* DecodeUtf8(const char* p, const char* end, unsigned int& cp)
        {
            auto u = [](const char c) { return static_cast<unsigned char>(c); };
            unsigned char c0 = u(*p);
            if (c0 < 0x80) { cp = c0; return p + 1; }
            if (c0 < 0xC0) { cp = 0xFFFD; return p + 1; }
            if (c0 < 0xE0 && p + 1 < end) { cp = ((c0 & 0x1F) << 6)  | (u(p[1]) & 0x3F); return p + 2; }
            if (c0 < 0xF0 && p + 2 < end) { cp = ((c0 & 0x0F) << 12) | ((u(p[1]) & 0x3F) << 6) | (u(p[2]) & 0x3F); return p + 3; }
            if (p + 3 < end) { cp = ((c0 & 0x07) << 18) | ((u(p[1]) & 0x3F) << 12) | ((u(p[2]) & 0x3F) << 6) | (u(p[3]) & 0x3F); return p + 4; }
            cp = 0xFFFD; return p + 1;
        }

        Vertex_P3_C4 ToVertex(const Math::Point3& pt, const Math::Color4& col) const
        {
            return {
                { static_cast<float>(pt.x), static_cast<float>(pt.y), static_cast<float>(pt.z) },
                { static_cast<float>(col.r), static_cast<float>(col.g),
                  static_cast<float>(col.b), static_cast<float>(col.a) }
            };
        }

        std::vector<Vertex_P3_C4>&     m_verts;
        std::vector<Vertex_P3_C4_UV>&  m_textVerts;
        Overlay&                       m_overlay;
        GlyphProvider                  m_glyphProvider;
        FontResolver                   m_fontResolver;
    };
}

#pragma once
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Color4.hpp"
#include "Render/VertexTypes.hpp"
#include "Editor/Overlay/Overlay.h"
#include <vector>
#include <string>
#include <functional>
#include <cmath>

namespace MiniCAD
{
    // 字形数据（归一化到字高=1的坐标系）
    struct GlyphInfo
    {
        float X0, Y0, X1, Y1; // 字形边界（单位：字高）
        float U0, V0, U1, V1; // 纹理 UV
        float AdvanceX;        // 水平步进（单位：字高）
    };

    // 字形查询回调：cp → GlyphInfo；找不到时写入 fallbackAdvance 并返回 false
    // Web 端传 nullptr，EmitText 自动跳过
    using GlyphProvider = std::function<bool(uint32_t cp, GlyphInfo& out, float& fallbackAdvance)>;

    class DrawContext : public IDrawSink
    {
    public:
        using ObjectID = uint64_t;

        DrawContext(std::vector<Vertex_P3_C4>&    sceneVertices,
                    std::vector<Vertex_P3_C4_UV>& textVertices,
                    Overlay&                       overlay,
                    GlyphProvider                  glyphProvider = nullptr)
            : m_verts(sceneVertices)
            , m_textVerts(textVertices)
            , m_overlay(overlay)
            , m_glyphProvider(std::move(glyphProvider))
        {
        }

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

        void EmitText(const Math::Point3& pos, const std::string& utf8Text,
                      float height, float rotation,
                      const Math::Color4& color) override
        {
            if (utf8Text.empty() || !m_glyphProvider) return;

            const float cosR = cosf(rotation);
            const float sinR = sinf(rotation);

            Math::Float4 c4 = {
                static_cast<float>(color.r),
                static_cast<float>(color.g),
                static_cast<float>(color.b),
                static_cast<float>(color.a)
            };

            float curX = 0.f; // 归一化光标（单位：字高）
            const char* p   = utf8Text.c_str();
            const char* end = p + utf8Text.size();

            while (p < end)
            {
                unsigned int cp;
                p = DecodeUtf8(p, end, cp);

                GlyphInfo g;
                float fallback = 0.f;
                if (!m_glyphProvider(cp, g, fallback))
                {
                    curX += fallback;
                    continue;
                }

                // 字形四角（世界坐标，Y-up）
                float lx0 = (curX + g.X0) * height;
                float ly0 =         g.Y0  * height;
                float lx1 = (curX + g.X1) * height;
                float ly1 =         g.Y1  * height;

                auto xform = [&](float lx, float ly) -> Math::Float3
                {
                    float wx =  lx;
                    float wy = -ly; // 翻转 Y：ImGui Y-down → 世界 Y-up
                    return {
                        static_cast<float>(pos.x) + wx * cosR - wy * sinR,
                        static_cast<float>(pos.y) + wx * sinR + wy * cosR,
                        static_cast<float>(pos.z)
                    };
                };

                Math::Float3 tl = xform(lx0, ly0);
                Math::Float3 tr = xform(lx1, ly0);
                Math::Float3 br = xform(lx1, ly1);
                Math::Float3 bl = xform(lx0, ly1);

                m_textVerts.push_back({ tl, c4, { g.U0, g.V0 } });
                m_textVerts.push_back({ tr, c4, { g.U1, g.V0 } });
                m_textVerts.push_back({ br, c4, { g.U1, g.V1 } });
                m_textVerts.push_back({ tl, c4, { g.U0, g.V0 } });
                m_textVerts.push_back({ br, c4, { g.U1, g.V1 } });
                m_textVerts.push_back({ bl, c4, { g.U0, g.V1 } });

                curX += g.AdvanceX;
            }
        }

    private:
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

        Vertex_P3_C4 ToVertex(const Math::Point3& pt, const Math::Color4& col) const
        {
            return {
                { static_cast<float>(pt.x), static_cast<float>(pt.y), static_cast<float>(pt.z) },
                { static_cast<float>(col.r), static_cast<float>(col.g),
                  static_cast<float>(col.b), static_cast<float>(col.a) }
            };
        }

        std::vector<Vertex_P3_C4>&    m_verts;
        std::vector<Vertex_P3_C4_UV>& m_textVerts;
        Overlay&                       m_overlay;
        GlyphProvider                  m_glyphProvider;
    };
}

#include "WebRenderer.h"

namespace MiniCAD
{
    void WebRenderer::BeginFrame(IRenderTarget& /*target*/, const ViewportDesc& /*viewport*/)
    {
        m_screenLine.clear();
        m_screenTri.clear();
        m_worldLine.clear();
        m_worldTri.clear();
        m_text.clear();
        m_hasScreenVP = false;
        m_hasWorldVP  = false;
    }

    void WebRenderer::AppendLineVerts(std::vector<float>& buf, std::span<const Vertex_P3_C4> verts)
    {
        buf.reserve(buf.size() + verts.size() * 7);
        for (const auto& v : verts)
        {
            buf.push_back(v.pos.x);
            buf.push_back(v.pos.y);
            buf.push_back(v.pos.z);
            buf.push_back(v.color.x); // r
            buf.push_back(v.color.y); // g
            buf.push_back(v.color.z); // b
            buf.push_back(v.color.w); // a
        }
    }

    void WebRenderer::AppendTriVerts(std::vector<float>& buf, std::span<const Vertex_P3_C4> verts)
    {
        AppendLineVerts(buf, verts); // same layout
    }

    void WebRenderer::Submit(std::span<const Vertex_P3_C4> verts,
                             const Math::Mat4& viewProj,
                             PrimitiveType type,
                             bool /*depth*/, bool /*blend*/)
    {
        if (verts.empty()) return;

        if (!m_hasScreenVP)
        {
            StoreMat(m_screenVP, viewProj);
            m_hasScreenVP = true;
            // All screen-space geometry
            if (type == PrimitiveType::Triangle)
                AppendTriVerts(m_screenTri, verts);
            else
                AppendLineVerts(m_screenLine, verts);
            return;
        }

        bool isScreen = MatEqual(viewProj, m_screenVP);
        if (isScreen)
        {
            if (type == PrimitiveType::Triangle)
                AppendTriVerts(m_screenTri, verts);
            else
                AppendLineVerts(m_screenLine, verts);
        }
        else
        {
            if (!m_hasWorldVP)
            {
                StoreMat(m_worldVP, viewProj);
                m_hasWorldVP = true;
            }
            if (type == PrimitiveType::Triangle)
                AppendTriVerts(m_worldTri, verts);
            else
                AppendLineVerts(m_worldLine, verts);
        }
    }

    void WebRenderer::SubmitTextured(std::span<const Vertex_P3_C4_UV> verts,
                                     const Math::Mat4& viewProj,
                                     void* /*nativeSRV*/,
                                     bool /*depth*/, bool /*blend*/)
    {
        if (verts.empty()) return;

        if (!m_hasWorldVP)
        {
            StoreMat(m_worldVP, viewProj);
            m_hasWorldVP = true;
        }

        m_text.reserve(m_text.size() + verts.size() * 9);
        for (const auto& v : verts)
        {
            m_text.push_back(v.pos.x);
            m_text.push_back(v.pos.y);
            m_text.push_back(v.pos.z);
            m_text.push_back(v.color.x); // r
            m_text.push_back(v.color.y); // g
            m_text.push_back(v.color.z); // b
            m_text.push_back(v.color.w); // a
            m_text.push_back(v.uv.x);
            m_text.push_back(v.uv.y);
        }
    }
}

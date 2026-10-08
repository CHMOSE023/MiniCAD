#pragma once
#include "Render/IRenderer.h"
#include "Render/VertexTypes.hpp"
#include "Core/Math/Mat4.hpp"
#include <vector>
#include <cstring>

namespace MiniCAD
{
    // WebGL renderer stub: collects all Submit/SubmitTextured calls into flat buffers.
    // Viewport uses two distinct matrices per frame:
    //   screenVP  – OrthoOffCenterLH(0, w, h, 0) for grid/axis/grips/cursor (screen-space)
    //   worldVP   – Camera::GetViewProj()          for scene geometry (world-space)
    // Detection: first matrix encountered after BeginFrame = screenVP;
    //            first different matrix = worldVP.
    class WebRenderer : public IRenderer
    {
    public:
        WebRenderer() = default;

        void BeginFrame(IRenderTarget& target, const ViewportDesc& viewport) override;
        void EndFrame() override {}

        void Submit(std::span<const Vertex_P3_C4> verts,
                    const Math::Mat4& viewProj,
                    PrimitiveType type,
                    bool depth, bool blend) override;

        void SubmitTextured(std::span<const Vertex_P3_C4_UV> verts,
                            const Math::Mat4& viewProj,
                            void* nativeSRV,
                            bool depth, bool blend) override;

        void* GetNativeDevice() override { return nullptr; }

        // ── Read-back (called from C exports after Tick) ──────────────
        const float* ScreenLineData()  const { return m_screenLine.data(); }
        int          ScreenLineCount() const { return (int)m_screenLine.size() / 7; }

        const float* ScreenTriData()   const { return m_screenTri.data(); }
        int          ScreenTriCount()  const { return (int)m_screenTri.size() / 7; }

        const float* WorldLineData()   const { return m_worldLine.data(); }
        int          WorldLineCount()  const { return (int)m_worldLine.size() / 7; }

        const float* WorldTriData()    const { return m_worldTri.data(); }
        int          WorldTriCount()   const { return (int)m_worldTri.size() / 7; }

        const float* TextData()        const { return m_text.data(); }
        int          TextCount()       const { return (int)m_text.size() / 9; }

        const float* ScreenVP()        const { return m_screenVP; }
        const float* WorldVP()         const { return m_worldVP; }

        // Font texture WebGL ID (set by TS, returned during SubmitTextured)
        void SetFontTexId(int id) { m_fontTexId = id; }
        int  GetFontTexId()  const { return m_fontTexId; }

    private:
        static bool MatEqual(const Math::Mat4& a, const float b[16])
        {
            for (int i = 0; i < 16; ++i)
                if (static_cast<float>(a.m[i]) != b[i]) return false;
            return true;
        }

        static void StoreMat(float dst[16], const Math::Mat4& src)
        {
            for (int i = 0; i < 16; ++i)
                dst[i] = static_cast<float>(src.m[i]);
        }

        void AppendLineVerts(std::vector<float>& buf, std::span<const Vertex_P3_C4> verts);
        void AppendTriVerts (std::vector<float>& buf, std::span<const Vertex_P3_C4> verts);

        bool  m_hasScreenVP = false;
        bool  m_hasWorldVP  = false;
        float m_screenVP[16]{};
        float m_worldVP[16]{};

        std::vector<float> m_screenLine; // 7 floats/vert
        std::vector<float> m_screenTri;  // 7 floats/vert
        std::vector<float> m_worldLine;  // 7 floats/vert
        std::vector<float> m_worldTri;   // 7 floats/vert（线宽 > 1 的填充三角形）
        std::vector<float> m_text;       // 9 floats/vert

        int m_fontTexId = 0;
    };
}

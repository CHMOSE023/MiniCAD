// ============================================================
// MiniCAD — render/D3D11/DrawPrimitives.cpp
// 职责：DrawPrimitives 实现；D3D11 API 封装于此文件
// 依赖：render/D3D11/DrawPrimitives.h, render/D3D11/ShaderManager.h
// 约束：D3D11 API 严禁泄漏到头文件
// ============================================================

#include "render/D3D11/DrawPrimitives.h"
#include "render/D3D11/ShaderManager.h" 

#include <d3d11.h>
#include <cassert>

namespace MiniCAD {

    // ============================================================
    // 顶点结构（仅 .cpp 可见）
    // ============================================================
    namespace {

        struct GpuVertex 
        {
            float  x, y, z;
            float  r, g, b, a;
        };

        inline GpuVertex ToGpuVertex(const Point3& p, const Vec4& color) 
        {
            return {
                static_cast<float>(p.x),
                static_cast<float>(p.y),
                static_cast<float>(p.z),
                static_cast<float>(color.x),
                static_cast<float>(color.y),
                static_cast<float>(color.z),
                static_cast<float>(color.w),
            };
        }

    } // anonymous namespace

    // ============================================================
    // DrawPrimitives
    // ============================================================
    void DrawPrimitives::Draw(ID3D11Device* device, ID3D11DeviceContext* context, const std::vector<Point3>& vertices, RenderItem::Topology topology, const RenderState& state)
    {
        switch (topology)
        {
        case RenderItem::Topology::LineList:
            DrawLineList(device, context, vertices, state);
            break;
        case RenderItem::Topology::LineStrip:
            DrawLineStrip(device, context, vertices, state);
            break;
        case RenderItem::Topology::TriangleList:
            DrawTriangleList(device, context, vertices, state);
            break;
        default:
            assert(false && "DrawPrimitives: unknown topology");
            break;
        }
    }

    // ============================================================
    // 内部：上传顶点缓冲
    // ============================================================

    bool DrawPrimitives::UploadVertices(ID3D11Device* device,   ID3D11DeviceContext* context,  const std::vector<Point3>& vertices) 
    {
        // 此处创建动态顶点缓冲并立即绑定
        // 真实项目应使用环形缓冲池，此处 Phase 1 实现简单版本  

        if (vertices.empty()) return false;

        // Phase 1: 使用简单 Map/Unmap 动态缓冲
        // （ShaderManager 持有复用缓冲对象以减少分配）
        (void)device;
        (void)context;
        (void)vertices;
        // 具体实现由 ShaderManager 的顶点缓冲池提供
        return true;
    }

    void DrawPrimitives::DrawLineList(ID3D11Device* device, ID3D11DeviceContext* context, const std::vector<Point3>& vertices, const RenderState& state)
    {
        if (vertices.size() < 2) return;

        ShaderManager& sm = ShaderManager::Instance();
        sm.BindLineShader(context);

        // 构建 GPU 顶点数组
        std::vector<GpuVertex> gpuVerts;

        gpuVerts.reserve(vertices.size());
         
        gpuVerts.push_back(ToGpuVertex(Point3(-1000, -1000, 0), Vec4(1, 0, 0, 0)));
        gpuVerts.push_back(ToGpuVertex(Point3(1000, 1000, 0), Vec4(1, 0, 0, 0)));

        for (const auto& p : vertices) 
        {
            gpuVerts.push_back(ToGpuVertex(p, state.color));  // double→float 在此发生
        }


        sm.UploadAndDraw(device, context, gpuVerts.data(), static_cast<UINT>(gpuVerts.size()), sizeof(GpuVertex), D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    }

    void DrawPrimitives::DrawLineStrip(ID3D11Device* device,  ID3D11DeviceContext* context, const std::vector<Point3>& vertices,  const RenderState& state)
    {
        if (vertices.size() < 2) return;

        ShaderManager& sm = ShaderManager::Instance();
        sm.BindLineShader(context);

        std::vector<GpuVertex> gpuVerts;
        gpuVerts.reserve(vertices.size());

        for (const auto& p : vertices) 
        {
            gpuVerts.push_back(ToGpuVertex(p, state.color));  // double→float 在此发生
        } 

        sm.UploadAndDraw(device, context, gpuVerts.data(), static_cast<UINT>(gpuVerts.size()), sizeof(GpuVertex), D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    }

    void DrawPrimitives::DrawTriangleList(ID3D11Device* device, ID3D11DeviceContext* context,  const std::vector<Point3>& vertices,  const RenderState& state)
    {
        if (vertices.size() < 3) return;

        ShaderManager& sm = ShaderManager::Instance();
        sm.BindFillShader(context);

        std::vector<GpuVertex> gpuVerts;
        gpuVerts.reserve(vertices.size());

        for (const auto& p : vertices)
        {
            gpuVerts.push_back(ToGpuVertex(p, state.color));  // double→float 在此发生
        }

        sm.UploadAndDraw(device, context, gpuVerts.data(), static_cast<UINT>(gpuVerts.size()), sizeof(GpuVertex), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

} // namespace MiniCAD

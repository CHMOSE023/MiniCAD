// ============================================================
// MiniCAD — render/D3D11/DrawPrimitives.h
// 职责：基础图元绘制（点 / 线段 / 折线 / 填充多边形）
// 依赖：math/Point.h, render/D3D11/RenderState.h, render/D3D11/RenderQueue.h
// 约束：不直接调用 D3D11 API（通过内部封装），不依赖 core / app / ui
//       所有绘制调用必须经由 RenderQueue 提交后由 Renderer 触发
// ============================================================
#pragma once

#include "math/Point.hpp"
#include "render/D3D11/RenderState.h"
#include "render/D3D11/RenderQueue.h"
#include <vector>

// 前向声明，避免在头文件中拉入 d3d11.h
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace MiniCAD {

    // ============================================================
    // DrawPrimitives — 无状态工具类，所有方法均为静态
    // ============================================================
    class DrawPrimitives {
    public:
        DrawPrimitives() = delete;
        ~DrawPrimitives() = delete;

        // 统一入口：由 D3D11Renderer::DrawItem() 调用
        static void Draw(ID3D11Device* device,
            ID3D11DeviceContext* context,
            const std::vector<Point3>& vertices,
            RenderItem::Topology       topology,
            const RenderState& state);

        // --- 具体图元 ---

        // 绘制线段列表（LineList：每两点一条线）
        static void DrawLineList(ID3D11Device* device,
            ID3D11DeviceContext* context,
            const std::vector<Point3>& vertices,
            const RenderState& state);

        // 绘制折线（LineStrip：相邻点连线）
        static void DrawLineStrip(ID3D11Device* device,
            ID3D11DeviceContext* context,
            const std::vector<Point3>& vertices,
            const RenderState& state);

        // 绘制填充三角形列表
        static void DrawTriangleList(ID3D11Device* device,
            ID3D11DeviceContext* context,
            const std::vector<Point3>& vertices,
            const RenderState& state);

    private:
        // 内部：创建临时顶点缓冲并绑定
        static bool UploadVertices(ID3D11Device* device,
            ID3D11DeviceContext* context,
            const std::vector<Point3>& vertices);
    };

} // namespace MiniCAD

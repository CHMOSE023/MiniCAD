// ============================================================
// MiniCAD — render/D3D11/SelectionHighlight.cpp
// 职责：SelectionHighlight Pass 实现
// 依赖：render/D3D11/SelectionHighlight.h, render/D3D11/DrawPrimitives.h,
//       render/D3D11/ShaderManager.h
// 约束：D3D11 API 通过 DrawPrimitives / ShaderManager 间接调用
// ============================================================

#include "render/D3D11/SelectionHighlight.h"
#include "render/D3D11/DrawPrimitives.h"
#include "render/D3D11/ShaderManager.h"

#include <d3d11.h>
#include <utility>

namespace MiniCAD {

    SelectionHighlight::SelectionHighlight() {
        m_items.reserve(256);
    }

    void SelectionHighlight::BeginHighlightPass() {
        // 当前帧高亮列表由 app 层在帧开始前填充
        // 此处预留扩展点（如开启模板缓冲等）
    }

    void SelectionHighlight::SubmitHighlight(const std::vector<Point3>& vertices,
        RenderItem::Topology       topology) {
        m_items.push_back({ vertices, topology });
    }

    void SelectionHighlight::EndHighlightPass(ID3D11Device* device,
        ID3D11DeviceContext* context) {
        if (m_items.empty()) return;

        // 绑定高亮 Shader
        ShaderManager::Instance().BindHighlightShader(context);

        // 构造高亮 RenderState
        RenderState hlState;
        hlState.color = m_config.color;
        hlState.lineWidth = m_config.lineWidth;
        hlState.lineStyle = LineStyle::SOLID;

        for (const auto& item : m_items) {
            DrawPrimitives::Draw(device, context, item.vertices, item.topology, hlState);
        }
    }

    void SelectionHighlight::Clear() {
        m_items.clear();
    }

} // namespace MiniCAD

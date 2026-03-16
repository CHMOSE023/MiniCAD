// ============================================================
// MiniCAD — ui/imgui/PropertyPanel.h
//
// 相比上一版新增的私有成员（支持类型转换缓存）：
//   float    m_colorF[3]   — Color4(uint8) 转 [0,1] float 供 ImGui 使用
//   float    m_lineWidthF  — Real(double) 转 float 供 DragFloat 使用
//   int      m_layerIdx    — uint32_t layerId 转 int 供 Combo 使用
// ============================================================
#pragma once
#include <cstdint>
#include "core/Entity/EntityAttr.h"
#include "ui/imgui/UIPanel.h"

namespace MiniCAD {

    class PropertyPanel final : public UIPanel {
    public:
        void Refresh();
        void Draw() override;

    private:
        void LoadFromSelection();
        void CommitAttrChange();

        EntityAttr m_attr = {};
        uint64_t   m_currentEntityId = 0;
        bool       m_dirty = false;

        // 类型转换缓存（ImGui 只接受 float，项目使用 uint8/double）
        float m_colorF[3] = { 1.f, 1.f, 1.f };     // Color4 → [0,1] float
        float m_lineWidthF = 1.f;                  // Real(double) → float
        int   m_layerIdx = 0;                      // uint32_t → int
    };

} // namespace MiniCAD
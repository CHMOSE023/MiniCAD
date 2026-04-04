#pragma once 
#include "App/Picking/Picking.h"
#include "Core/Object/Object.hpp"
#include "App/Abstractions/ITool.h"
#include "App/Abstractions/IInputHandler.h"
#include "App/Abstractions/IViewContext.h"
#include <unordered_set> 
#include <DirectXMath.h>
#include <memory>

namespace MiniCAD
{
    class Scene;
    class CommandStack;
    class Camera;

    class Editor : public IInputHandler
    {
    public:
        Editor(Scene* scene, CommandStack* cmdStack);

        // ── IEventHandler ─────────────────────────────────────
        bool OnInput(const InputEvent& e) override;

        void SetTool(std::unique_ptr<ITool> tool) { m_tool = std::move(tool); }
        void SetViewContext(IViewContext* ctx) { m_view = ctx; }

        void DeleteSelected();

        // 状态输出（给 Viewport）
        const std::unordered_set<Object::ObjectID>& GetSelection() const { return m_selection; }
        const std::unordered_set<Object::ObjectID>& GetHovered()  const { return m_hovered; }

    private:
        void OnMouseButtonDown(const InputEvent& e);
        void OnMouseButtonUp(const InputEvent& e);
        void OnKeyDown(const InputEvent& e);
        void OnMouseMove(const InputEvent& e);

        void ActivateLastTool();
        void SyncSelectionWithScene();

        // ── 框选辅助 ──────────────────────────────────────────
        void UpdateRubberBandPreview();          // 更新虚线矩形预览
        void CommitRubberBand(bool addToSel);    // 提交框选结果

    private:
        enum class ToolType { None, Line /*, Circle, Rect ... */ };
        ToolType m_lastToolType = ToolType::None;

        Scene* m_scene = nullptr;
        CommandStack* m_cmdStack = nullptr;
        IViewContext* m_view = nullptr;

        std::unique_ptr<ITool>               m_tool = nullptr;
        Picking                              m_picking;

        std::unordered_set<Object::ObjectID> m_selection;
        std::unordered_set<Object::ObjectID> m_hovered;

        Object::ObjectID                     m_hoveredID = Object::InvalidID;

        // ── 框选状态 ──────────────────────────────────────────
        bool             m_isRubberBanding = false;  // 正在框选
        // 框选起点（屏幕像素坐标，用于判断最小拖拽距离）
        float            m_rbStartScreenX = 0.f;
        float            m_rbStartScreenY = 0.f;
        // 框选起点（世界坐标）
        DirectX::XMFLOAT2 m_rbStartWorld = {};
        // 框选当前终点（世界坐标，随 MouseMove 更新）
        DirectX::XMFLOAT2 m_rbEndWorld = {};

        static constexpr float kDragThreshold = 4.f; // 像素，超过才进入框选模式
    };
}
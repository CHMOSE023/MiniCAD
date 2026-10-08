#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Core/Log.h"
#include <vector>

namespace MiniCAD
{
    // 区域覆盖工具（WIPEOUT）：依次点击多边形顶点，回车 / 右键闭合并生成（至少 3 点）。
    // 生成后的遮挡区域盖住绘制序在它之前的图形，之后画的图形不受影响。
    class WipeoutTool : public ITool
    {
    public:
        WipeoutTool()
        {
            LOG_DEBUG("[WipeoutTool] 左键依次指定多边形顶点 | 回车/右键闭合 | ESC 取消");
        }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                m_pts.push_back(GetPoint(e));
                RefreshOverlay(m_pts.back());
                return true;
            }

            if (e.Type == InputEventType::MouseMove && !m_pts.empty())
            {
                RefreshOverlay(GetPoint(e));
                return false;
            }

            if (e.IsKeyPressed(KeyCode::Enter) || e.IsRightClick())
            {
                if (m_pts.size() >= 3)
                {
                    Commit();
                    Reset();
                    if (OnFinished) OnFinished();
                    return true;
                }
                if (e.IsRightClick())
                {
                    Reset();
                    if (OnFinished) OnFinished();
                    return true;
                }
                return false;
            }

            if (e.IsCancel())
            {
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }

            return false;
        }

        bool HasAnchor() const override { return !m_pts.empty(); }

        Math::Point3 GetAnchor() const override { return m_pts.empty() ? Math::Point3{} : m_pts.back(); }

        std::string GetPrompt() const override
        {
            if (m_pts.empty()) return "指定第一点:";
            if (m_pts.size() < 3) return "指定下一点:";
            return "指定下一点 [回车/右键闭合]:";
        }

        void OnSceneChanged() override { Reset(); }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        void RefreshOverlay(const Math::Point3& cursor)
        {
            m_ctx->overlay.Clear();
            const Math::Color4 c{ 1.0, 1.0, 1.0, 1.0 };
            for (size_t i = 0; i + 1 < m_pts.size(); ++i)
                m_ctx->overlay.AddLine(m_pts[i], m_pts[i + 1], c);
            m_ctx->overlay.AddLine(m_pts.back(), cursor, c);
            if (m_pts.size() >= 2)
                m_ctx->overlay.AddLine(cursor, m_pts.front(), { 0.6, 0.6, 0.6, 0.8 });
        }

        void Commit()
        {
            auto id = m_ctx->scene.NextObjectID();
            auto wipe = std::make_unique<WipeoutEntity>(id, m_pts);
            m_ctx->ApplyCurrentAttr(*wipe);
            m_ctx->cmdStack.Execute(std::make_unique<AddEntityCommand>(std::move(wipe)), m_ctx->scene);
            LOG_DEBUG("[WipeoutTool] 区域覆盖 Id=%d (%zu 点)", static_cast<int>(id), m_pts.size());
        }

        void Reset()
        {
            m_pts.clear();
            if (m_overlay) m_overlay->Clear();
        }

    private:
        const EditorContext*      m_ctx = nullptr;
        Overlay*              m_overlay = nullptr;
        std::vector<Math::Point3> m_pts;
    };
}

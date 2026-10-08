#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Log.h"

namespace MiniCAD
{
    class RectangleTool : public ITool
    {
    public:
        RectangleTool()
        {
            LOG_DEBUG("[RectangleTool] 左键第一角点 | 左键第二角点确认 | 右键/ESC 退出");
        }

        ~RectangleTool()
        {
            LOG_DEBUG("exit RectangleTool");
        }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                auto pt = GetPoint(e);
                if (!m_hasStart)
                {
                    m_firstCorner = pt;
                    m_hasStart    = true;
                }
                else
                {
                    Commit(m_firstCorner, pt);
                    Reset();
                }
                return true;
            }

            if (e.Type == InputEventType::MouseMove && m_hasStart)
            {
                auto cursor = GetPoint(e);
                m_ctx->overlay.Clear();
                m_ctx->overlay.AddRect(m_firstCorner, cursor, { 1.0, 1.0, 1.0, 1.0 });
                return false;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                m_ctx->overlay.Clear();
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }

            return false;
        }

        bool HasAnchor() const override { return false; }

        Math::Point3 GetAnchor() const override { return m_firstCorner; }

        std::string GetPrompt() const override
        {
            return m_hasStart ? "指定另一个角点 [右键/ESC 退出]:" : "指定第一个角点:";
        }

        void OnSceneChanged() override { Reset(); }

    private:

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        void Commit(const Math::Point3& a, const Math::Point3& b)
        {
            auto id   = m_ctx->scene.NextObjectID();
            auto rect = std::make_unique<RectangleEntity>(id, a, b);
            m_ctx->ApplyCurrentAttr(*rect);
            auto cmd  = std::make_unique<AddEntityCommand>(std::move(rect));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);

            LOG_DEBUG("[RectangleTool] 矩形 Id=%d  (%.3f,%.3f)-(%.3f,%.3f)",
                   static_cast<int>(id), a.x, a.y, b.x, b.y);
        }

        void Reset()
        {
            m_hasStart    = false;
            m_firstCorner = {};
            if (m_overlay) m_overlay->Clear();
        }

    private:
        const EditorContext* m_ctx = nullptr;
        Overlay*              m_overlay = nullptr;

        bool         m_hasStart    = false;
        Math::Point3 m_firstCorner{};
    };
}

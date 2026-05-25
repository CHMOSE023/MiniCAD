#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Math/Point3.hpp"

namespace MiniCAD
{

    class PointTool : public ITool
    {
    public:
        PointTool()
        {
            printf("[PointTool] 左键放点 | 右键退出\n");
        }

        ~PointTool()
        {
            printf("退出点绘制工具\n");
        }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                auto pt = GetPoint(e);
                Commit(pt);
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                m_ctx->overlay.Clear();
                if (OnFinished) OnFinished();
                return true;
            }

            if (e.Type == InputEventType::MouseMove)
            {
                auto pt = GetPoint(e);
                m_ctx->overlay.Clear();
                m_ctx->overlay.AddPoint(pt, { 0.6,0.6,0.6,0.6 });
                return false;
            }

            return false;
        }

        bool HasAnchor() const override
        {
            return false;
        }

        Math::Point3 GetAnchor() const override
        {
            return Math::Point3(0.f, 0.f, 0.f);
        }

    private:

        Math::Point3 GetPoint(const InputEvent& e)
        {
            if (e.HasSnap)
                return e.SnapWorld;

            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        void Commit(const  Math::Point3& p)
        {
            auto id = m_ctx->scene.NextObjectID();

            auto pointEntity = std::make_unique<PointEntity>(id, p);

            auto cmd = std::make_unique<AddEntityCommand>(std::move(pointEntity));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);

            printf("点 Id %d  (%.3f, %.3f, %.3f)\n", static_cast<int>(id), p.x, p.y, p.z);
        }

    private:
        const EditorContext* m_ctx = nullptr;
    };
}

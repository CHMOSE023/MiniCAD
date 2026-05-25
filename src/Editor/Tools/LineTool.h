#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include <cstdio>
#include <optional>

namespace MiniCAD
{
    class LineTool : public ITool
    {
    public:
        LineTool()
        {
            printf("[LineTool] 左键起点 | 左键延续 | 右键结束段 | 空格继续 | ESC 退出\n");
        }
        ~LineTool()
        {
            printf("退出绘制\n");
        }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                auto pt = GetPoint(e);

                if (!m_hasStart)
                {
                    m_start = pt;
                    m_hasStart = true;
                }
                else
                {
                    Commit(m_start, pt);
                    m_start = pt;
                }
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                m_ctx->overlay.Clear();
                if (OnFinished) OnFinished();
                return true;
            }

            if (e.Type == InputEventType::MouseMove && m_hasStart)
            {
                m_preview = GetPoint(e);
                m_ctx->overlay.Clear();
                const auto& layer = m_ctx->scene.GetLayerManager().GetActiveLayer();
                m_ctx->overlay.AddLine(m_start, m_preview, layer.GetColor());
                return false;
            }

            return false;
        }

        bool HasAnchor() const override
        {
            return m_hasStart;
        }

        Math::Point3 GetAnchor() const override
        {
            return Math::Point3(m_start.x, m_start.y, 0.f);
        }

    private:
        Math::Point3 GetPoint(const InputEvent& e)
        {
            if (e.HasSnap) return e.SnapWorld;

            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        void Commit(const Math::Point3& a, const Math::Point3& b)
        {
            auto id   = m_ctx->scene.NextObjectID();
            auto line = std::make_unique<LineEntity>(id, a, b);
            auto cmd  = std::make_unique<AddEntityCommand>(std::move(line));

            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);

            printf("线段 Id %d  (%.3f,%.3f) (%.3f,%.3f)\n",static_cast<int>(id), a.x, a.y, b.x, b.y);
        }

    private:
        const EditorContext* m_ctx = nullptr;
        bool          m_hasStart = false;
        Math::Point3  m_start{};
        Math::Point3  m_preview{};
    };
}

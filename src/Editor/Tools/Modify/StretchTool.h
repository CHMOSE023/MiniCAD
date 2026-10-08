#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Editor/Overlay/EntityOverlay.h"
#include "Document/Command/EntityStretch.h"
#include "Document/Command/GeometryEditCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Object/Object.hpp"
#include "Core/Entity/Entity.hpp"
#include <cmath>
#include <limits>
#include <vector>
#include "Core/Log.h"

namespace MiniCAD
{
    // =========================================================================
    // StretchTool —— 拉伸
    //
    // 交叉窗口（两个对角点）→ 基点 → 位移目标点。
    // 窗口内的「定义点」（端点、顶点、圆心、插入点…）跟着位移，窗口外的不动，
    // 与窗口只是相交而没有定义点落在窗口内的对象不受影响（规则见 StretchEntityInPlace）。
    // 与 AutoCAD 一样不需要预选对象；位移 = 目标点 − 基点，因此也可以在拿到基点后键入距离。
    // 锁定 / 隐藏图层上的对象不参与。整批一步可撤销。
    // =========================================================================
    class StretchTool : public ITool
    {
    public:
        StretchTool()  { LOG_INFO("[StretchTool] 左键指定交叉窗口的第一个角点"); }
        ~StretchTool() { LOG_DEBUG("[StretchTool] 退出"); }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx     = &ctx;
            m_overlay = &ctx.overlay;
            const auto& e = ctx.event;

            if (e.IsRightClick() || e.IsCancel())
            {
                Cancel();
                return true;
            }

            switch (m_phase)
            {
            case Phase::Corner1:
                if (e.IsLeftClick())
                {
                    m_corner1 = GetPoint(e);
                    m_phase   = Phase::Corner2;
                    return true;
                }
                break;

            case Phase::Corner2:
                if (e.IsLeftClick())
                {
                    m_window = StretchWindow::FromCorners(m_corner1, GetPoint(e));
                    CollectAffected();
                    if (m_affected.empty())
                    {
                        LOG_WARN("[StretchTool] 窗口内没有可拉伸的定义点，请重新指定窗口");
                        m_ctx->overlay.Clear();
                        m_phase = Phase::Corner1;
                        return true;
                    }
                    m_phase = Phase::Base;
                    DrawWindow();
                    return true;
                }
                if (e.Type == InputEventType::MouseMove)
                {
                    m_ctx->overlay.Clear();
                    DrawRect(StretchWindow::FromCorners(m_corner1, GetPoint(e)));
                    return false;
                }
                break;

            case Phase::Base:
                if (e.IsLeftClick())
                {
                    m_base  = GetPoint(e);
                    m_phase = Phase::Target;
                    return true;
                }
                break;

            case Phase::Target:
                if (e.IsLeftClick())
                {
                    const Math::Point3 p = GetPoint(e);
                    Commit(p.x - m_base.x, p.y - m_base.y);
                    return true;
                }
                if (e.Type == InputEventType::MouseMove)
                {
                    const Math::Point3 p = GetPoint(e);
                    RebuildPreview(p.x - m_base.x, p.y - m_base.y);
                    return false;
                }
                break;
            }
            return false;
        }

        // 注意：m_ctx 指向 OnInput 期间的栈对象，事件之外只能用 m_overlay
        void Cancel() override
        {
            if (m_overlay) m_overlay->Clear();
            if (OnFinished) OnFinished();
        }

        void OnSceneChanged() override { Cancel(); }
        void OnFocusLost()    override { if (m_overlay) m_overlay->Clear(); }

        bool         HasAnchor() const override { return m_phase == Phase::Target; }
        Math::Point3 GetAnchor() const override { return m_base; }

        std::string GetPrompt() const override
        {
            switch (m_phase)
            {
                case Phase::Corner2: return "指定交叉窗口的对角点:";
                case Phase::Base:    return "指定基点:";
                case Phase::Target:  return "指定位移的目标点（或键入距离）:";
                default:             return "指定交叉窗口的第一个角点:";
            }
        }

    private:
        enum class Phase { Corner1, Corner2, Base, Target };

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        // 窗口内至少有一个定义点会被拉伸的对象
        void CollectAffected()
        {
            m_affected.clear();
            constexpr double inf = std::numeric_limits<double>::infinity();
            AABB q({ m_window.MinX, m_window.MinY, -inf }, { m_window.MaxX, m_window.MaxY, inf });
            std::vector<Object::ObjectID> candidates;
            m_ctx->picking.QueryWorldAABB(q, candidates);

            for (auto id : candidates)
            {
                const auto* ent = dynamic_cast<const Entity*>(m_ctx->scene.GetEntity(id));
                if (!ent || !Editable(*ent)) continue;
                auto probe = ent->Clone(id);
                if (StretchEntityInPlace(*probe, m_window, 1.0, 0.0))     // 试探：随便给个位移，看有没有定义点在窗口内
                    m_affected.push_back(id);
            }
        }

        bool Editable(const Entity& e) const
        {
            const auto& attr = e.GetAttr();
            if (!attr.Visible) return false;
            if (const Layer* layer = m_ctx->scene.GetLayerManager().GetLayer(attr.LayerId))
                if (!layer->IsVisible() || layer->IsLocked())
                    return false;
            return true;
        }

        void DrawRect(const StretchWindow& w)
        {
            static const Math::Color4 kWindow = { 0.2, 0.8, 0.4, 0.9 };       // 绿色：交叉窗口
            m_ctx->overlay.AddRect({ w.MinX, w.MinY, 0.0 }, { w.MaxX, w.MaxY, 0.0 }, kWindow);
        }

        void DrawWindow()
        {
            m_ctx->overlay.Clear();
            DrawRect(m_window);
        }

        void RebuildPreview(double dx, double dy)
        {
            m_ctx->overlay.Clear();
            DrawRect(m_window);

            static const Math::Color4 guide = { 0.6, 0.6, 0.6, 0.6 };
            m_ctx->overlay.AddLine(m_base, { m_base.x + dx, m_base.y + dy, m_base.z }, guide);

            const auto& color = m_ctx->scene.GetLayerManager().GetActiveLayer().GetColor();
            for (auto id : m_affected)
            {
                const auto* src = dynamic_cast<const Entity*>(m_ctx->scene.GetEntity(id));
                if (!src) continue;
                auto temp = src->Clone(0);
                if (StretchEntityInPlace(*temp, m_window, dx, dy))
                    DrawEntityToOverlay(m_ctx->overlay, *temp, color);
            }
        }

        void Commit(double dx, double dy)
        {
            std::vector<GeometryEditCommand::Item> items;
            for (auto id : m_affected)
            {
                const auto* src = dynamic_cast<const Entity*>(m_ctx->scene.GetEntity(id));
                if (!src) continue;
                GeometryEditCommand::Item item;
                item.id     = id;
                item.before = src->Clone(id);
                item.after  = src->Clone(id);
                if (!StretchEntityInPlace(*item.after, m_window, dx, dy))
                    continue;
                items.push_back(std::move(item));
            }

            if (!items.empty())
            {
                const size_t n = items.size();
                m_ctx->cmdStack.Execute(std::make_unique<GeometryEditCommand>("拉伸", std::move(items)), m_ctx->scene);
                LOG_DEBUG("[StretchTool] 拉伸 %zu 个对象，位移 (%.3f, %.3f)", n, dx, dy);
            }
            else
                LOG_WARN("[StretchTool] 位移为零，没有对象被拉伸");

            m_ctx->overlay.Clear();
            if (OnFinished) OnFinished();
        }

        const EditorContext*          m_ctx     = nullptr;      // 仅在 OnInput 内有效
        Overlay*                      m_overlay = nullptr;
        Phase                         m_phase   = Phase::Corner1;
        Math::Point3                  m_corner1{};
        Math::Point3                  m_base{};
        StretchWindow                 m_window;
        std::vector<Object::ObjectID> m_affected;
    };
}

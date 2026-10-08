#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/Math/Constants.hpp"
#include "Core/Entity/XLineEntity.hpp"
#include "Core/Entity/RayEntity.hpp"
#include "Core/Log.h"
#include <memory>

namespace MiniCAD
{
    // ─────────────────────────────────────────────────────────────
    // InfiniteLineToolBase — 无限线绘制工具基类(模板)
    //
    //   EntityT   = XLineEntity(构造线,双向无限) / RayEntity(射线,单向无限)
    //   BothSides = 预览是否向两侧延伸(XLine = true,Ray = false)
    //
    // 交互(对齐 AutoCAD XLINE / RAY):
    //   左键定根点 → 左键逐个定通过点(每次创建一条线,根点保持)
    //   → 右键 / ESC 退出
    // ─────────────────────────────────────────────────────────────
    template <typename EntityT, bool BothSides>
    class InfiniteLineToolBase : public ITool
    {
    public:
        explicit InfiniteLineToolBase(const char* tag) : m_tag(tag)
        {
            LOG_DEBUG("[%s] 左键定根点 | 左键定通过点 | 右键/ESC 退出", m_tag);
        }

        ~InfiniteLineToolBase() override { LOG_DEBUG("退出绘制"); }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                auto pt = GetPoint(e);

                if (!m_hasRoot)
                {
                    m_root    = pt;
                    m_hasRoot = true;
                }
                else
                {
                    Math::Vec3 dir = pt - m_root;
                    if (dir.LengthSq() > Math::LengthEPS * Math::LengthEPS)
                        Commit(m_root, dir);   // 根点保持,可连续创建多条
                }
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                m_ctx->overlay.Clear();
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }

            if (e.Type == InputEventType::MouseMove && m_hasRoot)
            {
                m_preview = GetPoint(e);
                m_ctx->overlay.Clear();

                Math::Vec3 dir = m_preview - m_root;
                if (dir.LengthSq() > Math::LengthEPS * Math::LengthEPS)
                {
                    const auto& layer = m_ctx->scene.GetLayerManager().GetActiveLayer();

                    // 无限线预览:沿单位方向延伸足够长以近似「无限」
                    Math::Vec3   u = dir.Normalized();
                    Math::Point3 a = BothSides ? (m_root - u * kPreviewHalfLength) : m_root;
                    Math::Point3 b = m_root + u * kPreviewHalfLength;

                    m_ctx->overlay.AddLine(a, b, layer.GetColor());
                    // 根点 → 光标 的实段提示(灰色半透明)
                    m_ctx->overlay.AddLine(m_root, m_preview, { 0.6, 0.6, 0.6, 0.4 });
                }
                return false;
            }

            return false;
        }

        bool HasAnchor() const override { return m_hasRoot; }

        Math::Point3 GetAnchor() const override { return { m_root.x, m_root.y, 0.0 }; }

        std::string GetPrompt() const override
        {
            return m_hasRoot ? "指定通过点 [右键/ESC 退出]:" : "指定根点:";
        }

        void OnSceneChanged() override { Reset(); }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        void Commit(const Math::Point3& origin, const Math::Vec3& dir)
        {
            auto id  = m_ctx->scene.NextObjectID();
            auto ent = std::make_unique<EntityT>(id, origin, dir);
            m_ctx->ApplyCurrentAttr(*ent);
            auto cmd = std::make_unique<AddEntityCommand>(std::move(ent));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);

            LOG_DEBUG("[%s] Id %d  origin(%.3f,%.3f)  dir(%.3f,%.3f)",
                   m_tag, static_cast<int>(id), origin.x, origin.y, dir.x, dir.y);
        }

        void Reset()
        {
            m_hasRoot = false;
            m_root    = {};
            if (m_overlay) m_overlay->Clear();
        }

    private:
        static constexpr double kPreviewHalfLength = 1.0e6;   // 近似无限延伸的(半)长

        const char*          m_tag = "InfiniteLine";
        const EditorContext* m_ctx = nullptr;
        Overlay*              m_overlay = nullptr;

        bool         m_hasRoot = false;
        Math::Point3 m_root{};
        Math::Point3 m_preview{};
    };

    // ─────────────────────────────────────────────────────────────
    // XLineTool — 构造线(XLINE):向两侧无限延伸
    // ─────────────────────────────────────────────────────────────
    class XLineTool : public InfiniteLineToolBase<XLineEntity, true>
    {
    public:
        XLineTool() : InfiniteLineToolBase("XLineTool") {}
    };

    // ─────────────────────────────────────────────────────────────
    // RayTool — 射线(RAY):从根点单侧无限延伸
    // ─────────────────────────────────────────────────────────────
    class RayTool : public InfiniteLineToolBase<RayEntity, false>
    {
    public:
        RayTool() : InfiniteLineToolBase("RayTool") {}
    };
}

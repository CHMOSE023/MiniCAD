#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Entity/SolidEntity.hpp"
#include "Core/Log.h"
#include <vector>

namespace MiniCAD
{
    // 二维填充工具（SOLID）。
    // 点序与 AutoCAD 一致：第 1、2 点是一条边，第 3、4 点是对边，且 3 与 2 同侧。
    // 第 3 点之后按回车 / 右键得到三角形，第 4 点之后直接生成四边形；
    // 生成后继续画下一个，ESC 或空点集时右键退出。
    class SolidTool : public ITool
    {
    public:
        SolidTool()
        {
            LOG_DEBUG("[SolidTool] 左键依次指定 1-2-3-4 点 | 第 3 点后回车/右键=三角形 | ESC 退出");
        }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                m_pts.push_back(GetPoint(e));
                if (m_pts.size() == 4)
                {
                    Commit();
                    Reset();
                }
                else
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
                if (m_pts.size() == 3)
                {
                    Commit();
                    Reset();
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
            switch (m_pts.size())
            {
            case 0:  return "指定第一点:";
            case 1:  return "指定第二点:";
            case 2:  return "指定第三点:";
            default: return "指定第四点或 [回车/右键=三角形]:";
            }
        }

        void OnSceneChanged() override { Reset(); }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        // 预览轮廓：已指定的点 + 光标点，按最终的轮廓顺序（1-2-4-3）连线
        void RefreshOverlay(const Math::Point3& cursor)
        {
            m_ctx->overlay.Clear();
            std::vector<Math::Point3> p = m_pts;
            if (p.size() < 4) p.push_back(cursor);

            std::vector<Math::Point3> ring;
            ring.push_back(p[0]);
            if (p.size() > 1) ring.push_back(p[1]);
            if (p.size() > 3) ring.push_back(p[3]);
            if (p.size() > 2) ring.push_back(p[2]);

            const Math::Color4 c{ 1.0, 1.0, 1.0, 1.0 };
            for (size_t i = 0; i + 1 < ring.size(); ++i)
                m_ctx->overlay.AddLine(ring[i], ring[i + 1], c);
            if (ring.size() > 2)
                m_ctx->overlay.AddLine(ring.back(), ring.front(), c);
        }

        void Commit()
        {
            auto id = m_ctx->scene.NextObjectID();
            std::unique_ptr<SolidEntity> solid;
            if (m_pts.size() == 4)
                solid = std::make_unique<SolidEntity>(id, m_pts[0], m_pts[1], m_pts[3], m_pts[2]);  // 点序 1-2-3-4 → 轮廓 1-2-4-3
            else
                solid = std::make_unique<SolidEntity>(id, m_pts[0], m_pts[1], m_pts[2]);

            m_ctx->ApplyCurrentAttr(*solid);
            m_ctx->cmdStack.Execute(std::make_unique<AddEntityCommand>(std::move(solid)), m_ctx->scene);
            LOG_DEBUG("[SolidTool] 二维填充 Id=%d (%zu 点)", static_cast<int>(id), m_pts.size());
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

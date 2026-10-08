#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Color4.hpp"
#include "Core/Entity/LeaderEntity.hpp"
#include "Core/Log.h"
#include <vector>
#include <functional>

namespace MiniCAD
{
    // 创建引线标注(LEADER)。依次点取引线顶点:第一点为箭头落点,后续点构成折线;
    // 右键提交。起端带实心箭头,末端带水平基线(hookline)承接注释文本。
    //
    // 文本输入沿用 Text/MText 工具的延迟录入约定:提交后通过 OnLeaderCreated
    // 把新建实体 Id 与末端位置交回编辑器,由编辑器弹出文本输入框写入注释;
    // 未挂接回调时直接以空注释提交(可后续经夹点 / 属性编辑补全)。
    class LeaderTool : public ITool
    {
    public:
        LeaderTool()
        {
            LOG_DEBUG("[LeaderTool] 左键箭头落点 | 左键追加顶点 | 右键提交 | ESC 取消");
        }

        ~LeaderTool() { LOG_DEBUG("[LeaderTool] 退出"); }

        // 提交后回调:newId = 新建引线 Id,tail = 末端顶点(文本锚点附近)。
        // 编辑器可挂接此回调弹出文本输入框写入注释;未挂接时引线以默认文字提交。
        std::function<void(Object::ObjectID, Math::Point3)> OnLeaderCreated;

        // 默认注释文字(提交时写入,之后可经文本输入框 / 夹点编辑修改)。
        void SetDefaultText(std::string t) { m_defaultText = std::move(t); }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.Type == InputEventType::MouseMove)
            {
                m_cursor = GetPoint(e);
                RefreshOverlay();
                return false;
            }

            if (e.IsLeftClick())
            {
                auto pt = GetPoint(e);
                m_points.push_back(pt);
                LOG_INFO("[LeaderTool] 顶点 #%zu (%.3f, %.3f)", m_points.size(), pt.x, pt.y);
                RefreshOverlay();
                return true;
            }

            if (e.IsRightClick())
            {
                Commit();
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }

            if (e.IsCancel())
            {
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }

            return false;
        }

        bool HasAnchor() const override { return !m_points.empty(); }

        Math::Point3 GetAnchor() const override
        {
            return m_points.empty() ? Math::Point3{} : m_points.back();
        }

        std::string GetPrompt() const override
        {
            if (m_points.empty()) return "指定引线箭头落点:";
            return "指定下一点 [右键提交/ESC 取消]:";
        }

        void OnSceneChanged() override { Reset(); }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            auto p = m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
            return { p.x, p.y, 0.0 };
        }

        void RefreshOverlay()
        {
            if (!m_ctx) return;
            m_ctx->overlay.Clear();
            if (m_points.empty()) return;

            const auto& layer = m_ctx->scene.GetLayerManager().GetActiveLayer();
            const Math::Color4 layerColor  = layer.GetColor();
            const Math::Color4 rubberColor = { 0.6f, 0.6f, 0.6f, 0.5f };
            const Math::Color4 vertColor   = { 1.0f, 0.8f, 0.2f, 0.8f };

            for (size_t i = 1; i < m_points.size(); ++i)
                m_ctx->overlay.AddLine(m_points[i - 1], m_points[i], layerColor);

            for (const auto& pt : m_points)
                m_ctx->overlay.AddPoint(pt, vertColor);

            // 当前段橡皮线。
            m_ctx->overlay.AddLine(m_points.back(), m_cursor, rubberColor);
        }

        void Commit()
        {
            if (m_points.size() < 2)
            {
                LOG_WARN("[LeaderTool] 顶点不足，放弃提交");
                return;
            }

            auto id     = m_ctx->scene.NextObjectID();
            Math::Point3 tail = m_points.back();
            auto entity = std::make_unique<LeaderEntity>(id, m_points);
            entity->SetText(m_defaultText);          // 默认注释,稍后可编辑
            m_ctx->ApplyCurrentAttr(*entity);
            auto cmd    = std::make_unique<AddEntityCommand>(std::move(entity));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);

            LOG_DEBUG("[LeaderTool] 提交 Id=%d  顶点=%zu",
                   static_cast<int>(id), m_points.size());

            if (OnLeaderCreated) OnLeaderCreated(id, tail);
        }

        void Reset()
        {
            m_points.clear();
            m_cursor = {};
            if (m_overlay) m_overlay->Clear();
        }

    private:
        const EditorContext* m_ctx = nullptr;
        Overlay*              m_overlay = nullptr;

        std::vector<Math::Point3> m_points;
        Math::Point3              m_cursor{};
        std::string               m_defaultText = "说明";   // 默认注释文字
    };
}

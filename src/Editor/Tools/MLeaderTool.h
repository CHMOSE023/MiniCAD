#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/Math/Color4.hpp"
#include "Core/Entity/MLeaderEntity.hpp"
#include "Core/Log.h"
#include <string>
#include <vector>
#include <functional>

namespace MiniCAD
{
    // 创建多重引线(MULTILEADER):多个箭头落点 → 一条基线 → 一个文字注释。
    //
    // 操作:左键依次放置各箭头落点;右键结束放置、进入指定基线/文字位置;
    //       再左键定下基线位置即提交。各箭头引线汇聚到 dogleg 起点,经水平基线接 MText。
    //
    // 文本输入沿用 Leader/MText 的延迟录入约定:提交后经 OnMLeaderCreated 把新建实体
    // Id 与基线位置交回编辑器弹框写入;未挂接回调时以默认文字提交(可后续编辑)。
    class MLeaderTool : public ITool
    {
    public:
        MLeaderTool()
        {
            LOG_DEBUG("[MLeaderTool] 左键放置箭头落点(可多个) | 右键结束并指定基线/文字位置 | ESC 退出");
        }

        ~MLeaderTool() { LOG_DEBUG("[MLeaderTool] 退出"); }

        // 提交后回调:newId = 新建多重引线 Id,landing = 基线端点(文字锚点附近)。
        std::function<void(Object::ObjectID, Math::Point3)> OnMLeaderCreated;

        // 默认注释文字(提交时写入,之后可经文本输入框 / 夹点编辑修改)。
        void SetDefaultText(std::string t) { m_defaultText = std::move(t); }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsLeftClick())
            {
                auto pt = GetPoint(e);
                if (m_step == 0)
                {
                    m_arrows.push_back(pt);
                    LOG_INFO("[MLeaderTool] 箭头落点 #%zu (%.3f, %.3f)",
                           m_arrows.size(), pt.x, pt.y);
                    RefreshOverlay();
                }
                else   // m_step == 1:定下基线位置 → 提交
                {
                    Commit(pt);
                    Reset();
                    if (OnFinished) OnFinished();
                }
                return true;
            }

            if (e.IsRightClick())
            {
                // 结束箭头放置,进入基线放置(至少需要一个箭头)。
                if (m_step == 0 && !m_arrows.empty())
                {
                    m_step = 1;
                    LOG_INFO("[MLeaderTool] 箭头共 %zu 个，请指定基线/文字位置", m_arrows.size());
                    return true;
                }
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

            if (e.Type == InputEventType::MouseMove)
            {
                m_cursor = GetPoint(e);
                RefreshOverlay();
                return false;
            }

            return false;
        }

        bool HasAnchor() const override { return !m_arrows.empty(); }
        Math::Point3 GetAnchor() const override
        {
            return m_arrows.empty() ? Math::Point3{} : m_arrows.front();
        }

        std::string GetPrompt() const override
        {
            if (m_step == 1) return "指定基线/文字位置 [右键/ESC 退出]:";
            if (m_arrows.empty()) return "指定第一个箭头落点:";
            return "指定下一个箭头落点 [右键结束放置]:";
        }

        void OnSceneChanged() override { Reset(); }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            auto p = m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
            return { p.x, p.y, 0.0 };
        }

        // 各箭头的平均位置(用于决定基线朝向)。
        Math::Point3 ArrowCentroid() const
        {
            Math::Point3 c{};
            for (const auto& a : m_arrows) { c.x += a.x; c.y += a.y; }
            double n = m_arrows.empty() ? 1.0 : static_cast<double>(m_arrows.size());
            return { c.x / n, c.y / n, 0.0 };
        }

        // 基线方向:水平,由内容(landing)指回引线侧。
        Math::Vec3 DoglegDir(const Math::Point3& landing) const
        {
            double sx = (ArrowCentroid().x >= landing.x) ? 1.0 : -1.0;
            return { sx, 0.0, 0.0 };
        }

        Math::Point3 LandingToDoglegStart(const Math::Point3& landing) const
        {
            Math::Vec3 d = DoglegDir(landing);
            return { landing.x + d.x * m_style.DoglegLength,
                     landing.y + d.y * m_style.DoglegLength, landing.z };
        }

        void RefreshOverlay()
        {
            if (!m_ctx) return;
            m_ctx->overlay.Clear();
            if (m_arrows.empty()) return;

            const auto& layer = m_ctx->scene.GetLayerManager().GetActiveLayer();
            const Math::Color4 layerColor  = layer.GetColor();
            const Math::Color4 helperColor = { 0.6f, 0.6f, 0.6f, 0.5f };
            const Math::Color4 vertColor   = { 1.0f, 0.8f, 0.2f, 0.8f };

            for (const auto& a : m_arrows)
                m_ctx->overlay.AddPoint(a, vertColor);

            if (m_step == 0)
            {
                // 仍在放置箭头:从最后一个箭头到光标画橡皮线提示下一点。
                m_ctx->overlay.AddLine(m_arrows.back(), m_cursor, helperColor);
            }
            else
            {
                // 放置基线:各箭头汇聚到 dogleg 起点 + 水平基线到光标。
                Math::Point3 doglegStart = LandingToDoglegStart(m_cursor);
                for (const auto& a : m_arrows)
                    m_ctx->overlay.AddLine(a, doglegStart, layerColor);
                m_ctx->overlay.AddLine(doglegStart, m_cursor, helperColor);
            }
        }

        void Commit(const Math::Point3& landing)
        {
            if (m_arrows.empty()) return;

            auto id  = m_ctx->scene.NextObjectID();
            auto ent = std::make_unique<MLeaderEntity>(id, m_style);
            for (const auto& a : m_arrows)
                ent->AddLeaderLine({ a }, /*arrow=*/true);   // 单点引线:箭头尖端 → dogleg 起点
            ent->SetLanding(landing);
            ent->SetDoglegDir(DoglegDir(landing));
            ent->SetText(m_defaultText);                      // 默认注释(同时置内容为 MText)

            m_ctx->ApplyCurrentAttr(*ent);
            auto cmd = std::make_unique<AddEntityCommand>(std::move(ent));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);

            LOG_DEBUG("[MLeaderTool] 提交 Id=%d  箭头=%zu  基线(%.3f,%.3f)",
                   static_cast<int>(id), m_arrows.size(), landing.x, landing.y);

            if (OnMLeaderCreated) OnMLeaderCreated(id, landing);
        }

        void Reset()
        {
            m_step = 0;
            m_arrows.clear();
            m_cursor = {};
            if (m_overlay) m_overlay->Clear();
        }

    private:
        const EditorContext* m_ctx = nullptr;
        Overlay*              m_overlay = nullptr;

        int                       m_step = 0;   // 0=放置箭头  1=放置基线/文字
        std::vector<Math::Point3> m_arrows;     // 各箭头落点(每个 = 一条单点引线)
        Math::Point3              m_cursor{};
        MLeaderStyle              m_style;
        std::string               m_defaultText = "说明";   // 默认注释文字
    };
}

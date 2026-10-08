#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/EditorContext.h"
#include "Editor/Tools/HatchBoundaryFinder.hpp"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Color4.hpp"
#include "Core/GeomKernel/Polyline.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Scene/HatchPatternLibrary.h"
#include "Editor/Tools/HatchSettings.h"
#include "Core/Log.h"
#include <vector>

namespace MiniCAD
{
    // ─────────────────────────────────────────────
    // HatchTool — 图案填充工具(类 AutoCAD 拾取内部点)
    //
    // 在封闭区域内部点一下，自动由现有几何计算出环绕该点的最小闭合边界并填充；
    // 若该点不被任何闭合域包围，则不创建任何对象。
    //
    // 操作：
    //   移动      实时高亮将被填充的闭合边界(找不到则不高亮)
    //   左键      在当前拾取点创建填充(无闭合域 → 不创建)
    //   S / H / N 切换图案：实心 / ANSI31 斜线 / 正交网格(无图案对话框的宿主用)
    //   右键/ESC  退出工具
    // 图案、比例、角度取自 Editor 持有的 HatchSettings(由填充对话框编辑)。
    // ─────────────────────────────────────────────
    class HatchTool : public ITool
    {
    public:

        explicit HatchTool(HatchSettings& settings)
            : m_settings(settings)
        {
            LOG_DEBUG("[HatchTool] 在闭合区域内部点击以填充 | S=实心 H=ANSI31 N=网格 | 右键/ESC 退出");
        }

        ~HatchTool()
        {
            LOG_DEBUG("[HatchTool] 退出");
        }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsKeyPressed(KeyCode::S)) { SetPattern("SOLID");  return true; }
            if (e.IsKeyPressed(KeyCode::H)) { SetPattern("ANSI31"); return true; }
            if (e.IsKeyPressed(KeyCode::N)) { SetPattern("NET");    return true; }

            if (e.Type == InputEventType::MouseMove)
            {
                m_cursor = GetPoint(e);
                UpdatePreview();
                return false;
            }

            if (e.IsLeftClick())
            {
                CreateAt(GetPoint(e));
                return true;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                if (m_overlay) m_overlay->Clear();
                if (OnFinished) OnFinished();
                return true;
            }

            return false;
        }

        std::string GetPrompt() const override
        {
            return "拾取内部点 [图案 " + m_settings.Pattern + " | 右键退出]:";
        }

        void OnSceneChanged() override
        {
            m_hasLoop = false;
            if (m_overlay) m_overlay->Clear();
        }

    private:

        void SetPattern(const char* name)
        {
            m_settings.Pattern = name;
            LOG_INFO("[HatchTool] 图案 = %s", name);
            UpdatePreview();
        }

        // 计算并高亮当前光标处的闭合边界。
        void UpdatePreview()
        {
            if (!m_ctx) return;
            m_ctx->overlay.Clear();

            m_loop.Edges.clear();
            m_hasLoop = HatchBoundary::FindEnclosingLoop(m_ctx->scene, m_cursor, m_loop);
            if (!m_hasLoop) return;

            const Math::Color4 edge = { 0.2f, 0.9f, 1.0f, 0.9f };   // 青色边界
            const Math::Color4 mark = { 0.2f, 0.9f, 1.0f, 0.25f };  // 拾取点标记

            // 按边界真实曲线离散后高亮(圆弧/椭圆/样条均跟随原形)
            std::vector<Math::Point3> pts = m_loop.Tessellate();
            for (size_t i = 0; i + 1 < pts.size(); ++i)
                m_ctx->overlay.AddLine(pts[i], pts[i + 1], edge);
            if (pts.size() >= 2)
                m_ctx->overlay.AddLine(pts.back(), pts.front(), edge);

            m_ctx->overlay.AddPoint(m_cursor, mark);
        }

        void CreateAt(const Math::Point3& pt)
        {
            HatchLoop loop;
            if (!HatchBoundary::FindEnclosingLoop(m_ctx->scene, pt, loop))
            {
                LOG_WARN("[HatchTool] 该点未被闭合区域包围，未创建填充");
                return;
            }

            std::vector<HatchLoop> loops{ std::move(loop) };
            auto id     = m_ctx->scene.NextObjectID();
            const HatchPattern pattern = HatchPatternLibrary::Instance().Get(m_settings.Pattern);
            auto entity = std::make_unique<HatchEntity>(id, std::move(loops), pattern);
            entity->SetScale(m_settings.Scale);
            entity->SetAngle(m_settings.AngleDeg);
            m_ctx->ApplyCurrentAttr(*entity);
            auto cmd    = std::make_unique<AddEntityCommand>(std::move(entity));
            m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene);

            LOG_DEBUG("[HatchTool] 提交 Id=%d  图案=%s",
                   static_cast<int>(id), pattern.Name.c_str());

            // 刷新预览(场景已变)
            m_hasLoop = false;
            UpdatePreview();
        }

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            auto p = m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
            return { p.x, p.y, 0.0 };
        }

    private:

        const EditorContext* m_ctx = nullptr;

        Overlay*              m_overlay = nullptr;

        Math::Point3 m_cursor{};

        bool      m_hasLoop = false;
        HatchLoop m_loop;

        HatchSettings& m_settings;      // Editor 持有,生命周期长于工具
    };
}

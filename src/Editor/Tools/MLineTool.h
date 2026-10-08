#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/Tools/MLineSettings.h"
#include "Editor/EditorContext.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Entity/MLineEntity.hpp"
#include "Core/Log.h"
#include <cstdio>
#include <string>
#include <vector>

namespace MiniCAD
{
    // 多线工具（MLINE）：依次点击路径顶点，回车 / 右键结束（至少 2 点）。
    //   J = 切换对正（上 / 无 / 下）   S = 切换比例   T = 切换当前多线样式
    //   C = 闭合并结束（至少 3 点）     Backspace = 撤销上一个点   ESC = 放弃
    // 没有数值输入通道，所以比例在常用值里循环；精确比例可由 MLineSettings 直接设置。
    class MLineTool : public ITool
    {
    public:
        explicit MLineTool(MLineSettings& settings) : m_settings(settings)
        {
            LOG_DEBUG("[MLineTool] 左键加点 | 回车/右键结束 | J 对正 S 比例 T 样式 C 闭合 | Backspace 撤销点 | ESC 放弃");
        }

        bool OnInput(const EditorContext& ctx) override
        {
            m_ctx = &ctx;
            m_overlay = &ctx.overlay;      // 事件之外只用它：m_ctx 指向的栈对象事件返回后即失效
            const auto& e = ctx.event;

            if (e.IsKeyPressed(KeyCode::J))
            {
                m_settings.Justify = static_cast<MLineJustify>((static_cast<int>(m_settings.Justify) + 1) % 3);
                Refresh(m_cursor);
                return true;
            }
            if (e.IsKeyPressed(KeyCode::S))
            {
                static const double kScales[] = { 1, 2, 5, 10, 20, 50, 100 };
                size_t i = 0;
                while (i < sizeof(kScales) / sizeof(*kScales) && kScales[i] <= m_settings.Scale + 1e-9) ++i;
                m_settings.Scale = kScales[i % (sizeof(kScales) / sizeof(*kScales))];
                Refresh(m_cursor);
                return true;
            }
            if (e.IsKeyPressed(KeyCode::T))
            {
                CycleStyle();
                Refresh(m_cursor);
                return true;
            }
            if (e.IsKeyPressed(KeyCode::C) && m_pts.size() >= 3)
            {
                Commit(true);
                Finish();
                return true;
            }
            if (e.IsKeyPressed(KeyCode::Backspace) && !m_pts.empty())
            {
                m_pts.pop_back();
                Refresh(m_cursor);
                return true;
            }

            if (e.IsLeftClick())
            {
                m_cursor = GetPoint(e);
                m_pts.push_back(m_cursor);
                Refresh(m_cursor);
                return true;
            }

            if (e.Type == InputEventType::MouseMove)
            {
                m_cursor = GetPoint(e);
                Refresh(m_cursor);
                return false;
            }

            if (e.IsKeyPressed(KeyCode::Enter) || e.IsRightClick())
            {
                if (m_pts.size() >= 2)
                    Commit(false);
                Finish();
                return true;
            }

            if (e.IsCancel())
            {
                Finish();
                return true;
            }

            return false;
        }

        bool HasAnchor() const override { return !m_pts.empty(); }
        Math::Point3 GetAnchor() const override { return m_pts.empty() ? Math::Point3{} : m_pts.back(); }

        std::string GetPrompt() const override
        {
            static const char* kJ[] = { "上", "无", "下" };
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s [J 对正=%s  S 比例=%g  T 样式=%s%s]:",
                          m_pts.empty() ? "指定起点" : "指定下一点",
                          kJ[static_cast<int>(m_settings.Justify)], m_settings.Scale,
                          StyleName().c_str(), m_pts.size() >= 3 ? "  C 闭合" : "");
            return buf;
        }

        void OnSceneChanged() override { m_pts.clear(); if (m_overlay) m_overlay->Clear(); }

    private:
        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        const MLineStyleRecord& Style() const
        {
            const auto& scene = m_ctx->scene;
            return scene.GetMLineStyleTable().Resolve(scene.GetCurrentMLineStyle());
        }
        std::string StyleName() const { return m_ctx ? Style().Name : std::string("Standard"); }

        void CycleStyle()
        {
            auto& scene = m_ctx->scene;
            const auto& recs = scene.GetMLineStyleTable().Records();
            if (recs.empty()) return;
            size_t cur = 0;
            for (size_t i = 0; i < recs.size(); ++i)
                if (recs[i].Id == scene.GetCurrentMLineStyle()) cur = i;
            scene.SetCurrentMLineStyle(recs[(cur + 1) % recs.size()].Id);
        }

        void Refresh(const Math::Point3& cursor)
        {
            m_ctx->overlay.Clear();
            if (m_pts.empty()) return;

            std::vector<Math::Point3> v = m_pts;
            v.push_back(cursor);
            const auto g = MLineEntity::Compute(v, Style(), m_settings.Justify, m_settings.Scale, false);
            const Math::Color4 c{ 1.0, 1.0, 1.0, 1.0 };
            for (const auto& el : g.Elements)
                for (size_t i = 0; i + 1 < el.size(); ++i)
                    m_ctx->overlay.AddLine(el[i], el[i + 1], c);
            // 路径本身用淡色标出，便于看对正位置
            for (size_t i = 0; i + 1 < g.Path.size(); ++i)
                m_ctx->overlay.AddLine(g.Path[i], g.Path[i + 1], { 0.5, 0.5, 0.5, 0.6 });
        }

        void Commit(bool closed)
        {
            auto id = m_ctx->scene.NextObjectID();
            auto ml = std::make_unique<MLineEntity>(id, m_pts, Style(), m_settings.Justify, m_settings.Scale, closed);
            m_ctx->ApplyCurrentAttr(*ml);
            m_ctx->cmdStack.Execute(std::make_unique<AddEntityCommand>(std::move(ml)), m_ctx->scene);
            LOG_DEBUG("[MLineTool] 多线 Id=%d (%zu 点%s)", static_cast<int>(id), m_pts.size(), closed ? "，闭合" : "");
        }

        void Finish()
        {
            m_pts.clear();
            m_ctx->overlay.Clear();
            if (OnFinished) OnFinished();
        }

    private:
        const EditorContext*      m_ctx = nullptr;
        Overlay*              m_overlay = nullptr;
        MLineSettings&            m_settings;
        std::vector<Math::Point3> m_pts;
        Math::Point3              m_cursor{};
    };
}

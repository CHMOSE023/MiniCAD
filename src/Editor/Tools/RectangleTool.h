#pragma once
#include "Scene/Scene.h"
#include "Editor/Tools/ITool.h"
#include "Editor/Overlay/Overlay.h"
#include "Editor/Viewport/Viewport.h"
#include "Document/CommandStack/CommandStack.h"
#include "Document/Command/AddEntityCommand.h"
#include "Core/Math/Point3.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include <cstdio>

namespace MiniCAD
{
    class RectangleTool : public ITool
    {
    public:
        RectangleTool(Scene& scene, CommandStack& cmdStack, Viewport& viewport, Overlay& overlay)
            : m_scene(scene)
            , m_cmdStack(cmdStack)
            , m_viewport(viewport)
            , m_overlay(overlay)
        {
            printf("[RectangleTool] 左键第一角点 | 左键第二角点确认 | 右键/ESC 退出\n");
        }

        ~RectangleTool()
        {
            printf("exit RectangleTool\n");
        }

        bool OnInput(const InputEvent& e) override
        {
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
                m_overlay.Clear();
                m_overlay.AddRect(m_firstCorner, cursor, { 1.0, 1.0, 1.0, 1.0 });
                return false;
            }

            if (e.IsRightClick() || e.IsCancel())
            {
                m_overlay.Clear();
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }

            return false;
        }

        // ── 锚点 ─────────────────────────────────────────────────────────
        //
        // 矩形本身是轴对齐的，正交约束对其没有意义：
        // 正交会把第二角点投影到水平或垂直轴上，使矩形高度或宽度为 0，
        // 退化为一条线。因此始终返回 false，让正交系统跳过本工具。
        //
        // 几何捕捉（端点 / 中点 / 最近点）不受影响，
        // 因为捕捉走 InjectSnap 通道，与正交相互独立。 
        bool HasAnchor() const override { return false; }

        Math::Point3 GetAnchor() const override { return m_firstCorner; }

        void OnSceneChanged() override { Reset(); }

    private:

        Math::Point3 GetPoint(const InputEvent& e) const
        {
            if (e.HasSnap) return e.SnapWorld;
            return m_viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        void Commit(const Math::Point3& a, const Math::Point3& b)
        {
            auto id   = m_scene.NextObjectID();
            auto rect = std::make_unique<RectangleEntity>(id, a, b);
            auto cmd  = std::make_unique<AddEntityCommand>(std::move(rect));
            m_cmdStack.Execute(std::move(cmd), m_scene);

            printf("[RectangleTool] 矩形 Id=%d  (%.3f,%.3f)-(%.3f,%.3f)\n",
                   static_cast<int>(id), a.x, a.y, b.x, b.y);
        }

        void Reset()
        {
            m_hasStart    = false;
            m_firstCorner = {};
            m_overlay.Clear();
        }

    private:
        Scene&        m_scene;
        CommandStack& m_cmdStack;
        Viewport&     m_viewport;
        Overlay&      m_overlay;

        bool         m_hasStart    = false;
        Math::Point3 m_firstCorner{};
    };
}
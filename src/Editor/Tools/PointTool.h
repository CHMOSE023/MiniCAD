#pragma once 
#include "Scene/Scene.h"
#include "Document/CommandStack/CommandStack.h"
#include "Document/Command/AddEntityCommand.h"
#include "Editor/Tools/ITool.h"
#include "Editor/Viewport/Viewport.h"
#include "Editor/Overlay/Overlay.h" 
#include "Core/Entity/PointEntity.hpp"
#include <cstdio>
#include <DirectXMath.h>

namespace MiniCAD
{
    using namespace DirectX;

    class PointTool : public ITool
    {
    public:
        PointTool(Scene& scene, CommandStack& cmdStack, Viewport& viewport, Overlay& overlay)
            : m_scene(scene)
            , m_cmdStack(cmdStack)
            , m_viewport(viewport)
            , m_overlay(overlay)
        {
            printf("[PointTool] 左键放点 | 右键退出\n");
        }

        ~PointTool()
        {
            printf("退出点绘制工具\n");
        }

        bool OnInput(const InputEvent& e) override
        {
            // ─────────────────────────────
            // 左键：创建点
            // ─────────────────────────────
            if (e.IsLeftClick())
            {
                auto pt = GetPoint(e);
                Commit(pt);
                return true;
            }

            // ─────────────────────────────
            // 右键 / ESC：退出工具
            // ─────────────────────────────
            if (e.IsRightClick() || e.IsCancel())
            {
                m_overlay.Clear();
                if (OnFinished) OnFinished();
                return true;
            }

            // ─────────────────────────────
            // 鼠标移动：预览点
            // ─────────────────────────────
            if (e.Type == InputEventType::MouseMove)
            {
                auto pt = GetPoint(e);

                m_overlay.Clear();  

                m_overlay.AddPoint(pt, { 0.6,0.6,0.6,0.6 });// 位置和颜色 预览 

                return false;
            }

            return false;
        }

        // ─────────────────────────────
        // ITool 接口
        // ─────────────────────────────
        bool HasAnchor() const override
        {
            return false; // PointTool 没有持续锚点
        }

        DirectX::XMFLOAT3 GetAnchor() const override
        {
            return XMFLOAT3(0.f, 0.f, 0.f);
        }

    private:

        DirectX::XMFLOAT3 GetPoint(const InputEvent& e)
        {
            if (e.HasSnap)
                return e.SnapWorld;

            return m_viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        void Commit(const XMFLOAT3& p)
        {
            auto id = m_scene.NextObjectID();

            auto pointEntity = std::make_unique<PointEntity>(id, p);

            auto cmd = std::make_unique<AddEntityCommand>(std::move(pointEntity));
            m_cmdStack.Execute(std::move(cmd), m_scene);

            printf("点 Id %d  (%.3f, %.3f, %.3f)\n", static_cast<int>(id), p.x, p.y, p.z);
        }

    private:
        Scene&        m_scene;
        CommandStack& m_cmdStack;
        Viewport&     m_viewport;
        Overlay&      m_overlay;
    };
}
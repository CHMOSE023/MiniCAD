#pragma once
#include "Scene/Scene.h" 
#include "Editor/Tools/ITool.h" 
#include "Editor/Viewport/Viewport.h"
#include "Document/CommandStack/CommandStack.h" 
#include "Document/Command/AddEntityCommand.h" 
#include "Core/Math/Point3.hpp"
#include <cstdio> 
#include <optional>   // 可选值容器
namespace MiniCAD
{ 

    class LineTool : public ITool
    {
    public:
        LineTool(Scene& scene, CommandStack& cmdStack, Viewport& viewport, Overlay& overlay)
            : m_scene(scene)
            , m_cmdStack(cmdStack)
            , m_viewport(viewport)
            , m_overlay(overlay)
        {
            printf("[LineTool] 左键起点 | 左键延续 | 右键结束段 | 空格继续 | ESC 退出\n");
        } 
        ~LineTool()
        {
            printf("退出绘制\n");
        }

        bool OnInput(const InputEvent& e) override
        {
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
                    m_start = pt; // 连续画
                }
                return true;
            }

            if (e.IsRightClick() || e.IsCancel()) // 结束绘制
            {
                m_overlay.Clear(); 
                if (OnFinished) OnFinished();
                return true;
            }

            if (e.Type == InputEventType::MouseMove && m_hasStart)
            {
                m_preview = GetPoint(e); // 预览终点
                m_overlay.Clear(); 
                m_overlay.AddLine(m_start, m_preview, { 0.6,0.6,0.6,0.6 }); 
                return false; // 交给渲染
            }

            return false;
        }

        // 是否有“锚点”
        bool HasAnchor() const override
        {
            return m_hasStart;
        }

        // 返回锚点
        Math::Point3 GetAnchor() const override
        {
            return Math::Point3(m_start.x, m_start.y, 0.f);
        }

    private: 

        Math::Point3 GetPoint(const InputEvent& e)
        {
            if (e.HasSnap) return e.SnapWorld;   // 获取捕获点

            return m_viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
        }

        void Commit(const Math::Point3& a, const Math::Point3& b)
        {
            auto id   = m_scene.NextObjectID(); 
            auto line = std::make_unique<LineEntity>(id, a, b); 
            auto cmd  = std::make_unique<AddEntityCommand>(std::move(line));

            m_cmdStack.Execute(std::move(cmd), m_scene);

            printf("线段 Id %d  (%.3f,%.3f) (%.3f,%.3f)\n",static_cast<int>(id), a.x, a.y, b.x, b.y);
        }
         
    private:
        Scene&        m_scene;
        CommandStack& m_cmdStack;
        Viewport&     m_viewport;
        Overlay&      m_overlay;  
        bool          m_hasStart = false;
        Math::Point3  m_start{};
        Math::Point3  m_preview{};  // 动态预览（MiniCAD关键）

    };
}
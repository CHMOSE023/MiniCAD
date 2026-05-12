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
#include <optional>   // 可选值容器

namespace MiniCAD
{ 
    class RectangleTool : public ITool
    {
    public:
        RectangleTool(Scene& scene, CommandStack& cmdStack,  Viewport& viewport, Overlay& overlay)
            : m_scene(scene)
            , m_cmdStack(cmdStack)
            , m_viewport(viewport)
            , m_overlay(overlay)
        {
            printf("[RectangleTool]  \n");
        }
        ~RectangleTool()
        {
            printf("exit RectangleTool \n");
        }
  
        bool OnInput(const InputEvent& e) override
        {
			if (e.IsLeftClick())       // 左键点击
            {
                auto pt = GetPoint(e);

                if (!m_hasStart)
                {
                    // 第一步：确定起点
                    m_firstCorner = pt;
                    m_hasStart    = true;
                }
                else
                {
					// 第二步：确定对角点并提交
                    Commit(m_firstCorner, pt);

                    m_firstCorner = pt; // 连续画

                    Reset();
                }
				return true;
            }

			// 预览矩形（仅在起点已定时） 
            if (e.Type == InputEventType::MouseMove && m_hasStart)
            {
                auto  cursor = GetPoint(e);  
                m_overlay.Clear();  
				m_overlay.AddRect(m_firstCorner, cursor, { 1.0,1.0,1.0,1.0 });
                return false; // 交给渲染
            }

			if (e.IsRightClick() || e.IsCancel()) // 右键点击或 ESC 取消
            {
                m_overlay.Clear();
                Reset();
                if (OnFinished) OnFinished();
                return true;
            }
        }

        // 是否有“锚点”
        bool HasAnchor() const override
        {
            return m_hasStart;
        }

        // 返回锚点
        Math::Point3 GetAnchor() const override
        {
            return Math::Point3(m_firstCorner.x, m_firstCorner.y, 0.f);
        }

        // ── Undo / Redo 后重置状态 ────────────────────────────────────────
        void OnSceneChanged() override
        {
            Reset();
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
            auto rect = std::make_unique<RectangleEntity>(id, a, b);
            auto cmd  = std::make_unique<AddEntityCommand>(std::move(rect));  

            m_cmdStack.Execute(std::move(cmd), m_scene); 
            
        }

        // ── 重置工具状态（不退出工具，可继续画下一个矩形）──────────────────
        void Reset()
        {
            m_hasStart    = false;
            m_firstCorner = {};

            m_overlay.Clear();
        }
         
        Scene&        m_scene;
        CommandStack& m_cmdStack;
        Viewport&     m_viewport;
        Overlay&      m_overlay;

        bool          m_hasStart = false;
        Math::Point3  m_firstCorner{};
        Math::Point3  m_preview{};     // 动态预览（MiniCAD关键）
         
    };

} 

#pragma once
#include "App/Abstractions/ITool.h"
#include "App/Abstractions/IViewContext.h"
#include "App/Scene/Scene.h"
#include "App/CommandStack/CommandStack.h"
#include "App/Input/InputEvent.h"
#include "Core/Entity/LineEntity.hpp"
#include "App/Command/AddEntityCommand.h"
#include "Core/Object/ObjectIDGenerator.hpp"
#include "App/Preview/PreviewPrimitive.h"
#include <optional>

namespace MiniCAD
{
    class LineTool : public ITool
    {
    public:
        LineTool(Scene* scene, CommandStack* cmd, IViewContext* view)
            : m_scene(scene)
            , m_cmd(cmd)
            , m_view(view)
        {
            printf("[LineTool] 左键起点 | 左键延续 | 右键结束段 | 空格继续 | ESC 退出\n");
        }

        ~LineTool()
        {
            m_view->ClearPreview();
            printf("[LineTool] 已退出\n");
        }

        void OnMouseDown(const InputEvent& e) override
        {
            if (e.button == MouseButton::Left)
            {
                auto pt = ScreenToWorld(e);

                if (m_state == State::WaitFirstPoint)
                {
                    m_start = pt;
                    m_state = State::Drawing;
                    printf("[LineTool] 起点 (%.2f, %.2f)\n", pt.x, pt.y);
                }
                else if (m_state == State::Drawing)
                {
                    CommitLine(m_start, pt);
                    m_start = pt; // 终点延续为下一段起点
                }
            }
            else if (e.button == MouseButton::Right)
            {
                // 右键：结束当前连续段，等待新起点
                if (m_state == State::Drawing)
                {
                    m_view->ClearPreview();
                    m_state = State::WaitFirstPoint;
                    printf("[LineTool] 右键结束，等待新起点\n");
                }
            }
        }

        void OnMouseUp(const InputEvent& e) override
        {
            // 连续绘制模式，MouseUp 不处理
        }

        void OnMouseMove(const InputEvent& e) override
        {
            if (m_state == State::Drawing)
            {
                UpdatePreview(ScreenToWorld(e));
            }
        }

        void OnKeyDown(const InputEvent& e) override
        {
            switch (e.keyCode)
            {
            case VK_ESCAPE:
                // 完全退出，清空所有状态，由 Editor 销毁 Tool
                m_view->ClearPreview();
                m_lastCommittedPoint.reset();
                m_state = State::WaitFirstPoint;
                printf("[LineTool] ESC 完全退出\n");
                break;

            case VK_SPACE:
                // 从上一个提交点继续绘制
                if (m_lastCommittedPoint.has_value())
                {
                    m_start = m_lastCommittedPoint.value();
                    m_state = State::Drawing;
                    printf("[LineTool] 空格继续，从 (%.2f, %.2f) 开始\n",
                        m_start.x, m_start.y);
                }
                else
                {
                    printf("[LineTool] 没有上一个点\n");
                }
                break;
            }
        }

        void Cancel() override
        {
            m_view->ClearPreview();
            m_lastCommittedPoint.reset();
            m_state = State::WaitFirstPoint;
        }

    private:
        enum class State { WaitFirstPoint, Drawing };

        State                    m_state = State::WaitFirstPoint;
        XMFLOAT3                 m_start = {};
        std::optional<XMFLOAT3>  m_lastCommittedPoint;

        Scene* m_scene;
        IViewContext* m_view;
        CommandStack* m_cmd;

        // ── 工具函数 ──────────────────────────────────

        XMFLOAT3 ScreenToWorld(const InputEvent& e)
        {
            auto p = m_view->ScreenToWorld((float)e.mouseX, (float)e.mouseY);
            return XMFLOAT3(p.x, p.y, 0.f); // CAD 永远在 XY 平面，强制 Z=0
        }

        void CommitLine(const XMFLOAT3& start, const XMFLOAT3& end)
        {
            m_view->ClearPreview();

            auto id = ObjectIDGenerator::Get().Next();
            auto line = std::make_unique<LineEntity>(id, start, end);
            line->GetAttr().Visible = true;
            line->GetAttr().Color = XMFLOAT4(1.f, 1.f, 0.f, 1.f);

            auto cmd = std::make_unique<AddEntityCommand>(std::move(line));
            m_cmd->Execute(std::move(cmd), *m_scene);

            m_lastCommittedPoint = end;

            printf("[LineTool] 提交 (%.2f,%.2f) → (%.2f,%.2f)\n", start.x, start.y, end.x, end.y);
        }

        void UpdatePreview(const XMFLOAT3& end)
        {
            PreviewPrimitive p;

            p.Type   = PreviewPrimitiveType::LineList;
            p.Points = { m_start, end };
            p.Color  = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.f);

            m_view->SetPreview(std::move(p));
        }
    };
}
 
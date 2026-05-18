#include "EditorContext.h"
#include "Document/CommandStack/CommandStack.h"
#include "Document/Command/BatchDeleteCommand.h"
#include "Editor/Viewport/Viewport.h"
#include "Editor/Overlay/Overlay.h"
#include "Editor/Picking/Picking.h"
#include "Editor/Snap/SnapResult.h"
#include "Editor/Snap/SnapEngine.h"
#include "Editor/Input/KeyCode.h"
#include "Scene/Scene.h"
#include "Core/Math/Point3.hpp"

// ── 绘制工具 ──────────────────────────────────────────────────
#include "Editor/Tools/LineTool.h"
#include "Editor/Tools/PointTool.h"
#include "Editor/Tools/CircleTool.h"
#include "Editor/Tools/RectangleTool.h"
#include "Editor/Tools/ArcTool.h"
#include "Editor/Tools/EllipseTool.h"
#include "Editor/Tools/PolylineTool.h"
#include "Editor/Tools/SplineTool.h"

// ── 编辑工具 ──────────────────────────────────────────────────
//#include "Editor/Tools/Modify/MoveTool.h"
//#include "Editor/Tools/Modify/CopyTool.h"
//#include "Editor/Tools/Modify/MirrorTool.h"
//#include "Editor/Tools/Modify/RotateTool.h"

// ── 几何编辑工具 ──────────────────────────────────────────────
//#include "Editor/Tools/Modify/TrimTool.h"
//#include "Editor/Tools/Modify/ExtendTool.h"
//#include "Editor/Tools/Modify/BreakTool.h"

#include <cstdio>
#include <memory> 

namespace MiniCAD
{
    // ─────────────────────────────────────────────────────────────
    //  构造
    // ─────────────────────────────────────────────────────────────
    EditorContext::EditorContext(Scene&        scene,
                                 CommandStack& cmdStack,
                                 Viewport&     viewport,
                                 Overlay&      overlay,
                                 Picking&      picking,
                                 SnapEngine&   snap,
                                 SnapResult&   currentSnap)
        : m_scene(scene)
        , m_cmdStack(cmdStack)
        , m_viewport(viewport)
        , m_overlay(overlay)
        , m_picking(picking)
        , m_snap(snap)
        , m_currentSnap(currentSnap)
        , m_gripEditor(viewport, scene, cmdStack, picking, overlay)
        , m_anchorLine({}, {})
    {
        RegisterBuiltinTools();
    }

    // ─────────────────────────────────────────────────────────────
    //  RegisterBuiltinTools
    //  内置工具 + 快捷键在这里统一注册。
    //  新增内置工具只改这一个函数，EditorContext.h 不需要动。
    // ─────────────────────────────────────────────────────────────
    void EditorContext::RegisterBuiltinTools()
    {
        // ── 绘制工具 ──────────────────────────────────────────
        RegisterTool("Line",      [this] { return std::make_unique<LineTool>     (m_scene, m_cmdStack, m_viewport, m_overlay);});
        RegisterTool("Point",     [this] { return std::make_unique<PointTool>    (m_scene, m_cmdStack, m_viewport, m_overlay);});
        RegisterTool("Circle",    [this] { return std::make_unique<CircleTool>   (m_scene, m_cmdStack, m_viewport, m_overlay);});
        RegisterTool("Rectangle", [this] { return std::make_unique<RectangleTool>(m_scene, m_cmdStack, m_viewport, m_overlay);});
        RegisterTool("Arc",       [this] { return std::make_unique<ArcTool>      (m_scene, m_cmdStack, m_viewport, m_overlay);});
        RegisterTool("Ellipse",   [this] { return std::make_unique<EllipseTool>  (m_scene, m_cmdStack, m_viewport, m_overlay);});
        RegisterTool("Polyline",  [this] { return std::make_unique<PolylineTool> (m_scene, m_cmdStack, m_viewport, m_overlay);});
        RegisterTool("Spline",    [this] { return std::make_unique<SplineTool>   (m_scene, m_cmdStack, m_viewport, m_overlay);});

        /*
      
        // ── 编辑工具 ──────────────────────────────────────────
        // 工厂内部检查选择集，空时返回 nullptr，ActivateToolById 会静默忽略
        RegisterTool("Move", [this] -> std::unique_ptr<ITool> {
            auto targets = GetSelectedObjects();
            if (targets.empty()) return nullptr;
            return std::make_unique<MoveTool>(targets, m_scene, m_cmdStack, m_viewport, m_overlay);
        });
        RegisterTool("Copy", [this] -> std::unique_ptr<ITool> {
            auto targets = GetSelectedObjects();
            if (targets.empty()) return nullptr;
            return std::make_unique<CopyTool>(targets, m_scene, m_cmdStack, m_viewport, m_overlay);
        });
        RegisterTool("Mirror", [this] -> std::unique_ptr<ITool> {
            auto targets = GetSelectedObjects();
            if (targets.empty()) return nullptr;
            return std::make_unique<MirrorTool>(targets, m_scene, m_cmdStack, m_viewport, m_overlay);
        });
        RegisterTool("Rotate", [this] -> std::unique_ptr<ITool> {
            auto targets = GetSelectedObjects();
            if (targets.empty()) return nullptr;
            return std::make_unique<RotateTool>(targets, m_scene, m_cmdStack, m_viewport, m_overlay);
        });

        // ── 几何编辑工具 ──────────────────────────────────────
        RegisterTool("Trim", [this]{
            return std::make_unique<TrimTool>(m_scene, m_cmdStack, m_viewport, m_overlay);
        });
        RegisterTool("Extend", [this]{
            return std::make_unique<ExtendTool>(m_scene, m_cmdStack, m_viewport, m_overlay);
        });
        RegisterTool("Break", [this]{
            return std::make_unique<BreakTool>(m_scene, m_cmdStack, m_viewport, m_overlay);
        });
        
        */

        // ── 快捷键绑定 ──────────────────────────────────────── 
        RegisterAlias("P",   "Previous"); // 上次选择
        RegisterAlias("L",   "Line");
        RegisterAlias("LI",  "Line");
        RegisterAlias("REC", "Rectangle");
        RegisterAlias("PL",   "Polyline");
        RegisterAlias("PL",  "Polyline");
        RegisterAlias("MI",  "Mirror");
        RegisterAlias("RO",  "Rotate"); 
        RegisterAlias("PT",  "Point");       
        RegisterAlias("C",   "Circle");
        RegisterAlias("ARC", "Arc");
        RegisterAlias("EL",  "Ellipse");
        RegisterAlias("SP",  "Spline");
        RegisterAlias("M",   "Move");
        RegisterAlias("CO",  "Copy");
        RegisterAlias("MI",  "Mirror");
        RegisterAlias("RO",  "Rotate");
        RegisterAlias("TR",  "Trim");
        RegisterAlias("EX",  "Extend");
        RegisterAlias("BR",  "Break");
    }

    // ─────────────────────────────────────────────────────────────
    //  工具注册表 — 对外接口
    // ─────────────────────────────────────────────────────────────
    void EditorContext::RegisterTool(const std::string& toolId, std::function<std::unique_ptr<ITool>()> factory)
    {
        m_toolRegistry[toolId] = std::move(factory);
    }
       
    void EditorContext::RegisterAlias(const std::string& alias, const std::string& toolId)
    {
        m_aliasRegistry[alias] = toolId;
    }

    void EditorContext::ActivateToolByAlias(const std::string& alias)
    {
        // 特殊命令：恢复上次选择
        if (alias == "Previous" || alias == "PREVIOUS")
        {
            m_picking.RestoreLastSelection();
            m_gripEditor.MarkDirty();
            return;
        }

        auto it = m_aliasRegistry.find(alias);
        if (it != m_aliasRegistry.end())
        {
            ActivateToolById(it->second);
            return;
        }

        // 找不到别名，直接尝试作为 toolId
        if (m_toolRegistry.contains(alias))
        {
            ActivateToolById(alias);
            return;
        }

        printf("[Editor] Unknown command: %s\n", alias.c_str());
    }

    char EditorContext::ToCommandChar(KeyCode key)
    {
        if (key >= KeyCode::A && key <= KeyCode::Z)
        {
            return static_cast<char>('A' + (static_cast<int>(key) - static_cast<int>(KeyCode::A)));
        }

        if (key >= KeyCode::Num0 && key <= KeyCode::Num9)
        {
            return static_cast<char>('0' + (static_cast<int>(key) - static_cast<int>(KeyCode::Num0)));
        }

        return '\0';
    }
     
    void EditorContext::ActivateToolById(const std::string& toolId)
    { 
        // 特殊命令拦截
        if (toolId == "Previous")
        {
            m_picking.RestoreLastSelection();
            m_gripEditor.MarkDirty();
            return;
        }

        auto it = m_toolRegistry.find(toolId);
        if (it == m_toolRegistry.end())
        {
            printf("[Editor] Unknown tool: %s\n", toolId.c_str());
            return;
        }

        auto tool = it->second();   // 调工厂

        if (!tool)                  // 工厂返回 nullptr（如编辑类工具选择集为空）
        {
            printf("[Editor] Tool '%s' has no targets, skipped.\n", toolId.c_str());
            return;
        }

        printf("[Editor] Start %s\n", toolId.c_str());
        m_lastCommand = toolId;
        ActivateTool(std::move(tool));
    }

    // ─────────────────────────────────────────────────────────────
    //  ActivateTool — 工具切换核心（私有）
    //  所有工具启动前的 boilerplate 集中在这里。
    // ─────────────────────────────────────────────────────────────
    void EditorContext::ActivateTool(std::unique_ptr<ITool> tool)
    {
        // 1. 停止旧工具
        if (m_tool)
        {
            m_tool->Cancel();
            m_tool.reset();
        }

        // 2. 清理编辑器状态
        m_toolSuspended = false;
        m_overlay.Clear();
        m_picking.ClearSelection();
        m_scene.MarkDirty();
        m_picking.MarkDirty();
        m_gripEditor.RebuildGrips();

        // 3. 接管新工具，注册完成回调
        m_tool = std::move(tool);
        m_tool->OnFinished = [this]()
        {
            m_toolSuspended = false;
            m_overlay.Clear();
            m_tool.reset();
        };
    }

   

    // ─────────────────────────────────────────────────────────────
    //  绘制工具便捷方法
    //  UI 层直接调用，内部只委托给 ActivateToolById，不含任何逻辑。
    // ─────────────────────────────────────────────────────────────
    void EditorContext::StartLineTool()      { ActivateToolById("Line");      }
    void EditorContext::StartPointTool()     { ActivateToolById("Point");     }
    void EditorContext::StartRectangleTool() { ActivateToolById("Rectangle"); }
    void EditorContext::StartCircleTool()    { ActivateToolById("Circle");    }
    void EditorContext::StartArcTool()       { ActivateToolById("Arc");       }
    void EditorContext::StartEllipseTool()   { ActivateToolById("Ellipse");   }
    void EditorContext::StartPolylineTool()  { ActivateToolById("Polyline");  }
    void EditorContext::StartSplineTool()    { ActivateToolById("Spline");    }

    // ─────────────────────────────────────────────────────────────
    //  编辑工具便捷方法
    // ─────────────────────────────────────────────────────────────
    void EditorContext::StartMoveTool()   { ActivateToolById("Move");   }
    void EditorContext::StartCopyTool()   { ActivateToolById("Copy");   }
    void EditorContext::StartMirrorTool() { ActivateToolById("Mirror"); }
    void EditorContext::StartRotateTool() { ActivateToolById("Rotate"); }

    // ─────────────────────────────────────────────────────────────
    //  几何编辑工具便捷方法
    // ─────────────────────────────────────────────────────────────
    void EditorContext::StartTrimTool()   { ActivateToolById("Trim");   }
    void EditorContext::StartExtendTool() { ActivateToolById("Extend"); }
    void EditorContext::StartBreakTool()  { ActivateToolById("Break");  }

    // ─────────────────────────────────────────────────────────────
    //  OnInput — 消息路由主干
    // ─────────────────────────────────────────────────────────────
    bool EditorContext::OnInput(const InputEvent& inputEvent)
    {
        // ── 1. Snap + 正交约束 ───────────────────────────────────────────
        UpdateSnap(inputEvent);
        InputEvent e = InjectSnap(inputEvent);
        e = ApplyConstraints(e);

        // ── 2. 键盘事件：工具优先，不消费再走全局 ────────────────────────
        //
        //   PolylineTool 等工具需要响应 A / L / C / Z 等按键切换模式。
        //   必须在 HandleGlobal 之前给工具处理机会，
        //   否则 HandleGlobal 的命令缓冲会把这些键吃掉。
        //
        if (e.Type == InputEventType::KeyDown || e.Type == InputEventType::KeyUp)
        {
            if (m_tool && !m_toolSuspended)
            {
                if (m_tool->OnInput(e))
                    return true;    // 工具消费了，结束
            }

            //  交给全局处理 
            if (HandleGlobal(e))
                return true;

            // HandleGlobal 不消费时，让 Picking 处理（如 Esc 清空选择）
            if (m_picking.OnInput(e))
            {
                m_gripEditor.MarkDirty();
                return true;
            }

            return false;
        }

        // ── 3. 鼠标事件：全局快捷键（中键/滚轮）→ 工具 → 夹点 → 选择 ────
        if (HandleGlobal(e))
            return true;

        if (m_tool && !m_toolSuspended)
            return m_tool->OnInput(e);

        if (m_gripEditor.OnInput(e))
            return true;

        if (!m_gripEditor.IsDragging())
        {
            if (m_picking.OnInput(e))
            {
                m_gripEditor.MarkDirty();
                return true;
            }
        }

        return HandleDefault(e);
    }

    // ─────────────────────────────────────────────────────────────
    //  HandleGlobal
    // ─────────────────────────────────────────────────────────────
    bool EditorContext::HandleGlobal(const InputEvent& e)
    {
        // ── Undo ────────────────────────────────────────────────
        if (e.IsUndo())
        { 
            if (m_tool) m_tool->OnSceneChanged();   // !!!先通知 tool：Scene 即将被修改，清理依赖 Scene 的中间状态

            m_cmdStack.Undo(m_scene);
            m_gripEditor.MarkDirty();
            m_scene.MarkDirty();
            return true;
        }

        // ── Redo ────────────────────────────────────────────────
        if (e.IsRedo())
        {
            if (m_tool) m_tool->OnSceneChanged();

            m_cmdStack.Redo(m_scene);
            m_gripEditor.MarkDirty();
            m_scene.MarkDirty();
            return true;
        }

        // ── Cancel ──────────────────────────────────────────────
        if (e.IsCancel())
        {
            if (m_tool)
            {
                m_tool->Cancel();
                m_tool.reset();
                m_toolSuspended = false;
                m_overlay.Clear();
                return true;
            }
            else if (m_gripEditor.IsDragging())
            {
                m_gripEditor.CancelDrag();
                m_gripEditor.MarkDirty();
                return true;
            }

            return false;
        }

        if (e.Type == InputEventType::KeyDown)
        {  
			// A~Z 的按键进入命令缓冲，直到 Enter / Space 触发工具切换，或 Escape 清空缓冲。
            if (e.Key >= KeyCode::A && e.Key <= KeyCode::Z)
            {
                m_cmdBuffer += ToCommandChar(e.Key);   
                printf("m_cmdBuffer: %s",m_cmdBuffer.c_str()); 
                return true;
            }

            if (e.Key == KeyCode::Enter || e.Key == KeyCode::Space)
            {
                printf("[Editor] Enter/Space: cmdBuffer='%s' lastCommand='%s'\n",
                    m_cmdBuffer.c_str(), m_lastCommand.c_str());

                if (!m_cmdBuffer.empty())   // 当前命令
                {
                    ActivateToolByAlias(m_cmdBuffer);
                    m_lastCommand = m_cmdBuffer;  
                    m_cmdBuffer.clear(); 

                    return true;
                }

				if (!m_lastCommand.empty()) // 重复上一个命令
                {
                    ActivateToolByAlias(m_lastCommand);
                    return true;
                }
                return true;
            }

            if (e.Key == KeyCode::Escape)
            {
                m_cmdBuffer.clear();   // 清空缓冲 

            }

            if (e.Key == KeyCode::Delete)
            {
                if (m_tool) m_tool->OnSceneChanged();

                DeleteSelected();
                m_gripEditor.RebuildGrips();
                return true;
            }
  
            if (e.Key == KeyCode::F3) // 捕捉开关  
            {
                ToggleSnap();
                return true;
            }
             
            if (e.Key == KeyCode::F8) // 正交开关 
            {
                ToggleOrtho();
                return true;
            }
        }
       

        // ── 中键平移：通知 tool Suspend / Resume ────────────────
        if (e.Type == InputEventType::MouseButtonDown && e.Button == MouseButton::Middle)
        {
            if (m_tool && !m_toolSuspended)
            {
                m_tool->OnFocusLost();
                m_toolSuspended = true;
            }
            return true;
        }

        if (e.Type == InputEventType::MouseButtonUp && e.Button == MouseButton::Middle)
        {
            if (m_tool && m_toolSuspended)
            {
                m_toolSuspended = false;
                m_tool->OnFocusRestored();
            }
            return false;   // 不吃掉，让后续系统也能感知中键释放
        }

        if (e.Type == InputEventType::MouseMove && e.IsMouseButtonDown(MouseButton::Middle))
        {
            m_viewport.Pan(e.MouseX - e.LastMouseX, e.MouseY - e.LastMouseY);
            return true;
        }

        // ── 滚轮缩放 ────────────────────────────────────────────
        if (e.Type == InputEventType::MouseWheel)
        {
            m_viewport.Zoom(e.WheelDelta, e.MouseX, e.MouseY);
            return true;
        }

        return false;
    }

    bool EditorContext::HandleDefault(const InputEvent& /*e*/)
    {
        return false;
    }

    // ─────────────────────────────────────────────────────────────
    //  Picking / 选择
    // ─────────────────────────────────────────────────────────────
    const std::unordered_set<Object::ObjectID>& EditorContext::GetSelection()
    {
        return m_picking.GetSelection();
    }

    const std::unordered_set<Object::ObjectID>& EditorContext::GetHovered()
    {
        return m_picking.GetHovered();
    }

    Object* EditorContext::GetPrimarySelectedObject()
    {
        const auto& sel = m_picking.GetSelection();
        if (sel.empty()) return nullptr;
        return m_scene.GetEntity(*sel.begin());
    }

    std::vector<Object*> EditorContext::GetSelectedObjects()
    {
        std::vector<Object*> result;
        for (auto id : m_picking.GetSelection())
        {
            if (auto* obj = m_scene.GetEntity(id))
                result.push_back(obj);
        }
        return result;
    }

    // ─────────────────────────────────────────────────────────────
    //  删除
    // ─────────────────────────────────────────────────────────────
    void EditorContext::DeleteSelected()
    {
        auto& ids = m_picking.GetSelection();
        if (ids.empty()) return;

        std::vector<Object::ObjectID> idsVec(ids.begin(), ids.end());
        auto cmd = std::make_unique<BatchDeleteCommand>(idsVec);
        m_cmdStack.Execute(std::move(cmd), m_scene);
    }

    // ─────────────────────────────────────────────────────────────
    //  Snap
    // ─────────────────────────────────────────────────────────────
    void EditorContext::UpdateSnap(const InputEvent& e)
    {
        if (!ShouldSnap()) return;

        const Camera& cam     = m_viewport.GetCamera();
        const auto&   exclude = m_gripEditor.IsDragging()
            ? m_picking.GetSelection()
            : std::unordered_set<Object::ObjectID>{};

        switch (e.Type)
        {
        case InputEventType::MouseMove:
        case InputEventType::MouseButtonDown:
        case InputEventType::MouseButtonUp:
            m_currentSnap = m_snap.Query({ (double)e.MouseX, (double)e.MouseY }, m_scene, cam, exclude);
            break;
        default:
            break;
        }
    }

    bool EditorContext::ShouldSnap() const
    {
        if (!m_snapEnabled)              return false;
        if (m_tool)                      return true;   // 绘制工具激活时捕捉
        if (m_gripEditor.IsDragging())   return true;   // 夹点拖拽时也捕捉
        return false;
    }

    InputEvent EditorContext::InjectSnap(const InputEvent& e)
    {
        InputEvent out = e;
        out.HasSnap    = false;

        if (m_currentSnap.IsValid())
        {
            out.HasSnap   = true;
            out.SnapWorld = m_currentSnap.WorldPos;
        }

        return out;
    }

    // ─────────────────────────────────────────────────────────────
    //  正交约束
    // ─────────────────────────────────────────────────────────────
    InputEvent EditorContext::ApplyConstraints(const InputEvent& e)
    {
        InputEvent out = e;

        if (!m_orthoEnabled)
        {
            m_anchorLine = { {}, {} };
            return out;
        }

        Math::Point3 anchor;
        if (!TryGetAnchor(anchor))
        {
            m_anchorLine = { {}, {} };
            return out;
        }

        Math::Point3 input;
        if (e.HasSnap)
        {
            input = e.SnapWorld;
        }
        else
        {
            auto p = m_viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
            input  = Math::Point3(p.x, p.y, 0.f);
        }

        float dx = input.x - anchor.x;
        float dy = input.y - anchor.y;

        Math::Point3 result;
        if (std::fabs(dx) > std::fabs(dy))
            result = { input.x, anchor.y, 0.f };
        else
            result = { anchor.x, input.y, 0.f };

        out.HasSnap   = true;
        out.SnapWorld = result;
        m_anchorLine  = { anchor, result };

        return out;
    }

    bool EditorContext::TryGetAnchor(Math::Point3& out) const
    {
        if (m_tool && m_tool->HasAnchor())
        {
            out = m_tool->GetAnchor();
            return true;
        }

        if (m_gripEditor.IsDragging())
        {
            // out = m_gripEditor.GetDragBase();
            return true;
        }
        return false;
    }

    // ─────────────────────────────────────────────────────────────
    //  正交 / 捕捉开关
    // ─────────────────────────────────────────────────────────────
    bool EditorContext::IsOrthoEnabled() const { return m_orthoEnabled; }

    void EditorContext::SetOrthoEnabled(bool enabled)
    {
        if (m_orthoEnabled == enabled) return;
        m_orthoEnabled = enabled;
        printf("[Editor] Ortho: %s\n", enabled ? "ON" : "OFF");
    }

    void EditorContext::ToggleOrtho() { SetOrthoEnabled(!m_orthoEnabled); }

    bool EditorContext::IsSnapEnabled() const { return m_snapEnabled; }

    void EditorContext::SetSnapEnabled(bool enabled)
    {
        if (m_snapEnabled == enabled) return;
        m_snapEnabled = enabled;
        printf("[Editor] Snap: %s\n", enabled ? "ON" : "OFF");
    }

    void EditorContext::ToggleSnap() { SetSnapEnabled(!m_snapEnabled); }

    // ─────────────────────────────────────────────────────────────
    //  Undo / Redo / Command
    // ─────────────────────────────────────────────────────────────
    void EditorContext::Undo()
    {
        m_cmdStack.Undo(m_scene);
    }

    void EditorContext::Redo()
    {
        m_cmdStack.Redo(m_scene);
    }

    void EditorContext::ExecuteCommand(std::unique_ptr<ICommand> cmd)
    {
        m_cmdStack.Execute(std::move(cmd), m_scene);
    }

} 

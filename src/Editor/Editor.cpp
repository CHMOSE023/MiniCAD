#include "Editor.h"
#include "Editor/EditorContext.h"
#include "Document/Document.h"
#include "Document/DrawContext.hpp"
#include "Document/CommandStack/CommandStack.h"
#include "Document/Command/BatchDeleteCommand.h"
#include "Document/Command/AddEntityCommand.h"
#include "Viewport/Viewport.h"
#include "Editor/Overlay/Overlay.h"
#include "Editor/Picking/Picking.h"
#include "Editor/Snap/SnapResult.h"
#include "Editor/Snap/SnapEngine.h"
#include "Editor/Input/KeyCode.h"
#include "Scene/Scene.h"
#include "Text/FontSystem.h"
#include "Core/Math/Point3.hpp"
#include "Core/Entity/Entity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"

#ifdef MINICAD_WEB
#include <emscripten.h>
#include "Render/WebGL/WebFontAtlas.hpp"
#else
#include <imgui.h>
#endif

// ── 绘制工具 ──────────────────────────────────────────────────
#include "Editor/Tools/LineTool.h"
#include "Editor/Tools/PointTool.h"
#include "Editor/Tools/CircleTool.h"
#include "Editor/Tools/RectangleTool.h"
#include "Editor/Tools/ArcTool.h"
#include "Editor/Tools/EllipseTool.h"
#include "Editor/Tools/PolylineTool.h"
#include "Editor/Tools/SplineTool.h"
#include "Editor/Tools/TextTool.h"
#include "Editor/Tools/MTextTool.h"
#include "Document/Command/EditTextCommand.h"

// ── 编辑工具 ──────────────────────────────────────────────────
#include "Editor/Tools/Modify/MoveTool.h"
#include "Editor/Tools/Modify/CopyTool.h"
#include "Editor/Tools/Modify/MirrorTool.h"
#include "Editor/Tools/Modify/RotateTool.h"

#include <cstdio>
#include <memory>
#include <algorithm>

namespace MiniCAD
{
    // ─────────────────────────────────────────────────────────────
    //  构造
    // ─────────────────────────────────────────────────────────────
    Editor::Editor()
    {
        RegisterBuiltinTools();
    }

    // ─────────────────────────────────────────────────────────────
    //  Bind / Unbind
    // ─────────────────────────────────────────────────────────────
    void Editor::Bind(Document& doc, Viewport& viewport)
    {
        Unbind();

        m_doc      = &doc;
        m_viewport = &viewport;

        auto& scene    = doc.GetScene();
        auto& cmdStack = doc.GetCommandStack();

        m_overlay.Bind(viewport);
        m_picking.Bind(scene, viewport);
        m_gripEditor.Bind(viewport, scene, cmdStack, m_picking, m_overlay);
    }

    void Editor::Unbind()
    {
        if (m_tool)
        {
            m_tool->Cancel();
            m_tool.reset();
        }
        m_toolSuspended    = false;
        m_pendingToolReset = false;
        m_overlay.Clear();
        m_currentSnap = {};

        m_doc      = nullptr;
        m_viewport = nullptr;
    }

    // ─────────────────────────────────────────────────────────────
    //  RegisterBuiltinTools
    // ─────────────────────────────────────────────────────────────
    void Editor::RegisterBuiltinTools()
    {
        // ── 绘制工具 ──────────────────────────────────────────
        RegisterTool("Line",      [] { return std::make_unique<LineTool>();      });
        RegisterTool("Point",     [] { return std::make_unique<PointTool>();     });
        RegisterTool("Circle",    [] { return std::make_unique<CircleTool>();    });
        RegisterTool("Rectangle", [] { return std::make_unique<RectangleTool>(); });
        RegisterTool("Arc",       [] { return std::make_unique<ArcTool>();       });
        RegisterTool("Ellipse",   [] { return std::make_unique<EllipseTool>();   });
        RegisterTool("Polyline",  [] { return std::make_unique<PolylineTool>();  });
        RegisterTool("Spline",    [] { return std::make_unique<SplineTool>();    });
        RegisterTool("Text",      [this]
        {
            auto tool = std::make_unique<TextTool>();
            tool->OnInsertPointPicked = [this](Math::Point3 pos)
            {
                m_textRequest.Active    = true;
                m_textRequest.InsertPos = pos;
#ifdef MINICAD_WEB
                EM_ASM({ if (typeof window._minicadShowTextInput === 'function') window._minicadShowTextInput(); });
#endif
            };
            return tool;
        });

        RegisterTool("MText", [this]
        {
            auto tool = std::make_unique<MTextTool>();
            tool->OnInsertPointPicked = [this](Math::Point3 pos)
            {
                m_mtextRequest.Active    = true;
                m_mtextRequest.InsertPos = pos;
#ifdef MINICAD_WEB
                EM_ASM({ if (typeof window._minicadShowMTextInput === 'function') window._minicadShowMTextInput(); });
#endif
            };
            return tool;
        });

        // ── 编辑工具 ──────────────────────────────────────────
        RegisterTool("Move", [this]() -> std::unique_ptr<ITool> {
            auto targets = GetSelectedObjects();
            if (targets.empty())
            {
                printf("[Editor] Move: 请先选择对象\n");
                return nullptr;
            }
            return std::make_unique<MoveTool>(std::move(targets));
        });

        RegisterTool("Copy", [this]() -> std::unique_ptr<ITool> {
            auto targets = GetSelectedObjects();
            if (targets.empty())
            {
                printf("[Editor] Copy: 请先选择对象\n");
                return nullptr;
            }
            return std::make_unique<CopyTool>(std::move(targets));
        });

        RegisterTool("Mirror", [this]() -> std::unique_ptr<ITool> {
            auto targets = GetSelectedObjects();
            if (targets.empty())
            {
                printf("[Editor] Mirror: 请先选择对象\n");
                return nullptr;
            }
            return std::make_unique<MirrorTool>(std::move(targets));
        });

        RegisterTool("Rotate", [this]() -> std::unique_ptr<ITool> {
            auto targets = GetSelectedObjects();
            if (targets.empty())
            {
                printf("[Editor] Rotate: 请先选择对象\n");
                return nullptr;
            }
            return std::make_unique<RotateTool>(std::move(targets));
        });

        // ── 快捷键绑定 ────────────────────────────────────────
        RegisterAlias("P",   "Previous");
        RegisterAlias("L",   "Line");
        RegisterAlias("LI",  "Line");
        RegisterAlias("REC", "Rectangle");
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
        RegisterAlias("T",   "Text");
        RegisterAlias("DT",  "Text");
        RegisterAlias("MT",  "MText");
        RegisterAlias("TR",  "Trim");
        RegisterAlias("EX",  "Extend");
        RegisterAlias("BR",  "Break");
    }

    // ─────────────────────────────────────────────────────────────
    //  工具注册表 — 对外接口
    // ─────────────────────────────────────────────────────────────
    void Editor::RegisterTool(const std::string& toolId, std::function<std::unique_ptr<ITool>()> factory)
    {
        m_toolRegistry[toolId] = std::move(factory);
    }

    void Editor::RegisterAlias(const std::string& alias, const std::string& toolId)
    {
        m_aliasRegistry[alias] = toolId;
    }

    void Editor::ActivateToolByAlias(const std::string& alias)
    {
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

        if (m_toolRegistry.contains(alias))
        {
            ActivateToolById(alias);
            return;
        }

        printf("[Editor] Unknown command: %s\n", alias.c_str());
    }

    char Editor::ToCommandChar(KeyCode key)
    {
        if (key >= KeyCode::A && key <= KeyCode::Z)
            return static_cast<char>('A' + (static_cast<int>(key) - static_cast<int>(KeyCode::A)));

        if (key >= KeyCode::Num0 && key <= KeyCode::Num9)
            return static_cast<char>('0' + (static_cast<int>(key) - static_cast<int>(KeyCode::Num0)));

        return '\0';
    }

    void Editor::ActivateToolById(const std::string& toolId)
    {
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

        auto tool = it->second();

        if (!tool)
        {
            printf("[Editor] Tool '%s' has no targets, skipped.\n", toolId.c_str());
            return;
        }

        printf("[Editor] Start %s\n", toolId.c_str());
        m_lastCommand = toolId;
        ActivateTool(std::move(tool));
    }

    // ─────────────────────────────────────────────────────────────
    //  ActivateTool
    // ─────────────────────────────────────────────────────────────
    void Editor::ActivateTool(std::unique_ptr<ITool> tool)
    {
        if (m_tool)
        {
            m_tool->Cancel();
            m_tool.reset();
        }

        m_toolSuspended = false;
        m_overlay.Clear();
        m_picking.ClearSelection();
        m_doc->GetScene().MarkDirty();
        m_picking.MarkDirty();
        m_gripEditor.RebuildGrips();

        m_tool = std::move(tool);
        m_pendingToolReset = false;
        m_tool->OnFinished = [this]()
        {
            m_toolSuspended    = false;
            m_overlay.Clear();
            m_pendingToolReset = true;
        };
    }

    // ─────────────────────────────────────────────────────────────
    //  绘制工具便捷方法
    // ─────────────────────────────────────────────────────────────
    void Editor::StartLineTool()      { ActivateToolById("Line");      }
    void Editor::StartPointTool()     { ActivateToolById("Point");     }
    void Editor::StartRectangleTool() { ActivateToolById("Rectangle"); }
    void Editor::StartCircleTool()    { ActivateToolById("Circle");    }
    void Editor::StartArcTool()       { ActivateToolById("Arc");       }
    void Editor::StartEllipseTool()   { ActivateToolById("Ellipse");   }
    void Editor::StartPolylineTool()  { ActivateToolById("Polyline");  }
    void Editor::StartSplineTool()    { ActivateToolById("Spline");    }
    void Editor::StartTextTool()      { ActivateToolById("Text");      }
    void Editor::StartMTextTool()     { ActivateToolById("MText");     }

    void Editor::SubmitTextInput(const std::string& utf8Text)
    {
        if (!m_textRequest.Active)
            return;

        m_textRequest.Active = false;

        if (utf8Text.empty())
        {
            m_textRequest.EditTargetId = Object::InvalidID;
            return;
        }

        auto& scene    = m_doc->GetScene();
        auto& cmdStack = m_doc->GetCommandStack();

        if (m_textRequest.EditTargetId != Object::InvalidID)
        {
            auto cmd = std::make_unique<EditTextCommand>(
                m_textRequest.EditTargetId,
                m_textRequest.InitialText,
                utf8Text);
            cmdStack.Execute(std::move(cmd), scene);
            m_textRequest.EditTargetId = Object::InvalidID;
            printf("[TextEdit] 文字已修改: %s\n", utf8Text.c_str());
            return;
        }

        const auto& layer = scene.GetLayerManager().GetActiveLayer();
        auto id  = scene.NextObjectID();
        auto ent = std::make_unique<TextEntity>(
            id,
            m_textRequest.InsertPos,
            utf8Text,
            m_textRequest.Height,
            m_textRequest.Rotation);

        EntityAttr attr;
        attr.Color   = layer.GetColor();
        attr.LayerId = layer.GetID();
        ent->SetAttr(attr);

        auto cmd = std::make_unique<AddEntityCommand>(std::move(ent));
        cmdStack.Execute(std::move(cmd), scene);

        printf("[TextTool] 文字已添加: %s\n", utf8Text.c_str());
    }

    void Editor::SubmitMTextInput(const std::string& utf8Text)
    {
        if (!m_mtextRequest.Active)
            return;

        m_mtextRequest.Active = false;

        if (utf8Text.empty())
        {
            m_mtextRequest.EditTargetId = Object::InvalidID;
            return;
        }

        auto& scene    = m_doc->GetScene();
        auto& cmdStack = m_doc->GetCommandStack();

        if (m_mtextRequest.EditTargetId != Object::InvalidID)
        {
            auto cmd = std::make_unique<EditMTextCommand>(
                m_mtextRequest.EditTargetId,
                m_mtextRequest.InitialText,
                utf8Text);
            cmdStack.Execute(std::move(cmd), scene);
            m_mtextRequest.EditTargetId = Object::InvalidID;
            printf("[MTextEdit] 多行文字已修改: %s\n", utf8Text.c_str());
            return;
        }

        const auto& layer = scene.GetLayerManager().GetActiveLayer();
        auto id  = scene.NextObjectID();
        auto ent = std::make_unique<MTextEntity>(
            id,
            0,
            utf8Text,
            m_mtextRequest.InsertPos,
            m_mtextRequest.Height,
            m_mtextRequest.Rotation,
            m_mtextRequest.BoxWidth);

        EntityAttr attr;
        attr.Color   = layer.GetColor();
        attr.LayerId = layer.GetID();
        ent->SetAttr(attr);

        auto cmd = std::make_unique<AddEntityCommand>(std::move(ent));
        cmdStack.Execute(std::move(cmd), scene);

        printf("[MTextTool] 多行文字已添加: %s\n", utf8Text.c_str());
    }

    // ─────────────────────────────────────────────────────────────
    //  编辑工具便捷方法
    // ─────────────────────────────────────────────────────────────
    void Editor::StartMoveTool()   { ActivateToolById("Move");   }
    void Editor::StartCopyTool()   { ActivateToolById("Copy");   }
    void Editor::StartMirrorTool() { ActivateToolById("Mirror"); }
    void Editor::StartRotateTool() { ActivateToolById("Rotate"); }

    // ─────────────────────────────────────────────────────────────
    //  几何编辑工具便捷方法
    // ─────────────────────────────────────────────────────────────
    void Editor::StartTrimTool()   { ActivateToolById("Trim");   }
    void Editor::StartExtendTool() { ActivateToolById("Extend"); }
    void Editor::StartBreakTool()  { ActivateToolById("Break");  }

    // ─────────────────────────────────────────────────────────────
    //  OnInput
    // ─────────────────────────────────────────────────────────────
    bool Editor::OnInput(const InputEvent& inputEvent)
    {
        if (!m_doc || !m_viewport) return false;

        m_mouseX = inputEvent.MouseX;
        m_mouseY = inputEvent.MouseY;

        auto& scene    = m_doc->GetScene();
        auto& cmdStack = m_doc->GetCommandStack();

        if (m_pendingToolReset)
        {
            m_pendingToolReset = false;
            m_tool.reset();
        }

        // 事件副本 e：ctx.event 绑定到它，Resolve 会就地回填捕获点，工具再经 ctx 读取
        InputEvent e = inputEvent;

        EditorContext ctx{
                .event      = e,
                .scene      = scene,
                .viewport   = *m_viewport,
                .snap       = m_snap,
                .constraint = m_constraintEngine,
                .picking    = m_picking,
                .cmdStack   = cmdStack,
                .overlay    = m_overlay,
                .tool       = m_tool.get(),
                .grip       = &m_gripEditor
        };

        m_resolver.Resolve(ctx);
        m_currentSnap = ctx.resolved.hasSnap ? ctx.resolved.snap : SnapResult{};


        if (e.Type == InputEventType::KeyDown || e.Type == InputEventType::KeyUp)
        {
            if (m_tool && !m_toolSuspended)
            {
                if (m_tool->OnInput(ctx))
                    return true;
            }

            if (HandleGlobal(e))
                return true;

            if (m_picking.OnInput(e))
            {
                m_gripEditor.MarkDirty();
                return true;
            }

            return false;
        }

        if (HandleGlobal(e))
            return true;

        if (m_tool && !m_toolSuspended)
            return m_tool->OnInput(ctx);

        if (m_gripEditor.OnInput(ctx.event))
            return true;

        if (!m_gripEditor.IsDragging())
        {
            // 二次点击已选中的文字实体 → 触发编辑（必须在 Picking 消费事件之前检测）
            if (e.IsLeftClick())
            {
                Math::Point2 screenPt{ static_cast<double>(e.MouseX), static_cast<double>(e.MouseY) };
                Object::ObjectID hit = m_picking.HitTest(screenPt, 5.0);
                if (hit != Object::InvalidID && m_picking.GetSelection().count(hit))
                {
                    auto* obj = scene.GetEntity(hit);
                    if (obj && obj->IsKindOf<TextEntity>())
                    {
                        auto* te = static_cast<TextEntity*>(obj);
                        m_textRequest.Active       = true;
                        m_textRequest.EditTargetId = hit;
                        m_textRequest.InitialText  = te->GetText();
                        printf("[Editor] 编辑文字 id=%u\n", static_cast<int>(hit));
#ifdef MINICAD_WEB
                        EM_ASM({
                            var t = UTF8ToString($0);
                            if (typeof window._minicadShowTextEdit === 'function')
                                window._minicadShowTextEdit(t);
                        }, te->GetText().c_str());
#endif
                        return true;
                    }
                    if (obj && obj->IsKindOf<MTextEntity>())
                    {
                        auto* me = static_cast<MTextEntity*>(obj);
                        m_mtextRequest.Active       = true;
                        m_mtextRequest.EditTargetId = hit;
                        m_mtextRequest.InitialText  = me->GetText();
                        printf("[Editor] 编辑多行文字 id=%u\n",  static_cast<int>(hit));
#ifdef MINICAD_WEB
                        EM_ASM({
                            var t = UTF8ToString($0);
                            if (typeof window._minicadShowMTextEdit === 'function')
                                window._minicadShowMTextEdit(t);
                        }, me->GetText().c_str());
#endif
                        return true;
                    }
                }
            }

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
    bool Editor::HandleGlobal(const InputEvent& e)
    {
        auto& scene    = m_doc->GetScene();
        auto& cmdStack = m_doc->GetCommandStack();

        if (e.IsUndo())
        {
            if (m_tool) m_tool->OnSceneChanged();
            cmdStack.Undo(scene);
            m_gripEditor.MarkDirty();
            scene.MarkDirty();
            return true;
        }

        if (e.IsRedo())
        {
            if (m_tool) m_tool->OnSceneChanged();
            cmdStack.Redo(scene);
            m_gripEditor.MarkDirty();
            scene.MarkDirty();
            return true;
        }

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

                if (!m_cmdBuffer.empty())
                {
                    ActivateToolByAlias(m_cmdBuffer);
                    m_lastCommand = m_cmdBuffer;
                    m_cmdBuffer.clear();
                    return true;
                }

                if (!m_lastCommand.empty())
                {
                    ActivateToolByAlias(m_lastCommand);
                    return true;
                }
                return true;
            }

            if (e.Key == KeyCode::Escape)
            {
                m_cmdBuffer.clear();
            }

            if (e.Key == KeyCode::Delete)
            {
                if (m_tool) m_tool->OnSceneChanged();
                DeleteSelected();
                m_gripEditor.RebuildGrips();
                return true;
            }

            if (e.Key == KeyCode::F3)
            {
                ToggleSnap();
                return true;
            }

            if (e.Key == KeyCode::F8)
            {
                ToggleOrtho();
                return true;
            }

            if (e.Key == KeyCode::F10)
            {
                TogglePolar();
                return true;
            }
        }

        // ── 中键平移 ────────────────────────────────────────────
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
            return false;
        }

        if (e.Type == InputEventType::MouseMove && e.IsMouseButtonDown(MouseButton::Middle))
        {
            m_viewport->Pan(e.MouseX - e.LastMouseX, e.MouseY - e.LastMouseY);
            scene.MarkDirty();
            return true;
        }

        // ── 滚轮缩放 ────────────────────────────────────────────
        if (e.Type == InputEventType::MouseWheel)
        {
            m_viewport->Zoom(e.WheelDelta, e.MouseX, e.MouseY);
            scene.MarkDirty();
            return true;
        }

        return false;
    }

    bool Editor::HandleDefault(const InputEvent& /*e*/)
    {
        return false;
    }

    // ─────────────────────────────────────────────────────────────
    //  Picking / 选择
    // ─────────────────────────────────────────────────────────────
    const std::unordered_set<Object::ObjectID>& Editor::GetSelection()
    {
        return m_picking.GetSelection();
    }

    const std::unordered_set<Object::ObjectID>& Editor::GetHovered()
    {
        return m_picking.GetHovered();
    }

    Object* Editor::GetPrimarySelectedObject()
    {
        const auto& sel = m_picking.GetSelection();
        if (sel.empty()) return nullptr;
        return m_doc->GetScene().GetEntity(*sel.begin());
    }

    std::vector<Object*> Editor::GetSelectedObjects()
    {
        std::vector<Object*> result;
        auto& scene = m_doc->GetScene();
        for (auto id : m_picking.GetSelection())
        {
            if (auto* obj = scene.GetEntity(id))
                result.push_back(obj);
        }
        return result;
    }

    // ─────────────────────────────────────────────────────────────
    //  删除
    // ─────────────────────────────────────────────────────────────
    void Editor::DeleteSelected()
    {
        auto& ids = m_picking.GetSelection();
        if (ids.empty()) return;

        std::vector<Object::ObjectID> idsVec(ids.begin(), ids.end());
        auto cmd = std::make_unique<BatchDeleteCommand>(idsVec);
        m_doc->GetCommandStack().Execute(std::move(cmd), m_doc->GetScene());
    }

    // ─────────────────────────────────────────────────────────────
    //  TryGetAnchor
    // ─────────────────────────────────────────────────────────────
    bool Editor::TryGetAnchor(Math::Point3& out) const
    {
        if (m_tool && m_tool->HasAnchor())
        {
            out = m_tool->GetAnchor();
            return true;
        }

        if (m_gripEditor.IsDragging())
        {
            if (const Grip* g = m_gripEditor.GetActiveGrip())
            {
                out = g->WorldPos;
                return true;
            }
            return false;
        }
        return false;
    }

    // ─────────────────────────────────────────────────────────────
    //  约束 / 捕捉开关
    // ─────────────────────────────────────────────────────────────
    bool Editor::IsOrthoEnabled() const { return m_constraintEngine.IsOrthoEnabled(); }

    void Editor::SetOrthoEnabled(bool enabled)
    {
        if (m_constraintEngine.IsOrthoEnabled() == enabled) return;
        m_constraintEngine.EnableOrtho(enabled);
        printf("[Editor] Ortho: %s\n", enabled ? "ON" : "OFF");
    }

    void Editor::ToggleOrtho() { SetOrthoEnabled(!IsOrthoEnabled()); }

    bool Editor::IsPolarEnabled() const { return m_constraintEngine.IsPolarEnabled(); }

    void Editor::SetPolarEnabled(bool enabled)
    {
        if (m_constraintEngine.IsPolarEnabled() == enabled) return;
        m_constraintEngine.EnablePolar(enabled);
        printf("[Editor] Polar: %s (%.1f deg)\n", enabled ? "ON" : "OFF",
               m_constraintEngine.GetPolar().GetAngleDeg());
    }

    void Editor::TogglePolar() { SetPolarEnabled(!IsPolarEnabled()); }

    double Editor::GetPolarAngle() const { return m_constraintEngine.GetPolar().GetAngleDeg(); }

    void Editor::SetPolarAngle(double deg) { m_constraintEngine.GetPolar().SetAngleDeg(deg); }

    bool Editor::IsSnapEnabled() const { return m_snap.IsEnabled(); }

    void Editor::SetSnapEnabled(bool enabled)
    {
        if (m_snap.IsEnabled() == enabled) return;
        m_snap.SetEnableSnap(enabled);
        printf("[Editor] Snap: %s\n", enabled ? "ON" : "OFF");
    }

    void Editor::ToggleSnap() { SetSnapEnabled(!m_snap.IsEnabled()); }

    // ─────────────────────────────────────────────────────────────
    //  Undo / Redo / Command
    // ─────────────────────────────────────────────────────────────
    void Editor::Undo()
    {
        m_doc->GetCommandStack().Undo(m_doc->GetScene());
    }

    void Editor::Redo()
    {
        m_doc->GetCommandStack().Redo(m_doc->GetScene());
    }

    void Editor::ExecuteCommand(std::unique_ptr<ICommand> cmd)
    {
        m_doc->GetCommandStack().Execute(std::move(cmd), m_doc->GetScene());
    }

    // ─────────────────────────────────────────────────────────────
    //  渲染
    // ─────────────────────────────────────────────────────────────
    void Editor::Render()
    {
        if (!m_doc || !m_viewport) return;

        m_overlayVertices.clear();

        UpdateSceneVertices();

        if (m_constraintEngine.IsAnyActive() &&
            (IsActiveTool() || m_gripEditor.IsDragging()))
        {
            const Line& guideLine = GetAnchorLine();
            if (guideLine.IsValid())
                m_overlay.AddLine(guideLine.Start, guideLine.End, { 0.1, 0.7, 0.1, 0.6 });
        }

        m_overlay.ToVertices(m_overlayVertices);

        auto vs = BuildViewState();
        m_viewport->Render(vs);
    }

    void Editor::UpdateSceneVertices()
    {
        auto& scene = m_doc->GetScene();

        if (!scene.IsDirty() && !m_picking.IsDirty())
            return;

        m_sceneVertices.clear();
        m_textVertices.clear();

        const auto& hoverIds     = m_picking.GetHovered();
        const auto& selectionIds = m_picking.GetSelection();

        if (!IsActiveTool() && !m_gripEditor.IsDragging())
        {
            m_overlay.Clear();
            m_gripEditor.RebuildGrips();
        }

#ifndef MINICAD_WEB
        GlyphProvider glyphProvider = [](uint32_t cp, GlyphInfo& out, float& fallback) -> bool
        {
            constexpr float kBakeSize = 128.f;
            ImFontBaked* font = ImGui::GetFont()->GetFontBaked(kBakeSize);
            if (!font || font->Size <= 0.f) { fallback = 0.f; return false; }

            const float inv = 1.f / font->Size;
            fallback = font->FallbackAdvanceX * inv;

            const ImFontGlyph* g = font->FindGlyph(static_cast<ImWchar>(cp));
            if (!g) return false;

            out.X0 = g->X0 * inv;  out.Y0 = g->Y0 * inv;
            out.X1 = g->X1 * inv;  out.Y1 = g->Y1 * inv;
            out.U0 = g->U0;        out.V0 = g->V0;
            out.U1 = g->U1;        out.V1 = g->V1;
            out.AdvanceX = g->AdvanceX * inv;
            return true;
        };
#else
        static WebFontAtlas s_fontAtlas;
        if (!m_fontTexture)
            m_fontTexture = reinterpret_cast<void*>(static_cast<uintptr_t>(s_fontAtlas.GetTexture()));

        GlyphProvider glyphProvider = s_fontAtlas.MakeProvider();
#endif

        FontResolver fontResolver;
        auto* fontSystem = m_doc->GetFontSystem();
        if (fontSystem && fontSystem->IsReady())
        {
            fontResolver = [fontSystem](uint32_t styleId) -> IFont*
            {
                const FontStyle* style = fontSystem->FindStyle(styleId);
                if (!style)
                    return fontSystem->GetTssdSHXCompositeFont();
                return &fontSystem->ResolveFont(*style);
            };
        }

        DrawContext ctx(m_sceneVertices, m_textVertices, m_overlay, glyphProvider, fontResolver);

        scene.ForEachObject([&](const Object& obj)
        {
           if (obj.IsKindOf<Entity>())
           {
               const auto& entity = static_cast<const Entity&>(obj);
               auto isSelected = selectionIds.contains(obj.GetID());
               auto isHovered  = hoverIds.contains(obj.GetID());
               entity.Draw(ctx, isSelected, isHovered);
           }
        });

        scene.ClearDirty();
        m_picking.ClearDirty();
    }

    ViewState Editor::BuildViewState()
    {
        auto& scene = m_doc->GetScene();

        ViewState vs;

        vs.Scene       = m_sceneVertices;
        vs.Overlay     = m_overlayVertices;
        vs.TextScene   = m_textVertices;
        vs.FontTexture = m_fontTexture;

        vs.Selection.Active = m_picking.IsBoxSelecting();
        vs.Selection.Start  = m_picking.GetBoxStart();
        vs.Selection.End    = m_picking.GetBoxEnd();

        vs.MouseX    = static_cast<float>(m_mouseX);
        vs.MouseY    = static_cast<float>(m_mouseY);

        vs.ShowGrid  = true;

        vs.Snap.SnapType = static_cast<SnapDraw::Type>(m_currentSnap.SnapType);
        vs.Snap.Pos      = m_viewport->GetCamera().WorldToScreen(m_currentSnap.WorldPos);
        if (!IsActiveTool())
        {
            m_currentSnap = {};
        }

        vs.ShowCurrorBox = !IsActiveTool();

        vs.ShowGizmo = true;
        m_gripVertices.clear();
        if (vs.ShowGizmo)
        {
            auto& hoveredIdxs = m_gripEditor.HoveredGrips();
            auto& grips       = m_gripEditor.GetGrips();

            for (int i = 0; i < (int)grips.size(); ++i)
            {
                const auto& g    = grips[i];
                auto        s    = m_viewport->GetCamera().WorldToScreen(g.WorldPos);
                auto        type = static_cast<GripDraw::Type>(g.GripType);

                bool hovered = std::find(hoveredIdxs.begin(), hoveredIdxs.end(), i) != hoveredIdxs.end();

                m_gripVertices.push_back({ s, type, hovered });
            }
        }
        vs.Grips = m_gripVertices;

        return vs;
    }

}

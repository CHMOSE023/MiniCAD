#pragma once
#include "Editor/Tools/ITool.h"
#include "Editor/Overlay/Overlay.h"
#include "Editor/Picking/Picking.h"
#include "Editor/Snap/SnapEngine.h"
#include "Editor/Snap/SnapResult.h"
#include "Editor/Grip/GripEditor.h"
#include "Editor/Input/InputEvent.h"
#include "Editor/Input/KeyCode.h"
#include "Editor/Constraint/ConstraintEngine.h"
#include "Editor/Resolver/InputResolver.h"
#include "Editor/CommandLine/CommandLine.h"
#include "Viewport/Viewport.h"
#include "Viewport/ViewState.h"
#include "Scene/Scene.h"
#include "Document/CommandStack/CommandStack.h"
#include "Core/GeomKernel/Line.hpp"
#include "Core/Object/Object.hpp"
#include "Render/VertexTypes.hpp"
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniCAD
{
    class Document; 

    class Editor
    {
    public:
        Editor();

        // ── 绑定 / 解绑文档与视口 ────────────────────────────
        void Bind(Document& doc, Viewport& viewport);
        void Unbind();
        bool IsBound() const { return m_doc != nullptr; }

        // ── 输入 ─────────────────────────────────────────────
        bool OnInput(const InputEvent& e);

        // ── 渲染：收集顶点 + 提交到 Viewport ─────────────────
        void Render();

        // ── Picking / 选择 ────────────────────────────────────
        const std::unordered_set<Object::ObjectID>& GetSelection();
        const std::unordered_set<Object::ObjectID>& GetHovered();

        Object*              GetPrimarySelectedObject();
        std::vector<Object*> GetSelectedObjects();

        // ── 夹点 ─────────────────────────────────────────────
        GripEditor&        GetGripEditor()       { return m_gripEditor; }
        const Line&        GetAnchorLine() const { return m_constraintEngine.GetGuideLine(); }
        bool               IsConstraintActive() const { return m_constraintEngine.IsAnyActive(); }
        ConstraintEngine&  GetConstraintEngine() { return m_constraintEngine; }
        bool               IsActiveTool() const  { return m_tool != nullptr; }

        // ── 工具注册表 ───────────────────────────────────────
        void RegisterTool(const std::string& toolId, std::function<std::unique_ptr<ITool>()> factory);
        void RegisterAlias(const std::string& alias, const std::string& toolId);
        void ActivateToolById(const std::string& toolId);

        // ── 命令行 ───────────────────────────────────────────
        CommandLine&       GetCmdLine()       { return m_commandLine; }
        const CommandLine& GetCmdLine() const { return m_commandLine; }
        void RunCommand(const std::string& text);   // 命令行/键盘提交的命令统一入口
        std::vector<std::string> GetCommandNames() const;  // 所有别名 + 工具全名，供补全

        // ── 绘制工具便捷方法 ────────────────────────────────
        void StartLineTool();
        void StartPointTool();
        void StartRectangleTool();
        void StartCircleTool();
        void StartArcTool();
        void StartEllipseTool();
        void StartPolylineTool();
        void StartSplineTool();
        void StartTextTool();
        void StartMTextTool();

        // ── 编辑工具便捷方法 ─────────────────────────────────
        void StartMoveTool();
        void StartCopyTool();
        void StartMirrorTool();
        void StartRotateTool();

        // ── 几何编辑工具便捷方法 ─────────────────────────────
        void StartTrimTool();
        void StartExtendTool();
        void StartBreakTool();

        // ── 文字输入请求 ─────────────────────────────────────
        struct TextInputRequest
        {
            bool             Active       = false;
            Math::Point3     InsertPos;
            float            Height       = 2.5f;
            float            Rotation     = 0.f;
            Object::ObjectID EditTargetId = Object::InvalidID;
            std::string      InitialText;
        };

        TextInputRequest&       GetTextInputRequest()       { return m_textRequest; }
        const TextInputRequest& GetTextInputRequest() const { return m_textRequest; }
        void SubmitTextInput(const std::string& utf8Text);

        // ── 多行文字输入请求 ─────────────────────────────────
        struct MTextInputRequest
        {
            bool             Active       = false;
            Math::Point3     InsertPos;
            double           Height       = 2.5;
            double           Rotation     = 0.0;
            double           BoxWidth     = 0.0;
            Object::ObjectID EditTargetId = Object::InvalidID;
            std::string      InitialText;
        };

        MTextInputRequest&       GetMTextInputRequest()       { return m_mtextRequest; }
        const MTextInputRequest& GetMTextInputRequest() const { return m_mtextRequest; }
        void SubmitMTextInput(const std::string& utf8Text);

        // ── 删除 ─────────────────────────────────────────────
        void DeleteSelected();

        // ── 约束 ─────────────────────────────────────────────
        bool TryGetAnchor(Math::Point3& out) const;

        bool IsOrthoEnabled() const;
        void SetOrthoEnabled(bool enabled);
        void ToggleOrtho();

        bool   IsPolarEnabled() const;
        void   SetPolarEnabled(bool enabled);
        void   TogglePolar();
        double GetPolarAngle() const;
        void   SetPolarAngle(double deg);

        // ── 捕捉 ─────────────────────────────────────────────
        bool IsSnapEnabled() const;
        void SetSnapEnabled(bool enabled);
        void ToggleSnap();

        // ── Undo / Redo / Command ─────────────────────────────
        void Undo();
        void Redo();
        void ExecuteCommand(std::unique_ptr<ICommand> cmd);

        // ── 字体纹理（由 UIManager 注入）─────────────────────
        void  SetFontTexture(void* srv) { m_fontTexture = srv; }

    private:
        bool HandleGlobal (const InputEvent& e);
        bool HandleDefault(const InputEvent& e);

        void ActivateTool(std::unique_ptr<ITool> tool);
        void ActivateToolByAlias(const std::string& alias);
        char ToCommandChar(KeyCode key);

        void RegisterBuiltinTools();

        // ── 渲染辅助 ─────────────────────────────────────────
        void UpdateSceneVertices();
        ViewState BuildViewState();

    private:
        // 绑定目标（非拥有）
        Document*  m_doc      = nullptr;
        Viewport*  m_viewport = nullptr;

        // Editor 拥有的子系统
        Overlay          m_overlay;
        Picking          m_picking;
        SnapEngine       m_snap;
        SnapResult       m_currentSnap;
        GripEditor       m_gripEditor;
        ConstraintEngine m_constraintEngine;
        InputResolver    m_resolver;        // 输入解析器
        CommandLine      m_commandLine;     // 命令行提示与回显缓冲

        // 工具
        std::unique_ptr<ITool> m_tool;
        bool                   m_toolSuspended    = false;
        bool                   m_pendingToolReset = false;

        std::unordered_map<std::string,
            std::function<std::unique_ptr<ITool>()>>     m_toolRegistry;
        std::unordered_map<std::string, std::string>     m_aliasRegistry;
        std::string                                      m_cmdBuffer;
        std::string                                      m_lastCommand;

        TextInputRequest  m_textRequest;
        MTextInputRequest m_mtextRequest;

        // 鼠标位置（每帧更新）
        double m_mouseX = 0;
        double m_mouseY = 0;

        // 渲染数据（每帧收集）
        std::vector<Vertex_P3_C4>    m_sceneVertices;
        std::vector<Vertex_P3_C4_UV> m_textVertices;
        std::vector<Vertex_P3_C4>    m_overlayVertices;
        std::vector<GripDraw>        m_gripVertices;
        void*                        m_fontTexture = nullptr;
    };
}

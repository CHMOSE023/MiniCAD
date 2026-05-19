#pragma once
#include "Editor/Tools/ITool.h"
#include "Editor/Overlay/Overlay.h"
#include "Editor/Picking/Picking.h"
#include "Editor/Viewport/Viewport.h"
#include "Editor/Snap/SnapEngine.h"
#include "Editor/Snap/SnapResult.h"
#include "Editor/Grip/GripEditor.h"
#include "Editor/Input/InputEvent.h"
#include "Editor/Input/KeyCode.h"
#include "Scene/Scene.h"
#include "Document/CommandStack/CommandStack.h"
#include "Core/GeomKernel/Line.hpp"
#include "Core/Object/Object.hpp"
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniCAD
{ 
    class EditorContext
    {
    public:
        EditorContext(Scene&        scene,
                      CommandStack& cmdStack,
                      Viewport&     viewport,
                      Overlay&      overlay,
                      Picking&      picking,
                      SnapEngine&   snap,
                      SnapResult&   currentSnap);

        bool OnInput(const InputEvent& e);

        // ── Picking / 选择 ────────────────────────────────────
        const std::unordered_set<Object::ObjectID>& GetSelection();
        const std::unordered_set<Object::ObjectID>& GetHovered();

        Object*              GetPrimarySelectedObject();
        std::vector<Object*> GetSelectedObjects();

        // ── 夹点 ─────────────────────────────────────────────
        GripEditor& GetGripEditor()       { return m_gripEditor; }
        const Line& GetAnchorLine() const { return m_anchorLine; }
        bool        IsActiveTool()  const { return m_tool != nullptr; }

        // ── 工具注册表（插件 / 内置工具统一入口）─────────────
        //
        // 插件在 load() 里调这两个方法挂载自己的工具，
        // 无需改动任何引擎头文件。
        //
        // toolId 命名约定：
        //   内置工具  →  "Line" / "Circle" / "Move" ...
        //   插件工具  →  "MyPlugin.Cloud" / "MyPlugin.Hatch" ...
        void RegisterTool(const std::string&  toolId,  std::function<std::unique_ptr<ITool>()> factory);

		//void RegisterShortcut(KeyCode key, const std::string& toolId);          // 单键快捷键（KeyCode）  F3 / F8 / Delete / ESC 这类功能键
		void RegisterAlias(const std::string& alias, const std::string& toolId);// 命令别名（Alias）      L / PL / C / MI / TR 这类字母序列

        // 按 ID 激活工具；工厂返回 nullptr 时静默忽略（如编辑类工具选择集为空）
        void ActivateToolById(const std::string& toolId);

        // ── 绘制工具便捷方法（UI 层直接调用）────────────────
        // 内部均委托给 ActivateToolById，不含任何逻辑
        void StartLineTool();
        void StartPointTool();
        void StartRectangleTool();
        void StartCircleTool();
        void StartArcTool();
        void StartEllipseTool();
        void StartPolylineTool();
        void StartSplineTool();
        void StartTextTool();

        // ── 编辑工具便捷方法 ─────────────────────────────────
        void StartMoveTool();
        void StartCopyTool();
        void StartMirrorTool();
        void StartRotateTool();

        // ── 几何编辑工具便捷方法 ─────────────────────────────
        void StartTrimTool();
        void StartExtendTool();
        void StartBreakTool();

        // ── 文字输入请求（由 TextTool 发起，UIManager 响应）─────
        struct TextInputRequest
        {
            bool         Active    = false;
            Math::Point3 InsertPos;
            float        Height    = 2.5f;
            float        Rotation  = 0.f;
        };

        TextInputRequest&       GetTextInputRequest()       { return m_textRequest; }
        const TextInputRequest& GetTextInputRequest() const { return m_textRequest; }
        void SubmitTextInput(const std::string& utf8Text);

        // ── 删除 ─────────────────────────────────────────────
        void DeleteSelected();

        // ── 正交 ─────────────────────────────────────────────
        bool TryGetAnchor(Math::Point3& out) const;
        bool IsOrthoEnabled() const;
        void SetOrthoEnabled(bool enabled);
        void ToggleOrtho();

        // ── 捕捉 ─────────────────────────────────────────────
        bool IsSnapEnabled() const;
        void SetSnapEnabled(bool enabled);
        void ToggleSnap();

        // ── Undo / Redo / Command ─────────────────────────────
        void Undo();
        void Redo();
        void ExecuteCommand(std::unique_ptr<ICommand> cmd);

    private:
        // ── 消息处理 ─────────────────────────────────────────
        bool       HandleGlobal    (const InputEvent& e);
        bool       HandleDefault   (const InputEvent& e);

        // ── Snap / 约束 ───────────────────────────────────────
        bool       ShouldSnap      ()                       const;
        void       UpdateSnap      (const InputEvent& e);
        InputEvent InjectSnap      (const InputEvent& e);
        InputEvent ApplyConstraints(const InputEvent& e);

        // ── 工具生命周期核心 ──────────────────────────────────
        // 负责：Cancel 旧工具、清 Overlay、清 Selection、MarkDirty、ReBuildGrip、注册 OnFinished、重置标志
        void ActivateTool(std::unique_ptr<ITool> tool);
        void ActivateToolByAlias(const std::string& alias);
        char ToCommandChar(KeyCode key);

        // ── 内置工具 / 快捷键注册 ─────────────────────────────
        void RegisterBuiltinTools(); 

    private:
        std::unique_ptr<ITool> m_tool;
        bool                   m_toolSuspended    = false;  // 中键平移期间为 true
        bool                   m_pendingToolReset = false;  // 延迟销毁，避免工具在自己方法内同步 reset 导致 UAF

        Scene&         m_scene;
        CommandStack&  m_cmdStack;
        Viewport&      m_viewport;
        Overlay&       m_overlay;
        Picking&       m_picking;
        SnapEngine&    m_snap;
        SnapResult&    m_currentSnap;
        GripEditor     m_gripEditor;

        Line m_anchorLine;
        bool m_snapEnabled  = true;
        bool m_orthoEnabled = false;
         
        std::unordered_map<std::string,
        std::function<std::unique_ptr<ITool>()>>     m_toolRegistry;
        std::unordered_map<std::string, std::string> m_aliasRegistry;
        std::string                                  m_cmdBuffer;
        std::string                                  m_lastCommand;

        TextInputRequest m_textRequest;
    };

} 


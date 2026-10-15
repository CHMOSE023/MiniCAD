#pragma once
#include "Document.h"
#include "Editor/Editor.h"
#include "Viewport/Viewport.h"
#include "Render/IRenderer.h"
#include "Render/IRenderTarget.h"
#include <vector>
#include <memory>
#include <functional>
#include <string>
#include "Text/FontSystem.h"
#include "Core/Entity/Entity.hpp"

namespace MiniCAD
{
    class DocumentManager
    {
    public:
        // 文件对话框回调:由应用层注入(库不依赖窗口系统)。
        // save=true 为「另存为」对话框,false 为「打开」。返回完整路径;空串 = 用户取消。
        using FileDialogFn = std::function<std::string(bool save)>;

        DocumentManager() = default;

        Document& Create();

        void Close(Document* doc);

        Document* GetActive() const;

        void SetActive(Document* doc);
        void SetFontSystem(FontSystem* fontSystem);

        std::vector<std::unique_ptr<Document>>& GetAll();

        void SetRenderer(IRenderer* renderer);

        // ── RenderTarget（由应用层创建，非拥有）──────────────
        void           SetRenderTarget(IRenderTarget* rt) { m_renderTarget = rt; }
        IRenderTarget* GetRenderTarget()                  { return m_renderTarget; }

        // ── Viewport / Editor ────────────────────────────────
        void InitViewport(IRenderer& renderer, float w, float h);

        Viewport&       GetViewport()       { return *m_viewport; }
        const Viewport& GetViewport() const { return *m_viewport; }

        // 缩放到全图：把活动文档所有可见实体放进视野；文档为空时不动
        void ZoomAll(bool undoable = true);

        Editor&       GetEditor()       { return m_editor; }
        const Editor& GetEditor() const { return m_editor; }

        // --- 字体样式管理（代理 FontSystem）---

        FontStyle::FontStyleId RegisterFontStyle(FontStyle style);
        const FontStyle*       FindFontStyle(const std::string& name) const;
        const FontStyle*       FindFontStyle(FontStyle::FontStyleId id) const;

        void SetFileDialogHandler(FileDialogFn fn) { m_fileDialog = std::move(fn); }

        void New();
        void Open();                              // 经文件对话框选择路径后打开
        // 加载成功才提交新文档；重复路径激活已有文档，失败详情可供界面显示。
        Document* Open(const std::string& path, std::string* error = nullptr);
        // Open 的两步，供界面在后台线程读取：Load 只读文件、建文档，不碰文档列表、视口与编辑器，可在其他线程调用；
        // Add 在界面线程把读好的文档加入列表并激活（外来图纸缩放到全图）
        std::unique_ptr<Document> Load(const std::string& path, std::string* error = nullptr, const ImportProgress& progress = nullptr) const;
        Document* Add(std::unique_ptr<Document> doc);
        Document* FindOpen(const std::string& path) const;   // 已打开同一文件时返回该文档
        void Save();
        void SaveAs();
        void SaveAll();
        void Undo() const;
        void Redo() const;

        // ── 内部剪贴板:复制当前选中实体的快照,可跨文档粘贴 ──
        void Paste();
        void CopySelected();
        void CopySelectedWithBase();   // 带基点复制(AutoCAD COPYBASE):拾取基点,粘贴时光标即基点
        void CutSelected();   // 复制到剪贴板 + 删除选中(删除为一步撤销)
    private:
        std::string GenerateUniqueName();
		void DocumentDrawTest(Document* doc); // 测试代码：创建一些示例实体展示字体样式和标注功能
    private:
        std::vector<std::unique_ptr<Document>> m_docs;
        FontSystem*    m_fontSystem    = nullptr;
        Document*      m_active        = nullptr;
        IRenderer*     m_renderer      = nullptr;
        IRenderTarget* m_renderTarget  = nullptr;
        FileDialogFn   m_fileDialog;
        float          m_defaultWidth  = 600.f;
        float          m_defaultHeight = 400.f;

        std::unique_ptr<Viewport> m_viewport;
        Editor                    m_editor;

        // 剪贴板持有实体克隆(与源文档解耦,源被删除/关闭后仍可粘贴)
        std::vector<std::unique_ptr<Entity>> m_clipboard;
        Math::Point3 m_clipboardBase{};            // 带基点复制的基点(仅 m_clipboardHasBase 时有效)
        bool         m_clipboardHasBase = false;

    private:
        int m_untitledCounter = 0;
    };
}

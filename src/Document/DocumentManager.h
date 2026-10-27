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
#include <unordered_map>
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
        // OLE 对象快照的光栅化回调（见 Editor::SetOleRasterizer）：给现有的与之后新建的每个编辑器
        void SetOleRasterizer(Editor::OleRasterizer rasterizer);

        std::vector<std::unique_ptr<Document>>& GetAll();

        void SetRenderer(IRenderer* renderer);

        // ── RenderTarget（由应用层创建，非拥有）──────────────
        void           SetRenderTarget(IRenderTarget* rt) { m_renderTarget = rt; }
        IRenderTarget* GetRenderTarget()                  { return m_renderTarget; }

        // ── Viewport / Editor ────────────────────────────────
        // 主视口：停靠在主窗口里的文档共用它，切换文档换相机状态；拖出成独立窗口的文档各有一个自己的视口（DetachDocument）
        void InitViewport(IRenderer& renderer, float w, float h);

        // 当前文档的视口（它自己的独立视口，或主视口）；没有当前文档时是主视口
        Viewport&       GetViewport();
        const Viewport& GetViewport() const;
        Viewport&       GetMainViewport()       { return *m_viewport; }
        const Viewport& GetMainViewport() const { return *m_viewport; }
        // 文档显示在哪个视口：拖出的文档是它自己的视口，其余是主视口（停靠但不在前面的文档也返回主视口，相机不是它的）；
        // 文档不存在或视口还没建时 nullptr
        Viewport*       ViewportOf(const Document* doc);

        // 当前文档（GetActive）与主视口里的文档分开：
        // - 当前文档：命令行、状态栏、面板、菜单命令操作的对象，随用户最后点击的窗口切换（SetActive）
        // - 主视口显示的文档（GetMainDocument）：停靠文档里最后激活的那个；当前文档是拖出去的文档时它不变。
        //   停靠的文档成为当前文档时也就成为主视口的文档
        Document* GetMainDocument() const { return m_mainDoc; }
        // 主视口改显示 doc（停靠的文档；保存原文档的相机、恢复它的），不改当前文档。nullptr = 主视口空着
        void      SetMainDocument(Document* doc);
        Editor&   GetMainEditor();              // 主视口文档的编辑器；没有时空闲编辑器
        Editor&   GetEditor(Document& doc);     // 某个文档的编辑器（没有就建）

        // 把文档从主视口拖出到自己的视口（像素尺寸；相机从文档保存的视图恢复，显示开关同主视口）：返回那个视口，
        // 文档不存在、已拖出或视口还没建时 nullptr。它原本是主视口的文档时主视口换成别的停靠文档（没有就空着）；当前文档不变
        Viewport* DetachDocument(Document* doc, float width, float height);
        // 拖回主视口：它成为主视口显示的文档，也成为当前文档；没拖出的文档不做什么
        void      AttachDocument(Document* doc);
        bool      IsDetached(const Document* doc) const { return m_floatViewports.count(doc) != 0; }

        // 缩放到全图：把活动文档所有可见实体放进视野；文档为空时不动
        void ZoomAll(bool undoable = true);

        // 活动文档的编辑器。每个文档有自己的 Editor（显示数据、选择集、工具状态随文档保留，切换文档不重建），
        // 没有活动文档时返回一个不绑定文档的空闲编辑器（承载启动时注入的设置，新文档的编辑器从它继承）。
        // 返回的引用只在当前文档不变期间有效，不要跨文档切换保存
        Editor&       GetEditor();
        const Editor& GetEditor() const;

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

        std::unique_ptr<Viewport> m_viewport;                                     // 主视口
        Document*                 m_mainDoc = nullptr;                            // 主视口显示的文档
        std::unordered_map<const Document*, std::unique_ptr<Viewport>> m_floatViewports;   // 拖出的文档各自的视口
        Editor                    m_idleEditor;                                   // 没有文档时的编辑器（只承载设置）
        std::unordered_map<const Document*, std::unique_ptr<Editor>> m_editors;  // 每个文档一个编辑器
        // 文档的编辑器（没有就新建：从 proto 继承设置，有视口时绑定）
        Editor& EnsureEditor(Document& doc, const Editor& proto);
        void    SyncCamera(Document* doc);      // 把文档所在视口的相机回写到文档（存盘前；只对主视口里的文档与拖出的文档有意义）
        void    SyncLiveCameras();              // 主视口文档与所有拖出的文档
        bool    Contains(const Document* doc) const;

        // 剪贴板持有实体克隆(与源文档解耦,源被删除/关闭后仍可粘贴)
        std::vector<std::unique_ptr<Entity>> m_clipboard;
        Math::Point3 m_clipboardBase{};            // 带基点复制的基点(仅 m_clipboardHasBase 时有效)
        bool         m_clipboardHasBase = false;

    private:
        int m_untitledCounter = 0;
    };
}

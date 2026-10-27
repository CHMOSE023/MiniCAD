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
#include <map>
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
        // 窗格：一个窗口里的视口，窗格里的多个文档轮流用它（切换时保存 / 恢复各自的相机）。
        // 0 号是主窗口的窗格（主视口）；其余由界面为独立的文档窗口建（CreatePane），里面的文档都移走后自动删掉
        using PaneId = uint32_t;
        static constexpr PaneId kMainPane = 0;
        static constexpr PaneId kNoPane   = 0xFFFFFFFFu;

        void InitViewport(IRenderer& renderer, float w, float h);

        // 当前文档的视口（它所在窗格的视口）；没有当前文档时是主视口
        Viewport&       GetViewport();
        const Viewport& GetViewport() const;
        Viewport&       GetMainViewport()       { return *m_viewport; }
        const Viewport& GetMainViewport() const { return *m_viewport; }
        // 文档显示在哪个视口：它所在窗格的视口（窗格里不在前面的文档也返回这个视口，相机不是它的）；
        // 文档不存在或视口还没建时 nullptr
        Viewport*       ViewportOf(const Document* doc);

        PaneId    CreatePane(float width, float height);     // 新的空窗格（像素尺寸；显示开关同主视口）；主视口还没建时 kNoPane
        bool      HasPane(PaneId pane) const;
        Viewport* GetPaneViewport(PaneId pane);
        PaneId    PaneOf(const Document* doc) const;         // 文档所在的窗格；文档不存在时 kNoPane
        Document* GetShownDocument(PaneId pane) const;       // 窗格当前显示的文档（窗格空着或不存在时 nullptr）
        std::vector<Document*> DocumentsIn(PaneId pane) const;   // 窗格里的文档（按打开顺序）
        // 把文档移到窗格：它成为那个窗格显示的文档（换下去的文档保存相机）；原窗格改显示剩下的文档里最后打开的那个，
        // 空了（不是主窗格）就删掉。当前文档不变；文档或窗格不存在时 false
        bool      MoveDocument(Document* doc, PaneId pane);
        // 新建 / 打开的文档放进哪个窗格：默认是当前文档所在的窗格（SetActive 时跟着改）；
        // 界面在用户操作一个空着的窗口（没有文档可激活）时改设它。窗格不存在时放进主窗格
        void      SetNewDocumentPane(PaneId pane) { m_newDocPane = pane; }
        PaneId    GetNewDocumentPane() const;

        // 当前文档（GetActive）与各窗格显示的文档分开：
        // - 当前文档：面板、菜单命令操作的对象，随用户最后点击的窗口切换（SetActive）。命令行、状态栏、视图方块每个窗口一套，
        //   显示那个窗口里的文档，在上面操作时它先成为当前文档
        // - 窗格显示的文档：窗格里最后激活的那个；文档成为当前文档时也就成为它所在窗格显示的文档。
        //   主视口显示的文档即 GetMainDocument
        Document* GetMainDocument() const { return m_mainDoc; }
        // 主视口改显示 doc（主窗格里的文档；保存原文档的相机、恢复它的），不改当前文档。nullptr = 主视口空着
        void      SetMainDocument(Document* doc);
        Editor&   GetMainEditor();              // 主视口文档的编辑器；没有时空闲编辑器
        Editor&   GetEditor(Document& doc);     // 某个文档的编辑器（没有就建）

        // 把文档从主窗格拖出到自己的新窗格（CreatePane + MoveDocument）：返回那个窗格的视口，
        // 文档不存在、不在主窗格或视口还没建时 nullptr。当前文档不变
        Viewport* DetachDocument(Document* doc, float width, float height);
        // 移回主窗格：它成为主视口显示的文档，也成为当前文档；在主窗格里的文档不做什么
        void      AttachDocument(Document* doc);
        bool      IsDetached(const Document* doc) const { return m_docPane.count(doc) != 0; }   // 不在主窗格

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

        std::unique_ptr<Viewport> m_viewport;                                     // 主视口（主窗格）
        Document*                 m_mainDoc = nullptr;                            // 主视口显示的文档
        struct Pane
        {
            std::unique_ptr<Viewport> viewport;
            Document*                 shown = nullptr;                            // 窗格显示的文档
        };
        std::map<PaneId, Pane>                         m_panes;                   // 主窗格之外的窗格
        std::unordered_map<const Document*, PaneId>    m_docPane;                 // 不在主窗格的文档在哪个窗格
        PaneId                    m_nextPane   = 1;
        PaneId                    m_newDocPane = kMainPane;
        Editor                    m_idleEditor;                                   // 没有文档时的编辑器（只承载设置）
        std::unordered_map<const Document*, std::unique_ptr<Editor>> m_editors;  // 每个文档一个编辑器
        // 文档的编辑器（没有就新建：从 proto 继承设置，有视口时绑定）
        Editor& EnsureEditor(Document& doc, const Editor& proto);
        void    SyncCamera(Document* doc);      // 把文档所在视口的相机回写到文档（存盘前；只对各窗格显示的文档有意义）
        void    SyncLiveCameras();              // 各窗格显示的文档
        bool    Contains(const Document* doc) const;
        void    ShowInPane(PaneId pane, Document* doc);   // 窗格改显示 doc（保存换下去的文档的相机，恢复 doc 的）
        void    ClearShown(PaneId pane);                  // 窗格不显示任何文档（不保存相机：调用方已处理）
        void    RefillPane(PaneId pane);                  // 窗格空着时显示剩下的文档里最后打开的；没有剩下的（不是主窗格）就删掉
        void    PlaceNewDocument(Document& doc);          // 新文档放进 GetNewDocumentPane，在那里显示

        // 剪贴板持有实体克隆(与源文档解耦,源被删除/关闭后仍可粘贴)
        std::vector<std::unique_ptr<Entity>> m_clipboard;
        Math::Point3 m_clipboardBase{};            // 带基点复制的基点(仅 m_clipboardHasBase 时有效)
        bool         m_clipboardHasBase = false;

    private:
        int m_untitledCounter = 0;
    };
}

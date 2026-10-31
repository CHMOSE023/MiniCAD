#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "GUI/AppPlatform.h"
#include "Render/IRenderer.h"
#include "Document/DocumentManager.h"
#include "Document/RecentFiles.h"
#include "Text/FontSystem.h"
#include "Render/IRenderBackend.h"
#include "Data/CommandRegistry.h"
#include "Widgets/Binding.h"
#include "Widgets/FloatingPanel.h"
#include "Widgets/Menu.h"
#include "NodeGraph/Graph.h"
#include "NodeGraph/GraphHistory.h"
#include "NodeGraph/Preview.h"
#include <map>

namespace MiniGUI
{
    class UIContext;
    class DrawList;
    class TitleBar;
    class ViewportHost;
    class CommandConsole;
    class Label;
    class ListView;
    class TextBox;
    class DockSpace;
    class Node;
    class ProgressBar;
    class TabView;
    class UiLayout;
    struct ViewportPointerEvent;
    struct KeyEvent;
}

namespace MiniGUI
{
    class Button;
    class Dialog;
    class Popup;
    class Ribbon;
    class ThemeColors;
}

namespace MiniCAD
{
    class StatusBarView;
    class NodeEditorView;
    class FloatingPanelHost;
    struct EntityAttr;
    class DynamicInputBox;

    // MiniCAD 主窗口的界面与逻辑（桌面版和网页版共用）。整个界面由 MiniGUI 绘制，平台相关的部分
    // （窗口、图形设备、消息循环、文件对话框）由 AppPlatform 提供：Win32 版见 apps/Win32/src/Host，网页版见 apps/Web/src/Host。
    //   - 标题栏自绘，菜单栏在标题栏里；桌面版右侧有最小化 / 最大化 / 关闭（无边框窗口，Win32Frame）
    //   - 多文档标签：停靠在主窗口里的文档共用主视口（DocumentManager 的主 Viewport，切换文档时换相机状态；每个文档一个 Editor）；
    //     标签拖出标签条松开即成独立的系统窗口（桌面版，DocumentWindows.cpp：自己的视口与渲染目标），拖回标签条合并回去
//   - 工具栏里的图层 / 线型 / 线宽下拉框，停靠的特性与图层面板、命令行，状态栏
    //   - 菜单、工具栏、布局由界面描述文件 ui/minicad_ui.json 描述（UiLayout），保存后自动重新加载（也可按 F5）；
    //     用户拖动后的面板布局保存到 AppPlatform::GetUserDataDir() 下的 layout.json（Win32：%LOCALAPPDATA%\MiniCAD），下次启动恢复
    //
    // 帧顺序（输入与画面同一帧）：
    //   1. 输入到达：平台输入层（Win32Input / WebInput）→ UIContext 分发 → ViewportHost 转发 → 立即交给 Editor（相机平移/缩放当场生效）
    //   2. 宿主在输入处理完之后调用 RenderFrame：UIContext::Update 布局 → ViewportHost::RenderContent 按最新尺寸和相机渲染 CAD
    //      → UIContext::Render 把界面连同视口纹理画到窗口 → 呈现
    //
    // RunSelfTest（只有 Win32 版，定义在 apps/Win32/src/Host/SelfTest.cpp）：注入窗口消息，验证视口输入在第一帧生效、
    // 标题栏命中测试、多文档切换，结果写到控制台，返回 0 表示通过
    class MainFrame
    {
    public:
        MainFrame();
        ~MainFrame();

        // 下拉框里一个图层的缩进：接在最近的、确有其层的上级下面，depth 为缩进几级，
        // labelStart 为显示的那段在图层名里的起点（只有 "A" 和 "A$B$C" 时 C 显示为 A 下面的 "B$C"）
        struct LayerIndent { int depth = 0; size_t labelStart = 0; };

        // 界面描述文件（UTF-8 路径）；不设置时优先用源码目录里的文件，其次是可执行文件旁的 ui/minicad_ui.json
        void SetUiFile(std::string utf8Path) { m_uiPath = std::move(utf8Path); }
        // 不读写用户布局文件（自测用，避免受本机保存的布局影响）
        void SetUseUserLayout(bool use) { m_useUserLayout = use; }

        // 平台对象必须比 MainFrame 活得久。尺寸为逻辑像素，pixelScale = 物理像素 / 逻辑像素
        bool Initialize(AppPlatform& platform, float width, float height, float pixelScale);
        int  RunSelfTest(bool full = false);       // full：另跑只打印不判定的耗时测量（20 万条线的大图纸），慢
        int  RunBenchmark(const std::string& path);     // 只有 Win32 版：--bench 图纸，打印打开、滚轮缩放的耗时与内存

        // ── 由宿主调用 ───────────────────────────────────────────
        void OnResize(float width, float height, float pixelScale);    // 窗口尺寸或 DPI 变化
        void RenderFrame();
        void RequestExit();                     // 有未保存的文档时先询问，确认后调用 AppPlatform::Quit
        void StateChanged();                    // 文档、选择集、当前工具可能变了：刷新命令状态、标签、面板、标题
        // 菜单、启动参数和拖放共用此入口；逐个在后台读取（状态栏显示进度），读完一个加入一个，失败汇总显示，成功文档保留。
        void OpenDrawings(const std::vector<std::string>& paths);
        // 还有图纸在读取、排队，或刚加入的图纸还在生成显示（分帧细分）
        bool IsOpeningDrawings() const { return m_openJob || m_openDone || !m_openQueue.empty() || m_buildDoc; }
        void CheckUiFileChanged();              // 界面描述文件变了就重新加载（宿主监视文件所在目录）
        bool HasUnsavedDocuments() const;       // 网页版：关闭页面前据此提示
        const std::string&  GetUiPath() const { return m_uiPath; }
        MiniGUI::UIContext& GetUI() const { return *m_ui; }
        MiniGUI::TitleBar*  GetTitleBar() const;

        // 单调时钟（统计帧耗时、输入延迟）
        static int64_t NowTicks();
        static constexpr int64_t kTicksPerSecond = 1'000'000'000;

    private:
        bool InitDocument(int width, int height);
        void InitUI();

        // ── 命令（MainCommands.cpp）──────────────────────────────
        void RegisterCommands();

        // ── 宿主面板（MainPanels.cpp / LayerPanels.cpp）─────────────
        struct ViewChrome;      // 一个窗口里的视图方块、导航条、命令行、状态栏（见下文“每个窗口一套的绘图区界面”）
        std::unique_ptr<MiniGUI::Node> CreateCanvas();            // 绘图区（面板 "canvas"）：铺满标题栏以下，其余界面浮在上面
        // 透明的“洞”（主窗口的面板 "documents"，文档窗口里铺满视口）：视图方块、导航条、命令行
        std::unique_ptr<MiniGUI::Node> CreateDocumentArea(ViewChrome& c);
        std::unique_ptr<MiniGUI::Node> CreateDocTabs();           // 文档标签条（面板 "doctabs"，放在标题栏里）
        std::unique_ptr<MiniGUI::Node> CreateViewNavBar(ViewChrome& c);   // 视图方块下面竖排的导航条（ViewNavBar.cpp）
        std::unique_ptr<MiniGUI::Node> CreateAppBar();            // 标题栏右侧：AI、设置、帮助、版本号（面板 "appbar"，ViewNavBar.cpp）
        std::unique_ptr<MiniGUI::Node> CreateLayoutTabs(ViewChrome& c);   // 布局页签条（模型 / 图纸空间布局），放在状态栏左侧
        // 特性、图层、节点编辑器面板（面板 "properties" / "layers" / "nodes"）：每个窗口的停靠区里一套，显示、修改那个窗口里的文档
        std::unique_ptr<MiniGUI::Node> CreatePropertiesPanel(ViewChrome& c);
        std::unique_ptr<MiniGUI::Node> CreateStartPage();         // 没有文档时的起始页（StartPage.cpp）
        void RefreshStartPage();                                  // 按最近打开列表重建起始页的"最近"区
        void ShowStartPage(bool show);                            // 有文档时切换起始页 / 图纸
        void ApplyStartPage(bool show);                           // 显示起始页时隐藏标题栏以下的其余界面
        void RefreshStartNav();                                   // 起始页导航列的可用状态、“返回”行的文档名
        void OpenNodeExample(const std::string& path, const std::string& name, const std::string& description);
        void OpenNodeEditorFromStart(bool newDocument);           // 离开起始页并显示节点编辑器（没有文档时新建）
        // 状态栏（主窗口的面板 "statusbar"，文档窗口底部）：布局标签 + 捕捉 / 正交等开关；主窗口的还有读写进度与界面描述的加载结果
        std::unique_ptr<MiniGUI::Node> CreateStatusBar(ViewChrome& c);
        std::unique_ptr<MiniGUI::Node> CreateLayerPanel(ViewChrome& c);
        // 功能区里的图层 / 线型 / 线宽下拉框（面板 "layerbar"，每个窗口一份）：显示 doc() 的状态、改动作用于它，绑定加进 bindings
        std::unique_ptr<MiniGUI::Node> CreateLayerBar(MiniGUI::BindingSet& bindings, std::function<Document*()> doc,
                                                      MiniGUI::ComboBox** layerOut = nullptr);
        std::unique_ptr<MiniGUI::Node> CreateOpenProgress();      // 状态栏里的读取进度（文件名 + 进度条），只在打开图纸时显示
        std::unique_ptr<MiniGUI::Node> CreateSaveProgress();      // 状态栏里的保存进度（同读取进度的样式），只在保存时显示
        std::unique_ptr<MiniGUI::Node> CreateCommandLine(ViewChrome& c);  // 命令行（CommandLine.cpp）
        void SyncCommandLine();                                   // 主窗口：回显与提示同步到命令行（文档窗口在它自己的帧里同步）
        void RunCommandLine(const std::string& text);             // 命令行提交
        bool RouteKeyToCommandLine(const MiniGUI::KeyEvent& e);   // 绘图区直接打字进入命令行

        // ── 节点图（NodePanel.cpp，docs/NodeGraph/PLAN.md）──────────
        std::unique_ptr<MiniGUI::Node> CreateNodePanel(ViewChrome& c);    // 节点编辑器面板：工具行 + 画布
        void SyncNodeGraph();                                     // 每帧：求值当前文档的节点图（引用的图纸变了也重算），需要时刷新绘图区预览
        void RefreshNodePreview(Document& doc);
        Nodes::Graph& ShownNodeGraph(Document& doc);              // 节点编辑器正在显示的图（主图或进入的组内图）
        void BakeNodeGraph();                                     // 烘焙：结果加入图纸（可撤销）
        void SaveNodeGraphFile(bool selectionOnly);               // 节点图存为 .mcnode（selectionOnly：只存选中的节点）
        void OpenNodeGraphFile();                                 // 打开 .mcnode：插入当前节点图，或替换整张图
        bool LoadNodeGraphText(const std::string& text, bool replace);
        void OpenNodeParamsDialog();                              // 参数表：全部命名参数的名称、类别、值、公式，以及类型（族类型）
        void ShowNodePanel(bool toggle);                          // 当前窗口的节点面板；第一次显示时停靠在绘图区下方
        NodeEditorView* ActiveNodeEditor() const;                 // 当前文档所在窗口的节点编辑器（求值、预览、烘焙都对当前文档）
        bool NodeEditorHasFocus() const;                          // 节点编辑器有焦点时 Ctrl+Z / Y / A / C / X / V 作用于节点图
        bool NodeGraphUndo(bool redo);
        Nodes::SolveContext  NodeSolveContext(Document& doc) const;
        // 后台求值（N11）：桌面版把节点图的副本交给单个工作线程求值，界面线程不等待；改了正在算的节点就取消重来
        void StartNodeEvaluation(Nodes::Graph& g, std::unique_ptr<Nodes::Graph> copy, const Nodes::SolveContext& ctx);
        bool CollectNodeEvaluation();                             // 后台求值完成时把结果合并回节点图，返回是否合并了
        void StopNodeEvaluation();                                // 用户停止：取消，修改节点图（或图纸）之前不再自动求值
        void WaitNodeEvaluation();                                // 取消并等工作线程退出（关闭程序、自测）
        void UpdateNodeStatus(const Nodes::Graph& g);
        Nodes::GraphHistory& NodeHistory(const Document& doc);

        // ── 动态输入（DynamicInput.cpp）与 Editor 请求的对话框（EditorDialogs.cpp）──
        // 光标旁的长度 / 角度输入框跟随 Editor 状态：每个窗口一个（ViewChrome::dynInput），只在当前文档所在的那个窗口里显示
        void SyncDynamicInput() { SyncDynamicInput(m_main); }
        void SyncDynamicInput(ViewChrome& c);
        bool RouteKeyToDynamicInput(const MiniGUI::KeyEvent& e);  // 工具进行中按数字：交给动态输入或命令行
        void SendKeyToEditor(MiniGUI::Key key);                   // 合成一次按下 + 抬起交给当前工具（选项字母、Enter、Esc）
        bool        IsDynInputVisible() const { return IsDynInputVisible(m_main); }   // 自测用
        bool        DynInputHasFocus() const  { return DynInputHasFocus(m_main); }
        std::string GetDynInputText(int field) const { return GetDynInputText(m_main, field); }  // 0 长度，1 角度
        bool        IsDynInputVisible(const ViewChrome& c) const;
        bool        DynInputHasFocus(const ViewChrome& c) const;
        std::string GetDynInputText(const ViewChrome& c, int field) const;
        void SyncEditorRequests();                                // Editor 发出的请求（文字输入、块名、插入块、阵列、填充、文字样式）→ 打开 / 关闭对应界面
        void OpenTextEditor(bool multiline);
        void OpenBlockNameDialog();
        void OpenBlockInsertDialog();
        void OpenArrayDialog();
        void OpenHatchDialog();
        void OpenTextStyleDialog();
        void OpenMLineStyleDialog();                              // 多线样式（MLSTYLE）
        void OpenPointStyleDialog();                             // 格式 → 点样式…（$PDMODE/$PDSIZE）
        void OpenLineTypeScaleDialog();                          // 格式 → 线型比例…（$LTSCALE）
        void LoadHatchPatterns();                                 // 载入 patterns/ 目录下的 .pat 文件（内置图案之外）
        void ShowAbout();
        void RefreshLayerPanel();                                 // 各窗口的图层面板（RefreshLayerPanel(ViewChrome&)）
        void RefreshLayerPanel(ViewChrome& c);
        // 图层按名称的自然顺序排列（0 层最前），下拉框、特性面板和图层面板都用这个顺序；
        // 结果缓存到图层增删改名为止，只在界面线程使用
        static const std::vector<uint32_t>& SortedLayerIds(Document& doc);
        // 下拉框里的图层按层级缩进（与 SortedLayerIds 一一对应，见 LayerIndent）
        static const std::vector<LayerIndent>& SortedLayerIndents(Document& doc);
        // 下拉框的一项图层：下拉列表里按层级缩进，收起时写全名（不同上级下可能有同名的）；icons 时行首画 开 / 锁定 / 颜色
        static void PaintLayerChoice(MiniGUI::UIContext* ctx, MiniGUI::DrawList& dl, const MiniGUI::Rect& box, Document& doc,
                                     int index, bool inList, bool icons);
        // 选择集的统计（特性面板与工具栏下拉框共用）：公共图层 / 颜色 / 线型 / 线宽的下标（没有选中为 -1，
        // 取值不同为 nullopt，显示"*多种*"）、线型比例、公共几何特性。按选择集版本与场景版本缓存：
        // 各个绑定每次刷新都要扫一遍选择集，13 万个对象扫 6 遍要 40 ms，而每次点击、按键都会刷新
        struct SelectionSummary
        {
            bool            valid = false;
            const Document* doc   = nullptr;
            uint64_t        selVersion = 0, sceneVersion = 0, displayVersion = 0;
            size_t          count = 0;
            std::optional<int>    layer, color, lineType, lineweight;
            std::optional<double> lineTypeScale;
            std::vector<CommonProperty> properties;
        };
        const SelectionSummary& SelectionInfo() { return SelectionInfo(m_docManager.GetActive()); }
        const SelectionSummary& SelectionInfo(Document* doc);     // 某个文档的（各窗口的图层下拉框显示各自文档的选择）

        // ── 界面描述文件与用户布局（MainPanels.cpp）────────────────
        std::unique_ptr<MiniGUI::Node> CreateUiHost();            // 主窗口里界面描述生成的内容放在这里
        void ReloadUi();
        void SetViewCubeVisible(bool visible);   // 显示 / 隐藏视图方块，并写进用户布局文件
        void SetCommandLineVisible(bool visible); // 显示 / 隐藏浮动命令行，并写进用户布局文件
        bool IsCommandLineShown() const { return IsCommandLineShown(m_main); }   // 主窗口的浮动命令行在界面上可见
        void UpdateChromeVisibility();           // 各窗口的视图方块、命令行、导航条按开关、文档与布局显示或隐藏（UpdateChromeVisibility(ViewChrome&)）
        void LoadUserLayout();
        void SaveUserLayout();
        void ResetLayout();
        std::string ResourceDir() const { return m_platform->GetResourceDir(); }   // icons/ ui/ patterns/ fonts/ 所在目录

        // ── 最近打开（RecentFiles，MainPanels.cpp）──────────────────
        std::filesystem::path RecentStorePath() const;            // exe 目录下的 recent.txt；空 = 该平台不保存
        void LoadRecentFiles();                                   // 启动时读入
        void RememberRecentFile(const std::string& path);         // 打开 / 另存为成功后记一笔并写回磁盘
        void ForgetRecentFile(const std::string& path);           // 打不开的文件从列表里去掉
        void ClearRecentFiles();
        std::vector<MiniGUI::MenuItem> BuildRecentMenuItems();     // 「文件 > 最近打开」的内容，打开菜单时生成
        void SaveRecentFiles();                                   // 写回磁盘（平台不保存时什么都不做）

        // ── 文档 ─────────────────────────────────────────────────
        void SyncDocuments();                   // 标签与 DocumentManager 的文档列表保持一致
        void StartNextOpen();                   // 后台读取队列里的下一张图纸
        void FinishOpen();                      // 界面线程：读完一张，提示"正在生成显示"，等这一帧画出后再加入
        void AddOpened();                       // 帧尾：把读完的图纸加入文档列表（之后每帧细分一批，见 Editor::BeginProgressiveBuild），继续下一张
        void EndOpenBatch();                    // 队列读完：同步界面，显示失败汇总
        void SyncOpenProgress();                // 每帧：把后台读取进度同步到状态栏
        // 外部参照（XREF）：本批打开的图纸有找不到的参照时提示，可选一个文件夹再找（网页版经浏览器授权读本地文件夹）
        void PromptMissingXrefs();
        void LocateXrefs(std::vector<Document*> docs);   // 选文件夹，在里面找这些图纸没读进来的参照并读入
        void UpdateTitle();
        void ActivateDocument(Document* doc);
        // 布局标签（模型 / 图纸空间布局）同步与切换
        void SyncLayoutTabs();                  // 各窗口的布局标签条同步它的文档
        void ActivateLayout(int index);
        void ZoomToLayout(const DrawingLayout& sheet);   // 框住布局保存的图纸空间视图，没有时框住纸张范围
        void NewLayout();                       // 新建布局（标签条的 + 与右键菜单、命令 layout.new）
        void DeleteLayout(int index);           // 删除布局（右键菜单、命令 layout.delete 删当前布局）
        void CloseDocument(Document* doc, std::function<void()> afterClose = {});   // 未保存时先询问；关掉了才调 afterClose
        void CloseDocuments(std::vector<Document*> docs);                         // 逐个关闭（未保存的逐个询问），取消就停下
        // 保存当前 / 全部文档，保存成功后通知平台（网页版下载）。onSaved：当前文档保存成功后调用
        // （平台没有系统另存为对话框时，另存为经 OpenSaveAsDialog 异步完成）
        void SaveDocuments(bool all, bool saveAs, std::function<void()> onSaved = {});
        void SaveWithProgress(bool all);        // 保存 / 全部保存命令：先在状态栏显示"正在保存"并画出来，再保存
        enum class SaveState { Saving, Saved, Failed, Hidden };
        void ShowSaveStatus(SaveState state, const std::string& text = {});
        void OpenSaveAsDialog(Document* doc, std::function<void()> onSaved);      // 文件名 + 格式（EditorDialogs.cpp）
        void LayerChanged();                    // 图层特性改了：重建场景显示、刷新面板、重绘视口

        void RenderViewport(int pixelWidth, int pixelHeight);
        void UpdateStatus();
        void UpdateAppBarVisibility();      // 窗口变窄、文档标签放不下时隐藏标题栏右侧的按钮，让给文档标签

        // ── 视口输入 → Editor ─────────────────────────────────────
        void OnViewportPointer(const MiniGUI::ViewportPointerEvent& e);   // 主窗口的视口：交给主视口里文档的编辑器
        bool OnViewportKey(const MiniGUI::KeyEvent& e);                   // 主窗口与文档窗口的视口：交给当前文档的编辑器
        void NoteInput();       // 记录输入时间，用于统计输入到呈现的延迟；请当前文档的视口重绘
        void NoteInput(MiniGUI::ViewportHost& host);

        // ── 每个窗口一套的面板状态：特性、图层、节点编辑器（放在停靠区里）与功能区的图层下拉框 ──────────
        struct PanelSet
        {
            MiniGUI::BindingSet      bindings;                  // 特性面板与功能区图层下拉框的绑定
            std::function<void()>    syncGeometryRows;          // 特性面板「几何」组：按选择集重新分配行
            MiniGUI::ListView*       layerList   = nullptr;
            MiniGUI::TextBox*        layerSearch = nullptr;     // 图层面板的搜索框
            static constexpr uint32_t kGroupRow = 0xFFFFFFFFu;  // layerIds 里的分组行：只有 "A$B" 没有 "A" 时补出的上级 "A"
            std::vector<uint32_t>    layerIds;                  // 图层面板每一行对应的图层 ID（分组行为 kGroupRow）
            std::vector<std::string> layerPaths;                // 每一行的完整路径：图层名，或分组行的上级路径
            std::set<std::string>    layerCollapsed;            // 图层面板里折叠了的上级路径
            std::string              layerSignature;            // 图层面板当前显示的内容，没变时不重建行
            std::string              layerFilter;               // 图层面板只显示名称含这段文字的图层（不分大小写）
            bool                     layerSortDesc = false;     // 图层面板按名称降序（点"名称"表头切换）
            NodeEditorView*          nodeEditor  = nullptr;
            MiniGUI::Label*          nodeStatus  = nullptr;
            MiniGUI::Button*         nodeStop    = nullptr;     // 工具行的“停止”按钮，只在后台求值时显示
            bool                     nodePanelPlaced = false;   // 节点面板已经放进过停靠区
            const Document*          nodeDoc     = nullptr;     // 节点编辑器上次显示的文档：换了就复位视图
        };

        // ── 每个窗口一套的绘图区界面：视图方块、导航条、命令行、状态栏（含模型 / 布局标签）──────────
        // 主窗口一套（m_main，绑定主视口里的文档 GetMainDocument），每个文档窗口一套（绑定它的文档）。
        // 在哪套上按下 / 提交，它的文档就成为当前文档（UseChrome），菜单命令与面板随之作用于它
        struct ViewChrome
        {
            bool                     main    = false;       // 主窗口的一套（多出读写进度等，在上面按下经 PressHook 激活）
            DocumentManager::PaneId  pane    = DocumentManager::kNoPane;   // 这个窗口的窗格：显示的是它当前显示的文档
            MiniGUI::ViewportHost*   host    = nullptr;     // 这个窗口的视口控件
            MiniGUI::Node*           area    = nullptr;     // 透明的“洞”：没被遮住的绘图区，视图方块、导航条、命令行在里面
            MiniGUI::Node*           cube    = nullptr;     // 视图方块
            MiniGUI::Node*           navBar  = nullptr;     // 视图方块下面的导航条
            MiniGUI::Node*           cmdCard = nullptr;     // 底部居中的浮动命令行卡片
            MiniGUI::CommandConsole* console = nullptr;
            StatusBarView*           statusBar     = nullptr;
            MiniGUI::TabView*        layoutTabs    = nullptr;   // 布局标签条（模型 / 图纸空间布局）
            MiniGUI::Node*           layoutTabsBar = nullptr;   // 布局标签条的外层容器（隐藏整行用）
            bool                     syncingLayouts = false;    // 正在程序化同步标签，忽略回调
            std::unique_ptr<PanelSet> panels       = std::make_unique<PanelSet>();   // 这个窗口的特性 / 图层 / 节点面板
            DynamicInputBox*         dynInput      = nullptr;   // 光标旁的长度 / 角度输入框（第一次要显示时建，界面重建后重新建）
            bool                     hovered       = false;     // 光标在这个窗口的视口里
            bool                     navFits       = true;      // 绘图区够高、放得下导航条（不压住命令行），见 SyncViewCube
            float                    navHeight     = 0.0f;      // 导航条上次布局的高度
            double                   cubeAzimuth   = 0.0;       // 视图方块上次绘制时的视线方向
            double                   cubeElevation = 0.0;
        };
        Document*   DocOf(const ViewChrome& c) const;            // 这套界面绑定的文档（可能为空）
        ViewChrome& ChromeOf(const Document* doc);               // 文档所在窗口的一套：它的文档窗口的，否则主窗口的
        void        UseChrome(const ViewChrome& c);              // 它的文档成为当前文档
        void        ActivateDocumentOf(Document* doc);           // 同上，按文档（没变时什么都不做）
        template <class F> void ForEachChrome(F&& f)
        {
            f(m_main);
            for (auto& w : m_docWindows)
                f(w->chrome);
        }
        void SyncCommandLine(ViewChrome& c);
        void SyncLayoutTabs(ViewChrome& c);
        void SyncViewCube(ViewChrome& c);
        void UpdateChromeVisibility(ViewChrome& c);      // 视图方块（开关、布局页不显示）、命令行（开关）、导航条：窗口里没有文档时都不显示
        bool IsCommandLineShown(const ViewChrome& c) const;
        void UpdateFitInsets(const ViewChrome& c, Camera& cam, float pixelScale);   // 按没被遮住的范围设置“全部显示”的留边

        // ── 文档窗口（DocumentWindows.cpp）：一个窗格一个窗口，可以放几个文档，标签在任意窗口之间拖动 ─────
        using PaneId = DocumentManager::PaneId;
        struct ViewInput { int mouseX = 0, mouseY = 0, pressX = 0, pressY = 0; uint8_t buttons = 0; };   // 一个视口里的鼠标状态（像素；buttons 为 MiniCAD 的 MouseButtonState 掩码）
        struct DocWindow
        {
            PaneId                           pane   = DocumentManager::kNoPane;   // 它的窗格（销毁后为 kNoPane）
            std::unique_ptr<IFloatingWindow> window;
            std::unique_ptr<FloatingPanelHost> floatHost;          // 它的停靠面板浮出时的独立窗口（归它所有）；比界面先建、后销毁
            std::unique_ptr<MiniGUI::UiLayout> layout;             // 按界面描述的 layouts.document 生成它的界面（命令表与主窗口共用）
            MiniGUI::Node*                   uiHost = nullptr;     // 界面描述生成的内容放在这里
            MiniGUI::TitleBar*               titleBar = nullptr;   // 界面描述生成的标题栏（重新加载后换新的）；空白处拖动交给系统
            MiniGUI::TabView*                tabs   = nullptr;     // 标题栏里的文档标签条
            MiniGUI::ComboBox*               layerBar = nullptr;   // 功能区里的图层下拉框（自测用）
            MiniGUI::ViewportHost*           host   = nullptr;     // 显示它的视口纹理
            ViewChrome                       chrome;               // 它自己的视图方块、导航条、命令行、状态栏
            std::unique_ptr<IRenderTarget>   target;               // 它的视口渲染目标
            void*                            srv    = nullptr;     // 登记过的着色器资源
            MiniGUI::TextureId               tex    = MiniGUI::InvalidTextureId;
            ViewInput                        input;
            bool                             active = false;       // 窗口是激活的（标题栏按活动样式画）
            MiniGUI::Vec2                    grab, size;           // 拖标题栏：按下点相对窗口左上角的位置、窗口尺寸（主窗口坐标）
        };
        // 文档移到新的独立窗口（标题栏落在 windowPos 下，主窗口坐标）：从哪个窗口来都行，它所在窗口只有它一个时不做什么；
        // 平台不支持时 false。activate = false：不激活窗口（标签拖出过程中原窗口持有鼠标捕获，松开后再激活）
        bool       FloatDocument(Document* doc, MiniGUI::Vec2 windowPos, bool activate = true);
        // 把文档移进窗格（标签插在那个窗口标签条的 index 处，-1 = 末尾）：show 成为当前文档；原窗口空了就关掉
        void       MoveDocumentsTo(const std::vector<Document*>& docs, Document* show, PaneId pane, int index);
        std::vector<Document*> TabOrder(PaneId pane) const;                 // 窗格里的文档，按它的标签条上的顺序
        DocWindow* FindDocWindow(const Document* doc) const;                // 文档所在的文档窗口（在主窗口里时 nullptr）
        DocWindow* FindPaneWindow(PaneId pane) const;
        MiniGUI::TabView* TabsOfPane(PaneId pane) const;                     // 窗口的文档标签条：主窗格是主窗口的
        MiniGUI::Vec2 PaneToMain(PaneId pane, MiniGUI::Vec2 windowPos) const;   // 某个窗口的坐标 → 主窗口坐标
        MiniGUI::Vec2 MainToPane(PaneId pane, MiniGUI::Vec2 mainPos) const;
        void       SetupDocTabs(MiniGUI::TabView& tabs, PaneId pane);        // 文档标签条的行为（主窗口与文档窗口共用）
        void       DestroyDocWindow(PaneId pane);                            // 关闭文档窗口（推迟到下一帧销毁）
        void       PruneDocWindows();                                        // 窗格已经没了（文档都移走 / 关掉）的窗口关掉
        void       PurgeDocWindows();                                        // 每帧开头销毁已关闭的文档窗口
        void       CloseAllDocWindows();
        void       CloseDocWindow(PaneId pane);                              // 窗口的 × / Alt+F4：关闭它里面的文档（未保存的逐个询问）
        void       RenderDocWindow(DocWindow& w, int pixelWidth, int pixelHeight);
        // 拖动中的落点：光标落在哪个窗口的文档标签条上、插在第几个（exclude：被拖的窗口自己）
        struct DropTarget { PaneId pane = DocumentManager::kNoPane; int index = -1; bool Valid() const { return index >= 0; } };
        DropTarget FindDropTarget(MiniGUI::Vec2 mainPos, PaneId exclude) const;
        void       ShowDropTarget(const DropTarget& target);                 // 落点的标签条画插入标记，其余清掉
        // 系统在移动窗口（拖标题栏）：画落点；在标签条上时窗口让到标签条下面（windowRect 主窗口坐标，可改）；松开在标签条上就合并过去
        void       OnDocWindowMoving(DocWindow& w, bool done, MiniGUI::Vec2 cursor, MiniGUI::Rect& windowRect);
        bool       OnDocTabDragOut(PaneId source, int index, MiniGUI::Vec2 windowPos);   // 标签拖出标签条：新窗口，或拖动只有它的窗口
        void       OnDocTabTornDrag(PaneId source, MiniGUI::FloatingPanel::DragPhase phase, MiniGUI::Vec2 windowPos);   // 继续拖：窗口跟着光标走
        void       DragDocWindow(DocWindow& w, MiniGUI::FloatingPanel::DragPhase phase, MiniGUI::Vec2 mainPos);   // 标签拖出后：移动窗口、画落点、松开在标签条上合并
        bool       BlockedByModal();                                         // 主窗口开着对话框：把主窗口置前并返回 true（文档窗口的操作不执行）
        void       RaiseMainForModal();                                      // 每帧：主窗口刚弹出对话框时置前，不被文档窗口挡住
        void       SyncDocWindowChrome(DocWindow& w);                        // 文档窗口的每一帧：命令行、状态栏、视图方块跟随它的文档
        MiniGUI::ViewportHost& HostOf(const Document* doc) const;            // 文档所在的视口控件：它的窗口的，否则主窗口的
        ViewInput& InputOf(const Document* doc);
        void       RouteViewportPointer(const MiniGUI::ViewportPointerEvent& e, Document* doc, ViewInput& in, MiniGUI::ViewportHost& host);
        void       RequestRenderAll();                                       // 主视口与所有文档窗口都重绘
        void       SetDocWindowsTheme(const MiniGUI::ThemeColors& theme);
        void       ApplyDocWindowUi(DocWindow& w);                           // 按界面描述文件（重新）生成文档窗口的界面
        MiniGUI::UiLayout* CurrentLayout() const;                            // 用户正在用的窗口（新文档会放进的那个）的界面
        MiniGUI::Ribbon*    CurrentRibbon() const;                           // 它的功能区
        MiniGUI::DockSpace* CurrentDock() const;                             // 它的停靠区（特性、图层、节点面板）
        void       SyncViewCube();                                           // 主窗口的视图方块跟着主视口里文档的视线方向
    public:
        size_t     DocWindowCount() const { return m_docWindows.size(); }    // 自测用
    private:

    private:
        AppPlatform* m_platform = nullptr;
        float        m_dpiScale = 1.0f;     // 物理像素 / 逻辑像素

        // ── MiniCAD ─────────────────────────────────────────────
        FontSystem                         m_fontSystem;
        DocumentManager                    m_docManager;

        // ── MiniGUI ─────────────────────────────────────────────
        std::unique_ptr<FloatingPanelHost>     m_floatHost;              // 浮出的停靠面板的独立窗口；先于 m_ui 声明：停靠区销毁时还要经它取回内容
        std::unique_ptr<MiniGUI::UIContext>    m_ui;
        MiniGUI::ViewportHost*                 m_viewport   = nullptr;
        MiniGUI::TabView*                      m_docTabs    = nullptr;
        ViewChrome                             m_main{ .main = true, .pane = DocumentManager::kMainPane };   // 主窗口的视图方块、导航条、命令行、状态栏
        MiniGUI::Node*                         m_startPage   = nullptr;  // 起始页（StartPage.cpp，界面描述文件里的面板 "start"）
        MiniGUI::Node*                         m_startRecent = nullptr;  // 起始页里放最近打开列表的容器
        MiniGUI::Button*                       m_startBack   = nullptr;  // 起始页导航列的“返回 文档名”
        std::vector<std::pair<MiniGUI::Button*, std::string>> m_startNav; // 起始页导航列里执行命令的行
        std::vector<MiniGUI::Node*>            m_startHidden;            // 显示起始页时隐藏的界面部分（收起时恢复）
        bool                                   m_startShown = false;     // 有文档时也显示起始页（标题栏首页键，Esc 返回）
        const Document*                        m_startOver  = nullptr;   // 显示起始页时的当前文档；当前文档变了就回到视口
        MiniGUI::Node*                         m_appBar     = nullptr;   // 标题栏右侧的 AI / 设置 / 帮助 / 版本（窗口窄时隐藏）
        float                                  m_appBarWidth    = 0.0f;  // 显示时量到的宽度与它右边到标题栏右端的距离
        float                                  m_appBarRightGap = 0.0f;
        bool                                   m_commandLineVisible = true; // 命令行的显示开关（各窗口共用，保存在用户布局文件里）
        bool                                   m_viewCubeVisible = true; // 视图方块的显示开关（同上）
        MiniGUI::Label*                        m_uiStatus   = nullptr;   // 界面描述文件的加载结果
        MiniGUI::ComboBox*                     m_layerBarLayer = nullptr;  // 工具栏的图层下拉框（自测用）
        MiniGUI::Popup*                        m_textPopup  = nullptr;   // 文字 / 多行文字原位编辑
        MiniGUI::Dialog*                       m_blockNameDialog = nullptr;
        MiniGUI::Dialog*                       m_insertDialog    = nullptr;
        MiniGUI::Dialog*                       m_arrayDialog     = nullptr;
        bool                                   m_arrayPicking    = false;   // 阵列对话框为拾取中心点暂时关闭
        MiniGUI::Dialog*                       m_hatchDialog     = nullptr;
        MiniGUI::Dialog*                       m_textStyleDialog = nullptr;
        MiniGUI::Dialog*                       m_mlineStyleDialog = nullptr;
        MiniGUI::Dialog*                       m_pointStyleDialog = nullptr;  // 点样式对话框（$PDMODE/$PDSIZE）
        Document*                              m_pointStyleDoc    = nullptr;  // 它对应的文档；文档换了就关掉
        MiniGUI::Dialog*                       m_lineTypeScaleDialog = nullptr;  // 线型比例对话框（$LTSCALE）
        Document*                              m_lineTypeScaleDoc    = nullptr;  // 同上
        void*                                  m_viewSRV    = nullptr;   // 当前登记的视口 SRV（尺寸变化后会重建）
        MiniGUI::TextureId                     m_viewTex    = MiniGUI::InvalidTextureId;
        uint64_t                               m_viewTexRebuilds = 0;    // 视口渲染目标重建（重新登记纹理）的次数，自测用
        std::string                            m_title;                  // 标题栏文字
        std::string                            m_pendingSavePath;        // 另存为对话框选定的路径，交给 DocumentManager 的文件对话框回调
        CadSaveVersion                         m_pendingSaveVersion = kDefaultCadSaveVersion;

        // ── 命令与界面描述（注册表要比工具栏活得久：先声明、后销毁）──
        MiniGUI::CommandRegistry               m_commands;
        std::unique_ptr<MiniGUI::UiLayout>     m_layout;
        std::array<SelectionSummary, 4>        m_selectionCache;       // 见 SelectionInfo：每个窗口的文档各缓存一份
        size_t                                 m_selectionNext = 0;
        MiniGUI::Node*                         m_uiHost     = nullptr;   // 界面描述生成的内容放在这里
        std::string                            m_uiPath;                 // 界面描述文件（UTF-8）
        int64_t                                m_uiWriteTime = 0;
        std::unordered_map<std::string, std::string> m_toolCommands;     // Editor 工具 ID → 命令 ID（状态栏显示工具名）
        bool                                   m_useUserLayout = true;
        bool                                   m_syncing    = false;     // SyncDocuments 进行中：忽略标签的选中回调
        RecentFiles                            m_recentFiles;            // 「文件 > 最近打开」，存到 exe 目录下的 recent.txt

        // ── 节点图 ──────────────────────────────────────────────
        Nodes::PreviewBuilder                  m_nodePreview;            // 预览几何（缓存内核实体的离散结果）
        std::map<const Document*, std::unique_ptr<Nodes::GraphHistory>> m_nodeHistory;   // 每个文档一份节点图撤销历史
        const Document*                        m_nodePreviewDoc = nullptr;   // 预览对应的文档
        uint64_t                               m_nodePreviewSig = 0;         // 预览对应的求值版本、预览开关、连线、选择
        // 一次后台求值，与工作线程共享：工作线程只碰 copy（节点图的副本）与几个原子量，界面线程在 finished 之后才读 copy
        struct NodeEvalJob
        {
            std::unique_ptr<Nodes::Graph>                   copy;
            uint64_t                                        graph = 0;   // 节点图的 Uid
            std::vector<std::pair<Nodes::NodeId, uint64_t>> pending;     // 要算的节点与脏戳：本图里有变化就取消
            std::atomic<bool>                               cancel{ false };
            std::atomic<bool>                               userStop{ false };
            std::atomic<bool>                               finished{ false };
            std::atomic<int>                                solved{ 0 };
            std::atomic<int64_t>                            lastRedraw{ 0 };   // 上次为进度请求重绘的时间（毫秒），限流用
            std::chrono::steady_clock::time_point           start;
        };
        std::shared_ptr<NodeEvalJob>           m_nodeJob;                    // 正在后台求值；空 = 空闲
        struct { uint64_t graph = 0, dirty = 0, scene = 0; } m_nodePause;    // 用户停止求值时的图、标脏次数、场景版本
        bool                                   m_nodeEvalSync = false;       // 不用后台线程（自测里检查同步结果）
        // 脏节点上次耗时之和不超过它（毫秒）时在界面线程上同步求值，当帧出结果，与 N11 之前一样；更重的交给后台
        double                                 m_nodeSyncBudget = 40.0;

        // ── 后台打开图纸（OpenDrawings）───────────────────────────
        struct OpenJob;                                                  // 一张图纸的读取任务，与后台线程共享（MainFrame.cpp）
        std::shared_ptr<OpenJob>               m_openJob;                // 正在读取的图纸；空 = 空闲
        std::shared_ptr<OpenJob>               m_openDone;               // 已读完、等本帧画完后加入的图纸
        bool                                   m_openAddPosted = false;  // 已投递"加入 m_openDone"（AppPlatform::Post）
        std::vector<std::string>               m_openQueue;              // 排队等待读取的路径
        DocumentManager::PaneId                m_openPane = DocumentManager::kMainPane;   // 读完的图纸加进哪个窗口（发起打开时的）
        std::string                            m_openFailures;           // 本批打开失败的汇总，全部读完后显示
        std::vector<Document*>                 m_xrefMissingDocs;        // 本批打开的、有找不到的外部参照的图纸
        bool                                   m_openedAny  = false;
        bool                                   m_hideOpenProgress = false;   // 最后一张已加入：它生成完显示后隐藏进度
        Document*                             m_buildDoc = nullptr;     // 最近加入、还在生成显示的图纸（只用来比较，可能已关闭）
        std::string                            m_buildName;
        MiniGUI::Node*                         m_openBar    = nullptr;   // 状态栏里的读取进度
        MiniGUI::Label*                        m_openLabel  = nullptr;
        MiniGUI::ProgressBar*                  m_openProgress = nullptr;
        int                                    m_openPercent = -1;       // 进度条当前显示的百分比
        MiniGUI::Node*                         m_saveBar    = nullptr;   // 状态栏里的保存进度
        MiniGUI::Label*                        m_saveLabel  = nullptr;
        MiniGUI::ProgressBar*                  m_saveProgress = nullptr;
        uint32_t                               m_saveHideTimer = 0;      // 保存完成后隐藏进度的定时器
        bool                                   m_saveStatusShown = false;   // 本次保存前已显示"正在保存"（据此区分失败与取消另存为）

        // ── 视口指针状态（物理像素，视口内坐标）──────────────────
        ViewInput m_mainInput;      // 主视口的鼠标状态（光标是否在视口里：m_main.hovered）
        std::vector<std::unique_ptr<DocWindow>> m_docWindows;
        std::vector<std::unique_ptr<DocWindow>> m_docWindowGraveyard;    // 已关闭、等下一帧销毁
        // 正从标签条拖出、鼠标还没松开：哪个文档、被拖的窗口（新建的，或只有它的原窗口）、按住的是哪个窗口的标签条
        struct TornDrag { Document* doc = nullptr; PaneId window = DocumentManager::kNoPane; PaneId source = DocumentManager::kNoPane; };
        TornDrag                                m_torn;
        bool                                    m_modalRaised = false;   // 上一帧主窗口已开着对话框（RaiseMainForModal）

        // ── 统计 ────────────────────────────────────────────────
        uint64_t m_frames          = 0;
        uint64_t m_viewportFrames  = 0;
        int64_t  m_pendingInputTicks = 0;    // 尚未呈现的最早一次输入（NowTicks）
        double   m_latencyLast     = 0.0;    // 毫秒：输入 → Present 返回
        double   m_latencyMax      = 0.0;

        // 最近一帧各阶段耗时（毫秒）。m_syncGpu 为 true 时（只在自测里）每个阶段后等 GPU 做完，
        // 得到包含 GPU 的耗时；平时 GPU 与 CPU 并行，Present 里才等待
        struct FrameTiming
        {
            double layout   = 0.0;      // 状态同步 + 布局（含命令行 / 动态输入同步后的第二次布局）
            double status   = 0.0;      //   其中：节点图 / 状态栏 / 读取进度同步
            double layout1  = 0.0;      //   其中：第一次布局（UIContext::Update）
            double layout2  = 0.0;      //   其中：命令行 / 动态输入同步 + 第二次布局
            double viewport = 0.0;      // CAD 视口渲染（Editor::Render + Viewport::Render）
            double ui       = 0.0;      // 界面绘制（生成 DrawList + 后端绘制）
            double present  = 0.0;      // Present（含垂直同步等待）
            bool   viewportRendered = false;
        };
        FrameTiming m_timing;
        bool        m_syncGpu = false;
    };
}

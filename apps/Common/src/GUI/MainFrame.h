#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
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
#include "Widgets/Menu.h"

namespace MiniGUI
{
    class UIContext;
    class TitleBar;
    class ViewportHost;
    class CommandConsole;
    class Label;
    class ListView;
    class Node;
    class ProgressBar;
    class TabView;
    class UiLayout;
    struct ViewportPointerEvent;
    struct KeyEvent;
}

namespace MiniGUI
{
    class Dialog;
    class Popup;
}

namespace MiniCAD
{
    class StatusBarView;
    struct EntityAttr;
    class DynamicInputBox;

    // MiniCAD 主窗口的界面与逻辑（桌面版和网页版共用）。整个界面由 MiniGUI 绘制，平台相关的部分
    // （窗口、图形设备、消息循环、文件对话框）由 AppPlatform 提供：Win32 版见 apps/Win32/src/Host，网页版见 apps/Web/src/Host。
    //   - 标题栏自绘，菜单栏在标题栏里；桌面版右侧有最小化 / 最大化 / 关闭（无边框窗口，Win32Frame）
    //   - 多文档标签：所有文档共用一个视口（MiniCAD 的 DocumentManager 只有一个 Viewport + Editor，切换文档时换相机状态）
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

        // 界面描述文件（UTF-8 路径）；不设置时优先用源码目录里的文件，其次是可执行文件旁的 ui/minicad_ui.json
        void SetUiFile(std::string utf8Path) { m_uiPath = std::move(utf8Path); }
        // 不读写用户布局文件（自测用，避免受本机保存的布局影响）
        void SetUseUserLayout(bool use) { m_useUserLayout = use; }

        // 平台对象必须比 MainFrame 活得久。尺寸为逻辑像素，pixelScale = 物理像素 / 逻辑像素
        bool Initialize(AppPlatform& platform, float width, float height, float pixelScale);
        int  RunSelfTest();
        int  RunBenchmark(const std::string& path);     // 只有 Win32 版：--bench 图纸，打印打开、滚轮缩放的耗时与内存

        // ── 由宿主调用 ───────────────────────────────────────────
        void OnResize(float width, float height, float pixelScale);    // 窗口尺寸或 DPI 变化
        void RenderFrame();
        void RequestExit();                     // 有未保存的文档时先询问，确认后调用 AppPlatform::Quit
        void StateChanged();                    // 文档、选择集、当前工具可能变了：刷新命令状态、标签、面板、标题
        // 菜单、启动参数和拖放共用此入口；逐个在后台读取（状态栏显示进度），读完一个加入一个，失败汇总显示，成功文档保留。
        void OpenDrawings(const std::vector<std::string>& paths);
        bool IsOpeningDrawings() const { return m_openJob || m_openDone || !m_openQueue.empty(); }   // 还有图纸在读取或排队
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
        std::unique_ptr<MiniGUI::Node> CreateDocumentArea();      // 文档标签 + 视口（没有文档时显示提示）
        std::unique_ptr<MiniGUI::Node> CreatePropertiesPanel();
        std::unique_ptr<MiniGUI::Node> CreateStatusBar();
        std::unique_ptr<MiniGUI::Node> CreateLayerPanel();
        std::unique_ptr<MiniGUI::Node> CreateLayerBar();          // 工具栏里的图层 / 线型 / 线宽下拉框
        std::unique_ptr<MiniGUI::Node> CreateOpenProgress();      // 状态栏里的读取进度（文件名 + 进度条），只在打开图纸时显示
        std::unique_ptr<MiniGUI::Node> CreateCommandLine();       // 命令行（CommandLine.cpp）
        void SyncCommandLine();                                   // 回显与提示同步到命令行
        void RunCommandLine(const std::string& text);             // 命令行提交
        bool RouteKeyToCommandLine(const MiniGUI::KeyEvent& e);   // 绘图区直接打字进入命令行

        // ── 动态输入（DynamicInput.cpp）与 Editor 请求的对话框（EditorDialogs.cpp）──
        void SyncDynamicInput();                                  // 光标旁的长度 / 角度输入框跟随 Editor 状态
        bool RouteKeyToDynamicInput(const MiniGUI::KeyEvent& e);  // 工具进行中按数字：交给动态输入或命令行
        void SendKeyToEditor(MiniGUI::Key key);                   // 合成一次按下 + 抬起交给当前工具（选项字母、Enter、Esc）
        bool        IsDynInputVisible() const;                    // 自测用
        bool        DynInputHasFocus() const;
        std::string GetDynInputText(int field) const;             // 0 长度，1 角度
        void SyncEditorRequests();                                // Editor 发出的请求（文字输入、块名、插入块、阵列、填充、文字样式）→ 打开 / 关闭对应界面
        void OpenTextEditor(bool multiline);
        void OpenBlockNameDialog();
        void OpenBlockInsertDialog();
        void OpenArrayDialog();
        void OpenHatchDialog();
        void OpenTextStyleDialog();
        void OpenPointStyleDialog();                             // 格式 → 点样式…（$PDMODE/$PDSIZE）
        void LoadHatchPatterns();                                 // 载入 patterns/ 目录下的 .pat 文件（内置图案之外）
        void ShowAbout();
        void RefreshLayerPanel();
        static std::vector<uint32_t> SortedLayerIds(Document& doc);     // 按 ID（创建顺序）排列，下拉框和图层面板都用这个顺序
        // 选中实体的公共属性下标：没有选中返回 -1，取值不同返回 nullopt（"*多种*"）。特性面板与工具栏下拉框共用
        static std::optional<int> SelectionCommonIndex(Editor& editor, const std::function<int(const EntityAttr&)>& indexOf);

        // ── 界面描述文件与用户布局（MainPanels.cpp）────────────────
        void ReloadUi();
        void SetViewCubeVisible(bool visible);   // 显示 / 隐藏视图方块，并写进用户布局文件
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
        void AddOpened();                       // 帧尾：把读完的图纸加入文档列表（下一帧生成显示），继续下一张
        void EndOpenBatch();                    // 队列读完：同步界面，显示失败汇总
        void SyncOpenProgress();                // 每帧：把后台读取进度同步到状态栏
        void UpdateTitle();
        void ActivateDocument(Document* doc);
        void CloseDocument(Document* doc);      // 未保存时先询问
        // 保存当前 / 全部文档，保存成功后通知平台（网页版下载）。onSaved：当前文档保存成功后调用
        // （平台没有系统另存为对话框时，另存为经 OpenSaveAsDialog 异步完成）
        void SaveDocuments(bool all, bool saveAs, std::function<void()> onSaved = {});
        void OpenSaveAsDialog(Document* doc, std::function<void()> onSaved);      // 文件名 + 格式（EditorDialogs.cpp）
        void LayerChanged();                    // 图层特性改了：重建场景显示、刷新面板、重绘视口

        void RenderViewport(int pixelWidth, int pixelHeight);
        void UpdateStatus();

        // ── 视口输入 → Editor ─────────────────────────────────────
        void OnViewportPointer(const MiniGUI::ViewportPointerEvent& e);
        bool OnViewportKey(const MiniGUI::KeyEvent& e);
        void NoteInput();       // 记录输入时间，用于统计输入到呈现的延迟

    private:
        AppPlatform* m_platform = nullptr;
        float        m_dpiScale = 1.0f;     // 物理像素 / 逻辑像素

        // ── MiniCAD ─────────────────────────────────────────────
        FontSystem                         m_fontSystem;
        DocumentManager                    m_docManager;

        // ── MiniGUI ─────────────────────────────────────────────
        std::unique_ptr<MiniGUI::UIContext>    m_ui;
        MiniGUI::ViewportHost*                 m_viewport   = nullptr;
        MiniGUI::TabView*                      m_docTabs    = nullptr;
        MiniGUI::Node*                         m_noDocHint  = nullptr;   // 没有文档时代替视口显示
        MiniGUI::Node*                         m_viewCube   = nullptr;   // 视口右上角的视图方块（M6）
        bool                                   m_viewCubeVisible = true; // 视图方块的显示开关（保存在用户布局文件里）
        double                                 m_cubeAzimuth   = 0.0;    // 视图方块上次绘制时的视线方向
        double                                 m_cubeElevation = 0.0;
        StatusBarView*                         m_statusBar  = nullptr;
        MiniGUI::Label*                        m_uiStatus   = nullptr;   // 界面描述文件的加载结果
        MiniGUI::ListView*                     m_layerList  = nullptr;
        MiniGUI::ComboBox*                     m_layerBarLayer = nullptr;  // 工具栏的图层下拉框（自测用）
        MiniGUI::CommandConsole*               m_console    = nullptr;
        DynamicInputBox*                       m_dynInput   = nullptr;
        MiniGUI::Popup*                        m_textPopup  = nullptr;   // 文字 / 多行文字原位编辑
        MiniGUI::Dialog*                       m_blockNameDialog = nullptr;
        MiniGUI::Dialog*                       m_insertDialog    = nullptr;
        MiniGUI::Dialog*                       m_arrayDialog     = nullptr;
        bool                                   m_arrayPicking    = false;   // 阵列对话框为拾取中心点暂时关闭
        MiniGUI::Dialog*                       m_hatchDialog     = nullptr;
        MiniGUI::Dialog*                       m_textStyleDialog = nullptr;
        MiniGUI::Dialog*                       m_pointStyleDialog = nullptr;  // 点样式对话框（$PDMODE/$PDSIZE）
        Document*                              m_pointStyleDoc    = nullptr;  // 它对应的文档；文档换了就关掉
        std::vector<uint32_t>                  m_layerIds;               // 图层面板每一行对应的图层 ID
        std::string                            m_layerSignature;         // 图层面板当前显示的内容，没变时不重建行
        void*                                  m_viewSRV    = nullptr;   // 当前登记的视口 SRV（尺寸变化后会重建）
        MiniGUI::TextureId                     m_viewTex    = MiniGUI::InvalidTextureId;
        uint64_t                               m_viewTexRebuilds = 0;    // 视口渲染目标重建（重新登记纹理）的次数，自测用
        std::string                            m_title;                  // 标题栏文字
        std::string                            m_pendingSavePath;        // 另存为对话框选定的路径，交给 DocumentManager 的文件对话框回调
        CadSaveVersion                         m_pendingSaveVersion = kDefaultCadSaveVersion;

        // ── 命令与界面描述（注册表要比工具栏活得久：先声明、后销毁）──
        MiniGUI::CommandRegistry               m_commands;
        std::unique_ptr<MiniGUI::UiLayout>     m_layout;
        MiniGUI::BindingSet                    m_bindings;
        std::function<void()>                  m_syncGeometryRows;     // 特性面板「几何」组：按选择集重新分配行
        MiniGUI::Node*                         m_uiHost     = nullptr;   // 界面描述生成的内容放在这里
        std::string                            m_uiPath;                 // 界面描述文件（UTF-8）
        int64_t                                m_uiWriteTime = 0;
        std::unordered_map<std::string, std::string> m_toolCommands;     // Editor 工具 ID → 命令 ID（状态栏显示工具名）
        bool                                   m_useUserLayout = true;
        bool                                   m_syncing    = false;     // SyncDocuments 进行中：忽略标签的选中回调
        RecentFiles                            m_recentFiles;            // 「文件 > 最近打开」，存到 exe 目录下的 recent.txt

        // ── 后台打开图纸（OpenDrawings）───────────────────────────
        struct OpenJob;                                                  // 一张图纸的读取任务，与后台线程共享（MainFrame.cpp）
        std::shared_ptr<OpenJob>               m_openJob;                // 正在读取的图纸；空 = 空闲
        std::shared_ptr<OpenJob>               m_openDone;               // 已读完、等本帧画完后加入的图纸
        bool                                   m_openAddPosted = false;  // 已投递"加入 m_openDone"（AppPlatform::Post）
        std::vector<std::string>               m_openQueue;              // 排队等待读取的路径
        std::string                            m_openFailures;           // 本批打开失败的汇总，全部读完后显示
        bool                                   m_openedAny  = false;
        bool                                   m_hideOpenProgress = false;   // 最后一张已加入：画完它的首帧后隐藏进度
        MiniGUI::Node*                         m_openBar    = nullptr;   // 状态栏里的读取进度
        MiniGUI::Label*                        m_openLabel  = nullptr;
        MiniGUI::ProgressBar*                  m_openProgress = nullptr;
        int                                    m_openPercent = -1;       // 进度条当前显示的百分比

        // ── 视口指针状态（物理像素，视口内坐标）──────────────────
        int     m_mouseX   = 0;
        int     m_mouseY   = 0;
        int     m_pressX   = 0;
        int     m_pressY   = 0;
        uint8_t m_buttons  = 0;     // MiniCAD 的 MouseButtonState 掩码
        bool    m_hovered  = false;

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
            double viewport = 0.0;      // CAD 视口渲染（Editor::Render + Viewport::Render）
            double ui       = 0.0;      // 界面绘制（生成 DrawList + 后端绘制）
            double present  = 0.0;      // Present（含垂直同步等待）
            bool   viewportRendered = false;
        };
        FrameTiming m_timing;
        bool        m_syncGpu = false;
    };
}

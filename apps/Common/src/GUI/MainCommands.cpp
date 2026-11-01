// ── MiniCAD 命令注册：菜单栏、工具栏、快捷键都引用这里的命令 ID ─────────
// 名称、图标、快捷键是默认值，界面描述文件（ui/minicad_ui.json）的 "commands" 可以覆盖
#include "GUI/MainFrame.h"
#include "GUI/FloatingPanelHost.h"
#include "GUI/Language.h"
#include "MiniI18n/I18n.h"
#include "GUI/NodeEditor.h"
#include "Document/Document.h"
#include "Document/Command/LayerCommands.h"
#include "Document/Command/LayoutCommands.h"
#include "Import/CadExchange.h"
#include "Widgets/Ribbon.h"
#include "Widgets/TabView.h"
#include "Widgets/TitleBar.h"
#include "Core/UIContext.h"
#include "Style/ThemeColors.h"
#include "Widgets/Dialog.h"
#include "Widgets/DockSpace.h"
#include "Widgets/UiLayout.h"
#include "Widgets/ViewportHost.h"

namespace MiniCAD
{
    void MainFrame::RegisterCommands()
    {
        using MiniGUI::Command;
        DocumentManager& dm = m_docManager;
        auto hasDoc       = [&dm] { return dm.GetActive() != nullptr; };
        auto hasSelection = [&dm] { return dm.GetActive() && !dm.GetEditor().GetSelection().empty(); };

        // 另存为的路径由平台选择（Win32：系统对话框；网页：浏览器内存文件系统，保存后下载）。
        // 打开文件不经过 DocumentManager::Open()：网页版的文件选择是异步的，见 file.open
        AppPlatform* platform = m_platform;
        dm.SetFileDialogHandler([this, platform, &dm](bool save)
        {
            if (!save)
                return std::string();
            Document* doc = dm.GetActive();
            if (!m_pendingSavePath.empty())
            {
                if (doc)
                    doc->SetCadSaveVersion(m_pendingSaveVersion);
                return m_pendingSavePath;           // 另存为对话框（没有系统对话框的平台）已经选好
            }
            CadSaveVersion version = doc ? doc->GetCadSaveVersion() : kDefaultCadSaveVersion;
            const std::string path = platform->ChooseSavePath(doc ? doc->GetName() : Tr("未命名"), version);
            if (doc && !path.empty())
                doc->SetCadSaveVersion(version);    // DWG / DXF 按所选版本写出
            return path;
        });

        // ── 窗口：标题栏的最小化 / 最大化 / 关闭按钮执行这三个命令（网页版没有窗口按钮：不注册，标题栏就不显示）──
        if (platform->HasWindowControls())
        {
            m_commands.Register({ .id = MiniGUI::TitleBar::kMinimizeCommand, .label = N_("最小化(&N)"), .execute = [platform] { platform->MinimizeWindow(); } });
            m_commands.Register({ .id = MiniGUI::TitleBar::kMaximizeCommand, .label = N_("最大化(&X)"),
                                  .execute = [platform] { platform->ToggleMaximizeWindow(); }, .isChecked = [platform] { return platform->IsWindowMaximized(); } });
            m_commands.Register({ .id = MiniGUI::TitleBar::kCloseCommand,    .label = N_("关闭(&C)"),   .execute = [platform] { platform->CloseWindow(); } });
        }

        // ── 文件 ─────────────────────────────────────────────────
        m_commands.Register({ .id = "file.new",     .label = N_("新建(&N)"),  .icon = "New.png",   .shortcut = "Ctrl+N",       .execute = [&dm] { dm.New(); }});
        m_commands.Register({ .id = "file.open",    .label = N_("打开(&O)…"), .icon = "Open.png",   .shortcut = "Ctrl+O",
                              .execute = [this, platform, &dm]
                              {
                                  platform->PickDrawingFiles([this](const std::vector<std::string>& paths)
                                  {
                                      OpenDrawings(paths);
                                  });
                              } });
        m_commands.Register({ .id = "file.save",    .label = N_("保存(&S)"),   .icon = "Save.png", .shortcut = "Ctrl+S",       .execute = [this] { SaveWithProgress(false); }, .canExecute = hasDoc });
       
        m_commands.Register({ .id = "file.saveAs",  .label = N_("另存为(&A)…"),  .shortcut = "Ctrl+Shift+S", .execute = [this] { SaveDocuments(false, true); },  .canExecute = hasDoc });
        m_commands.Register({ .id = "file.saveAll", .label = N_("全部保存(&L)"), .shortcut = "Ctrl+Alt+S",   .execute = [this] { SaveWithProgress(true); },  .canExecute = hasDoc });
        m_commands.Register({ .id = "file.close",   .label = N_("关闭(&C)"),     .shortcut = "Ctrl+W",
                              .execute = [this, &dm] { CloseDocument(dm.GetActive()); }, .canExecute = hasDoc });
        m_commands.Register({ .id = "xref.locate", .label = N_("查找外部参照(&E)…"),
                              .tooltip = N_("为没有找到的外部参照选择所在的文件夹，在里面查找并读入（命令 XREF）"),
                              .execute = [this, &dm]
                              {
                                  Document* doc = dm.GetActive();
                                  if (!doc) return;
                                  const size_t missing = FindUnresolvedXrefs(doc->GetScene()).size();
                                  if (missing == 0)
                                  {
                                      dm.GetEditor().GetCmdLine().Echo(Tr("没有找不到的外部参照"));
                                      return;
                                  }
                                  dm.GetEditor().GetCmdLine().Echo(TrF("{0} 个外部参照没有找到，请选择所在的文件夹", missing));
                                  LocateXrefs({ doc });
                              },
                              .canExecute = hasDoc });
        m_commands.Register({ .id = "app.exit",     .label = N_("退出(&X)"),     .shortcut = "Alt+F4",       .execute = [platform] { platform->CloseWindow(); } });
        m_commands.Register({ .id = "view.start", .label = N_("起始页(&H)"),
                              .tooltip = N_("显示起始页：新建、打开、最近的图纸（Esc 返回当前图纸）"), .icon = "Home.png",
                              .execute    = [this] { ShowStartPage(!m_startShown); },
                              .canExecute = hasDoc,
                              .isChecked  = [this] { return m_startShown || !m_docManager.GetMainDocument(); } });
        m_commands.Register({ .id = "ui.search", .label = N_("搜索命令(&K)"),
                              .tooltip = N_("在功能区右上角的搜索框里按名称或所在选项卡查找命令，Enter 执行"), .shortcut = "Ctrl+K",
                              .execute    = [this] { if (MiniGUI::Ribbon* r = CurrentRibbon()) r->FocusSearch(); },
                              .canExecute = [this]
                              {
                                  // 用户正在用的那个窗口的功能区；主窗口显示起始页时它的功能区是隐藏的
                                  const MiniGUI::Ribbon* r = CurrentRibbon();
                                  const bool inMain = r == m_layout->GetRibbon();
                                  return r && r->GetSearchBox() && m_docManager.GetActive() && !(inMain && (m_startShown || !m_docManager.GetMainDocument()));
                              } });
        // 「最近打开」的内容由 BuildRecentMenuItems() 在打开菜单时生成，这两条供命令行 / 快捷键直接调用
        m_commands.Register({ .id = "file.recent.clear", .label = N_("清除最近打开记录(&C)"),
                              .tooltip = N_("清空「文件 > 最近打开」列表"),
                              .execute    = [this] { ClearRecentFiles(); },
                              .canExecute = [this] { return !m_recentFiles.Empty(); } });

        // ── 文档标签：按标签顺序切换 ────────────────────────────────
        auto stepDoc = [this](int dir)
        {
            const int n = m_docTabs->GetTabCount();
            if (n > 1)
                m_docTabs->SetSelected((m_docTabs->GetSelected() + dir + n) % n);     // 选中回调里激活文档
        };
        auto multiDoc = [&dm] { return dm.GetAll().size() > 1; };
        m_commands.Register({ .id = "doc.next", .label = N_("下一个文档(&N)"), .shortcut = "Ctrl+Tab",       .execute = [stepDoc] { stepDoc(1); },  .canExecute = multiDoc });
        m_commands.Register({ .id = "doc.prev", .label = N_("上一个文档(&P)"), .shortcut = "Ctrl+Shift+Tab", .execute = [stepDoc] { stepDoc(-1); }, .canExecute = multiDoc });

        // ── 编辑 ─────────────────────────────────────────────────
        // 节点编辑器有焦点时，撤销 / 重做 / 全选作用于节点图（节点图有自己的撤销历史，见 docs/NodeGraph/PLAN.md 第 3.6 节）
        m_commands.Register({ .id = "edit.undo", .label = N_("撤销(&U)"), .icon = "Undo.png", .shortcut = "Ctrl+Z",
                              .execute = [this, &dm] { if (NodeEditorHasFocus()) { NodeGraphUndo(false); return; } dm.Undo(); RequestRenderAll(); },
                              .canExecute = [this, &dm] { return dm.GetActive() && (NodeEditorHasFocus() ? NodeHistory(*dm.GetActive()).CanUndo() : dm.GetActive()->CanUndo()); } });
        m_commands.Register({ .id = "edit.redo", .label = N_("重做(&R)"), .icon = "Redo.png", .shortcut = "Ctrl+Y",
                              .execute = [this, &dm] { if (NodeEditorHasFocus()) { NodeGraphUndo(true); return; } dm.Redo(); RequestRenderAll(); },
                              .canExecute = [this, &dm] { return dm.GetActive() && (NodeEditorHasFocus() ? NodeHistory(*dm.GetActive()).CanRedo() : dm.GetActive()->CanRedo()); } });
        // 删除：与视口里按 Delete 键是同一个操作（Delete 键由 Editor 处理，这里不再注册快捷键，避免文本框里按 Delete 被抢走）
        m_commands.Register({ .id = "edit.delete", .label = N_("删除(&D)"),
                              .execute = [&dm] { dm.GetEditor().DeleteSelected(); },
                              .canExecute = [&dm] { return dm.GetActive() && !dm.GetEditor().GetSelection().empty() && !dm.GetEditor().IsToolRunning(); } });
        m_commands.Register({ .id = "modify.explode", .label = N_("分解(&X)"),
                              .execute = [&dm] { dm.GetEditor().ExplodeSelection(); },
                              .canExecute = [&dm] { return dm.GetActive() && !dm.GetEditor().GetSelection().empty() && !dm.GetEditor().IsToolRunning(); } });
        m_commands.Register({ .id = "edit.selectAll", .label = N_("全选(&A)"), .shortcut = "Ctrl+A",
                              .execute = [this, &dm]
                              {
                                  if (NodeEditorHasFocus())
                                  {
                                      std::set<Nodes::NodeId> all;
                                      for (const auto& n : dm.GetActive()->GetNodeGraph().GetNodes())
                                          all.insert(n->Id);
                                      ActiveNodeEditor()->SetSelection(std::move(all));
                                      return;
                                  }
                                  dm.GetEditor().SelectAll();
                              },
                              .canExecute = [&dm] { return dm.GetActive() && !dm.GetEditor().IsToolRunning(); } });
        // 剪切 / 复制 / 粘贴：节点编辑器有焦点时作用于节点（子图经剪贴板，见 docs/NodeGraph/PLAN.md 第 9 节）
        auto hasNodeOrEntitySelection = [this, hasSelection]
        {
            return NodeEditorHasFocus() ? !ActiveNodeEditor()->GetSelection().empty() : hasSelection();
        };
        m_commands.Register({ .id = "edit.cut",      .label = N_("剪切(&T)"),       .shortcut = "Ctrl+X",
                              .execute = [this, &dm] { if (NodeEditorHasFocus()) { ActiveNodeEditor()->CutSelection(); return; } dm.CutSelected(); },
                              .canExecute = hasNodeOrEntitySelection });
        m_commands.Register({ .id = "edit.copy",     .label = N_("复制(&C)"),       .shortcut = "Ctrl+C",
                              .execute = [this, &dm] { if (NodeEditorHasFocus()) { ActiveNodeEditor()->CopySelection(); return; } dm.CopySelected(); },
                              .canExecute = hasNodeOrEntitySelection });
        m_commands.Register({ .id = "edit.copyBase", .label = N_("带基点复制(&B)"), .shortcut = "Ctrl+Shift+C", .execute = [&dm] { dm.CopySelectedWithBase(); }, .canExecute = hasSelection });
        m_commands.Register({ .id = "edit.paste",    .label = N_("粘贴(&P)"),       .shortcut = "Ctrl+V",
                              .execute = [this, &dm] { if (NodeEditorHasFocus()) { ActiveNodeEditor()->PasteFromClipboard(); return; } dm.Paste(); },
                              .canExecute = hasDoc });

        // ── 绘图 / 修改：启动 Editor 的工具，工具进行中时按钮显示为选中 ─────
        // 第二列是 Editor 的工具 ID：无论从工具栏、菜单还是命令行启动，按钮都据此显示选中
        // context：label 的翻译上下文（同字不同义，如三维的"旋转"与修改的"旋转"），用 NC_ 标记时同时填上
        struct ToolCommand { const char* id; const char* tool; const char* label; const char* icon; const char* context = ""; };
        const ToolCommand tools[] =
        {
            { "draw.line",     "Line",      N_("直线(&L)"),     "Line.png" },
            { "draw.point",    "Point",     N_("点(&O)"),       "" },
            { "draw.rect",     "Rectangle", N_("矩形(&G)"),     "Rect.png" },
            { "draw.circle",   "Circle",    N_("圆(&C)"),       "Circle.png" },
            { "draw.arc",      "Arc",       N_("圆弧(&A)"),     "Arc.png" },
            { "draw.ellipse",  "Ellipse",   N_("椭圆(&E)"),     "Ellipse.png" },
            { "draw.ellipseArc", "EllipseArc", N_("椭圆弧(&R)"), "" },
            { "draw.pline",    "Polyline",  N_("多段线(&P)"),   "Pline.png" },
            { "draw.spline",   "Spline",    N_("样条曲线(&S)"), "Spline.png" },
            { "draw.solid",    "Solid",     N_("二维填充(&F)"), "" },
            { "draw.image",    "Image",     N_("光栅图像(&I)…"), "" },
            { "draw.mline",    "MLine",     N_("多线(&M)"),     "MLine.png" },
            { "draw.xline",    "XLine",     N_("构造线(&X)"),   "" },
            { "draw.ray",      "Ray",       N_("射线(&Y)"),     "" },
            { "draw.table",    "Table",     N_("表格(&B)"),     "" },
            { "draw.region",   "Region",    N_("面域(&N)"),     "" },
            { "region.union",     "Union",     N_("并集(&U)"),     "" },
            { "region.subtract",  "Subtract",  N_("差集(&S)"),     "" },
            { "region.intersect", "Intersect", N_("交集(&I)"),     "" },
            { "solid.box",      "Box",      N_("长方体(&B)"),   "" },
            { "solid.cylinder", "Cylinder", N_("圆柱体(&C)"),   "" },
            { "solid.sphere",   "Sphere",   N_("球体(&S)"),     "" },
            { "solid.extrude",  "Extrude",  N_("拉伸(&X)"),     "" },
            { "solid.revolve",  "Revolve",  NC_("三维实体", "旋转(&R)"), "", "三维实体" },    // i18n: 上下文
            { "solid.sweep",    "Sweep",    N_("扫掠(&W)"),     "" },
            { "solid.filletedge",  "FilletEdge",  N_("圆角边(&F)"), "" },
            { "solid.chamferedge", "ChamferEdge", N_("倒角边(&H)"), "" },
            { "solid.section",  "SolidSection", N_("截面(&E)"),  "" },
            { "solid.flatshot", "Flatshot", N_("平面投影(&P)"), "" },
            { "inquiry.area",  "Area",      N_("面积(&A)"),     "" },
            { "draw.wipeout",  "Wipeout",   N_("区域覆盖(&W)"), "" },
            { "draw.hatch",    "Hatch",   N_("图案填充(&H)…"), "" },
            { "draw.text",     "Text",      N_("单行文字(&T)"), "Text.png" },
            { "draw.mtext",    "MText",     N_("多行文字(&M)"), "MText.png" },
            { "format.textStyle", "Style",    N_("文字样式(&S)…"), "" },
            { "format.mlineStyle", "MLStyle", N_("多线样式(&M)…"), "" },
            { "format.pointStyle", "PointStyle", N_("点样式(&P)…"), "" },
            { "format.lineTypeScale", "LineTypeScale", N_("线型比例(&L)…"), "" },
            { "dim.linear",    "Dimension",   N_("线性(&L)"),     "" },
            { "dim.angular",   "DimAngular",  N_("角度(&A)"),     "" },
            { "dim.radius",    "DimRadius",   N_("半径(&R)"),     "" },
            { "dim.diameter",  "DimDiameter", N_("直径(&D)"),     "" },
            { "dim.jogged",    "DimJogged",   N_("折弯(&J)"),     "" },
            { "dim.arc",       "DimArc",      N_("弧长(&H)"),     "" },
            { "dim.ordinate",  "DimOrdinate", N_("坐标(&O)"),     "" },
            { "dim.leader",    "Leader",      N_("引线(&E)"),     "" },
            { "dim.mleader",   "MLeader",     N_("多重引线(&U)"), "" },
            { "block.define",  "Block",     N_("创建块(&B)…"),  "" },
            { "block.insert",  "Insert",    N_("插入块(&I)…"),  "" },
            { "modify.move",   "Move",      N_("移动(&V)"),     "Move.png" },
            { "modify.mledit", "MLineEdit", N_("多线编辑(&D)"), "" },
            { "modify.copy",   "Copy",      N_("复制(&Y)"),     "Copy.png" },
            { "modify.mirror", "Mirror",    N_("镜像(&I)"),     "Mirror.png" },
            { "modify.offset", "Offset",    N_("偏移(&S)"),     "" },
            { "modify.rotate", "Rotate",    N_("旋转(&R)"),     "Rotate.png" },
            { "modify.scale",  "Scale",     N_("缩放(&L)"),     "" },
            { "modify.fillet", "Fillet",    N_("圆角(&F)"),     "" },
            { "modify.chamfer", "Chamfer",  N_("倒角(&C)"),     "" },
            { "modify.stretch", "Stretch",  N_("拉伸(&H)"),     "" },
            { "modify.trim",   "Trim",      N_("修剪(&T)"),     "" },
            { "modify.extend", "Extend",    N_("延伸(&D)"),     "" },
            { "modify.break",  "Break",     N_("打断(&K)"),     "" },
            { "modify.array",  "Array",     N_("阵列(&A)…"),    "" },
        };
        for (const ToolCommand& t : tools)
        {
            const std::string id   = t.id;
            const std::string tool = t.tool;
            m_toolCommands[tool] = id;
            m_commands.Register({
                .id         = id,
                .label      = t.label,
                .labelContext = t.context,
                .icon       = t.icon,
                .execute    = [this, &dm, tool]
                {
                    dm.GetEditor().ActivateToolById(tool);
                    HostOf(m_docManager.GetActive()).Focus();            // 回到视口，继续用键盘输入坐标、Esc 取消
                    RequestRenderAll();
                },
                .canExecute = hasDoc,
                .isChecked  = [&dm, tool] { return dm.GetActive() && dm.GetEditor().IsActiveTool() && dm.GetEditor().GetLastCommand() == tool; },
            });
        }

        // ── 视图 ─────────────────────────────────────────────────
        auto viewToggle = [this, &dm](const char* id, const char* label, void (Viewport::*toggle)(), bool (Viewport::*shown)() const)
        {
            m_commands.Register({ .id = id, .label = label,
                                  .execute   = [this, &dm, toggle] { (dm.GetViewport().*toggle)(); RequestRenderAll(); },
                                  .isChecked = [&dm, shown] { return (dm.GetViewport().*shown)(); } });
        };
        m_commands.Register({ .id = "view.zoomExtents", .label = N_("缩放到全图(&A)"), .icon = "ZoomExtents.png", .shortcut = "Ctrl+Shift+E",
                              .execute = [this, &dm] { dm.ZoomAll(); RequestRenderAll(); }, .canExecute = hasDoc });
        // 视口导航条（ViewNavBar.cpp）上的放大 / 缩小（以绘图区中心为基点，相当于滚轮两格）与窗口缩放
        auto zoomStep = [this, &dm](float steps)
        {
            const MiniGUI::Vec2 size = m_viewport->GetSize();
            dm.GetViewport().Zoom(steps, size.x * m_dpiScale * 0.5f, size.y * m_dpiScale * 0.5f);
            RequestRenderAll();
        };
        m_commands.Register({ .id = "view.zoomIn",  .label = N_("放大(&I)"), .icon = "ZoomIn.png",
                              .execute = [zoomStep] { zoomStep(2.0f); },  .canExecute = hasDoc });
        m_commands.Register({ .id = "view.zoomOut", .label = N_("缩小(&O)"), .icon = "ZoomOut.png",
                              .execute = [zoomStep] { zoomStep(-2.0f); }, .canExecute = hasDoc });
        m_commands.Register({ .id = "view.zoomWindow", .label = N_("窗口缩放(&W)"), .tooltip = N_("指定两个角点，缩放到该矩形范围"),
                              .icon = "ZoomWindow.png",
                              .execute = [this, &dm] { dm.GetEditor().RunZoomOption("W"); HostOf(m_docManager.GetActive()).Focus(); }, .canExecute = hasDoc });
        m_commands.Register({ .id = "view.pan", .label = N_("平移(&P)"), .tooltip = N_("按住左键拖动平移视图，右键 / ESC 退出（也可以拖动中键）"),
                              .icon = "Pan.png",
                              .execute = [this, &dm] { dm.GetEditor().ActivateToolById("Pan"); HostOf(m_docManager.GetActive()).Focus(); }, .canExecute = hasDoc });
        viewToggle("view.grid",  N_("显示栅格(&G)"), &Viewport::ShowGridToggle,  &Viewport::IsGridShown);
        viewToggle("view.axis",  N_("显示极轴(&A)"), &Viewport::ShowAxisToggle,  &Viewport::IsAxisShown);
        viewToggle("view.gizmo", N_("显示坐标(&Z)"), &Viewport::ShowGizmoToggle, &Viewport::IsGizmoShown);
        m_commands.Register({ .id = "view.thinLines", .label = N_("细线显示(&T)"),
                              .tooltip = N_("忽略线宽，所有线按 1 像素细线显示（只影响显示，不修改图纸）"),
                              .execute   = [this, &dm] { dm.GetEditor().ToggleThinLines(); RequestRenderAll(); },
                              .isChecked = [&dm] { return dm.GetEditor().IsThinLines(); } });

        // ── 三维视图（M6）：标准视图（可撤销，同缩放）与视觉样式 ──────────
        static const struct { const char* id; const char* label; StandardView view; } kViews[] = {
            { "view.top",    N_("俯视(&T)"),       StandardView::Top },   { "view.bottom", N_("仰视(&B)"),       StandardView::Bottom },
            { "view.left",   N_("左视(&L)"),       StandardView::Left },  { "view.right",  N_("右视(&R)"),       StandardView::Right },
            { "view.front",  N_("前视(&F)"),       StandardView::Front }, { "view.back",   N_("后视(&K)"),       StandardView::Back },
            { "view.swiso",  N_("西南等轴测(&S)"), StandardView::SWIso }, { "view.seiso",  N_("东南等轴测(&E)"), StandardView::SEIso },
            { "view.neiso",  N_("东北等轴测(&N)"), StandardView::NEIso }, { "view.nwiso",  N_("西北等轴测(&W)"), StandardView::NWIso } };
        for (const auto& v : kViews)
            m_commands.Register({ .id = v.id, .label = v.label,
                                  .execute    = [this, &dm, view = v.view] { dm.GetEditor().SetStandardView(view); RequestRenderAll(); },
                                  .canExecute = hasDoc });
        auto visualStyle = [this, &dm](const char* id, const char* label, Editor::VisualStyle style)
        {
            m_commands.Register({ .id = id, .label = label,
                                  .execute   = [this, &dm, style] { dm.GetEditor().SetVisualStyle(style); RequestRenderAll(); },
                                  .isChecked = [&dm, style] { return dm.GetEditor().GetVisualStyle() == style; } });
        };
        visualStyle("view.style.wireframe", N_("线框(&W)"), Editor::VisualStyle::Wireframe);
        visualStyle("view.style.shaded",    N_("着色(&S)"), Editor::VisualStyle::Shaded);
        m_commands.Register({ .id = "view.viewcube", .label = N_("显示视图方块(&V)"),
                              .execute   = [this] { SetViewCubeVisible(!m_viewCubeVisible); },
                              .isChecked = [this] { return m_viewCubeVisible; } });

        // ── 绘图辅助：功能键在输入框有焦点时也生效 ──────────────────
        m_commands.Register({ .id = "aux.snap",  .label = N_("对象捕捉(&S)"), .shortcut = "F3", .allowInTextInput = true,
                              .execute = [&dm] { dm.GetEditor().ToggleSnap(); },  .isChecked = [&dm] { return dm.GetEditor().IsSnapEnabled(); } });
        m_commands.Register({ .id = "aux.ortho", .label = N_("正交(&O)"),     .shortcut = "F8", .allowInTextInput = true,
                              .execute = [&dm] { dm.GetEditor().ToggleOrtho(); }, .isChecked = [&dm] { return dm.GetEditor().IsOrthoEnabled(); } });
        m_commands.Register({ .id = "aux.polar", .label = N_("极轴追踪(&P)"), .shortcut = "F10", .allowInTextInput = true,
                              .execute = [&dm] { dm.GetEditor().TogglePolar(); }, .isChecked = [&dm] { return dm.GetEditor().IsPolarEnabled(); } });
        m_commands.Register({ .id = "aux.hover", .label = N_("悬停高亮(&H)"),
                              .execute = [&dm] { dm.GetEditor().ToggleHover(); }, .isChecked = [&dm] { return dm.GetEditor().IsHoverEnabled(); } });

        // ── 停靠面板的显示 / 隐藏（面板在界面描述文件的 dock 里；每个窗口一套，作用于用户正在用的窗口）────
        auto panelToggle = [this](const char* id, const char* panel, const char* label)
        {
            m_commands.Register({ .id = id, .label = label,
                                  .execute   = [this, panel] { if (auto* d = CurrentDock()) d->TogglePanel(panel); },
                                  .canExecute = [this, panel] { auto* d = CurrentDock(); return d && d->HasPanel(panel); },
                                  .isChecked = [this, panel] { auto* d = CurrentDock(); return d && d->IsPanelVisible(panel); } });
        };
        panelToggle("view.panel.properties", "properties", N_("特性(&P)"));
        panelToggle("view.panel.layers",     "layers",     N_("图层(&L)"));
        // 命令行是浮在绘图区底部的卡片（不在停靠区里），开关保存在用户布局文件里
        m_commands.Register({ .id = "view.panel.commandline", .label = N_("命令行(&C)"), .shortcut = "Ctrl+9",     // 同 AutoCAD
                              .execute   = [this] { SetCommandLineVisible(!m_commandLineVisible); },
                              .canExecute = [this] { return m_main.cmdCard != nullptr; },
                              .isChecked = [this] { return m_commandLineVisible; } });
        // 视口导航条在加载界面描述文件之前创建，它用到的图标在这里给出
        m_commands.Find("view.grid")->icon     = "Grid.png";
        m_commands.Find("view.viewcube")->icon = "ViewCube.png";
        m_commands.Register({ .id = "view.panel.nodes", .label = N_("节点编辑器(&N)"),
                              .tooltip = N_("节点式参数化建模：把几何运算连成节点图，改参数实时更新（docs/NodeGraph/PLAN.md）"),
                              .shortcut = "Ctrl+Shift+N",
                              .execute   = [this] { ShowNodePanel(true); },
                              .canExecute = [this] { auto* d = CurrentDock(); return d && d->HasPanel("nodes"); },
                              .isChecked = [this] { auto* d = CurrentDock(); return d && d->IsPanelVisible("nodes"); } });
        m_commands.Register({ .id = "nodes.bake", .label = N_("烘焙节点图(&B)"),
                              .tooltip = N_("把节点图的结果加入图纸（有选中节点时只烘焙选中的节点），可撤销"),
                              .execute = [this] { BakeNodeGraph(); }, .canExecute = hasDoc });
        m_commands.Register({ .id = "view.resetLayout", .label = N_("重置面板布局(&R)"),
                              .tooltip = N_("删除保存的面板布局，恢复界面描述文件里的默认布局"),
                              // 重建界面会销毁功能区等节点（可能正是触发命令的按钮），等当前事件处理完再执行
                              .execute = [this] { m_platform->Post([this] { ResetLayout(); }); } });

        // ── 图层 ─────────────────────────────────────────────────
        m_commands.Register({ .id = "layer.new", .label = N_("新建图层(&N)"),
                              .execute = [this, &dm]
                              {
                                  Document& doc = *dm.GetActive();
                                  doc.GetCommandStack().Execute(
                                      std::make_unique<AddLayerCommand>(LayerOps::UniqueName(doc.GetLayerManager()), /*makeCurrent=*/true),
                                      doc.GetScene());
                                  LayerChanged();
                              },
                              .canExecute = hasDoc });

        // ── 布局 ─────────────────────────────────────────────────
        m_commands.Register({ .id = "layout.new", .label = N_("新建布局(&N)"),
                              .tooltip = N_("新建一张图纸布局（与当前布局同样的纸张）"),
                              .execute = [this] { NewLayout(); },
                              .canExecute = hasDoc });
        m_commands.Register({ .id = "layout.delete", .label = N_("删除布局(&D)"),
                              .tooltip = N_("删除当前布局及其上的视口等图纸空间对象（可撤销）"),
                              .execute = [this] { if (Document* d = m_docManager.GetActive()) DeleteLayout(d->ActiveLayout()); },
                              .canExecute = [this]
                              {
                                  const Document* d = m_docManager.GetActive();
                                  return d && d->ActiveLayoutIsPaper() && RemoveLayoutCommand::PaperCount(d->GetScene()) > 1;
                              } });

        // ── 格式 ─────────────────────────────────────────────────
        m_commands.Register({ .id = "format.mlineStyle", .label = N_("多线样式(&M)…"),
                              .tooltip = N_("编辑多线样式：元素线偏移、封口与顶点连接线（MLSTYLE）"),
                              .execute = [this] { OpenMLineStyleDialog(); },
                              .canExecute = hasDoc });

        m_commands.Register({ .id = "format.pointStyle", .label = N_("点样式(&P)…"),
                              .tooltip = N_("设置 POINT 的显示图形与大小（HEADER 的 $PDMODE/$PDSIZE）"),
                              .execute = [this] { OpenPointStyleDialog(); },
                              .canExecute = hasDoc });

        m_commands.Register({ .id = "format.lineTypeScale", .label = N_("线型比例(&L)…"),
                              .tooltip = N_("设置全局线型比例（HEADER 的 $LTSCALE）"),
                              .execute = [this] { OpenLineTypeScaleDialog(); },
                              .canExecute = hasDoc });

        // ── 界面 ─────────────────────────────────────────────────
        m_commands.Register({ .id = "ui.reload", .label = N_("重新加载界面(&R)"),
                              .tooltip = N_("重新读取界面描述文件（保存文件时也会自动重新加载）"),
                              .shortcut = "F5", .allowInTextInput = true,
                              .execute = [this] { m_platform->Post([this] { ReloadUi(); }); } });
        m_commands.Register({ .id = "ui.theme", .label = N_("浅色主题(&T)"), .shortcut = "Ctrl+T",
                              .execute   = [this]
                              {
                                  const bool toLight = m_ui->GetTheme().IsDark();
                                  m_ui->SetTheme(toLight ? MiniGUI::ThemeColors::Light() : MiniGUI::ThemeColors::Dark());
                                  if (m_floatHost)
                                      m_floatHost->SetTheme(m_ui->GetTheme());     // 浮动窗口跟着换
                                  SetDocWindowsTheme(m_ui->GetTheme());
RequestRenderAll();      // 视口背景随主题反转白色内容（RenderFrame 同步）
                              },
                              .isChecked = [this] { return !m_ui->GetTheme().IsDark(); } });
        m_commands.Register({ .id = "help.about", .label = N_("关于(&A)…"), .shortcut = "F1", .execute = [this] { ShowAbout(); } });

        // ── 界面语言（"设置 > 语言"）：每种语言一条命令 ui.language.<代码>，选了立即切换并写进用户设置 ──
        // 名称是该语言自己的写法（"English"），不翻译：看不懂当前界面语言的人也找得到自己的语言。
        // 切换要重建界面，而菜单还开着（命令在菜单项的回调里执行）：放到这一帧之后
        for (const LanguageInfo& lang : ListLanguages(ResourceDir()))
        {
            const std::string code = lang.code;
            m_commands.Register({ .id = std::string(kLanguageCommandPrefix) + code, .label = lang.name,
                                  .execute = [this, code]
                                  {
                                      if (code != CurrentLanguage())
                                          m_platform->Post([this, code] { SetLanguage(code); });
                                  },
                                  .isChecked = [code] { return CurrentLanguage() == code; } });
        }
    }
}

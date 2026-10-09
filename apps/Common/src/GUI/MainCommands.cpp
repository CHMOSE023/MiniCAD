// ── MiniCAD 命令注册：菜单栏、工具栏、快捷键都引用这里的命令 ID ─────────
// 名称、图标、快捷键是默认值，界面描述文件（ui/minicad_ui.json）的 "commands" 可以覆盖
#include "GUI/MainFrame.h"
#include "Document/Document.h"
#include "Document/Command/LayerCommands.h"
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
            CadSaveVersion version = doc ? doc->GetCadSaveVersion() : CadSaveVersion::R2018;
            const std::string path = platform->ChooseSavePath(doc ? doc->GetName() : "未命名", version);
            if (doc && !path.empty())
                doc->SetCadSaveVersion(version);    // DWG / DXF 按所选版本写出
            return path;
        });

        // ── 窗口：标题栏的最小化 / 最大化 / 关闭按钮执行这三个命令（网页版没有窗口按钮：不注册，标题栏就不显示）──
        if (platform->HasWindowControls())
        {
            m_commands.Register({ .id = MiniGUI::TitleBar::kMinimizeCommand, .label = "最小化(&N)", .execute = [platform] { platform->MinimizeWindow(); } });
            m_commands.Register({ .id = MiniGUI::TitleBar::kMaximizeCommand, .label = "最大化(&X)",
                                  .execute = [platform] { platform->ToggleMaximizeWindow(); }, .isChecked = [platform] { return platform->IsWindowMaximized(); } });
            m_commands.Register({ .id = MiniGUI::TitleBar::kCloseCommand,    .label = "关闭(&C)",   .execute = [platform] { platform->CloseWindow(); } });
        }

        // ── 文件 ─────────────────────────────────────────────────
        m_commands.Register({ .id = "file.new",     .label = "新建(&N)",     .shortcut = "Ctrl+N",       .execute = [&dm] { dm.New(); } });
        m_commands.Register({ .id = "file.open",    .label = "打开(&O)…",    .shortcut = "Ctrl+O",
                              .execute = [this, platform, &dm]
                              {
                                  platform->PickDrawingFiles([this](const std::vector<std::string>& paths)
                                  {
                                      OpenDrawings(paths);
                                  });
                              } });
        m_commands.Register({ .id = "file.save",    .label = "保存(&S)",     .shortcut = "Ctrl+S",       .execute = [this] { SaveDocuments(false, false); }, .canExecute = hasDoc });
        m_commands.Register({ .id = "file.saveAs",  .label = "另存为(&A)…",  .shortcut = "Ctrl+Shift+S", .execute = [this] { SaveDocuments(false, true); },  .canExecute = hasDoc });
        m_commands.Register({ .id = "file.saveAll", .label = "全部保存(&L)", .shortcut = "Ctrl+Alt+S",   .execute = [this] { SaveDocuments(true, false); },  .canExecute = hasDoc });
        m_commands.Register({ .id = "file.close",   .label = "关闭(&C)",     .shortcut = "Ctrl+W",
                              .execute = [this, &dm] { CloseDocument(dm.GetActive()); }, .canExecute = hasDoc });
        m_commands.Register({ .id = "app.exit",     .label = "退出(&X)",     .shortcut = "Alt+F4",       .execute = [platform] { platform->CloseWindow(); } });

        // ── 文档标签：按标签顺序切换 ────────────────────────────────
        auto stepDoc = [this](int dir)
        {
            const int n = m_docTabs->GetTabCount();
            if (n > 1)
                m_docTabs->SetSelected((m_docTabs->GetSelected() + dir + n) % n);     // 选中回调里激活文档
        };
        auto multiDoc = [&dm] { return dm.GetAll().size() > 1; };
        m_commands.Register({ .id = "doc.next", .label = "下一个文档(&N)", .shortcut = "Ctrl+Tab",       .execute = [stepDoc] { stepDoc(1); },  .canExecute = multiDoc });
        m_commands.Register({ .id = "doc.prev", .label = "上一个文档(&P)", .shortcut = "Ctrl+Shift+Tab", .execute = [stepDoc] { stepDoc(-1); }, .canExecute = multiDoc });

        // ── 编辑 ─────────────────────────────────────────────────
        m_commands.Register({ .id = "edit.undo", .label = "撤销(&U)", .icon = "Undo.png", .shortcut = "Ctrl+Z",
                              .execute = [this, &dm] { dm.Undo(); m_viewport->RequestRender(); }, .canExecute = [&dm] { return dm.GetActive() && dm.GetActive()->CanUndo(); } });
        m_commands.Register({ .id = "edit.redo", .label = "重做(&R)", .icon = "Redo.png", .shortcut = "Ctrl+Y",
                              .execute = [this, &dm] { dm.Redo(); m_viewport->RequestRender(); }, .canExecute = [&dm] { return dm.GetActive() && dm.GetActive()->CanRedo(); } });
        // 删除：与视口里按 Delete 键是同一个操作（Delete 键由 Editor 处理，这里不再注册快捷键，避免文本框里按 Delete 被抢走）
        m_commands.Register({ .id = "edit.delete", .label = "删除(&D)",
                              .execute = [&dm] { dm.GetEditor().DeleteSelected(); },
                              .canExecute = [&dm] { return dm.GetActive() && !dm.GetEditor().GetSelection().empty() && !dm.GetEditor().IsToolRunning(); } });
        m_commands.Register({ .id = "modify.explode", .label = "分解(&X)",
                              .execute = [&dm] { dm.GetEditor().ExplodeSelection(); },
                              .canExecute = [&dm] { return dm.GetActive() && !dm.GetEditor().GetSelection().empty() && !dm.GetEditor().IsToolRunning(); } });
        m_commands.Register({ .id = "edit.selectAll", .label = "全选(&A)", .shortcut = "Ctrl+A",
                              .execute = [&dm] { dm.GetEditor().SelectAll(); },
                              .canExecute = [&dm] { return dm.GetActive() && !dm.GetEditor().IsToolRunning(); } });
        m_commands.Register({ .id = "edit.cut",      .label = "剪切(&T)",       .shortcut = "Ctrl+X",       .execute = [&dm] { dm.CutSelected(); },          .canExecute = hasSelection });
        m_commands.Register({ .id = "edit.copy",     .label = "复制(&C)",       .shortcut = "Ctrl+C",       .execute = [&dm] { dm.CopySelected(); },         .canExecute = hasSelection });
        m_commands.Register({ .id = "edit.copyBase", .label = "带基点复制(&B)", .shortcut = "Ctrl+Shift+C", .execute = [&dm] { dm.CopySelectedWithBase(); }, .canExecute = hasSelection });
        m_commands.Register({ .id = "edit.paste",    .label = "粘贴(&P)",       .shortcut = "Ctrl+V",       .execute = [&dm] { dm.Paste(); },                .canExecute = hasDoc });

        // ── 绘图 / 修改：启动 Editor 的工具，工具进行中时按钮显示为选中 ─────
        // 第二列是 Editor 的工具 ID：无论从工具栏、菜单还是命令行启动，按钮都据此显示选中
        struct ToolCommand { const char* id; const char* tool; const char* label; const char* icon; };
        const ToolCommand tools[] =
        {
            { "draw.line",     "Line",      "直线(&L)",     "Line.png" },
            { "draw.point",    "Point",     "点(&O)",       "" },
            { "draw.rect",     "Rectangle", "矩形(&G)",     "Rect.png" },
            { "draw.circle",   "Circle",    "圆(&C)",       "Circle.png" },
            { "draw.arc",      "Arc",       "圆弧(&A)",     "Arc.png" },
            { "draw.ellipse",  "Ellipse",   "椭圆(&E)",     "Ellipse.png" },
            { "draw.ellipseArc", "EllipseArc", "椭圆弧(&R)", "" },
            { "draw.pline",    "Polyline",  "多段线(&P)",   "Pline.png" },
            { "draw.spline",   "Spline",    "样条曲线(&S)", "Spline.png" },
            { "draw.solid",    "Solid",     "二维填充(&F)", "" },
            { "draw.image",    "Image",     "光栅图像(&I)…", "" },
            { "draw.mline",    "MLine",     "多线(&M)",     "" },
            { "draw.xline",    "XLine",     "构造线(&X)",   "" },
            { "draw.ray",      "Ray",       "射线(&Y)",     "" },
            { "draw.table",    "Table",     "表格(&B)",     "" },
            { "draw.region",   "Region",    "面域(&N)",     "" },
            { "region.union",     "Union",     "并集(&U)",     "" },
            { "region.subtract",  "Subtract",  "差集(&S)",     "" },
            { "region.intersect", "Intersect", "交集(&I)",     "" },
            { "inquiry.area",  "Area",      "面积(&A)",     "" },
            { "draw.wipeout",  "Wipeout",   "区域覆盖(&W)", "" },
            { "draw.hatch",    "Hatch",   "图案填充(&H)…", "" },
            { "draw.text",     "Text",      "单行文字(&T)", "Text.png" },
            { "draw.mtext",    "MText",     "多行文字(&M)", "MText.png" },
            { "format.textStyle", "Style",    "文字样式(&S)…", "" },
            { "dim.linear",    "Dimension",   "线性(&L)",     "" },
            { "dim.angular",   "DimAngular",  "角度(&A)",     "" },
            { "dim.radius",    "DimRadius",   "半径(&R)",     "" },
            { "dim.diameter",  "DimDiameter", "直径(&D)",     "" },
            { "dim.jogged",    "DimJogged",   "折弯(&J)",     "" },
            { "dim.arc",       "DimArc",      "弧长(&H)",     "" },
            { "dim.ordinate",  "DimOrdinate", "坐标(&O)",     "" },
            { "dim.leader",    "Leader",      "引线(&E)",     "" },
            { "dim.mleader",   "MLeader",     "多重引线(&U)", "" },
            { "block.define",  "Block",     "创建块(&B)…",  "" },
            { "block.insert",  "Insert",    "插入块(&I)…",  "" },
            { "modify.move",   "Move",      "移动(&V)",     "Move.png" },
            { "modify.copy",   "Copy",      "复制(&Y)",     "Copy.png" },
            { "modify.mirror", "Mirror",    "镜像(&I)",     "Mirror.png" },
            { "modify.offset", "Offset",    "偏移(&S)",     "" },
            { "modify.rotate", "Rotate",    "旋转(&R)",     "Rotate.png" },
            { "modify.scale",  "Scale",     "缩放(&L)",     "" },
            { "modify.fillet", "Fillet",    "圆角(&F)",     "" },
            { "modify.chamfer", "Chamfer",  "倒角(&C)",     "" },
            { "modify.stretch", "Stretch",  "拉伸(&H)",     "" },
            { "modify.trim",   "Trim",      "修剪(&T)",     "" },
            { "modify.extend", "Extend",    "延伸(&D)",     "" },
            { "modify.break",  "Break",     "打断(&K)",     "" },
            { "modify.array",  "Array",     "阵列(&A)…",    "" },
        };
        for (const ToolCommand& t : tools)
        {
            const std::string id   = t.id;
            const std::string tool = t.tool;
            m_toolCommands[tool] = id;
            m_commands.Register({
                .id         = id,
                .label      = t.label,
                .icon       = t.icon,
                .execute    = [this, &dm, tool]
                {
                    dm.GetEditor().ActivateToolById(tool);
                    m_viewport->Focus();            // 回到视口，继续用键盘输入坐标、Esc 取消
                    m_viewport->RequestRender();
                },
                .canExecute = hasDoc,
                .isChecked  = [&dm, tool] { return dm.GetActive() && dm.GetEditor().IsActiveTool() && dm.GetEditor().GetLastCommand() == tool; },
            });
        }

        // ── 视图 ─────────────────────────────────────────────────
        auto viewToggle = [this, &dm](const char* id, const char* label, void (Viewport::*toggle)(), bool (Viewport::*shown)() const)
        {
            m_commands.Register({ .id = id, .label = label,
                                  .execute   = [this, &dm, toggle] { (dm.GetViewport().*toggle)(); m_viewport->RequestRender(); },
                                  .isChecked = [&dm, shown] { return (dm.GetViewport().*shown)(); } });
        };
        m_commands.Register({ .id = "view.zoomExtents", .label = "缩放到全图(&A)", .shortcut = "Ctrl+Shift+E",
                              .execute = [this, &dm] { dm.ZoomAll(); m_viewport->RequestRender(); }, .canExecute = hasDoc });
        viewToggle("view.grid",  "显示栅格(&G)", &Viewport::ShowGridToggle,  &Viewport::IsGridShown);
        viewToggle("view.axis",  "显示极轴(&A)", &Viewport::ShowAxisToggle,  &Viewport::IsAxisShown);
        viewToggle("view.gizmo", "显示坐标(&Z)", &Viewport::ShowGizmoToggle, &Viewport::IsGizmoShown);
        m_commands.Register({ .id = "view.thinLines", .label = "细线显示(&T)",
                              .tooltip = "忽略线宽，所有线按 1 像素细线显示（只影响显示，不修改图纸）",
                              .execute   = [this, &dm] { dm.GetEditor().ToggleThinLines(); m_viewport->RequestRender(); },
                              .isChecked = [&dm] { return dm.GetEditor().IsThinLines(); } });

        // ── 绘图辅助：功能键在输入框有焦点时也生效 ──────────────────
        m_commands.Register({ .id = "aux.snap",  .label = "对象捕捉(&S)", .shortcut = "F3", .allowInTextInput = true,
                              .execute = [&dm] { dm.GetEditor().ToggleSnap(); },  .isChecked = [&dm] { return dm.GetEditor().IsSnapEnabled(); } });
        m_commands.Register({ .id = "aux.ortho", .label = "正交(&O)",     .shortcut = "F8", .allowInTextInput = true,
                              .execute = [&dm] { dm.GetEditor().ToggleOrtho(); }, .isChecked = [&dm] { return dm.GetEditor().IsOrthoEnabled(); } });
        m_commands.Register({ .id = "aux.polar", .label = "极轴追踪(&P)", .shortcut = "F10", .allowInTextInput = true,
                              .execute = [&dm] { dm.GetEditor().TogglePolar(); }, .isChecked = [&dm] { return dm.GetEditor().IsPolarEnabled(); } });
        m_commands.Register({ .id = "aux.hover", .label = "悬停高亮(&H)",
                              .execute = [&dm] { dm.GetEditor().ToggleHover(); }, .isChecked = [&dm] { return dm.GetEditor().IsHoverEnabled(); } });

        // ── 停靠面板的显示 / 隐藏（面板在界面描述文件的 dock 里）────
        auto panelToggle = [this](const char* id, const char* panel, const char* label)
        {
            m_commands.Register({ .id = id, .label = label,
                                  .execute   = [this, panel] { if (auto* d = m_layout->GetDock("main")) d->TogglePanel(panel); },
                                  .canExecute = [this, panel] { auto* d = m_layout->GetDock("main"); return d && d->HasPanel(panel); },
                                  .isChecked = [this, panel] { auto* d = m_layout->GetDock("main"); return d && d->IsPanelVisible(panel); } });
        };
        panelToggle("view.panel.properties", "properties", "特性(&P)");
        panelToggle("view.panel.layers",     "layers",     "图层(&L)");
        panelToggle("view.panel.commandline", "commandline", "命令行(&C)");
        m_commands.Find("view.panel.commandline")->shortcut = "Ctrl+9";     // 同 AutoCAD
        m_commands.Register({ .id = "view.resetLayout", .label = "重置面板布局(&R)",
                              .tooltip = "删除保存的面板布局，恢复界面描述文件里的默认布局",
                              .execute = [this] { ResetLayout(); } });

        // ── 图层 ─────────────────────────────────────────────────
        m_commands.Register({ .id = "layer.new", .label = "新建图层(&N)",
                              .execute = [this, &dm]
                              {
                                  Document& doc = *dm.GetActive();
                                  doc.GetCommandStack().Execute(
                                      std::make_unique<AddLayerCommand>(LayerOps::UniqueName(doc.GetLayerManager()), /*makeCurrent=*/true),
                                      doc.GetScene());
                                  LayerChanged();
                              },
                              .canExecute = hasDoc });

        // ── 界面 ─────────────────────────────────────────────────
        m_commands.Register({ .id = "ui.reload", .label = "重新加载界面(&R)",
                              .tooltip = "重新读取界面描述文件（保存文件时也会自动重新加载）",
                              .shortcut = "F5", .allowInTextInput = true,
                              .execute = [this] { ReloadUi(); } });
        m_commands.Register({ .id = "ui.theme", .label = "浅色主题(&T)", .shortcut = "Ctrl+T",
                              .execute   = [this]
                              {
                                  const bool toLight = m_ui->GetTheme().IsDark();
                                  m_ui->SetTheme(toLight ? MiniGUI::ThemeColors::Light() : MiniGUI::ThemeColors::Dark());
                                  m_viewport->RequestRender();      // 视口背景随主题反转白色内容（RenderFrame 同步）
                              },
                              .isChecked = [this] { return !m_ui->GetTheme().IsDark(); } });
        m_commands.Register({ .id = "help.about", .label = "关于(&A)…", .shortcut = "F1", .execute = [this] { ShowAbout(); } });
    }
}

# MiniCAD 界面改进计划（借鉴 SolidCraft）

依据：SolidCraft 0.19.0 实际运行界面（起始页、主界面、文件菜单），`D:\solidcraft\crates\ui-egui` 源码（`theme.rs`、`titlebar.rs`、`home.rs`、`ui_kit/`、`docs/ui-kit.md`），以及 MiniCAD 当前界面（`assets/ui/minicad_ui.json`、`src/UI`、`apps/Common/src/GUI`）。

## 1 原则

- **布局向 SolidCraft 靠拢**（2026-10-24 决定，取代原来“菜单栏 + 工具栏不动”的原则）：标题栏只留“文件”菜单、快捷按钮和文档标签；其余命令进功能区（AutoCAD 的默认界面本来也是功能区）；起始页占满标题栏以下的整个窗口；视口底部居中放导航条。
- **功能不变**：命令行、特性 / 图层面板、停靠布局、命令与快捷键都保留。SolidCraft 的特征树、历史时间线只服务参数化三维建模，不照搬。
- 尺寸与配色照 SolidCraft 的令牌（`D:\solidcraft\assets\styles\ui\palettes.rs`、`crates/ui-egui/src/ui_kit/file_view.rs`、`toolbar.rs`）。参考的是设计，不拷贝代码；如以后直接移植 SolidCraft 的代码（Apache-2.0 / MIT），在 `ATTRIBUTION` 里署名。

## 2 SolidCraft 界面分析

### 2.1 外观

| 项 | SolidCraft | MiniCAD 现状 |
|---|---|---|
| 圆角 | 令牌 `radius_sm / md / lg / xl`；控件 `control_radius`；窗口、菜单用 `radius_md`；卡片更大 | 主题只有一个 `Rounding = 4`，`Widgets/` 里仅约 8 处引用，多数控件直角 |
| 控件高度 | `control_height` 28pt，统一 | `ControlH = 28`、`RowH = 26`，已有但未覆盖所有控件 |
| 间距 | `space_2/3/4/6/8/10/12/14/16/24` 一套刻度 | 各控件内部写死数字 |
| 字号 | `font_badge/micro/small/label/body/heading/title/display` 分级 | 只有一个 `FontSize = 14` |
| 配色 | `theme::Tokens` 集中，面板、卡片、选中、强调色有语义名 | `ThemeColors.cpp` 已有语义色（Background、Panel、Control…），基础不错 |
| 容器 | 所有浮窗、菜单、对话框经 `ui_kit::Container`，CI 检查禁止绕过 | 对话框各写各的 |

### 2.2 结构与交互

- **标题栏**一行里放：应用图标、首页键、"文件"菜单、保存 / 撤销 / 重做图标、文档标签（带 `+`）、设置、帮助、版本号、窗口按钮。保存 / 撤销 / 重做在没有可用操作时灰显。
- **文件菜单**：新建、打开、最近打开、导入、保存、另存、导出（各种格式）、设置、快捷键，用图标 + 快捷键右对齐，分组之间有分隔线。文件相关入口全部收在这里，不再占工具栏。
- **起始页**：左侧导航列（新建 / 打开 / 恢复 / 保存 / 设置…），右侧三块：新建卡片（新建、导入）、最近文件、示例（带缩略图的卡片）。启动无文件时显示，点首页键可回来，`Esc` 返回当前文档。
- **命令搜索**：功能区右上角常驻，快捷键 `S`，结果行带图标、路径、快捷键。
- **视口导航条**：视口底部居中一排小图标（环绕、平移、缩放、全部显示、视图、视觉样式、设置）；右上角是视图方块。
- **面板外观**：圆角卡片、标题行 + 次级信息（如 "0 bodies"）、折叠箭头。

## 3 MiniCAD 对应方案

### P0 圆角与令牌（基础，其余都建立在它上面）

1. `src/UI/Style/Theme.hpp`：增加 `RadiusSm = 4`、`RadiusMd = 6`、`RadiusLg = 10`；增加间距刻度（`Space2/4/6/8/12/16/24`）和字号级别（`FontSmall/Body/Heading/Title`）。保留 `Rounding` 作为 `RadiusSm` 的别名，避免一次改动过大。
2. 逐个控件改为读令牌并画圆角：`Button`、`TextBox`、`NumberBox`、`ComboBox`、`ListView` 选中行、`Menu`（弹出菜单 `RadiusMd`、菜单项 `RadiusSm`）、`Dialog`（`RadiusLg`）、`TabView`（标签顶部圆角）、`Panel` / `DockSpace`（停靠面板卡片 `RadiusMd`，面板之间留 4px 间隙）、`PropertyGrid`、提示框。
3. 不动：CAD 视口内容区、窗口外框（无边框窗口 `Win32Frame` 自己处理）。
4. 验证：`MiniGUITests`、`MiniGUIGolden`（基准会整体变化，先对比旧的 10 个已知不一致场景，再看图片后 `--update`）、桌面版自测 `--selftest`。

### P1 标题栏收敛文件操作

1. `minicad_ui.json` 的 `titlebar` 子项改为：`menubar`（保留各菜单）+ 一组紧凑的 `toolbar`（保存、撤销、重做）+ 右侧搜索框与窗口按钮。AutoCAD 用户依赖完整菜单，所以菜单栏保留，不收成单个"文件"。
2. 从 `standard` 工具栏去掉新建、打开、保存、撤销、重做（已在标题栏和文件菜单里），省出一行高度；保留复制、粘贴等编辑类按钮，或并入 `modify` 行。
3. `TitleBar.cpp` 需要支持在标题栏里放 `toolbar` 子控件并随窗口宽度收缩（窄时先隐藏搜索框，再把菜单折叠为"≡"）。
4. 文件菜单加图标与快捷键对齐，导入 / 导出（DWG、DXF、各版本）分组放在一起，对应 `AppPlatform` 的 `kSaveFormats`。

### P2 起始页

1. 替换 `MainPanels.cpp` 里"没有打开的文档"的空状态：新建 / 打开 / 导入 DWG·DXF 三张卡片，下面是最近文件（`RecentFiles`，已有）和示例（`assets/nodes/examples`、测试图纸）。
2. 缩略图：打开或保存文档时用现有离屏渲染（`Viewport` + 渲染器）出一张小图，缓存到用户数据目录（`AppPlatform` 的用户数据目录接口），按路径 + 修改时间失效；后台线程生成（`RunInBackground`），桌面和网页共用，不写平台分支。
3. 标题栏加首页键；有文档时点击回到起始页，`Esc` 返回。网页版同样适用。
4. 新增 `GUI/StartPage.cpp/.h`，加入 `apps/Common/CMakeLists.txt`。

### P3 命令搜索

1. 在 `CommandUI` / 命令注册表上做模糊搜索：匹配命令名称、拼音首字母、命令行别名和快捷键，结果行显示图标、名称、所属菜单路径、快捷键。
2. 放标题栏右侧；快捷键用 `Ctrl+K`（AutoCAD 里单字母键会和命令行输入冲突，不用 `S`）。
3. 与命令行的自动补全（`AutoComplete`）共用匹配逻辑，避免两套。

### P4 视口导航条

1. 在绘图区底部居中浮动一条小工具条：环绕、平移、缩放、全部显示、视觉样式（线框 / 着色）、视图方块开关。命令都已存在（`view.zoomExtents`、`view.style.*`、`view.viewcube` 等），只加一个浮动控件。
2. 二维俯视时只显示平移、缩放、全部显示，避免占地方；右键菜单或命令可关闭。

### P5 面板与对话框统一（持续进行）

1. 面板标题行：名称 + 次级信息 + 折叠箭头，统一 `Panel` 的头部样式。
2. 对话框收到一个统一容器（头部 40、页脚 48、`RadiusLg`），其他对话框逐个迁移。
3. 增加一条轻量检查（单元测试或脚本）：控件里不得写死圆角、间距数字，必须走令牌。

## 4 顺序与工作量

| 阶段 | 内容 | 估计 | 风险 |
|---|---|---|---|
| P0 | 令牌 + 圆角 | 2–3 天 | 截图基准整体更新；需逐个确认布局没被圆角挤坏 |
| P1 | 标题栏 | 2 天 | `TitleBar` 窄窗口收缩逻辑 |
| P2 | 起始页 + 缩略图 | 3–4 天 | 缩略图的离屏渲染与缓存失效；网页版没有本地缓存目录时降级为不缓存 |
| P3 | 命令搜索 | 1–2 天 | 拼音匹配需要一份汉字首字母表 |
| P4 | 导航条 | 1 天 | 与视图方块、命令行叠放位置 |
| P5 | 统一容器 | 持续 | 对话框数量多，分批迁移 |

建议先做 P0，再做 P1、P3，然后 P2 和 P4。每一阶段独立可合并，不依赖后面的阶段。

### 4.1 进度（2026-10-24）

| 部分 | 已完成 | 未完成 / 注意 |
|---|---|---|
| P0 圆角令牌 | `Theme.hpp` 加 `RadiusXs/Sm/Md/Lg`（`Rounding` = `RadiusSm`），大部分控件改读令牌；主题加起始页 / 主卡片颜色槽位 `Rail*`、`CardPrimary*`（深浅两套，取自 SolidCraft 的 `file_*`） | 间距刻度 `Space*`、字号级别 `Font*` 未加；仍写死圆角：`ScrollView`、`PropertyGrid`、滑块轨道、`TextBox` 多行滚动条、`CadPreview`、`StatusBarView`、`DynamicInput`、`EditorDialogs`、`NodeEditor` 提示框；`MiniGUIGolden` 基准已于 2026-10-25 看图后更新（35 个场景：圆角、标题栏高 32→34、命令行新布局） |
| 标题栏 | 图标 + “MiniCAD” + 首页键 + “文件”菜单（`MenuBar::SetHideMnemonicSuffix`，描述文件 `"mnemonicSuffix": false`：不显示 “(F)”，Alt+F 仍可用）+ 保存、撤销、重做 + 文档标签（面板 `doctabs`，`TabView::SetBarStyle` 圆角块样式；空白处可拖动窗口）+ 右侧 AI 助手（占位，点击说明计划）、设置菜单（主题、视图方块、命令行、重置面板布局、重新加载界面）、帮助菜单（搜索命令、起始页、关于）、版本号（面板 `appbar`）；`TitleBar::kHeight` 34 | AI 助手未接入；窄窗口时标签条的收缩 |
| 功能区 | 新控件 `MiniGUI::Ribbon`（`src/UI/Widgets/Ribbon.*`；界面描述文件顶层 `ribbons` + 布局类型 `ribbon`）：选项卡行 30 + 分组 86，按钮 54–76 宽，点组名列出全组命令；宽度不够时先收窄按钮，再从按钮最多的组起逐个收进下拉，剩一个时整组折叠；可放宿主控件（“常用 > 图层”组里两行的图层 / 线型 / 线宽下拉框）；双击选项卡收起 / 展开；重新加载界面后停在原选项卡。86 个新图标由 `tools/make_icons.py` 生成，对应关系写在 `minicad_ui.json` 的 `commands`。命令搜索框（`Ribbon::EnableSearch`，`ribbons.main.search`）：选项卡行右端，Ctrl+K（命令 `ui.search`）或点击获得焦点后变宽；按命令名称、编号、提示和所在选项卡 / 组名查找（打分照 SolidCraft：前缀 > 词首 > 包含 > 按顺序出现，多个词都要匹配，不可用的排在后面），结果注明位置（“绘图 › 绘制”）；↑↓ 选择、Enter 执行、Esc 关闭，之后焦点回到绘图区；自测第 26 项 选项卡行右端的 ⌃ / ⌄ 按钮收起 / 展开（也可双击选项卡）；组名只在有命令收进下拉时带 ▾，下拉只列出没显示出来的命令 | 收起状态不保存；左侧“工作空间”卡片未做；搜索框没有“最近使用”和拼音首字母匹配 |
| 起始页 | `StartPage.cpp`：面板 `start` 放在标题栏下面，显示时隐藏其余界面；左侧蓝色导航列（返回 文档名 [Esc]、开始、新建、打开、保存…，可用状态随命令）；新建卡片（新建图纸、新建三维模型、打开、节点建模）；最近（圆角列表，带修改时间）；示例（`assets/nodes/examples`，点击新建图纸并插入节点图）；卡片网格按宽度自动分列；首页键 `view.start`，Esc 返回，点任一文档标签回到图纸；自测第 25 项 | 缩略图（最近、示例都还是图标）；导入卡片；“恢复”“导出”页 |
| 导航条 | `ViewNavBar.cpp`：竖排在视图方块正下方的圆角浮条——放大、缩小、窗口缩放、全部显示 \| 标准视图、视觉样式（菜单向右弹出，放不下翻到左侧）\| 视图方块、栅格；新命令 `view.zoomIn` / `zoomOut` / `zoomWindow` | 环绕 / 平移按钮（目前只能用鼠标）；二维俯视时精简 |
| 浮动面板 | 绘图区铺满标题栏以下（面板 `canvas`），其余界面在它上面一层（`position: absolute` + `passThrough`，空白处点击穿透到绘图区）：功能区卡片、浮动停靠区（`DockSpace::SetFloating`，描述文件 `"floating": true`：停靠区与缝透明，文档区是透明的“洞”，其余标签组画成圆角卡片、内容内缩 3 露出圆角）、状态栏卡片（左侧模型 / 布局标签，`TabView::SetFitToTabs`）。“洞”里放视图方块（右上）、竖排导航条（方块正下方）、底部居中的命令行卡片（`view.panel.commandline` / Ctrl+9 显示隐藏，开关存进 layout.json；用户布局格式升到 2，旧布局回到默认）。“全部显示”、框选缩放把范围放进没被挡住的区域（`Camera::SetFitInsets`，每帧由 `MainFrame::UpdateFitInsets` 按“洞”减去命令行设置）；坐标轴图标画在该区域左下角。视口的“隐藏指针”只对视口本身生效（`ViewportHost::GetCursor`），浮动工具条上显示箭头、绘图区不画十字光标。自测第 27 项 | 卡片没有阴影；面板挡住的绘图内容仍可在缝里看到但不能隔着卡片拾取；窄窗口时命令行与右侧面板可能挤在一起 |

注意：
- 1280 × 800 的窗口里功能区展开时，停靠在绘图区下方的节点编辑器只有约 155 高（`ShowNodePanel` 按文档区高度的 45% 分配，空间不够时被压缩），可双击选项卡收起功能区。自测 16c'' 为此收起功能区、关掉命令行面板，结束后恢复。
- 网页版最近列表只在本次会话有效；上传的文件在 `/upload`，点最近的图纸可能已不存在（打开失败后从列表移除）。
- 网页版本轮没有重新构建验证。

## 5 验证

- 每阶段：`run_tests.bat`，桌面版 `MiniCADWin.exe --selftest`，实际运行看效果。
- 涉及网页版（P0、P2、P4）：`build_wasm.bat serve` 在浏览器里检查。
- 深色和浅色主题都要看一遍（圆角描边在浅色背景上最容易发虚）。

## 6 不做的事

- 不引入特征树、历史时间线（二维制图用不上）。
- 不更换 UI 框架，不改成 egui。
- 不改命令行、特性、图层面板的功能。

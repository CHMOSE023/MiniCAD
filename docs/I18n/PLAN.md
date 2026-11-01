# 多语言版本（界面国际化）

> 状态：阶段 0 完成（2026-11-01）：`MiniI18n` 库、显示时翻译命令与界面描述的标题、语言选择与"设置 > 语言"、提取与检查工具、"文件"菜单的英文。
> 阶段 6 提前做完（2026-11-01）：运行时切换语言，"设置 > 语言"选了立即生效。
> 阶段 1 完成（2026-11-01）：主窗口外壳（命令名称与提示、MiniGUI 控件、起始页、状态栏、视图方块、布局标签、宿主的文件对话框与提示、网页加载画面）。
> 阶段 2 完成（2026-11-02）：编辑器（工具提示与回显、命令行、动态输入、特性面板、图层面板、对话框、打开 / 保存失败的原因）。阶段 3～5 未开始。
> 定位：界面、命令行提示、对话框、节点编辑器能切换成英文（以后可再加语言），图纸数据与文件格式不变；
> 源语言仍是简体中文，代码里照旧写中文原文，没有翻译的地方显示中文。
> 不做：图纸里的文字、图层名、样式名等**数据**的翻译；`docs/` 的文档翻译（README 已有两份：`README.md` 英文为默认，`README.zh-CN.md` 中文）；从右到左的文字（阿拉伯文等）。

---

## 1. 现状

没有任何本地化的机制，界面文字全部是写在代码里的中文字面量。按 2026-11-01 的 `dev` 统计（不含注释，含中文的字符串字面量，去重后约 3000 条）：

| 位置 | 条数 | 内容 | 要不要翻译 |
|---|---|---|---|
| `apps/Common/src/GUI` | ~650 | 命令名称与提示（`MainCommands.cpp` ~150）、对话框（`EditorDialogs.cpp` ~130）、节点面板、起始页、状态栏、图层面板、标题栏右侧（`ViewNavBar.cpp`） | 要 |
| `src/Editor` | ~550 | 工具的命令行提示（`src/Editor/Tools` ~380）、编辑器回显（`Editor.cpp` ~100）、特性面板的属性名（`src/Editor/Properties` ~70） | 要 |
| `src/NodeGraph` | ~1600 | 内置节点的名称、分类、说明、端口与参数名、选项、出错信息 | 要（单独一个阶段） |
| `assets/ui/minicad_ui.json` | ~70 | 菜单标题、功能区选项卡与组名、搜索框占位文字 | 要 |
| `src/UI`（MiniGUI） | ~150 | 控件自带的文字（文本框右键菜单、对话框默认按钮、停靠区、功能区、标题栏）与界面描述的出错信息（`UiLayout.cpp`） | 要 |
| `src/Document`、`src/Import` | ~100 | 打开 / 保存失败的原因、命令的名称（`GetName()`）、外部参照提示 | 面向用户的要 |
| 宿主 `apps/Win32`、`apps/Linux`、`apps/Web` | ~40 | 系统对话框的文件类型过滤（`Win32Window.cpp`）、`MessageBoxW`、Linux 的文件对话框提示、网页外壳 `apps/Web/shell/minigui_shell.html`（`lang="zh-CN"`） | 要 |
| `apps/Win32/src/Host/SelfTest.cpp` | ~600 | 自测的小节名与检查项说明 | 不要（开发者看的） |
| `src/Serialization/Dwg`、`src/Serialization/Dxf`、`src/Solid/SMLib`、`apps/Render` | ~250 | 读写诊断（MiniDWG 的通知，约 26 处）、实体内核的出错信息（`SMLibKernel.cpp` ~120）、图形设备的出错信息 | 要（阶段 4，见 3.7） |
| 各处的 `LOG_*` | ~160 行 | 控制台 / 浏览器控制台的日志，其中 `src/Editor` ~76、`src/Import` ~26、`apps/Render` ~20 | 要（阶段 4，见 3.7） |
| `src/Scene` | 少量 | 标注样式名 `GB_机械` / `GB_建筑`、多线样式说明、默认布局名 `模型` | **不能翻**（是数据，见 5.3） |

已有的、能直接用上的：
- 命令行的命令名与别名本来就是英文（`Editor.cpp` 里的 `RegisterAlias`，同 AutoCAD），选项靠按键字母（`KeyCode::X`），不随语言变
- `CommandRegistry::StripMnemonic` 已同时支持中文菜单的 `保存(&S)` 与英文的 `&Save`
- 节点图存档按 `NodeTypeInfo::Id`（如 `input.number`）与参数下标保存，不存名称——节点名称可以随便翻
- 界面描述文件保存后自动重新加载（`ui.reload`），界面能整体重建
- 界面字体：桌面版微软雅黑（`src/UI/Platform/Win32/Win32Fonts.cpp`），网页版与 Linux 版 `assets/fonts/NotoSansSC-UI.ttf`（`tools/make_web_font.py` 生成，含 ASCII、拉丁补充、GB2312 全部字符）——英文不缺字

## 2. 目标

| | 第一版 | 以后 |
|---|---|---|
| 语言 | 简体中文（源语言）、English | 繁體中文、日本語等，加一个翻译文件即可（字体覆盖见 5.6） |
| 切换 | "设置 > 语言"，**立即生效**（阶段 6，已做）；首次启动跟随系统语言 | |
| 覆盖 | 主窗口、命令行、对话框、特性 / 图层面板、节点编辑器、宿主的系统对话框、诊断信息与日志 | 文档 |
| 命令行 | 提示与回显翻译；命令名、别名、选项字母不变（中英文用户输入完全一样） | |
| 文件 | 图纸、`.mcad`、节点图、布局文件与语言无关，中英文版本互相打开不丢东西 | |

英文术语照 AutoCAD 英文版（见第 7 节术语表），让用过 AutoCAD 的人一看就懂。

## 3. 设计

### 3.1 原文即键（gettext 式）

翻译表的键就是代码里的中文原文，不另起 `prompt.line.first` 这类符号名：

```cpp
std::string GetPrompt() const override
{
    return m_hasStart ? Tr("指定下一点 [右键结束/ESC 退出]:") : Tr("指定第一个点:");
}
```

理由：3000 条字符串，改成符号名要么改得面目全非、要么代码里看不出显示什么；原文作键时迁移只是给字面量包一层，
没有翻译（或翻译表缺这一条）时自动显示中文，不会出现空白或键名。代价是改了中文原文，旧的翻译就对不上——由提取工具（3.5）报出来。

- **同字不同义**加上下文：`TrC("窗口", "关闭(&C)")` 与 `TrC("文档", "关闭(&C)")`；只在确实需要不同译文时才加。
  存起来稍后显示的写 `NC_("上下文", "…")`（与 `N_` 一样原样返回，提取时带上下文），显示处 `TrC`；
  命令的名称同字不同义时填 `Command::labelContext`（如三维的"旋转"Revolve 与修改的"旋转"Rotate），`CommandRegistry::GetLabel` 据此查
- **拼接改成整句加占位符**：`std::to_string(n) + " 个外部参照没有找到，请选择所在的文件夹"` →
  `TrF("{0} 个外部参照没有找到，请选择所在的文件夹", n)`。英文的语序、单复数与中文不同，碎片拼接没法翻。
  占位符用 `{0}`、`{1}`（按位置，译文里可以换顺序），自己实现替换，不依赖 `std::format` 的运行时格式串（三个平台的标准库支持程度不一）
- **单复数**：不做复数规则，改写成不需要复数的说法（"Missing external references: 3"）
- **只标记、显示时再翻**：存进数据结构、稍后才显示的字符串（命令注册的 `label` / `tooltip`、`NodeTypeInfo` 的各字段、静态表）
  用 `N_("…")` 标记——它原样返回字面量，只是让提取工具找到；显示的地方再 `Tr()`。见 3.3

### 3.2 库的划分

MiniCAD 核心库、MiniGUI、MiniDWG 三者之间：MiniCAD 私有链接 MiniDWG，MiniGUI 与它们都不相干（`MiniCADApp` 同时链接 MiniCAD 与 MiniGUI）。
三者都要翻译（MiniDWG 的诊断信息也要，见 3.7），所以翻译放进一个**不依赖任何库的小静态库**，三者都链接它：

| 位置 | 内容 |
|---|---|
| 新库 `MiniI18n`（`cmake/MiniI18n.cmake`，源码 `src/I18n/MiniI18n/`，include 根 `src/I18n`，命名空间 `MiniI18n`） | `Catalog`（键 → 译文，带上下文），全局的当前翻译表，`Tr` / `TrC` / `TrF` / `N_`，`{0}` 占位替换。一个头文件 + 一个 `.cpp`，只用标准库 |
| MiniCAD、MiniGUI、MiniDWG、`MiniCADSolid`、渲染后端 | 链接 `MiniI18n`，直接 `#include "MiniI18n/I18n.h"` 调用；MiniGUI 在建控件时翻译命令的名称 / 提示、界面描述的标题，控件自带的文字也经它 |
| 共用应用 `apps/Common` | `GUI/Language.cpp`：选定语言、读翻译文件、装进 `MiniI18n`（翻译文件的 JSON 由 `MiniI18n::Catalog::Parse` 自己解析，支持注释，单元测试不用链接 MiniGUI）。**宿主在创建图形设备之前调用**（3.7） |

include 根目录：`src/I18n` 下只有 `MiniI18n/` 一个子目录，`MiniI18n/I18n.h` 这个相对路径在其他根下都不存在，不会与 `src`、`src/UI` 的 `Core/` 等撞名。
不用"每个库各留一个翻译钩子"的做法：那样占位替换要写三份，MiniDWG 的通知也没法在原处整句翻译。

查表：`std::unordered_map` + 透明哈希（`std::string_view` 直接查），`Tr` 返回 `std::string`（复制一份：拼接方便、没有生命周期问题，开销可以忽略）；翻译表装上后**不再修改**，
当前表的指针是原子的，换语言时换指针、旧表留到程序退出才释放（每张几十 KB），所以后台线程（`OpenDrawings` 里的 `DocumentManager::Load` 出错信息）
可以直接调用，不用加锁。每帧都调用的 `GetPrompt` 只是一次哈希查找，开销可以忽略。

### 3.3 什么时候翻译

一条规则：**数据里存原文，显示的那一刻翻译**。

- 命令：`MainCommands.cpp` 注册时 `.label = N_("保存(&S)")`，MiniGUI 建菜单、功能区、提示、命令搜索时经翻译函数
- 界面描述：`minicad_ui.json` 里写的仍是中文，`UiLayout` 读到 `title` / `label` / `tooltip` / `search` 时翻译；
  用户自己改过的文字在翻译表里查不到，就原样显示（用户写什么显示什么）
- 节点：`NodeTypeInfo` 的名称、分类、说明、端口、参数、选项存原文，节点编辑器与节点面板显示时翻译
- 工具提示、回显、对话框里临时拼出来的文字：直接 `Tr()`

这样做的原因：
1. 静态初始化、懒加载的注册表（如 `NodeRegistry::Builtin`）可能早于翻译表加载，提前翻译会悄悄得到中文
2. 运行时切换语言（阶段 6）时不用重建这些数据
3. **按显示文字比较的代码**不会因为翻译而失效——迁移时要把这类代码找出来改成按 Id / 下标比较，
   例如 `NodePanel.cpp` 里拿节点类型的 `Name` 与下拉框的选中文字比较

### 3.4 翻译文件

`assets/i18n/<语言>.json`，随 `assets/` 复制到可执行文件旁、打包进网页版的虚拟文件系统：

```jsonc
{
  "language": "en",
  "name": "English",            // "设置 > 语言"里显示的名字（用该语言自己写）
  "messages": {
    "保存(&S)": "Save",
    "指定第一个点:": "Specify first point:",
    "{0} 个外部参照没有找到，请选择所在的文件夹": "{0} external reference(s) not found. Choose the folder that contains them."
  },
  "contexts": {
    "窗口": { "关闭(&C)": "Close" },
    "文档": { "关闭(&C)": "Close Drawing" }
  },
  "obsolete": { }               // 提取工具把代码里已经没有的条目移到这里，确认没用后删掉
}
```

- 简体中文没有翻译文件（原文就是）
- 值为空串表示还没翻译，显示原文
- 翻译文件写错（JSON 格式错误）时退回中文，日志里报出错的行号（阶段 0 只进日志；以后可以像界面描述文件那样显示在状态栏）

### 3.5 提取与检查工具 `tools/i18n_extract.py`

- **提取**：扫描 `src/`、`apps/` 里 `Tr` / `TrC` / `TrF` / `N_` 的字面量与 `minicad_ui.json` 的文字字段，更新每个翻译文件：
  新条目加进 `messages`（值为空），代码里没有了的移进 `obsolete`；输出各语言的完成度
- **检查**（`--check`，进 CI 的 Windows 任务）：
  - 占位符一致：译文里的 `{n}` 与原文相同
  - 命令行选项字母一致：原文 `[闭合(C)/放弃(U)]` 里括号中的字母，译文里也要有（英文可写成 `[Close(C)/Undo(U)]`）——选项靠按键，字母错了按了没反应
  - 助记符：译文里带 `&` 时同一个菜单里的字母不重复（菜单结构取自界面描述文件）；英文不带，只是警告
- **漏网检查**（`--lint`）：列出没有包 `Tr` / `N_` 的中文字面量，以及格式串含中文的 `LOG_*`（要改成 3.7 的写法）。白名单：注释、`tests/`、`SelfTest.cpp`、`tools/`、
  第 5.3 节的数据字符串（行尾注释 `// i18n: 数据` 标明）。迁移阶段只报告，阶段 5 起进 CI
- **反查**（`--where "<译文>"`）：由英文（或其他语言）的一句话找到中文原文和代码位置——英文用户贴来的日志、截图靠它定位（3.7）

### 3.6 语言设置

- 优先级：命令行参数 `--lang <语言>` / 环境变量 `MINICAD_LANG`（测试、截图用）→ 用户设置 → 系统语言 → 中文
- 系统语言：新的 `AppPlatform::GetSystemLanguage()`——Win32 用 `GetUserDefaultUILanguage`，网页版用 `navigator.language`，
  Linux 用 `LC_ALL` / `LC_MESSAGES` / `LANG`。`zh*` → 简体中文（繁体翻译出来之前 `zh-TW` 也用简体），没有对应翻译文件的 → English
- 用户设置：用户数据目录（`AppPlatform::GetUserDataDir`）下新建 `settings.json`，`{ "language": "en" }`；以后主题等设置也放这里。
  网页版的用户数据目录是空的，改存浏览器的 `localStorage`（`WebApp` 里经 JS 读写），网址带 `?lang=en` 时优先
- 界面：标题栏右侧"设置"菜单（`ViewNavBar.cpp` 的 `AddMenuButton`）加"语言"子菜单，列出 `assets/i18n/` 里的语言（显示各自的 `name`），
  选了立即切换（阶段 6）
- 伪语言 `--lang qps`（只给开发用，不在菜单里）：每条译文 = `[原文]` 再拉长 40%（补 `~`）。用来一眼找出漏掉的文字（没有方括号的就是没走 `Tr`）
  和放不下的控件。比等英文翻完再看高效

### 3.7 诊断信息与日志

诊断信息同界面文字一样走翻译表，用户看到的（状态栏、命令行、出错对话框、控制台 / 浏览器控制台）都是当前语言。

- **MiniDWG 的通知**：`Notify(NotificationType::Warning, …)` / `Warn(notify, …)` 在产生的地方整句翻译，
  `"找不到符号表 " + … + "（类型 " + … + "）"` 这类拼接改成 `TrF("找不到符号表 {0}（类型 {1}）", …)`。
  `NotificationHandler` 的接口不变，`CadExchange.cpp` 的 `MakeNotify` 收到的已是译文：错误照旧成为"打开失败的原因"，警告照旧进日志。
  读图在后台线程，翻译表只读（3.2），可以直接调用
- **专有名词不翻**：DXF 名称（`BLOCK_RECORD`）、组码、句柄、`HRESULT`、校验名（`AcFssFcAJMB`）、着色器编译器的原始输出都放进占位符，译文里原样出现
- **日志**：`LOG_*` 是 printf 式的格式串，**格式串本身不能拿去翻译**——译文里 `%s` / `%d` 的个数或顺序错了就是未定义行为。
  `Core/Log.h` 加 `LOG_INFO_TR` / `LOG_WARN_TR` / `LOG_ERROR_TR`（`{0}` 占位，内部先 `TrF` 再输出），含中文的日志都改用它们；`--lint` 报出格式串含中文的 `LOG_*`
- **实体内核**：`SMLibKernel.cpp` 的 `Fail("Box: 边长必须为正")` 等改 `TrF`（前缀的英文操作名保留）；`3rd/SMLib` 自己的英文信息不动
- **图形设备**：`apps/Render` 各后端的出错信息。图形设备在 `MainFrame::Initialize` **之前**创建（`Win32Window::Initialize` 里先 `CreateGraphicsDevice`），
  所以翻译表要由宿主在创建图形设备之前装好：`apps/Common` 提供一个加载函数（参数是资源目录、用户数据目录、命令行的 `--lang`），
  `Win32Window`、`WebApp`、`LinuxApp` 启动时最先调用。网页版的图形设备是异步就绪的，也在请求设备之前装
- **问题报告**：英文用户贴来的日志由 `tools/i18n_extract.py --where` 反查原文与代码位置；开发者也可以 `--lang zh-CN`（或 `MINICAD_LANG`）复现出中文日志。
  "关于"对话框显示当前语言，便于报告时注明
- **测试**：单元测试不装翻译表，`MiniDWGTests` 等按中文通知断言的测试不受影响；`I18nTests` 加一条"装上英文表后 MiniDWG 的通知是英文"

## 4. 分阶段

每个阶段单独编译、测试、提交，做完一段英文版就多一块能用。

### 阶段 0：基础设施（小）——已完成（2026-11-01）

做法与计划的出入：
- `Tr` 返回 `std::string`，不是表里字符串的引用（3.2）；翻译文件的 JSON 由 `MiniI18n::Catalog::Parse` 解析（不依赖 MiniGUI）
- 用户设置做成 `AppPlatform::ReadUserSetting` / `AppPlatform::WriteUserSetting`：默认实现在 `GUI/UserSettings.cpp`（用户数据目录下 `settings.json`），`WebApp` 改用 `localStorage`（键 `MiniCAD.<名称>`）
- 每种语言一条命令 `ui.language.<代码>`（`kLanguageCommandPrefix`），"设置"菜单里写 `@language` 展开成"语言"子菜单（`ViewNavBar.cpp`）；选了立即切换（阶段 6）
- 自测、基准（`--selftest` / `--bench`）不读设置、不看系统语言，只认 `--lang` / `MINICAD_LANG`：自测按中文文字检查
- 关于对话框加了"界面语言"一行；功能区命令搜索同时匹配译文与原文（原计划在阶段 1）
- 除了"文件"菜单，界面描述文件里的功能区选项卡与组名、命令提示也一并有了英文（提取到什么就翻了什么，共 120 条）；起始页（左侧导航、标题、新建卡片、最近、示例的分组名）也已翻译，示例的名称与说明来自 `examples.json`，仍是中文；功能区按钮的命令名称仍是中文，等阶段 1
- 英文译文不带助记符（`&`）：菜单上不显示下划线
- `--lint` 没做（阶段 5 前补上）；翻译文件出错只进日志
- 没在网页版、Linux 版上实际运行过（只改了代码，Linux 宿主在 Windows 上不编译）


- `MiniI18n` 库（`cmake/MiniI18n.cmake`）：`Catalog`、`Tr` / `TrC` / `TrF` / `N_`；MiniCAD、MiniGUI、MiniDWG、`MiniCADSolid`、渲染后端链接它
- `CommandRegistry` 与 `UiLayout` 在显示命令名称、提示、标题时调用 `Tr`
- `Core/Log.h` 的 `LOG_*_TR`（3.7）
- `apps/Common` 的加载函数，宿主在创建图形设备之前调用；`AppPlatform::GetSystemLanguage` 在 `Win32Window`、`WebApp`、`LinuxApp` 各实现一份；`settings.json`；`--lang` 与伪语言
- "设置 > 语言"子菜单
- `tools/i18n_extract.py` 的提取、`--check` 与 `--where`；`assets/i18n/en.json` 先只放"文件"菜单几条
- `tools/make_web_font.py` 的字符来源加上 `assets/i18n/*.json`（以后加语言时译文里的字都进网页字体）
- 测试：`tests/Core/I18nTests.cpp`（查表、上下文、占位符换序、缺条目回退原文、翻译文件格式错误、空串当没翻译）
- 验收：`MiniCADWin --lang en` 的"文件"菜单显示英文；`--lang qps` 能看出哪些没包

### 阶段 1：主窗口外壳（中）——已完成（2026-11-01）

做法与计划的出入：
- 加了 `NC_`（带上下文的标记）与 `Command::labelContext`（3.1）：视图方块的单字（上 / 下 / 东 / 西……）、两个"旋转(&R)"命令要分开译
- 保存格式表 `kSaveFormats` 的名称改 `N_`，各宿主显示时翻译；桌面版的文件类型过滤每次打开对话框时现拼（`Win32Window.cpp`），Linux 版翻译后的过滤随请求保存到回调（SDL 要求回调前一直有效）
- 网页外壳的加载画面在程序和翻译表读进来之前显示：脚本按网址的 `?lang=`、`localStorage` 的设置、浏览器语言只分中文与英文；
  程序起来后 `WebApp` 设定 `<html lang>`，之后切换语言经新加的 `AppPlatform::OnLanguageChanged` 跟着改
- 切换语言时 `MainFrame::Retranslate` 另外重写状态栏（`StatusBarView::Retranslate`）、视图方块的提示（`ViewCube::Retranslate`），并重建模型 / 布局标签（模型页显示 `Tr("模型")`）；视图方块面上的字每帧翻译
- 英文更长（5.5）：功能区按钮按名称算宽度时单词不断开（窄模式也至少放得下最长的词），名称分成两行时图标与名称一起上移；视图方块面上的字超出面宽时缩小字号
- 命令搜索同时匹配译文、原文与命令 Id，阶段 0 已做
- 快捷键冲突的列表改用 `, ` 分隔（不随语言变），整句 `TrF("{0}：{1}", 快捷键, 列表)`
- 留到阶段 2：特性面板（含颜色名"其他"）、图层面板、命令行（`CommandConsole.cpp` 的"命令:"、占位文字）、多个值时的"*多种*"（`Binding.cpp`、`NumberBox.cpp`）、线型预览的名称（`CadPreview.cpp`）；
  这些在 `--lang qps` 下仍不带方括号
- 网页版只编译过（`build_wasm.bat`），没在浏览器里看；Linux 宿主在 Windows 上不编译，没编译过


- `MainCommands.cpp` 全部命令的名称与提示改 `N_`
- `minicad_ui.json` 的菜单、功能区选项卡与组名、搜索框占位文字
- MiniGUI 控件自带的文字：文本框右键菜单（`TextBox.cpp`）、对话框默认按钮（`Dialog.h` 的"确定"）、停靠区、功能区、标签页、标题栏；
  `UiLayout.cpp` 的出错信息也给用户看（状态栏），一并翻
- 起始页、状态栏（`StatusBarView.cpp`）、标题栏右侧（AI、设置、帮助、版本）、关于对话框、文档标签（"未命名"）、模型 / 布局标签（见 5.3）
- 宿主：`Win32Window.cpp` 的文件类型过滤与 `MessageBoxW`（UTF-8 转宽字符）、Linux 的文件对话框提示、网页外壳的 `lang` 属性与网页里的提示文字
- 命令搜索（`ui.search`）同时匹配译文、中文原文与命令 Id（中文用户切到英文也能用中文搜）
- 验收：英文下逐个打开菜单、功能区选项卡、"设置"/"帮助"，`--lang qps` 下没有不带方括号的文字；看一遍放不下的地方（5.5）

### 阶段 2：编辑器（大）——已完成（2026-11-02）

做法与计划的出入：
- 工具头文件经 `ITool.h` 统一包含 `MiniI18n/I18n.h`；`PickPointTool` 收 `N_` 原文，`GetPrompt` 时翻译
- 面积、周长、体积等数值先按原来的精度格式化成字符串（`Editor.cpp` 的 `Fixed4`、`%.4g`、`%g`），再填进 `TrF`——`TrF` 的数值占位是 `%g`
- 同字不同义：三维实体的操作名（拉伸 Extrude、旋转 Revolve……）用 `NC_("三维实体", …)`；多线对正 `NC_("多线对正", …)`；点样式的形状 `NC_("点样式", …)`；图层表头"开 / 锁" `NC_("图层表头", …)`；特性面板的行名"图层"（单数 Layer，面板标题"图层"是复数）`NC_("特性面板", "图层")`
- 多段线的宽度特性原来也叫"线宽"，与线宽（Lineweight）同字：改名"全局宽度"（同 AutoCAD 中文版，英文 Global width）；视图缩放命令的默认名改"缩放视图"，与比例缩放的"缩放"分开
- 特性名（`PropertyDesc::name`）同时是多选求交集的键：表里只标 `N_`，面板显示时翻译
- 特性面板、图层面板、功能区的图层条只建一次：建的时候把"写译文"的回调登记到 `PanelSet::retranslate`，`MainFrame::Retranslate` 逐个再调；命令行加 `CommandConsole::Retranslate`；动态输入框的字段名每次刷新时翻译
- 图层表"开 / 锁"两列加宽（40 / 48），放得下英文表头
- 实体类型名在应用里只出现在节点图的值显示（`src/NodeGraph/Value.cpp`），归阶段 3
- 数据不翻、加 `// i18n: 数据` 注明：新图层 / 样式的默认名、示例图纸内容、天正的图层名与材料名、标注样式与多线样式、TrueType 字体名映射、字体预览的中文样例
- 留到阶段 4：日志的续行（`EllipseTool.h`、`SplineTool.h`、`RotateTool.h`）、填充图案文件的解析错误（`HatchPatternLibrary.cpp`）、MiniDWG 传上来的出错信息（`firstError`）
- 验收只做了一部分：英文下截图看了命令行（圆角、多线的提示）、特性面板、图层面板，`--lang qps` 下这几处都带方括号；没有逐个命令在英文下跑一遍；网页版只编译过；Linux 版没编译

- 工具的命令行提示（`src/Editor/Tools`，含 `src/Editor/Tools/Modify`）、编辑器回显（`Editor.cpp`）、动态输入（`DynamicInput.cpp`）、命令行界面层（`CommandLine.cpp`）
- 命令的名称（`src/Document/Command` 里各命令的 `GetName()`）目前不显示给用户，只改成 `N_` 标记，等以后有撤销列表再在显示处翻译
- 特性面板的属性名与选项（`src/Editor/Properties/PropertyTable.cpp`）、实体类型名
- 对话框（`EditorDialogs.cpp`：填充、阵列、块、文字样式、标注样式……）、图层面板（`LayerPanels.cpp`）
- 打开 / 保存失败的原因（`Document.cpp`、`DocumentManager.cpp`）、外部参照与读写进度提示、`CadExchange.cpp` 里面向用户的提示
- 提示里的拼接全部改成整句（如 `SolidTools.h` 的 `"旋转轴：" + (X ? "X 轴" : "Y 轴")`）
- 验收：英文下跑一遍常用命令（直线、多段线、圆、修剪、圆角、阵列、标注、填充、块），命令行提示与 AutoCAD 英文版的说法一致；`--check` 的选项字母检查通过

### 阶段 3：节点编辑器（大，但机械）

- `src/NodeGraph/Builtin*.cpp` 的节点名称、分类、说明、端口、参数、选项改 `N_`，节点编辑器（`NodeEditor.cpp`）与节点面板（`NodePanel.cpp`）显示时翻译
- 搜索关键字（`NodeTypeInfo::Keywords`）保留中文，再加英文——两种语言都能搜到
- `Value.cpp`、`Graph.cpp`、`Expression.cpp`、`Group.cpp` 里显示在节点上的出错信息
- 把按名称比较的地方改成按 Id / 下标（3.3）
- 验收：英文下打开 `assets/nodes/examples` 的示例节点图，节点、端口、参数都是英文，计算结果与中文下相同；中英文之间互相打开节点图不变

### 阶段 4：诊断信息与日志（中）

- MiniDWG：`src/Serialization/Dwg`、`src/Serialization/Dxf` 的通知与出错信息改 `TrF`（拼接改整句，专有名词进占位符），`cmake/MiniDWG.cmake` 链接 `MiniI18n`
- 日志：含中文的 `LOG_*`（约 160 行，`src/Editor`、`src/Import`、`apps/Render`、各宿主）改 `LOG_*_TR`
- 实体内核 `SMLibKernel.cpp` 的出错信息（节点上、命令行里显示）
- 图形设备：`apps/Render` 各后端与 `GraphicsDevice.cpp` 的出错信息；创建失败时给用户看的提示
- 其他：填充图案文件的解析错误（`HatchPatternLibrary.cpp`）、字体加载失败等
- 验收：`--lang en` 打开 `tests/Data` 里会产生警告的样图（版本较老的 DWG、天正图纸），控制台与状态栏是英文；
  `--lang qps` 下控制台没有不带方括号的中文；`--gpu` 指定一个不可用的后端，退回时的提示是英文；`MiniDWGTests` 照常通过

### 阶段 5：收尾与发布（小）

- `--lint` 进 CI（新加的中文字面量、格式串含中文的日志必须改写或进白名单）
- 重新生成网页字体；去掉 `README.md` 里"界面目前是中文"的说明，两份 README 的截图换成各自语言的界面
- 自测加一轮 `--lang en`：只检查界面能建起来、命令能执行，不重复全部检查项
- 发布说明写明语言切换的位置；英文翻译由 AI 按术语表初译，主窗口外壳与常用命令的提示人工过一遍

### 阶段 6：运行时切换语言——已完成（2026-11-01，提前到阶段 1 之前做）

参照 MiniBIM（D:\MiniBIM 的 MainFrame::SetLanguage / RetranslateUi：换语言后重新应用界面描述、重建代码里的面板）。做法：
- 换表：`MiniI18n::SetCatalog` 换原子指针，换下来的表不释放（后台线程可能正在查），到程序退出才释放
- `SwitchLanguage`（`GUI/Language.cpp`）换表并写用户设置；`MainFrame::SetLanguage` 再调 `MainFrame::Retranslate`
- `MainFrame::Retranslate`：先记下主窗口与各文档窗口的停靠布局（`DockSpace::SaveLayout`），`ReloadUi` 重新应用界面描述
  （菜单、功能区、标题栏、停靠区标签；浮出的面板窗口会关掉），再 `DockSpace::LoadLayout` 恢复；
  代码里建的、显示了译文的部分各自重建：起始页（`BuildStartPage`）、标题栏右侧（`BuildAppBar`）、各窗口的导航条（`BuildViewNavBar`）
- 停靠面板的标题（"特性"、"图层"……）注册时存原文（`N_`），`UiLayout` 放进停靠区时翻译
- 菜单项的回调里不能重建菜单所在的界面：`ui.language.<代码>` 经 `AppPlatform::Post` 放到这一帧之后
- 自测第 28.5 节：主窗口 + 一个文档窗口，切到英文再切回，检查功能区、命令名称、起始页与停靠布局

**以后各阶段的约束**：代码里建的界面（状态栏、特性 / 图层 / 节点面板、命令行……）开始显示译文时，要在 `MainFrame::Retranslate`
里重建它或重写它的文字，否则切换后它还是旧语言；只在打开时才建的（对话框、弹出菜单、提示框）不用管。
`--lang qps` 启动后再切回简体中文，能看出哪些地方没跟着换（还带方括号）。

## 5. 约束与坑

### 5.1 选项字母与命令名不翻译

命令行的选项靠按键（`e.Key == KeyCode::X`），命令名与别名是 AutoCAD 的英文名。所以中文版与英文版输入完全一样，
提示里只翻译文字，括号里的字母照抄。提取工具检查这一点（3.5）。

### 5.2 数字与单位

不用 `std::locale`，小数点永远是 `.`、不加千位分隔符——CAD 的输入解析、DXF 文本、坐标显示都按这个，德语等系统区域设置下也不变。

### 5.3 数据字符串不能翻译

下面这些是写进图纸 / 文档的数据，翻了会改变文件内容，或让中英文版本打开同一张图得到不同结果：

- 默认布局名：`src/Scene/Scene.h` 的 `DrawingLayout` 默认名为 `模型`。DWG 里模型页由 `*Model_Space` 表示，导入时 `Model` / `模型` 都认（`CadExchange.cpp`）。
  显示时：`Model == true` 的布局标签显示 `Tr("模型")`，不改数据
- 标注样式名 `GB_机械` / `GB_建筑`（`src/Scene/DimStyleTable.cpp`）、多线样式的说明（`src/Scene/MLineStyleTable.cpp`）、图层名、文字样式名、填充图案名
- 示例 / 测试图纸里的文字内容

这些地方加行尾注释 `// i18n: 数据`，`--lint` 跳过。

### 5.4 标点与格式

- 中文原文用全角标点（`：`、`，`、`（）`），英文译文用半角并按英文习惯加空格；命令行提示照 AutoCAD 英文版以 `:` 结尾
- 原文里的 `(&S)` 是中文菜单的助记符写法，英文译文不写助记符（`Save`，菜单上不显示下划线）；需要时可写成 `&Save`，`CommandRegistry::StripMnemonic` 两种都处理；快捷键不翻译
- 原文里有 `\n` 的多行提示，译文行数可以不同

### 5.5 英文更长

中文一个字约一个字宽，英文同样意思通常宽 1.5～2.5 倍。要逐个看：功能区按钮下的文字与组名（宽度不够时整组折叠，英文下会更早折叠）、
特性面板的属性名列宽、固定宽度的对话框与按钮、状态栏的开关按钮、起始页、命令行提示（长提示截断）、提示框（`tooltip`）。
用 `--lang qps` 先找，英文翻完再按实际文字调；尽量改成按内容算宽度，不为英文单独写尺寸。

### 5.6 字体

- 英文：现有字体都覆盖，不用改。可以评估英文下桌面版主字体换 Segoe UI、微软雅黑作后备（拉丁字形更好看），不是必需
- 中文界面文字（图层名、文件名）在英文界面下照样要显示，所以中文字体不能去掉
- 以后加繁体 / 日文：网页字体子集要扩充（GB2312 不含大部分繁体字），或另带一个字体；韩文微软雅黑不覆盖，要加后备字体（如 Malgun Gothic）

### 5.7 其他

- 翻译表装上后只读；运行中换语言只换当前表的指针，旧表不释放（`MiniI18n::SetCatalog`），后台线程查表不用加锁
- 翻译表缺条目、文件损坏时一律显示原文，不能让界面出现空白
- 单元测试不装翻译表，`Tr` 原样返回中文，现有按中文提示断言的测试（如 `tests/UI/ConsoleTests.cpp`）不用改
- 网页版的翻译文件随 `assets/` 打进虚拟文件系统，大小可以忽略（英文全部约 200 KB，压缩后更小）

## 6. 先不做 / 待定

- **开发者工具的输出**：自测（`SelfTest.cpp`）、`--bench`、`tools/` 下的命令行工具（`MiniCADDwgInspect` 等）保持中文
- **第三方库自己的信息**：`3rd/SMLib`、着色器编译器等的英文原文不翻，作为占位符嵌进译文（3.7）
- **AutoCAD 的系统变量、DXF 名称**：本来就是英文，不动
- **繁体中文、日文**：框架支持，等有需要再加翻译文件与字体
- **外部翻译协作**：需要时让提取工具导出 / 导入 `.po`，第一版只用 JSON

## 7. 术语表（英文照 AutoCAD）

| 中文 | English | 中文 | English |
|---|---|---|---|
| 直线 | Line | 构造线 / 射线 | Construction Line / Ray |
| 多段线 | Polyline | 多线 | Multiline |
| 样条曲线 | Spline | 椭圆弧 | Elliptical Arc |
| 图案填充 | Hatch | 区域覆盖 | Wipeout |
| 面域 | Region | 二维填充 | 2D Solid |
| 移动 / 复制 / 旋转 / 镜像 | Move / Copy / Rotate / Mirror | 缩放 / 拉伸 | Scale / Stretch |
| 偏移 / 修剪 / 延伸 | Offset / Trim / Extend | 打断 / 圆角 / 倒角 | Break / Fillet / Chamfer |
| 分解 / 阵列 | Explode / Array | 夹点 | Grip |
| 单行文字 / 多行文字 | Single Line Text / Multiline Text | 文字样式 / 标注样式 | Text Style / Dimension Style |
| 线性 / 对齐 / 角度标注 | Linear / Aligned / Angular Dimension | 半径 / 直径 / 折弯 / 弧长 / 坐标 | Radius / Diameter / Jogged / Arc Length / Ordinate |
| 引线 / 多重引线 | Leader / Multileader | 块定义 / 插入块 | Block Definition / Insert Block |
| 属性定义 | Attribute Definition | 外部参照 | External Reference (Xref) |
| 图层 / 线型 / 线宽 | Layer / Linetype / Lineweight | 随层 / 随块 | ByLayer / ByBlock |
| 冻结 / 锁定 / 关闭 | Freeze / Lock / Off | 当前图层 | Current Layer |
| 模型 / 布局 / 视口 | Model / Layout / Viewport | 图纸空间 / 模型空间 | Paper Space / Model Space |
| 对象捕捉 / 正交 / 极轴 | Object Snap / Ortho / Polar Tracking | 动态输入 | Dynamic Input |
| 特性 / 命令行 / 功能区 | Properties / Command Line / Ribbon | 范围缩放 | Zoom Extents |
| 俯视 / 前视 / 右视 / 西南等轴测 | Top / Front / Right / SW Isometric | 视图方块 | ViewCube |
| 着色 / 线框 | Shaded / Wireframe | 实体 / 曲面 / 网格 | Solid / Surface / Mesh |
| 拉伸（实体） / 旋转（实体） | Extrude / Revolve | 并集 / 差集 / 交集 | Union / Subtract / Intersect |
| 节点编辑器 / 烘焙 | Node Editor / Bake | 天正对象 | Tianzheng (TArch) Object |

翻译时先查这张表，新术语定下来后补进来；"拉伸"在二维修改（Stretch）与实体（Extrude）里不同，靠上下文区分（3.1）。



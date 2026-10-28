---
covers: .agents/features/*.md
---

# 功能指南索引

改某一块功能之前先读对应的指南：入口在哪、数据怎么流、有哪些已知的约束和坑。各库的整体开发计划在 `docs/*/PLAN.md`，按版本的变化在 `docs/release-notes/`。

| 指南 | 内容 | 主要代码 | 测试 |
|---|---|---|---|
| [cad-exchange](cad-exchange.md) | DWG/DXF 读写：写出版本、已映射的实体、以原图为底另存、文字样式与线型、OLE 对象 | `src/Import/CadExchange.*`、`src/Serialization/Dwg|Dxf` | `MiniCADTests`、`MiniCADCadPreserveTests`、`MiniDWGTests` |
| [xref](xref.md) | 外部参照：读入、查找文件、未加载时显示路径、选文件夹再找（含网页版） | `CadExchange.cpp` 的 `Importer::LoadXref` / `ResolveXrefPath`、`MainFrame::LocateXrefs` | `MiniCADTests`（`CadXrefTests.cpp`）、`tests/Web/FolderPickerTests.cjs` |
| [layout-viewport](layout-viewport.md) | 布局页与图纸空间视口：视图中心换算、冻结图层、模型内容合成、`$PSLTSCALE` | `ViewportEntity`、`Editor::ComposeModelIntoViewport` | `MiniCADTests`（`LayoutTests.cpp`、`OleFrameTests.cpp`） |
| [tianzheng](tianzheng.md) | 天正对象（`TCH_*`）的解析与显示适配 | `src/Import/Tch*.h` | `MiniCADTch*Tests`、`MiniCADTianzhengTests` |
| [view3d](view3d.md) | 三维视图、着色与轮廓线、视图方块、背景深浅 | `Camera`、`Editor`、`GUI/ViewCube` | `SolidViewTests` |
| [main-window](main-window.md) | 主窗口界面：布局、功能区、面板（含浮出成独立窗口，浮动窗口里合并标签、最大化）、文档拖出成独立窗口（对等多窗口：各有标题栏、功能区、快捷键）、命令行、桌面 / 网页 / Linux 宿主| `apps/Common/src/GUI`、`apps/Win32`、`apps/Web`、`apps/Linux` | `MiniCADWin --selftest`、`MiniGUITests` |
| [display-performance](display-performance.md) | 显示性能：静态 / 动态流、虚线延迟铺设、字形实例化、选择集与夹点缓存、打开图纸后分帧细分 | `Editor`、`Viewport`、`DrawContext`、渲染器 | `MiniCADWin --bench` |

新增功能指南时照现有的结构写（概述 → 入口与数据流 → 约束与坑 → 测试与排查），开头用 `covers:` 列出覆盖的代码（逗号分隔，`*` 通配），并在上表里加一行。写完运行 `.agents/scripts/check-docs.ps1` 核对引用。

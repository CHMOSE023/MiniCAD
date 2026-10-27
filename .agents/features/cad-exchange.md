---
covers: src/Import/CadExchange.*, src/Serialization/Dwg/*, src/Serialization/Dxf/*, src/Serialization/Database/*, src/Core/Ole/*, src/Document/Document.*
---

# DWG/DXF 交换

## 概述

`src/Import/CadExchange.*` 经 MiniDWG 的 `CadDatabase` 与 `Scene` 互转：`ImportCad`（读）、`ExportCad`（写）。`Document::LoadFromFile` / `SaveToFile` 按扩展名 `.dwg` / `.dxf` 分派，DXF 写 ASCII。核心库的公开头文件不暴露 MiniDWG 类型，映射代码只在 `CadExchange.cpp` 里。

## 写出版本

- `CadSaveVersion`：2018（AC1032）、2013（AC1027，AutoCAD 2013～2017 共用，界面上标为"2013/2014"）、2010、2007、2004、2000；新建文档默认 2004（`kDefaultCadSaveVersion`）
- 2007 只能写 DXF：MiniDWG 没有实现 AC1021 的写出，DWG 2007 改写为 2010
- 2007 以前的版本按 `ANSI_936` 代码页写中文
- 打开时记住版本（`Document::GetCadSaveVersion`），另存为时由用户选择；格式列表 `kSaveFormats`（`AppPlatform.h`）桌面版与网页版共用

## 已映射的内容

读入（`Importer::Convert` 一带，按 MiniDWG 的类型分派）：

- 表：图层、线型、文字样式、多线样式、块（含外部参照块）、布局（标注样式只在组装标注时查参数，不导入成表）
- 实体：Line / Circle / Arc / Ellipse / Point / Polyline / LwPolyline / Spline / Text / MText / Insert（含属性 ATTRIB 与属性定义 ATTDEF）/ Solid / 3DFace / Hatch（直线、圆弧、椭圆弧、样条、多段线边界）/ MultiLeader / MLine / Wipeout / Viewport；PolyfaceMesh 与 Mesh 读成 `MeshEntity`
- 标注用其匿名块还原为 Insert；绘制次序按 SortEntitiesTable
- 三维实体 `Solid3DEntity` 只导出（写成多面网格，见 `docs/Solid/PLAN.md`）
- 不支持的类型跳过并按类型统计，导入结束时 `LOG_WARN`（读入 `m_skipped`；导出时不能写的实体经 `Skip` 同样统计）。改映射前先用 skill `drawing-debug` 看原图里有哪些类型
- OLE2FRAME（嵌入的 Word / Excel 等）只导入显示：MiniDWG 原样保留，`Dwg::DecodeOle2Frame` 解出数据，`src/Core/Ole/OleObject` 解析 AutoCAD 头与 OLE 复合文档里的展示图（WMF / EMF / DIB），显示为 `OleFrameEntity`；图元文件经 `AppPlatform::RasterizeOle` 光栅化（桌面版 GDI，网页版只画边框）
- 天正对象见 [tianzheng](tianzheng.md)，外部参照见 [xref](xref.md)，图纸空间视口见 [layout-viewport](layout-viewport.md)

## 以原图为底另存（`CadSource`）

打开时 `Document` 留下原图 `CadSource`：原始字节、场景实体 ↔ 原图句柄、导入时的签名。另存为**同格式同版本**时 `ExportCad` 重新读出原图，只改写变化的实体与表项；未修改的实体（包括天正 `TCH_*` 等自定义对象）、跳过的实体、图纸空间原样写回。格式或版本不同时重新生成整张图纸，自定义对象只剩恢复出的普通几何。

## 文字样式与线型

- TrueType 样式的文件名为空时，按扩展数据里的字体族名换算成文件名（SimSun → simsun.ttc 等）
- 与预置样式（Standard、GB2312）同名的按图纸参数更新字高、宽度因子、倾斜；图纸没有的预置样式删掉（不再写进 DWG）
- 天正文字的显示宽度因子写在 `MTextEntity::SetWidthFactor`（导出写成开头的 `\W0.7;`），不派生文字样式
- 含形 / 文字的复杂线型保留这些元素的长度（形本身不画，0 长度的形显示为点）
- `$PSLTSCALE` 读写（`Scene::GetPaperSpaceLineTypeScaling`），用法见 [layout-viewport](layout-viewport.md)
- 字体查找：先 `fonts/`，再 `AppPlatform::GetSystemFontDirs()`（桌面版 `%WINDIR%\Fonts` 与当前用户安装的字体），都没有才用自带字体代替

## 测试与排查

- `MiniCADTests`（`CadExchangeTests.cpp` 等）、`MiniCADCadPreserveTests`、`MiniDWGTests`
- 打开后显示不对：用 skill `drawing-debug`（`MiniCADDwgInspect` 检查原图内容，`--bench` 离屏截图）

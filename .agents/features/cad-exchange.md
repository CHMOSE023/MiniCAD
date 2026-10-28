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
- 实体：Line / Circle / Arc / Ellipse / Point / Polyline / LwPolyline / Spline / Text / MText / Insert（含属性 ATTRIB 与属性定义 ATTDEF）/ Solid / 3DFace / Hatch（直线、圆弧、椭圆弧、样条、多段线边界）/ MultiLeader / MLine / Wipeout / Viewport / Ray / XLine / Leader / Tolerance / RasterImage（`ImageEntity`，读写）；PolyfaceMesh 与 Mesh 读成 `MeshEntity`；ACAD_TABLE 按其匿名块还原为 Insert（`Importer::ConvertTable`）；SHAPE 读成 `ShapeEntity`，形号未建模，只画占位框，导出时 DWG / DXF 写出器都不写
- 填充边界（`Importer::ConvertHatchPath`）：相邻边之间的小缺口用直线接上（AutoCAD 的间隙容差 HPGAPTOL）；缺口超过该路径范围对角线的 10% 时整条路径丢弃——这是编辑后残留的"未闭合"路径（标志 32），各段散落在图纸各处，AutoCAD 不画，硬接会画出横跨图纸的大块实心。用 `MINICAD_HATCHPATHS` 看各段是否相接
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
- 文字内容里的 `\U+XXXX`（代码页外的字符，旧版 DXF 或其他软件转出的图纸里整段中文都可能这样存）读入时保留原文、另存原样写回，显示时还原（`src/Core/TextCodes.h`）：多行文字在 `MTextEntity::StripInlineCodes`，单行文字与属性在 `TextLayoutEngine::DecodeLine`。漏掉会画出 `+8D1F+4E00…`（`\U` 被当作未知格式码丢掉）
- 镜像的块参照（负比例、法向朝 -Z，插入变换在 XY 平面的行列式为负）里的文字与 AutoCAD 一样画成镜像：`TransformDrawSink` 按变换求文字基线的真实方向（`TextRotation`，沿 X 轴镜像时 30° 画成 -30°），转发文字前后各调一次 `IDrawSink::ToggleTextMirror`，`DrawContext` 据此把字形、下划线沿基线上下翻转；两层镜像相互抵消。新写的转发型 sink 要转发它（`ClipDrawSink` 已转发），否则镜像块里的字画成正的。块定义级缓存的键已含镜像，镜像与不镜像的插入不共用
- 下划线 / 上划线 / 删除线：多行文字的 `\L` `\O` `\K`（按 `{}` 分组恢复）由 `StripInlineCodes(text, true)` 换成排版流里的开关字符，单行文字的 `%%u` `%%o` 由 `DecodeLine` 解析成同样的开关；`TextLayoutEngine::Layout` 输出 `DecorationLine`，`DrawContext` 画成直线。位置按 AutoCAD 2020 用 TXTEXP 分解量得：基线下 0.2、上 1.2、上 0.5 倍字高（TrueType 与 SHX、单行与多行相同），横向从开处的步进位置到关处。`GetPlainText` 与度量不含开关
- 含形 / 文字的复杂线型：每个元素的长度进 `LineTypeRecord::Pattern`，文字 / 形段另记在 `LineTypeRecord::Symbols`（组码 9 / 75 / 340 / 46 / 50 / 44 / 45，导出写回）。绘制时 `DrawContext::LayPattern` 在该段**末尾**落符号，文字走排版，形段按 `LineTypeSymbol::ShapeFont`（组码 340 所指 STYLE 的字体文件，如 `ltypeshp.shx`）经 `ShapeFontResolver` 取 SHX 字形，只描边不填充；符号是世界坐标的字形实例，不随缩放重铺；一段超过 `DrawContext::kMaxDashes` 个周期时与普通虚线一样画成实线、不画符号。界面里的线型预览只画占位标记（`apps/Common/src/GUI/LinetypePreview.h`）
- `$PSLTSCALE` 读写（`Scene::GetPaperSpaceLineTypeScaling`），用法见 [layout-viewport](layout-viewport.md)
- 字体查找：先 `fonts/`，再 `AppPlatform::GetSystemFontDirs()`（桌面版 `%WINDIR%\Fonts` 与当前用户安装的字体），都没有才用自带字体代替
- TrueType 字体里缺的字（`arial.ttf` 等西文字体写的汉字）改用自带 `GB2312.ttf` 的字形与步进（`FontEngine` 经 `TTFFont::SetFallback` 设好，各字体共用一份），相当于 Windows 的字体链接，AutoCAD 里也照样显示；以前画成 .notdef 方块。`%%d` 等控制码在 TrueType 里按 Unicode 码点查。`MINICAD_GLYPHS` 报的缺字已算上这份替代字体

## 测试与排查

- `MiniCADTests`（`CadExchangeTests.cpp` 等）、`MiniCADCadPreserveTests`、`MiniDWGTests`
- 打开后显示不对：用 skill `drawing-debug`（`MiniCADDwgInspect` 检查原图内容，`--bench` 离屏截图）

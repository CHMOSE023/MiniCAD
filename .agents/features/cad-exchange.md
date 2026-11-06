---
covers: src/Import/CadExchange.*, src/Import/AcisReader.*, src/Serialization/Dwg/*, src/Serialization/Dxf/*, src/Serialization/Database/*, src/Core/Ole/*, src/Document/Document.*
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

- 表：图层、线型、文字样式、多线样式、块（含外部参照块）、布局（标注样式只在组装标注时查参数，不导入成表）。图层的冻结（标志位 1）当作关闭；锁定（标志位 4）读成 `Layer::SetLocked`，导出时写回同一位——锁定图层上的对象淡显、选不中（见 [display-performance](display-performance.md) 的"锁定图层"）
- 实体：Line / Circle / Arc / Ellipse / Point / Polyline / LwPolyline / Spline / Text / MText / Insert（含属性 ATTRIB 与属性定义 ATTDEF）/ Solid / 3DFace / Hatch（直线、圆弧、椭圆弧、样条、多段线边界）/ MultiLeader / MLine / Wipeout / Viewport / Ray / XLine / Leader / Tolerance / RasterImage（`ImageEntity`，读写）；PolyfaceMesh 与 Mesh 读成 `MeshEntity`；ACAD_TABLE 按其匿名块还原为 Insert（`Importer::ConvertTable`）；SHAPE 读成 `ShapeEntity`，形号未建模，只画占位框，导出时 DWG / DXF 写出器都不写
- 填充边界（`Importer::ConvertHatchPath`）：相邻边之间的小缺口用直线接上（AutoCAD 的间隙容差 HPGAPTOL）；缺口超过该路径范围对角线的 10% 时整条路径丢弃——这是编辑后残留的"未闭合"路径（标志 32），各段散落在图纸各处，AutoCAD 不画，硬接会画出横跨图纸的大块实心。用 `MINICAD_HATCHPATHS` 看各段是否相接
- 渐变填充（GRADIENT，组码 450～470）：`Importer::ConvertHatch` 映射成 `HatchGradient`（类型、角度、位移、单色色调、颜色停靠点），导出写回同一组码。绘制走 `IDrawSink::FillTriangleColored` 逐顶点着色，柱面 / 球面 / 半球按网格细分
- 法向 -Z 的 ARC / CIRCLE / LWPOLYLINE：圆心、顶点是 OCS，导入时换到 WCS（ARC 角度取 (π−end, π−start)，凸度取反）并**不保留法向**，导出按 +Z 写，往返不再镜像；ELLIPSE 的中心与长轴本来是 WCS，只把参数区间镜像成 (−end, −start)。其他非默认法向原样保留 OCS 坐标与法向
- 标注用其匿名块还原为 Insert；绘制次序按 SortEntitiesTable
- 3DSOLID / REGION / BODY / 各类曲面（`Dwg::IsAcisEntity`）：`Importer::ConvertAcis` 经 `Dwg::DecodeAcisData` 取出 ACIS（R2013 起查 `Dwg::ReadAcisDataStore`），`Import::ReadAcis` 读成 B-rep，用 `SetCadImportSolidKernel` 设的内核 `FromBrep` 建成 `Solid3DEntity`；没有内核或建不出时只显示边线框，结束时按类型 `LOG_WARN`。格式与限制见 `docs/Solid/PLAN.md` 第 16 节。导出：`Solid3DEntity` 写成多面网格（只有边线、没有网格的跳过并提示）；原图未改动的实体在同格式同版本另存时原样写回
- 不支持的类型跳过并按类型统计，导入结束时 `LOG_WARN`（读入 `m_skipped`；导出时不能写的实体经 `Skip` 同样统计）。改映射前先用 skill `drawing-debug` 看原图里有哪些类型
- OLE2FRAME（嵌入的 Word / Excel 等）只导入显示：MiniDWG 原样保留，`Dwg::DecodeOle2Frame` 解出数据，`src/Core/Ole/OleObject` 解析 AutoCAD 头与 OLE 复合文档里的展示图（WMF / EMF / DIB），显示为 `OleFrameEntity`；图元文件经 `AppPlatform::RasterizeOle` 光栅化（桌面版 GDI，网页版只画边框）
- 其他软件的自定义实体（`SWR_*`）没有专门适配：带代理图形的经 `Proxy::Decode`（`src/Import/ProxyGraphics.h`，命名空间 `MiniCAD::Proxy`）按代理图形显示（图层 / 线型序号与文字字体经 `Proxy::ProxyGraphicsContext` 映射，格式与支持的记录见 `docs/evidence/Tch-proxy-graphics.md`），结束时按类型 `LOG_WARN` "显示为普通几何"；`SWR_BLOCK_INSERT` 的代理为空，按块参照前缀解（`Tch::DecodeBlockInsert`）。`MINICAD_PROXYEXT=SWR_*` 列出各对象用到的记录类型
  - 代理图形解码器是**厂商无关**的：只解记录流，不看对象类名。天正、斯维尔、浩辰、建筑结构设计工具（`ASDT_*`）等写进来的都是同一套记录格式，因此走同一个函数
  - **容错按「能解多少算多少」**：不认识的记录、数值不合理的记录只**跳过该条**（`ProxyGraphics::Skipped` 计数），其它已识别的记录照常输出，不再整份放弃；坐标超出 `|v| < 1e12` 的图元直接丢弃（`Discarded` 计数），防止少量脏数据把包围盒撑到 1e70 量级、导致"全部显示"时整张图缩成一个点。一条都解不出时才返回 false，看 `m_tchFailures` 打出的原因（含跳过 / 丢弃条数）
  - 支持记录类型：`1`（包围盒）`2/3`（圆）`4/5`（弧）`6/7/32`（折线 / 多边形 / 带法向）`9`（shell）`10/11/36/38`（文字）`14/16/18/19/20/22/23/24/25`（颜色 / 图层 / 线型 / 标记 / 填充 / 线宽 / 线型比例 / 厚度）`33`（内嵌 LWPOLYLINE，位字段编码，payload 前 4 字节是长度前缀）
  - **位流的位序是 MSB-first**（先取字节最高位）；用 LSB-first 解 `33` 会得到 `flags=0x1FE4` 这类垃圾值，进而算出天文数字坐标。`DD`（带默认值的 double）是**字节差分编码**（`00` 用默认值、`01` 覆盖低 4 字节、`10` 覆盖偏移 4-5 加低 4 字节、`11` 完整 8 字节），不是"沿用旧值"
  - `22` 颜色记录的 4 字节按 **[method, r, g, b]** 存放，不是小端 u32：实测句柄 `368F5` 的 `c2 00 00 00` = 真彩 RGB(0,0,0) 黑框、`c3 00 00 07` = ACI 7 白字。按 `value >> 24` 取 method 会得到 0，两条都落进 ByLayer 兜底，黑框就画成了背景色（浅色主题下是白块）盖住文字
  - `fill`（记录 `20`，`1` = 总是填充、`2` = 从不）作用在闭合图元上时，**既出实心填充、也出轮廓线**。只出填充不加轮廓，会让文字的背景框渲染成一个没有边框的纯色块
- 天正对象见 [tianzheng](tianzheng.md)，外部参照见 [xref](xref.md)，图纸空间视口见 [layout-viewport](layout-viewport.md)

## 以原图为底另存（`CadSource`）

打开时 `Document` 留下原图 `CadSource`：原始字节、场景实体 ↔ 原图句柄、导入时的签名。另存为**同格式同版本**时 `ExportCad` 重新读出原图，只改写变化的实体与表项；未修改的实体（包括天正 `TCH_*` 等自定义对象）、跳过的实体、图纸空间原样写回。格式或版本不同时重新生成整张图纸，自定义对象只剩恢复出的普通几何。

## 文字样式与线型

- TrueType 样式的文件名为空时，按扩展数据里的字体族名换算成文件名（SimSun → simsun.ttc 等）
- 与预置样式（Standard、GB2312）同名的按图纸参数更新字高、宽度因子、倾斜；图纸没有的预置样式删掉（不再写进 DWG）
- 天正文字的显示宽度因子写在 `MTextEntity::SetWidthFactor`（导出写成开头的 `\W0.7;`），不派生文字样式
- 属性 ATTDEF / ATTRIB 的文字样式按自己的组码 7（`AttribBase::GetStyleId`），导入、序列化、导出都带上
- 文字内容里的 `\U+XXXX`（代码页外的字符，旧版 DXF 或其他软件转出的图纸里整段中文都可能这样存）读入时保留原文、另存原样写回，显示时还原（`src/Core/TextCodes.h`）：多行文字在 `MTextEntity::StripInlineCodes`，单行文字与属性在 `TextLayoutEngine::DecodeLine`。漏掉会画出 `+8D1F+4E00…`（`\U` 被当作未知格式码丢掉）
- 镜像的块参照（负比例、法向朝 -Z，插入变换在 XY 平面的行列式为负）里的文字与 AutoCAD 一样画成镜像：`TransformDrawSink` 按变换求文字基线的真实方向（`TextRotation`，沿 X 轴镜像时 30° 画成 -30°），转发文字前后各调一次 `IDrawSink::ToggleTextMirror`，`DrawContext` 据此把字形、下划线沿基线上下翻转；两层镜像相互抵消。新写的转发型 sink 要转发它（`ClipDrawSink` 已转发），否则镜像块里的字画成正的。块定义级缓存的键已含镜像，镜像与不镜像的插入不共用
- 下划线 / 上划线 / 删除线：多行文字的 `\L` `\O` `\K`（按 `{}` 分组恢复）由 `StripInlineCodes(text, true)` 换成排版流里的开关字符，单行文字的 `%%u` `%%o` 由 `DecodeLine` 解析成同样的开关；`TextLayoutEngine::Layout` 输出 `DecorationLine`，`DrawContext` 画成直线。位置按 AutoCAD 2020 用 TXTEXP 分解量得：基线下 0.2、上 1.2、上 0.5 倍字高（TrueType 与 SHX、单行与多行相同），横向从开处的步进位置到关处。`GetPlainText` 与度量不含开关
- 含形 / 文字的复杂线型：每个元素的长度进 `LineTypeRecord::Pattern`，文字 / 形段另记在 `LineTypeRecord::Symbols`（组码 9 / 75 / 340 / 46 / 50 / 44 / 45，导出写回）。绘制时 `DrawContext::LayPattern` 在该段**末尾**落符号，文字走排版，形段按 `LineTypeSymbol::ShapeFont`（组码 340 所指 STYLE 的字体文件，如 `ltypeshp.shx`）经 `ShapeFontResolver` 取 SHX 字形，只描边不填充；符号是世界坐标的字形实例，不随缩放重铺；一段超过 `DrawContext::kMaxDashes` 个周期时与普通虚线一样画成实线、不画符号。界面里的线型预览只画占位标记（`apps/Common/src/GUI/LinetypePreview.h`）
- `$PSLTSCALE` 读写（`Scene::GetPaperSpaceLineTypeScaling`），用法见 [layout-viewport](layout-viewport.md)
- 字体查找：先 `fonts/`，再 `AppPlatform::GetSystemFontDirs()`（桌面版 `%WINDIR%\Fonts` 与当前用户安装的字体），都没有才用自带字体代替
- 字体族名换算（`TrueTypeFontFile`）含简体、繁体（PMingLiU、DFKai-SB、Microsoft JhengHei 等，中文名同）、日文（MS Gothic / ＭＳ ゴシック、Meiryo、Yu Gothic 等，日文名的 ＭＳ 是全角）、韩文（Gulim / 굴림、Batang、Malgun Gothic 等）
- 找不到的 TrueType 字体按名字判断文字体系（`FontEngine::ScriptOfFont`：简体 / 繁体 / 日文 / 韩文），用同体系里第一个存在的字体代替（`mingliu.ttc` 没有时用 `msjh.ttc` 或 Noto Sans CJK TC），都没有才用 `GB2312.ttf`
- 缺字回退链（`TTFFallbackChain`，相当于 Windows 的字体链接）：TrueType 字体里缺的字（`arial.ttf` 写的汉字、`GB2312.ttf` 里没有的繁体字与韩文）按该字体所属体系的字体表依次找，再找其他体系的，最后 `DroidSansFallbackFull.ttf`；每个体系一条链、各字体共用，链里的字体用到时才加载，按码点缓存走哪个字体。字体表在 `src/Text/Font/FontEngine.cpp` 的 `ScriptFonts`（系统字体、Noto Sans CJK 的 `.ttc` 按面序号取、自带的界面字体子集）。网页版下载了新字体后 `FontEngine::RescanFallbacks` 重新找，以前缺的字补上
- SHX 大字体按自己的代码页查字：`SHXFont::DetectBigFontCodePage` 先按文件名（chineset / big5 → 950，extfont / bigfont → 932，whgtxt 等 → 949），再按大字体里的编码范围判断，默认 936；码点经 `MiniDWG::Codec::FromUtf8` 转成该代码页的编码。代码页里没有的字不查大字体（不能拿 Unicode 码点当形号，否则韩文会查到同编码的汉字）。`%%d` 等控制码在 TrueType 里按 Unicode 码点查
- SHX 缺字只回退到单线字体，不混用 TrueType 填充字：主字体、大字体都没有的字（含 `%%130`～`%%133` 钢筋符号等形号）经 `SHXFallbackChain` 依次在随程序带的 `assets/fonts/minicad_sc.shx`、`minicad_tc.shx`、`minicad_jp.shx`、`minicad_kr.shx` 里找，按大字体的代码页（没有大字体时按主字体名）定先找哪个（`FontEngine::ShxFallbackChain`），都没有时画主字体的 "?"（同 AutoCAD）。线型的形文件（`FontStyle::shapesOnly`）缺形不回退。这四个字体是 Unicode 编码的 unifont，由 `tools/make_shx_cjk.py` 从 Make Me a Hanzi / KanjiVG 的笔画中线生成，韩文音节由字母部件子形（私用区 U+F000 起）拼成；简繁共用的字只在 `minicad_sc.shx` 里。`MINICAD_GLYPHS` 只报 TrueType 的缺字（`SHXCompositeFont::HasGlyph` 总是 true）

## 测试与排查

- `MiniCADTests`（`CadExchangeTests.cpp` 等）、`MiniCADCadPreserveTests`、`MiniDWGTests`；ACIS 读取在 `AcisImportTests`（`MINICAD_ACIS_DIR` 可批量读一个目录的图纸看建体统计）
- 打开后显示不对：用 skill `drawing-debug`（`MiniCADDwgInspect` 检查原图内容，`--bench` 离屏截图）

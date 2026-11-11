---
covers: src/Import/Exb/*, tests/Core/ExbContainerTests.cpp, tests/Core/ExbGeometryTests.cpp
---

# EXB 图纸导入

## 概述

EXB 是 **CAXA 电子图板的私有图纸格式**，容器为 MS-CFB（OLE2 复合文档），内部流带一套自定义分块 zlib 信封。只读导入，分三层：

- 容器层（`src/Import/Exb/ExbContainer.cpp`）：CFB、压缩信封、版本信息、完整性报告。对手头全部样例都能完整解压
- 记录层（`src/Import/Exb/ExbGeometry.cpp`）：块流按"类标识 + 长度"逐条切分（`WalkRecords` / `ParseRecord`，格式写在它们上方的注释里），一个流里可以有多个块。模型空间是 `/*BlockStg/*Blk_fffffffb`（`ParseBlockStream`），全部块定义在 `/*BlockStg/*GBlock`（`ParseBlockDefinitions`）；样式流 `/*StyleStream` → 图层表、线型表。每条记录在 `ClassifyRecord` 里按类标识分派到对应的 `Read*`：点、直线、圆、圆弧、椭圆、射线、构造线、多段线（凸度、分段宽度、闭合）、样条（求值成折线）、文字（含多行文字的换行宽度）、块参照（4×4 矩阵 + 块 ID + 属性）、填充（边界 + 文件里已展开的图案线）、实心 / 渐变填充（闭合环 + 渐变参数）、区域覆盖（边框）。标注、工程符号、表格不解语义，取它们自带的显示用嵌套记录，展开成一个 `ExbGeometryKind::Group`。找不到块头时退回 `ParseBlockStreamByMarks`（按 `FB FF FF FF` 标记切分、按字节结构猜类型的旧办法）
- 场景层（`src/Import/Exb/ExbReader.cpp`）：建图层（含关闭 / 冻结 / 锁定）、线型、块定义（`BlockTable::Create`，`*` 开头的记为匿名块）和实体；颜色、线型、线宽按实体头里的"随层 / 随块 / 自己的值"应用；块参照直接 `InsertEntity::SetBlock`，块里嵌套的块参照也一样；`Group` 展开成多个实体

各类正文的字段布局参照 ezexb（MIT，见 `THIRD_PARTY_NOTICES.md`），它按 CAXA 主版本号分支：实体头在 18 起是 37 字节，之前 33 字节；主版本号取自 `/*FileInfoStream` 的版本串（`ExbReport::Info`），传给 `ParseBlockStream` 的 `productMajor`。

**支持不完整，按"能看个大概"对待**（见下面"已知缺口"）。

刻意不进 `CadFileKind`：那一套枚举和 `CadSaveVersion` / `ExportCad` / `kSaveFormats` 绑定的是"可写出"格式，EXB 只读。`src/Import/Exb/ExbContainer.*`、`ExbGeometry.*` 只用标准库，可独立编译测试。

## 已知缺口

用 `tests/Data/Exb/sample_AC20*.exb` 实测（本机样例，不入库；与 `tests/Data/Dwg/sample_AC1027.dwg` 是同一张图，拿 `MiniCADDwgInspect` 的结果和桌面版截图当真值）。文件名里的年份是导出时选的格式，实际的 CAXA 版本号依次是 12.5、18.0、16.0、18.0。4 个版本都切出 226 条记录、31 个块定义，没有需要跳过的字节；除了 18 版里的一个 PDF 参照，每条记录都出了几何，位置、形状与 DWG 一致：

- 不显示：PDF 参照（18 版里是单独的类，更早的版本存成块）、带转角的图片（没有样例核对过）；三维实体、网格 CAXA 没有存。没显示的对象在导入后按名字各打一条警告（`ExbImportResult::Unsupported`，名字来自 `UnsupportedClassName`，没有名字的写成 `class <类标识前 8 位>`）
- 带图案的填充只画线：画文件里已展开的图案线（12.5 版没存，只有边界），不是可编辑的填充实体。实心 / 渐变填充建成 `HatchEntity`：边界曲线在 `BuildFillLoops` 里首尾接成闭合环（`ExbGeometryItem::Loops`），渐变名字是 `SOLID` 的按普通实心填充；边界里的样条、椭圆按折线近似
- 标注、工程符号、表格是一组普通图形，不是可编辑的标注 / 表格实体
- 样条是求值后的折线，不是样条实体；三维多段线丢掉 Z 按平面折线显示；区域覆盖只画边框，不遮挡
- 文字样式读字体、宽度因子、字高、倾斜角（`ParseTextStyles`）：字体是字体名时经 `FontEngine::TrueTypeFileOf` 换成文件名，西文与中文两个字体只用西文那个（两个都是 `.shx` 时中文的当大字体）；与预置样式同名的（Standard）改写预置样式。多行文字按对齐方式（`ExbGeometryItem::Align`）设附着点，单行文字一律按左下角
- 图片按图纸所在目录解析相对路径，找不到文件时只有边框
- 块的基点按 (0, 0)；块参照的裁剪边界不读；块定义里的属性定义按文字显示
- 没对上类型的类不出几何（计入 `ExbGeometryResult::UnclassifiedByClass`）。新认出一个类就在 `ClassifyRecord` 里按类标识分派、按固定布局读，不要按字节结构去猜：圆心在原点的圆、带凸度的多段线、区域覆盖的边框都这样画错过
- 布局页不读：`/*SpaceStream` 和各 `/*BlockStg/*Blk_<句柄>` 里的图纸空间内容、视口（类 `f1277d25…`，ezexb 里也没有它的布局）都跳过，导入后只有模型空间，视口记进"没显示的对象"
- 复合文档之前的老格式（文件头是 `CaxaEbForWindows`，`Exb::IsLegacyExbData`）不支持，打开时提示另存为新版；手头没有这种文件，没有照 ezexb 的 `legacy.rs` 盲写

核对显示：`.agents/skills/drawing-debug/scripts/shot.ps1 <样例>.exb -Views "x0,y0,x1,y1;…"`，同样的窗口再截一次 DWG 对比。改了 `src/Import/Exb/` 之后先确认 `MiniCADWin` 重新链接过再看图。

排查：`MINICAD_EXB_DUMP=1` 运行 `MiniCADExbGeometryTests <样例>`，打印 `ITEM|偏移|长度|类标识前 8 位|对象 ID|类型|图层|线型|颜色|点数|坐标|半径|文字`（块参照另带块 ID、比例、转角；`Group` 的子条目以 `SUB` 开头跟在后面）、按类标识统计的 `UNCLASSIFIED|类标识|条数`，以及各块定义 `BLOCK|ID|名字|记录数|未识别数` 和块里的条目；`MINICAD_EXB_STREAMDIR=<目录>` 把每个流解压后的字节写成 `<序号>.bin`，对着十六进制看某个类的正文。

## 入口与数据流

```
文件路径（拖放 / 命令行 / 文件对话框 / 网页上传）
 └─ MainFrame::OpenDrawings → 后台线程 DocumentManager::Load
     └─ Document::LoadFromFile                    src/Document/Document.cpp
         ├─ Exb::IsExbPath(path) → 读整文件为 bytes
         └─ Exb::ImportExb(bytes, scene, ...)     src/Import/Exb/ExbReader.cpp
             └─ Exb::ProbeExb(...)                src/Import/Exb/ExbContainer.cpp
                 ├─ CFB：DIFAT → FAT → 目录链（红黑树重建路径）→ miniFAT / mini stream
                 ├─ 逐流：原始 SHA-256、链长、编码识别
                 ├─ 信封：20 字节头 + 首块长度 + [4 字节长度前缀 + zlib 块] × (n−1)
                 │        ExbInflate.h 自包含 DEFLATE，逐块校验 Adler-32
                 └─ ExbReport：流表 / 版本信息 / 警告 / UnknownBytes
```

公开接口（`src/Import/Exb/ExbReader.h`）：`IsExbPath`、`IsExbData`、`ImportExb`。
`ImportExb` 返回 `true` 表示"文件被识别、容器完整"，实体数量看 `ExbImportResult::EntityCount`，没识别出来的记录数在 `ExbImportResult::SkippedRecords`。

### 压缩信封的字节规则

| 字段 | 偏移 | 值 / 含义 |
|---|---|---|
| magic | 0 | `0x3DE3DE00`（小端 `00 DE E3 3D`） |
| page_size | 4 | `262144`，每块解压出整页 |
| logical_size | 8 | 有效逻辑字节数（尾部是页填充） |
| flags | 12 | 样本为 `0` |
| first_block_size | 16 | 首块 zlib 长度 |
| 首块 | 20 | `78 9c` 起的 zlib 数据 |
| 后续块 | … | `u32 长度` + zlib 数据 |

`allocated_size = 块数 × page_size`，`logical_size ≤ allocated_size`。
非 `0x3DE3DE00` 开头的流按 raw 处理（`/*FileInfoStream`、`/*PreviewStream`、
`/*XrefInfoStream`、`/SummaryInformation`、`/DocumentSummaryInformation`）。
版本信息来自 `/*FileInfoStream` 的 `[u32 字节长度][UTF-16LE 含 NUL]` 串表，不是 CFB 头。
`/DocumentSummaryInformation` 的 `FileVersion` 属性可作交叉验证；`ProductVersion` 是**写入程序**版本。

> 🔴 **不要用 `/*AppInfoStream` 判格式版本**：已实测 CAXA 2024 把同一张图另存为
> 2013 / 2015 / 2018 / 2022 / 2024 五个格式时，该流**哈希完全相同**，一律写
> `CAXA 2D / 18.0.0.17670`。它记录的是"谁写的"，不是"写成什么格式"。

## 约束与坑

- **只读**：`Document::SaveToFile` 对 `.exb` 直接返回 false。不拦截的话
  `SaveToString` 会按"其余扩展名 → JSON 文本"把 JSON 写进 `.exb` 覆盖原图。
  各平台打开过滤器加了 `*.exb`，但**另存格式表 `kSaveFormats` 不加**。
- **没有可链接的 zlib**：`3rd/stb/stb_image.h` 里的 DEFLATE 解码器被
  `STB_IMAGE_STATIC` 编成各 translation unit 的私有符号，别的 `.cpp` 链接不到；
  `DwgDecompress.h` 是 DWG 专用 LZ77 变种，不是 DEFLATE。所以自带 `ExbInflate.h`。
  不要改成直接调 stb，也不要为了 EXB 引入 zlib 依赖。
- **后台线程安全**：导入在 `MainFrame::OpenDrawings` 的后台线程跑，模块内没有可变全局状态；
  解压上下文、CFB 缓存全是局部对象。
- **公开头不暴露 MiniDWG 类型**（`AGENTS.md`），路径一律 UTF-8 `std::string`；
  流名从 UTF-16LE 转成 UTF-8，`std::filesystem::path` 用 `std::u8string` 构造。
- **`.gitattributes` 里 `*.exb binary`**：EXB 是二进制，`text=auto` 会改掉 `0x0A` 换行。
- **完整性报告而非"返回一批成功实体"**：每个长度 / 偏移 / 数组数量都做边界检查，
  未知流、未解释字节进 `ExbReport::UnknownBytes` / `Warnings`，不静默丢弃。

## 测试

- `MiniCADExbTests`（`tests/Core/ExbContainerTests.cpp`）：容器层，输出 `HEADER|…` / `STREAM|…` / `BLOCK|…` / `SUMMARY|…`
- `MiniCADExbGeometryTests`（`tests/Core/ExbGeometryTests.cpp`）：现做几段块流，测记录切分（扩展属性的各种类型、重新同步、一个流里的多个块、记录版本 3 的扩展段与显示缓存）、块参照的矩阵、椭圆、三维多段线、图片、文字样式表、实心填充的环与渐变、不认识的类不出几何，加上去格式码、UTF-16、图层归属，这些不用样例；有样例时再跑容器 → 样式流 → 块流 → `Scene`，校验结构自洽。本机有 `tests/Data/Exb/sample_AC2013.exb` 时自动用它，并核对各类条目的个数、闭合与带凸度的多段线、块定义数、文字高度与换行宽度
- 两个程序都从第一个参数或环境变量 `MINICAD_EXB_SAMPLE` 取样例，没有时打印 `SKIP` 并通过

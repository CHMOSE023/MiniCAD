# MiniDWG 开发计划

> 状态：M0～M6 已完成（2026-10-08）：骨架、数据库模型与代码生成、DXF 读取、DXF 写入、DWG 读取、DWG 写入、补齐（未知对象原样回写、多重引线、标注关联、网格、表格、缩略图）
> 定位：一个**与平台无关**的 C++ DWG/DXF 读写库，移植自 [ACadSharp](https://github.com/DomCR/ACadSharp)（MIT），首要目标是作为 [MiniCADLib](D:/MiniCADLib) 的文件交换层。

---

## 1. 目标与非目标

### 目标
- **DWG 读写**：读 R13～R2018（AC1012～AC1032，R2007 只读）；写 R14、R2000、R2004、R2010、R2013、R2018。
- **DXF 读写**：ASCII 与二进制 DXF，读 R12～R2018，写 R2000～R2018（R14 及以前需要降级转换，暂不支持）。
- **统一数据模型**：DWG 与 DXF 的读写器都只面向 `CadDatabase`，宿主程序只需对接一次。
- **写出的文件能被 AutoCAD 直接打开**：不弹出"需要修复"提示。
- **零外部依赖**：只用 C++20 标准库；能用 Emscripten 编译（MiniCAD 有 WASM 版）。
- **可测试**：用同一张图的各版本 DWG 和 DXF 互相比对，读→写→读往返测试。

### 非目标（至少在 1.0 之前）
- 不写 R2007（AC1021）：ACadSharp 的 AC21 压缩器也没有实现。
- 不解析 ACIS 几何（3DSOLID、REGION、BODY 的 SAT 数据）、动态块参数与动作、AEC/ACM 对象，读取时跳过。
- 未建模对象只能原样写回同一格式、同一版本（见第 11 节），不做跨格式、跨版本的转换。
- 不做几何运算：库只负责数据，几何由宿主程序处理。

---

## 2. 技术约束（与 MiniCAD / MiniGUI 保持一致）

| 项 | 选择 |
|---|---|
| 语言 | C++20 |
| 构建 | CMake 3.20+、Ninja、`CMakePresets.json`（debug / release） |
| 编译器 | MSVC（`/utf-8 /W4`、`/MT` 静态运行库），Emscripten |
| 依赖 | 无 |
| 命名风格 | 命名空间 `MiniDWG`，类名和方法名 PascalCase，成员变量 `m_` 前缀，注释用中文 |
| 目录名 | 不与 MiniCADLib / MiniGUI 的 `src/` 顶层目录重名（三个库都以 `PUBLIC src` 导出头文件） |
| 文件访问 | 库不打开文件路径，只处理内存数据（`std::span<const std::uint8_t>`），由宿主读写文件 |
| 对象引用 | 对象由 `CadDatabase` 以 `unique_ptr` 持有，对象之间只存 `Handle`，不互相持有指针 |
| 日志 | 库不打印，经 `NotificationHandler` 回调交给宿主 |

**构建环境注意（沿用 MiniCAD 的经验）**：在普通 PowerShell 中直接 `cmake --build` 会找不到 MSVC 标准库头文件，需要先调用 `vcvars64.bat`，或者用 `run_tests.bat`。

---

## 3. 目录结构

```
src/
  Database/     CadDatabase、CadHeader、表、块、实体、字典与对象（纯数据）、版本、文件格式识别
  Codec/        字节序、CRC8/CRC32、代码页转换（GBK 等）、LZ77 压缩/解压、Reed-Solomon
  Dwg/Read/     位流读取、文件头、各段（Header/Classes/Handles/Objects ...）读取
  Dwg/Write/    位流写入、文件头、各段写入
  Dxf/Read/     ASCII/二进制流读取、各段读取
  Dxf/Write/    ASCII/二进制流写入、各段写入
  Build/        读取后的句柄解析：句柄引用 → 对象关系（替代 ACadSharp 的 Templates）
tools/
  dxfgen/           从 ACadSharp 源码生成数据模型、枚举、头变量与 DXF 元数据（见第 6 节）
  codepages/        生成代码页查找表（DOS/Windows/ISO 单字节代码页与 GBK、Shift-JIS、韩文、Big5）
  oracle/           ACadSharp 对照：AcadSharpDump（C#）导出属性，与 dwgconv props 逐项比较（见第 7 节）
  acadcheck/        AutoCAD 验证：用 accoreconsole 打开写出的文件并 AUDIT，可另存为 DXF 供对照（见第 8、10 节）
  dwgconv/          命令行工具：info / 转换 / 转储
tests/
  unit/         单元测试（与 MiniGUI 相同的最小测试框架）
  data/         ACadSharp 样例图纸：AC1009～AC1032 的 DWG 与 ASCII/二进制 DXF
```

与 ACadSharp 的对应：`IO/DWG/DwgStreamReaders` → `Dwg/Read`，`IO/DWG/DwgStreamWriters` → `Dwg/Write`，
`IO/DXF/DxfStreamReader` → `Dxf/Read`，`IO/DXF/DxfStreamWriter` → `Dxf/Write`，
`IO/Templates` + `CadDocumentBuilder` → `Build`，`Entities`/`Tables`/`Objects`/`Header` 的数据部分 → `Database`。

---

## 4. 里程碑

| 里程碑 | 内容 | 验收 | 状态 |
|---|---|---|---|
| M0 | 仓库骨架：CMake、测试框架、样例数据、版本与文件格式识别、`dwgconv info`；接入 MiniCADLib | Win32 与 WASM 都能编译，测试通过 | ✅ |
| M1 | `CadDatabase` 模型：头变量、表（Layer/LType/Style/DimStyle/AppId/VPort/View/UCS/BlockRecord）、常用实体、字典/XRecord/Layout；`CreateDefaults`；`dxfgen` 代码生成 | 能构造出最小合法文档 | ✅ |
| M2 | DXF 读：ASCII + 二进制，头段、表、块、实体、对象，句柄解析 | 全部 `sample_*_ascii/binary.dxf` 读入且两者结果一致；与 ACadSharp 逐属性对照一致 | ✅ |
| M3 | DXF 写：ASCII + 二进制（R2000～R2018） | 读→写→读往返一致；AutoCAD 能打开且 AUDIT 无错误 | ✅ |
| M4 | DWG 读：R2000（AC1015），再 R2004 及以后（AC1018/1024/1027/1032），最后 R14、R2007 | 与同版本 DXF 的读取结果逐对象一致 | ✅ |
| M5 | DWG 写：R2000，再 R2004/R2010/R2013/R2018 | AutoCAD 打开不提示修复、AUDIT 无错误；读回一致；AutoCAD 另存的 DXF 与原图一致 | ✅ |
| M6 | 补齐：MLeader、ACAD_TABLE、标注关联、缩略图、未知对象原样回写 | 同格式同版本写回时未建模对象不丢失；新建模类型 DWG 与 DXF 读取一致、跨格式写出 AutoCAD AUDIT 无错误 | ✅ |

---

## 5. 与 MiniCADLib 的分工

- MiniDWG 只负责文件 ↔ `CadDatabase`，不依赖 MiniCADLib 的任何东西。
- MiniCADLib 顶层 `CMakeLists.txt` 通过 `MINIDWG_DIR`（默认 `../MiniDWG`）`add_subdirectory` 引入，`PRIVATE` 链接。
- `CadDatabase` ↔ `Scene` 的映射写在 MiniCADLib 的 `src/Import/`，`Document::LoadFromFile` / `SaveAs` 按扩展名分派。

---

## 6. 代码生成（dxfgen）

数据模型不手写，由 `tools/dxfgen/dxfgen.py` 解析 ACadSharp 的 C# 源码生成：

```
python tools/dxfgen/dxfgen.py            # 默认读取 ../ACadSharp
```

| 输出（`src/Database/Generated/`） | 内容 |
|---|---|
| `Enums.g.h` | 属性用到的枚举，`[Flags]` 枚举带位运算符；`ObjectType` 改名为 `CadObjectType` |
| `Model.g.h` | 实体、表项、块、对象的数据类：带 `[DxfCodeValue]` 的属性 + `config.MANUAL_MEMBERS` 补充的成员；对象引用一律是 `Handle` |
| `CadHeader.g.h` | 249 个头变量（`[CadSystemVariable]`） |
| `DxfMeta.g.cpp` | 每个具体类的子类标记与组码表（与 ACadSharp `DxfMap` 的规则一致）、头变量表、类注册表 |
| `GenerationReport.md` | 无法自动处理的内容：告警、没有生成的公开可写属性，每次生成后检查 |

- 生成文件不要手工修改；调整在 `tools/dxfgen/config.py`：要生成的类、手工补充的成员、ObjectType 覆盖等。
- 默认值按 ACadSharp 的属性初始值、backing 字段、转发属性（如 `$DIM*` → `DimensionStyle`）推断。
- 手写部分：`CadObject`（句柄、所有者、反应器、扩展数据）、`CadTable`（符号表控制对象）、`CadDatabase`、基础类型（`XY`/`XYZ`/`Color`/`Transparency` ...）。

- 元数据中每个属性带有赋值函数 `Set` 与取值函数 `Get`（`src/Database/DxfAssign.hpp` 中按成员类型重载的
  `Assign` / `Extract`）；由其他数据计算出来的属性（`config.COMPUTED`）不生成存储，标记为 `Computed`。

### 已知缺口
- `CreateDefaults` 未创建 TableStyle、Material 的默认对象（这两类没有建模；MLeaderStyle 的 Standard 已创建）。
- 未建模、原样保留（见第 11 节）：MESH、3DSOLID/REGION/BODY、PDFUNDERLAY、TABLESTYLE、ACAD_TABLE 的单元格与样式，
  以及 MATERIAL、VISUALSTYLE、DBCOLOR、动态块参数、多重引线的注释比例上下文对象等。引用它们的句柄原样保留，
  写成不能写出它们的格式或版本时写为 0 或省略。
- 带多行文字的属性（`AttributeBase.MText`）、`Insert.SpatialFilter`、`Viewport.Scale` 尚未建模。
- R12 标注没有组码 42（测量值），读入为 0；写 DXF 时由定义点计算。

---

## 7. DXF 读取与对照验证（M2）

- DXF 是一串以组码 0 开头的记录：先把整条记录读进缓冲，再按类型处理。标注、多段线、顶点的具体类型由
  子类标记（R12 由标志位）决定，不需要边读边替换对象。
- 组码处理顺序：扩展数据 → 类型专门处理（LWPOLYLINE 顶点、HATCH 边界、样条控制点、线型分段 ……）→
  公共组码（句柄、所有者、102 组）→ 按元数据通用赋值。
- 名称引用（组码 8 图层名、2 块名 ……）在建库阶段解析为句柄；文件中没有的图层按名称新建。
- 字符串：R2007 及以后为 UTF-8，之前按 `$DWGCODEPAGE`；`^J` 等按规范解码；`\U+XXXX` 保留原文
  （它是 MTEXT 内容本身的一部分），显示时由使用方调用 `Codec::DecodeUnicodeEscapes`。

**对照验证**：用 ACadSharp 读同一文件，逐属性比较。

```
dotnet build -c Release tools/oracle/AcadSharpDump
python tools/oracle/check_samples.py [--only AC1018] [--verbose]
```

14 个样例全部一致。所有差异都核对过原始文件，确认是 ACadSharp 自身的读取偏差或引用了尚未建模的对象，
逐条记录在 `tools/oracle/known_differences.txt`。

---

## 8. DXF 写入与 AutoCAD 验证（M3）

`WriteDxf(const CadDatabase&, DxfWriteOptions)` 返回文件内容（ASCII 或二进制），不修改数据库。
段与对象的写法参照 ACadSharp 的 `DxfStreamWriter`，组码顺序有出入时以 AutoCAD 自己写出的样例为准
（AutoCAD 按顺序读 HATCH 等实体，顺序不对整张图被放弃）。

- **版本**：R2000～R2018。R14 及以前没有布局、打印样式字典、比例列表等对象，需要降级转换，
  请求这些版本时改写为 R2000 并给出提示；R12 读入的图同样写为 R2000。
- **编码**：R2007 起 UTF-8；之前按 `$DWGCODEPAGE`，代码页外的字符写成 `\U+XXXX`。^、换行、制表符写成 `^ ` `^J` `^I`。
  浮点数用最短可往返表示，ASCII 往返没有精度损失。
- **句柄**：缺少的 BLOCK / ENDBLK / SEQEND 在写出时分配新句柄，`$HANDSEED` 取所有句柄之后。
  指向不存在对象的句柄（未建模的 MATERIAL、VISUALSTYLE 等）写为 0 或省略；找不到条目的字典项不写。
- **对象段**：从根字典开始遍历字典条目、扩展字典与实体引用的对象（与 ACadSharp 相同），
  所有者链断在未建模对象上的对象（它们的扩展字典等）不写出。CLASSES 段保留读入的类定义，补上写出的对象需要的。
- **头变量**：默认只写 ACadSharp 的常用集合，`WriteAllHeaderVariables` 写全部（两种方式 AutoCAD 都能打开）。
- 刻意不往返的内容（`tests/unit/DxfWriteTests.cpp` 的比较中逐条列出）：
  SHAPE 实体（依赖 SHX，与 ACadSharp 默认一致不写）；多行属性写为单行（内嵌 MTEXT 未建模）；
  DEFPOINTS 图层总是不打印；扩展数据中不跟在 X 后的单独 Y/Z 分量（R12 偶见，R13 起 AutoCAD 拒绝）；
  标注组码 70 的类型位由类决定，多段线/顶点的 3D 标志由类决定。

**验证**：

```
run_tests.bat                                                  # 14 个样例 × ASCII/二进制 读→写→读，逐对象逐属性比较
powershell tools\acadcheck\acadcheck.ps1 -Versions "","AC1015","AC1032" [-Binary] [-AllHeader]
```

`acadcheck` 用 accoreconsole（AutoCAD 2018 / 2020）打开写出的文件并执行 AUDIT：全部样例按原版本、
跨版本（R2000～R2018）、ASCII 与二进制、全部头变量都能打开且"共发现 0 个错误"。
另用 ACadSharp 读写出的文件与 MiniDWG 的读取结果对照，属性值全部一致。

---

## 9. DWG 读取（M4）

`ReadDwg(data, DwgReadOptions)` 读 R13～R2018（AC1012～AC1032），结构对应 ACadSharp 的 `DwgReader` / `DwgObjectReader`：

| 文件（`src/Dwg/Read/`） | 内容 |
|---|---|
| `DwgFile` | 容器层：文件头、段定位；R2004 起分页与 LZ77 解压（`DwgDecompress`），R2007 另有 LZ77 变种与按块交错（"Reed-Solomon"） |
| `DwgBitReader` | 位流：BS/BL/BD/3BD、句柄、CMC/ENC 颜色、R2007 起的独立字符串流 |
| `DwgReadHeader` | 头段：头变量与引用对象的句柄 |
| `DwgReader` | CLASSES 段、句柄段（对象位置表），从头段引用的对象出发按引用遍历对象段 |
| `DwgReadObjects` / `DwgReadEntities` | 各类对象与实体；未建模的类型读完公共数据后跳过，并记入 `DwgReadReport::Skipped` |
| `DwgReadBuild` | 建库：表、块记录的实体链、多段线顶点与属性、字典条目名、线型标志、默认结构 |

DWG 中没有、需要建库时补出的内容（都以 AutoCAD 写出的同一张图的 DXF 为准）：

- **匿名块编号**不存于 DWG（AutoCAD 打开时重编），补不重复的 `*Un`。
- **视口叠放序号**（DXF 68）：按布局中的顺序编号，总视口为 1，关闭的为 0。
- **样条组码 70**：平面/直线位按定义点（拟合样条加上起止切向）判断，`Flags1 << 7` 并入高位；
  R2013 之前的 DWG 没有 `Flags1`，拟合点样条按"拟合点、节点参数化"处理。
- **颜色簿颜色**：实体只存 DBCOLOR 句柄，DBCOLOR 未建模，读取时只取其颜色填给实体。
- **带默认值字典的默认项**：AutoCAD 存为 R14 时打印样式字典没有条目，补登记为条目 `Normal`（与 AUDIT 的修复相同）。
- R2000 等旧格式中 AutoCAD 用 `ACAD` 扩展数据（RTTcAl 真彩色、RTMaterial 材质）保存旧格式表达不了的属性，
  **原样保留**（回写旧版本 DWG 时不丢），不转换为属性。

**验证**（`tests/unit/DwgReadTests.cpp`，`run_tests.bat` 运行）：

- `DwgRead_AllSamples_NoWarnings`：AC1014～AC1032 七个样例无警告，句柄、图层、所有者完整。
- `DwgRead_MatchesDxf`：同一张图的 DWG 与 ASCII DXF 逐对象逐属性比较（AC1015～AC1032）。两份样例各有一批对象
  另存时换了句柄（AC1015 约 200 个），只比较两边都有的；已知差异在 `KnownDwgDxfDiff` 中逐条列出并说明，均为
  AutoCAD 写 DXF 时的取舍：DXF 只存标注样式真彩色的近似索引色、拟合点样条写成算出的控制点、引线插入钩线顶点、
  多边形裁剪边界多一个闭合点、R2013 之前的 DXF 去掉线型分段标志的 8 位、R2018 分栏高度写 0、未启用的渐变不写等。
- `DwgRead_WriteDxf_RoundTrip`：DWG 读入 → 写 DXF（ASCII/二进制）→ 读回，逐对象比较。
- ACadSharp 对照：`check_samples.py` 同时比较 7 个 DWG 样例，差异记录在 `known_differences.txt`（`.dwg:` 前缀）。
- AutoCAD：`acadcheck.ps1 -Dwg [-Versions "","AC1015","AC1032"] [-Binary]` 把 DWG 样例转成 DXF 后打开并 AUDIT，
  全部"共发现 0 个错误"。

M4 中顺带修正的 DXF 读写问题：DXF 读取漏读 GROUP 的成员实体（340）；写 DXF 时未启用的渐变写成 450=0 加渐变组码
（AutoCAD 报"对象提前结束"放弃整张图），改为与 AutoCAD 一样不写；多边形裁剪边界补闭合点；R2013 之前不写线型分段标志的 8 位。

**尚未覆盖**：R13（AC1012）没有样例，按 ACadSharp 的分支实现但未验证；MTEXT 内嵌对象（R2018）中的文字范围、
多行属性的内嵌 MTEXT 读出后丢弃（未建模）；未建模的对象类型同 DXF（见第 6 节"已知缺口"）。

---

## 10. DWG 写入（M5）

`WriteDwg(const CadDatabase&, DwgWriteOptions)` 返回文件内容，不修改数据库。写 R2000、R2004、R2010、R2013、R2018；
R14 及以前改写为 R2000，R2007 改写为 R2010（ACadSharp 也没有 R2007 的压缩器），并给出提示。

| 文件（`src/Dwg/Write/`） | 内容 |
|---|---|
| `DwgBitWriter` | 位流：与 `DwgBitReader` 一一对应（BS/BL/BLL/BD/DD/MC/MS/H/TV/TU/CMC/ENC/OT ……） |
| `DwgCompress` | R2004 的 LZ77 压缩（移植 ACadSharp）、16 位 CRC、CRC-32、页校验和、伪随机序列 |
| `DwgWriter` | 准备（补句柄、规划写出的对象、类定义）、CLASSES、句柄段与小段（AuxHeader、Preview、SummaryInfo …） |
| `DwgWriteHeader` | 头段，变量顺序与 `DwgReadHeader` 逐项对应 |
| `DwgWriteObjects` / `DwgWriteEntities` | 对象外壳与公共数据、符号表、字典与对象、实体，字段顺序与读取逐项对应 |
| `DwgFileLayout` | 文件容器：R2000 的段定位表；R2004 起的分页、压缩、段表、页表与加密文件头 |

格式细节以 AutoCAD 2018 写出的样例为准（用 Python 解析样例逐项核对过），与 ACadSharp 不同的地方：

- **字符串**：R2000/R2004 的 TV 长度含结尾的 0（并写出 0）；R2007 起 UTF-16，字符数不含结尾的 0。
- **对象段**：所有版本都以 RL 0x0DCA 开头。R2010 起 LAYOUT、ACDBPLACEHOLDER 用固定类型码，R2000 用类号。
- **R2004 起的容器**：段表顺序、段号（从 n 递减到 1，0 号为空段）、页的排列与 AutoCAD 相同；每页补 0 到整页
  （0x7400）后压缩，压缩数据以 `11 00 00` 结束；全 0 的页不写；页头的两个校验和、系统页校验和、文件头 CRC-32
  都按样例验证过算法。维护版本取 AutoCAD 另存各版本时写的值（R2000 15、R2004 104、R2010 226、R2013 125、R2018 0）。
- **R2000 的视口实体头表（VX）**：当前布局中的每个视口一项，视口与头段都引用它；没有时 AutoCAD 把这些视口当作关闭。
- **样条**：R2013 起场景字段总写 1，节点参数化（模型新增 `Spline::KnotParametrization`）按读入的值写，
  控制点样条为 15；不保留时 AutoCAD 会按另一种参数化重算拟合点样条。
- **补全**：缺少的 BLOCK/ENDBLK/SEQEND 分配新句柄；指向不写出对象的句柄写 0；行距样式 0（AutoCAD 核查报错）写为 1；
  DEFPOINTS 图层总不打印；R12 标注的测量值由定义点计算、有匿名块时置块参照位（与 DXF 写出一致）。

**不写出的内容**：SHAPE（形号未建模）；未建模的对象（同第 6 节）及只引用它们的字典条目；多行属性写为单行；
缩略图（Preview 段为空）、AppInfoHistory、R2018 的 AcDsPrototype 数据存储、R2000 文件末尾的第二文件头；
写成旧版本时旧版本存不了的属性（真彩色取近似索引色、透明度、R2007 起的光照与块缩放设置、R2018 的分栏 ……，
`DwgWriteTests.cpp` 的 `KnownDwgWriteDiff` 中逐条列出）。

**验证**：

```
run_tests.bat                                                   # 位流、压缩、CRC；各样例 DWG/DXF → DWG → 读回逐对象比较；跨版本
powershell tools\acadcheck\acadcheck.ps1 -OutDwg [-Dwg] -Versions "","AC1015","AC1018","AC1024","AC1027","AC1032"
powershell tools\acadcheck\acadcheck.ps1 -OutDwg -Dwg -DxfOut   # 另由 AutoCAD 另存为同版本 DXF（*.acad.dxf）
dwgconv compare <AutoCAD 另存的原图.dxf> <AutoCAD 另存的写出文件.dxf> -header -below <原图 HANDSEED>
python tools\oracle\check_samples.py --dir %TEMP%\minidwg-acadcheck   # ACadSharp 读写出的文件，与 MiniDWG 对照
```

- AutoCAD 2020 与 2018：全部 DXF 与 DWG 样例写成 R2000～R2018 各版本，都能打开且"共发现 0 个错误"。
- **AutoCAD 的解读**：原样例与写出的文件分别由 AutoCAD 另存为 DXF 后比较，已建模的数据全部一致；剩下的差异
  都来自未写出的未建模对象（字典条目、扩展字典、ACAD_TABLE 的匿名块被 AutoCAD 清理、动态块 GUID 重新生成）
  与 AutoCAD 另存时新建的对象。头段中只有 $LASTSAVEDBY（SummaryInfo 未读取）、$EXTMIN（未写出的三维实体）、编辑时间不同。
- ACadSharp 读写出的文件：与 MiniDWG 的读取结果全部一致（已知差异见 `known_differences.txt`）。

M5 对照中发现并修正的读取问题：R2000 的 Template 段（MEASUREMENT）没有读；整段全 0 时没有页的段（R2004 的
Template）读成空；头段 DIMLTYPE/DIMLTEX1/DIMLTEX2 没有换成名称；CLASSES 段按字节判断结尾，最后一个类之后有填充位时
多读；R2007 起 Template 段的说明长度按字符计；R14 标注样式的 DIMTIH/DIMTOH/DIMSE1/DIMSE2 错位（照搬了 ACadSharp）；
R14 的 MTEXT、标注没有行距与对齐字段，读入后取 AutoCAD 打开 R14 时的值；R14 文件中 AutoCAD 保存的往返数据
（`ACAD_XREC_ROUNDTRIP` 中的 EXTNAMES 原名称、ACADR14ROUNDTRIP 标注样式变量、图层线宽与打印标志）与 AutoCAD 一样
应用并删除。

**尚未覆盖**：R14 视口的视图参数存在 `ACAD` 扩展数据（MVIEW）中，读取时没有转换，写成 R2000 后这些视口的视图
中心、高度等为 0；R2000 等旧格式中 AutoCAD 保存的其他往返数据（真彩色、着色打印等）原样保留、不转换为属性，
写成新版本时 AutoCAD 不再使用它们。

---

## 11. 补齐（M6）

### 未知对象原样回写

没有建模的对象不再丢弃，读入为 `UnknownEntity` / `UnknownObject`（`src/Database/UnknownObjects.h`）：

- **公共数据照常读入模型**：句柄、所有者、反应器、扩展字典、扩展数据，实体的图层、颜色、线型等；写出时重新编码。
- **其余数据原样保存**：DWG 按位保存数据流、R2007 起的字符串流、句柄流（`RawDwgData`），并解析出句柄流中的引用，
  读取时据此读入被引用的对象，写出时据此带上它们；DXF 保存公共组码之后的全部组码（`RawDxfData`）。
  未建模实体的代理图形（DWG）也保存并写回。
- **只能写回同一格式、同一版本**（数据中的句柄沿用原值，格式随版本变化）；否则不写出并提示，引用它们的句柄写 0。
- R2013 起三维实体等的 ACIS 数据在数据存储段 AcDb:AcDsPrototype_1b 中：整段原样保留，写回同一版本时写出，
  对象的"数据存储中有数据"位同时保留。
- **没有被引用的对象也读入**：R2010 起 ACAD_TABLE 的 TABLECONTENT 只有所有者指向表格，按引用遍历读不到；
  遍历结束后再读句柄表中剩下的对象，写出时所有者会写出的对象一并写出（DWG 与 DXF）。
- XRECORD 中 350～369 组码表示拥有关系，被拥有的对象随之写出（以前只按字典遍历，会丢 TABLECONTENT 等）。
- 顺带补上：颜色簿颜色（DBCOLOR 原样保留后，实体的 ENC 写 0x4000 + 句柄、DXF 写 430 名称）、材质 ByBlock
  （DWG 中只是标志，DXF 中是 ByBlock 材质对象的句柄）、扩展数据中的句柄按"是否写出"判断。

### 新建模的类型

| 类型 | DXF | DWG | 说明 |
|---|---|---|---|
| 多面网格、多边形网格（POLYLINE 及其 VERTEX） | ✓ | ✓ | `PolyfaceMesh` 的面记录在 `Faces` 中，顶点之后写出 |
| 标注关联 DIMASSOC | ✓ | ✓ | DWG 格式由样例 DWG 与同一张图的 DXF 对照得出（ACadSharp 只读了一部分），样例中没出现过的结构原样保留 |
| 多重引线 MULTILEADER、样式 MLEADERSTYLE | ✓ | ✓ | DXF 按 AutoCAD 写的样例实现（含嵌套的 CONTEXT_DATA / LEADER / LEADER_LINE、断点、块属性、版本差别）；DWG 按 ODA 规范，样式中 295/296 两位的顺序与 ACadSharp 相反（按 DXF 核对）；R2007 及以前带箭头列表的原样保留 |
| 表格 ACAD_TABLE | 部分 | 部分 | `TableEntity`：单元格与样式原样保留，只解析块参照部分（匿名块 *T、插入点、比例、旋转、法向）供宿主显示；写成别的格式或版本时写为引用该匿名块的块参照（外观不变，失去表格编辑能力） |
| 缩略图 | ✓ | ✓ | `CadDatabase::Preview`：DWG 的 AcDb:Preview（BMP / WMF / PNG，地址规则按样例：R2000 为文件中的位置，R2004 起为段内位置 + 0x1C0），DXF 的 THUMBNAILIMAGE（只有 BMP） |

ACAD_TABLE 没有完整建模的原因：DWG 有两套格式（R2007 及以前与 R2010 起内嵌 TABLECONTENT），ACadSharp 的旧格式读取
丢弃了全部替代样式，样例中也只有两张表格，无法可靠验证完整的单元格结构。

`CreateDefaults` 新建 MLEADERSTYLE Standard（取值与 AutoCAD 新建图纸一致）。

### 验证

- 单元测试（`run_tests.bat`）：同版本 DWG → DWG、DXF → DXF 往返时原样保留对象的数据逐位、逐组码相同；
  DWG 与同一张图的 DXF 读取逐对象一致（新增多重引线上下文、引线、点引用、表格块参照的比较）；
  跨格式、跨版本写出读回一致（旧版本存不了的属性见 `KnownDwgWriteDiff`）；缩略图往返。
- AutoCAD 2020：全部样例写成 R2000～R2018 各版本的 DWG 与 DXF（ASCII、二进制）都能打开、"共发现 0 个错误"。
  同版本写回时 AutoCAD 识别的实体由 144 增加到 167（三维实体、网格、多重引线、表格、PDF 参考底图等写回来了）；
  原图与写出图分别由 AutoCAD 另存 DXF 后比较，剩余差异只有多行属性（写为单行）、动态块 GUID、AutoCAD 另存时新建的对象；
  DXF 样例写成 DWG 后，多重引线、标注关联在 AutoCAD 中读到的内容与原 DWG 完全一致（表格写为块参照）。
- ACadSharp 对照：全部样例通过（ACadSharp 自身的多重引线颜色、样式位序问题登记在 `known_differences.txt`）。


# MiniDWG

与平台无关的 C++20 DWG/DXF 读写库，移植自 [ACadSharp](https://github.com/DomCR/ACadSharp)（MIT）。
它是 [MiniCADLib](../MiniCADLib) 的文件交换层，但不依赖 MiniCADLib，可以单独使用。

- **DWG**：读 R13～R2018（AC1012～AC1032，含 R2007）；写 R2000、R2004、R2010、R2013、R2018。
- **DXF**：ASCII 与二进制，读 R12～R2018，写 R2000～R2018。
- **统一数据模型**：DWG 与 DXF 的读写器都只面向 `CadDatabase`，宿主程序只需对接一次。
- **写出的文件 AutoCAD 能直接打开**：全部样例写成各版本的 DWG 与 DXF，AutoCAD 2020 AUDIT 均为"共发现 0 个错误"。
- **零外部依赖**：只用标准库，可用 MSVC 与 Emscripten 编译；库不打开文件、不打印日志。

开发计划、各里程碑的实现细节与验证记录见 [PLAN.md](PLAN.md)。

---

## 构建

需要 CMake 3.20+、Ninja、支持 C++20 的编译器（MSVC 或 Emscripten）。

```bat
run_tests.bat            rem 配置、编译 debug，并运行全部单元测试
run_tests.bat release
```

也可以直接用 `CMakePresets.json` 中的预设（`debug`、`release`，输出到 `out/<预设>/`）：

```bat
cmake --preset debug
cmake --build --preset debug
```

> 在普通 PowerShell / cmd 中直接 `cmake --build` 会报找不到 `cmath` 等标准库头文件：MSVC 的 `INCLUDE`/`LIB`
> 来自开发者环境变量。先调用 `vcvars64.bat`，或者用 `run_tests.bat`（它会自动调用；VS 安装位置不同时修改其中的路径）。

| CMake 选项 | 默认 | 说明 |
|---|---|---|
| `MINIDWG_BUILD_TESTS` | `ON` | 单元测试 `MiniDWGTests`（Emscripten 下不编译） |
| `MINIDWG_BUILD_TOOLS` | `ON` | 命令行工具 `dwgconv`（Emscripten 下不编译） |

MSVC 下使用静态运行库（`/MT`、`/MTd`），与 MiniCADLib 一致。

### 在其他项目中使用

```cmake
add_subdirectory(${MINIDWG_DIR} MiniDWG)        # 例如 MINIDWG_DIR = ../MiniDWG
target_link_libraries(MyApp PRIVATE MiniDWG)
```

头文件以 `src/` 为根引用，如 `#include "Dwg/Read/DwgReader.h"`。全部符号在命名空间 `MiniDWG` 中。

---

## 使用

### 读取

库只处理内存数据，文件由调用方读写。先用文件开头的字节识别格式，再选读取器：

```cpp
#include "Database/CadFileFormat.h"
#include "Dwg/Read/DwgReader.h"
#include "Dxf/Read/DxfReader.h"

std::unique_ptr<MiniDWG::CadDatabase> Load(std::span<const std::uint8_t> data)
{
    using namespace MiniDWG;

    auto notify = [](NotificationType type, std::string_view message) {
        // 接到宿主自己的日志系统
    };

    const CadFileInfo info = DetectFileFormat(data);
    if (info.format == CadFileFormat::Dwg)
    {
        DwgReadOptions options;
        options.Notify = notify;
        return ReadDwg(data, options);          // 不是 DWG 或版本不支持时返回 nullptr
    }
    if (info.format == CadFileFormat::DxfAscii || info.format == CadFileFormat::DxfBinary)
    {
        DxfReadOptions options;
        options.Notify = notify;
        return ReadDxf(data, options);
    }
    return nullptr;
}
```

读取选项：

- `CreateDefaults`（默认 `true`）：读完后补齐缺失的默认结构（符号表、根字典、模型空间与图纸空间等），
  保证得到的数据库完整。
- `Report`：不为空时填写读取报告，包括跳过的类型、原样保留的类型，以及文件中已有句柄的上限。

### 访问数据

对象由 `CadDatabase` 持有、按句柄索引，对象之间的引用（所有者、图层、块 ……）一律存 `Handle`，
用 `Find` / `FindAs<T>` 取对象：

```cpp
using namespace MiniDWG;

const BlockRecord* model = db->ModelSpace();
for (Handle h : model->Entities)
{
    const Entity* entity = db->FindAs<Entity>(h);
    const Layer* layer = db->FindAs<Layer>(entity->LayerHandle);

    if (auto* line = dynamic_cast<const Line*>(entity))
    {
        // line->StartPoint、line->EndPoint、entity->Color、layer->Name ...
    }
    else if (auto* insert = dynamic_cast<const Insert*>(entity))
    {
        const BlockRecord* block = db->FindAs<BlockRecord>(insert->BlockHandle);
        // block->Entities 中是块内实体
    }
}

const Layer* layer0 = db->FindTableEntry<Layer>(db->Layers(), "0");   // 表项按名称查找，不区分大小写
```

- 数据类（`Line`、`Layer`、`MText`、`Hatch`、`MultiLeader` ……）在 `src/Database/Generated/Model.g.h` 中，
  成员名与 ACadSharp 的属性一致，注释标出对应的 DXF 组码。
- 头变量在 `db->Header`（`CadHeader`，249 个），自定义类在 `db->Classes`，缩略图在 `db->Preview`。
- 颜色 `Color` 为 ACI 索引色（含 ByLayer、ByBlock）或真彩色；角度一律为弧度。

### 新建与编辑

```cpp
#include "Database/CadDatabase.h"

using namespace MiniDWG;

CadDatabase db;
db.CreateDefaults();     // 新图纸的默认内容：符号表、图层 0、线型、Standard 样式、模型空间与图纸空间 ...

auto line = std::make_unique<Line>();
line->StartPoint = {0, 0, 0};
line->EndPoint = {100, 50, 0};
db.AddEntity(db.ModelSpace(), std::move(line));   // 图层、线型为空时取 "0" 与 ByLayer

auto layer = std::make_unique<Layer>();
layer->Name = "墙";
layer->Color = Color(std::int16_t(1));
db.AddTableEntry(db.Layers(), std::move(layer));
```

`AddEntity`、`AddTableEntry`、`AddDictionaryEntry`、`CreateBlockRecord` 会分配句柄并维护所有者与集合；
直接用 `Add` / `RemoveObject` 时由调用方维护引用关系。

### 写出

```cpp
#include "Dwg/Write/DwgWriter.h"
#include "Dxf/Write/DxfWriter.h"

using namespace MiniDWG;

DwgWriteOptions dwg;
dwg.Version = CadVersion::AC1032;                 // Unknown 表示沿用数据库版本
std::vector<std::uint8_t> dwgBytes = WriteDwg(db, dwg);

DxfWriteOptions dxf;
dxf.Binary = true;                                // 默认 ASCII
dxf.Version = CadVersion::AC1018;
std::vector<std::uint8_t> dxfBytes = WriteDxf(db, dxf);
```

写出不修改数据库：缺少的 BLOCK / ENDBLK / SEQEND 在写出时分配新句柄，指向不存在对象的句柄写为 0。

请求库写不了的版本时，库会改用相近的版本并经 `Notify` 提示：
- R14 及以前：没有布局、打印样式等对象，需要降级转换，尚未实现，改写为 R2000；
- R2007 的 DWG：压缩格式没有实现（ACadSharp 也没有），改写为 R2010。

### 未建模的对象

没有建模的实体与对象（三维实体、网格 MESH、PDF 参考底图、材质、视觉样式、表格样式、动态块参数等）
不会丢失：读入为 `UnknownEntity` / `UnknownObject`（`src/Database/UnknownObjects.h`）。
其中句柄、所有者、扩展数据、图层与颜色等公共数据照常可读可改，其余数据原样保存。

- 写回**同一格式、同一版本**时原样写出；
- 写成别的格式或版本时不写出，并经 `Notify` 提示。

表格 `ACAD_TABLE` 读入为 `TableEntity`，额外解析出块参照部分（匿名块 `*T`、插入点、比例、旋转），
宿主可以据此显示表格。写成别的格式或版本时，表格写为引用该匿名块的块参照：外观不变，但不能再按表格编辑。

---

## 支持的内容

| 类别 | 内容 |
|---|---|
| 符号表 | 块记录、图层、线型、文字样式、标注样式、APPID、视口、视图、UCS |
| 实体 | 直线、圆、圆弧、椭圆、点、射线、构造线、多段线（LWPOLYLINE、二维、三维、多面网格、多边形网格）、样条、单行文字、多行文字、属性与属性定义、块参照（含阵列）、填充、实体填充 SOLID、三维面、各类标注、引线、多重引线、公差、多线、形、视口、光栅图像、区域覆盖 |
| 对象 | 字典（含带默认值的字典）、字典变量、XRECORD、组、布局与打印设置、多线样式、多重引线样式、比例、图像定义及其反应器、光栅变量、绘制次序表、标注关联、占位对象 |
| 其他 | 头变量、CLASSES 段、扩展数据、扩展字典、反应器、缩略图（BMP / WMF / PNG）、代码页（R2007 之前按 `$DWGCODEPAGE`，含 GBK、Shift-JIS、Big5 等） |
| 原样保留 | 其余对象，以及 R2013 起存放 ACIS 数据的数据存储段（见上一节） |

已知限制：

- 不解析 ACIS 几何（3DSOLID、REGION、BODY）、动态块参数与动作、AEC/ACM 对象。
- `CreateDefaults` 不创建表格样式与材质的默认对象（两者都没有建模）。
- 带多行文字的属性写为单行；`Insert.SpatialFilter`、`Viewport.Scale` 没有建模。
- 库不做几何运算，只负责数据。

---

## 目录结构

```
src/
  Database/     CadDatabase、头变量、表、实体与对象的数据类、颜色、版本、文件格式识别
    Generated/  由 tools/dxfgen 生成的数据模型、枚举、头变量与 DXF 元数据（不要手工修改）
  Codec/        代码页转换及其查找表
  Dwg/Read/     DWG 读取：容器与解压、位流、头段、对象、建库
  Dwg/Write/    DWG 写入：位流、压缩、各段与文件布局
  Dxf/Read/     DXF 读取：ASCII/二进制流、各段、建库
  Dxf/Write/    DXF 写入
tests/
  unit/         单元测试
  data/         ACadSharp 样例图纸：AC1009～AC1032 的 DWG 与 ASCII/二进制 DXF
tools/
  dwgconv/      命令行工具
  dxfgen/       从 ACadSharp 源码生成数据模型
  codepages/    生成代码页查找表
  oracle/       与 ACadSharp 逐属性对照
  acadcheck/    用 AutoCAD（accoreconsole）打开写出的文件并 AUDIT
```

## 工具

**dwgconv**（编译后在 `out/<预设>/tools/dwgconv/`）：

```bat
dwgconv info <文件>...                         查看格式与版本
dwgconv dump [-v] <文件>...                    读入 DXF 或 DWG 并统计对象
dwgconv props <文件>                           逐对象导出属性（与 ACadSharp 对照用）
dwgconv convert <输入> <输出> [-binary] [-version ACxxxx] [-allheader]
                                               转换格式与版本，按输出扩展名写 DWG 或 DXF
dwgconv compare <基准> <比较> [-all]           比较两个文件的读取结果
```

**dxfgen**：数据模型不手写，由 `python tools/dxfgen/dxfgen.py` 解析 ACadSharp 的 C# 源码（默认 `../ACadSharp`）
生成到 `src/Database/Generated/`。要调整生成内容，修改 `tools/dxfgen/config.py`，生成后检查 `GenerationReport.md`。

## 验证

- **单元测试**（`run_tests.bat`）：同一张图的 DWG 与 DXF 读取结果逐对象一致；读→写→读往返一致；
  跨格式、跨版本写出读回一致；原样保留的对象往返逐位、逐组码相同。
- **ACadSharp 对照**（`tools/oracle`）：用 ACadSharp 读同一文件逐属性比较，全部样例一致；
  已核实属于 ACadSharp 自身偏差的差异记录在 `tools/oracle/known_differences.txt`。
- **AutoCAD**（`tools/acadcheck/acadcheck.ps1`，需要安装 AutoCAD）：全部样例写成 R2000～R2018 的 DWG 与 DXF
  （ASCII、二进制），AutoCAD 打开后 AUDIT 均无错误；另由 AutoCAD 另存为 DXF，与原图对照。

## 许可证

MiniDWG 以 [MIT 许可证](LICENSE) 发布。DWG/DXF 读写实现移植自 ACadSharp，`tests/data/` 中的样例图纸也取自该项目，
二者均为 MIT 许可，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

# 三维实体建模开发计划

> 状态：M0～M5 已完成（2026-10-18）：SMLib 静态库构建（C++20）；`ISolidKernel` 接口与 SMLib 适配层；`Solid3DEntity`（俯视线框、变换、拾取、捕捉、夹点、撤销、读写）；桌面版建模命令（长方体、圆柱、球、拉伸、旋转、扫掠、实体并 / 差 / 交、边圆角 / 倒角）；截面、平面投影出二维图，DWG / DXF 导出为多面网格
> 定位：在 MiniCAD 里加入**三维实体**（布尔、圆角、截面出图），几何内核用 NVIDIA usd-brep 的 SMLib（Apache-2.0），经 MiniCAD 自己的接口 `ISolidKernel` 隔离，以后可以换成或并用其他内核（如 OCCT）。

---

## 1. 目标与非目标

### 目标
- **三维实体实体类型**：长方体、圆柱、球等基本体，拉伸、旋转、扫掠生成的实体；并、差、交；圆角、倒角
- **在现有二维视图里可用**：先以俯视线框显示（边线 + 轮廓线），不改渲染器就能用
- **三维出二维图**：截面、平面投影生成直线 / 圆弧 / 样条，再用现有的标注、图层、DWG 导出出图——这是与 MiniCAD 已有能力结合最紧的部分
- **编辑走命令、可撤销**：与现有实体一致
- **内核可替换**：上层只依赖 `ISolidKernel`

### 非目标（至少在第一版之前）
- **不与 AutoCAD 互通三维实体**：DWG 的 3DSOLID 存的是 ACIS（SAT/SAB）数据，SMLib 读写不了。DWG 里已有的三维实体仍由 MiniDWG 原样保留，不转成可编辑的实体；MiniCAD 建的实体导出 DWG 时只能退化为网格 / 3DFACE
- 不做 STEP / IGES 交换（SMLib 没有，需要时考虑 OCCT）
- 不做参数化（特征树、约束），见第 9 节，作为独立模块另做
- 网页版暂不带（`MINICAD_WITH_SOLID` 在 Emscripten 下默认关），体积与异常支持评估后再定
- 不改内核算法：SMLib 只做构建适配（见 `3rd/SMLib/README.md`）

---

## 2. 技术约束

| 项 | 选择 |
|---|---|
| 内核 | SMLib + SM_API（usd-brep 1.0.0，提交 c9c5979），源码在 `3rd/SMLib`，目标 `SMLib` / `SM_API`，定义在 `cmake/SMLib.cmake` |
| 构建 | 静态库，`SM_STATIC`，C++20，`/MT`；源文件按目录通配 |
| 线程 | 静态库走进程全局的线程局部存储路径，**内核只能在一个线程里使用**。桌面版的耗时运算若放到后台，只能用单个工作线程并串行化 |
| 上下文 | 使用前调用一次 `SmApiCreateContext()` |
| 头文件隔离 | SMLib 头文件带 `ULONG`、`TRUE` 等宏，只在适配层的 `.cpp` 里包含；MiniCAD 的公开头文件里不出现任何 `Sm*` 类型（与 MiniDWG `PRIVATE` 链接同理） |
| 内存 | SM_API 有大量"由调用方释放"的返回值，所有权约定写在各函数注释里；适配层负责全部释放，接口外只交出 MiniCAD 的数据或句柄 |

**为什么保留 SM_API**：它只有约 1 万行，但做了参数检查、上下文管理、流形 / 非流形算法选择，并写明了所有权约定；直接用 SMLib 的内部类要自己补这些，且没有文档。SM_API 没有的功能（如 `SmAttribute`、拓扑遍历细节）在适配层里直接用 SMLib 的类。

---

## 3. 分层

```
MiniCAD（实体、命令、工具、视口）
      │ 只认识 ISolidKernel 和 MiniCAD 自己的类型（Point3、Mat4、网格、句柄）
      ▼
ISolidKernel（纯虚接口，MiniCAD 定义）
      │
      ▼
SMLibKernel（实现接口；唯一包含 Sm*.h 的地方）
      │
      ▼
SM_API → SMLib
```

- 桌面版：适配层可编成 `MiniSolid.dll`（内部静态链接 SMLib，只导出工厂函数），以便没有三维功能时也能运行；也可以先静态链接，后续再拆
- 网页版：只能静态链接

---

## 4. 接口（M1 已实现）

| 文件 | 内容 |
|---|---|
| `src/Solid/ISolidKernel.h` | 接口，属于核心库，只用 MiniCAD 的类型（`Point3`、`Vec3`、`Mat4`、`AABB`） |
| `src/Solid/SMLib/SMLibKernel.h/.cpp` | SMLib 实现与工厂 `CreateSMLibKernel()`；目标 `MiniCADSolid`（`cmake/MiniCAD.cmake`），私有链接 `SM_API`。单独成库是因为核心库定义了 `UNICODE`，SMLib 按它切换宽字符文件名 |
| `tests/Solid/SolidKernelTests.cpp` | 接口测试（69 项），只经接口调用，换内核实现时应原样通过 |

- `BodyPtr = shared_ptr<const IBody>`：实体不可变，所有运算返回新实体、输入不变，可在撤销栈、剪贴板间共享
- 创建：`Box`、`Cylinder`、`Sphere`、`Extrude`（轮廓沿 ±Z）、`Revolve`、`Sweep`（M5，轮廓沿 `SweepPath` 扫掠）
- 轮廓 `Profile`：z = 常数平面上的闭合多段线，顶点带凸度（tan(圆心角 / 4)，**正值为逆时针，与 DXF 相同、与 MiniCAD 的 `Polyline` 相反**；顺 / 逆时针绕行都可以），或整圆
- 扫掠路径 `SweepPath`：同一水平面上首尾相接的直线 / 圆弧（顶点带凸度，约定同 `Profile`），可闭合
- 修改：`Boolean`（并 / 差 / 交）、`Fillet` / `Chamfer`（M5，按 `Edges()` 给出的边编号）、`Transform`（任意 `Mat4`，含镜像、非均匀缩放）
- 显示与查询：`Tessellate`（顶点按面拆分、面内平滑法线、三角形逆时针朝外）、`Edges`（每条边一条折线 + 编号）、`HiddenLines`（隐藏线消除：可见 / 被挡住的边与轮廓线）、`Section`、`Volume`、`Bounds`
- 读写：`Save` / `Load`，数据 = 16 字节头（`SMLB`、版本、长度、CRC32）+ SMLib 二进制格式；`Load` 先校验，损坏的数据不交给 SMLib（它的读取器遇到乱码会崩溃）
- 失败：返回空指针 / 空结果，原因由 `LastError()` 给出

### 实现要点与已知限制

- **视线方向**：SMLib 的 `SmHCR` 用"从物体指向眼睛"的向量，适配层取反后传入；接口里 `viewDir` 是视线方向（俯视为 (0, 0, −1)）
- **隐藏线**：`SmApiCreateSilhouetteCurves` 实际是 `SmHCR` 隐藏线消除且只给可见线、显示曲面接缝，适配层直接用 `SmHCR`，不显示接缝和光滑相接的边。**已知限制**：轮廓线与曲面接缝重合时（如从正面看球，接缝在 xz 平面上），重合的那一段不输出
- **法线**：`SmPolyVertex` 自带的面 / 法线表实测对不上（长方体顶点拿到相邻面的法线），改为按面对周围三角形法线面积加权平均
- **边编号**：`SmApiGetEdges` 的顺序；深拷贝保持顺序，所以 `Fillet` 在副本上按同一编号取边。编号只在同一实体内有效，运算后的新实体重新编号（稳定的拓扑命名见第 9 节）
- **读写经临时文件**：SM_API 只有文件接口；SMLib 内部有内存流 `SmDatabaseIOStream`，但直接用要重复其版本头逻辑，留待以后。临时文件名用窄字符（ANSI）路径，临时目录含 ANSI 无法表示的字符时会失败
- **旋转**：轴在轮廓平面内时先检查轮廓是否整个在轴的一侧（可以贴着轴），穿过轴的返回失败——`SmApiCreateRotationalSweep` 对这种轮廓也会生成自相交的实体
- **扫掠**：`SmApiCreateSweepAlongPlanarPath`，轮廓的包围盒中心移到路径起点、转到与起点切线垂直（绕切线的转角由 SMLib 决定），折角处斜接，不闭合的路径两端封口。圆弧段按凸度方向生成（顺时针圆弧反转参数），保证路径首尾相接
- **倒角**：`SmApiFilletEdges` 的平面截面（`SM_FSG_LINEAR`），距离即滚球半径：两面垂直时沿两个面各退回该距离（等距倒角，暂无两个不同距离）
- **单线程**：见第 2 节

---

## 5. 实体 `Solid3DEntity`（M2 已实现）

`src/Core/Entity/Solid3DEntity.hpp`（纯头文件，核心库）。命名：MiniCAD 已有 `SolidEntity`（二维填充，DXF 的 SOLID），三维实体用 `Solid3DEntity`（对应 DXF 3DSOLID）。

- **几何 = 共享数据 + 变换矩阵**：内核数据（`Save` 的字节）与局部坐标下的线框在 `Create(id, kernel, body)` 时一次求出、之后不变，克隆之间共享（`shared_ptr`）；变换矩阵 `Mat4`（局部 → 世界，行向量约定）每个实体一份
- **核心库不依赖内核实现**：实体只用 `ISolidKernel` 接口；需要内核实体时 `GetBody(kernel)` 从字节懒加载、`GetWorldBody(kernel)` 再施加变换。没有内核（网页版）也能显示、移动、原样保存
- **显示**（第一期）：世界坐标线框 = 实体所有边的折线，经 `IDrawSink::DrawLine` 输出。暂不画曲面轮廓线（球只显示接缝和赤道）
- **包围盒**：创建时由内核求出局部坐标下的精确包围盒（`GetLocalBounds`，与字节一起保存），变换后取 8 个角点；只用线框会偏小（球的线框只覆盖半个球），影响缩放到全图、空间索引和夹点位置
- **变换只改矩阵**：`TranslateEntityInPlace` / `Rotate` / `Mirror` / `Scale` 各加一个分支（复制、阵列、缩放工具经它们生效）；移动 / 旋转 / 镜像命令的 `MoveEntityEntry` 与夹点的 `DragEntityEntry` 增加 `Kind::Transform`，保存前后矩阵用于撤销
- **拾取**：走 `Picking` 的通用回退（按 `Draw()` 输出的线段命中），无需特化
- **捕捉**（`Document/FeaturePoints.cpp`、`SnapEngine::TryNearest`）：端点 = 不闭合的边的两端 + 圆形边的圆心（同圆 / 圆弧的约定）；中点 = 不闭合的边按长度的中点；圆心；最近点 = 线框各段上的最近点
- **夹点**（`Editor/Grip/Solid3DGripHandler.h`）：包围盒中心一个，拖动即整体移动
- **网格**（M4，`GetLocalMesh`）：创建时由 `Tessellate` 离散，合并同位置顶点；每个三角形记下哪些边在同一个面内部（两侧在端点处的法线相同），用于导出
- **读写**（`EntityIO`）：`data`（内核字节，base64，`Ser::Bytes`）、`transform`（16 个数）、`bounds`（局部包围盒）、`mesh`（网格，打包成字节，base64）、`wire`（局部线框，没有内核时用来显示）；`.mcad` 与 JSON 都适用
- **DWG / DXF**（M4，`CadExchange` 的 `ConvertSolid3D`）：写成多面网格（POLYLINE，AcDbPolyFaceMesh），施加变换；面内部的边写成不可见（面记录的索引取负），AutoCAD 里看到的线框与实体的边一致。多面网格的顶点数、面数是 16 位，超过 32767 的跳过；M4 之前保存的实体没有网格，导出时跳过并提示。读入 DWG 时多面网格仍不转成实体
- 测试：`tests/Solid/Solid3DEntityTests.cpp`（51 项：显示、克隆、四种变换、命令与撤销、复制、夹点、捕捉、JSON / .mcad / .json 往返、base64）

---

## 6. 显示分期

| 期 | 内容 | 改动范围 |
|---|---|---|
| 一 | 俯视线框：边线 + 视线方向 (0,0,-1) 的轮廓线，经 `IDrawSink::DrawLine` 输出；可选 `FillTriangle` 浅色填充。按实体缓存，走静态流 | 只有实体和适配层 |
| 二 | 三维视图：`Camera` 增加视线方向（旋转、标准视图），`LookAt` 视图矩阵；顶点格式 `Vertex_P3_N3_C4` 与光照着色器；两个渲染器给实体层开深度测试（GL 渲染器目前关闭）；`IDrawSink` 增加 `EmitMesh`；轮廓线随视角变化，进动态流 | 视口、渲染器、着色器 |
| 三 ✅ | 三维出二维图：截面、平面投影生成二维实体（M4，见第 7 节） | 新命令 |

先做一、三，二的改动面最大，放最后。

---

## 7. 编辑与命令（第一批已在 M3 实现）

### 内核从哪里来

`AppPlatform::GetSolidKernel()`（默认返回空）：桌面版 `Win32Window` 在 `MINICAD_WITH_SOLID` 时第一次请求时创建 SMLib 内核并持有；网页版不实现。`MainFrame::Initialize` 把它交给 `Editor::SetSolidKernel`。没有内核时建模命令只在命令行提示“不可用”。

### 第一批命令（`src/Editor/Tools/SolidTools.h`，注册在 `Editor::RegisterBuiltinTools`）

| 命令 | 别名 | 操作 |
|---|---|---|
| `Box` | BOX | 底面两个角点 → 高度 |
| `Cylinder` | CYLINDER、CYL | 底面中心 → 半径 → 高度 |
| `Sphere` | SPHERE | 中心 → 半径 |
| `Extrude` | EXTRUDE、EXT | 预选闭合对象（圆、闭合多段线含圆弧段、矩形）→ 高度；原对象被替换（同 AutoCAD 默认）。多段线的凸度交给内核前取反（M5 修正：M3 时直接传入，圆弧段凸向反侧） |
| `Union` / `Subtract` / `Intersect` | UNION / SUBTRACT / INTERSECT | 选择集里有三维实体时做实体布尔（`Editor::SolidBoolean`），否则仍是面域布尔。差集与面域相同：ID 最小（最先创建）的为被减对象 |

- 视图仍是俯视：底面在点取点的水平面上，高度沿 +Z（负数沿 −Z），键入数值或点取两点距离
- 工具在命令行键入数值时也要提交，那时 `EditorContext` 已失效：工具持有 `SolidToolEnv`（场景、撤销栈、选择、预览层、内核、命令行输出），由 `Editor::MakeSolidToolEnv` 创建
- 撤销：新建经 `AddEntityCommand`；拉伸与布尔经 `GeometryEditCommand`（原对象与结果一步替换，一次撤销全部还原）
- 失败：内核返回空时命令行提示原因，不入撤销栈
- 菜单：顶层“建模”（长方体、圆柱体、球体、拉伸、并集、差集、交集）；“修改 > 面域”保留同样的并集 / 差集 / 交集。命令 ID `solid.box` / `solid.cylinder` / `solid.sphere` / `solid.extrude` / `region.union` / `region.subtract` / `region.intersect`
- 测试：`tests/Solid/SolidToolTests.cpp`（30 项，经 `Editor` 的公开入口驱动）；桌面版自测 `--selftest` 的“三维实体”一节经真实命令行入口验证建模、拉伸、并集、显示与撤销

### 三维出二维图（M4）

| 命令 | 别名 | 操作 |
|---|---|---|
| `SolidSection` | SOLSECTION、SECTION、SEC | 预选三维实体。键入高度 z → 水平截面，曲线原位输出（z 取 0），闭合轮廓可直接再拉伸；或点取两点 → 过这两点的铅垂截面，展开到图纸平面（沿两点方向为 +X、原来的 Z 为 +Y），第三点为第一点在 Z = 0 处的放置位置 |
| `Flatshot` | FLATSHOT、FSHOT | 预选三维实体。T / U / F / B / R / L 选俯视、仰视、前视、后视、右视、左视（默认前视），H 切换是否生成隐藏线；点取放置点 = 投影图左下角 |

- 截交线由 `ISolidKernel::Section` 给出，按面分段，先首尾相接连成轮廓；投影线由 `HiddenLines` 给出（隐藏线消除）。多个实体投影时先并成一个，互相遮挡才能消隐（合并失败时逐个投影）
- **拟合**（`src/Solid/CurveFit.h`）：内核给的是采样点折线，拟合回直线、圆弧、整圆、带凸度的多段线（容差为图形尺寸的 1e-5），出图后可直接标注半径、直径、长度。相邻采样点绕圆心转角超过 30° 的不当作圆弧（内核约 10° 采样一次），避免正多边形被认成圆。椭圆（倾斜的圆的投影）仍是折线
- **隐藏线**：用 `HIDDEN` 线型（不存在时加入线型表），线型比例按图形尺寸取（一段虚线约为尺寸的 1/50）；与可见线重合（如正对视线的面后面的边）或彼此重复的隐藏线不画
- 结果用当前图层、线宽，经 `BatchAddCommand` 一步撤销
- 测试：`tests/Solid/SolidDrawingTests.cpp`（53 项：拟合、连接、网格、`.mcad` / JSON 往返、DWG / DXF 导出读回、截面、剖面、投影、隐藏线、撤销）

### 第二批命令（M5）

| 命令 | 别名 | 操作 |
|---|---|---|
| `Revolve` | REVOLVE、REV | 预选闭合对象 → 轴的两点（或 X / Y 键取世界 X / Y 轴）→ 角度（键入度数，按右手定则绕起点 → 端点；回车 / 右键为整圈）。轴放到轮廓所在的水平面上；原对象被替换 |
| `Sweep` | SWEEP | 预选闭合对象 → 点选路径（直线、圆弧、圆、多段线含圆弧段、矩形，闭合或不闭合）。原对象（轮廓）被替换，路径保留 |
| `FilletEdge` | FILLETEDGE | 点选同一个实体上的边（再点取消），键入数值改半径（默认 1，命令之间保留），回车 / 右键完成 |
| `ChamferEdge` | CHAMFEREDGE | 同上，倒角距离 |

- **选边**（`SolidEdgeTool`）：光标下的实体经 `GetWorldBody` 取世界坐标下的内核实体、再取 `Edges()`（编号对这个实体有效，运算也在它上面做，结果的变换为单位矩阵、留在原位）；离光标 8 像素内的边按“距离（按像素取整）→ 投影成一点的竖直边 → 高的在前”排序，光标处高亮第一条。俯视图里重叠的边（长方体的顶边与底边）按 **Tab** 切换；竖直边在角点处点选。桌面版绘图区在有进行中的工具时把 Tab 交给工具（`MainFrame::OnViewportKey`），工具不处理时仍用于切换焦点
- 旋转、扫掠、拉伸共用 `SolidToolDetail::ReplaceProfiles`（逐个轮廓生成、替换原对象、一步撤销）；圆角 / 倒角经 `GeometryEditCommand` 一步替换。半径过大等内核失败时提示且不改动
- 菜单“建模”：拉伸、旋转、扫掠一组，圆角边、倒角边一组。命令 ID `solid.revolve` / `solid.sweep` / `solid.filletedge` / `solid.chamferedge`
- 测试：`SolidToolTests` 增至 66 项（旋转：两点轴、X / Y 键、角度、轴穿过轮廓；扫掠：直线、折线、圆弧、带圆弧段的多段线、圆路径；圆角 / 倒角：重叠边取高的、角点取竖直边、Tab 切到底边、取消选择、两条边、移动过的实体、半径过大）；桌面版自测经真实命令行与按键验证旋转、圆角边（含 Tab）与撤销

### 之后

- 不等距倒角（两个距离）、变半径圆角；按面选边（选一个面的全部边）
- 耗时运算：桌面版可交给单个后台工作线程（内核只能单线程），网页版同步执行，与打开图纸的做法一致。目前在界面线程同步执行

---

## 8. 里程碑

| 里程碑 | 内容 | 验收 |
|---|---|---|
| **M0** ✅ | 引入 SMLib 源码；静态库、C++20 补丁；`cmake/SMLib.cmake`；冒烟测试 `SMLibSmokeTests` | Debug / Release 全部测试通过 |
| **M1** ✅ | `ISolidKernel` 接口与 `SMLibKernel` 适配层（目标 `MiniCADSolid`）；接口测试 `SolidKernelTests`（基本体、拉伸、旋转、布尔、圆角、变换、离散化、边、隐藏线、截面、读写与损坏数据） | 测试只依赖接口；MiniCAD 公开头文件不含 `Sm*` |
| **M2** ✅ | `Solid3DEntity`：俯视线框显示、变换、拾取、捕捉、夹点、撤销、`.mcad` / JSON 读写；测试 `Solid3DEntityTests` | 测试覆盖实体与命令 |
| **M3** ✅ | 第一批命令接入界面（`MainCommands.cpp`、`minicad_ui.json`），内核经 `AppPlatform` 注入；测试 `SolidToolTests` 与桌面版自测 | 桌面版里实际建模、布尔、撤销 |
| **M4** ✅ | 截面（`SolidSection`）、平面投影（`Flatshot`）出二维图，拟合为直线 / 圆弧 / 圆；DWG / DXF 导出为多面网格；测试 `SolidDrawingTests` | 出图结果可标注、可导出 DWG |
| **M5** ✅ | 第二批命令：旋转（`Revolve`）、扫掠（`Sweep`）、边圆角（`FilletEdge`）、边倒角（`ChamferEdge`），俯视图里选边（Tab 切换重叠的边）；内核增加 `Sweep`、`Chamfer` | 桌面版里实际旋转、扫掠、选边圆角 / 倒角、撤销 |
| M6 | 三维视图（显示第二期） | |
| — | 网页版评估：Emscripten 编译、异常、WASM 体积 | 决定网页版是否默认带 |

---

## 9. 参数化（另立模块，不在本计划内）

参数化不依赖具体内核，做成独立模块：约束求解器 → 二维草图 → 特征树 → 经 `ISolidKernel` 调内核。约束求解器可以先用在 MiniCAD 现有的二维图纸上（几何约束、标注约束）。三维参数化的难点是**拓扑命名**：SMLib 的 `SmAttribute` 可以挂在面 / 边上并在拆分、复制、合并时传递（`SM_AB_COPY` / `SM_AB_REFERENCE`），能否支撑稳定命名需在 M1 之后实测布尔、圆角后属性是否正确传递。

---

## 10. 风险

| 风险 | 应对 |
|---|---|
| SMLib 刚开源，复杂模型上布尔、圆角的鲁棒性未经公开验证 | 积累真实测试模型，必要时与 OCCT 对比；接口隔离保证可替换 |
| 内核只能单线程 | 运算串行化；界面线程执行或单个后台线程 |
| DWG 三维实体无法互通 | 明确为非目标；导出退化为网格并提示 |
| 网页版体积（Release 静态库 `SMLib.lib` 约 100 MB，但链接后只带进用到的部分，冒烟测试 exe 仅 4.9 MB） | 先测 WASM 实际体积再决定 |
| 跟进上游更新 | 本地修改都记在 `3rd/SMLib/README.md`，更新时重新应用 |

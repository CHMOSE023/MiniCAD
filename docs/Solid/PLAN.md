# 三维实体建模开发计划

> 状态：M0～M2 已完成（2026-10-17）：SMLib 静态库构建（C++20）；`ISolidKernel` 接口与 SMLib 适配层；`Solid3DEntity`（俯视线框、变换、拾取、捕捉、夹点、撤销、读写）
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
| `tests/Solid/SolidKernelTests.cpp` | 接口测试（55 项），只经接口调用，换内核实现时应原样通过 |

- `BodyPtr = shared_ptr<const IBody>`：实体不可变，所有运算返回新实体、输入不变，可在撤销栈、剪贴板间共享
- 创建：`Box`、`Cylinder`、`Sphere`、`Extrude`（轮廓沿 ±Z）、`Revolve`
- 轮廓 `Profile`：z = 常数平面上的闭合多段线，顶点带凸度（与 MiniCAD 多段线相同，顺 / 逆时针都可以），或整圆
- 修改：`Boolean`（并 / 差 / 交）、`Fillet`（按 `Edges()` 给出的边编号）、`Transform`（任意 `Mat4`，含镜像、非均匀缩放）
- 显示与查询：`Tessellate`（顶点按面拆分、面内平滑法线、三角形逆时针朝外）、`Edges`（每条边一条折线 + 编号）、`HiddenLines`（隐藏线消除：可见 / 被挡住的边与轮廓线）、`Section`、`Volume`、`Bounds`
- 读写：`Save` / `Load`，数据 = 16 字节头（`SMLB`、版本、长度、CRC32）+ SMLib 二进制格式；`Load` 先校验，损坏的数据不交给 SMLib（它的读取器遇到乱码会崩溃）
- 失败：返回空指针 / 空结果，原因由 `LastError()` 给出

### 实现要点与已知限制

- **视线方向**：SMLib 的 `SmHCR` 用"从物体指向眼睛"的向量，适配层取反后传入；接口里 `viewDir` 是视线方向（俯视为 (0, 0, −1)）
- **隐藏线**：`SmApiCreateSilhouetteCurves` 实际是 `SmHCR` 隐藏线消除且只给可见线、显示曲面接缝，适配层直接用 `SmHCR`，不显示接缝和光滑相接的边。**已知限制**：轮廓线与曲面接缝重合时（如从正面看球，接缝在 xz 平面上），重合的那一段不输出
- **法线**：`SmPolyVertex` 自带的面 / 法线表实测对不上（长方体顶点拿到相邻面的法线），改为按面对周围三角形法线面积加权平均
- **边编号**：`SmApiGetEdges` 的顺序；深拷贝保持顺序，所以 `Fillet` 在副本上按同一编号取边。编号只在同一实体内有效，运算后的新实体重新编号（稳定的拓扑命名见第 9 节）
- **读写经临时文件**：SM_API 只有文件接口；SMLib 内部有内存流 `SmDatabaseIOStream`，但直接用要重复其版本头逻辑，留待以后。临时文件名用窄字符（ANSI）路径，临时目录含 ANSI 无法表示的字符时会失败
- **单线程**：见第 2 节

---

## 5. 实体 `Solid3DEntity`（M2 已实现）

`src/Core/Entity/Solid3DEntity.hpp`（纯头文件，核心库）。命名：MiniCAD 已有 `SolidEntity`（二维填充，DXF 的 SOLID），三维实体用 `Solid3DEntity`（对应 DXF 3DSOLID）。

- **几何 = 共享数据 + 变换矩阵**：内核数据（`Save` 的字节）与局部坐标下的线框在 `Create(id, kernel, body)` 时一次求出、之后不变，克隆之间共享（`shared_ptr`）；变换矩阵 `Mat4`（局部 → 世界，行向量约定）每个实体一份
- **核心库不依赖内核实现**：实体只用 `ISolidKernel` 接口；需要内核实体时 `GetBody(kernel)` 从字节懒加载、`GetWorldBody(kernel)` 再施加变换。没有内核（网页版）也能显示、移动、原样保存
- **显示**（第一期）：世界坐标线框 = 实体所有边的折线，经 `IDrawSink::DrawLine` 输出；包围盒由线框求出。暂不画曲面轮廓线（球只显示接缝和赤道）
- **变换只改矩阵**：`TranslateEntityInPlace` / `Rotate` / `Mirror` / `Scale` 各加一个分支（复制、阵列、缩放工具经它们生效）；移动 / 旋转 / 镜像命令的 `MoveEntityEntry` 与夹点的 `DragEntityEntry` 增加 `Kind::Transform`，保存前后矩阵用于撤销
- **拾取**：走 `Picking` 的通用回退（按 `Draw()` 输出的线段命中），无需特化
- **捕捉**（`Document/FeaturePoints.cpp`、`SnapEngine::TryNearest`）：端点 = 不闭合的边的两端 + 圆形边的圆心（同圆 / 圆弧的约定）；中点 = 不闭合的边按长度的中点；圆心；最近点 = 线框各段上的最近点
- **夹点**（`Editor/Grip/Solid3DGripHandler.h`）：包围盒中心一个，拖动即整体移动
- **读写**（`EntityIO`）：`data`（内核字节，base64，`Ser::Bytes`）、`transform`（16 个数）、`wire`（局部线框，没有内核时用来显示）；`.mcad` 与 JSON 都适用
- **DWG / DXF**：导出时作为不支持的实体跳过并 `LOG_WARN`（退化为网格留到 M4）
- 测试：`tests/Solid/Solid3DEntityTests.cpp`（51 项：显示、克隆、四种变换、命令与撤销、复制、夹点、捕捉、JSON / .mcad / .json 往返、base64）

---

## 6. 显示分期

| 期 | 内容 | 改动范围 |
|---|---|---|
| 一 | 俯视线框：边线 + 视线方向 (0,0,-1) 的轮廓线，经 `IDrawSink::DrawLine` 输出；可选 `FillTriangle` 浅色填充。按实体缓存，走静态流 | 只有实体和适配层 |
| 二 | 三维视图：`Camera` 增加视线方向（旋转、标准视图），`LookAt` 视图矩阵；顶点格式 `Vertex_P3_N3_C4` 与光照着色器；两个渲染器给实体层开深度测试（GL 渲染器目前关闭）；`IDrawSink` 增加 `EmitMesh`；轮廓线随视角变化，进动态流 | 视口、渲染器、着色器 |
| 三 | 三维出二维图：截面、平面投影生成二维实体 | 新命令 |

先做一、三，二的改动面最大，放最后。

---

## 7. 编辑与命令

- 撤销：命令里保存新旧 `BodyPtr`，撤销时换回；布尔运算的输入实体用现有的移除 / 恢复所有权机制
- 失败：内核返回失败时 `Execute()` 返回 false，不入撤销栈，命令行提示原因
- 第一批命令：长方体、圆柱、球、拉伸（选闭合多段线 / 圆 / 面域）、并、差、交
- 第二批：旋转、扫掠、圆角、倒角（需要能选中实体上的边：拾取命中的边线折线带回 `EdgeId`）、截面、平面投影
- 耗时运算：桌面版可交给单个后台工作线程（内核只能单线程），网页版同步执行，与打开图纸的做法一致

---

## 8. 里程碑

| 里程碑 | 内容 | 验收 |
|---|---|---|
| **M0** ✅ | 引入 SMLib 源码；静态库、C++20 补丁；`cmake/SMLib.cmake`；冒烟测试 `SMLibSmokeTests` | Debug / Release 全部测试通过 |
| **M1** ✅ | `ISolidKernel` 接口与 `SMLibKernel` 适配层（目标 `MiniCADSolid`）；接口测试 `SolidKernelTests`（基本体、拉伸、旋转、布尔、圆角、变换、离散化、边、隐藏线、截面、读写与损坏数据） | 测试只依赖接口；MiniCAD 公开头文件不含 `Sm*` |
| **M2** ✅ | `Solid3DEntity`：俯视线框显示、变换、拾取、捕捉、夹点、撤销、`.mcad` / JSON 读写；测试 `Solid3DEntityTests` | 测试覆盖实体与命令 |
| M3 | 第一批命令接入界面（`MainCommands.cpp`、`minicad_ui.json`） | 桌面版里实际建模、布尔、撤销、保存再打开 |
| M4 | 截面、平面投影出二维图；DWG 导出退化为网格 | 出图结果可标注、可导出 DWG |
| M5 | 第二批命令：旋转、扫掠、圆角、倒角，选边 | |
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

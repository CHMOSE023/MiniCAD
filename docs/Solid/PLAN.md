# 三维实体建模开发计划

> 状态：M0～M6 已完成（2026-10-18）：SMLib 静态库构建（C++20）；`ISolidKernel` 接口与 SMLib 适配层；`Solid3DEntity`（俯视线框、变换、拾取、捕捉、夹点、撤销、读写）；桌面版建模命令（长方体、圆柱、球、拉伸、旋转、扫掠、实体并 / 差 / 交、边圆角 / 倒角）；截面、平面投影出二维图，DWG / DXF 导出为多面网格；三维视图（标准视图、环绕、着色、隐藏线、轮廓线）
> M7 已完成（2026-10-20）：空间 NURBS 曲线（`NurbsCurve`：插值、拟合、螺旋线）与曲面（放样、直纹面、旋转曲面、点阵曲面、偏移曲面、管道、加厚、曲面取点），开放曲面也是 `BodyPtr`，经 `Solid3DEntity` 显示、读写、导出；节点图里的“曲线/NURBS”与“曲面”节点见 `docs/NodeGraph/PLAN.md`。见第 11 节
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
| `src/Solid/NurbsCurve.h` | 空间 NURBS 曲线数据（M7）：求值、采样、仿射变换、直线 / 圆弧段 → 二次有理曲线，不依赖内核 |
| `tests/Solid/SolidKernelTests.cpp` | 接口测试（123 项，其中 M7 的曲线与曲面 54 项），只经接口调用，换内核实现时应原样通过 |

- `BodyPtr = shared_ptr<const IBody>`：实体不可变，所有运算返回新实体、输入不变，可在撤销栈、剪贴板间共享
- 创建：`Box`、`Cylinder`、`Sphere`、`Extrude`（轮廓沿 ±Z）、`Revolve`、`Sweep`（M5，轮廓沿 `SweepPath` 扫掠）
- 轮廓 `Profile`：z = 常数平面上的闭合多段线，顶点带凸度（tan(圆心角 / 4)，**正值为逆时针，与 DXF 相同、与 MiniCAD 的 `Polyline` 相反**；顺 / 逆时针绕行都可以），或整圆
- 扫掠路径 `SweepPath`：同一水平面上首尾相接的直线 / 圆弧（顶点带凸度，约定同 `Profile`），可闭合
- 修改：`Boolean`（并 / 差 / 交）、`Fillet` / `Chamfer`（M5，按 `Edges()` 给出的边编号）、`Transform`（任意 `Mat4`，含镜像、非均匀缩放）
- 显示与查询：`Tessellate`（顶点按面拆分、面内平滑法线、三角形逆时针朝外）、`Edges`（每条边一条折线 + 编号）、`HiddenLines`（隐藏线消除：可见 / 被挡住的边与轮廓线）、`Section`、`Volume`、`Bounds`；M7 增加 `Area`、`IsSolid`、`FaceCount`
- 空间曲线与曲面（M7）：`InterpolateCurve`、`FitCurve`、`Helix`、`Loft`、`RuledSurface`、`RevolveSurface`、`SurfaceFromPoints`、`OffsetSurface`、`Pipe`、`Thicken`、`EvaluateSurface`，见第 11 节
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
| 二 ✅ | 三维视图（M6，见下）：相机的视线方向、标准视图与环绕；着色面、深度测试（被挡住的边看不见）、随视角变化的轮廓线 | 相机、视口、两个渲染器的渲染目标、编辑器 |
| 三 ✅ | 三维出二维图：截面、平面投影生成二维实体（M4，见第 7 节） | 新命令 |

### 三维视图（M6）

- **相机**（`Viewport/Camera`）：视线方向用方位角 / 仰角表示（`CameraState::Azimuth` / `Elevation`，度，从目标看向眼睛），俯视为 (−90°, 90°)。**俯视（`IsPlanView()`）时矩阵与换算和二维版本完全相同**，二维图纸的显示、拾取、捕捉不受影响。三维视图仍是正交投影：视图矩阵 = 平移到目标点 + 转到 (屏幕右, 屏幕上, 视线)；`ScreenToWorld` 把屏幕点沿视线投到 z = 0 平面（构造平面），视线与 XY 平面夹角小于约 3°（前视等）时改为过目标点、垂直于视线的平面；`Pan` 沿屏幕方向移动目标点；`ZoomToBounds(AABB)` 按视图方向投影后容纳三维范围。视图方向随文档保存（`.mcad` / JSON 的 `camera.azimuth` / `elevation`，旧文件为俯视）
- **命令**：标准视图 `ViewTop` / `ViewBottom` / `ViewFront` / `ViewBack` / `ViewLeft` / `ViewRight` / `ViewSWIso` / `ViewSEIso` / `ViewNEIso` / `ViewNWIso`（`PLAN` = 俯视），切换后缩放到全图，经 `ZoomViewCommand` 可撤销；**Shift + 中键拖动环绕**（绕目标点，左右改方位角、上下改仰角）；视觉样式 `Shade`（着色，默认）/ `Wireframe`（线框），别名 SHADE / WIREFRAME。菜单“视图 > 三维视图 / 视觉样式”
- **视图方块**（`apps/Common/src/GUI/ViewCube`，MiniGUI 控件，绝对定位叠在绘图区右上角，桌面版与网页版共用）：按当前视线方向画出立方体和指南针圆环（东 = +X、南 = −Y、西 = −X、北 = +Y）。立方体按 3×3×3 分块（6 个面中央、12 条棱中间、8 个角），点哪一块就从那个方向看（面 → 正视，棱 → 两面之间 45°，角 → 等轴测），点东南西北从那个方向看（仰角不变）；经 `Editor::SetViewDirection` 切换并缩放到全图、可撤销。悬停时整块高亮（角块亮三个面、棱块亮两个面）；面上的字（上 / 下 / 前 / 后 / 左 / 右）先排版再经 `MiniGUI::DrawList::TransformVertices` 变换到面上，跟着面一起转；在方块上拖动即环绕。显示 / 隐藏：“视图 > 显示视图方块”、方块的右键菜单（常用视图与“隐藏视图方块”）、命令行 `NAVVCUBE` / `VIEWCUBE`（不带选项切换，`ON` / `OFF` 指定，同 AutoCAD），开关保存在用户布局文件 `layout.json` 的 `viewCube` 里；桌面版自测经 `UIContext` 的指针入口点击、拖动验证，并验证三种显示 / 隐藏方式
- **着色面**（`Editor::UpdateShadedVertices`）：取 `Solid3DEntity` 保存的网格（网页版没有内核也能显示），每个三角形角的法线由 `BuildCornerNormals` 求出（按“同一个面内部的边”连成光滑扇区，曲面光滑、棱边锐利）；**明暗在 CPU 上按视线方向算好写进顶点颜色**（光源跟着相机，亮度最高 0.8，边画在面上看得清），所以沿用 `Vertex_P3_C4` 与现有着色器，没有新增带法线的顶点格式和光照着色器。视线方向、视觉样式或场景变化时重建，平移、缩放不重建；三角形从远到近排序（俯视时着色面不做深度测试，按绘制顺序叠放）
- **深度**：两个渲染器的渲染目标增加深度缓冲（D3D11 `D32_FLOAT`，GL `DEPTH_COMPONENT24`），每帧清除。俯视时深度仍统一取 0.5、只按绘制顺序；三维视图按场景范围（`ViewState::SceneBounds`，按场景版本缓存）把沿视线的距离映射到 [0, 1]（两侧留余量，构造线、叠加层不被裁剪），着色面写深度，线、填充、文字只测试不写。线向眼睛拉近约 1.5 像素（轮廓线 6 像素：旁边的三角形几乎平行于视线）——偏移按像素而不是按图纸范围取，否则图纸很大时紧挨在面后面的边会透出来
- **轮廓线**：面内部的网格边两侧三角形一个朝向眼睛、一个背向眼睛的，就是这个视角下的轮廓线（圆柱、球的外轮廓）；在 CPU 上从网格求出，不需要内核，与着色面一起随视图重建，两种视觉样式都画。为此 `SMLibKernel::Tessellate` 改为在同一个原始面里按位置合并顶点（SMLib 给相邻面片各建顶点对象，原来按对象合并时曲面法线只在每个面片内平均，面内的边也没被标记），M4 的多面网格导出也因此少了曲面上多余的可见边
- **网格与坐标轴**：俯视时仍按屏幕坐标生成；三维视图里画在 z = 0 平面上（`Grid::BuildGrid3D`：屏幕四角投到平面上的范围，限制在目标点外 4 倍视图高度以内，间距约 60 像素、每 5 格加深；视线接近水平时不画），坐标轴（`Axis::BuildAxis3D`）画 +X 红、+Y 绿、+Z 蓝三条正半轴；用场景矩阵绘制、画在着色面之后，被实体挡住的部分不显示
- **拾取**：三维视图里屏幕上的一点对应一条视线，空间索引（世界 XY 网格）筛不出候选，改为全部候选在屏幕空间精确测试（通用回退按投影后的线段求像素距离）；捕捉、夹点本来就按屏幕距离比较，三维视图里可以捕捉到实体的角点
- 测试：`tests/Solid/SolidViewTests.cpp`（相机换算、标准视图与撤销、视图方向读写、法线、着色与重建条件、轮廓线、视口的深度设置、点选与窗选）；桌面版自测读回视口像素，检查着色面、可见边与被挡住的边，并把画面存为 `%TEMP%/minicad_selftest_3d.bmp`

**已知限制**：绘图工具仍在 z = 0 平面上取点（没有 UCS）；透视投影、真实感材质未做；网格、坐标轴图标之外的辅助显示（如动态输入）按俯视设计。

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
| **M6** ✅ | 三维视图（显示第二期）：标准视图、Shift + 中键环绕、着色 / 线框、深度测试消隐、轮廓线；两个渲染器的渲染目标加深度缓冲；测试 `SolidViewTests` | 桌面版里等轴测看到着色的实体，被挡住的边不显示 |
| **M7** ✅ | 空间 NURBS 曲线与开放曲面（第 11 节）：`NurbsCurve`；内核增加插值 / 拟合 / 螺旋线、放样、直纹面、旋转曲面、点阵曲面、偏移、管道、加厚、曲面取点、面积与是否封闭；节点图的“曲线/NURBS”与“曲面”节点；测试 `SolidKernelTests`、`NodeSolidTests` | 节点图里放样 → 加厚得到实体；开放曲面着色、烘焙、读写、导出 DWG / DXF |
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

---

## 11. 空间曲线与曲面（M7）

第一版的建模只有封闭实体，轮廓、路径都是水平面上的二维曲线实体，没有曲面。M7 把 SMLib 的 NURBS 曲线、曲面接口经 `ISolidKernel` 开放出来，主要给节点式参数化建模用（`docs/NodeGraph/PLAN.md` 第 4.4 节“曲面”）。

### 11.1 数据

- **空间曲线** `Solid::NurbsCurve`（`src/Solid/NurbsCurve.h`，纯头文件，核心库）：阶数、控制点、完整的节点矢量（长度 = 控制点数 + 阶数 + 1，与 DXF SPLINE、MiniCAD 的 `Spline` 相同）、权重（空为非有理）。它是 MiniCAD 自己的数据：求值（齐次坐标 de Boor）、导数（中心差分）、按节点区间采样、长度、仿射变换（只变换控制点）都不需要内核，网页版也能显示、变换。
  - `FromControlPoints(点, 阶数)`：两端夹持的均匀节点，点数不够时降阶
  - `FromSegments(点, 凸度, 闭合)`：首尾相接的直线 / 圆弧段（凸度约定同 `ProfileVertex`：正值逆时针）→ 二次有理曲线，圆弧按不超过 90° 分段，精确表示圆；每段占一个单位参数区间，内部节点重复 2 次
- **曲面**：仍是 `BodyPtr`。开放的曲面片（一个或多个面、不围成体积）与实体用同一个类型，`IsSolid()` 区分（`SmApiBrepIsManifoldSolid`）。好处是变换、离散、边、截面、读写、`Solid3DEntity` 的显示与导出全部沿用，不需要新的实体类型

### 11.2 接口与实现（`SMLibKernel.cpp`）

| 接口 | 实现 | 说明 |
|---|---|---|
| `InterpolateCurve(点, 闭合)` | `SmBSplineCurve::CreateInterpolatingCurve`（弦长参数化、三次） | 至少 3 个点、相邻点不重合。闭合时首点加到末尾、两端给同一个切线向量（方向取首点两侧点的连线，长度按“总弦长 / 参数域长”换算：先插值一次取参数域），接点处 C1 |
| `FitCurve(点, 容差)` | `SmBSplineCurve::CreateApproximatingCurve`（弦长、三次） | `SmApiCreateCurveApproxPoints` 的控制点数固定为点数的 1/4，正弦样例偏差 0.37。改为从 1/4 起每次增加一半，直到每个点到曲线的距离 ≤ 容差（默认点集尺寸的 1/1000），最多等于点数（即插值） |
| `Helix` | `SmApiCreateHelix` | 拟合容差取最大半径的 1e-5；圈数 ≤ 124（SM_API 的上限） |
| `Loft` | `SmApiCreateSkin` | 截面全部闭合时得到不封口的管状曲面 |
| `RuledSurface` | `SmApiCreateRuledSurface` | |
| `RevolveSurface` | `SmApiCreateSurfaceRevolution` | 负角度改为轴反向、角度取正（右手定则下等价） |
| `SurfaceFromPoints` | `SmBSplineSurface::ApproximatePoints`（控制点数 = 点数，即经过各点） | 与 `SmApiCreateSurfaceFromOrderedPoints` 相同，但阶数取 min(3, 点数 − 1)：后者固定三次，行或列少于 4 个点时失败 |
| `OffsetSurface` | 每个面的曲面 `SmApiCreateOffsetSurface` | 沿面的外侧（见下）；裁剪过的面按整个曲面偏移 |
| `Pipe` | `SmApiCreatePipeSweep` | 封口时得到实体（螺旋线 → 弹簧） |
| `Thicken` | `SmApiShellBrepFull(…, bCreateOffsetSolid = TRUE, 面列表为空)` | 面列表是“不偏移、原样保留”的面（如抽壳时开口的面），加厚时必须为空，传全部面会失败；侧边由 SMLib 用直纹面封闭并缝合成实体 |
| `EvaluateSurface(面, u, v)` | `SmApiEvaluateSurfacePoint` / `Normal` | u、v 归一化到面所在曲面的参数域 |
| `Area` / `IsSolid` / `FaceCount` | `SmApiBrepComputeArea` / `SmApiBrepIsManifoldSolid` / `SmApiGetFaces` | |

- **曲面 → BRep**：`SmBrep::CreateFacesFromSurface(曲面, 参数域, SM_CT_C1)`，曲面交给 brep（拆分时原曲面被删除）。在 C1 不连续处拆成几个面：`Tessellate` 在同一个面内平滑法线，放样经过矩形截面时折痕留在一个面里会被抹平（两个正方形放样得到 4 个侧面）。之后 `SmApiStitchAdvanced`（只缝合单侧边）把闭合曲面的接缝、拆开的相邻面连起来，加厚才能得到封闭的实体
- **法线方向**：面的第一个面使用（faceuse）的法线就是曲面法线；它在无穷区域（实体外面）时外侧即曲面法线方向，开放曲面两侧都在无穷区域，取曲面法线。`EvaluateSurface` 的法线、`OffsetSurface` / `Thicken` 的正方向都按这个外侧，测试检查它与离散网格的三角形朝向一致
- **曲线转换**：`NurbsCurve` ↔ `SmBSplineCurve` 经 `SmApiCreateCanonicalCurve` / `GetCanonical`（唯一节点 + 重复度）；节点矢量要两端夹持

### 11.3 显示、读写与导出

开放曲面放进 `Solid3DEntity` 不需要改实体：离散、线框（边界边）、包围盒、读写（内核字节 + 网格）、DWG / DXF 多面网格都按原来的方式工作——网格的边界边只属于一个三角形，导出时是可见边。着色本来就把背对视线的法线翻过来（双面光照），曲面的背面同样有明暗。

- 对开放曲面无意义的操作给出明确提示：节点的“实体信息”对开放曲面输出体积 0 并警告（面积见“曲面信息”）；节点的并 / 差 / 交与桌面版 `UNION` / `SUBTRACT` / `INTERSECT` 遇到开放曲面报错“只适用于封闭实体”；“加厚”“偏移曲面”的输入是实体时报错
- 空间曲线烘焙为带控制点的 `SplineEntity`（精确求值，`.mcad` / JSON 保存控制点、节点、权重）；DWG / DXF 导出仍跳过样条（`CadExchange` 的已知限制）

### 11.4 已知限制

- 放样的截面要方向一致、起点大致对齐，否则曲面会扭转（SMLib 不自动对齐）；没有导轨、没有封口选项（可以先加厚，或用“管道”）
- 偏移曲面对裁剪过的面按整个曲面偏移；`SmApiCreateOffsetSurface` 是近似（容差 0.001）
- 没有曲面的修剪、分割、曲面间求交 / 圆角（SMLib 有 `SmApiTrimSurfaceWith3dCurves`、`SmApiSurfaceSurfaceFillet`，留待以后）
- 绘图工具仍不能直接画空间曲线（没有 UCS），空间曲线只在节点图里生成
- `CreateFacesFromSurface` 失败时曲面的所有权不确定（可能已挂到 brep 上），适配层宁可泄漏也不重复释放

---

## 12. 实体形体（节点 N7）

节点式参数化建模的 Revit 式形体（`docs/NodeGraph/PLAN.md` 第 5 节）需要的接口，都在 `SMLibKernel.cpp`：

| 接口 | SMLib | 说明 |
|---|---|---|
| `Cone(baseCenter, radiusBase, radiusTop, height)` | `SmApiCreateCone` | 顶面半径为 0 时是尖顶圆锥，两个半径不能都为 0 |
| `Torus(center, majorRadius, minorRadius)` | `SmApiCreateTorus` | 轴沿 +Z，主半径 > 截面半径 > 0 |
| `LoftSolid(sections, ruled)` | `SmApiCreateSkinPrimitive` | 依次经过各闭合截面并两端封口；`ruled` 时一次，否则阶数为 min(3, 截面数 − 1)。截面方向要一致（节点侧统一为逆时针） |
| `SweepAlong(profile, path)` | `SmApiCreateCurveSweep` | 闭合轮廓沿空间曲线扫掠并封口。只设 `bMoveProfileCenterToPath`：轮廓的包围盒中心移到路径起点（不设时 SMLib 把轮廓的**起点**移到路径起点，圆形轮廓沿螺旋线扫掠的体积会大约 9%）；轮廓的朝向由调用方放好 |
| `Shell(body, thickness, openFaces)` | `SmApiShellBrepFull`（偏移 −thickness，即向内） | `openFaces` 为去掉的面（序号同 `FaceCount`），为空时得到带封闭空腔的实体；10³ 长方体厚 1：空腔 488、去掉一个面 424 |

`NurbsCurve::Reversed()` 给出反向的同一条曲线（截面方向统一用）。测试在 `NodeSolidTests` 的 `TestN7Forms`（经节点）。

---

## 13. 曲面的创建、修改与分析（节点 N8）

`docs/NodeGraph/PLAN.md` 第 6 节的曲面节点用到的接口，都在 `SMLibKernel.cpp`，输入不变（修改类在副本上做）：

| 接口 | SMLib | 注意 |
|---|---|---|
| `PlanarSurface(loops)` | `SmApiCreatePlanarFaces` | 成功时曲线被接管，失败时由适配层释放 |
| `ExtrudeCurve(curve, vector)` | `SmApiCreateExtrude` | 开放曲面，不封口 |
| `SweepSurface(section, rail)` | `SmApiCreateCurveSweep`（`bMoveToProfile`） | SMLib 把截面起点移到轨道起点再扫，结果移回截面所在位置：截面起点应在轨道起点 |
| `CoonsSurface(bottom, right, top, left)` | `SmBSplineSurface::CreateCoonsPatch`（双线性） | SMLib 按左、右、下、上的顺序读取，对边方向要一致 |
| `NetworkSurface(u, v)` | `SmBSplineSurface::CreateGordonSurface` | |
| `FitSurface(points, controlPoints)` | `SmBSplineSurface::ApproximateRandomPoints`（复杂度 1） | 点投影到平面上分配参数 |
| `TrimSurface(surface, cutter, direction, keepPoint, split)` | `SmApiTrimProjectParallel` | 成功时副本被接管；失败时副本归调用方 |
| `JoinSurfaces(bodies, tol)` | `SmBrep::CopyFaces`（不合并）+ `SmApiStitchIntoSolid`，不成实体时 `SmApiStitchAdvanced` | |
| `CapPlanarHoles(body)` | 边界边（`SmEdge::IsLamina`）按顶点连环，共面的环用 `SmApiCreatePlanarFaces` 补面后缝合 | 非平面开口跳过；一个也补不上时失败 |
| `ExplodeFaces(body)` | 每个面 `CopyFaces` 到新实体 | |
| `FlipSurface` / `ExtendSurface` / `RebuildSurface` | 面所在曲面的 B 样条副本：`Reverse(SM_SP_U)` / `CreateExtendedSurface(G1)` / `RebuildSurface` | 只用于单个面；按整张曲面计算，面被修剪过（面积与整张曲面不同）时失败 |
| `EdgeCurves(body)` | 边的曲线转成 B 样条（`MakeExactBSplineIfPossible`）后 `Trim` 到边的区间 | 顺序同 `Edges()` |
| `IsoCurve(surface, face, alongU, t)` | `SmSurface::CreateIsoParametricCurve` | 按整张曲面，不按修剪 |
| `ClosestPoint` / `IntersectCurve` / `IntersectBodies` | `SmApiBrepClosestPoint` / `SmApiIntersectCurveBrep` / `SmApiIntersectBreps` | 没有交点时 SMLib 返回错误，适配层按空结果处理 |
| `SurfaceDerivatives(surface, face, u, v)` | `SmApiEvaluateSurfaceDerivatives` | 导数是对归一化参数的；曲率由节点用相邻参数处的一阶导数差分 |

已知问题：带洞的修剪面（如投影圆修剪后的方板）`SmApiBrepComputeArea` 的结果偏小约 3%，离散网格的面积正确。测试在 `NodeSolidTests` 的 `TestN8Surfaces`。

## 14. 空间曲线的修改（节点 N6b）

`docs/NodeGraph/PLAN.md` 第 4.5 节的曲线修改节点用到的接口（分割、子曲线、延伸、重建、同阶连接在节点侧的 `CurveOps` 里计算，不经内核）：

| 接口 | SMLib | 注意 |
|---|---|---|
| `OffsetCurve(curve, distance)` | `SmBSplineCurve::CreateSimpleOffset`（平面法向 +Z，逼近容差为曲线尺寸的 1e-6） | 曲线须在水平面内（节点先变换到曲线平面的坐标系）；SMLib 的正值偏向右侧，接口约定正值偏向左侧 |
| `JoinCurves(curves)` | `DegreeElevate` 到最高阶后 `SmBSplineCurve::CreateByJoining` | 曲线须已排好顺序、方向且首尾相接 |
| `ProjectCurve(curve, target, direction)` | `SmApiProjectCurve`（`CreateParallelProjectionCurves`） | 投影不到时 SMLib 返回错误，适配层按空结果处理 |

测试在 `NodeSolidTests` 的 `TestN6bCurves`。

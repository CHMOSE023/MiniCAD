# SMLib 对节点式参数化建模的能力核查

核查日期：2026-10-20。

结论：本地 SMLib 具有支撑现有节点式建模计划的实体与 NURBS 曲面基础。空间扫掠、封口放样、带洞轮廓、双轨蒙皮、Coons、Gordon、散点拟合、抽壳、投影修剪和网格转 BRep 均通过了代表性运行实验。主要工作是扩展 MiniCAD 适配接口、处理输入与拓扑，以及建立容差和失败语义；尚不能据此承诺任意复杂几何的稳健性。

## 1. 核查方法与证据范围

- 检查本地 `3rd/SMLib/SM_API` 高层接口及 `3rd/SMLib/SMLib` 核心源码，区分声明与实现。
- 运行现有 `SMLibSmokeTests.exe`、`SolidKernelTests.exe`、`NodeSolidTests.exe`，均以退出码 0 完成。
- 在 `out/smlib-capability-probe/` 新建独立探针，链接已有 Debug `SM_API.lib` 与 `SMLib.lib`；未重新编译第三方库。
- 探针有 11 个独立进程案例、26 项断言，最终全部通过；没有修改产品实现或原有测试。
- 探针、构建脚本、输出分别是 `probe.cpp`、`build.cmd`、`results.txt`。这些文件在构建输出目录内，清理 out 后可能消失。
- 此结论针对本地库与构建环境，不外推至其他 SMLib 版本或网页版。

## 2. 新增运行验证

| 能力 | 实际接口 | 案例与结果 |
|---|---|---|
| Coons 边界曲面 | `SmBSplineSurface::CreateCoonsPatch` | 非共面四边生成双线性曲面，25 个参数采样满足 `z=0.1xy`，最大采样残差 0 |
| Gordon 网络曲面 | `SmBSplineSurface::CreateGordonSurface` | 3×3 曲线网络，25 个参数采样满足 `z=0.1xy`，最大采样残差 0 |
| 双轨蒙皮 | `SmBSplineSurface::CreateSkinnedSurface` | 两条二次曲线轨道、三个横截面形成拱面；25 个参数采样符合解析抛物线，最大采样残差 0 |
| 封口放样 | `SmApiCreateSkinPrimitive` | 两个半径 5、高差 10 的圆；封闭实体，体积 785.398276157，符合 `250π` 的断言容差 |
| 闭合抽壳 | `SmApiShellBrepFull` | 10³ 长方体向内偏移 1，未选择开口面；材料体积 488，原对象仍为 1000 |
| 网格转 BRep | `SmBrep::CreateFromPolyBrep` | 长方体离散后转回 BRep；结果为封闭实体，体积 1000 |
| 带洞平面 | `SmApiCreatePlanarFaces` | 同心圆半径 5 和 2；圆环面积 65.9724471695，符合 `21π` |
| 散点拟合 | `SmBSplineSurface::ApproximateRandomPoints` | 打乱顺序的 25 个 `z=0.1xy` 点；4×4 控制点、三次曲面，输入点至拟合曲面的最大查询距离约 `3.62e-6` |
| 通用空间扫掠 | `SmApiCreateCurveSweep` | 半径 1 圆沿半径 5、高 10、一圈螺旋线扫掠并封口；体积 103.575254341，解析目标约 103.575429246 |
| 带洞拉伸 | `SmApiCreateLinearSweep` | 半径 5 和 2 的嵌套圆沿 Z 拉伸 10；封闭实体，体积 659.734487051，符合 `210π` |
| 投影修剪 | `SmApiTrimProjectParallel` | 将半径 2 圆投影到平面并保留中心区域；面积 12.5666442323，符合 `4π` 的断言容差 |

Coons、Gordon、双轨的“残差 0”仅指本案例的离散采样结果。散点拟合仅验证易于平面参数化的点集，不代表任意点云都能拟合成一张曲面。

## 3. 已有测试覆盖

现有内核与节点测试覆盖基本体、正负拉伸、旋转、水平路径扫掠、布尔、圆角、倒角、非均匀缩放、镜像、离散、边查询、隐藏线、截面、序列化、空间曲线插值和拟合、螺旋线、放样曲面、直纹面、旋转曲面、规则点阵曲面、偏移、加厚、管道等。

节点测试还覆盖曲面预览、烘焙撤销、MCAD/JSON 往返以及曲面导出 DWG/DXF 多面网格。已有测试通过表示其既定案例通过，不表示未测试输入都可靠。

## 4. 已找到实现，但本次未新增运行验证

| 功能 | 接入依据 | 待验证点 |
|---|---|---|
| 精确几何求交 | `SmApiIntersectCurves/Surfaces/Faces/Breps`、`SmApiIntersectCurveSurface/Face/Brep` | 相切、重合、跨接缝、修剪域、空结果语义 |
| 缝合 | `SmApiStitchIntoSolid/IntoShell/Advanced` | 有间隙的面、非流形、封闭判定、容差 |
| 最近点、距离、关系 | `SmApiQueries.h` 中曲线/曲面/面/边/BRep 查询 | 修剪域、多个最近点、退化输入 |
| 曲率 | `SmSurface::EvaluateGeometric` | 奇点、主曲率方向与面朝向 |
| 等参线 | `SmBSplineSurface::CreateIsoParametricCurve` | 基础曲面等参线与修剪后面内片段的区别 |
| 曲面延伸与重建 | `SmBSplineSurface::CreateExtendedSurface/RebuildSurface` | 保持边界、误差、修剪后的拓扑重建 |
| 多边形布尔 | `SmApiPolyBoolean*` | 非流形、自交和精度；也不等于网页版已可用 |
| 网格简化 | `SmPolyDecimate`、`SmPolyBrep::CleanSolidMesh` | 质量、边界保持、目标面数控制 |
| 圆锥与圆环 | `SmApiCreateCone/Torus` | 参数范围和退化情况 |
| 几何修复 | `SmApiHealBrep` | 具体能修复哪些错误、是否改变几何 |

未完整验证通用封口、炸开、曲面切实体后的多块输出，以及完整的放样融合语义。不能仅从相邻接口推断这些节点已完成。

## 5. 已确认的接入限制

1. **Coons 边界顺序**：实现按左、右、下、上读取四条曲线，并要求对边方向相容。最初按首尾相接顺序输入时返回成功，但解析形状检查失败；修正顺序后通过。节点层应整理边界并验证结果。
2. **双轨约束**：`CreateSkinnedSurface` 要求两轨同步，并提供截面与轨道交点的参数。它有真实双轨实现，但仍需要方向、参数和截面对应处理。
3. **散点参数化**：底层拟合支持通过可选边界曲面或平面投影分配 UV。闭合点云、折叠点集或多层数据不能默认用单张曲面表达。
4. **修剪所有权**：`SmApiTrimSurfaceWith3dCurves` 和 `SmApiTrimProjectParallel` 的头文件明确说明输入可能被消耗，失败路径还存在所有权不确定或内部对象未释放的情况。接入不可变 `BodyPtr` 前需私有副本和失败清理策略。
5. **容差**：部分高层接口内部固定容差，例如三维曲线修剪使用 0.01；曲线/面求交路径有 0.001。应按文档单位和模型尺度审查，必要时使用底层可配置接口。
6. **求交域语义**：曲线/面的重合区间，头文件说明按中点在修剪面内与否整段保留或丢弃，不自动裁成面内片段。完整节点功能需要补处理。
7. **线程**：本地静态库使用全局上下文路径，项目约定内核只能在一个线程中使用；并发调用及跨线程对象生命周期需统一管理。
8. **拓扑身份**：`SmPrimitiveCreation` 有部分构造拓扑映射，可作为研究基础；尚不能据此认定跨布尔、编辑和重算的稳定拓扑命名已解决。
9. **网格转换含义**：`CreateFromPolyBrep` 按多边形建立面并缝合，支持多面 BRep，不会自动恢复原来的光滑 NURBS 曲面。
10. **网页**：当前 CMake 对 Emscripten 默认关闭内核。桌面原生能力不代表浏览器已有相同能力。

## 6. 对开发计划的建议

- N7 可直接研究 `SmApiCreateCurveSweep`、`SmApiCreateSkinPrimitive`、`SmApiShellBrepFull` 与带洞线性扫掠，不必把这些都列为“没有实现途径”。
- N8 优先对接真实 Coons、Gordon、双轨蒙皮、散点拟合，不宜默认用采样点阵替代其语义。
- 精确求交已有接口与实现，应先验证原生接口，网格求交作为明确的近似选项。
- N9 网格转实体可走 `SmBrep::CreateFromPolyBrep` 或 `CreateFromTriangleBag`，先做输入验证、小模型和后续布尔测试。
- 仍需单独开发节点图、参数公式、组定义、数据分组、交互选择、引用管理、输出与烘焙更新；几何内核不提供这些应用功能。
- 下一批测试优先补复杂修剪/缝合、开口抽壳、非圆截面空间扫掠、曲面切实体、退化与失败清理、大面数性能。通过后再扩大节点的公开支持范围。

## 7. 第二轮挖掘：计划外节点候选

以下是本地接口与实现检查得到的候选，不表示节点已经实现，也不属于第 2 节已运行的 11 个案例。节点 ID 为建议值，实施前需检查注册表冲突。已有计划中的普通圆角、抽壳、曲面重建、基本求交等不重复列出。

| 建议节点 | 建议 ID | SMLib 依据 | 输入与输出约定要点 |
|---|---|---|---|
| 拔模拉伸 | `solid.taperextrude` | `SmApiCreateTaperExtrude` | 共面轮廓、高度、拔模角、封口与角部形式 → Body；需转换角度约定 |
| 金字塔 | `solid.pyramid` | `SmApiCreatePyramid` | 基点、底边长、高 → Body |
| 扇形环柱 | `solid.annularsector` | `SmApiCreateCylindricalBox` | 内外半径、高度、起止角 → Body |
| 扇形基本体 | 现有基本体增加角度选项 | `SmApiCreatePartialSphere/Cone/Cylinder/Torus` | 参数化部分球、圆锥、圆柱、圆环；避免无必要地复制节点 |
| 斜端圆锥 | `solid.tiltedcone` | `SmApiCreateConeEllipticEnds` | 两端半径、高、两端倾角、封口 → Body |
| 双轮廓成形（Swung） | `solid.swung` | `SmApiCreateSwungPrimitive` | XY/XZ 两组轮廓与比例 → Body；需验证适用形状与节点命名 |
| 逐边圆角 | 现有圆角增加模式 | `SmApiFilletEdgesPerEdge` | 边选择与一一对应的半径 → Body |
| 变半径圆角 | `solid.variablefillet` | `SmApiVariableRadiusFillet` | 边选择、起终半径、截面形式 → Body；当前 API 使用线性半径律 |
| 高阶连续过渡 | 圆角节点高级选项 | `SmApiFilletEdges` 的 blend curve 与 continuity | G1/G2/G3 选项；头文件说明三边角点边界仅保证 G1 |
| 曲面间圆角 | `surface.fillet` | `SmApiSurfaceSurfaceFillet` | 两基础曲面、半径、容差、侧别 → 过渡 Body；修剪面应区分基础曲面与有效域 |
| 移除圆角 | `solid.removefillet` | `SmApiRemoveFillet` | Body → Body；需验证识别与删除失败情况，不能保证任意导入圆角可恢复 |
| 实体整体偏移 | `solid.offsetbody` | `SmApiOffsetBrepFull` | Body、距离、自交与延伸设置 → Body |
| 质量属性 | `solid.massproperties` | `SmApiBrepComputeMassProperties` | Body、密度、惯性参考原点 → 面积、体积、质量、质心、惯性矩和惯性积 |
| 实体关系 | `solid.relationship` | `SmApiBrepRelationship` | 两 Body → 关系、距离；关系枚举应按实际源码映射 |
| 实体距离 | `solid.distance` | `SmApiBrepDistance` | 两 Body → 距离；无需重新实现距离算法 |
| 壳体与空腔统计 | `solid.materialcensus` | `SmApiBrepMaterialCensus` | Body → 材料/空腔统计；具体输出按接口语义命名 |
| 面积中心 | `surface.centroid` | `SmApiFaceComputeCentroid` | 单面 Body → 质心/面积；多面应定义面积加权规则 |
| 面内点 | `surface.interiorpoint` | `SmApiFaceInternalPoint` | 单面 Body → 面内点，用于选择保留区域等 |
| 面参数域判断 | `surface.containsuv` | `SmApiFaceClassifyUV` | 单面 Body、UV → 内/外/边界；不是空间点包含判断 |
| 平滑连接曲线 | `nurbs.smoothjoin` | `SmBSplineCurve::SmoothJoin` | 两曲线、连接端、端部保留系数 → 修改后的两曲线；原接口修改输入，需先复制 |
| 曲线升阶/降阶 | `nurbs.degree` | `DegreeElevate/DegreeReduction` | 曲线、阶数/容差 → 曲线；升阶与近似降阶语义分开 |
| 曲线节点编辑 | `nurbs.knots` | `InsertKnots/RemoveKnots` | 曲线、节点参数、操作模式/容差 → 曲线 |
| 弧长参数化 | `nurbs.arclengthparam` | `RebuildCurveWithArcLengthParam/ReparametrizeWithArcLength` | 曲线、误差约束 → 曲线；不承诺所有曲线都能精确弧长参数化 |
| 曲线过指定点 | `nurbs.forcethrough` | `ForceThroughPoint` | 曲线、目标点与局部控制参数 → 曲线 |
| 网格清理 | `mesh.clean` | `RemoveDegenerateFaces/RemoveCoincidentFaces/RemoveInternalFaces` | Mesh、清理选项 → Mesh；需测试后公开具体选项 |
| 网格平面裁切 | `mesh.trim` | `SmPolyBrep::TrimWithPlane` | Mesh、平面、保留侧 → Mesh；闭合封口行为需验证 |
| 网格平滑 | 现有计划的 `mesh.smooth` 可复用内核实现 | `SmPolyBrep::SmoothPolygons` | Mesh、迭代次数、步长 → Mesh；边界锁定另验证 |
| 网格四边形化 | `mesh.quadmesh` | `TessellateWithQuads` | Mesh、尺寸与清理选项 → Mesh；仅为候选，非任意网格高质量全四边重拓扑保证 |
| 几何修复 | `solid.heal` | `SmApiHealBrep` | Body → Body、状态；修复范围与失败语义须实验确定 |

封装优先顺序：先质量属性、距离、关系与简单基本体，再做拔模拉伸、整体偏移和曲线工具；需要面/边选择或新 Mesh 表达的节点随后接入。

产品节点应通过 MiniCAD 类型进入 `ISolidKernel`，不要在 `Builtin*Nodes.cpp` 暴露第三方指针。修改类接口先复制输入，查询类共享输入。网格四边形输出与当前三角 Mesh 有数据表示差异，需先决定保留四边形还是三角化；SMLib 网格实现也不自动解决网页版能力。

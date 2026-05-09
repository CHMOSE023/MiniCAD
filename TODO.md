# MiniCAD 开发 TODO

> 基于 `src/` 实际代码 × `README.md` 规划
> 6 个阶段 · 25 项任务

---

## P0 · 立即修复 — 清理现有代码

> 预估工期：1 ~ 2 天
> 消除已知的 bug 和死代码，不新增功能，让主干保持干净可编译。

- [x] **P0-1** 修复 `StartPointTool` 的复制粘贴 bug
  - 文件：`EditorContext.cpp`
  - `StartPointTool()` 里误打 `"[Editor] Start LineTool"`，改为 `"[Editor] Start PointTool"`
  - 预估：10 分钟 · 风险：低

- [x] **P0-2** 清理死代码 `InputSystem`
  - 文件：`MainWindow.h` / `MainWindow.cpp`
  - `m_inputSystem` 声明但从未调用，`EventProc` 里也没有接入
  - 选择：① 彻底删除；② 等平台层抽象时按 `Platform/IInput` 接口重新设计
  - 推荐先删除
  - 预估：30 分钟 · 风险：低

- [ ] **P0-3** 实现 `SaveToFile` / `Open`
  - 文件：`Document.cpp` / `DocumentManager.cpp`
  - `SaveToFile` 目前是空壳（TODO 注释），`Open` / `CopySelected` 是 printf 占位 
  - 先实现最简单的自定义二进制格式：写入实体数量 + 每个实体的类型标识 + 坐标数据
  - 对齐 README 中 `NativeFormat` 的方向
  - 预估：1 天 · 风险：中

- [x] **P0-4** 统一 Redo 快捷键大小写 
  - 文件：`InputEvent.h`
  - `IsRedo()` 检测 `'Y'` 和 `'y'`，但 `ViewportInputAdapter` 只注册了大写 VK 码
  - 确认 `setKeyState` 里 `'Y'` 和 `'Z'` 对应的 `ImGuiKey` 是否覆盖小写场景，必要时补齐
  - 抽象编辑器帧消息输入， Abstraction Editor Input Framework 。
  - 预估：30 分钟 · 风险：低

---

## P1 · 阶段 4 — 稳定化基础设施

> 预估工期：1 ~ 2 周
> README 阶段 4 的全部内容。这些是后续所有功能的地基，跳过会导致越来越多的边界 bug。

- [ ] **P1-1** `ToolStateMachine` — 工具状态协议
  - 文件：新建 `Editor/Tools/ToolStateMachine.h`
  - 现在 `m_tool` 是裸 `unique_ptr`，切换工具只做 `reset()`，没有 suspend/resume 语义
  - 实现 `IToolState` 枚举（`Idle` / `Active` / `Suspended` / `Finished`）和 `ToolStateMachine`
  - 统一处理：工具切换时先调 `Cancel()`；Undo 打断绘制中工具；Snap 失效时的状态回退
  - 避免当前「画线中按 Undo，Overlay 不清空」等边界 bug
  - 预估：3 天 · 风险：中

- [ ] **P1-2** `TwoPhaseExecutor` — Command 两阶段提交
  - 文件：新建 `Document/Transaction/TwoPhaseExecutor.h`
  - 当前 Execute = 直接写入，没有 `prepare()` 校验阶段
  - 实现：① `prepare()` 做合法性校验，失败直接返回 `Failure`，不写入任何数据；② `commit()` 写入；③ 失败时自动 `rollback()`
  - `CommandResult<T>`（`Success` / `Failure` / `PartialFailure`）一并实现，替换目前 `void` 返回
  - 预估：3 天 · 风险：中

- [ ] **P1-3** `EventBus` — per-Document 事件总线
  - 文件：新建 `Document/Runtime/EventBus.h` / `EventBus.cpp`
  - 当前脏标记是直接在 Scene/Picking 上设布尔值，耦合严重
  - 实现泛型 EventBus：`Subscribe<T>(handler)` / `Publish<T>(event)`，类型擦除用 `std::function`
  - Document 持有独立实例（非全局单例）
  - 事件类型先实现：`EntityAdded` / `EntityRemoved` / `EntityModified` / `SelectionChanged`
  - 后续 `Render/DirtyPropagator` 订阅此总线触发增量重建
  - 预估：2 天 · 风险：中

- [ ] **P1-4** `EntityHandle` — ID + 版本号句柄
  - 文件：新建 `Scene/EntityHandle.h`，改造 `Scene`
  - 当前用裸 `ObjectID (uint64_t)` 当句柄，实体删除后 ID 悬空
  - `EntityHandle = { ObjectID id; uint32_t version; }`
  - `EntityDatabase` 对每个槽维护 version 计数器，删除时递增
  - `GetEntity(handle)` 版本不匹配返回 `nullptr`
  - `GripEditor` / `Picking` 里所有存 `ObjectID` 的地方迁移到 `EntityHandle`
  - 预估：2 天 · 风险：高

- [ ] **P1-5** `SelectionSet` 迁移至 `Editor/Context`
  - 文件：新建 `Editor/Context/SelectionSet.h`，`Picking` 解耦
  - 当前选中集合存在 `Picking` 内部（`m_selection`），`EditorContext` 要通过 `GetPicking().GetSelection()` 拿数据
  - README 明确选择状态属于视图层（per-Viewport），应移到 `EditorContext`
  - `Picking` 改为只做「命中测试」，结果回调给 `EditorContext` 更新 `SelectionSet`
  - 预估：2 天 · 风险：中

---

## P2 · 阶段 5A — 补全绘图 / 修改工具

> 预估工期：2 ~ 3 周
> 把 `EditorContext` 里所有 printf 占位工具变成真实实现，按使用频率排序。

- [ ] **P2-1** `Circle` 工具（圆）
  - 文件：新建 `Editor/Tools/Draw/CircleTool.h`
  - 三步状态机：① 点击圆心；② 移动预览半径；③ 点击确认
  - Commit 生成 `CircleEntity`（需先在 `Core/GeomKernel` 补 `Circle.hpp`）
  - 吸附和正交约束复用现有 `SnapEngine` + `ApplyConstraints`
  - 预估：2 天 · 风险：低

- [ ] **P2-2** `Rectangle` 工具（矩形）
  - 文件：新建 `Editor/Tools/Draw/RectangleTool.h`
  - 两点对角线定义矩形，生成 4 条 `LineEntity`（复用 `BatchAddCommand`）
  - 预估：1 天 · 风险：低

- [ ] **P2-3** `Arc` 工具（圆弧）
  - 文件：新建 `Editor/Tools/Draw/ArcTool.h`，`Core/GeomKernel/Arc.hpp`
  - 三点定弧（起点 → 终点 → 弧上一点），先实现三点模式
  - 需补 `ArcEntity` 和弧线的屏幕渲染（细分为折线段）
  - 预估：3 天 · 风险：中

- [ ] **P2-4** `Polyline` 工具（多段线）
  - 文件：新建 `Editor/Tools/Draw/PolylineTool.h`，`Core/Entity/PolylineEntity.hpp`
  - 连续点击追加节点，右键结束
  - Commit 一次性生成 `PolylineEntity`（内部存 `vector<XMFLOAT3>`），便于后续整体 Grip 编辑
  - 预估：2 天 · 风险：低

- [ ] **P2-5** `Move` 工具（移动）
  - 文件：新建 `Editor/Tools/Modify/MoveTool.h`
  - 基于当前 `SelectionSet`，两步：① 点击基点；② 点击目标点
  - 生成 `DragEntitiesCommand`（已有），复用正交约束和 Snap
  - 预估：2 天 · 风险：低

- [ ] **P2-6** `Copy` 工具（复制）
  - 文件：新建 `Editor/Tools/Modify/CopyTool.h`
  - 与 Move 逻辑相同，区别是生成 `BatchAddCommand`（克隆选中实体并偏移）
  - 可抽取 `MoveBase` 基类与 `MoveTool` 共享交互逻辑
  - 预估：1 天 · 风险：低

- [ ] **P2-7** `SnapEngine` 插件化重构
  - 文件：新建 `Editor/Snap/ISnapStrategy.h`，重构 `SnapEngine`
  - 当前 4 种吸附硬编码在 `SnapEngine` 内部
  - 重构为：`SnapEngine` 持有 `vector<ISnapStrategy*>`，`Query()` 按优先级遍历
  - 4 种策略分别提取为：`EndpointSnap` / `MidpointSnap` / `NearestSnap` / `GridSnap`
  - 对外接口不变，改动对 `EditorContext` 透明
  - 预估：2 天 · 风险：低

---

## P3 · 阶段 5B — 渲染架构升级

> 预估工期：2 ~ 3 周
> 把当前 D3D11 硬耦合的渲染层抽象为 `IRenderer`/`IDevice` 接口，为后续多后端铺路，同时实现增量重建优化。

- [ ] **P3-1** `IRenderer` / `IDevice` 接口抽象
  - 文件：新建 `Render/Core/IRenderer.h`，`IDevice.h`，`RenderTypes.h`
  - 从现有 `Renderer` / `Device` 中提取纯虚接口
  - `IDevice`：创建资源（Buffer / Texture / Shader / Pipeline）
  - `IRenderer`：帧循环（`BeginFrame` / `EndFrame` / `Submit`）
  - D3D11 实现挪到 `Render/Backend/D3D11/` 并实现接口
  - `Document` 和 `Viewport` 改持 `IRenderer*` 而非 `Renderer*`
  - 预估：3 天 · 风险：高

- [ ] **P3-2** `RenderScene` / `RenderEntity` 分离
  - 文件：新建 `Render/Scene/RenderScene.h`，`RenderEntity.h`，`RenderBuilder.h`
  - 当前 `Document::UpdateSceneVertices()` 直接把 Scene 实体展开成顶点数组
  - `RenderBuilder`：把 `Scene::Entity` → `RenderEntity`（GPU 可消费结构）
  - `RenderScene`：持有扁平 `RenderEntity` 列表
  - `Renderer` 只消费 `RenderScene`，不再感知 `Scene`
  - 预估：3 天 · 风险：中

- [ ] **P3-3** `DirtyPropagator` — 增量重建
  - 文件：新建 `Render/Sync/DirtyPropagator.h`
  - 当前每次 Scene 脏都全量重建所有顶点
  - 订阅 `EventBus` 的 `EntityAdded` / `EntityModified` / `EntityRemoved`
  - 只对变化的实体调用 `RenderBuilder` 增量更新对应 `RenderEntity`
  - 依赖 **P1-3 EventBus** 先完成
  - 预估：2 天 · 风险：中

- [ ] **P3-4** `RenderSyncBuffer` — 双缓冲
  - 文件：新建 `Render/Sync/RenderSyncBuffer.h`
  - 当前是单线程，先用简单 swap 模拟双缓冲语义
  - 数据结构上预留 `mutex` + `atomic`，真正的多线程渲染等平台层稳定后再启用
  - 预估：2 天 · 风险：中

---

## P4 · 阶段 5C — 文件 IO

> 预估工期：1 ~ 2 周
> 实现文件读写，让 Save / Open 真正可用。

- [ ] **P4-1** `Serializer` 主框架 + `FileFormat` 接口
  - 文件：新建 `Document/IO/Serializer.h` / `Serializer.cpp`，`FileFormat.h`
  - `Serializer` 遍历 Scene，按实体类型分发到注册的 Format 驱动（策略模式）
  - `FileFormat` 接口：`read(path) → Scene`；`write(Scene, path)`
  - 新增格式只加文件不改主流程
  - 预估：1 天 · 风险：低

- [ ] **P4-2** `NativeFormat` — 原生二进制格式
  - 文件：新建 `Document/IO/Formats/NativeFormat.h` / `NativeFormat.cpp`
  - 格式规范：① 文件头（魔数 + 版本号）；② 实体数量；③ 每个实体（类型 ID + 属性 + 坐标）
  - 读到未知类型时跳过而非崩溃（向前兼容）
  - 这是 `DocumentManager::Save` / `Open` 的实际实现
  - 预估：2 天 · 风险：低

- [ ] **P4-3** `SVGFormat` — SVG 导出
  - 文件：新建 `Document/IO/Formats/SVGFormat.h` / `SVGFormat.cpp`
  - 只做写入（导出）
  - `LineEntity` → `<line>`，`CircleEntity` → `<circle>`，`ArcEntity` → `<path d='A...'>`
  - 用 `std::string` 拼 XML，不引入第三方 XML 库
  - 预估：2 天 · 风险：低

- [ ] **P4-4** `DXFFormat` — DXF 格式支持（子集）
  - 文件：新建 `Document/IO/Formats/DXFFormat.h` / `DXFFormat.cpp`
  - 优先支持 R2000 的 LINE / CIRCLE / ARC 实体读写，LAYER 表
  - 跳过 BLOCK / XREF 等复杂结构
  - 可使用开源 dxflib 或手写简单解析器
  - 预估：1 周 · 风险：高

---

## P5 · 阶段 5D — 约束系统

> 预估工期：2 ~ 4 周
> 参数化建模的核心，数学密度最高的部分。建议在 IO 稳定后再启动。

- [ ] **P5-1** `IConstraint` 接口 + `CoincidentConstraint`
  - 文件：新建 `Document/Constraint/IConstraint.h`，`Constraints/CoincidentConstraint.h`
  - 先实现最简单的重合约束（两点坐标相等）
  - `IConstraint::evaluate()` 返回残差向量，用于后续 Solver 迭代
  - 重合约束可不进求解器，直接在 Command 执行时强制赋值，是验证框架的最低成本路径
  - 预估：2 天 · 风险：低

- [ ] **P5-2** `DOFAnalyzer` — 自由度分析
  - 文件：新建 `Document/Constraint/DOFAnalyzer.h`
  - 在调用 Solver 前检测欠约束（方程数 < 未知数）和过约束（方程数 > 未知数）
  - 用清晰的错误信息报告给用户，阻止病态方程组进入 Solver
  - 预估：2 天 · 风险：中

- [ ] **P5-3** `ConstraintSolver` — 基础求解器
  - 文件：新建 `Document/Constraint/ConstraintSolver.h` / `ConstraintSolver.cpp`
  - 用 Newton-Raphson 迭代求解恰定方程组
  - 仅在 `DOFAnalyzer` 前置校验通过后调用
  - 建议引入 Eigen 处理稀疏线性方程组（CMake 引入）
  - 预估：1 周 · 风险：高

- [ ] **P5-4** `Parallel` / `Perpendicular` 约束
  - 文件：新建 `Constraints/ParallelConstraint.h`，`PerpendicularConstraint.h`
  - 平行约束：两线段方向向量叉积为零
  - 垂直约束：两线段方向向量点积为零
  - 实现 `evaluate()` 残差函数，接入 Solver 框架
  - UI 侧在属性面板显示已有约束并提供添加 / 删除入口
  - 预估：3 天 · 风险：中

---

## P6 · 阶段 6 — 平台抽象 + 跨平台

> 预估工期：4 ~ 8 周
> 最后阶段，依赖前面全部稳定。Platform 抽象必须先于 Web / Vulkan 后端。

- [ ] **P6-1** Platform 接口层
  - 文件：新建 `Platform/IWindow.h`，`IInput.h`，`IFileSystem.h` 等
  - 提取 `MainWindow` 里的 Win32 调用为接口：`IWindow` / `IInput` / `IFileSystem`
  - Desktop/Win32 实现迁移现有代码，不改行为
  - 这是 Wasm 和 macOS 的前提
  - 预估：1 周 · 风险：中

- [ ] **P6-2** Plugin 框架骨架
  - 文件：新建 `Plugin/IPlugin.h`，`PluginManager.h` / `PluginManager.cpp`，`SDK/PublicAPI.h`
  - `IPlugin` 接口：`load()` / `unload()` / `getMetadata()`
  - `PluginManager`：动态加载 `.dll` / `.so`，版本校验（`MINICAD_API_VERSION` 宏）
  - `PublicAPI.h` 只暴露稳定接口，不暴露内部 STL 容器（ABI 稳定性要求）
  - 先实现框架骨架，空插件可加载即可
  - 预估：3 天 · 风险：中

- [ ] **P6-3** Vulkan 后端
  - 文件：新建 `Render/Backend/Vulkan/`
  - 约 10,000 行，是所有后端里最复杂的（同步原语、RenderPass、描述符集）
  - 依赖 **P3-1 IRenderer/IDevice 接口** 先完成
  - 建议参考 vk-bootstrap 简化初始化代码
  - 预估：3 ~ 4 周 · 风险：高

- [ ] **P6-4** WebAssembly + WebGPU 后端
  - 文件：新建 `App/MainWasm.cpp`，`Platform/Web/`，`Render/Backend/WebGPU/`
  - Emscripten 编译，注意限制：无默认线程模型（需 SharedArrayBuffer + COOP/COEP 头）、文件系统用 MEMFS 或 IDBFS
  - WebGPU 后端约 5,000 行，WebGL 2.0 作降级方案
  - 预估：4 ~ 6 周 · 风险：高

---

_生成日期：2026-05-09_

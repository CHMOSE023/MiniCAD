# MiniCAD
  
一个轻量二维CAD。
 
---

## 目录

1. [项目简介](#1-项目简介)
2. [必备知识](#2-必备知识 (C++))
3. [代码预估](#3-代码预估)
4. [开发进度](#4-开发进度)
5. [文件目录](#5-文件目录)

---

## 1. 项目简介

MiniCAD 是一个面向工程师和开发者的**跨平台 CAD 编辑器框架**，目标是在桌面端（Windows / macOS / Linux）和 Web 端（WebAssembly）运行同一套核心代码，提供专业级的二维几何编辑能力。

### 核心设计目标

| 目标 | 说明 |
|---|---|
| 跨平台 | Desktop（D3D11 / Vulkan）+ Web（WebGPU / WebGL），单一代码库 |
| 可撤销性 | 所有修改通过 Command 模式进行，完整 Undo / Redo 支持 |
| 数据安全 | Scene 层只读，唯一写入口为 Document 层，防止数据竞态 |
| 可扩展性 | 插件系统支持自定义 Tool、Command、吸附策略、实体类型 |
| 参数化建模 | 几何约束求解器支持平行、垂直、重合等约束关系 |

### 架构分层概览

```
┌─────────────────────────────────────┐
│              App 启动层              │  组装所有模块，不含业务逻辑
├──────────────┬──────────────────────┤
│  Editor 交互层│    Document 内核      │  只产"意图" ←→ 唯一写入口
├──────────────┴──────────────────────┤
│              Scene 数据层            │  只读运行时快照
├──────────────┬──────────────────────┤
│  Core 计算库  │    Render 渲染层      │  纯函数 ←→ RenderScene 快照
├──────────────┴──────────────────────┤
│   Platform 系统抽象  │  UI 层         │  OS 差异隔离 ←→ ImGui
└─────────────────────────────────────┘
```

### 关键技术决策

- **EntityHandle**：用 ID + 版本号替代裸指针，实体删除后句柄自动失效，消除悬空指针
- **TwoPhaseExecutor**：Command 执行分 `prepare()`（校验）和 `commit()`（写入），失败自动 `rollback()`，消除 partial execution
- **per-Document EventBus**：事件总线非全局单例，每个 Document 独立持有，多文档并行编辑不互串
- **RenderSyncBuffer**：双缓冲隔离 Game thread（写）和 Render thread（读），消除渲染并发竞态
- **DOFAnalyzer**：约束求解前的前置校验，欠约束/过约束在进入 Solver 前即检测并报告，不让病态方程组进入求解器
- **SnapEngine 插件化**：吸附策略通过 `ISnapStrategy` 注册，运行时可启停，插件可扩展
- **ToolStateMachine**：统一处理工具切换、Undo 打断、Snap 失效等状态笛卡尔积场景
- **GripController**：Grip 拖拽生命周期统一管理，完成后走 `CommandBridge` 生成 `ModifyEntityCommand`，不绕过 Command 体系

---

## 2. 必备知识 (C++)

### 基础层（不具备则无法开始）

- 类、继承、虚函数、纯虚接口
- RAII、构造/析构顺序、拷贝/移动语义
- 模板基础：函数模板、类模板、特化
- STL 容器与算法：`vector`、`unordered_map`、`std::sort`、迭代器
- 智能指针：`unique_ptr`、`shared_ptr`、`weak_ptr` 及其所有权语义

### 中级层（本项目的日常工具）

- **模板进阶**：`std::variant`、`std::optional`、`if constexpr`、SFINAE 基础
  > `CommandResult`、`EntityHandle` 查询返回空值都会用到

- **并发**：`std::mutex`、`std::atomic`、`std::thread`、memory order 基础
  > `RenderSyncBuffer` 双缓冲交换是多线程临界区

- **设计模式的 C++ 惯用法**：Command、Observer、Strategy、Factory 的现代实现
  > 整个 Document + Editor 层的骨架

- **虚函数表与接口设计**：纯虚接口 + `override` + Pimpl / 前向声明
  > `IRenderer`、`ITool`、`ISnapStrategy` 都是这个模型

- **内存布局意识**：SoA vs AoS、cache line、内存对齐
  > `EntityDatabase` 和 `RenderEntity` 的存储方式直接影响遍历性能

- **CMake 工程管理**：`target_link_libraries`、接口库、编译选项隔离
  > 400+ 文件如果不会 CMake 会很痛苦

### 高级层（本项目的真实难点）

- **类型擦除**：`std::function`、手写 type-erased wrapper
  > `EventBus` 的泛型订阅机制需要这个

- **编译期计算**：`constexpr`、`consteval`、编译期类型列表
  > `CommandFactory` 的自动注册可以利用，不用也能写但会很冗余

- **ABI 稳定性**：不在公开头文件暴露 STL 容器、虚析构、符号可见性
  > `Plugin/SDK/PublicAPI.h` 的核心约束

- **平台差异处理**：`#ifdef` 隔离、Emscripten 限制（无默认线程、虚拟文件系统）
  > `Platform/Web/` 有很多反直觉的地方

- **图形 API 底层**：至少精通一个后端（D3D11 或 WebGL）才能写 `IDevice` 接口
  > 否则接口设计会不断被实现倒逼修改

- **数值方法基础**：牛顿迭代、最小二乘、稀疏线性方程组
  > `ConstraintSolver` 的核心，纯软件工程背景在这里会卡住

---

## 3. 代码预估

> 只计 `.h` + `.cpp` 业务代码，不含第三方库和生成文件。

| 模块 | 文件数（估） | 代码行数（估） | 说明 |
|---|---|---|---|
| Core | 40 | 8,000 | 纯算法，逻辑密度高但边界清晰 |
| Scene | 12 | 2,500 | 数据容器为主，逻辑少 |
| Document/Transaction | 18 | 4,000 | Command 模式模板代码多 |
| Document/Constraint | 12 | 5,500 | 求解器 + DOFAnalyzer 是数学密集区 |
| Document/IO | 12 | 6,000 | DXF 格式解析本身就是大工程 |
| Document/Runtime | 6 | 1,500 | EventBus + DirtyTracker 不复杂 |
| Editor/Tools | 60 | 15,000 | 工具最多，每个 Tool 都有状态逻辑 |
| Editor/Snap + Picking | 10 | 2,500 | 策略模式，结构清晰 |
| Editor/Grip | 6 | 1,500 | 拖拽生命周期 + 屏幕绘制 |
| Editor/其他 | 15 | 3,500 | Viewport、Camera、Annotation 等 |
| Render/Core + Scene | 14 | 3,000 | 接口定义为主 |
| Render/Graph + Passes | 16 | 5,000 | RenderGraph 编译逻辑有复杂度 |
| Render/Backend × 4 | 80 | 25,000 | D3D11/WebGL 各约 5,000；Vulkan 约 10,000；WebGPU 约 5,000 |
| Platform × 2 | 20 | 4,000 | 接口薄，实现跟平台 API 走 |
| UI | 30 | 6,000 | ImGui 面板代码量线性增长 |
| Plugin | 8 | 1,500 | 框架骨架，初期不多 |
| App | 4 | 500 | 组装代码 |
| Tests | 60 | 12,000 | 按 1:3 测试覆盖比估算 |
| **合计** | **~423** | **~107,000** | 上限按 120,000 行规划 |

### 分阶段代码量

| 阶段 | 累计代码量 | 描述 |
|---|---|---|
| MVP | ~35,000 行 | Core + Scene + Document + 基础 Editor + 单后端 |
| 完整桌面版 | ~70,000 行 | 全部桌面后端，不含 Web |
| 完整双端版 | ~107,000 行 | Desktop + WebAssembly 全功能 |
 

---

## 4. 开发进度
 

### 阶段规划

```
[×]  阶段 0  基础设施                              
[×]  ├── CMake 多目标工程搭建
[×]  ├── Core/Math + Core/Geom 实现与测试
[×]  ├── Core/Base（Result<T>、错误码）
[×]  └── Platform 接口 + Desktop 实现（窗口 + 输入）
     
[·]  阶段 1  数据层 MVP                             
[√]  ├── Scene（EntityDatabase + EntityHandle + Layer）
[√]  ├── Document（CommandStack + 3 个基础 Command）
[×]  ├── EventBus（per-Document）+ DirtyTracker
[×]  └── Tests/Document/TransactionTests
     
[√]  阶段 2  第一个可见结果                          
[√]  ├── Render/Backend 单后端（D3D11 或 WebGL 选一个）
[√]  ├── Render/Scene（RenderBuilder 全量版，先不增量）
[√]  ├── Render/Graph（先平铺 Pass，不做 DAG）
[√]  ├── Editor/Viewport + Camera
[√]  └── 能在窗口里显示一个线段
     
[√]  阶段 3  可交互 MVP                             
[√]  ├── Editor/Tools/Draw（Line、Circle）
[√]  ├── Editor/Snap（Endpoint + Grid）
[√]  ├── Editor/Picking
[√]  ├── Editor/Grip（IGripPoint + GripController + GripRenderer）
[√]  ├── CommandBridge
[√]  ├── Undo/Redo 可用
[√]  └── UI 基础面板（属性 + 图层）
     
[×]  阶段 4  稳定化                                 
[×]  ├── TwoPhaseExecutor（补 partial exec 防护）
[×]  ├── Render/Sync（双缓冲 + DirtyPropagator 增量重建）
[×]  ├── SelectionSet 迁移至 Editor/Context
[×]  ├── ToolStateMachine（补状态协议）
[×]  └── Tests/Integration 第一批
     
[×]  阶段 5  功能完善                              
[×]  ├── Modify 类 Tool（Move、Rotate、Trim）
[×]  ├── Document/Constraint（DOFAnalyzer + 基础 Solver）
[×]  ├── Document/IO/Formats（Native + SVG）
[×]  ├── 第二渲染后端（WebGPU 或 Vulkan）
[×]  └── Plugin 框架骨架
     
[×]  阶段 6  双端 + 发布质量                         
[×]  ├── Platform/Web + MainWasm.cpp
[×]  ├── 剩余渲染后端
[×]  ├── DXF 格式支持
[×]  ├── 完整 Tests 覆盖
[×]  └── 性能剖析与优化
```


### 里程碑

| 里程碑 | 进度 | 状态描述 |
|---|---|---|
| 能显示线段 | **√** | 技术可行性验证完成 |
| 可交互 MVP | √ | 可以画线、Grip 编辑、撤销、保存 |
| 内部可用版 | × | 稳定到可以每天使用 |
| 功能完整版 | × | 对标基础 CAD 功能集 |
| 双端发布版 | × | Desktop + Web 同时可用 |

 
---

## 5. 文件目录

```
MiniCAD
│    
├── assets/                                         # 只读资源，打包进可执行文件或 wasm 数据段
│   ├── Icons/                                      # UI 图标：SVG 源文件 + 构建时生成的 PNG 图集
│   ├── Fonts/                                      # 字体文件：UI 字体 + CAD 标注专用字体（含工程字符）
│   └── Shader/                                     # 预编译 Shader 字节码：构建时由 Render/Shader/ 源码编译产出
│ 
├── src
│   │
│   ├── App/                                        # 启动层：唯一负责组装所有模块依赖，不含任何业务逻辑
│   │   ├── Main.cpp                                # 桌面端入口，初始化 Platform/Desktop 并启动主循环
│   │   ├── MainWasm.cpp                            # WebAssembly 入口，初始化 Platform/Web 并挂载 canvas 主循环
│   │   ├── Bootstrapper.cpp                        # 依赖注入组装：按序创建 Document、Editor、Renderer 并绑定
│   │   └── AppContext.h                            # 全局运行环境句柄，持有各顶层模块的生命周期指针
│   │
│   ├── Core/                                       # 纯计算库：无 I/O、无副作用、无任何其他层的依赖
│   │   ├── Math/                                   # 向量、矩阵、四元数、变换矩阵、数值工具
│   │   ├── Geom/                                   # 几何基元：点、线、圆弧、贝塞尔曲线、NURBS、包围盒
│   │   ├── Algorithm/                              # 几何算法：布尔运算、偏移、裁剪、交点、曲线拟合
│   │   ├── Query/                                  # 无状态查询：点到线距离、线段相交测试、凸包、拓扑关系
│   │   └── Base/                                   # 基础设施：Result<T>、Option<T>、错误码、类型工具
│   │
│   ├── Scene/                                      # 只读运行时快照：外部只能读，写操作全部通过 Document 层
│   │   ├── Scene.h/cpp                             # 顶层场景容器，聚合 EntityDatabase + Layer + Spatial
│   │   ├── EntityDatabase.h/cpp                    # 实体存储：对外只暴露 const Entity&，强制只读访问
│   │   ├── EntityHandle.h                          # 实体句柄（实体 ID + 版本号），替代裸指针；版本号不匹配时查询返回空
│   │   ├── Layer/                                  # 图层定义：图层属性（颜色、线型、可见性、锁定状态）
│   │   └── Spatial/                                # 空间索引：BVH / 四叉树，支持视锥裁剪和 Picking 加速查询
│   │
│   ├── Document/                                   # 数据库内核：系统中唯一合法的 Scene 写入口
│   │   ├── Document.h/cpp                          # 文档根对象，持有 Scene、CommandStack、EventBus、DirtyTracker
│   │   ├── DocumentManager.h/cpp                   # 多文档管理：新建、打开、关闭、活跃文档切换
│   │   │
│   │   ├── Transaction/                            # 所有写操作的执行通道，保证可撤销性和原子性
│   │   │   ├── ICommand.h                          # Command 基类接口：execute() / undo() / redo()
│   │   │   ├── CommandStack.h/cpp                  # Undo/Redo 栈：管理历史记录上限、合并相邻同类 Command
│   │   │   ├── CommandResult.h                     # 执行结果：Success / Failure(reason) / PartialFailure
│   │   │   ├── CompoundCommand.h                   # 组合命令：将多个 Command 包装为单一原子操作
│   │   │   ├── TwoPhaseExecutor.h                  # 两阶段提交：prepare() 做合法性校验，通过后才 commit() 写入；失败自动 rollback()
│   │   │   └── Commands/                           # 具体 Command 实现
│   │   │       ├── CreateEntityCommand.h           # 创建实体：携带初始属性，undo 时从 EntityDatabase 移除
│   │   │       ├── ModifyEntityCommand.h           # 修改实体：存储 before/after 快照，支持精确 undo
│   │   │       ├── DeleteEntityCommand.h           # 删除实体：undo 时恢复实体并重新分配相同 Handle
│   │   │       └── LayerCommand.h                  # 图层操作：新建/删除/重命名/属性修改
│   │   │
│   │   ├── Runtime/                                # 文档运行时服务，每个 Document 实例独立持有
│   │   │   ├── EventBus.h/cpp                      # per-Document 事件总线（非全局单例）：发布 EntityChanged / LayerChanged 等事件
│   │   │   ├── DirtyTracker.h                      # 写入侧脏标记：Command 执行后标记变更实体，供 Render/Sync 消费
│   │   │   └── Versioning.h                        # 文档版本号：每次 commit 递增，用于外部缓存失效判断
│   │   │
│   │   ├── Constraint/                             # 几何约束系统：参数化建模的核心，约束关系持久化到文档
│   │   │   ├── IConstraint.h                       # 约束基类：evaluate() 返回残差，用于求解器迭代
│   │   │   ├── DOFAnalyzer.h                       # 自由度分析：欠约束/过约束检测，Solver 调用前的前置校验，结果报告给用户
│   │   │   ├── ConstraintSolver.h/cpp              # 约束求解器：迭代法求解恰定方程组；仅在 DOFAnalyzer 校验通过后被调用
│   │   │   └── Constraints/                        # 具体约束类型
│   │   │       ├── ParallelConstraint.h            # 平行约束：两直线方向向量叉积为零
│   │   │       ├── PerpendicularConstraint.h       # 垂直约束：两直线方向向量点积为零
│   │   │       └── CoincidentConstraint.h          # 重合约束：两点坐标相等或点在曲线上
│   │   │
│   │   └── IO/                                     # 文件读写：序列化与反序列化
│   │       ├── Serializer.h/cpp                    # 序列化主流程：遍历 Scene 并分发到对应 Format 驱动
│   │       ├── FileFormat.h                        # 格式注册接口：每种格式实现 read() / write()
│   │       └── Formats/                            # 各格式驱动实现，新增格式只加文件不改主流程
│   │           ├── DXFFormat.h/cpp                 # AutoCAD DXF 格式：支持 R12/R2000/R2010 实体子集
│   │           ├── SVGFormat.h/cpp                 # SVG 导出：将 Scene 实体映射为 SVG path/circle/rect
│   │           └── NativeFormat.h/cpp              # 原生二进制格式：含版本头、增量存储、校验和
│   │
│   ├── Editor/                                     # 交互层：只产生"意图"（Command），不直接修改数据
│   │   ├── Context/                                # 编辑器运行时上下文，每个 Viewport 独立持有一份
│   │   │   ├── EditorContext.h                     # 当前活跃 Tool、活跃图层、坐标系模式等编辑状态
│   │   │   ├── ViewState.h                         # 视图状态：平移偏移、缩放比例、旋转角度
│   │   │   └── SelectionSet/                       # 从 Scene/ 迁入：视图状态，per-Viewport 独立，不属于场景数据
│   │   │       ├── SelectionSet.h/cpp              # 选中集合：内部持有 EntityHandle 列表，不持有裸指针
│   │   │       └── SelectionFilter.h               # 选择过滤器：按实体类型、图层、属性过滤可选对象
│   │   │
│   │   ├── Viewport/                               # 视口：负责将 Scene 坐标系映射到屏幕坐标系
│   │   │   ├── Viewport.h/cpp                      # 视口容器：持有 Camera、Grid、SelectionSet，驱动渲染请求
│   │   │   ├── Camera.h                            # 摄像机：正交/透视投影矩阵，pan/zoom/orbit 操作
│   │   │   ├── Grid.h                              # 辅助网格：自适应间距（随缩放级别调整密度）
│   │   │   └── Gizmo.h                             # 操作手柄：坐标轴箭头、旋转环、缩放点的屏幕空间绘制
│   │   │
│   │   ├── Tools/                                  # 工具系统：每种操作（画线、修剪、移动）对应一个 Tool
│   │   │   ├── ITool.h                             # Tool 基类接口：强制实现 cancel() / suspend() / resume()
│   │   │   ├── IToolState.h                        # Tool 状态枚举：Idle / Active / Suspended / Finished
│   │   │   ├── ToolStateMachine.h                  # 统一状态机：处理工具切换、Undo 打断、Snap 失效等笛卡尔积场景
│   │   │   ├── ToolManager.h                       # 工具注册与切换：确保前一个 Tool 正确 cancel 后再激活新 Tool
│   │   │   ├── Draw/                               # 绘制类工具：Line、Arc、Circle、Polyline、Spline
│   │   │   ├── Modify/                             # 修改类工具：Move、Copy、Rotate、Scale、Trim、Extend、Fillet
│   │   │   └── Selection/                          # 选择类工具：点选、窗口选、穿越选、快速选
│   │   │
│   │   ├── Input/                                  # 输入事件归一化：将平台原始事件转为编辑器语义事件
│   │   ├── Picking/                                # 拾取系统：屏幕坐标→世界坐标→实体 Handle，利用 Spatial 加速
│   │   │
│   │   ├── Snap/                                   # 吸附系统：插件化策略，运行时按优先级注册和启停
│   │   │   ├── ISnapStrategy.h                     # 吸附策略接口：trySnap(cursor) → SnapResult?
│   │   │   ├── SnapEngine.h/cpp                    # 吸附调度器：按优先级遍历策略，返回第一个有效吸附点
│   │   │   └── Strategies/                         # 内置吸附策略，插件亦可注册自定义策略
│   │   │       ├── EndpointSnap.h                  # 端点吸附：吸附到线段/弧线端点
│   │   │       ├── MidpointSnap.h                  # 中点吸附：吸附到线段/弧线中点
│   │   │       ├── GridSnap.h                      # 网格吸附：吸附到最近网格交叉点
│   │   │       └── IntersectionSnap.h              # 交点吸附：吸附到两条曲线的几何交点
│   │   │
│   │   ├── Grip/                                   # Grip 编辑：点选实体后显示控制点，拖拽直接修改几何
│   │   │   ├── IGripPoint.h                        # Grip 点接口：位置、类型（端点/中点/圆心），各实体类型各自实现
│   │   │   ├── GripController.h/cpp                # 拖拽生命周期：激活→拖拽→提交/取消；完成后走 CommandBridge 生成 ModifyEntityCommand
│   │   │   └── GripRenderer.h                      # Grip 点屏幕绘制：小方块/圆点，与 SelectionPass 协作渲染
│   │   │
│   │   ├── Annotation/                             # 标注系统：独立于 Tool，管理尺寸标注和引线样式
│   │   │   ├── DimensionStyle.h                    # 标注样式：箭头类型、文字大小、精度、单位
│   │   │   └── LeaderStyle.h                       # 引线样式：折线引线、样条引线、文字对齐方式
│   │   │
│   │   └── CommandBridge/                          # Tool → Command 转换层：Tool 不直接创建 Command
│   │       ├── CommandFactory.h                    # Command 工厂：根据参数创建对应 Command 实例
│   │       └── CommandTranslator.h                 # 意图翻译器：将 Tool / Grip 产生的几何意图翻译为具体 Command
│   │
│   ├── Render/                                     # 渲染层：从 RenderScene 快照绘制，不直接访问 Scene
│   │   ├── Core/                                   # 渲染抽象接口，后端无关
│   │   │   ├── IRenderer.h                         # 渲染器接口：beginFrame() / endFrame() / submit(RenderScene)
│   │   │   ├── IDevice.h                           # 设备接口：创建 Buffer、Texture、Shader、Pipeline
│   │   │   ├── RenderTypes.h                       # 渲染基础类型：顶点格式、混合模式、深度状态、图元类型
│   │   │   └── RenderContext.h                     # 单帧渲染上下文：持有当前帧的 CommandBuffer 和资源绑定状态
│   │   │
│   │   ├── Scene/                                  # 渲染侧场景快照，与 Document/Scene 完全解耦
│   │   │   ├── RenderScene.h                       # 渲染场景：持有当前帧所有可见 RenderEntity 的扁平列表
│   │   │   ├── RenderEntity.h                      # 渲染实体：GPU 可直接消费的顶点缓冲、材质、变换矩阵
│   │   │   └── RenderBuilder.h                     # 快照构建器：将 Scene::Entity 转换为 RenderEntity，由 DirtyPropagator 驱动增量调用
│   │   │
│   │   ├── Sync/                                   # Scene → RenderScene 同步边界：并发安全的核心
│   │   │   ├── RenderSyncBuffer.h                  # 双缓冲：Game thread 写 back buffer，Render thread 读 front buffer，帧末交换
│   │   │   └── DirtyPropagator.h                   # 订阅 Document/DirtyTracker 事件，只对变脏实体调用 RenderBuilder 增量重建
│   │   │
│   │   ├── Graph/                                  # Render Pass 有向无环图：自动依赖排序，按需裁剪
│   │   │   ├── RenderGraph.h/cpp                   # Pass 图编译器：每帧 compile() 做拓扑排序，剔除无输出 Pass
│   │   │   ├── RenderPass.h                        # Pass 基类：声明读写的资源（Texture/Buffer），供 Graph 推导依赖
│   │   │   └── Passes/                             # 内置 Pass 实现
│   │   │       ├── GridPass.h                      # 网格线绘制 Pass：无限延伸辅助网格，淡出远端
│   │   │       ├── EntityPass.h                    # 实体几何绘制 Pass：线段、弧线、填充区域
│   │   │       ├── OverlayPass.h                   # 叠加层 Pass：临时预览线（Tool 绘制中的橡皮筋）
│   │   │       ├── GizmoPass.h                     # Gizmo 绘制 Pass：坐标轴手柄、旋转环，始终在最前层
│   │   │       └── SelectionPass.h                 # 选区高亮 Pass：选中实体轮廓描边 + Grip 点渲染
│   │   │
│   │   ├── Backend/                                # 图形 API 后端实现，均实现 IRenderer + IDevice 接口
│   │   │   ├── D3D11/                              # Direct3D 11 后端：Windows 桌面端主要后端，约 5,000 行
│   │   │   ├── Vulkan/                             # Vulkan 后端：跨平台高性能桌面端，约 10,000 行（同步原语复杂）
│   │   │   ├── WebGPU/                             # WebGPU 后端：现代浏览器首选，约 5,000 行
│   │   │   └── WebGL/                              # WebGL 2.0 后端：WebGPU 不可用时的降级方案，约 5,000 行
│   │   │
│   │   └── Shader/                                 # Shader 源码（HLSL/WGSL/GLSL），构建时按后端编译
│   │
│   ├── Platform/                                   # 系统抽象层：将 OS 差异收进接口，上层代码不感知平台
│   │   ├── IWindow.h                               # 窗口接口：创建、销毁、resize、全屏切换
│   │   ├── IInput.h                                # 输入接口：鼠标、键盘、触控板事件归一化
│   │   ├── IFileSystem.h                           # 文件系统接口：读写文件、目录遍历、路径拼接
│   │   ├── ITimer.h                                # 计时器接口：高精度时间戳、帧间隔计算
│   │   ├── IClipboard.h                            # 剪贴板接口：读写文本和自定义实体格式
│   │   ├── ICursor.h                               # 光标接口：切换系统光标形状（CAD 频繁切换十字/箭头/手型）
│   │   ├── Desktop/                                # 桌面端实现：Win32 / Cocoa / X11
│   │   └── Web/                                    # Web 端实现：Emscripten + Browser API
│   │
│   ├── UI/                                         # UI 层：可整体替换，当前基于 ImGui
│   │   ├── ImGui/
│   │   │   ├── ImGuiLayer.h/cpp                    # ImGui 帧循环集成：NewFrame / Render，管理字体和主题
│   │   │   └── Backend/
│   │   │       ├── DesktopImGuiBackend.h           # 桌面端 ImGui 后端：对接 Platform/Desktop + D3D11/Vulkan
│   │   │       └── WebImGuiBackend.h               # Web 端 ImGui 后端：对接 Platform/Web + WebGPU/WebGL
│   │   ├── Panels/                                 # 停靠面板：属性面板、图层面板、命令历史面板
│   │   ├── Windows/                                # 模态窗口：文件对话框、设置窗口、关于窗口
│   │   └── Widgets/                                # 可复用控件：颜色选择器、线型选择器、数值输入框
│   │
│   └── Plugin/                                     # 插件系统：第三方扩展的统一入口
│       ├── IPlugin.h                               # 插件基类接口：load() / unload() / getMetadata()
│       ├── PluginManager.h/cpp                     # 插件管理器：动态加载 .dll/.so、生命周期管理、版本校验
│       └── SDK/                                    # 暴露给插件的最小稳定接口集，不暴露内部头文件
│           ├── PublicAPI.h                         # 公开 API 入口：版本宏（MINICAD_API_VERSION）管控 ABI 兼容性
│           └── PluginContext.h    
│
├── thirdParty/                                     # 第三方库：以源码或预编译二进制形式管理
│                                                   
└── tests/                                          # 测试：按层分组，CI 可并行运行各子目录
    ├── Core/                                       # Core 层单元测试：算法正确性、数值稳定性、边界情况
    ├── Document/                                   # Document 层测试
    │   ├── TransactionTests/                       # Command 执行、Undo/Redo、CompoundCommand 原子性、TwoPhaseExecutor 回滚
    │   └── ConstraintTests/                        # DOFAnalyzer 检测正确性、约束求解精度、过约束冲突回滚
    ├── Editor/                                     # Editor 层测试
    │   ├── SnapTests/                              # 各吸附策略命中精度、优先级顺序、策略注册与停用
    │   └── GripTests/                              # Grip 拖拽提交/取消、Command 生成正确性、Handle 失效保护
    ├── Render/                                     # Render 层测试：RenderBuilder 增量重建正确性、双缓冲交换时序
    └── Integration/                                # 跨层联合测试：画线→Undo→渲染验证、Grip 编辑→约束求解、多文档事件隔离
```
 

---
 

*日期：2026-05-08* 

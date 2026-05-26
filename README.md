# MiniCAD

一个轻量级二维CAD编辑器。

![MiniCAD](docs/images/MiniCAD.png)

## 文件目录 
 

```
MiniCAD
│    
├── assets/                                         # 只读资源，打包进可执行文件或 WASM 数据段
│   ├── fonts/                                      # 字体文件：TTF + SHX（CAD 工程字体）
│   ├── icons/                                      # UI 图标（PNG）
│   └── shader/                                     # 预编译 Shader 字节码
│ 
├── src
│   │
│   ├── App/                                        # 启动层：组装所有模块，不含业务逻辑
│   │   ├── Main.cpp                                # 桌面端（Windows）入口
│   │   ├── WebMain.cpp                             # WebAssembly（Emscripten）入口
│   │   ├── MainWindow.h                            # 窗口管理
│   │   └── AppContext.h                            # 全局运行环境
│   │
│   ├── Core/                                       # 纯计算库：无 I/O、无副作用、独立无依赖
│   │   ├── Math/                                   # 向量、矩阵、四元数、变换
│   │   └── Geom/                                   # 几何基元：Point, Line, Arc, Circle, Ellipse, Polyline, Spline, AABB
│   │
│   ├── Scene/                                      # 只读运行时快照
│   │   ├── Scene.h/cpp                             # 场景容器
│   │   ├── EntityDatabase.h/cpp                    # 实体存储
│   │   ├── Layer.h/cpp                             # 图层定义
│   │   └── LayerManager.h/cpp                      # 图层管理
│   │
│   ├── Document/                                   # 数据库内核：唯一的 Scene 写入口
│   │   ├── Document.h/cpp                          # 文档根对象
│   │   ├── DocumentManager.h/cpp                   # 多文档管理
│   │   │
│   │   ├── Command/                                # Command 模式实现
│   │   │   ├── ICommand.h                          # Command 基类：execute() / undo() / redo()
│   │   │   ├── CommandStack.h/cpp                  # Undo/Redo 栈
│   │   │   ├── CreateEntityCommand.h               # 创建实体
│   │   │   ├── ModifyEntityCommand.h               # 修改实体（含 before/after 快照）
│   │   │   ├── DeleteEntityCommand.h               # 删除实体
│   │   │   ├── MoveCommand.h / CopyCommand.h       # 平移、复制
│   │   │   ├── RotateMoveCommand.h / RotateCopyCommand.h  # 旋转（移动/复制）
│   │   │   ├── MirrorMoveCommand.h / MirrorCopyCommand.h  # 镜像（移动/复制）
│   │   │   └── ...其他 Command 变体
│   │   │
│   │   ├── CommandStack/                           # CommandStack 实现（与 Command/ 合并）
│   │   │
│   │   ├── Runtime/                                # ⏸️ TODO: EventBus、DirtyTracker、Versioning
│   │   ├── Constraint/                             # ⏸️ TODO: 约束系统、DOFAnalyzer、ConstraintSolver
│   │   └── IO/                                     # ⏸️ TODO: DXF/SVG/Native 格式支持
│   │
│   ├── Editor/                                     # 交互层：只产生 Intent（Command），不直接修改数据
│   │   ├── Context/                                # 编辑器上下文
│   │   │   ├── EditorContext.h                     # 当前工具、活跃图层、坐标系
│   │   │   └── ViewState.h                         # 视图状态（平移、缩放、旋转）
│   │   │
│   │   ├── Viewport/                               # 视口系统
│   │   │   ├── Viewport.h/cpp                      # 视口容器
│   │   │   ├── Camera.h                            # 摄像机（投影矩阵）
│   │   │   ├── Grid.h                              # 辅助网格
│   │   │   ├── Gizmo.h                             # 操作手柄
│   │   │   ├── Cursor.h                            # 光标管理
│   │   │   └── Axis.h                              # 坐标轴显示
│   │   │
│   │   ├── Tools/                                  # 工具系统（12 个工具）
│   │   │   ├── ITool.h                             # 工具基类
│   │   │   ├── Draw/                               # 绘制工具（8 个）
│   │   │   │   ├── LineTool.h
│   │   │   │   ├── CircleTool.h
│   │   │   │   ├── ArcTool.h
│   │   │   │   ├── EllipseTool.h
│   │   │   │   ├── PointTool.h
│   │   │   │   ├── PolylineTool.h
│   │   │   │   ├── RectangleTool.h
│   │   │   │   ├── SplineTool.h
│   │   │   │   └── TextTool.h
│   │   │   └── Modify/                             # 修改工具（4 个 + 复制变体）
│   │   │       ├── MoveTool.h
│   │   │       ├── CopyTool.h
│   │   │       ├── RotateTool.h
│   │   │       └── MirrorTool.h
│   │   │
│   │   ├── Input/                                  # 输入事件处理
│   │   │   ├── InputEvent.h                        # 事件定义
│   │   │   ├── InputSystem.h/cpp                   # 输入系统
│   │   │   ├── KeyCode.h                           # 键码定义
│   │   │   ├── KeyCodeUtils.h
│   │   │   ├── ViewportInputAdapter.h              # 视口输入适配
│   │   │   └── IInputHandler.h                     # 输入处理接口
│   │   │
│   │   ├── Snap/                                   # 吸附系统
│   │   │   ├── SnapEngine.h/cpp                    # 吸附调度器
│   │   │   └── SnapResult.h                        # 吸附结果
│   │   │
│   │   ├── Picking/                                # 拾取系统
│   │   │   └── Picking.h                           # 屏幕坐标→实体 Handle
│   │   │
│   │   ├── Grip/                                   # Grip 编辑系统
│   │   │   ├── GripEditor.h                        # Grip 编辑器
│   │   │   ├── GripType.h                          # Grip 类型枚举
│   │   │   ├── IEntityGripHandler.h                # Grip 基类
│   │   │   └── [实体类型]GripHandler.h             # 各实体的 Grip 实现
│   │   │       ├── LineGripHandler.h
│   │   │       ├── CircleGripHandler.h
│   │   │       ├── ArcGripHandler.h
│   │   │       ├── EllipseGripHandler.h
│   │   │       ├── PolylineGripHandler.h
│   │   │       ├── SplineGripHandler.h
│   │   │       ├── PointGripHandler.h
│   │   │       └── RectangleGripHandler.h
│   │   │
│   │   └── Overlay/                                # 临时几何层（工具进行中的橡皮筋）
│   │       └── Overlay.h
│   │
│   ├── Render/                                     # 渲染层（基础实现）
│   │   ├── IRenderer.h                             # 渲染器接口
│   │   ├── IRenderTarget.h                         # 渲染目标接口
│   │   │
│   │   ├── D3D11/                                  # Direct3D 11 后端（Windows）
│   │   │   ├── D3D11Renderer.h/cpp                 # D3D11 渲染器实现
│   │   │   ├── Device.h/cpp                        # D3D11 设备
│   │   │   ├── Shader.h/cpp                        # Shader 管理
│   │   │   ├── SwapChain.h/cpp                     # 交换链
│   │   │   └── D3D11RenderTarget.h                 # 渲染目标
│   │   │
│   │   ├── WebGL/                                  # WebGL 2.0 后端（Emscripten）
│   │   │   ├── WebGLRenderer.h/cpp                 # WebGL 渲染器实现
│   │   │   └── WebGLRenderTarget.h                 # WebGL 渲染目标
│   │   │
│   │   ├── Scene/                                  # ⏸️ TODO: RenderScene、RenderEntity、RenderBuilder
│   │   ├── Sync/                                   # ⏸️ TODO: RenderSyncBuffer、DirtyPropagator
│   │   ├── Graph/                                  # ⏸️ TODO: RenderGraph、RenderPass、Pass DAG
│   │   └── Backend/Vulkan/                         # ⏸️ TODO: Vulkan 后端
│   │       WebGPU/                                 # ⏸️ TODO: WebGPU 后端
│   │
│   ├── Text/                                       # 文本与字体系统（✓ 完整实现）
│   │   ├── Font/                                   # 字体引擎
│   │   │   ├── IFont.h                             # 字体接口
│   │   │   ├── TTFFont.h                           # TrueType 字体
│   │   │   ├── SHXFont.h                           # SHX 字体（CAD 标准）
│   │   │   └── SHXCompositeFont.h                  # SHX 组合字体
│   │   ├── Glyph/                                  # 字形缓存与管理
│   │   │   ├── Glyph.h                             # 单个字形
│   │   │   ├── GlyphCache.h/cpp                    # LRU 字形缓存
│   │   │   ├── GlyphKey.h                          # 字形 Key
│   │   ├── Parser/                                 # SHX 解析与虚拟机
│   │   │   ├── SHXParser.h/cpp                     # SHX 文件解析
│   │   │   └── SHXVM.h                             # SHX 虚拟机执行器
│   │   ├── Layout/                                 # 文本排版
│   │   │   ├── TextLayoutEngine.h                  # 排版引擎
│   │   │   └── Utf8Iterator.h                      # UTF-8 迭代器
│   │   └── FontSystem.h                            # 全局字体系统
│   │
│   ├── UI/                                         # UI 层（基于 ImGui）
│   │   ├── ImGuiLayer.h/cpp                        # ImGui 集成
│   │   ├── UIManager.h                             # UI 管理器
│   │   ├── Panels/                                 # 面板（属性、图层等）
│   │   ├── Windows/                                # 模态窗口
│   │   └── Widgets/                                # 可复用控件
│   │
│   ├── Platform/                                   # ⏸️ TODO: 平台抽象层（IWindow、IInput、IFileSystem 等）
│   └── Plugin/                                     # ⏸️ TODO: 插件系统（IPlugin、PluginManager、SDK）
│
├── 3rd/                                            # 第三方库（预编译或源码）
│   └── imgui/                                      # ImGui UI 框架
│
├── CMakeLists.txt                                  # CMake 构建配置（多目标）
├── build_web.bat                                   # WebAssembly 构建脚本 
├── README.md                                       # 项目文档
└── .gitignore                                      # Git 忽略列表
```

 
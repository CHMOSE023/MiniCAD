# Lesson02：整体架构地图

## 学习目标

建立 MiniCAD 完整的心智模型。本课不只是列举模块，而是说清楚：**谁持有谁**、**谁能写 Scene**、**数据怎么从鼠标事件变成屏幕像素**，以及为什么这样设计。

---

## 顶层所有权结构

运行时只有一个 `DocumentManager`，它是整个程序的根对象：

```
DocumentManager
├── vector<unique_ptr<Document>>   ← 所有文档（可多个）
│     └── Document
│           ├── Scene              ← 实体数据库 + 图层
│           └── CommandStack       ← 撤销/重做栈
├── unique_ptr<Viewport>           ← 唯一视口（所有文档共用）
│     ├── Camera / Grid / Axis / Cursor / Gizmo
│     └── IRenderer&               ← 不拥有，只引用
├── Editor                         ← 唯一编辑器实例（值成员，非指针）
│     ├── Document*   m_doc        ← 非拥有，Bind() 后指向活跃文档
│     ├── Viewport*   m_viewport   ← 非拥有，Bind() 后指向唯一视口
│     ├── Overlay / Picking / SnapEngine / GripEditor
│     ├── ConstraintEngine / InputResolver / CommandLine
│     └── 四组顶点缓冲 (scene / text / overlay / grip)
└── FontSystem*                    ← 不拥有，由外部注入
```

**关键细节**：
- `DocumentManager` 用 `unique_ptr` 拥有所有 `Document`，`Document*` 只是非拥有观察指针。
- `Editor` 是 `DocumentManager` 的**值成员**，随 `DocumentManager` 生死，不是指针。
- `Viewport` 是 `unique_ptr`，在 `InitViewport()` 后创建，之后不会重建。
- `IRenderer` 由平台层（Main.cpp / WebMain.cpp）创建并注入，`DocumentManager` 不拥有它。

---

## 三模块运行时分离

MiniCAD 的核心设计决策是将运行时拆成三个**职责完全不同**的模块：

| 模块 | 职责 | 持有什么 |
|---|---|---|
| **Document** | 纯数据，不依赖任何交互或渲染 | Scene（实体）、CommandStack（历史）|
| **Viewport** | 视图与渲染，不知道工具是什么 | Camera、Grid、IRenderer& |
| **Editor** | 交互，通过 Bind 连接到前两者 | 工具、选择集、吸附、顶点缓冲 |

这三者**互不拥有对方**。`Editor` 通过 `Bind(Document&, Viewport&)` 持有非拥有指针，切换文档时调用 `Unbind()` + `Bind(newDoc, viewport)` 即可，不需要重建任何对象。

---

## Scene 写入规则（最重要的不变量）

**Scene 的所有修改必须经过 `ICommand → CommandStack`，没有例外。**

```
允许：  Editor → Tool → ICommand → CommandStack::Execute(scene) → Scene::AddEntity()
禁止：  Editor 直接调用 scene.AddEntity()
禁止：  UI 层直接修改 Scene
禁止：  Viewport 修改 Scene
```

这条规则保证撤销/重做始终有效。`CommandStack::Execute()` 在执行成功后才入栈；`Execute()` 返回 `false` 时命令不入栈（校验失败，例如两点重合的零长线段）。

**所有权在 Execute/Undo 之间流动**：

```
Execute 之前：Command 拥有 unique_ptr<Entity>（持有实体）
Execute 之后：Scene  拥有 unique_ptr<Entity>（AddEntity 转移所有权）
Undo   之后：Command 重新拿回 unique_ptr<Entity>（RemoveEntity 返回所有权）
Redo   之后：Scene  再次获得所有权
```

---

## Document 内部：Scene 与 CommandStack

```cpp
class Document {
    Scene        m_scene;      // 实体数据库
    CommandStack m_cmdStack;   // undo/redo 双栈
};
```

`Scene` 内部：

```
Scene
├── unordered_map<ObjectID, unique_ptr<Object>>  m_entities  ← 正式实体
├── vector<unique_ptr<Object>>                   m_previews  ← 预览对象（工具绘制中间状态，不进命令栈）
├── atomic<ObjectID>                             m_nextObjectID
├── LayerManager                                 m_layerManager
└── DirtyCallback                                m_onDirty   ← 脏标记回调
```

`m_previews` 与 `m_entities` 的区别：预览对象只用于当前帧显示工具的"橡皮筋"效果，不持久化，不可撤销。

---

## Editor 内部：七个子系统

`Editor` 看上去大，实际上是七个子系统的组合器：

| 子系统 | 职责 |
|---|---|
| `Overlay` | 管理预览几何（工具绘制的临时线段） |
| `Picking` | 鼠标命中检测，维护 hovered/selected 集合 |
| `SnapEngine` | 端点/中点/最近点/象限点/网格吸附 |
| `GripEditor` | 夹点拖拽状态机（选中后才激活） |
| `ConstraintEngine` | 正交/极轴约束，需要锚点 |
| `InputResolver` | 把原始鼠标坐标解析成带吸附、约束、拾取结果的 `InputResult` |
| `CommandLine` | 命令行缓冲：提示语 + 回显历史 |

每帧 `Editor::OnInput()` 的执行顺序：

```
InputEvent 进入
  ↓
HandleGlobal()         ← Esc 取消工具、Ctrl+Z/Y、Delete 等全局快捷键
  ↓
GripEditor::OnInput()  ← 夹点拖拽优先于工具
  ↓
InputResolver::Resolve()
    Raw: 屏幕坐标 → 世界坐标 (Camera)
    Snap: 对世界坐标进行吸附计算
    Constraint: 若工具有锚点则施加正交/极轴约束
    Picking: 对未约束坐标做 HitTest
  ↓
ITool::OnInput(EditorContext)  ← 当前活跃工具处理事件
  ↓
HandleDefault()        ← 工具未消费时：单击选择、框选等默认行为
```

---

## 渲染管线：从实体到像素

渲染分两个阶段：**顶点收集**（CPU）和**提交绘制**（GPU）。

### 阶段一：顶点收集（Editor::UpdateSceneVertices）

```
Scene::ForEachObject(entity)
  → entity.Draw(DrawContext)          ← 实体向 DrawContext 发射几何
       ↓
  DrawContext（IDrawSink 实现）
       ├── DrawLine()   → m_sceneVertices   (Vertex_P3_C4 线段)
       ├── EmitText()   → m_textVertices    (Vertex_P3_C4_UV 纹理四边形)
       └── EmitMText()  → m_sceneVertices   (矢量字形，输出线段)

Editor 同时收集：
  Overlay::GetLines()  → m_overlayVertices  (工具预览线段)
  GripEditor           → m_gripVertices     (夹点小方块)
```

`DrawContext` 是 `IDrawSink` 的唯一实现。实体只调用 `IDrawSink` 接口，不知道背后是 D3D11 还是 WebGL，也不知道自己是在正式场景里还是预览里。

**脏标记优化**：`UpdateSceneVertices()` 只在 `scene.IsDirty() || picking.IsDirty()` 时执行，静态帧跳过重建，复用上一帧的缓冲容量。

### 阶段二：ViewState 传递（零拷贝）

```cpp
ViewState BuildViewState() {
    ViewState vs;
    vs.Scene    = std::span(m_sceneVertices);    // 不复制，只传 span
    vs.Overlay  = std::span(m_overlayVertices);
    vs.TextScene= std::span(m_textVertices);
    vs.Grips    = std::span(m_gripVertices);
    // ... 鼠标坐标、吸附标记、选择框 ...
    return vs;
}
```

`ViewState` 全部用 `std::span`，指向 `Editor` 持有的顶点缓冲，没有任何数据复制。

### 阶段三：Viewport 提交（Viewport::Render）

```
Viewport::Render(ViewState)
  ├── IRenderer::BeginFrame(renderTarget, viewportDesc)
  ├── IRenderer::Submit(vs.Scene,    viewProj)      ← 场景线段
  ├── IRenderer::Submit(vs.Overlay,  viewProj)      ← 预览线段
  ├── IRenderer::SubmitTextured(vs.TextScene, ...)  ← 文字纹理四边形
  ├── BuildGripGeometry → IRenderer::Submit(grips)  ← 夹点方块
  ├── BuildSnapGeometry → IRenderer::Submit(snap)   ← 吸附标记
  └── IRenderer::EndFrame()
```

`Viewport` 不知道顶点从哪里来（不依赖 Editor），只知道调用 `IRenderer` 的接口。`IRenderer` 的具体实现（D3D11Renderer / WebGLRenderer）在平台层。

---

## 完整数据流（以画一条线为例）

```
用户按 L + Enter
  → Editor::RunCommand("L")
  → ActivateToolByAlias("L") → "Line" → LineTool 实例化并激活

用户点击第一个点
  → InputResolver 解析：屏幕坐标 → 世界坐标 → 吸附修正
  → LineTool::OnInput() 记录第一个点，设置锚点
  → LineTool::SetPrompt("指定下一点:")

用户点击第二个点
  → LineTool::OnInput() 收到第二个点
  → 创建 LineEntity(pt1, pt2)
  → 构造 AddEntityCommand(entity)
  → CommandStack::Execute(cmd, scene)
      → cmd.Execute(scene)：scene.AddEntity(entity)，Scene 标记 dirty
      → cmd 入 undoStack
  → LineTool 等待下一个点（连续画线模式）

下一帧渲染
  → Editor::Render()
      → scene.IsDirty() == true → UpdateSceneVertices()
          → 遍历所有实体，调用 entity.Draw(DrawContext)
          → LineEntity 向 DrawContext 发射两个顶点
      → BuildViewState() → ViewState（span 引用顶点缓冲）
      → Viewport::Render(viewState) → IRenderer::Submit()
  → 线段出现在屏幕上

用户按 Ctrl+Z
  → Editor::Undo() → Document::Undo()
  → CommandStack::Undo(scene)
      → cmd.Undo(scene)：scene.RemoveEntity()，所有权归还给 cmd
      → cmd 移入 redoStack
  → Scene 标记 dirty → 下一帧重建顶点 → 线段消失
```

---

## 层间访问规则速查

| 发起层 | 可读 | 可写 | 禁止 |
|---|---|---|---|
| **App** | 全部 | 组装/初始化 | 业务逻辑 |
| **UI** | DocumentManager（读）、Editor（读选择集） | 调用 DocumentManager 的 New/Open/Save/Undo/Redo | 直接写 Scene |
| **Editor** | Document（只读 Scene）、Viewport（调用 Render） | 通过 ICommand→CommandStack 写 Scene | 直接写 Scene |
| **Document** | Scene（读写）、CommandStack | — | Editor、Viewport、Render |
| **Core** | 自身 | — | 任何上层 |

---

## 课堂演示

打开以下文件，按下面顺序阅读：

1. `DocumentManager.h` — 理解顶层所有权（m_docs、m_viewport、m_editor）
2. `Document.h` — 理解 Scene + CommandStack 的关系
3. `CommandStack.h` — Execute/Push/Undo/Redo 接口与两个 stack
4. `Editor.h` — 七个子系统成员变量 + 四组顶点缓冲
5. `DrawContext.hpp` — IDrawSink 的实现：DrawLine/EmitText/EmitMText
6. `ViewState.h` — 全 span，零拷贝

## 拓展练习

1. `Editor` 为什么用值成员（`Editor m_editor`）而不是 `unique_ptr<Editor>`？改成 `unique_ptr` 会有什么问题？
2. 为什么 `Scene` 要区分 `m_entities` 和 `m_previews`？把预览对象也放进 `m_entities` 会破坏什么？
3. `CommandStack::Execute()` 返回 `false` 时命令不入栈，找一个实际的工具看它在哪里可能返回 `false`。
4. 追踪 `Scene::MarkDirty()` 的调用路径，从 `CommandStack::Execute()` 到最终 `m_onDirty` 回调被触发。

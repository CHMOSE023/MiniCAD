# Lesson05：绘图工具如何工作

## 学习目标

以 `LineTool` 为解剖对象，读懂工具的完整生命周期：接口定义、状态机设计、预览渲染、坐标获取（含吸附回退）、提交命令、锚点供约束使用、提示语更新。完成后应能独立实现一个新工具。

---

## 相关源码

- `src/Editor/Tools/ITool.h`
- `src/Editor/Tools/LineTool.h`
- `src/Editor/EditorContext.h`
- `src/Document/Command/AddEntityCommand.h`
- `src/Editor/Overlay/Overlay.h`
- `src/Editor/Resolver/InputResult.h`

---

## ITool 接口全解

```cpp
class ITool {
    virtual bool         OnInput(const EditorContext& ctx) = 0;
    virtual void         Cancel()          {}   // ESC / 右键 / 工具切换
    virtual void         OnSceneChanged()  {}   // Undo/Redo/Delete 后通知工具
    virtual void         OnFocusLost()     {}   // 中键平移开始，工具暂停
    virtual void         OnFocusRestored() {}   // 中键平移结束，工具恢复
    virtual bool         HasAnchor() const { return false; }
    virtual Point3       GetAnchor() const { return {}; }
    virtual std::string  GetPrompt() const { return {}; }

    std::function<void()> OnFinished;           // 工具主动完成时调用
};
```

各方法职责：

| 方法 | 何时调用 | 作用 |
|---|---|---|
| `OnInput()` | 每个输入事件 | 核心逻辑，返回 true 表示事件已消费 |
| `Cancel()` | ESC / 工具切换 | 清理 Overlay、重置状态 |
| `OnSceneChanged()` | Undo/Redo/Delete | 场景突变后工具需要重置（如已选点可能不再有效）|
| `OnFocusLost()` | 中键按下开始平移 | 工具暂停，隐藏预览 |
| `OnFocusRestored()` | 中键释放 | 工具恢复，重绘预览 |
| `HasAnchor()` / `GetAnchor()` | `ConstraintEngine` 每帧查询 | 第一点已确定后提供锚点，用于正交/极轴约束 |
| `GetPrompt()` | 每帧 `CommandLine` 读取 | 提示语随状态变化（"指定第一个点" vs "指定下一点"）|
| `OnFinished` | 工具自身在完成时触发 | 通知 `Editor` 销毁当前工具 |

---

## EditorContext：工具能用到的所有上下文

```cpp
struct EditorContext {
    InputEvent&        event;      // 当前输入事件（非 const：可被回填）
    Scene&             scene;      // 场景（只读），工具不直接写
    Viewport&          viewport;   // 坐标转换
    SnapEngine&        snap;       // 吸附（一般不直接用，已经由 InputResolver 处理）
    ConstraintEngine&  constraint; // 约束（一般不直接用）
    Picking&           picking;    // 选择集查询
    CommandStack&      cmdStack;   // 提交命令的唯一入口
    Overlay&           overlay;    // 预览几何（临时线、辅助线）
    InputResult        resolved;   // InputResolver 已解析的结果（含吸附/约束/拾取）
    ITool*             tool;       // 当前工具自身（一般不用）
    GripEditor*        grip;       // 夹点编辑器引用
};
```

**工具三件套**：`cmdStack`（提交）、`overlay`（预览）、`viewport.GetCamera()`（坐标转换）。

---

## LineTool 状态机

`LineTool` 只有两个状态，状态由 `m_hasStart` 控制：

```
[初始] m_hasStart = false
  左键 → 记录 m_start，m_hasStart = true
  进入 [已有起点]

[已有起点] m_hasStart = true
  MouseMove  → 更新 m_preview，刷新 Overlay（橡皮筋预览线）
  左键       → Commit(m_start, pt)，m_start = pt（连续画线，保留上一个终点为下一段起点）
  右键 / ESC → Clear Overlay，调用 OnFinished()，工具结束
```

实际代码：

```cpp
bool OnInput(const EditorContext& ctx) override {
    m_ctx = &ctx;
    const auto& e = ctx.event;

    if (e.IsLeftClick()) {
        auto pt = GetPoint(e);          // ← 含吸附优先逻辑
        if (!m_hasStart) {
            m_start = pt;
            m_hasStart = true;
        } else {
            Commit(m_start, pt);
            m_start = pt;               // 保留终点继续连线
        }
        return true;                    // 消费事件
    }

    if (e.IsRightClick() || e.IsCancel()) {
        m_ctx->overlay.Clear();
        if (OnFinished) OnFinished();   // 通知 Editor 销毁工具
        return true;
    }

    if (e.Type == InputEventType::MouseMove && m_hasStart) {
        m_preview = GetPoint(e);
        m_ctx->overlay.Clear();
        const auto& layer = m_ctx->scene.GetLayerManager().GetActiveLayer();
        m_ctx->overlay.AddLine(m_start, m_preview, layer.GetColor());
        return false;                   // 不消费 MouseMove（其他系统还需要处理）
    }

    return false;
}
```

---

## 坐标获取：吸附优先

`GetPoint()` 的逻辑体现了输入解析优先级：

```cpp
Math::Point3 GetPoint(const InputEvent& e) {
    if (e.HasSnap) return e.SnapWorld;   // 吸附点优先（端点、中点等）
    return m_ctx->viewport.GetCamera().ScreenToWorld(e.MouseX, e.MouseY);
}
```

`InputEvent::HasSnap` 和 `SnapWorld` 由 `InputResolver` 在每帧填充：如果 `SnapEngine` 在屏幕 12px 范围内找到了吸附点，就将结果写入事件。工具不需要自己查询吸附，直接读取即可。

---

## Commit：提交命令

```cpp
void Commit(const Point3& a, const Point3& b) {
    auto id   = m_ctx->scene.NextObjectID();           // 分配唯一 ID（atomic++）
    auto line = std::make_unique<LineEntity>(id, a, b); // 构造实体
    auto cmd  = std::make_unique<AddEntityCommand>(std::move(line)); // 包装命令

    m_ctx->cmdStack.Execute(std::move(cmd), m_ctx->scene); // 执行并入栈
}
```

**为什么用 `NextObjectID()`**：`Scene` 内部用 `std::atomic<ObjectID>` 分配 ID，工具在提交前必须先获取 ID，然后才能创建实体。ID 是 `uint64_t`，0 保留为 `InvalidID`，从 1 开始。

**为什么不直接 `scene.AddEntity()`**：绕过命令栈的修改无法撤销。`CommandStack::Execute()` 在 `Execute()` 返回 true 后才入栈，返回 false（校验失败，如零长度线）则不入栈。

---

## 锚点：为约束系统服务

```cpp
bool HasAnchor() const override { return m_hasStart; }
Math::Point3 GetAnchor() const override { return m_start; }
```

`ConstraintEngine` 每帧查询活跃工具的锚点。有锚点时，正交约束（F8）和极轴约束会以锚点为参考修正鼠标坐标——这就是为什么画线第二点会自动对齐水平/垂直方向。

---

## Overlay 预览原则

- **只用于当前帧显示**：每次 `MouseMove` 先 `Clear()` 再 `AddLine()`，不累积。
- **ESC 时必须 Clear()**：否则预览线段会残留在画面上。
- **不进命令栈**：`Overlay` 里的线段不属于任何实体，不可选择，不可撤销。

---

## 工具注册与激活

工具在 `Editor::RegisterBuiltinTools()` 中注册：

```cpp
RegisterTool("Line",   []{ return std::make_unique<LineTool>(); });
RegisterAlias("L",     "Line");
RegisterAlias("直线",  "Line");
```

激活时通过 `ActivateToolById("Line")` 调用工厂函数创建新实例，每次激活都是全新状态。

---

## 工具是 header-only

所有工具类（`LineTool`、`CircleTool` 等）**只有 `.h` 文件，没有对应 `.cpp`**。这是有意为之：工具逻辑短小，内联到头文件避免了编译单元拆分带来的头文件依赖问题，也方便整体阅读。

---

## 拓展练习

1. `LineTool` 的 `OnSceneChanged()` 是默认空实现。如果用户在连续画线时按 Ctrl+Z 撤销了上一条线，会有什么问题？应该如何修复？
2. 比较 `LineTool` 和 `CircleTool` 的状态数量（变量）和状态转换逻辑。
3. 尝试实现一个"两点矩形工具"：第一点左上角，第二点右下角，`MouseMove` 时显示矩形预览，`Commit` 时创建 `RectangleEntity`。
4. `GetPoint()` 只处理了吸附，为什么不需要处理约束？约束修正在哪里发生？

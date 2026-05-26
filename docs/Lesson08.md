# Lesson08：选择、拾取与夹点编辑

## 学习目标

读懂 `Picking` 的三种拖拽状态和三个像素阈值、点选与框选的判定逻辑；读懂 `GripEditor` 的四阶段状态机；理解 `IEntityGripHandler` 如何通过 RuntimeTypeInfo 分派、`LineGripHandler` 的完整实现（BuildGrips / BeginDrag / UpdateDrag / DrawPreview / EndDrag / CancelDrag）。

---

## 相关源码

- `src/Editor/Picking/Picking.h`
- `src/Editor/Grip/GripEditor.h`
- `src/Editor/Grip/IEntityGripHandler.h`
- `src/Editor/Grip/LineGripHandler.h`
- `src/Editor/Grip/GripType.h`

---

## Picking：选择与悬停

### 三个像素阈值

```cpp
static constexpr float DRAG_THRESH  = 2.0f;  // 区分"点击"和"拖拽"
static constexpr float HOVER_THRESH = 6.0f;  // 悬浮检测半径（略大，容易扫到）
static constexpr float PICK_THRESH  = 5.0f;  // 点击选中检测半径（略小，精确）
```

三个阈值的设计哲学：
- `DRAG_THRESH < PICK_THRESH < HOVER_THRESH`
- 先检测是否超过 `DRAG_THRESH` 来决定是"点击"还是"框选起始"
- 悬浮比选中半径大（用户"扫到"比"命中"更容易），提高可发现性

### 三种拖拽状态

```cpp
enum class DragState : uint8_t {
    Idle,           // 无操作
    Pressing,       // 鼠标已按下，等待判断是点击还是拖拽
    BoxSelecting    // 已超过 DRAG_THRESH，进入框选模式
};
```

**状态转换**：

```
Idle
  MouseDown 左键 → Pressing（记录 pressX/pressY）

Pressing
  MouseMove（移动距离 > DRAG_THRESH）→ BoxSelecting
  MouseUp（未超过阈值）→ DoPointPick → Idle

BoxSelecting
  MouseMove → 更新框选范围（实时显示蓝色选择框）
  MouseUp → DoBoxPick → Idle
```

### 点选（DoPointPick）

```
HitTest(鼠标世界坐标, PICK_THRESH)
  → 遍历 Scene 中所有实体
  → entity.GetBoundingBox() 初步过滤
  → 具体几何精确计算距离（line.DistanceToPoint 等）
  → 返回距离最近且 < PICK_THRESH 的实体 ID
```

**两个阶段过滤**：AABB 是快速粗筛，具体距离计算才是精确判断。对于线段，DistanceToPoint 是点到线段的最短距离（非无限直线距离，投影参数 t 需 clamp 到 [0,1]）。

### 框选（DoBoxPick）

```
BoxSelect(pt1, pt2)
  → 对所有实体执行包围盒相交测试

框选方向决定语义：
  左→右（窗口选择）：实体包围盒完全在框内才选中
  右→左（交叉选择）：实体包围盒与框有任何相交即选中
```

交叉选择用浅蓝填充框，窗口选择用深蓝实线框——视觉上区分两种模式（在 `ViewState::Selection` 中体现）。

### 选择集与悬停集

```cpp
unordered_set<ObjectID> m_selection;  // 当前选中
unordered_set<ObjectID> m_hovered;    // 当前悬停（鼠标附近）
unordered_set<ObjectID> m_lastSelection;  // 上一次选择快照（恢复用）
```

`Editor::GetSelection()` / `GetHovered()` 对外暴露这两个集合。`Picking::IsDirty()` 在集合变化时置位，通知 `Editor::UpdateSceneVertices()` 重建顶点（改变实体颜色）。

---

## GripEditor：四阶段夹点编辑状态机

### 状态变量

```cpp
bool m_pendingActivate = false;  // MouseDown 命中夹点，等待 MouseUp 确认
bool m_activated       = false;  // DoActivate 后，进入跟随
bool m_following       = false;  // MouseUp 后实时跟手
```

### 完整状态机

```
[空闲]  m_picking 有选中 → RebuildGrips()（自动构建夹点列表）

        MouseDown：HitTest 是否命中夹点
          ├── 命中 → m_pendingActivate = true（不立即激活）
          └── 未命中 → 不处理（交给 Picking）

[PendingActivate]
        MouseUp：以 MouseUp 位置再次 HitTest（防止抖动误触）
          ├── 命中 → DoActivate()，m_activated = m_following = true
          └── 未命中 → 取消

[Following]  实时跟手模式
        MouseMove → handler->UpdateDrag(entity, state, activeGrip, worldPos, grips)
                 → entity 几何实时更新
                 → DrawPreview（向 Overlay 写入 Ghost 线等辅助几何）

        MouseDown 左键 → DoConfirm()
          → handler->EndDrag() 产出 DragEntityEntry（before/after 数据）
          → CommandStack::Push(DragEntitiesCommand)  // 只入栈，不重新执行
          → 重置状态到空闲

        MouseDown 右键 → CancelDrag()
          → handler->CancelDrag(entity, state)（还原到快照）
          → 清空 Overlay
          → 重置状态到空闲
```

**为什么 MouseDown 命中后等到 MouseUp 才激活**：防止误触。用户可能只是在夹点附近按下然后拖拽（框选操作），MouseUp 位置仍在夹点范围内才确认是夹点编辑意图。

**为什么用 `Push` 而不是 `Execute`**：拖拽期间实体几何已经被实时修改了（`UpdateDrag` 直接改了 entity），`EndDrag` 时操作"已发生"，只需记录到撤销栈即可，不需要重新执行。

---

## IEntityGripHandler 接口

```cpp
class IEntityGripHandler {
    virtual void BuildGrips(Entity*, vector<Grip>& outGrips) = 0;
    virtual unique_ptr<IGripDragState> BeginDrag(Entity*, const Grip&) = 0;
    virtual void UpdateDrag(Entity*, IGripDragState*, const Grip& activeGrip,
                            const Point3& worldPos, vector<Grip>& grips) = 0;
    virtual void DrawPreview(Entity*, IGripDragState*, const Grip&, Overlay&) {}
    virtual bool EndDrag(Entity*, IGripDragState*, DragEntityEntry& outEntry) { return false; }
    virtual void CancelDrag(Entity*, IGripDragState*) = 0;
};
```

**按 RuntimeTypeInfo 注册**：

```cpp
// GripEditor 构造时注册
RegisterHandler<LineEntity>(make_unique<LineGripHandler>());
RegisterHandler<CircleEntity>(make_unique<CircleGripHandler>());
// ...

// 运行时查找
IEntityGripHandler* handler = m_handlers[entity->GetTypeInfo()].get();
```

用 `RuntimeTypeInfo*` 作为 map 键，精确匹配具体类型（不需要 `IsKindOf` 的链式查找）。

---

## LineGripHandler 全解

### BuildGrips：构建三个夹点

```cpp
void BuildGrips(Entity* entity, vector<Grip>& outGrips) override {
    auto* line = static_cast<LineEntity*>(entity);
    const Line& L = line->GetLine();

    outGrips.push_back({ entity->GetID(), Grip::Type::Start, L.Start,      0 });
    outGrips.push_back({ entity->GetID(), Grip::Type::Mid,   L.Midpoint(), 1 });
    outGrips.push_back({ entity->GetID(), Grip::Type::End,   L.End,        2 });
}
```

三个夹点：起点（拖动改变起点）、中点（拖动平移整条线）、终点（拖动改变终点）。

### BeginDrag：冻结快照

```cpp
unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip&) override {
    auto state = make_unique<LineDragState>();
    state->EntityId = entity->GetID();
    state->Base = static_cast<LineEntity*>(entity)->GetLine();  // 快照
    return state;
}
```

`LineDragState::Base` 是拖拽开始时的线段快照。后续所有计算都基于 `Base` 而不是当前值，避免误差累积。

### UpdateDrag：按夹点类型修改

```cpp
void UpdateDrag(Entity* entity, IGripDragState* ds, const Grip& grip,
                const Point3& worldPos, vector<Grip>& grips) override {
    auto* line  = static_cast<LineEntity*>(entity);
    auto* state = static_cast<LineDragState*>(ds);
    Line seg = state->Base;  // 总是从快照出发

    switch (grip.GripType) {
    case Grip::Type::Start: seg.Start = worldPos; break;
    case Grip::Type::End:   seg.End   = worldPos; break;
    case Grip::Type::Mid: {
        // 平移：以快照中点为参考计算 delta
        double midX = (state->Base.Start.x + state->Base.End.x) * 0.5;
        double midY = (state->Base.Start.y + state->Base.End.y) * 0.5;
        double dx = worldPos.x - midX, dy = worldPos.y - midY;
        seg.Start.x += dx; seg.Start.y += dy;
        seg.End.x   += dx; seg.End.y   += dy;
        break;
    }
    }

    line->SetLine(seg);        // 直接修改实体几何（实时更新渲染）

    // 同步夹点坐标（使夹点小方块跟着线段移动）
    for (auto& g : grips) {
        if (g.OwnerID != entity->GetID()) continue;
        // 按类型同步...
    }
}
```

**关键设计**：`UpdateDrag` 直接修改 `entity` 的几何，`Scene::MarkDirty()` 因此被触发，下一帧渲染就能看到实时变化。这就是夹点"跟手"的原理。

### DrawPreview：Ghost 线

```cpp
void DrawPreview(Entity* entity, IGripDragState* ds,
                 const Grip& activeGrip, Overlay& overlay) override {
    auto* state = static_cast<LineDragState*>(ds);
    const Color4 kGhost = { 0.55, 0.55, 0.55, 0.45 };  // 灰，半透明
    overlay.AddLine(state->Base.Start, state->Base.End, kGhost);  // 原始位置
}
```

拖拽时在原始位置画一条半透明灰线（Ghost），让用户看到"从哪里移动到哪里"。

### EndDrag：产出 before/after 数据

```cpp
bool EndDrag(Entity* entity, IGripDragState* ds, DragEntityEntry& out) override {
    out.Id         = entity->GetID();
    out.Kind       = DragEntityEntry::Kind::Line;
    out.BeforeLine = static_cast<LineDragState*>(ds)->Base;  // 快照（before）
    out.AfterLine  = static_cast<LineEntity*>(entity)->GetLine();  // 当前（after）
    return true;
}
```

`DragEntitiesCommand` 用这些 before/after 数据实现 Undo（还原到 Before）和 Redo（恢复到 After）。

### CancelDrag：还原快照

```cpp
void CancelDrag(Entity* entity, IGripDragState* ds) override {
    static_cast<LineEntity*>(entity)->SetLine(
        static_cast<LineDragState*>(ds)->Base);  // 还原到 BeginDrag 时的快照
}
```

---

## 渲染夹点

`GripEditor::GetGrips()` 返回当前所有夹点的位置和类型，`Editor::BuildViewState()` 把它们转成 `std::span<GripDraw>`，`Viewport::BuildGripGeometry()` 生成小方块顶点（实心填充 + 边框），最后通过 `IRenderer::Submit()` 以 `PrimitiveType::Triangle` 绘制。

---

## 拓展练习

1. 为 `CircleEntity` 设计 `CircleGripHandler::BuildGrips()`：圆有哪些夹点（圆心、4 个象限点）？拖圆心夹点和拖象限点夹点各改变什么？
2. `GripEditor` 为什么用索引 `m_activeGripIdx` 而不是指针 `Grip*` 来标记激活的夹点？
3. 找到 `DragEntitiesCommand`，确认它如何存储多个 `DragEntityEntry` 并实现 Undo。
4. 框选从右到左（交叉选择）时，一条线段的一端在框内、另一端在框外，会被选中吗？找到判断逻辑。

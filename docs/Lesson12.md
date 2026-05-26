# Lesson12：吸附与约束系统

## 学习目标

本课讲清楚 MiniCAD 中"鼠标落点不是鼠标点"的完整管线：输入坐标经过吸附修正和约束投影，最终变成工具收到的语义坐标。完成后学习者应能理解 `SnapEngine`、`ConstraintEngine`、`InputResolver` 三者的分工，并能新增一种吸附类型或修改约束角度。

## 相关源码

- `src/Editor/Snap/SnapEngine.h`
- `src/Editor/Snap/SnapResult.h`
- `src/Editor/Constraint/IConstraint.h`
- `src/Editor/Constraint/OrthoConstraint.h`
- `src/Editor/Constraint/PolarConstraint.h`
- `src/Editor/Constraint/ConstraintEngine.h`
- `src/Editor/Resolver/InputResolver.h`
- `src/Editor/Resolver/InputResult.h`
- `src/Editor/EditorContext.h`

## 原理

输入管线分三步，依次叠加：

```text
屏幕坐标
  ↓ Camera::ScreenToWorld()
原始世界坐标（rawPoint）
  ↓ SnapEngine::FindNearest()
吸附点（可选，snapPoint）
  ↓ ConstraintEngine::Apply()
约束点（可选，constrainedPoint）
  ↓
InputResult.point（工具最终使用的点）
```

**吸附（Snap）**：`SnapEngine` 遍历场景实体，对每个实体计算候选吸附点（端点、中点、最近点、象限点），找到距离当前鼠标最近且在吸附半径内的点。结果保存为 `SnapResult`，包含点坐标和类型。

**约束（Constraint）**：`ConstraintEngine` 持有 `OrthoConstraint` 和 `PolarConstraint`，在有锚点（来自工具的 `GetAnchor()` 或夹点的激活点）时生效。正交约束把候选点投影到最近的水平或垂直轴；极轴约束把候选点投影到距离最近的极轴射线。约束作用于**吸附后**的点，优先级高于吸附。

**InputResolver**：`InputResolver::Resolve(EditorContext&)` 负责按序调用上述步骤，把结果写入 `ctx.resolved`（即 `InputResult`）。

## MiniCAD 中的应用

`Editor::OnInput()` 每帧构建 `EditorContext` 后立即调用 `m_resolver.Resolve(ctx)`，工具在 `OnInput(ctx)` 中直接读 `ctx.resolved.point`。

快捷键绑定（在 `Editor::HandleGlobal()`）：

- `F3`：切换吸附开关 `ToggleSnap()`
- `F8`：切换正交约束 `ToggleOrtho()`
- 极轴角度通过 `SetPolarAngle()` 设置

渲染时，`SnapResult` 中的点类型和屏幕坐标由 `Editor::BuildViewState()` 写入 `ViewState.Snap`，`Viewport` 再用对应图标（方框/三角/十字等）绘制吸附标记。

## 课堂演示

1. 画一条线，移动鼠标靠近另一条线的端点，观察自动吸附到端点。
2. 按 `F3` 关闭吸附，再靠近端点，观察不再吸附。
3. 开启正交（`F8`），画线时鼠标只能沿水平或垂直方向移动。
4. 在代码中修改 `SnapEngine` 的吸附半径，观察捕捉范围变化。

## 拓展练习

1. 阅读 `SnapEngine`，找到中点吸附的计算方式，说明它如何得到线段中点。
2. 在 `OrthoConstraint` 中增加 45° 方向，使正交约束变成八方向约束。
3. 在 `InputResult` 中新增 `hasConstraint` 标记的使用，找到工具中用它做特殊处理的地方。

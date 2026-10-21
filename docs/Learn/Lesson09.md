# Lesson09：视口、相机与坐标转换

## 学习目标

读懂 `Camera` 的正交投影模型（`m_zoom` / `m_target`），理解 `GetView()` 和 `GetProj()` 各自做什么，掌握 `ScreenToWorld` / `WorldToScreen` 的数学推导，以及缩放时"鼠标下世界点不动"算法的实现细节。

---

## 相关源码

- `src/Viewport/Camera.h`
- `src/Viewport/Camera.cpp`
- `src/Viewport/Viewport.h`
- `src/Viewport/ViewState.h`

---

## 坐标系约定

| 坐标系 | 原点 | X 正方向 | Y 正方向 | 用途 |
|---|---|---|---|---|
| **屏幕坐标** | 左上角 | 向右 | 向下 | 鼠标事件像素坐标 |
| **NDC** | 中心 | 向右 | 向上 | GPU 裁剪空间 [−1, 1] |
| **世界坐标** | (0,0) | 向右 | 向上 | 图纸/CAD 坐标 |

注意屏幕 Y 向下，而世界 Y 向上——这是 `ScreenToWorld` 中 `ndcY = (-2.0 * py / height) + 1.0` 要取负号的原因。

---

## Camera 的状态

```cpp
double       m_zoom;          // 正交视口高度（世界单位），越大看的范围越大
double       m_aspect;        // 宽高比 = screenWidth / screenHeight
double       m_screenWidth;
double       m_screenHeight;
Math::Point3 m_target;        // 观察中心（世界坐标），平移时修改
Math::Mat4   m_viewProj;      // View × Proj，缓存避免重复计算
Math::Mat4   m_invViewProj;   // 逆矩阵，用于 ScreenToWorld
```

`m_zoom` 是理解一切的核心：它表示**视口在世界坐标下覆盖的高度**。
- `m_zoom = 10`：视口能看到世界 Y 方向 10 个单位
- `m_zoom = 100`：视口能看到世界 Y 方向 100 个单位（看到更大范围，图形更小）

---

## 矩阵构成

### View 矩阵：平移观察中心到原点

```cpp
Mat4 Camera::GetView() const {
    return Mat4::Translation({ -m_target.x, -m_target.y, 0.0 });
}
```

MiniCAD 是 2D 俯视图，相机始终在 Z 轴正方向俯视 XY 平面，**不需要旋转**。View 矩阵只做一件事：把 `m_target` 移到原点，使相机中心对准原点。

### Proj 矩阵：正交投影

```cpp
Mat4 Camera::GetProj() const {
    double viewWidth  = m_zoom * m_aspect;
    double viewHeight = m_zoom;
    return Mat4::OrthoLH(viewWidth, viewHeight, 0.0, 1000.0);
}
```

正交投影把 `[-viewWidth/2, viewWidth/2] × [-viewHeight/2, viewHeight/2]` 映射到 NDC `[-1, 1] × [-1, 1]`。

**为什么用正交而不是透视**：CAD 制图要求尺寸不失真——远处的线段和近处的线段在图纸上应该等长。透视投影会让远处的对象显得更小，不适合工程图。

### UpdateViewProj：预计算并缓存

```cpp
void Camera::UpdateViewProj() {
    m_viewProj    = GetView() * GetProj();
    m_invViewProj = Mat4::Inverse(m_viewProj);
}
```

每次 `Pan()`、`Zoom()`、`Resize()` 后都调用一次，之后每帧直接使用缓存结果，不需要重复乘法。

---

## ScreenToWorld

```cpp
Point3 Camera::ScreenToWorld(int px, int py) const {
    // 步骤 1：屏幕像素 → NDC
    double ndcX = (2.0 * px / m_screenWidth)  - 1.0;   // [0,W] → [-1,+1]
    double ndcY = (-2.0 * py / m_screenHeight) + 1.0;  // [0,H] → [+1,-1]（Y轴翻转）

    // 步骤 2：NDC → 世界空间（正交投影，w=1，无透视除法）
    Point3 worldPos = m_invViewProj.TransformPoint({ ndcX, ndcY, 0.0 });

    return { worldPos.x, worldPos.y, 0.0 };  // CAD 强制 Z=0
}
```

**透视投影与正交投影的差异**：透视投影中 NDC 点反变换需要除以 w 分量（透视除法），正交投影 w 恒为 1，可以直接乘逆矩阵，这也是 `TransformPoint` 内部处理的。

---

## WorldToScreen

```cpp
Point2 Camera::WorldToScreen(const Point3& worldPos) const {
    Point3 clip = m_viewProj.TransformPoint(worldPos);
    //  clip.x ∈ [-1,+1]，clip.y ∈ [-1,+1]

    double sx = (clip.x * 0.5 + 0.5) * m_screenWidth;   // [-1,+1] → [0,W]
    double sy = (-clip.y * 0.5 + 0.5) * m_screenHeight; // [+1,-1] → [0,H]（翻转）
    return { sx, sy };
}
```

夹点、吸附标记、光标框等 UI 元素的绘制位置都需要先把世界坐标转到屏幕坐标。

---

## Pan（平移）

```cpp
void Camera::Pan(double dx, double dy) {
    double worldPerPixelX = (m_zoom * m_aspect) / m_screenWidth;
    double worldPerPixelY = m_zoom / m_screenHeight;

    m_target.x -= dx * worldPerPixelX;  // 屏幕右移 = 相机向右移 = 世界点向左
    m_target.y += dy * worldPerPixelY;  // 屏幕下移（dy>0）= Y向上补偿

    UpdateViewProj();
}
```

**worldPerPixel 的意义**：1 个屏幕像素对应多少世界单位。当 `m_zoom` 大时（缩小看），一个像素对应的世界距离也大，平移速度相对"加快"；`m_zoom` 小时（放大看），平移速度"减慢"——这正是 CAD 平移的直觉感受。

---

## Zoom（缩放）：鼠标下世界点不动

```cpp
void Camera::Zoom(double delta, int mouseX, int mouseY) {
    // 1. 缩放前记录鼠标对应的世界坐标
    Point3 worldBefore = ScreenToWorld(mouseX, mouseY);

    // 2. 更新 zoom（鼠标上滚 delta>0 缩小缩放值 = 放大画面）
    constexpr double factor = 1.1;
    m_zoom = delta > 0 ? m_zoom / factor : m_zoom * factor;
    m_zoom = clamp(m_zoom, 0.01, 10000.0);

    // 3. 刷新矩阵（zoom 变了）
    UpdateViewProj();

    // 4. 缩放后同一屏幕点对应的新世界坐标（已经偏移）
    Point3 worldAfter = ScreenToWorld(mouseX, mouseY);

    // 5. 平移 target 补偿偏差
    m_target.x += worldBefore.x - worldAfter.x;
    m_target.y += worldBefore.y - worldAfter.y;

    // 6. target 变了，再刷新一次
    UpdateViewProj();
}
```

**为什么需要两次 UpdateViewProj**：第 3 步刷新是为了让第 4 步的 ScreenToWorld 使用新的 zoom，计算出偏差；第 6 步刷新是因为第 5 步修改了 target，最终生效。

**为什么这个算法能保证鼠标下世界点不动**：缩放后屏幕的同一点对应了不同的世界坐标（`worldBefore ≠ worldAfter`），通过移动 `m_target` 把偏差补回来，使鼠标下的点重新对应 `worldBefore`。

---

## Viewport 中的两套矩阵

`Viewport::Render(ViewState)` 用到两套不同的投影矩阵：

```cpp
Mat4 viewProj = m_camera.GetViewProj();        // 场景矩阵（含平移缩放）
Mat4 screenOrtho = Mat4::OrthoLH(m_width, m_height, 0, 1);  // 屏幕矩阵

// 场景实体、overlay（随相机移动）
m_renderer.Submit(viewState.Scene,   viewProj,   PrimitiveType::Line);
m_renderer.Submit(viewState.Overlay, viewProj,   PrimitiveType::Line);

// 夹点、吸附标记、选择框（固定在屏幕上）
m_renderer.Submit(gripVerts,         screenOrtho, PrimitiveType::Triangle);
m_renderer.Submit(snapVerts,         screenOrtho, PrimitiveType::Triangle);
```

**区别**：夹点小方块始终显示为固定大小（不随缩放变大），因为它们用屏幕正交矩阵渲染——坐标本身就是像素坐标，不受 `m_zoom` 影响。

---

## 坐标转换在各子系统中的使用

| 子系统 | 使用方向 | 目的 |
|---|---|---|
| `ITool::GetPoint()` | ScreenToWorld | 鼠标点击 → 图纸坐标 |
| `Picking::HitTest()` | WorldToScreen | 世界坐标 → 屏幕距离比较（阈值是像素）|
| `SnapEngine::Query()` | WorldToScreen | 吸附点 → 屏幕距离检测 |
| `GripEditor::HitTest()` | WorldToScreen | 夹点世界坐标 → 屏幕距离检测 |
| `Viewport::BuildGripGeometry()` | WorldToScreen | 夹点世界坐标 → 屏幕像素位置 |

---

## 拓展练习

1. 推导：当 `m_zoom = 20`，`m_aspect = 16/9`，屏幕 1920×1080 时，屏幕最左上角 `(0,0)` 对应的世界坐标是多少（假设 `m_target = (0,0)`）？
2. 在 `Camera::Zoom()` 中把 `factor = 1.1` 改成 `2.0`，会有什么用户体验问题？
3. `Camera::Pan()` 中 `m_target.x -= dx * worldPerPixelX`，为什么是减号？平移方向和鼠标移动方向有什么关系？
4. 为什么 `GetCameraPos()` 返回 `{m_target.x, m_target.y, m_height}`？`m_height` 在这个 2D 正交相机里有什么实际用处？

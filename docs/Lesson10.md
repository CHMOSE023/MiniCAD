# Lesson10：渲染层入门

## 学习目标

读懂 `IRenderer` 接口设计，理解两种顶点类型和两种图元类型，掌握 `Viewport::Render()` 的完整渲染顺序，理解场景矩阵和屏幕矩阵的使用时机，以及 D3D11 和 WebGL 如何通过同一接口工作。

---

## 相关源码

- `src/Render/IRenderer.h`
- `src/Render/IRenderTarget.h`
- `src/Render/VertexTypes.hpp`
- `src/Render/RendererFactory.hpp`
- `src/Viewport/Viewport.h` / `Viewport.cpp`
- `src/Viewport/ViewState.h`

---

## 顶点类型

```cpp
struct Vertex_P3_C4 {
    Math::Float3 pos;    // xyz 位置
    Math::Float4 color;  // rgba 颜色
};

struct Vertex_P3_C4_UV {
    Math::Float3 pos;
    Math::Float4 color;
    Math::Float2 uv;     // 纹理坐标（文字用）
};
```

**Float3/Float4 vs Point3/Vec3/Color4**：渲染层用 `Float3/Float4`（单精度，GPU 格式），Core 层用 `Point3/Vec3/Color4`（双精度，计算用）。`DrawContext` 负责在两者之间做类型转换：

```cpp
// DrawContext 中的转换
m_verts.push_back({
    { (float)pt.x, (float)pt.y, (float)pt.z },
    { (float)col.r, (float)col.g, (float)col.b, (float)col.a }
});
```

---

## IRenderer 接口

```cpp
class IRenderer {
    virtual void BeginFrame(IRenderTarget& target, const ViewportDesc& viewport) = 0;
    virtual void EndFrame() = 0;
    virtual void Submit(
        span<const Vertex_P3_C4> verts,
        const Mat4& viewProj,
        PrimitiveType type,
        bool depth = true,
        bool blend = false) = 0;
    virtual void SubmitTextured(
        span<const Vertex_P3_C4_UV> verts,
        const Mat4& viewProj,
        void* nativeSRV,
        bool depth = false,
        bool blend = true) = 0;
    virtual void* GetNativeDevice() = 0;
};

enum class PrimitiveType { Line, Triangle };
```

**参数说明**：

| 参数 | 含义 |
|---|---|
| `IRenderTarget&` | 渲染目标（D3D11 RTV 或 WebGL Framebuffer） |
| `ViewportDesc` | 视口区域（x, y, width, height, minDepth, maxDepth）|
| `span<verts>` | 零拷贝顶点数据引用 |
| `viewProj` | MVP 矩阵（场景用相机矩阵，UI 用屏幕正交矩阵）|
| `PrimitiveType::Line` | 线列表：每两个顶点一条线段 |
| `PrimitiveType::Triangle` | 三角形列表：每三个顶点一个三角形 |
| `depth` | 是否启用深度测试（2D 场景通常不需要）|
| `blend` | 是否启用 alpha 混合（半透明元素需要）|
| `nativeSRV` | D3D11：`ID3D11ShaderResourceView*`；WebGL：`GLuint` 纹理 ID |

---

## RendererFactory：平台路由

```cpp
// RendererFactory.hpp
#ifdef MINICAD_WEB
    // WebGLRenderer 构造
#else
    // D3D11Renderer 构造
#endif
```

`CreateRenderer(RendererCreateInfo)` 根据编译时宏返回对应实现。调用方（`MainWindow::InitD3D11` 或 `WebMain::main`）只持有 `IRenderer*`，不知道底层是 D3D11 还是WebGL。

---

## Viewport::Render 完整渲染顺序

以下是每帧 `Viewport::Render(viewState)` 的执行步骤：

```
1. BeginFrame(renderTarget, viewportDesc)
   └─ 绑定渲染目标，设置视口矩形

2. 绘制网格（Grid）                           ← 屏幕正交矩阵
   └─ 等间距水平/垂直虚线

3. 绘制坐标轴（Axis）                         ← 场景矩阵（随相机移动）
   └─ X 轴红色，Y 轴绿色

4. 绘制 Gizmo（左下角坐标指示器）              ← 屏幕正交矩阵（固定位置）

5. 绘制光标框（Cursor）                       ← 屏幕正交矩阵（跟随鼠标）
   └─ 十字光标 + 中心小方框

6. 绘制场景实体：Submit(viewState.Scene, viewProj, Line)
   └─ 所有 Entity::Draw 产生的线段顶点

7. 绘制 Overlay：Submit(viewState.Overlay, viewProj, Line)
   └─ 工具预览线、约束辅助线

8. 绘制文字：SubmitTextured(viewState.TextScene, viewProj, fontTexture, blend=true)
   └─ TextEntity 产生的纹理四边形，使用 ImGui 字体图集

9. 绘制吸附标记：BuildSnapGeometry → Submit(snap, screenOrtho, Triangle)
   └─ 端点吸附：小方框；中点：三角形；最近点：X 形

10. 绘制选择框：BuildSelectionGeometry → Submit(fill + border, screenOrtho, Triangle+Line)
    └─ 半透明填充 + 实线边框

11. 绘制夹点：BuildGripGeometry → Submit(fill + border, screenOrtho, Triangle+Line)
    └─ 每个夹点：实心小方块 + 边框

12. EndFrame()
    └─ D3D11：Present 前的收尾；WebGL：flush
```

---

## 两套矩阵的使用规则

| 元素 | 使用矩阵 | 原因 |
|---|---|---|
| 场景实体（线段、圆等） | `camera.GetViewProj()` | 随相机平移/缩放 |
| 工具预览（Overlay） | `camera.GetViewProj()` | 是图纸上的几何 |
| 文字（TextScene） | `camera.GetViewProj()` | 文字附着在图纸上，随相机变换 |
| 网格 | 屏幕正交 | 始终铺满视口 |
| 夹点方块 | 屏幕正交 | 固定像素大小，不随缩放变化 |
| 吸附标记 | 屏幕正交 | 固定像素大小 |
| 选择框 | 屏幕正交 | 像素拖拽，与图纸无关 |
| 光标框 | 屏幕正交 | 跟随鼠标像素位置 |

---

## 为什么文字用 SubmitTextured

`TextEntity`（单行文字）的字形来自 ImGui 字体图集（一张预烘焙的字形纹理）。`DrawContext::EmitText()` 把每个字符生成两个三角形（一个矩形），UV 坐标指向字形在图集中的位置。渲染时需要把这张纹理绑定到 shader 才能采样——这就是 `SubmitTextured` 的 `nativeSRV` 参数。

`MTextEntity`（多行文字，SHX/TTF 矢量字体）则走 `DrawLine` 路径，字形是线段，直接进入 `m_sceneVertices`，走普通的 `Submit`。

---

## D3D11Renderer 要点（了解）

- `BeginFrame`：调用 `ClearRenderTargetView`，设置 `RSSetViewports`
- `Submit(PrimitiveType::Line)`：`IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST)`
- `Submit(PrimitiveType::Triangle)`：`IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)`
- 每次 `Submit` 把顶点数据 `Map → memcpy → Unmap` 到动态 Vertex Buffer

## WebGLRenderer 要点（了解）

- `BeginFrame`：`glViewport`，`glClear`
- `Submit(PrimitiveType::Line)`：`glDrawArrays(GL_LINES, ...)`
- `Submit(PrimitiveType::Triangle)`：`glDrawArrays(GL_TRIANGLES, ...)`
- 每次 `Submit` 用 `glBufferData` 上传数据（动态缓冲）

两者都是**"每帧重传顶点"**的简单模型，没有静态 VBO 池或分批合并优化。

---

## 拓展练习

1. 追踪一个圆形实体从 `CircleEntity::Draw()` 到 `IRenderer::Submit()` 的完整路径，数一数中间经过了几次数据传递。
2. `SubmitTextured` 的 `nativeSRV` 参数类型是 `void*`，为什么不用模板或接口？这种设计有什么优缺点？
3. 如果要新增一种渲染效果（例如：选中的实体用虚线显示），需要改动哪些文件？修改范围有多大？
4. 夹点、吸附标记用 `PrimitiveType::Triangle` 而不是 `Line`，为什么？（提示：想想如何绘制一个实心方块）

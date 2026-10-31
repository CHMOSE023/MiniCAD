---
covers: src/Viewport/Camera.*, src/Viewport/ViewState.h, apps/Common/src/GUI/ViewCube.*, src/Core/Entity/Solid3DEntity.hpp
---

# 三维视图

## 概述

相机有视线方向（`CameraState::Azimuth` / `Elevation`）；俯视（`Camera::IsPlanView`）时与二维完全相同。整体计划见 `docs/Solid/PLAN.md` 第 6 节。

## 实现要点

- 深度：三维视图按场景范围设置深度，着色面写深度、线只测试深度（渲染目标都带深度缓冲）
- 着色面与轮廓线由 `Editor` 在 CPU 上随视线方向生成（`ViewState::Shaded` / `Silhouettes`），明暗写在顶点颜色里，不需要新的着色器；顶点与场景流一样相对渲染原点（`ViewState::RenderOrigin`，见 [display-performance](display-performance.md) 的"大坐标：渲染原点"）。三维视图的网格与坐标轴（`Grid::BuildGrid3D`）按世界坐标生成，用合进原点之前的矩阵
- 三维实体 `Solid3DEntity`：内核数据 + 变换矩阵，编辑只改矩阵；`MeshEntity` 继承它，只有局部网格、没有内核数据。实体内核只经 `ISolidKernel` 使用
- 命令：标准视图 `ViewTop` / `ViewSWIso` 等，Shift + 中键环绕，视觉样式 `Shade` / `Wireframe`
- 视图方块 `GUI/ViewCube`（绘图区右上角）：点面 / 棱 / 角或东南西北切换视图、拖动环绕；右键菜单或命令 `NAVVCUBE` 隐藏 / 显示，开关存进 `layout.json`

## 背景深浅

`IRenderer::SetLightBackground`（7 号色语义）：浅色背景时纯白顶点画成纯黑、深色背景时纯黑画成纯白，在两个渲染器的顶点着色器里处理（光栅图像不变）。`MainFrame::RenderFrame` 每帧按界面主题设置 `Viewport::SetLightBackground`；图层颜色块在 `LayerPanels.cpp` 的 `SwatchColor` 里做同样的反转。

## 测试

- `SolidViewTests`；三维实体与建模工具见 `Solid3DEntityTests`、`SolidToolTests`

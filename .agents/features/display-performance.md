---
covers: src/Editor/Editor.*, src/Viewport/Viewport.*, src/Document/DrawContext.hpp, src/Document/WipeClip.hpp, src/Render/*, apps/Render/*
---

# 显示性能

改实体绘制、编辑器的显示流、视口或渲染器之前读。改完用 `MiniCADWin.exe --bench 图纸` 前后对比（打印每帧耗时、是否重建与内存；Release 版要重定向输出）。

## 缓存与版本

- 拾取的空间索引按 `Scene::GeometryVersion()` 缓存与失效
- 渲染器按 (slot, version) 缓存顶点缓冲，版本没变时不重新上传；渲染缓存的版本号由 `Editor::NextRenderVersion` 在进程内全局分配，多个编辑器共用一个渲染器也不会串缓冲
- 视口缓存层：场景版本、相机、尺寸、显示开关、背景深浅都没变时，每帧只复制缓存层再画动态内容
- 每个文档一个 `Editor`，显示数据、选择集、工具状态随文档保留；切换文档只请编辑器重新拼接显示流，不重新细分

## 静态流与动态流

- 随视图变化的实体（按像素等宽的线宽、按像素画的点样式、按屏幕大小的文字）绘制时调用 `IDrawSink::MarkViewDependent()`，线与填充进**动态流**；其余进**静态流**
- 滚轮缩放只重建动态流（`Editor::RebuildDynamicStream`），静态流不重新拼接、不重新上传
- 虚线的几何在世界坐标里（线型的"点"笔划走点流，恒定 1 像素）；不超过 200 个周期的虚线不随缩放重建。更长的（`DrawContext::kStaticMaxDashes`）"延迟铺设"（`DeferredDash`：细分时只记下端点、颜色、线型，实体本身仍进静态流；`Editor::EmitDeferredDashes` 按当前缩放铺进动态流，一个周期不足 4px 时画成实线）
- 拼接后的静态流上传 GPU 后即释放（`Viewport::SceneUploaded` → `Editor::ReleaseUploadedSceneStreams`），CPU 端只留逐实体缓存；需要时（追加实体、渲染器 `ReleaseCachedResources` 后 `RequestSceneRestitch`）从缓存重新拼接，不重新细分
- 顶点颜色为 RGBA8（`Vertex_P3_C4` 16 字节）；D3D11 的大缓冲（≥ 4 MB）用 DEFAULT 用途按实际大小创建，避免 WRITE_DISCARD 重命名造成的内存抖动。NVIDIA 驱动会为上传的顶点缓冲在进程里留一份同样大小的系统内存，GPU 数据量直接计入私有内存（D3D11 与 D3D12 都是：默认堆的缓冲多少 MB，私有内存就涨多少 MB）
- D3D12（`D3D12Renderer` / `D3D12Backend`，共用 `MiniGUI::D3D12Context`）：缓存提交放在默认堆的缓冲里，规则同 D3D11（小的按 1.5 倍预留，≥ 4 MB 按实际大小）；数据经上传页复制（`D3D12Context::AllocateUpload`：默认 1 MB 一页，GPU 用完（围栏）后整页复用，空闲 60 帧归还）。超过 4 MB 的上传单独建页，`D3D12Context::UploadBuffer` 录完复制就提交并等 GPU，暂存区与换下的旧缓冲当场释放——否则换图纸、切回文档的那一帧会同时留着新旧两份加暂存区，私有内存比 D3D11 多出几十 MB。换下的缓冲、纹理一律 `D3D12Context::Defer`，执行完已录制的命令再释放；常量用根常量，没有常量缓冲

## 文字

矢量文字按字形实例化：`DrawContext` 把每个字输出为一条 `GlyphInstance`（36 字节，局部 → 世界的仿射变换 + 颜色），字形网格在 `GlyphMeshLibrary` 里每个字形只存一份；实例流按网格排序分批，用 `DrawInstanced` / `drawArraysInstanced` 绘制（字形三角形画在填充之后、线之前，轮廓线画在线之后）。被区域覆盖盖到的实例由 `WipeClip::Batch::ClipGlyphs` 展开成顶点再裁剪；选中流仍按顶点展开。

## 选择集与夹点

- 选中流优先从逐实体缓存复制并改成选中色（`Editor::UpdateSelectionVertices`，不重新细分；拖动中、带光栅图像、缓存里没有的实体仍实时绘制），分静态与随视图变化两路（`ViewState::SelScene` / `SelDyn`），缩放时只重建后者
- 夹点超过 `GripEditor::kGripObjectLimit`（100 个对象，同 AutoCAD 的 GRIPOBJLIMIT）不显示；夹点几何在 `Viewport` 里按内容缓存
- 框选先用包围盒粗筛（俯视时包围盒完全在框内直接选中），块、标注、填充的精确判定用 `Picking::GetPickGeometry` 的缓存
- 特性面板与工具栏下拉框的选择集统计按选择集版本（`Picking::SelectionVersion`）与场景版本缓存（`MainFrame::SelectionInfo`）

## 已知的热点

- `SnapEngine` 每帧 O(n)，大图纸时可考虑接入空间索引
- 布局页视口的合成结果不随缩放重建，见 [layout-viewport](layout-viewport.md)

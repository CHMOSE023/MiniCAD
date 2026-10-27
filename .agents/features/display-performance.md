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
- 拼接后的静态流上传 GPU 后即释放（`Viewport::SceneUploaded` → `Editor::ReleaseUploadedSceneStreams`），CPU 端只留逐实体缓存；需要时（渲染器 `ReleaseCachedResources` 后 `RequestSceneRestitch`、修改 / 删除实体）从缓存重新拼接，不重新细分
- 拼接流释放后的增量更新（`Editor::UpdateSceneIncremental`）：后端能接追加段、原地改写（`IRenderer::SupportsCachedAppend`，D3D11 / D3D12 支持；宿主在 `MainFrame::InitDocument` 里 `Editor::SetSceneAppendOnGpu`）时，脏实体只细分自己，不从逐实体缓存重新拼接整张图
  - 新实体：静态顶点放进追加段（`ViewState::SceneAppendActive`），`Viewport::Render` 经 `IRenderer::AppendCached` 接到 (slot, 基础版本) 缓冲末尾并升级版本——放得下就写到末尾，放不下就新建多留 1/8 容量的缓冲在 GPU 内复制
  - 修改 / 删除 / 撤销 / 夹点拖动的起止：逐实体缓存记着每个实体在静态流与字形实例流里的位置（`placed` 与各 `*Off`），旧的那一段经 `IRenderer::WriteCached` 改写成透明的退化图元（改写段 `ViewState::ScenePatches`，先写再追加），新顶点放得下就写回原位、放不下就追加；字形实例流按拼接序保存（`m_glyphInstances`，作废的洞标成 `kGlyphInstanceDead`），交给后端的是按网格计数排序的副本（`Editor::SortSceneGlyphs`）。拖动开始时被拖实体那段清掉（它由选中流实时画），放下时重新拼进去。洞在下次全量拼接时清掉。修剪 / 延伸 / 偏移 / 圆角 / 拉伸（`GeometryEditCommand`，同一 ID 换几何）、复制 / 阵列 / 镜像 / 分解 / 定义块（`AddEntity` / `RemoveEntity`）都按实体标脏，自然走这条路；撤销 / 重做靠命令自己标脏，`Editor::HandleGlobal` 与定义块之后不再整体标脏（整体标脏 = 所有实体重新细分，几十万实体要几十秒）
  - 全量拼接的情形：区域覆盖相关（新实体带区域覆盖、改动的实体碰到已拼进去的区域覆盖的包围盒、实体那段被裁剪挪动过——`WipeClip::Batch::FirstTouched` 之后的位置作废）、带纹理文字 / 图像的实体改动、`RequestSceneRestitch`、后端接不上或写不进（这一帧按基础版本画旧内容，`MainFrame` 见 `SceneUploaded` 为假就 `RequestSceneRestitch`）；CPU 端还有拼接流时（上传前那一帧、后端不支持追加）修改 / 删除也全量拼接，新增直接接到流尾
  - 开着追加能力时文字顶点与字形实例留在 CPU 端，有自己的版本号（`ViewState::SceneGlyphVersion`）：只有改动带文字的实体才重排、重传实例流，画一条线不动几十万个字形实例
  - 效果用 `--bench` 的"新增直线""移动""夹点拖动""删除""撤销"几项看：3000 万顶点的图纸，画一条直线的下一帧从 345 ms 降到 27 ms，移动 / 删除 / 撤销 / 夹点拖起与放下从 400～460 ms 降到与新画一条线相同（同一次运行里都是 60 ms 上下），剩下的是缓存层重画；第一次追加要新建缓冲、GPU 内复制，多 10 ms 左右。Vulkan / GLES3 / WebGPU 后端还没实现追加与改写，仍是全量拼接
- 顶点颜色为 RGBA8（`Vertex_P3_C4` 16 字节）；D3D11 的大缓冲（≥ 4 MB）用 DEFAULT 用途按实际大小创建，避免 WRITE_DISCARD 重命名造成的内存抖动。NVIDIA 驱动会为上传的顶点缓冲在进程里留一份同样大小的系统内存，GPU 数据量直接计入私有内存（D3D11 与 D3D12 都是：默认堆的缓冲多少 MB，私有内存就涨多少 MB）
- D3D12（`D3D12Renderer` / `D3D12Backend`，共用 `MiniGUI::D3D12Context`）：缓存提交放在默认堆的缓冲里，规则同 D3D11（小的按 1.5 倍预留，≥ 4 MB 按实际大小）；数据经上传页复制（`D3D12Context::AllocateUpload`：默认 1 MB 一页，GPU 用完（围栏）后整页复用，空闲 60 帧归还）。超过 4 MB 的上传单独建页，`D3D12Context::UploadBuffer` 录完复制就提交并等 GPU，暂存区与换下的旧缓冲当场释放——否则换图纸、切回文档的那一帧会同时留着新旧两份加暂存区，私有内存比 D3D11 多出几十 MB。换下的缓冲、纹理一律 `D3D12Context::Defer`，执行完已录制的命令再释放；常量用根常量，没有常量缓冲
- 多窗口：停靠面板拖出的独立窗口各有一个 `IWindowSurface`（`IGraphicsDevice::CreateWindowSurface`）——D3D11 再建一个 `SwapChain`，D3D12 的交换链抽成了 `D3D12WindowSurface`（主窗口也用它），每个窗口的一帧是一次完整的 BeginPass → 界面 Render → Present（D3D12 即一次提交与 EndFrame）。Vulkan / GLES3 / WebGPU 不支持，返回 nullptr，面板退回窗口内浮动
- Vulkan（Linux，`VulkanRenderer` / `VulkanBackend`，共用 `MiniGUI::VulkanContext`）：缓存提交放在显存的缓冲里，规则同 D3D12；帧内数据走 1 MB 的上传页（主机可见，直接当顶点 / 索引缓冲用），超过 1 MB 的单独建页，超过 4 MB 的与换下的大缓冲在帧末等 GPU 执行完就释放。复制、布局转换要先结束动态渲染（`VulkanContext::EndRendering`），所以一帧里上传越少、渲染被打断得越少
- WebGPU（网页）：缓冲写入在提交前统一生效，一帧里同一块缓冲不能写两次——矩阵占帧内 uniform 环形缓冲的一段，不缓存的顶点写帧内流缓冲的不同区段，缓存槽在一帧里只按一个版本上传；换下来的资源提交后再销毁（`WebGPUCommon::RetireList`）。见 `docs/Render/PLAN.md` 阶段 5

## 文字

矢量文字按字形实例化：`DrawContext` 把每个字输出为一条 `GlyphInstance`（36 字节，局部 → 世界的仿射变换 + 颜色），字形网格在 `GlyphMeshLibrary` 里每个字形只存一份；实例流按网格排序分批，用 `DrawInstanced` / `drawArraysInstanced` 绘制（字形三角形画在填充之后、线之前，轮廓线画在线之后）。被区域覆盖盖到的实例由 `WipeClip::Batch::ClipGlyphs` 展开成顶点再裁剪；选中流仍按顶点展开。

## 选择集与夹点

- 选中流优先从逐实体缓存复制并改成选中色（`Editor::UpdateSelectionVertices`，不重新细分；拖动中、带光栅图像、缓存里没有的实体仍实时绘制），分静态与随视图变化两路（`ViewState::SelScene` / `SelDyn`），缩放时只重建后者
- 夹点拖动中的实体不在场景流里（`m_sceneExcluded`），由选中流逐帧画；拖起、放下各改一次场景流（大图纸走上面的增量更新，只动它那一段）
- 夹点超过 `GripEditor::kGripObjectLimit`（100 个对象，同 AutoCAD 的 GRIPOBJLIMIT）不显示；夹点几何在 `Viewport` 里按内容缓存
- 框选先用包围盒粗筛（俯视时包围盒完全在框内直接选中），块、标注、填充的精确判定用 `Picking::GetPickGeometry` 的缓存
- 特性面板与工具栏下拉框的选择集统计按选择集版本（`Picking::SelectionVersion`）与场景版本缓存（`MainFrame::SelectionInfo`）

## 已知的热点

- `SnapEngine` 每帧 O(n)，大图纸时可考虑接入空间索引
- 布局页视口的合成结果不随缩放重建，见 [layout-viewport](layout-viewport.md)

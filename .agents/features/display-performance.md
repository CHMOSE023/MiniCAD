---
covers: src/Editor/Editor.*, src/Viewport/Viewport.*, src/Document/DrawContext.hpp, src/Document/BlockTessCache.hpp, src/Document/WipeClip.hpp, src/Render/*, apps/Render/*, src/Core/Draw/IDrawSink.hpp, src/Core/Draw/ClipDrawSink.hpp, src/Core/Draw/CurveSampling.hpp
---

# 显示性能

改实体绘制、编辑器的显示流、视口或渲染器之前读。改完用 `MiniCADWin.exe --bench 图纸` 前后对比（打印每帧耗时、是否重建与内存；Release 版要重定向输出）。

## 缓存与版本

- 拾取的空间索引按 `Scene::GeometryVersion()` 缓存与失效
- 渲染器按 (slot, version) 缓存顶点缓冲，版本没变时不重新上传；渲染缓存的版本号由 `Editor::NextRenderVersion` 在进程内全局分配，多个编辑器共用一个渲染器也不会串缓冲
- 视口缓存层：场景版本、相机、尺寸、显示开关、背景深浅都没变时，每帧只复制缓存层再画动态内容
- 每个文档一个 `Editor`，显示数据、选择集、工具状态随文档保留；切换文档只请编辑器重新拼接显示流，不重新细分
- 多视口（文档拖出成独立窗口，见 [main-window](main-window.md)）：每个窗口一个窗格（`DocumentManager::CreatePane`），窗格里的文档共用它的 `Viewport`（相机、尺寸、缓存层各窗格独立；文档在窗格之间移动用 `DocumentManager::MoveDocument`，编辑器经 `Editor::SetViewport` 换视口，显示数据、选择集保留，缩放不同时下一帧重建动态流）。渲染器只有一个，按槽位各留一份缓冲：几个视口轮流重画场景时会互相顶掉，所以每个视口渲染前调用 `Editor::EnsureSceneCached`——拼接流已释放而后端里没有自己的场景缓冲就重新拼接（不重新细分）再上传，否则会按空流上传、图纸消失。缓存层有效时（只动鼠标、选择）不碰场景槽位，轮流平移 / 缩放两个窗口才各付一次"切换文档"的代价。网格 / 坐标轴 / Gizmo 的版本号也进程内全局分配（`Viewport::NextViewVersion`），两个视口不会被当成同一份缓冲

## 静态流与动态流

- 随视图变化的实体（按像素等宽的线宽、按像素画的点样式、按屏幕大小的文字）绘制时调用 `IDrawSink::MarkViewDependent()`，线与填充进**动态流**；其余进**静态流**
- 滚轮缩放只重建动态流（`Editor::RebuildDynamicStream`），静态流不重新拼接、不重新上传
- 虚线的几何在世界坐标里（线型的"点"笔划走点流，恒定 1 像素）；不超过 200 个周期的虚线不随缩放重建。更长的（`DrawContext::kStaticMaxDashes`）"延迟铺设"（`DeferredDash`：细分时只记下端点、颜色、线型，实体本身仍进静态流；`Editor::EmitDeferredDashes` 按当前缩放铺进动态流，一个周期不足 4px 时画成实线）。带文字 / 形符号的复杂线型不延迟也不按像素取舍（符号是世界坐标的字形实例，`Editor::RebuildDynamicStream` 不动字形实例流），超过 `DrawContext::kMaxDashes` 个周期时画成实线、不画符号
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

## 大坐标：渲染原点

- 测绘坐标的图纸（如 `tests/Data/地形测试.dwg`，x≈53 万、y≈368 万）直接把世界坐标转 float 只剩 0.25 的分辨率，视图投影矩阵里几百万的平移转成 float 后再与顶点相减又丢一截：放大后线画偏几十个像素，画线的橡皮筋一格一格地跳，而夹点、捕捉标记、框选框是按双精度换算的屏幕坐标，看起来"不在线上"
- 所有世界坐标的顶点流（场景静态 / 动态流、点流、字形实例的平移、纹理文字、图像、选中流、悬停高亮、`Overlay`、着色面与轮廓线、节点图预览）都存"世界坐标 − 渲染原点"（`ToRenderFloat3`）：`DrawContext::SetRenderOrigin`，`Overlay` 内部存双精度、`Overlay::ToVertices` 时才减；区域覆盖与视口合成的裁剪多边形同样减去原点（与顶点比较），块定义级缓存记的仍是世界坐标、延迟长虚线段的端点也是世界坐标（铺的时候减，`DrawContext::EmitDeferredDash` 的 origin 参数）
- `ViewState::RenderOrigin` 交给 `Viewport::Render`，原点平移在双精度里乘进 `vp` / `vpLines` / `vpSilhouettes`，GPU 拿到的矩阵平移量只剩视图中心与原点之差。三维视图的网格与坐标轴按世界坐标生成，用合进原点之前的矩阵
- 原点在所有实体重新细分时（`Editor::UpdateSceneVertices` 的 invalidateAll：打开、`Scene::MarkDirty`、`Scene::MarkDisplayDirty`）由 `Editor::ChooseRenderOrigin` 重选：可见实体外包框中心的中位数（各轴分开，等间隔抽至多 1024 个；远处零散的几个实体拉不动它），取 `Editor::kRenderOriginGrid`（4096）的整数倍——图纸在原点附近时原点就是 0，顶点与以前一样。改动的实体离原点超过 `Editor::kRenderRebaseDistance` 且超过选原点时可见范围的 4 倍（`Editor::NeedsRenderRebase`，如空图纸里画到测绘坐标上）时也整体重新细分、换原点
- 读顶点流的测试与自测要加回原点：`Editor::GetRenderOrigin`；`Editor::ForEachCachedVertex` 已加回。回归测试 `tests/Core/RenderOriginTests.cpp` 按 GPU 的 float 运算把顶点换到屏幕，与 `Camera::WorldToScreen` 比（场景线、选中流、夹点、橡皮筋、深度放大）
- 核对：`--bench` 加 `MINICAD_BENCH_SHOT` / `MINICAD_BENCH_VIEWS` 放大到几个单位宽的窗口截图（打印的画面角点有 4 位小数，按它把原图坐标换成像素）。地形测试.dwg 在约 4.7 单位宽的视窗里，折线到最近亮像素的距离从平均 20 像素（最多 43）降到 0.2 像素

## 深度放大：局部显示与曲线加密

- 全图一个原点、圆每圈 64 段，只够常规缩放：放到最大（可视高度 `Camera` 的下限 0.01）时，原点附近的普通图纸坐标几百就差几个像素、上千差十几像素（float），R21 的圆 64 段折线离真圆 0.025，即 2500 像素（issue #29：看着线交不上、圆上取的点不在圆上；几何数据本身是双精度的捕捉结果，没错）
- 曲线细分（`src/Core/Draw/CurveSampling.hpp`）：圆、圆弧、椭圆、多段线的圆弧段（含带宽度的，`PolylineEntity` 的 `BuildNodes` 与虚线粗线）先按各自原来的段数均分（`SampleCurveSpans`，不加密时画出的点与以前逐位相同），再逐段问 `IDrawSink::RefineCurveSegment` 要不要二分。默认不加密；`TransformDrawSink` 把弦端点经插入变换、弦高与伸出量按最大轴向缩放放大后转发，`ClipDrawSink` 原样转发，`DrawContext::SetCurveRefinement` 与悬停高亮按 `CurveRefinement` 判断：靠近范围、弦高超过容差的段要加密。天正对象、填充、样条、文字（SHX 字形）里的圆弧仍按自己的段数
- 局部显示（`Editor::UpdateLocalDisplay`，每帧在拾取索引同步之后判断，相机、视口、几何版本都没变时跳过）：视图附近（向四周各扩一个视图）的实体不多（块参照按块内子实体计，不超过 `Editor::kLocalMaxWeight`），且全图显示在这个缩放下 float 误差超过 1/4 像素、或那里按默认段数细分的曲线棱角（半径 × 0.0012）超过半个像素时，只细分那个范围里的实体（`isShown` 按 `LocalDisplay::ids` 过滤，范围的 z 取全范围：测点块参照的 z 是高程）：原点取在范围中心（`Editor::ChooseRenderOrigin`），圆弧在范围里按弦高 ≤ 1/4 像素加密（`Editor::LocalRefinement`），不用块定义级缓存（加密只在范围里，接到别的插入上不对）。视图出了范围或缩放出了建立时的一半到两倍就换范围（整体重新细分范围里的实体）；已在局部显示时门槛放宽（float 0.1 像素、棱角 1/4 像素、实体数 2 倍），免得在临界处来回切换
- 查视图附近的结果记在 `Editor::LocalEval`（范围、是否太多、曲线最大半径）：视图还在它的范围里、范围不比视图大 6 倍以上时不再查拾取索引，只按当前缩放重算误差；包围盒取拾取索引里记的（`Picking::GetIndexedBox`），曲线半径从大的实体看起。不缓存时每次相机变化都查一遍、逐个算包围盒，CK-05 平移每帧从 25 ms 变成 245 ms（调试版）；缓存后与不判断时相同
- 全图的逐实体缓存进局部显示时收起来（`m_globalStash`），缩小回来（`Editor::LeaveLocalDisplay`）拿回来重新拼接、不重新细分；局部显示期间改动过的实体与按当时缩放烘焙的（`viewDependent`）丢掉重新细分，全图的显示设置变了（`Scene::MarkDisplayDirty` 等）整个作废。局部显示期间改动的实体按包围盒重新判断在不在范围里；选中流里范围外的实体不画（拖动中的照画）
- 三维视图、布局页里的模型视口照常（局部显示只在俯视时）；进出局部显示都是一次整体重新拼接，拼接流交给后端释放过时要重新上传
- 回归测试：`tests/Core/RenderOriginTests.cpp` 的"放到最大"一段（原点附近、带标高的 R21 圆与直线：圆离真圆 < 0.5 像素、直线 < 0.2 像素、顶点数没有暴涨，局部显示期间新画的线缩小回来仍在）。截图核对：地形测试.dwg 测点圆盘（带宽度的多段线）放到最大，边缘离真圆平均 0.24 像素（改前是 5° 一段的折线，折角处差约 15 像素）

## 打开图纸：分帧细分

- 打开的图纸加入后（`MainFrame::AddOpened`）调用 `Editor::BeginProgressiveBuild`：全量拼接不再当帧细分缓存里没有的实体，而是按绘制序排进队列，每帧细分一批（`Editor::ContinueBuild`），用时到本帧时限就停，细分出来的接到场景流尾部——拼接流还在 CPU 端时直接接上（区域覆盖照常裁剪此前的顶点），已交给后端时进追加段（同上面的增量更新）。先细分好的先显示，窗口一直响应；画全后恢复同步细分（编辑、切换布局、改显示设置仍当帧画完）
- 时限：默认 `Editor::kProgressiveBudgetMs`（50 ms），两帧细分之间的开销（上传、整图重画、界面）大时放宽到它的 3 倍、最多预算的 4 倍，免得大半时间花在帧间开销上
- 大块参照按块内子实体分段（`DrawContext::BeginChunk`）：`InsertEntity::Draw` 画每个子实体前问 `IDrawSink::BeginBlockChild`（`TransformDrawSink`、`ClipDrawSink` 转发），到时限就停，停下的位置记成路径（每层嵌套块参照里第几个子实体），下一帧接着画时路径之前的子实体整个跳过、不进已画完的嵌套块参照——不能用"第几个子实体"的扁平计数：几十万个子实体的块每帧要从头数一遍，越画越慢。画着的子实体里面（天正对象、标注里再套的块）不分段。各段接进流里；整个实体画完才进逐实体缓存：后面几段里的区域覆盖要裁剪它之前的顶点，记下来（`ProgressiveBuild::partWipes`）等画完或全量拼接要重新拼进已画的几段时一次裁剪（`Editor::ClipBuildPart`；每段都裁一遍已累积的几百万个顶点太慢），缓存里的结果与一次细分完的相同。分几段接上的实体位置不连续（`placed` 为假），之后修改它走全量拼接
- 画全后全量拼接一次（全部命中缓存）的情形：追加段里有没裁剪的区域覆盖（已上传的顶点裁不到）、细分期间图纸有改动（改动的实体由增量更新接到流尾，绘制序不同）、分段的实体里有随视图变化的段。细分到一半的块参照被改动 / 删除 / 拖动、全局显示失效（改图层显示等）时丢掉它已细分的几段，全量拼接后接着分帧
- 后端接不了追加段（Vulkan / GLES3 / WebGPU）时每帧从逐实体缓存全量拼接、在 CPU 端的流上接着细分，本帧细分时限至少是拼接用时的两倍
- 进度（`Editor::DisplayBuildProgress`）按实体个数估算，块参照按块内子实体个数（嵌套展开，按块定义缓存）
- `--bench` 打印"首帧"（第一次出图，含画出的顶点数）、"画全"（帧数、最慢一帧）与打开后多久第一次出图、多久画全。2026-10-28 本机（GTX 1060，D3D12）：CK-05（`tests/Data/TchElec`）打开后 2.8 s 才出图 → 1.35 s 第一次出图（这一帧主要是拾取索引，几乎还是空的，再过约 0.15 s 有 30 万个线顶点）、3.4 s 画全（约 25 帧，最慢一帧 0.35 s）；CK-T（`tests/Data/TchHvac`）3.6 s → 1.1 s、4.9 s 画全（约 35 帧，最慢约 0.3 s，是画全后的那次全量拼接）。画全比同步细分晚 20%～35%（每帧的上传、整图重画），换来窗口不再"未响应"；与同步细分的截图逐字节相同。量的时候旧版 exe 要放在 `out/release/MiniCADWin` 里（资源目录是 exe 所在目录，放到别处缺字体、填充图案，慢十几倍）

## 块定义级细分缓存

- 同一块定义插入多次时只细分一次（`BlockTessCache`，`src/Document/BlockTessCache.hpp`）：`InsertEntity::Draw` 画每个阵列单元前问 `IDrawSink::BeginBlockInstance`（`TransformDrawSink` 把自己的变换接上再转发），`DrawContext` 命中就把记下的结果经平移、旋转接上，不再逐个画块内子实体，也不再为它克隆块内容（`InsertEntity::EnsureResolved`）
- 键（`BlockTessKey`）：块定义；块参照的颜色、线型、线宽、图层（块内 ByBlock 与 0 层随它）；插入变换去掉旋转、平移后剩下的部分（缩放、镜像、错切——线型在变换之后按世界长度铺，见 `DrawContext::EmitStyled`，缩放不同的插入不能共用）；外层的顶层实体是否在锁定图层上（整块淡显，`BlockTessKey::faded`）。只缓存 XY 平面里的变换
- 记录：记录期间 `DrawContext` 的输出（`DrawContext::PutLine` 等）同时按世界坐标双精度记一份，画完按这次插入的旋转、平移换回去存下；接上时换到新位置再落成 float，与逐个细分只差双精度的舍入。区域覆盖记下出现时各流的位置，接上时在同一位置调用 `Wipe`，裁剪范围不变。CK-05 / CK-T 的截图与不缓存时逐字节相同；线顶点只差几个（线长正好是线型周期的整数倍时末尾那段零长度的虚线有没有，取决于舍入），另有 Z 的 ±0 之差
- 不记：按像素烘焙的（像素线宽、`MarkViewDependent`）、纹理文字、图像、绝对角度的线型符号、分段细分停在块中间的、一次超过 `DrawContext::kMaxBlockRecord` 个顶点的；只展开一次的块（如整张底图作为一个块插入）不记——第一次要判断时由 `Editor::CountBlockUses` 数一遍，数不到的块（天正对象、标注里引用的）第二次遇到才记；总量超过 `BlockTessCache::kMaxVertices` 后不再记新的
- 裁剪框（XCLIP，`ClipDrawSink`）里：`BeginBlockInstance` 经过时加上本层裁剪框（`BlockClipChain`）。条目记着块内输入（线端点、三角形顶点、文字锚点等）在块坐标里的范围，`ClipDrawSink::Classify` 判断它整个在框内、整个在框外还是跨边：
  - 整个在框内：原样接上，但要求块里没有区域覆盖、点、图像，也没问过裁剪框不转发的点样式、像素比例（`BlockTessEntry::clipSafe`；这些在框里画法不同）
  - 整个在框外：逐个画也什么都不剩，直接跳过（只推进分段序号与进度）
  - 跨边：照常逐个画
  - 框里也照常记录：裁剪框截断、丢掉了内容或吞掉了查询时经 `IDrawSink::NoteClipped` 告诉 `DrawContext`（带着它往下数是第几层），在这层框里开始的记录作废——存下的必须与不裁剪时相同。CK-T 的建筑底图整个带裁剪框，不在框里记录时画全反而慢 0.2 s
- 分段细分（`DrawContext::BeginChunk`）：块内子实体的层次不论分不分段都记着（`BeginBlockChild`），命中缓存时分段的序号跳过这个块的直接子实体个数、进度加上它的叶子子实体个数，停下的路径与接着画都不受影响（阵列里前一个单元接上缓存、后一个单元分段画，见 `tests/Core/BlockTessCacheTests.cpp`）
- 生命周期：`Editor` 只在成批细分时留着——分帧细分期间跨帧保留，否则 `Editor::UpdateSceneVertices` 结束就清空；图纸有任何改动或显示设置变了时先清空（块定义可能也改了）。视口合成（`Editor::ComposeModelIntoViewport`）不用：每个视口冻结的图层、像素比例不同。`Editor::SetBlockCacheEnabled` 供测试对比
- 锁定图层：顶层实体在锁定图层上时整个淡显（块参照即整块），块里锁定图层上的子实体也淡显——`DrawContext::BeginEntity` / `SetEntityContext` 记下是否淡显，输出的顶点不透明度乘 `DrawContext::kLockedFade`（0.5，同 AutoCAD 的 `LAYLOCKFADECTL` 默认 50：画到背景上前景、背景各半；场景流都开着混合，浅色背景的白↔黑互换照常）。锁定 / 解锁走 `ChangeLayerCommand`，`Scene::MarkDisplayDirty` 让逐实体缓存与块定义级缓存整体失效。拾取侧 `Picking::IsPickable` 排除锁定图层（点选、框选、悬停、工具点取都选不中，捕捉不受影响），显示版本变化时 `Picking::DropUnpickable` 把选择集里已锁定 / 关闭图层上的对象去掉
- 效果（2026-10-28 本机，`--bench` 与旧版交替各跑两次）：画全 CK-05 约 2.32 → 2.15 s、CK-T 约 3.71 → 3.26 s（-7%、-12%），打开后画全 CK-05 3.3 → 3.1 s、CK-T 4.6 → 4.2 s 左右；CK-T 4.7 万次块参照单元里 2.3 万次接上缓存或在框外跳过。收益不大：重复多的是便宜的线、圆弧、多段线（家具、线图案块），贵的填充（每个约 40 µs）、单行文字（约 34 µs）几乎都只出现一次

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
- 有了块定义级缓存之后，大图纸画全的大头：只出现一次的填充与单行文字（CK-T 里约 0.4 s + 0.3 s，单行文字每个约 34 µs，字体解析、排版每次都重做）、分帧细分每帧的上传与整图重画（约 30 帧 × 20 多 ms）；没命中缓存的块参照仍按插入深拷贝块内容（`InsertEntity::EnsureResolved`）
- 布局页的视口实体一个就合成整个模型空间（`Editor::ComposeModelIntoViewport`），不分段：切到布局页仍在一帧里细分完（打开图纸总是先显示模型页）
- 打开后第一帧的拾取空间索引（CK-05 约 0.2 s）、细分都在界面线程，单线程

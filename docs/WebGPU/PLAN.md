# WebGPU 渲染器原型

> 状态：**已放弃**（2026-10-26）。原型完成并验证后决定不保留，代码与 CMake 开关均已删除、没有进版本库，本文件只留作记录。结论见第 5 节：功能、后端选择、画面都可行，性能接近 D3D11；主要问题是内存（D3D12 后端启动就多约 500 MB 提交内存）。
> 下文的文件、开关、命令行参数都是原型里的，现在的代码里没有。
> 定位：验证"一套现代图形接口覆盖所有平台"的路线（借鉴 SolidCraft：wgpu，Windows 上 D3D12 → Vulkan → GL，没有显卡时用软件渲染）。代码只按标准的 `webgpu.h` 写，原型用 wgpu-native 的预编译包，以后可以换成 Dawn。

---

## 1. 范围

- 只做 Windows 桌面版，默认仍是 D3D11；`--gpu` 切换，WebGPU 创建失败自动退回 D3D11
- 实现完整的 `IRenderer`（缓存提交、字形实例化、缓存层、光栅图像、7 号色反转）与 MiniGUI 的 `IRenderBackend`
- 不做：Linux / macOS 宿主、网页版的 WebGPU、设备丢失后的恢复（只打印日志）

## 2. 编译与运行

```bash
cmake -S . -B out/webgpu -G Ninja -DCMAKE_BUILD_TYPE=Release -DMINICAD_WITH_WEBGPU=ON -DMINICAD_BUILD_TESTS=OFF -DUSE_WIN32=OFF
cmake --build out/webgpu --target MiniCADWin
```

- `MINICAD_WITH_WEBGPU`（默认关）：配置时下载 wgpu-native v29.0.1.1 的 Windows x64 预编译包并校验 SHA256，只放在构建目录里（`out/` 不进版本库），编译后把 `wgpu_native.dll`（9.1 MB）复制到 exe 旁
- 只链接 DLL 的导入库：包里的静态库依赖动态 C 运行库（/MD），与本程序的 /MT 混用会冲突
- 运行：`MiniCADWin.exe --gpu <名称>`，或环境变量 `MINICAD_GPU`。名称：`d3d11`（默认）、`webgpu`（自动选后端）、`dx12`、`vulkan`、`gl`、`cpu`（软件适配器）
- 关于对话框的图形名称显示实际用的后端，例如 "WebGPU D3D12"

## 3. 代码结构

| 文件 | 内容 |
|---|---|
| `apps/Win32/src/Render/WebGPU/WebGPUDevice.*` | 实例、适配器、设备、队列、窗口表面；选后端；等待 GPU。**wgpu-native 的专有接口（`wgpu.h`）只在 `WebGPUDevice.cpp` 里用**，换成 Dawn 时只改这一个文件 |
| `apps/Win32/src/Render/WebGPU/WebGPURenderTarget.*` | 视口 / 缓存层的离屏目标（RGBA8 + Depth32Float）；`WebGPURenderTarget::ReadPixels` 读回像素（自测、`MINICAD_BENCH_SHOT` 截图） |
| `apps/Win32/src/Render/WebGPU/WebGPURenderer.*` | CAD 视口渲染器，着色器为 WGSL |
| `apps/Win32/src/Render/WebGPU/WebGPUBackend.*` | MiniGUI 界面后端 |
| `apps/Win32/src/Host/Win32Window.*` | `Win32Window::InitWebGPU`；界面通道、呈现、等待 GPU 按图形接口分派 |

除了 `WebGPUDevice` 里创建表面（HWND），这几个类与平台无关，以后 Linux / macOS 宿主可以共用。

### 后端选择（`WebGPUDevice::CreateAdapter`）

- 默认依次试 D3D12、Vulkan、OpenGL，**每次只启用一个后端**创建实例，取第一个有硬件适配器的（独立显卡优先）——同时启用 Vulkan 会加载机器上所有的 Vulkan 驱动，SolidCraft 遇到过 Intel 驱动在初始化时崩溃
- 都没有硬件适配器、或指定了 `cpu` 时用 D3D12 的软件适配器（WARP）
- 设备按适配器的上限申请（几百万顶点的图纸要很大的缓冲）

### 与 D3D11 实现不同的地方

- WebGPU 的缓冲写入（`wgpuQueueWriteBuffer`）在提交命令前统一生效，同一帧里不能反复改写同一块缓冲：每次提交的矩阵占帧内环形缓冲的一段（256 字节对齐，按动态偏移绑定），不缓存的顶点写进帧内流缓冲的不同区段；`EndFrame` 提交后复位。帧内换下来的缓冲、绑定组等提交之后再释放
- 缓存层复制不能在渲染通道里做：`WebGPURenderer::DrawLayer` 结束当前通道，`CopyTextureToTexture` 后以"保留内容"重新开始
- 渲染目标总带深度缓冲，"不测试深度"的管线也要声明深度格式（比较总是通过、不写入）
- 管线按 程序 × 图元 × 深度模式 × 混合 用到时创建（最多 72 条）
- 点图元：WebGPU 的 point-list 本来就是 1 像素，与 D3D11 的 POINTLIST 一致
- 界面后端没有"当前渲染目标"：宿主开始窗口的渲染通道后用 `MiniGUI::WebGPUBackend::BeginFrame` 交给后端

## 4. 验证结果

环境：Windows 11，NVIDIA GeForce GTX 1060 6GB，Release 版，同一个 exe 用 `--gpu` 切换。

### 4.1 后端与自测

| 后端 | 启动 | `--selftest` |
|---|---|---|
| D3D11（现有） | ✓ | 0 项失败 |
| WebGPU D3D12 | ✓ | 0 项失败 |
| WebGPU Vulkan | ✓ | 0 项失败 |
| WebGPU OpenGL | ✓ | 0 项失败 |
| WebGPU WARP（`cpu`） | ✓ | —（能画，小图纸每帧约 120 ms，只适合兜底） |

全程没有 WebGPU 校验错误。

### 4.2 画面（`MINICAD_BENCH_SHOT` 离屏截图逐像素比较，1280×766）

- 模型页：与 D3D11 只差 2～11 个像素，可以认为一致
- 布局页：差约 1.6%，但 **D3D11 自己运行两次也差 1.6%**——与渲染器无关。根因后来查明：stb_truetype 处理宋体空格（单点轮廓）时越界读内存，空格画成位置随机的长斜线（已修，见 `3rd/stb/README.md`）。复测（1920×1149）：D3D12 与 D3D11 的模型页、布局页、OLE 对象截图逐像素相同，大图纸模型页只差 6 个像素

### 4.3 性能（`--bench tests/Data/总平面图1008.dwg`：线顶点 40.8 万、填充顶点 107 万）

| | D3D11 | WebGPU D3D12 |
|---|---|---|
| 首帧（视口） | 852 ms（838） | 866 ms（853） |
| 滚轮 48 帧 | 平均 16.5 ms | 平均 16.4 ms |
| 平移全图：视口 / 呈现 | 4.1 / 0.1 ms | 4.9 / 0.7 ms |
| 平移放大 6 格：视口 / 呈现 | 2.5 / 0.1 ms | 3.1 / 0.7 ms |
| 布局页首帧 | 623 ms | 652 ms |

帧率都被垂直同步钉在约 16 ms。自测里 2 万条线逐阶段等 GPU 的测量：视口 0.51 → 1.44 ms，界面 0.59 → 0.89 ms，呈现 0.07 → 0.94 ms。WebGPU 每帧多约 1 ms 的固定开销，大图纸上视口慢约 20%，用户感觉不到。

### 4.4 内存（私有字节）

| | D3D11 | D3D12 | Vulkan | OpenGL |
|---|---|---|---|---|
| 启动（空文档） | 105 MB | 613 MB | 387 MB | 196 MB |
| 小图纸跑完 | 138 MB | 628 MB | 408 MB | 244 MB |
| `总平面图1008.dwg` 跑完 | 827 MB | 1559 MB | | |
| `0525最终改总平面图.dwg` 跑完 | 1060 MB | 1955 MB | | |

- D3D12 后端启动就多约 500 MB（工作集只多约 125 MB：大部分是提交了还没用到的内存），大图纸上多 70%～90%
- 与按适配器上限申请设备无关（改用默认上限后不变）；wgpu 有控制分配策略的 `MemoryHints`，但 wgpu-native 的 C 接口没有暴露

## 5. 结论与去留

- **可行**：一套 `webgpu.h` 代码在 D3D12 / Vulkan / OpenGL / WARP 上都能跑通全部自测，画面与 D3D11 一致，性能接近；按 SolidCraft 的顺序选后端可以照搬
- **问题**：内存开销明显偏大，D3D12 最严重。要继续这条路线，先要把内存降下来：用 Dawn 编一份对比（Dawn 的分配策略不同，也能静态链接、单步调试），或者给 wgpu-native 打补丁暴露 `MemoryHints`
- 当时列的三种去向：放弃；继续、正式版改用 Dawn（只重写设备层，渲染器与界面后端保留）；继续用 wgpu-native、先解决内存
- **决定：放弃**（2026-10-26）。原型代码、`MINICAD_WITH_WEBGPU` 开关、`--gpu` / `MINICAD_GPU` 与宿主里的接入全部删除，构建目录里的 wgpu-native 一并删掉。原型期间顺带的一处改动保留：`apps/Win32/src/Host/Win32Window.cpp` 文件对话框过滤串里的原始 NUL 字节改成 `\0` 转义（文件不再被当成二进制）
- 以后重新考虑这条路线时，先解决内存（用 Dawn 对比，或给 wgpu-native 暴露 `MemoryHints`），再按第 3 节的结构重写

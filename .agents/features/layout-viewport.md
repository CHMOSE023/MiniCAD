---
covers: src/Core/Entity/ViewportEntity.hpp, tests/Core/LayoutTests.cpp, tests/Core/OleFrameTests.cpp
---

# 布局页与图纸空间视口

## 概述

布局页显示图纸空间：纸张、标题栏等图纸空间实体，加上若干视口（`ViewportEntity`），每个视口按自己的视图把模型空间内容合成进来。进布局页时按图纸里保存的图纸空间视图（布局主视口的中心与高度）显示。

## 视口的视图参数

- 视图中心（组码 12/22）是以视图目标点（17/27/37）为原点、转过 Twist 的 DCS 坐标。导入时换算成 WCS（`SetViewCenterDcs`）；导出时目标点写 (0,0,0)、12/22 写完整位置。经 DVIEW / 动态观察过的视口目标点落在模型上，漏加目标点会对到世界原点附近
- 视口内冻结的图层（VPLAYER，组码 331）存在 `GetFrozenLayers`，合成时跳过（含块里的子实体），随 `.mcad` / JSON 保存，导出时写回

## 合成（`Editor::ComposeModelIntoViewport`）

- 视口把模型空间内容合成进自己的显示数据，合成结果**不随缩放重建**：长虚线照常延迟铺设、合成后按视口矩形裁剪（裁掉前半段时相位接着原线算），相对尺寸的点按视口的纸面高度换算。改合成逻辑时不要让整个视口变成"随视图变化"，否则每格滚轮都重新合成
- `$PSLTSCALE`（`Scene::GetPaperSpaceLineTypeScaling`）：为 0 时视口内的线型比例乘视口比例（1:500 的视口里虚线细到接近实线），为 1（默认）时按图纸单位铺

## 测试与排查

- `MiniCADTests`：`LayoutTests.cpp`、`OleFrameTests.cpp`（视口目标点换算、冻结图层往返；本机有 `tests/Data/0525最终改总平面图.dwg` 时再核对真实图纸）
- `MiniCADWin.exe --bench 图纸`：有布局时切到第一张布局测滚轮与平移；`MINICAD_BENCH_SHOT=目录` 存模型页、布局页的截图，`MINICAD_BENCH_PAUSE=秒` 在布局页停留
- `MiniCADDwgInspect 图纸` 加 `MINICAD_VIEWPORTS=1` 打印全部视口的目标点、方向、视图中心、冻结图层

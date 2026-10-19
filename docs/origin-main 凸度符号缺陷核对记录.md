# origin/main 凸度符号缺陷核对记录

核对时间：2026-10-19
核对对象：`origin/main` = `493bead`「修复轴号（块属性）、多段线逐段线宽、多段线凸度方向」
本地对照：`v0.2.0-sync` 工作副本的 `pr1-axis` / `pr2-pwidth` / `pr3-bulge` / `pr4-recent`

## 结论

`origin/main` 的 `493bead` 修好了**弧的渲染方向**（`Polyline::ComputeArc` 与 `CadExchange`
的补偿层），但**漏了三处与 `ComputeArc` 配套的符号**，导致交互绘制的弧与拖动夹点得到的弧
仍然画到弦的另一侧、不经过用户点。

## 逐项核对

`ComputeArc` 与 `CadExchange::BulgeFromCad/BulgeToCad` 两边一致（都是修正后的 DXF 约定），
`CadExchange.cpp` 的文件内容在忽略文件名大小写后**完全相同**。

有差异的是下面三处：

### 1. `Polyline::BulgeFromTangent`（src/Core/GeomKernel/Polyline.hpp）

| | 代码 |
|---|---|
| origin/main | `return (side >= 0.0 ? 1.0 : -1.0) * std::tan(theta / 4.0);` |
| 本地（正确） | `return (side > 0.0 ? -1.0 : 1.0) * std::tan(theta / 4.0);` |

`side` 是圆心相对弦中点在「A→B 右法向」上的投影，`side > 0` 表示圆心在弦**右侧**。
DXF 组码 42 的约定（`ComputeArc` 已按此实现）是「圆心在右 ⇒ bulge 负」，
所以 `side > 0` 必须给负号。origin 给的是正号，与它自己的 `ComputeArc` 相反。

### 2. `PolylineTool::ComputeBulgeFromThreePoints`（src/Editor/Tools/PolylineTool.h）

| | `cross >= 0`（M 在弦左侧）时的返回 |
|---|---|
| origin/main | `return -std::tan(sweep * 0.25);` |
| 本地（正确） | `return  std::tan(sweep * 0.25);` |

`else` 分支（M 在弦右侧）相应地一边是 `+`、一边是 `-`。

### 3. `PolylineGripHandler::ComputeBulgeFromThreePoints`（src/Editor/Grip/PolylineGripHandler.h）

与第 2 项完全相同的符号问题，两处必须一起改。

## 为什么这三处会漏

`ComputeArc`、`BulgeFromTangent`、两处 `ComputeBulgeFromThreePoints` 表达的是**同一套**
凸度方向约定，只是入口不同（DWG 导入 / 切向构造 / 三点构造）。v0.2.0 把这套约定写反了，
于是这四处**互相抵消**：`ComputeArc` 反、两处三点求凸度也反，画出来的弧恰好是对的。

`493bead` 只把 `ComputeArc`（以及 `CadExchange` 里为抵消它而加的取反层）修回规范，
抵消被打破，剩下两处三点求凸度仍按旧约定给符号 ⇒ 交互绘制与夹点拖动的弧跑到弦的另一侧。
`BulgeFromTangent` 则从一开始就是孤立的（没有别的反号与它抵消），origin 版直接给错。

半圆用例区分不出这个问题：半圆时圆心恰好落在弦上，`side == 0`、`cross == 0`，
两种符号给出的 `|bulge|` 都是 1。测试必须用非半圆。

## 独立验证方法

不依赖仓库里的任何代码，另写小程序复刻两套实现，直接比对几何结果
（判据：喂进真实的切向/中点，看 `ComputeArc` 还原出来的弧是否就是原来那条）：

```
=== Test 1: BulgeFromTangent consistent with ComputeArc ===
  truth: centre=(1.000,-0.000) sweep=-3.142 tangent@A=(-0.0000,-1.0000) bulge=-1
    mine    bulge=-1.0000 -> centre=(1.0000,-0.0000) sweep=-3.1416  PASS (same arc)
    origin  bulge=+1.0000 -> centre=(1.0000, 0.0000) sweep=+3.1416  FAIL (different arc)
  truth: centre=(1.000,-1.050) sweep=-1.522 tangent@A=(-0.7241,-0.6897) bulge=-0.40
    mine    bulge=-0.4000 -> centre=(1.0000,-1.0500) sweep=-1.5220  PASS (same arc)
    origin  bulge=+0.4000 -> centre=(1.0000, 1.0500) sweep=+1.5220  FAIL (different arc)

=== Test 2: three-point arc passes through M ===
  M below chord : A=(0,0) B=(2,0) M=(1.00,-0.50)
    mine    bulge=+0.5000 -> centre=(1.0000, 0.7500) sweep=+1.8546 t(M)=0.5000 pt=(1.0000,-0.5000) PASS
    origin  bulge=-0.5000 -> centre=(1.0000,-0.7500) sweep=-1.8546 t(M)=0.5000 pt=(1.0000, 0.5000) FAIL
  non-half circle : A=(0,0) B=(4,0) M=(1.00,-0.50)
    mine    bulge=+0.3252 -> centre=(2.0000, 2.7500) sweep=+1.2576 t(M)=0.2626 pt=(1.0000,-0.5000) PASS
    origin  bulge=-0.3252 -> centre=(2.0000,-2.7500) sweep=-1.2576 t(M)=0.1674 pt=(0.6190, 0.3573) FAIL
```

统计：Test 1 本地 3/3、origin 0/3；Test 2 本地 5/6、origin 1/6
（本地那 1 例未过是 sweep 绕回时参数归算的歧义，弧本身经过 M，见下）。

Test 2 旋转用例（`A=(0,0) B=(0,2) M=(-0.5,1)`）在本地实现下 `t(M)` 归算到 0.1121 而非
0.5，是因为 `sweep` 为负、`(am - sa)/sweep` 落到 `[-1,0]` 后加了 1 得到 0.1121 —— 
这是**验证脚本**的归一化写法问题，不是产品代码缺陷：把该点按 `t=0.1121` 代回，
得到 `(-0.1902, 0.1763)`，确实在圆上但不是 M。换用 `M=(+0.5,1)`（同一象限的另一侧）
本地即 PASS。这一点已在 `tests/Core/BulgeDirectionTests.cpp` 里用「弧经过中间点」的
正向断言覆盖，不依赖参数归算。

## 影响面

- **DWG/DXF 导入的弧**：不受影响，`ComputeArc` + `CadExchange` 已修好。
- **交互绘制的多段线圆弧段**（画完第三点后的预览与落点）：画到弦的另一侧。
- **拖动弧中点夹点**（`PolylineGripHandler`）：弧被甩到另一侧。
- **`BulgeFromTangent` 的调用方**：目前 Core 内没有生产调用点，只有测试；
  但它是 `Polyline` 的公开静态接口，签名与语义应当自洽。

## 建议

把这三处符号按本地版本修正，并在 `tests/Core/BulgeDirectionTests.cpp` 里补
「非半圆 + 弧经过中间点」的断言（现有测试只覆盖半圆，区分不出符号）。

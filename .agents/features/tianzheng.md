---
covers: src/Import/Tch*, tests/Core/Tch*, tests/Core/TianzhengWallTests.cpp
---

# 天正对象（`TCH_*`）

## 概述

天正建筑 / 给排水 / 电气 / 暖通的自定义对象（墙、门窗、柱、楼梯、尺寸标注、符号、文字、管线、桥架、风管等）在 DWG 里是 MiniDWG 读成的未知对象。`src/Import/Tch*.h` 按各类的私有数据布局解析出参数，组装成普通几何，在 MiniCAD 里显示为匿名块 `*U_TCH_<十进制句柄>` 的块参照。

## 入口与数据流

- `CadExchange.cpp` 的 `IsTch` 判断天正对象；各类经 `Tch::Build*`（如 `BuildDimension`、`BuildCoord`、`BuildRectStair`）得到几何，再由 `MakeTchGraphics` / `MakeTchSymbol` 组装成块
- 另存为同格式同版本时天正对象原样写回（以原图为底另存，见 [cad-exchange](cad-exchange.md)）；格式或版本不同时只剩组装出的普通几何

| 文件 | 对象 |
|---|---|
| `TchOpening.h` | 天正基类（`TCH_Base`）的私有布局（R2007 起字符串在文字流，R2004 及更早嵌在主数据流）、门窗 |
| `TchWall.h` | 墙、幕墙（只读到受保护几何，取门窗的墙厚） |
| `TchColumn.h` | 柱 |
| `TchStair.h`、`TchMultiStair.h` | 双跑楼梯；双分、交叉、剪刀、三角、矩形转角等楼梯 |
| `TchDimension.h` | 尺寸标注（逐点、角度 / 弧长）、轴号 |
| `TchSymbol.h`、`TchAnnotation.h` | 符号：坐标、箭头、剖切、索引、图名、引出标注、标高、指北针等 |
| `TchText.h` | 天正文字的样式规则（文字样式上 `_TCH` 扩展数据给出西文字高与宽度系数） |
| `TchShape.h`、`TchPathArray.h` | 天正矩形；线图案（沿路径重复排列图案块） |
| `TchPipe.h`、`TchElec.h`、`TchHvac.h` | 给排水管线；电气桥架与配件、折断线；暖通风管与管件 |
| `TchBlockInsert.h`、`TchProxyGraphics.h` | 天正块参照的标准几何前缀；代理图形记录 |

## 约束

- 布局按版本分支解析，必须**恰好读完主数据流**；遇到未核对的版本按不支持处理（跳过并 `LOG_WARN`），不要猜字段
- 符号类（坐标、箭头、剖切、引出标注、标高等）的线按实线组装，不用对象自身的线型（原图对象线型可能是 `__DASH`，天正显示仍是实线）
- 天正文字的显示宽度因子写在多行文字实体上（`MTextEntity::SetWidthFactor`），不派生文字样式
- 几何以天正（AutoCAD 2014 + 对应天正版本）分解出的结果为准核对

## 测试与排查

- 按对象分的测试程序：`MiniCADTianzhengTests`（墙）、`MiniCADTchImportTests`、`MiniCADTchOpeningTests`、`MiniCADTchDimensionTests`、`MiniCADTchSymbolTests`、`MiniCADTchStairTests`、`MiniCADTchPathArrayTests`、`MiniCADTchGeometryTests`；样例图纸多在本机 `tests/Data`，不存在时跳过
- `MiniCADDwgInspect` 加 `MINICAD_GEOM=1` 输出天正对象生成的几何（与天正分解结果比对），`MINICAD_RAWOBJ=<DXF 名>` 导出某类对象的原始数据流，`MINICAD_H=<句柄>` 查看指定对象；详见 skill `drawing-debug`

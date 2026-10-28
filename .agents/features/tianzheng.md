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
- 天正的中西文混排文字拆成西文、汉字段（`Tch::Detail::AddRuns`）：各段起点按天正步进排，**画出的宽度也取天正步进**——按 MiniCAD 实际显示的字体（西文 simplex、汉字 TSSDCHN，`Detail::DisplayExtent`）反算每段的宽度因子。原图常用的大字体 GBCBIG 程序不带，替代的 TSSDCHN 汉字宽约 37%，按字体自身宽度画会压到下一段、长句冲出表格（8#楼平面的平面小说明）。天正单行文字（含纯西文 / 纯汉字）、天正多行文字的符号字体分段、引出标注都按这条规则；引出标注的注释符号字宽乘样式宽度因子（`_TCH_DIM` 宽 0.7 时以前中间空出一截）
- 天正上标 `^U…^U`（`m^U2^U` 为 m²）：字高为本段的一半、抬高 0.5 × 名义字高，每字步进 0.35 × 西文步进（8#楼平面房间面积的分解结果核对）；TrueType 样式的上标未核对，原样显示
- 几何以天正（AutoCAD 2014 + 对应天正版本）分解出的结果为准核对；显示宽度、遮挡等分解结果看不出的以天正的显示截图为准。脚本里核对时先用天正样板启动（`acad.exe /nologo /p TArch9 /t <TArch2014\sys19x64\ACAD.DWT> /b 脚本`）、在脚本里 `_.OPEN` 图纸：把图纸作为启动参数时图纸先于插件读入，天正对象成了代理，分解不出东西

## 测试与排查

- 按对象分的测试程序：`MiniCADTianzhengTests`（墙）、`MiniCADTchImportTests`、`MiniCADTchOpeningTests`、`MiniCADTchDimensionTests`、`MiniCADTchSymbolTests`、`MiniCADTchStairTests`、`MiniCADTchPathArrayTests`、`MiniCADTchGeometryTests`；样例图纸多在本机 `tests/Data`，不存在时跳过
- `MiniCADDwgInspect` 加 `MINICAD_GEOM=1` 输出天正对象生成的几何（与天正分解结果比对），`MINICAD_RAWOBJ=<DXF 名>` 导出某类对象的原始数据流，`MINICAD_H=<句柄>` 查看指定对象；详见 skill `drawing-debug`

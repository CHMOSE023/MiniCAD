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
| `TchCipher.h` | `Tch::Detail::CipherReader`：新版对象的读取；各类的头文件有 `Decode*Cipher` 分支。也能读旧版明文对象 |
| `TchSheet.h` | 天正表格（`TCH_SHEET`）：`Tch::DecodeSheet` 读出行、列、合并区、单元格与标题，`Tch::BuildSheet` 画格线与文字 |
| `TchWall.h` | 墙、幕墙（`Tch::DecodeCurtainWall` 读出与墙相同的字段；读不出时 `Tch::DecodeWallPrefix` 只取轴线与墙厚） |
| `TchColumn.h` | 柱 |
| `TchStair.h`、`TchMultiStair.h` | 双跑楼梯；双分、交叉、剪刀、三角、矩形转角等楼梯 |
| `TchDimension.h` | 尺寸标注（逐点、角度 / 弧长）、轴号 |
| `TchSymbol.h`、`TchAnnotation.h` | 符号：坐标、箭头、剖切、索引、图名、内视符号、引出标注、标高、指北针等 |
| `TchText.h` | 天正文字的样式规则（文字样式上 `_TCH` 扩展数据给出西文字高与宽度系数） |
| `TchShape.h`、`TchPathArray.h` | 天正矩形；线图案（沿路径重复排列图案块） |
| `TchPipe.h`、`TchElec.h`、`TchHvac.h` | 给排水管线；电气桥架与配件、折断线；暖通风管与管件 |
| `TchBlockInsert.h`、`TchProxyGraphics.h` | 天正块参照的标准几何前缀；代理图形记录 |

## 约束

- **解析**：按版本分支，必须恰好读完主数据流；未核对的版本按不支持处理（跳过并 `LOG_WARN`），不要猜字段。新版对象经 `Tch::Detail::CipherReader` 读，旧版明文对象也走它
- **天正表格**：`Tch::DecodeSheet` / `Tch::BuildSheet`；读完子对象要恢复外层状态（`Tch::Detail::SheetLayer`）；文字放不放得下按天正步进估宽（`Tch::Detail::SheetTextWidth`）。未处理：格线的颜色与线型、单元格里的图块、主样式与标题样式以外的单元格文字样式、多行单元格里的上标
- **内视符号**：`Tch::DecodeInSight` / `Tch::BuildInSight`；编号大小取对象的比例，为 0 时取 `TchConfig::IndexTextRatio` / `TchConfig::IndexTextMode`；实心区域经 `TchGraphics::Fills` 画成 SOLID 填充（`Importer::MakeTchGraphics`）。未做：遮罩、视图翻转时上下文字对调
- **剖切符号**：旧样式 `Tch::BuildSection`，新国标样式 `Tch::BuildSectionNewStyle`；样式开关在 `Tch::Detail::CipherReader::XData` 里，旧版明文对象一律旧样式
- **对称轴**：`Tch::BuildSymmetry`，两端各两条横线，距端点 Gap、Gap + Spacing（乘比例）
- **符号类**（坐标、箭头、剖切、引出标注、标高等）的线一律按实线组装，不用对象自身的线型
- **天正文字**：显示宽度因子写在多行文字上（`MTextEntity::SetWidthFactor`），不派生文字样式；不限宽的居中 / 右对齐按实际排版宽度（`IDrawSink::MeasureMTextWidth`）定起点。中西文混排拆段（`Tch::Detail::AddRuns`），各段起点与画出的宽度都取天正步进，按实际显示的字体（`Detail::DisplayExtent`）反算宽度因子
- **上标** `^U…^U`：字高为本段一半、抬高 0.5 × 名义字高、每字步进 0.35 × 西文步进；TrueType 样式未核对
- **墙**（`TchWall::Connections`）：门窗断口两侧封口；同材料相接处不封口，材料不同的一端画分界线；断口与墙线端点比较用 1e-6 容差
- **幕墙**：非玻璃幕墙画法同墙；玻璃幕墙沿墙厚三等分画 4 条线，两端按连接量缩进或伸出，方头一端封口、与相邻幕墙斜接的一端不封口
- **墙柱填充**（`Importer::MakeTchHatch`）：图纸设置（`Tch::DecodeConfig`）的填充开关打开时才填；按材料码（`TchWall::Material`、`TchColumn::Material`）取图案，出图比例小于详图比例时用详图图案；图案经 `HatchPatternLibrary::FindTianzheng` 取，找不到就不画；填充放 `PUB_HATCH` 图层、随层
- **尺寸标注**：有拖动过的文字位置（`TchDimension::TextPositions`，文字中心）就用；竖直方向带误差时按自下而上读（`Detail::Upright`）
- **文字颜色**：尺寸数字取标注样式的 DIMCLRT（`TchDimStyle::TextColor`）；轴号放在对象引用的文字图层上、随层（`TchAxisLabel::TextLayer`）；索引、引出标注、箭头引注、剖切、指北针、楼梯的文字固定 7 号色，图名按对象存的颜色；天正单行 / 多行文字随块；角度标注按 DIMAZIN 去零（`TchDimStyle::AngularZeros`）
- **天正矩形**（`TchRect::Flag`）：1、2、3 分别加一条或两条对角线
- **门槛线**（`TchOpening::Flags`）：0x200 画在门扇一侧、0x400 画在对面，都没有时不画
- **核对基准**：几何以天正分解结果为准，显示宽度、遮挡等以天正的显示截图为准。脚本核对时先用天正的样板启动，再在脚本里 `_.OPEN` 图纸：把图纸作为启动参数时图纸先于天正插件读入，天正对象成了代理，分解不出东西

## 测试与排查

- 按对象分的测试程序：`MiniCADTianzhengTests`（墙）、`MiniCADTchImportTests`、`MiniCADTchOpeningTests`、`MiniCADTchDimensionTests`、`MiniCADTchSymbolTests`、`MiniCADTchCipherTests`（新版对象：合成对象往返、内视符号与剖切符号新国标样式、图纸设置）、`MiniCADTchSheetTests`（天正表格：合成的新旧版表格、格线与文字位置）、`MiniCADTchStairTests`、`MiniCADTchPathArrayTests`、`MiniCADTchGeometryTests`；样例图纸多在本机 `tests/Data`，不存在时跳过
- `MiniCADDwgInspect` 加 `MINICAD_GEOM=1` 输出天正对象生成的几何（与天正分解结果比对），`MINICAD_RAWOBJ=<DXF 名>` 导出某类对象的原始数据流，`MINICAD_H=<句柄>` 查看指定对象，`DIMSTYLE` 行打印 `azin`、`clrt` / `clrd` / `clre`；详见 skill `drawing-debug`

---
covers: src/Import/Tch*, tests/Core/Tch*, tests/Core/TianzhengWallTests.cpp
---

# `TCH_*`

## 概述

`TCH_*` 在 DWG 里是 MiniDWG 读成的未知对象。`src/Import/Tch*.h` 按各类的私有数据布局解析出参数，组装成普通几何，在 MiniCAD 里显示为匿名块 `*U_TCH_<十进制句柄>` 的块参照。

## 入口与数据流

- `CadExchange.cpp` 的 `IsTch` 判断TCH对象；各类经 `Tch::Build*`（如 `BuildDimension`、`BuildCoord`、`BuildRectStair`）得到几何，再由 `MakeTchGraphics` / `MakeTchSymbol` 组装成块
- 另存为同格式同版本时TCH对象原样写回（以原图为底另存，见 [cad-exchange](cad-exchange.md)）；格式或版本不同时只剩组装出的普通几何

| 文件 | 对象 |
|---|---|
| `TchOpening.h` | TCH基类（`TCH_Base`）的私有布局（R2007 起字符串在文字流，R2004 及更早嵌在主数据流）、门窗 |
| `TchCipher.h` | `Tch::Detail::CipherReader`：新版对象的读取；各类的头文件有 `Decode*Cipher` 分支或 `Read*Fields` 模板（`Tch::Detail::PlainFields` / `Tch::Detail::CipherFields` 两种读法共用一份字段顺序）。也能读旧版明文对象；属性表 `TchXDataMap` 也在这里 |
| `TchSheet.h` | TCH表格（`TCH_SHEET`）：`Tch::DecodeSheet` 读出行、列、合并区、单元格与标题，`Tch::BuildSheet` 画格线与文字 |
| `TchWall.h` | 墙、幕墙（`Tch::DecodeCurtainWall` 读出与墙相同的字段；读不出时 `Tch::DecodeWallPrefix` 只取轴线与墙厚） |
| `TchColumn.h` | 柱 |
| `TchStair.h`、`TchMultiStair.h` | 双跑楼梯；双分、交叉、剪刀、三角、矩形转角等楼梯 |
| `TchDimension.h` | 尺寸标注（逐点、角度 / 弧长）、轴号 |
| `TchSymbol.h`、`TchAnnotation.h` | 符号：坐标、箭头、剖切、索引、图名、内视符号、引出标注、标高、指北针等 |
| `TchText.h` | TCH文字的样式规则（文字样式上 `_TCH` 扩展数据给出西文字高与宽度系数） |
| `TchShape.h`、`TchPathArray.h` | TCH矩形；线图案（沿路径重复排列图案块） |
| `TchPipe.h`、`TchElec.h`、`TchHvac.h` | 给排水管线；电气桥架与配件、折断线；暖通风管、管件（法兰、变径、弯头、三通、四通、乙字弯）与风管标注 |
| `TchBlockInsert.h`、`TchProxyGraphics.h` | TCH块参照的标准几何前缀（`SWR_BLOCK_INSERT` 布局相同）；代理图形记录（也用于其他带代理图形的自定义实体，见 `docs/evidence/Tch-proxy-graphics.md`；TCH另存为代理实体的图纸里图层、线型序号多为 -1，按对象自身的处理；不支持或数据不合理的记录只跳过那一条，裁剪 / 变换矩阵记录整个放弃） |

## 约束

- **解析**：按版本分支，必须恰好读完主数据流；未核对的版本按不支持处理（跳过并 `LOG_WARN`），不要猜字段。新版对象经 `Tch::Detail::CipherReader` 读，旧版明文对象也走它
- **TCH表格**：`Tch::DecodeSheet` / `Tch::BuildSheet`；读完子对象要恢复外层状态（`Tch::Detail::CipherLayer`）；文字放不放得下按TCH步进估宽（`Tch::Detail::SheetTextWidth`）。未处理：格线的颜色与线型、单元格里的图块、主样式与标题样式以外的单元格文字样式、多行单元格里的上标
- **内视符号**：`Tch::DecodeInSight` / `Tch::BuildInSight`；编号大小取对象的比例，为 0 时取 `TchConfig::IndexTextRatio` / `TchConfig::IndexTextMode`；实心区域经 `TchGraphics::Fills` 画成 SOLID 填充（`Importer::MakeTchGraphics`）。未做：遮罩、视图翻转时上下文字对调
- **新版（T30，TCH基类版本 9～14）**：各类的版本号与明文版本的对应写在 `Read*Fields` / `Decode*Cipher` 上方的注释里。自带 RC 版本与密钥、第二个密钥为 0 的子层用 `Tch::Detail::CipherLayer`（表格子对象、屋顶基类、管线接口、标注样式、管件基类的列表）。TCH基类里的属性包值与文字方向记在 `Tch::Detail::CipherReader::FieldStrings` / `Tch::Detail::CipherReader::FieldNumbers` / `Tch::Detail::CipherReader::Angle`（立管、多管标注要用）。机电：管线节点基类没有加密层（`Tch::Detail::ReadCipherPipeNode`）；属性表移进了TCH基类（`Tch::Detail::CipherReader::XData`），管线基类版本 4 在末尾另存 Start、End（`Tch::Detail::ReadCipherPipeBase`）
- **DWG2004**：没有单独的文字流，GUID、属性名与密文都是主数据流里的 GBK 文字；密文字符在 GBK 里有的存为双字节，没有的写成 `\U+XXXX`，`Tch::Detail::CipherReader` 取各字符的低字节解密，与 2007 起 UTF-16 的做法相同。实物图纸（中望看图另存）里坐标标注读出的格式 48、方向 3 超出已核对的范围，仍跳过。明文机电对象里只有管线、管件、管线文字（`Tch::DecodePipe`、`Tch::DecodePipeFitting`、`Tch::DecodePipeText`，`Tch::Detail::CheckPayload` 的 `inlineText`）按内嵌字符串读 2004 格式，其余给排水类仍要求 2007 起
- **暖通**：风管与管件的几何都在属性表里（`Tch::ReadCableTrayMap` / `Tch::ReadCableFittingMap`，明文管件的属性表后是边界列表（每条带若干段）与连接数，与风管相同）。管件按属性表的键分类型（`Tch::DecodeHvacFitting`）：三通 I4 为支管样式（1 直、3 弧形、5 Y 形、7 Y 形而对侧为直边），四通有 Pt4 / Pt5，乙字弯有 R28、R34 而无 Vt6（R2 为偏移，竖向偏移画直管加 Z 形符号），分节弯头的节数 I5 ≥ 2 时取 I5、否则 I4 − 1。风管标注 `TCH_HVACDIMENSION` 是TCH基类 + BS 版本 1 + 属性表（`Tch::DecodeHvacDimension`）：S1 为“|”分隔的显示文字，I4 = 0 时文字居中在 Pt6，I4 = 4 时引线到 Pt7 再画两行两列的表格；天正按字形实际宽度定列宽，这里是估算（误差约 1 个字宽）。以上对照 2004 格式图纸的天正分解核对；分解结果会被重叠对象裁掉一部分，比对时只看“参照有而 MiniCAD 没有”的部分
- **TCH基类版本7**（ZW看图另存为2018格式）：`ReadTchBase` 在版本 5 的字段后读属性表（键在文字流、值在主数据流，`Tch::Detail::SkipXDataMap`），两个句柄个数分别多存了 5、3；之后接旧版明文的各类字段（柱 6、门窗 5 等）。属性表要从头读文字流，所以 `ReadTchBase` 要传 `data`，只适用于对象自身的基类（组合门窗的子门窗不传，版本 7 按不支持）。墙版本 10（`Tch::Detail::DecodeWallFields`）：受保护几何同标记 7 先互换半字节再异或密钥，墙高在左右宽之前，结尾标记后多 13 个 0 位（含义未核对，只接受不超过 16 位且全为 0）。坐标标注版本 6（格式 48、方向 3）仍跳过
- **新版未核对**（只有合成样例，没有实物图纸对照）：屋顶、曲线楼梯、剖切线、双跑楼梯、阳台；自动扶梯（第三方查看器画两道、MiniCAD 一道）；设备 / 井 / 水泵的图块、多管标注、版本 4 的桥架与风管、桥架配件与风管管件（井标注能显示井的标高，说明井按新版布局读对了）。未支持：带形窗（与转角窗同为 `TCH_CORNER_WINDOW`）；修订标记另存的多段线不画
- **剖切符号**：旧样式 `Tch::BuildSection`，新国标样式 `Tch::BuildSectionNewStyle`；样式开关在 `Tch::Detail::CipherReader::XData` 里，旧版明文对象一律旧样式
- **柱、幕墙**：柱 `Tch::Detail::DecodeColumnCipher`（版本 7～10，版本 ≥ 9 的出图比例取对象自己的）；矩形 `Tch::Detail::DecodeRectCipher`（版本 3）；幕墙在 `Tch::Detail::DecodeWallCipher` 的墙字段之后读幕墙版本 3，分格列表是否存在看墙标志 0x100 / 0x10000（旧版明文同样，样例两位都有，没有实物核对）
- **影响显示的开关**（新旧版同读）：折断线按 `TchRupture::Count` 等分、0 不画；扶手 `TchHandRail::Align` 为 1 / 2 时路径是一侧边线，`TchHandRail::Draw2d` 为假不画；阳台 `TchBalcony::NoRailEdges` 置位的段（靠墙）不画，内缘在该段不偏移（`Tch::Detail::OffsetPathEdges`）；转角窗两段各按 `TchCornerWindow::Thickness` / `TchCornerWindow::Thickness2` 向自身方向右侧偏移；自动扶梯与多跑梯标志位 10 清零时不画箭头（`TchAutoStair::Flags`）；设备 Y 比例、阀门 Y 比例与象限（1 X 向镜像、2 两向、3 Y 向镜像），Y 比例近 0 时取 X 比例。读了仍没用的：修订版次文字、柱标注文字与保温层、双线管法兰、弧形平台与外侧栏杆、幕墙分格线、设备系统图块、转角窗文字转角
- **对称轴**：`Tch::BuildSymmetry`，两端各两条横线，距端点 Gap、Gap + Spacing（乘比例）
- **符号类**（坐标、箭头、剖切、引出标注、标高等）的线一律按实线组装，不用对象自身的线型
- **TCH文字**：显示宽度因子写在多行文字上（`MTextEntity::SetWidthFactor`），不派生文字样式；不限宽的居中 / 右对齐按实际排版宽度（`IDrawSink::MeasureMTextWidth`）定起点。中西文混排拆段（`Tch::Detail::AddRuns`），各段起点与画出的宽度都取TCH步进，按实际显示的字体（`Detail::DisplayExtent`）反算宽度因子
- **上标** `^U…^U`：字高为本段一半、抬高 0.5 × 名义字高、每字步进 0.35 × 西文步进；TrueType 样式未核对
- **墙**（`TchWall::Connections`）：门窗断口两侧封口；一端相接的墙里有同材料的就不封口（十字交叉处两段钢筋混凝土墙斜接、砖墙接上，三者之间都不画线），全是别的材料时画分界线；两端连接句柄都为空的自由端画端线（只在读到了连接时，`TchWall::HasConnections`）；断口与墙线端点比较用 1e-6 容差
- **连接量**（`TchWall::Joins`：左起、左终、右起、右终，正为缩进、负为伸出）：直墙沿墙线方向；弧墙是沿该侧圆弧的弧长（与天正显示核对）
- **门窗编号**（`TchOpening::Label`）：普通门窗与组合门窗、带形窗一样画编号；文字中心在 `TchOpening::LabelX` / `TchOpening::LabelY`，字高按 `TchOpening::LabelHeight` × 出图比例，沿门窗方向并转成正读（-90°～90°），放在对象引用的编号图层（`TchOpening::LabelLayer`）上、随层（与天正显示核对）。组合门窗的子门窗不画
- **幕墙**：非玻璃幕墙画法同墙；玻璃幕墙沿墙厚三等分画 4 条线，两端按连接量缩进或伸出，方头一端封口、与相邻幕墙斜接的一端不封口
- **墙柱填充**（`Importer::MakeTchHatch`）：图纸设置（`Tch::DecodeConfig`）的填充开关打开时才填；按材料码（`TchWall::Material`、`TchColumn::Material`）取图案，出图比例小于详图比例时用详图图案；图案经 `HatchPatternLibrary::FindTianzheng` 取，找不到就不画；填充放 `PUB_HATCH` 图层、随层
- **尺寸标注**：有拖动过的文字位置（`TchDimension::TextPositions`，文字中心）就用；竖直方向带误差时按自下而上读（`Detail::Upright`）
- **文字颜色**：尺寸数字取标注样式的 DIMCLRT（`TchDimStyle::TextColor`）；轴号放在对象引用的文字图层上、随层（`TchAxisLabel::TextLayer`）；索引、引出标注、箭头引注、剖切、指北针、楼梯的文字固定 7 号色，图名按对象存的颜色；TCH单行 / 多行文字随块；角度标注按 DIMAZIN 去零（`TchDimStyle::AngularZeros`）
- **TCH矩形**（`TchRect::Flag`）：1、2、3 分别加一条或两条对角线
- **门槛线**（`TchOpening::Flags`）：0x200 画在门扇一侧、0x400 画在对面，都没有时不画
- **核对基准**：几何以TCH分解结果为准，显示宽度、遮挡等以TCH的显示截图为准。脚本核对时先用TCH的样板启动，再在脚本里 `_.OPEN` 图纸：把图纸作为启动参数时图纸先于TCH插件读入，TCH对象成了代理，分解不出东西

## 测试与排查

- 按对象分的测试程序：`MiniCADTianzhengTests`（墙）、`MiniCADTchImportTests`、`MiniCADTchOpeningTests`、`MiniCADTchDimensionTests`、`MiniCADTchSymbolTests`、`MiniCADTchCipherTests`（新版对象：合成对象往返与写成 DWG 再读回、内视符号与剖切符号新国标样式、图纸设置；设 `MINICAD_TCH_SAMPLE=<路径>` 时把合成的建筑、楼梯、屋顶、符号与给排水、电气、暖通对象写成 R2010 图纸，可用第三方查看器对照；设 `MINICAD_HVAC_SAMPLE=<路径>` 时检查该暖通图纸的管线、风管标注与风管管件都能解码）、`MiniCADTchSheetTests`（TCH表格：合成的新旧版表格、格线与文字位置）、`MiniCADTchStairTests`、`MiniCADTchPathArrayTests`、`MiniCADTchGeometryTests`；样例图纸多在本机 `tests/Data`，不存在时跳过
- `MiniCADDwgInspect` 加 `MINICAD_GEOM=1` 输出TCH对象生成的几何（与TCH分解结果比对），`MINICAD_RAWOBJ=<DXF 名>` 导出某类对象的原始数据流，`MINICAD_H=<句柄>` 查看指定对象，`DIMSTYLE` 行打印 `azin`、`clrt` / `clrd` / `clre`；详见 skill `drawing-debug`

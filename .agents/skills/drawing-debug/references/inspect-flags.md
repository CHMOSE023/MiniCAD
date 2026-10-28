# MiniCADDwgInspect 的环境变量

`out/debug/tests/MiniCADDwgInspect.exe 图纸`（源文件 `tools/inspect_dwg.cpp`）。不设环境变量时打印原图的实体统计与未支持的对象；设了下面的变量再打印对应细节。坐标都是世界坐标，句柄是十六进制。

另有 `MiniCADDwgInspect.exe --advances 字体文件`：打印 MiniCAD 排版用的字符步进（字高 1）。

## 定位

| 变量 | 作用 |
|---|---|
| `MINICAD_AT=x0,y0,x1,y1` | 列出在该范围内画出线段的实体（逐层展开块参照） |
| `MINICAD_REDAT=x0,y0,x1,y1` | 列出在该范围内画出红色线段的顶层实体 |
| `MINICAD_BBOX=x0,y0,x1,y1` | 包围盒与之相交的顶层块参照及块内实体（递归 2 层）的类型、图层、颜色 |
| `MINICAD_RENDER=x0,y0,x1,y1[,文件]` | 把范围内的线画成 BMP（白底黑线，填充灰色），默认 `render.bmp`；`MINICAD_RENDER_INS` 另外打印范围内的块参照 |
| `MINICAD_LOCATE` | 画一遍模型空间，打印区域覆盖与含填充的顶层实体包围盒 |
| `MINICAD_H=句柄,…` | 这些对象的类型、所有者、是否在模型空间实体表里、不可见标志；实体另打印线型、颜色、透明度、图层（含图层扩展数据里的透明度 1071）；块记录打印块名与引用它的 INSERT（颜色、透明度、图层）；填充打印每条边界路径的边数、范围与边类型 |

## 文字、样式、图层

| 变量 | 作用 |
|---|---|
| `MINICAD_FINDTEXT=内容` | 内容相同的 TEXT 的句柄、所有者、是否在模型空间实体表里 |
| `MINICAD_GREPTEXT=子串` | 原文含该子串的 TEXT / ATTRIB / ATTDEF / MTEXT（句柄、类型、所有者、插入点（块内的是块坐标）、字高、文字样式及字体、未经处理的原始内容，格式码与 `\U+XXXX` 都原样打印） |
| `MINICAD_BLOCK=块名子串` | 名称含该子串的块里的文字（位置、字高、宽度因子、内容） |
| `MINICAD_GLYPHS` | 按文字样式解析字体，列出字体里没有字形的字符（需在有 `fonts` 目录的工作目录下运行） |
| `MINICAD_TABLES=1` | DWG 的 STYLE / LTYPE 表、布局与图纸空间块记录，导入后再打印场景里的两张表 |
| `MINICAD_LAYERS=子串` | 名称含该子串的图层的标志（1 冻结、4 锁定）、开关与颜色 |
| `MINICAD_HATCH` | 按所有者块 / 图层 / 图案统计 HATCH；`HATCHSKIP` 行列出边界没有边、或非实心又没存图案线的填充（读入时会跳过）。其余被跳过的是缺口过大、整条丢弃的边界路径（AutoCAD 也不画），用 `MINICAD_H` 看它的路径 |
| `MINICAD_HATCHPATHS` | 模型空间里每个 HATCH 的全部边界路径（标志、逐条边；多段线边逐顶点）——填充画成大块实心、范围不对时看各段是否相接 |
| `MINICAD_BULGE` | 统计多段线弧段与前一直线段相切时符合哪种凸度约定 |

## 天正对象

| 变量 | 作用 |
|---|---|
| `MINICAD_GEOM` | 天正对象生成的几何（`*U_TCH_<十进制句柄>` 块，嵌套块参照按 2D 变换展开），与天正分解结果比对；`MINICAD_GEOMBLOCKS` 另外输出块定义里的天正对象（块内局部坐标） |
| `MINICAD_RAWOBJ=DXF 名` | 导出该类型非图形未知对象（如 `TCH_DBCONFIG`）的主数据流与文字流（十六进制） |
| `MINICAD_PROXYEXT=类型` | 该类未建模实体的所有者、代理图形的记录类型与范围、私有数据大小 |
| `MINICAD_MLEADER` | 天正引出标注的解码结果与文字样式（字体、宽度、`_TCH` 扩展数据） |
| `MINICAD_EMAP` | 天正桥架 / 桥架配件的属性表（句柄、所有者、键(类型)=值） |
| `MINICAD_WALLRAW` | 天正墙受保护几何前的标记、密钥字节与 84 字节原始数据 |

## 视口、遮挡、裁剪、OLE

| 变量 | 作用 |
|---|---|
| `MINICAD_VIEWPORTS=1` | 全部 VIEWPORT 的视图参数（目标点、方向、视图中心、冻结图层） |
| `MINICAD_WIPEOUT` | WIPEOUT 的所有者块、图层与边界 |
| `MINICAD_WIPECHECK` | 按 Editor 的方式逐实体细分并用区域覆盖裁剪，统计遮罩内残留的线段 |
| `MINICAD_XCLIP` | 带 XCLIP 裁剪的块参照，解码裁剪边界 |
| `MINICAD_OLEDIR=目录` | 把每个 OLE2FRAME 的原始数据流写成 `<句柄>.ole` |
| `MINICAD_MEM` | 块参照展开与细分各占多少内存 |

源文件里还有少数未列出的变量（如 `MINICAD_ALLBLOCKS`、`MINICAD_LEAFTYPE`、`MINICAD_QUIT`），用到时看 `tools/inspect_dwg.cpp` 里 `getenv` 附近的注释。

---
name: drawing-debug
description: 排查 DWG/DXF 图纸在 MiniCAD 里打开后显示不对：缺东西、位置 / 线型 / 文字不对、天正对象、布局页视口、外部参照、OLE 对象。先看原图里有什么，再看 MiniCAD 画成了什么，对比定位是哪一步出错。不用于程序崩溃的调试、与图纸无关的界面问题，也不用于跑回归测试（用 test）。
covers: tools/inspect_dwg.cpp, apps/Win32/src/Host/SelfTest.cpp, .agents/skills/drawing-debug/*
---

# 排查图纸显示问题

思路：**原图里有什么** → **MiniCAD 读成了什么** → **画成了什么**，找到第一个出错的环节再改代码。改之前读对应的功能指南（`.agents/features/INDEX.md`）。

## 1. 先编好工具

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cmake --build out/debug --target MiniCADDwgInspect MiniCADWin 2>&1' | Select-String "error|FAILED"
```

- `out/debug/tests/MiniCADDwgInspect.exe 图纸`：只读地列出原图内容（按类型统计实体、未支持的对象），再按环境变量打印细节，见 [references/inspect-flags.md](references/inspect-flags.md)
- `out/debug/MiniCADWin/MiniCADWin.exe --bench 图纸`：按真实程序的流程打开、全部显示（同样分帧生成显示，画全后再截图、计时），可以离屏截图

## 2. 看原图里有什么

```powershell
$env:TEMP='D:\MiniCAD\out\tmp'; $env:TMP=$env:TEMP
& out/debug/tests/MiniCADDwgInspect.exe 图纸 *> out/tmp/inspect.txt
```

输出很长，重定向到文件再搜。按问题选环境变量（一次可以设多个，用完 `Remove-Item Env:MINICAD_*` 清掉）：

| 问题 | 环境变量 |
|---|---|
| 某处缺东西 / 多东西 | `MINICAD_AT=x0,y0,x1,y1`（谁在这里画了线）、`MINICAD_BBOX=…`（包围盒相交的块参照与块内实体）、`MINICAD_RENDER=x0,y0,x1,y1[,文件]`（把范围画成 BMP） |
| 某个对象 | `MINICAD_H=句柄,…`（类型、所有者、是否在模型空间、不可见标志） |
| 文字 / 字体 | `MINICAD_FINDTEXT=内容`、`MINICAD_BLOCK=块名子串`、`MINICAD_GLYPHS`（字体缺哪些字形）、`MINICAD_TABLES=1`（文字样式 / 线型表） |
| 天正对象 | `MINICAD_GEOM=1`（生成的几何，与天正分解结果比对）、`MINICAD_RAWOBJ=DXF 名`（原始数据流）、`MINICAD_MLEADER`、`MINICAD_EMAP`、`MINICAD_WALLRAW` |
| 布局页 / 视口 | `MINICAD_VIEWPORTS=1`、`MINICAD_LAYERS=子串`（冻结 / 关闭的图层） |
| 遮挡 / 裁剪 | `MINICAD_WIPEOUT`、`MINICAD_WIPECHECK`、`MINICAD_XCLIP` |
| OLE 对象 | `MINICAD_OLEDIR=目录`（导出原始数据） |

## 3. 看 MiniCAD 画成了什么

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/skills/drawing-debug/scripts/shot.ps1 图纸 [-Pause 秒] [-Release]
```

脚本把模型页（全部显示）、第一张布局页、第一个 OLE 对象的视口离屏存成 BMP 并转成 PNG，打印路径；用 Read 工具查看 PNG。**不要截整个屏幕**（会截到用户的其他窗口）。日志里的 `LOG_WARN`（跳过的实体、找不到的字体 / 外部参照）也在脚本输出的日志文件里。

## 4. 定位与修复

- 原图有、读入后没有：看 `src/Import/CadExchange.cpp` 的映射（跳过的类型会 `LOG_WARN`），天正对象看 `src/Import/Tch*.h`
- 读入了但画得不对：看实体的 `Draw()`、`InsertEntity` 的展开、`DrawContext` 的线型与文字
- 只在布局页不对：看 `Editor::ComposeModelIntoViewport` 与视口参数（[layout-viewport](../../features/layout-viewport.md)）
- 修好后补测试：能在临时目录里现做样例就现做；只能用用户图纸时放 `tests/Data`（不入库），测试在文件不存在时跳过；然后用 skill `test` 验证

## 注意

- 用户的图纸是用户数据：不要提交进版本库，想入库先问
- 核对天正对象以天正（AutoCAD 2014 + 对应天正版本）分解出的结果为准

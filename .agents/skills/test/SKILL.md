---
name: test
description: 按改动编译并运行 MiniCAD 的相关测试，只返回汇总（每个测试程序通过 / 失败项数、失败的那几行）。改完代码要验证时用它，不要自己拼编译命令或跑 run_tests.bat 全量；可指定测试名或 all。不用于设计测试用例、排查某个测试为什么失败（看输出与代码）或 CI 上的失败（先看 CI 日志）；排查图纸显示问题用 drawing-debug。
covers: tests/CMakeLists.txt, run_tests.bat, .agents/skills/test/run.ps1
---

# 按需测试

运行脚本（PowerShell 工具）：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/skills/test/run.ps1                   # 按工作区改动挑测试
pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/skills/test/run.ps1 MiniDWGTests      # 指定测试，可多个
pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/skills/test/run.ps1 all               # 全部 ctest + tests/Web 的 JS 测试
```

选项：`-Preset release`；`-Base <提交>`（改动按与该提交的差异算，默认 `HEAD` 即没提交的改动；分支上已提交的改动用 `-Base main`）。命令运行时间可能较长，给足超时（`NodeSolidTests` 的 debug 版约 7 分钟，用 `run_in_background`）。

脚本做的事：

1. 用 `git diff` 与未跟踪文件找出改动，按 `run.ps1` 顶部的 `$rules` 对照出测试目标（改了 `apps/Common` / `apps/Win32` 只编译 `MiniCADWin`，改了 `apps/Web` 跑 `tests/Web/*.cjs`）
2. 用 `vcvars64` 包好环境，只编译这些目标（VS 装在别处时设 `MINICAD_VCVARS`）；编译失败时列出错误行后退出
3. 把 TEMP 指到 `out/tmp`（本机程序写不了系统 TEMP），逐个运行，完整输出写进 `out/tmp/test/<目标>.txt`
4. 输出表格：每个程序的结果与用时，失败时附上 `[失败]` / `[FAIL]` 行（最多 15 行）；退出码 = 失败的程序个数
5. 最后运行 `.agents/scripts/check-docs.ps1`：报出文档里找不到的引用，以及改了代码而对应文档没改的提醒（不影响退出码）。有提醒时看一眼那份文档，行为、接口或约束变了就在同一次改动里更新

## 看结果之后

- 失败：先看表格里的失败行，需要上下文再读 `out/tmp/test/<目标>.txt`，不要重跑全部
- "崩溃"：程序异常退出，stdout 缓冲里的输出可能丢了，最后一行不一定是出错处；单独运行那个程序或在测试里加打印定位
- "找不到程序"：目标名写错，或该测试没启用（如三维测试要 `MINICAD_WITH_SOLID`）
- 界面改动（`MiniCADWin` 只编译）：需要时 `out/debug/MiniCADWin/MiniCADWin.exe --selftest`，或实际运行看；核对显示效果用 `--bench 图纸` 加 `MINICAD_BENCH_SHOT=目录` 离屏截图，不要截整个屏幕
- 改了控件外观：再跑 `MiniGUIGolden`（CI 不跑它；`--update` 更新基准，检查图片后再提交）
- 网页版行为：`build_wasm.bat serve` 后在浏览器里看
- Linux 宿主（`apps/Linux`）与 `src/UI/Platform/SDL` 在 Windows 上不编译，本脚本也不挑它们：改了要到 Linux 上编译运行（见 README 的 Linux 构建说明）

## 新增测试或测试程序

- 测试源文件登记在 `tests/CMakeLists.txt`；核心库的各组测试在 `tests/Core/SpatialIndexTests.cpp` 的 `main` 里调用
- 测试也要能在 Linux（GCC）上编译运行：Windows 专有的调用（`windows.h`、`_dupenv_s`、`SetConsoleOutputCP` 等）放进 `#ifdef _WIN32`；Windows 风格的路径别交给 `std::filesystem` 拆（Linux 不认反斜杠）
- 新增测试程序时在 `run.ps1` 的 `$rules` 里加一条对照
- 不依赖本机图纸：样例在临时目录里现做（`ExportCad` 写出再读回）；`tests/Data` 不在版本库里，本机图纸不存在时打印"跳过"并通过
- 中文路径按 UTF-8 构造 `std::filesystem::path`（`std::u8string`）
- 节点图测试放进哪个文件见 `tests/NodeGraph/NodeGraphTestCommon.h` / `NodeSolidCommon.h`

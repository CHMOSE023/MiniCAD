---
covers: cmake/ClShowIncludes.cpp, build_wasm.bat, run_tests.bat, CMakePresets.json, .gitignore
---

# 排错：按现象查

遇到报错或怪现象时，先在这里按现象找。每条：**现象** → 原因 → 做法。解决了不在表里的新问题，补一条。跑测试用 `.agents/skills/test`（编译环境、TEMP 等已在脚本里处理）。

## 编译

### `fatal error C1083: Cannot open include file: "cmath"`
- 原因：MSVC 的 `INCLUDE` / `LIB` 来自开发者环境变量，普通 PowerShell / cmd 里没有
- 做法：用 `vcvars64` 包一层，只编要用的目标（VS 装在别处时改路径；在 Developer PowerShell 或 Visual Studio 里不需要）：
  ```powershell
  cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cmake --build out/debug --target MiniCADWin 2>&1'
  ```

### 编译命令"成功"了，但 exe 时间戳没变 / 只打印了 cmd 的版权横幅 / `'elease' 不是内部或外部命令`
- 原因：在 Git Bash 里写 `cmd /c "\"...vcvars64.bat\" && ..."`，内层的 `\"` 被拆坏，cmd 收到残缺的命令行
- 做法：用 PowerShell，外层单引号（见上一条）；编完看一眼 exe 的时间戳

### 改了头文件却没重编，运行时莫名崩溃或行为是旧的
- 原因：中文版 cl 的 `/showIncludes` 前缀随控制台代码页变化，Ninja 曾因此记不下头文件依赖，新旧对象文件混用。现在 cl 经启动器 `cmake/ClShowIncludes.cpp` 运行，把前缀改写成 ASCII
- 做法：检查 `ninja -C out/debug -t deps`，除 SMLib（预编译头）和 `.rc.res` 外不应有 `#deps 0`；有的话重新配置 `out/debug`

### 编译到一半 exe 或 `rules.ninja` 不见了
- 原因：用户在 Visual Studio 里打开了项目，VS 正在重新配置 `out/debug`
- 做法：等它配置完再编，不要同时编译

### 网页版构建出现 `warning: unknown escape sequence`（在 `EM_JS` 里）
- 原因：`EM_JS` 里的 JS 被当作 C 字符串字面量，`/^\.+$/` 里的 `\.` 是 C 不认识的转义，结果被改成 `.`，正则的意思全变了
- 做法：JS 里避免这类转义（`[\\/]` 这种合法转义没问题），改写成不用反斜杠的判断；改完看构建输出

### `emcmake: no compatible cmake generator found`
- 原因：找不到 Ninja
- 做法：`build_wasm.bat` 会在 Visual Studio 安装目录里找 Ninja；手动配置时把 Ninja 加进 `PATH`，或传 `-DCMAKE_MAKE_PROGRAM=<ninja.exe 的路径>`

### `PermissionError: emsdk_set_env.bat`
- 原因：`emsdk_env.bat` 要在 emsdk 目录（`D:\dev\emsdk`）里写 `emsdk_set_env.bat`
- 做法：用对该目录有写权限的身份运行

## 运行与测试

### 测试或自测往临时目录写文件失败，`--selftest` 在写大图纸那一项以 `0xC0000409` 崩溃，`MiniCADDrawingOpenTests` 无输出、退出码 3
- 原因：本机从项目目录启动的程序写不了系统 TEMP（`%LOCALAPPDATA%`、用户目录也不行，只能写项目目录；管理员身份可以）
- 做法：先把 TEMP 指到项目内：`$env:TEMP='D:\MiniCAD\out\tmp'; $env:TMP=$env:TEMP`（测试 skill 已经这样做）

### `cmd /c "cd /d 目录 && xxx.exe"` 报 "不是内部或外部命令"
- 原因：本机的 cmd 不从当前目录找程序
- 做法：写 exe 的全路径，或在 PowerShell 里 `Push-Location` 后 `& .\xxx.exe`

### 测试程序输出停在一半，退出码 3（`abort`），最后一行看起来没问题
- 原因：`abort` 时 stdout 缓冲里的输出丢了，最后一行不是出错处；常见的是未捕获的异常（例如窄字符串构造中文路径时抛出的转换异常）
- 做法：重定向到文件；单独运行那一组；在可疑处加打印，或用 stderr（不缓冲）输出

### 中文路径的图纸打不开 / 测试建出乱码目录（如 `建筑` 变成 `寤虹瓚`）
- 原因：`std::filesystem::path` 用窄字符串构造、或用 `path::string()` 转回，都按系统代码页（GBK）转换；英文系统或遇到 GBK 无法映射的字节时直接抛异常
- 做法：接口间一律传 UTF-8；转成 path 用 `std::u8string`，转回用 `u8string()`；测试里拼路径同样如此

### 网页版改了代码，浏览器里还是旧的
- 原因：浏览器缓存了 `index.js` / `index.wasm`
- 做法：确认 `build_wasm.bat` 输出了 `[完成]`，浏览器里 `Ctrl+F5` 强制刷新

## 改文件

### 编辑工具拒绝修改 `apps/Win32/src/Host/Win32Window.cpp`（"File has NUL bytes"）
- 原因：文件里有故意的 NUL 字节（文件对话框的过滤串），而且是 CRLF 换行
- 做法：用脚本按字节读写，插入的行也用 `\r\n`

### 经 Bash 写进文件的代码里，`\n` 变成了真换行、`\\` 变成了单个反斜杠
- 原因：Bash 工具里的 heredoc / 引号会处理反斜杠
- 做法：往文件里写带转义的代码时用 Edit / Write 工具，或先把脚本写成文件再运行；PowerShell 的 `` `n `` 替换也出过事（整个文件的换行被换掉），改完核对行数

### 依赖样例图纸的测试在 CI 上失败（本机通过）
- 原因：`tests/Data` 被 `.gitignore` 忽略，本机的样例图纸（天正图纸、`Xref.dwg` 等）不在版本库里
- 做法：文件不存在时打印"跳过"并通过；能在临时目录里现做样例就现做；想把图纸入库先问用户

## 核对显示效果

- 不要截整个屏幕（会截到用户的其他窗口）：用 `MiniCADWin.exe --bench 图纸` 加 `MINICAD_BENCH_SHOT=目录` 离屏存成 BMP，再转 PNG 查看（见 skill `drawing-debug`）

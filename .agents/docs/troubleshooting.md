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

### 用备份把文件还原后，编出的程序还是改过的那一版（例如临时跳过的自测节仍被跳过）
- 原因：`Copy-Item`、`cp -p`、解压都保留备份文件的修改时间，还原后的源文件比对象文件旧，Ninja 认为不用重编（编译"通过"只用几秒）
- 做法：还原后更新时间戳 `(Get-Item <文件>).LastWriteTime = Get-Date` 再编；`git checkout` / `git stash pop` 写出的文件是当前时间，没有这个问题

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

### 网页版构建停在 / 失败于 `ports:INFO: retrieving port: emdawnwebgpu`
- 原因：WebGPU 绑定是 Emscripten 的远程端口（`--use-port=emdawnwebgpu`，见 `cmake/Render.cmake`），第一次构建时从 GitHub 下载固定版本的端口包到 `<emsdk>/upstream/emscripten/cache/ports`
- 做法：联网后重新构建；离线的机器从已经下载过的机器复制 `cache/ports/emdawnwebgpu*` 过来。升级 emsdk 后端口版本可能跟着变，会再下载一次

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

### 网页版显示不对，不知道是哪个图形后端的问题
- 原因：网页版默认用 WebGPU，浏览器不支持时用 WebGL2，两套渲染器各写一份（`apps/Render/WebGPU`、`apps/Render/GLES3`）
- 做法：关于对话框（F1）看"图形"一行；网址加 `?webgl` 强制 WebGL2 对比。WebGPU 的校验错误打在浏览器控制台（`WebGPU 错误`），`abort('wgpuSurfacePresent is unsupported')` 表示有人调用了 `wgpuSurfacePresent`——浏览器里呈现是自动的，不能调用它

## 改文件

### 经 Bash 写进文件的代码里，`\n` 变成了真换行、`\\` 变成了单个反斜杠
- 原因：Bash 工具里的 heredoc / 引号会处理反斜杠
- 做法：往文件里写带转义的代码时用 Edit / Write 工具，或先把脚本写成文件再运行；PowerShell 的 `` `n `` 替换也出过事（整个文件的换行被换掉），改完核对行数

### 依赖样例图纸的测试在 CI 上失败（本机通过）
- 原因：`tests/Data` 被 `.gitignore` 忽略，本机的样例图纸（天正图纸、`Xref.dwg` 等）不在版本库里
- 做法：文件不存在时打印"跳过"并通过；能在临时目录里现做样例就现做；想把图纸入库先问用户

## 核对显示效果

- 不要截整个屏幕（会截到用户的其他窗口）：用 `MiniCADWin.exe --bench 图纸` 加 `MINICAD_BENCH_SHOT=目录` 离屏存成 BMP，再转 PNG 查看（见 skill `drawing-debug`）

### 同一张图纸两次运行，画面不一样（间歇出现）
- 原因：多半是读了未初始化的内存。实例：stb_truetype 处理"只有一个曲线外的点"的轮廓（宋体的空格）时越界读下一个点，空格画成一串长斜线，位置每次不同（已修，见 `3rd/stb/README.md`）
- 做法：同一个 exe 用 `--bench` 加不同的 `MINICAD_BENCH_SHOT` 目录连跑多次，逐像素比较截图，先确认是不确定性而不是渲染器差异；再在截图处把场景存成 JSON（数据层）、把各路显示数据与字形网格（`GlyphMeshLibrary`）逐项打印出来比较，缩小到具体的实体或字形

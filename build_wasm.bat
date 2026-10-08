@echo off
chcp 65001 >nul
rem 构建 MiniCAD 网页版（界面与桌面版相同，MiniGUI + WebGL2）
rem 用法：build_wasm.bat [configure|serve]
rem   不带参数  增量构建（首次自动配置）
rem   configure 清空 out\build\wasm 后重新配置并构建
rem   serve     构建后在 http://localhost:8080 启动本地服务器（需要 Python）
rem 输出：out\build\wasm\MiniCADWeb\index.html（另有 MiniGUIWebGallery：MiniGUI 控件展示）

setlocal

REM ---- 路径配置 ------------------------------------------------------------
set "EMSDK_ENV=D:\dev\emsdk\emsdk_env.bat"
set "NINJA_EXE="
for %%p in (
    "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
) do if not defined NINJA_EXE if exist %%p set "NINJA_EXE=%%~p"

REM 项目目录：%~dp0 自带尾随 \，需去掉避免 -S "...\\" 引号转义错乱
set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"

set "BUILD_DIR=%PROJECT_DIR%\out\build\wasm"

REM ---- 检查依赖 ------------------------------------------------------------
if not exist "%EMSDK_ENV%" (
    echo [错误] emsdk 未找到: %EMSDK_ENV%
    exit /b 1
)
if not defined NINJA_EXE (
    echo [错误] 未找到 ninja，请编辑本脚本顶部的候选路径
    exit /b 1
)

REM ---- 把 ninja 目录加到 PATH（emcmake 自检需要） --------------------------
for %%i in ("%NINJA_EXE%") do set "NINJA_DIR=%%~dpi"
set "PATH=%NINJA_DIR%;%PATH%"
call "%EMSDK_ENV%" >nul

if /i "%1"=="configure" goto :CONFIGURE
if not exist "%BUILD_DIR%\build.ninja" goto :CONFIGURE
goto :BUILD

REM ---- 配置 ----------------------------------------------------------------
:CONFIGURE
echo [配置] 重新生成 CMake 工程...
if exist "%BUILD_DIR%" rd /s /q "%BUILD_DIR%"
call emcmake cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%"
if errorlevel 1 (
    echo [错误] CMake 配置失败
    exit /b 1
)

REM ---- 构建 ----------------------------------------------------------------
:BUILD
echo [构建] 编译 MiniCAD 网页版...
cmake --build "%BUILD_DIR%" --target MiniCADWeb MiniGUIWebGallery
if errorlevel 1 (
    echo [错误] 构建失败
    exit /b 1
)
echo [完成] %BUILD_DIR%\MiniCADWeb\index.html

if /i "%1"=="serve" (
    echo [服务] http://localhost:8080/MiniCADWeb/  （Ctrl+C 停止）
    python -m http.server 8080 --bind 127.0.0.1 --directory "%BUILD_DIR%"
)
exit /b 0

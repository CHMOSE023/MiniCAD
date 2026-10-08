@echo off
chcp 65001 >nul 

 setlocal

REM ---- 路径配置 ------------------------------------------------------------
set "EMSDK_ENV=D:\dev\emsdk\emsdk_env.bat"
set "NINJA_EXE=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
REM "NINJA_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

REM 项目目录：%~dp0 自带尾随 \，需去掉避免 -S "...\\" 引号转义错乱
set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"

set "BUILD_DIR=%PROJECT_DIR%\out\build\wasm"


REM ---- 检查依赖 ------------------------------------------------------------
if not exist "%EMSDK_ENV%" (
    echo [错误] emsdk 未找到: %EMSDK_ENV%
    exit /b 1
)
if not exist "%NINJA_EXE%" (
    echo [错误] ninja 未找到: %NINJA_EXE%
    echo        请编辑本脚本顶部的 NINJA_EXE 路径
    exit /b 1
)

REM ---- 把 ninja 目录加到 PATH（emcmake 自检需要） --------------------------
for %%i in ("%NINJA_EXE%") do set "NINJA_DIR=%%~dpi"
set "PATH=%NINJA_DIR%;%PATH%"

REM ---- 配置 ----------------------------------------------------------------
:CONFIGURE
echo [配置] 重新生成 CMake 工程...
if exist "%BUILD_DIR%" rd /s /q "%BUILD_DIR%"
call "%EMSDK_ENV%" >nul
call emcmake cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%"
if errorlevel 1 (
    echo [错误] CMake 配置失败
    exit /b 1
)
echo [完成] 配置完成
goto :BUILD

REM ---- 构建 ----------------------------------------------------------------
:BUILD
if not exist "%BUILD_DIR%\build.ninja" (
    echo [提示] 首次构建，先执行配置...
    goto :CONFIGURE
)

echo [构建] 编译 WASM...
call "%EMSDK_ENV%" >nul
cmake --build "%BUILD_DIR%" --target WASM
if errorlevel 1 (
    echo [错误] 构建失败
    exit /b 1
)
 
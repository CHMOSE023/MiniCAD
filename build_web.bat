@echo off
chcp 65001 >nul
REM ============================================================================
REM  MiniCAD Web (WebAssembly) 构建脚本
REM  ----------------------------------------------------------------------------
REM  用法:
REM    build_web.bat              ; 增量构建
REM    build_web.bat clean        ; 清理后全量重建
REM    build_web.bat configure    ; 重新生成 CMake 工程
REM    build_web.bat serve        ; 构建并启动本地服务器 (http://localhost:8080)
REM ============================================================================

setlocal

REM ---- 路径配置 ------------------------------------------------------------
set "EMSDK_ENV=D:\dev\emsdk\emsdk_env.bat"
set "NINJA_EXE=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

REM 项目目录：%~dp0 自带尾随 \，需去掉避免 -S "...\\" 引号转义错乱
set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"

set "BUILD_DIR=%PROJECT_DIR%\out\web"
set "OUTPUT_DIR=%BUILD_DIR%\MiniCADWeb"
set "PORT=8080"

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

REM ---- 解析命令 ------------------------------------------------------------
set "CMD=%~1"
if "%CMD%"=="" set "CMD=build"

if /i "%CMD%"=="clean"     goto :CLEAN
if /i "%CMD%"=="configure" goto :CONFIGURE
if /i "%CMD%"=="serve"     goto :BUILD_AND_SERVE
if /i "%CMD%"=="build"     goto :BUILD
goto :USAGE


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


REM ---- 清理 ----------------------------------------------------------------
:CLEAN
echo [清理] 清理后全量重建...
if not exist "%BUILD_DIR%\build.ninja" goto :CONFIGURE
call "%EMSDK_ENV%" >nul
cmake --build "%BUILD_DIR%" --target MiniCADWeb --clean-first
goto :END


REM ---- 构建 ----------------------------------------------------------------
:BUILD
if not exist "%BUILD_DIR%\build.ninja" (
    echo [提示] 首次构建，先执行配置...
    goto :CONFIGURE
)

REM 强制重链：shell-file (index.html) 不在依赖追踪里
if exist "%OUTPUT_DIR%\index.html" del /q "%OUTPUT_DIR%\index.html"

echo [构建] 编译 MiniCADWeb...
call "%EMSDK_ENV%" >nul
cmake --build "%BUILD_DIR%" --target MiniCADWeb
if errorlevel 1 (
    echo [错误] 构建失败
    exit /b 1
)

echo.
echo ============================================================================
echo  [完成] 构建成功
echo  输出: %OUTPUT_DIR%\index.html
echo ============================================================================
echo.
goto :END


REM ---- 构建 + 启动本地服务器 -----------------------------------------------
:BUILD_AND_SERVE
call :BUILD
where python >nul 2>&1
if errorlevel 1 (
    echo [警告] 未找到 python，无法启动本地服务器
    echo        手动运行: python -m http.server %PORT% --directory "%OUTPUT_DIR%"
    goto :END
)
echo [服务] http://localhost:%PORT%   按 Ctrl+C 停止
echo.
start "" http://localhost:%PORT%
python -m http.server %PORT% --directory "%OUTPUT_DIR%"
goto :END


REM ---- 用法 ----------------------------------------------------------------
:USAGE
echo 用法:
echo   build_web.bat              增量构建
echo   build_web.bat clean        清理后全量重建
echo   build_web.bat configure    重新生成 CMake 工程
echo   build_web.bat serve        构建并启动本地服务器
exit /b 1

:END
endlocal

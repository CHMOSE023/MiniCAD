@echo off
chcp 65001 >nul
rem 一键构建并运行全部测试（MiniCADTests、MiniGUITests、MiniGUIGolden、MiniDWGTests）
rem 用法：run_tests.bat [debug|release]，默认 debug
rem 基准图片需要更新时：out\<preset>\MiniGUIGolden.exe --update，检查图片后再提交

setlocal
set PRESET=%1
if "%PRESET%"=="" set PRESET=debug

rem 普通命令行里缺少 MSVC 的 INCLUDE/LIB 环境变量，先调用 vcvars64（VS 安装位置不同时修改这里）
if not defined VCINSTALLDIR (
    call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)

cd /d "%~dp0"
cmake --preset %PRESET% >nul || exit /b 1
cmake --build --preset %PRESET% || exit /b 1
ctest --test-dir out\%PRESET% --output-on-failure
exit /b %ERRORLEVEL%

cmake_minimum_required(VERSION 3.20)

# 版本号的唯一来源：发布时只改这里（桌面版的 exe 版本信息由它生成，见 apps/Win32）
set(MINICAD_VERSION 0.2.1.2)

project(MiniCAD VERSION ${MINICAD_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 静态链接 MSVC 运行库（/MT、/MTd），拷到其他电脑上运行时不再依赖 MSVCP140(D).dll / VCRUNTIME140(D).dll
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# 三个库的头文件各有自己的 include 根：MiniCAD → src，MiniGUI → src/UI，MiniDWG → src/Serialization。
# 各根之下的 "目录/文件.h" 相对路径不能重复，否则会按搜索顺序互相遮挡（编译器不报错）。

# ── MiniDWG（DWG/DXF 读写库）────────────────────────────────────────
include(cmake/MiniDWG.cmake)

# ── MiniGUI（保留模式界面库）─────────────────────────────────────────
include(cmake/MiniGUI.cmake)

# ── SMLib（第三方实体建模内核，可选，MINICAD_WITH_SOLID）─────────────────
include(cmake/SMLib.cmake)

# ── MiniCAD（平台无关的 CAD 核心库）──────────────────────────────────
include(cmake/MiniCAD.cmake)

# ── 平台应用 ────────────────────────────────────────────────────────
add_subdirectory("apps/Common")     # 主窗口界面与逻辑（桌面版、网页版共用）

if(WIN32 AND NOT EMSCRIPTEN)
    add_subdirectory("apps/Win32")
endif()

if(EMSCRIPTEN)
    if(MINICAD_BUILD_LEGACY_WASM)
        add_subdirectory(wasm)
    else()
        add_subdirectory("apps/Web")
    endif()
endif()

# ── 测试（桌面端）：tests/Core、tests/UI、tests/Serialization ─────────
option(MINICAD_BUILD_TESTS "Build MiniCAD tests" ON)
if(MINICAD_BUILD_TESTS AND NOT EMSCRIPTEN)
    enable_testing()
    add_subdirectory(tests)
endif()

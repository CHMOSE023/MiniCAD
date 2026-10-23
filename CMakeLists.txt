cmake_minimum_required(VERSION 3.20)

# 版本号的唯一来源：发布时只改这里（MiniCADVersion.h 由它生成，见 apps/Common：exe 版本信息、关于对话框、状态栏）
set(MINICAD_VERSION 0.2.4)

project(MiniCAD VERSION ${MINICAD_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 静态链接 MSVC 运行库（/MT、/MTd），拷到其他电脑上运行时不再依赖 MSVCP140(D).dll / VCRUNTIME140(D).dll
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Ninja + MSVC：头文件依赖靠解析 cl /showIncludes 的输出。中文版 cl 的前缀"注意: 包含文件:"按构建时控制台的代码页
# 输出（GBK / UTF-8），ninja 却只认配置时写进 rules.ninja 的那一种字节；对不上时 ninja 记下的依赖为空，改了头文件不重编，
# 新旧 .obj / .lib 混用，编译通过、运行时崩溃。VSLANG=1033 只在装了英文语言包时有效，所以这里经启动器 ClShowIncludes
# （cmake/ClShowIncludes.cpp）编译：它把各种编码的本地化前缀改写成 ASCII 的 "Note: including file:"，rules.ninja 也写这个。
if(CMAKE_GENERATOR MATCHES "Ninja" AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_CXX_CL_SHOWINCLUDES_PREFIX)
    string(REGEX REPLACE " +$" "" _clPrefix "${CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}")
    if(NOT _clPrefix MATCHES "^[ -~]*$")   # 含非 ASCII 字符才需要
        set(_clLauncher "${CMAKE_BINARY_DIR}/ClShowIncludes.exe")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/cmake/ClShowIncludes.cpp")   # 改了启动器就重新配置、重新编译它
        if(NOT EXISTS "${_clLauncher}" OR "${CMAKE_SOURCE_DIR}/cmake/ClShowIncludes.cpp" IS_NEWER_THAN "${_clLauncher}")
            try_compile(_clLauncherOk "${CMAKE_BINARY_DIR}/ClShowIncludes" "${CMAKE_SOURCE_DIR}/cmake/ClShowIncludes.cpp"
                        COMPILE_DEFINITIONS /utf-8 COPY_FILE "${_clLauncher}" OUTPUT_VARIABLE _clLauncherLog)
        endif()
        if(EXISTS "${_clLauncher}")
            string(HEX "${_clPrefix}" _clPrefixHex)
            set(CMAKE_CL_SHOWINCLUDES_PREFIX "Note: including file:")
            set(CMAKE_CXX_COMPILER_LAUNCHER "${_clLauncher}" "--prefix=${_clPrefixHex}" ${CMAKE_CXX_COMPILER_LAUNCHER})
        else()
            message(WARNING "ClShowIncludes 编译失败，头文件依赖可能因控制台代码页不同而失效：\n${_clLauncherLog}")
        endif()
    endif()
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

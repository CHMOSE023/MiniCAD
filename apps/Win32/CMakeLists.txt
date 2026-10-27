cmake_minimum_required(VERSION 3.20)
project(MiniCADWin VERSION ${MINICAD_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 渲染后端（MiniCADRender、MiniGUI_D3D11、MiniGUI_Software）定义在 cmake/Render.cmake，源码在 apps/Render

# ── MiniCAD 桌面版：Win32 宿主（窗口 + 图形设备）+ 共用的主窗口界面（MiniCADApp）──

# 版本号头文件 MiniCADVersion.h 由 apps/Common 生成（目标 MiniCADVersionHeader），MiniCAD.rc 用它填写 exe 版本信息

add_executable(MiniCADWin
    src/Host/GuiMain.cpp
    src/Host/Win32Window.cpp
    src/Host/Win32Shell.cpp
    src/Host/SelfTest.cpp
    src/MiniCAD.rc
)

target_include_directories(MiniCADWin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${MINICAD_VERSION_DIR}
    ${CMAKE_SOURCE_DIR}/3rd
)

add_dependencies(MiniCADWin MiniCADVersionHeader)

target_link_libraries(MiniCADWin PRIVATE
    MiniCADApp
    MiniCAD
    MiniGUI_Win32
    MiniCADRender
)

# 三维实体内核（SMLib，见 docs/Solid/PLAN.md）
if(MINICAD_WITH_SOLID)
    target_link_libraries(MiniCADWin PRIVATE MiniCADSolid)
    target_compile_definitions(MiniCADWin PRIVATE MINICAD_WITH_SOLID)
endif()

if(MSVC)
    target_compile_options(MiniCADWin PRIVATE
        /utf-8
        /Z7                  # 调试信息嵌入 .obj，避免共享 PDB 锁定问题
        /FIpch.h             # 强制包含 Windows 头文件
    )
    target_compile_definitions(MiniCADWin PRIVATE
        UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

option(USE_WIN32 "Use Win GUI subsystem (no console)" OFF)
if(USE_WIN32)
    set_target_properties(MiniCADWin PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_definitions(MiniCADWin PRIVATE USE_WIN32)
endif()

set_target_properties(MiniCADWin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/MiniCADWin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/MiniCADWin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/MiniCADWin"
    VS_DEBUGGER_WORKING_DIRECTORY    "${CMAKE_BINARY_DIR}/MiniCADWin"
)

# 复制共享资产（字体、图标、shader、界面描述文件）
add_custom_command(TARGET MiniCADWin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/../../assets"
            "$<TARGET_FILE_DIR:MiniCADWin>"
    COMMENT "Copy shared assets for MiniCADWin"
)

cmake_minimum_required(VERSION 3.20)
project(MiniCADWin VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── MiniGUI 渲染后端（源码在 src/Render，头文件以 src/Render 为根："D3D11/D3D11Backend.h"）──
# D3D11Backend 画界面（MiniGUI::IRenderBackend），与画 CAD 视口的 D3D11Renderer 是两回事
add_library(MiniGUI_D3D11 STATIC
    src/Render/D3D11/D3D11Backend.cpp
)

target_include_directories(MiniGUI_D3D11 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Render
)

target_link_libraries(MiniGUI_D3D11
    PUBLIC  MiniGUI
    PRIVATE d3d11 D3DCompiler
)

# 软件光栅后端（与平台无关，用于截图对比测试）
add_library(MiniGUI_Software STATIC
    src/Render/Software/SoftwareBackend.cpp
)

target_include_directories(MiniGUI_Software PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Render
)

target_link_libraries(MiniGUI_Software PUBLIC MiniGUI)

if(MSVC)
    target_compile_options(MiniGUI_D3D11 PRIVATE /utf-8 /W4)
    target_compile_definitions(MiniGUI_D3D11 PRIVATE
        UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    target_compile_options(MiniGUI_Software PRIVATE /utf-8 /W4)
endif()

# ── MiniCAD 桌面版：整个界面由 MiniGUI 绘制，CAD 视口由 D3D11Renderer 渲染 ──
add_executable(MiniCADWin
    src/GUI/GuiMain.cpp
    src/GUI/MainFrame.cpp
    src/GUI/MainCommands.cpp
    src/GUI/MainPanels.cpp
    src/GUI/LayerPanels.cpp
    src/GUI/CommandLine.cpp
    src/GUI/DynamicInput.cpp
    src/GUI/EditorDialogs.cpp
    src/GUI/StatusBarView.cpp
    src/MiniCAD.rc
    src/Render/D3D11/D3D11Renderer.cpp
    src/Render/D3D11/D3D11RenderTarget.cpp
    src/Render/D3D11/Device.cpp
    src/Render/D3D11/Shader.cpp
    src/Render/D3D11/SwapChain.cpp
)

target_include_directories(MiniCADWin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/3rd
)

target_link_libraries(MiniCADWin PRIVATE
    MiniCADLib
    MiniGUI_Win32
    MiniGUI_D3D11
    d3d11
    dxgi
    D3DCompiler
)

if(MSVC)
    target_compile_options(MiniCADWin PRIVATE
        /utf-8
        /Z7                  # 调试信息嵌入 .obj，避免共享 PDB 锁定问题
        /FIpch.h             # 强制包含，解决 D3D11 头文件顺序依赖
    )
    target_compile_definitions(MiniCADWin PRIVATE
        UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# 开发时直接读源码目录里的界面描述文件：修改后立即重新加载，不必重新编译或复制
target_compile_definitions(MiniCADWin PRIVATE
    MINICAD_UI_SOURCE="${CMAKE_SOURCE_DIR}/assets/ui/minicad_ui.json"
)

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

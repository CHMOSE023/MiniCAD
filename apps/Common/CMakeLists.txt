# ── MiniCAD 主窗口的界面与逻辑（桌面版、网页版共用）──────────────────
# 平台相关的部分（窗口、图形设备、消息循环、文件对话框）由 AppPlatform 提供：
# Win32 版见 apps/Win32/src/Host，网页版见 apps/Web/src/Host
add_library(MiniCADApp STATIC
    src/GUI/MainFrame.cpp
    src/GUI/MainCommands.cpp
    src/GUI/MainPanels.cpp
    src/GUI/LayerPanels.cpp
    src/GUI/CommandLine.cpp
    src/GUI/DynamicInput.cpp
    src/GUI/EditorDialogs.cpp
    src/GUI/StatusBarView.cpp
)

target_include_directories(MiniCADApp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(MiniCADApp PUBLIC MiniCADLib MiniGUI)

if(MSVC)
    target_compile_options(MiniCADApp PRIVATE /utf-8)
    target_compile_definitions(MiniCADApp PRIVATE
        UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

if(NOT EMSCRIPTEN)
    # 开发时直接读源码目录里的界面描述文件：修改后立即重新加载，不必重新编译或复制
    target_compile_definitions(MiniCADApp PRIVATE
        MINICAD_UI_SOURCE="${CMAKE_SOURCE_DIR}/assets/ui/minicad_ui.json"
    )
endif()

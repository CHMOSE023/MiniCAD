# ── MiniCAD 主窗口的界面与逻辑（桌面版、网页版共用）──────────────────
# 平台相关的部分（窗口、图形设备、消息循环、文件对话框）由 AppPlatform 提供：
# Win32 版见 apps/Win32/src/Host，网页版见 apps/Web/src/Host
# 版本号由根 CMakeLists.txt 的 MINICAD_VERSION 提供，生成 MiniCADVersion.h：关于对话框、状态栏（桌面版与网页版）、
# 桌面版 MiniCAD.rc 的 exe 版本信息共用。第四段构建号取 git 提交数，每次构建都重新生成（内容不变时不改写），新提交后不用重新配置
set(MINICAD_VERSION_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated CACHE INTERNAL "MiniCADVersion.h 所在目录")
set(MINICAD_VERSION_HEADER ${MINICAD_VERSION_DIR}/MiniCADVersion.h)
add_custom_target(MiniCADVersionHeader
    COMMAND ${CMAKE_COMMAND}
        -DVERSION=${MINICAD_VERSION}
        -DSOURCE_DIR=${MiniCAD_SOURCE_DIR}
        -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/src/MiniCADVersion.h.in
        -DOUTPUT=${MINICAD_VERSION_HEADER}
        -P ${MiniCAD_SOURCE_DIR}/cmake/MiniCADVersion.cmake
    BYPRODUCTS ${MINICAD_VERSION_HEADER}
    COMMENT "生成 MiniCADVersion.h"
    VERBATIM
)

add_library(MiniCADApp STATIC
    src/GUI/MainFrame.cpp
    src/GUI/MainCommands.cpp
    src/GUI/MainPanels.cpp
    src/GUI/StartPage.cpp
    src/GUI/ViewNavBar.cpp
    src/GUI/LayerPanels.cpp
    src/GUI/CommandLine.cpp
    src/GUI/DynamicInput.cpp
    src/GUI/EditorDialogs.cpp
    src/GUI/StatusBarView.cpp
    src/GUI/ViewCube.cpp
    src/GUI/NodeEditor.cpp
    src/GUI/NodePanel.cpp
)

target_include_directories(MiniCADApp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${MINICAD_VERSION_DIR})
add_dependencies(MiniCADApp MiniCADVersionHeader)
target_link_libraries(MiniCADApp PUBLIC MiniCAD MiniGUI)

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

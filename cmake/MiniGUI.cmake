# ── MiniGUI（保留模式界面库，源码在 src/UI）─────────────────────────────
# 头文件以 src/UI 为根（"Core/UIContext.h"、"Widgets/Button.h"），命名空间 MiniGUI。
# 平台层在 src/UI/Platform：Win32、Web（Emscripten）、SDL（Linux）。渲染后端在 apps/Render（cmake/Render.cmake）。

set(MINIGUI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/UI")

# ── Core ────────────────────────────────────────────────────────────
set(MINIGUI_CORE_SOURCES
    "${MINIGUI_SRC}/Core/Node.cpp"
    "${MINIGUI_SRC}/Core/UIContext.cpp"
    "${MINIGUI_SRC}/Core/ShortcutTable.cpp"
    "${MINIGUI_SRC}/Core/Popup.cpp"
)

# ── Layout ──────────────────────────────────────────────────────────
set(MINIGUI_LAYOUT_SOURCES
    "${MINIGUI_SRC}/Layout/FlexLayout.cpp"
)

# ── Paint ───────────────────────────────────────────────────────────
set(MINIGUI_PAINT_SOURCES
    "${MINIGUI_SRC}/Paint/DrawList.cpp"
    "${MINIGUI_SRC}/Paint/Image.cpp"
)

# ── Data ────────────────────────────────────────────────────────────
set(MINIGUI_DATA_SOURCES
    "${MINIGUI_SRC}/Data/Json.cpp"
    "${MINIGUI_SRC}/Data/CommandRegistry.cpp"
)

# ── Style ───────────────────────────────────────────────────────────
set(MINIGUI_STYLE_SOURCES
    "${MINIGUI_SRC}/Style/ThemeColors.cpp"
)

# ── Text ────────────────────────────────────────────────────────────
set(MINIGUI_TEXT_SOURCES
    "${MINIGUI_SRC}/Text/Font.cpp"
    "${MINIGUI_SRC}/Text/GlyphAtlas.cpp"
    "${MINIGUI_SRC}/Text/TextSystem.cpp"
)

# ── Widgets ─────────────────────────────────────────────────────────
set(MINIGUI_WIDGETS_SOURCES
    "${MINIGUI_SRC}/Widgets/Panel.cpp"
    "${MINIGUI_SRC}/Widgets/Button.cpp"
    "${MINIGUI_SRC}/Widgets/Label.cpp"
    "${MINIGUI_SRC}/Widgets/TextBox.cpp"
    "${MINIGUI_SRC}/Widgets/Controls.cpp"
    "${MINIGUI_SRC}/Widgets/ScrollView.cpp"
    "${MINIGUI_SRC}/Widgets/ListView.cpp"
    "${MINIGUI_SRC}/Widgets/Menu.cpp"
    "${MINIGUI_SRC}/Widgets/ComboBox.cpp"
    "${MINIGUI_SRC}/Widgets/Dialog.cpp"
    "${MINIGUI_SRC}/Widgets/Splitter.cpp"
    "${MINIGUI_SRC}/Widgets/TabView.cpp"
    "${MINIGUI_SRC}/Widgets/NumberBox.cpp"
    "${MINIGUI_SRC}/Widgets/AutoComplete.cpp"
    "${MINIGUI_SRC}/Widgets/ImageView.cpp"
    "${MINIGUI_SRC}/Widgets/ColorPicker.cpp"
    "${MINIGUI_SRC}/Widgets/InlineEditor.cpp"
    "${MINIGUI_SRC}/Widgets/CadPreview.cpp"
    "${MINIGUI_SRC}/Widgets/PropertyGrid.cpp"
    "${MINIGUI_SRC}/Widgets/ViewportHost.cpp"
    "${MINIGUI_SRC}/Widgets/CommandUI.cpp"
    "${MINIGUI_SRC}/Widgets/Binding.cpp"
    "${MINIGUI_SRC}/Widgets/UiLayout.cpp"
    "${MINIGUI_SRC}/Widgets/DockSpace.cpp"
    "${MINIGUI_SRC}/Widgets/TitleBar.cpp"
    "${MINIGUI_SRC}/Widgets/Ribbon.cpp"
    "${MINIGUI_SRC}/Widgets/CommandConsole.cpp"
)

# ── MiniGUI 核心库（与平台、图形 API 无关）─────────────────────────────
add_library(MiniGUI STATIC
    ${MINIGUI_CORE_SOURCES}
    ${MINIGUI_LAYOUT_SOURCES}
    ${MINIGUI_PAINT_SOURCES}
    ${MINIGUI_STYLE_SOURCES}
    ${MINIGUI_DATA_SOURCES}
    ${MINIGUI_TEXT_SOURCES}
    ${MINIGUI_WIDGETS_SOURCES}
)

target_include_directories(MiniGUI
    PUBLIC  ${MINIGUI_SRC}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rd     # stb_truetype / stb_image 只在 Font.cpp / Image.cpp 内部使用
)

if(MSVC)
    target_compile_options(MiniGUI PRIVATE /utf-8 /W4)
endif()

# ── Win32 平台层 ─────────────────────────────────────────────────────
if(WIN32)
    add_library(MiniGUI_Win32 STATIC
        "${MINIGUI_SRC}/Platform/Win32/Win32Input.cpp"
        "${MINIGUI_SRC}/Platform/Win32/Win32Fonts.cpp"
        "${MINIGUI_SRC}/Platform/Win32/Win32Clipboard.cpp"
        "${MINIGUI_SRC}/Platform/Win32/Win32Frame.cpp"
    )

    target_link_libraries(MiniGUI_Win32 PUBLIC MiniGUI)

    if(MSVC)
        target_compile_options(MiniGUI_Win32 PRIVATE /utf-8 /W4)
        target_compile_definitions(MiniGUI_Win32 PRIVATE
            UNICODE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
    endif()
endif()

# ── Web 平台层（Emscripten）─────────────────────────────────────────
if(EMSCRIPTEN)
    add_library(MiniGUI_Web STATIC
        "${MINIGUI_SRC}/Platform/Web/WebInput.cpp"
        "${MINIGUI_SRC}/Platform/Web/WebFonts.cpp"
    )

    target_link_libraries(MiniGUI_Web PUBLIC MiniGUI)
endif()

# ── SDL3 平台层（Linux）：输入、剪贴板、界面字体 ─────────────────────
# SDL3 用发行版的 libsdl3-dev，或自己编译安装后用 CMAKE_PREFIX_PATH 指定；找不到时不编译 Linux 宿主
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    find_package(SDL3 CONFIG QUIET)
    if(SDL3_FOUND)
        add_library(MiniGUI_SDL STATIC
            "${MINIGUI_SRC}/Platform/SDL/SDLInput.cpp"
            "${MINIGUI_SRC}/Platform/SDL/SDLFonts.cpp"
        )
        target_link_libraries(MiniGUI_SDL PUBLIC MiniGUI SDL3::SDL3)
    else()
        message(STATUS "没有找到 SDL3，不编译 MiniGUI_SDL 与 Linux 宿主（MiniCADLinux）")
    endif()
endif()

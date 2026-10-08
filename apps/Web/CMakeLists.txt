# ── MiniCAD 网页版（Emscripten）：界面由 MiniGUI 绘制，经 WebGL2 渲染 ──

# ── MiniGUI WebGL2 渲染后端（头文件以 src/Render 为根："WebGL2/WebGL2Backend.h"）──
add_library(MiniGUI_WebGL2 STATIC
    src/Render/WebGL2/WebGL2Backend.cpp
)
target_include_directories(MiniGUI_WebGL2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/Render)
target_link_libraries(MiniGUI_WebGL2 PUBLIC MiniGUI)

# ── 网页宿主：画布、WebGL2 上下文、尺寸与按需刷新 ─────────────────────
add_library(MiniCADWebHost STATIC
    src/Host/WebHost.cpp
)
target_include_directories(MiniCADWebHost PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(MINICAD_WEB_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/shell/minigui_shell.html")

# 网页程序共用的链接选项
function(minicad_web_executable target)
    target_link_options(${target} PRIVATE
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sALLOW_MEMORY_GROWTH=1
        -sEXPORTED_FUNCTIONS=_main,_malloc,_free
        -sENVIRONMENT=web
        "SHELL:--shell-file ${MINICAD_WEB_SHELL}"
    )
    set_target_properties(${target} PROPERTIES
        SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${target}"
        LINK_DEPENDS "${MINICAD_WEB_SHELL}"
    )
endfunction()

# ── MiniGUI 网页原型：控件展示（Gallery）────────────────────────────
add_executable(MiniGUIWebGallery
    gallery/GalleryMain.cpp
    "${CMAKE_SOURCE_DIR}/tests/UI/Golden/Demo/DemoNodes.cpp"
    "${CMAKE_SOURCE_DIR}/tests/UI/Golden/Demo/Gallery.cpp"
)
target_include_directories(MiniGUIWebGallery PRIVATE "${CMAKE_SOURCE_DIR}/tests/UI/Golden/Demo")
target_link_libraries(MiniGUIWebGallery PRIVATE MiniGUI MiniGUI_Web MiniGUI_WebGL2 MiniCADWebHost)
minicad_web_executable(MiniGUIWebGallery)
target_link_options(MiniGUIWebGallery PRIVATE
    # 同一个选项出现多次时 CMake 会去重，用 SHELL: 保持成对
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/fonts/NotoSansSC-UI.ttf@/fonts/NotoSansSC-UI.ttf"
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/tests/Data/UI@/assets"
)

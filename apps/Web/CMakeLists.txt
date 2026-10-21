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
    # 给 .js / .wasm / .data 的地址加内容哈希参数，发版后浏览器不会混用新旧文件
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DHTML=$<TARGET_FILE:${target}> -P "${CMAKE_SOURCE_DIR}/cmake/WebCacheBust.cmake"
        VERBATIM
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

# ── MiniCAD 网页版：WebApp 宿主 + 共用的主窗口界面（MiniCADApp）──────────
add_executable(MiniCADWeb
    src/Host/WebMain.cpp
    src/Host/WebApp.cpp
    src/Render/GLES3/GLES3Renderer.cpp
    src/Render/GLES3/GLRenderTarget.cpp
)
target_include_directories(MiniCADWeb PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Render
)
target_link_libraries(MiniCADWeb PRIVATE MiniCADApp MiniCAD MiniGUI MiniGUI_Web MiniGUI_WebGL2 MiniCADWebHost)
# 三维实体内核（SMLib，见 docs/Solid/PLAN.md）：默认编入网页版，
# 配置 -DMINICAD_WITH_SOLID=OFF 可去掉（.wasm 小约 6 MB）
if(MINICAD_WITH_SOLID)
    target_link_libraries(MiniCADWeb PRIVATE MiniCADSolid)
    target_compile_definitions(MiniCADWeb PRIVATE MINICAD_WITH_SOLID)
    # SMLib 的数值算法递归较深，emcc 默认 64 KB 的栈在建模时会溢出（表现为 call_indirect 报错），给 8 MB
    target_link_options(MiniCADWeb PRIVATE -sSTACK_SIZE=8388608)
endif()
minicad_web_executable(MiniCADWeb)
# 资源打包到虚拟文件系统根目录：/fonts（CAD 字体 + 界面字体）、/icons、/patterns、/ui、/nodes（示例节点图）
target_link_options(MiniCADWeb PRIVATE
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/fonts@/fonts"
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/icons@/icons"
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/patterns@/patterns"
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/ui@/ui"
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/nodes@/nodes"
)
set_target_properties(MiniCADWeb PROPERTIES OUTPUT_NAME "index")

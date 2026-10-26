# ── MiniCAD Linux 版：SDL3 宿主（窗口、事件、EGL / GLES3 上下文）+ 共用的主窗口界面（MiniCADApp）──
# 渲染走 GLES3GraphicsDevice（cmake/Render.cmake 的 Linux 段）；平台层 MiniGUI_SDL 定义在 cmake/MiniGUI.cmake。
# 需要 SDL3 与 OpenGL ES 3 开发文件，缺任何一个时只编译核心库与测试（见 README 的 Linux 构建说明）

if(NOT TARGET MiniGUI_SDL OR NOT MINICAD_LINUX_GLES3)
    message(STATUS "MiniCADLinux：缺少 SDL3 或 OpenGL ES 3，跳过")
    return()
endif()

add_executable(MiniCADLinux
    src/Host/LinuxMain.cpp
    src/Host/LinuxApp.cpp
)

target_include_directories(MiniCADLinux PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(MiniCADLinux PRIVATE
    MiniCADApp
    MiniCAD
    MiniGUI_SDL
    MiniCADRender
    SDL3::SDL3
)

# 三维实体内核（SMLib，见 docs/Solid/PLAN.md）
if(MINICAD_WITH_SOLID)
    target_link_libraries(MiniCADLinux PRIVATE MiniCADSolid)
    target_compile_definitions(MiniCADLinux PRIVATE MINICAD_WITH_SOLID)
endif()

set_target_properties(MiniCADLinux PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/MiniCADLinux"
)

# 复制共享资产（字体、图标、填充图案、界面描述文件）到可执行文件旁，与桌面版相同
add_custom_command(TARGET MiniCADLinux POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:MiniCADLinux>"
    COMMENT "Copy shared assets for MiniCADLinux"
)

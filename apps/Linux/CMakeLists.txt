# ── MiniCAD Linux 版：X11 / Wayland 宿主（窗口、事件、Vulkan 表面或 EGL / GLES3 上下文）+ 共用的主窗口界面（MiniCADApp）──
# 渲染走 VulkanGraphicsDevice，不行时 GLES3GraphicsDevice（cmake/Render.cmake 的 Linux 段）；平台层 MiniGUI_Linux 定义在 cmake/MiniGUI.cmake。
# 需要 EGL / OpenGL ES 3，以及 X11（x11、xcursor）与 Wayland（wayland-client / egl / cursor、xkbcommon、wayland-protocols、wayland-scanner）
# 至少一套；有 dbus-1 时文件对话框走 xdg-desktop-portal，否则只用 zenity。缺这些时只编译核心库与测试（见 README 的 Linux 构建说明）

if(NOT TARGET MiniGUI_Linux OR NOT MINICAD_LINUX_GLES3)
    message(STATUS "MiniCADLinux：缺少 OpenGL ES 3，跳过")
    return()
endif()

find_package(PkgConfig QUIET)
find_path(MINICAD_EGL_INCLUDE_DIR EGL/egl.h)
find_library(MINICAD_EGL_LIBRARY EGL)
if(NOT PKG_CONFIG_FOUND OR NOT MINICAD_EGL_INCLUDE_DIR OR NOT MINICAD_EGL_LIBRARY)
    message(STATUS "MiniCADLinux：缺少 pkg-config 或 EGL，跳过")
    return()
endif()

pkg_check_modules(MINICAD_X11 QUIET IMPORTED_TARGET x11 xcursor)
pkg_check_modules(MINICAD_WAYLAND QUIET IMPORTED_TARGET wayland-client wayland-egl wayland-cursor xkbcommon)
pkg_check_modules(MINICAD_WAYLAND_PROTOCOLS QUIET wayland-protocols>=1.31)
pkg_check_modules(MINICAD_DBUS QUIET IMPORTED_TARGET dbus-1)
find_program(MINICAD_WAYLAND_SCANNER wayland-scanner)

set(MINICAD_LINUX_WAYLAND OFF)
if(MINICAD_WAYLAND_FOUND AND MINICAD_WAYLAND_PROTOCOLS_FOUND AND MINICAD_WAYLAND_SCANNER)
    set(MINICAD_LINUX_WAYLAND ON)
endif()
if(NOT MINICAD_X11_FOUND AND NOT MINICAD_LINUX_WAYLAND)
    message(STATUS "MiniCADLinux：没有 X11（libx11-dev、libxcursor-dev）也没有 Wayland（libwayland-dev、libxkbcommon-dev、wayland-protocols）开发文件，跳过")
    return()
endif()

add_executable(MiniCADLinux
    src/Host/LinuxMain.cpp
    src/Host/LinuxApp.cpp
    src/Host/LinuxFloatingWindow.cpp
    src/Host/WindowSystem.cpp
    src/Host/EglContext.cpp
    src/Host/FileDialog.cpp
)

target_include_directories(MiniCADLinux PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${MINICAD_EGL_INCLUDE_DIR})

target_link_libraries(MiniCADLinux PRIVATE
    MiniCADApp
    MiniCAD
    MiniGUI_Linux
    MiniCADRender
    ${MINICAD_EGL_LIBRARY}
)

if(MINICAD_X11_FOUND)
    target_sources(MiniCADLinux PRIVATE src/Host/X11Window.cpp)
    target_compile_definitions(MiniCADLinux PRIVATE MINICAD_LINUX_X11)
    target_link_libraries(MiniCADLinux PRIVATE PkgConfig::MINICAD_X11)
    # 有 Shape 扩展（libxext）时自绘标题栏的窗口切成圆角
    pkg_check_modules(MINICAD_XEXT QUIET IMPORTED_TARGET xext)
    if(MINICAD_XEXT_FOUND)
        target_compile_definitions(MiniCADLinux PRIVATE MINICAD_X11_SHAPE)
        target_link_libraries(MiniCADLinux PRIVATE PkgConfig::MINICAD_XEXT)
    endif()
else()
    message(STATUS "MiniCADLinux：没有 X11 开发文件，只支持 Wayland")
endif()

# Wayland 协议：构建时用 wayland-scanner 从 wayland-protocols 的 XML 生成客户端代码（generated/Wayland）
if(MINICAD_LINUX_WAYLAND)
    enable_language(C)
    pkg_get_variable(MINICAD_WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
    set(protocol_dir "${CMAKE_BINARY_DIR}/generated/Wayland")
    file(MAKE_DIRECTORY "${protocol_dir}")
    foreach(xml
        stable/xdg-shell/xdg-shell.xml
        stable/viewporter/viewporter.xml
        unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
        unstable/text-input/text-input-unstable-v3.xml
        staging/fractional-scale/fractional-scale-v1.xml)
        get_filename_component(name "${xml}" NAME_WE)
        set(src "${MINICAD_WAYLAND_PROTOCOLS_DIR}/${xml}")
        add_custom_command(
            OUTPUT  "${protocol_dir}/${name}-client-protocol.h" "${protocol_dir}/${name}-protocol.c"
            COMMAND ${MINICAD_WAYLAND_SCANNER} client-header "${src}" "${protocol_dir}/${name}-client-protocol.h"
            COMMAND ${MINICAD_WAYLAND_SCANNER} private-code  "${src}" "${protocol_dir}/${name}-protocol.c"
            DEPENDS "${src}"
            COMMENT "wayland-scanner：${name}"
            VERBATIM
        )
        target_sources(MiniCADLinux PRIVATE "${protocol_dir}/${name}-client-protocol.h" "${protocol_dir}/${name}-protocol.c")
    endforeach()
    target_sources(MiniCADLinux PRIVATE src/Host/WaylandWindow.cpp)
    target_include_directories(MiniCADLinux PRIVATE "${protocol_dir}")
    target_compile_definitions(MiniCADLinux PRIVATE MINICAD_LINUX_WAYLAND)
    target_link_libraries(MiniCADLinux PRIVATE PkgConfig::MINICAD_WAYLAND)
else()
    message(STATUS "MiniCADLinux：没有 Wayland 开发文件（或 wayland-protocols 低于 1.31），只支持 X11")
endif()

if(MINICAD_DBUS_FOUND)
    target_compile_definitions(MiniCADLinux PRIVATE MINICAD_LINUX_DBUS)
    target_link_libraries(MiniCADLinux PRIVATE PkgConfig::MINICAD_DBUS)
else()
    message(STATUS "MiniCADLinux：没有 dbus-1 开发文件，文件对话框只用 zenity")
endif()

if(MINICAD_LINUX_VULKAN)
    target_compile_definitions(MiniCADLinux PRIVATE MINICAD_LINUX_VULKAN)
    target_link_libraries(MiniCADLinux PRIVATE Vulkan::Vulkan)
endif()

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

cmake_minimum_required(VERSION 3.16)
project(MiniCAD)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_definitions("-DUNICODE" "-D_UNICODE")
    add_compile_options(/utf-8)
endif()

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(EMSCRIPTEN)
    # Force Debug-style build for browser debugging (no optimization, full DWARF).
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
    endif()

    # -fexceptions             : 启用 C++ 异常（throw 不变成 unreachable）
    # -g3                      : 最完整的 DWARF 调试信息（变量名、行号、类型）
    # -O0                      : 不做优化，行号/变量与源码 1:1
    add_compile_options(-fexceptions -g3 -O0)

    # -sASSERTIONS=2            : 运行时安全检查（含内存对齐、参数校验）
    # -sSAFE_HEAP=1             : 堆访问越界检查
    # -sSTACK_OVERFLOW_CHECK=2  : 栈溢出检测
    # -gseparate-dwarf          : 把 DWARF 输出到独立 .debug.wasm 文件（更快加载）
    add_link_options(
        -fexceptions
        -g3
        -O0
        -gseparate-dwarf=index.debug.wasm
        -sASSERTIONS=2
        -sSAFE_HEAP=1
        -sSTACK_OVERFLOW_CHECK=2
        -sNO_DISABLE_EXCEPTION_CATCHING
    )

    file(GLOB_RECURSE MINICAD_CORE_SRC CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/Core/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/Scene/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/Document/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/Editor/*.cpp"
    )

    list(FILTER MINICAD_CORE_SRC EXCLUDE REGEX ".*[\\\\/]Editor[\\\\/]Input[\\\\/]InputSystem\\.cpp$")
    list(FILTER MINICAD_CORE_SRC EXCLUDE REGEX ".*[\\\\/]Editor[\\\\/]Input[\\\\/]KeyCodeUtils\\.cpp$")
    list(FILTER MINICAD_CORE_SRC EXCLUDE REGEX ".*[\\\\/]Editor[\\\\/]Input[\\\\/]ViewportInputAdapter\\.cpp$")

    add_executable(MiniCADWeb
        ${MINICAD_CORE_SRC}
        src/App/WebMain.cpp
        src/Render/WebGL/WebGLRenderer.cpp
        src/Render/WebGL/WebGLRenderTarget.cpp
    )

    target_include_directories(MiniCADWeb PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/thirdParty"
    )

    target_compile_definitions(MiniCADWeb PRIVATE MINICAD_WEB)

    target_link_options(MiniCADWeb PRIVATE
        "-sUSE_WEBGL2=1"
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sSTACK_SIZE=5242880"
        "-sINITIAL_MEMORY=33554432"
        "-sWASM=1"
        "-sEXPORTED_FUNCTIONS=['_main','_MiniCAD_StartLine','_MiniCAD_StartPoint','_MiniCAD_StartCircle','_MiniCAD_StartRectangle','_MiniCAD_StartArc','_MiniCAD_StartEllipse','_MiniCAD_StartPolyline','_MiniCAD_StartSpline','_MiniCAD_StartMove','_MiniCAD_StartCopy','_MiniCAD_StartMirror','_MiniCAD_StartRotate','_MiniCAD_Delete','_MiniCAD_ToggleSnap','_MiniCAD_ToggleOrtho','_MiniCAD_Undo','_MiniCAD_Redo','_MiniCAD_Cancel','_MiniCAD_StartText','_MiniCAD_SubmitText','_MiniCAD_CancelText']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
        "--shell-file=${CMAKE_SOURCE_DIR}/web/index.html"
    )

    set_target_properties(MiniCADWeb PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/MiniCADWeb"
        OUTPUT_NAME "index"
        SUFFIX ".html"
    )

    add_custom_command(TARGET MiniCADWeb POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets/icons"
                "${CMAKE_BINARY_DIR}/MiniCADWeb/icons"
        COMMENT "Copy icons for MiniCADWeb"
    )
else()
    add_library(Imgui
        thirdParty/imgui/imgui.cpp
        thirdParty/imgui/imgui_draw.cpp
        thirdParty/imgui/imgui_widgets.cpp
        thirdParty/imgui/imgui_tables.cpp
        thirdParty/imgui/imgui_demo.cpp
        thirdParty/imgui/backends/imgui_impl_win32.cpp
        thirdParty/imgui/backends/imgui_impl_dx11.cpp
    )

    target_include_directories(Imgui PUBLIC
        thirdParty/imgui
        thirdParty/imgui/backends
    )

    file(GLOB_RECURSE SRC
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/assets/MiniCAD.rc"
    )

    list(FILTER SRC EXCLUDE REGEX ".*[\\\\/]src[\\\\/]App[\\\\/]WebMain\\.cpp$")
    list(FILTER SRC EXCLUDE REGEX ".*[\\\\/]src[\\\\/]Render[\\\\/]WebGL[\\\\/].*")

    option(USE_WIN32 "Use Win32 GUI subsystem" OFF)

    if(USE_WIN32)
        add_executable(MiniCAD WIN32 ${SRC})
        target_compile_definitions(MiniCAD PRIVATE USE_WIN32)
    else()
        add_executable(MiniCAD ${SRC})
    endif()

    target_include_directories(MiniCAD PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/thirdParty"
    )

    target_link_libraries(MiniCAD PRIVATE Imgui d3d11 dxgi D3DCompiler)

    set_target_properties(MiniCAD PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/MiniCAD"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/MiniCAD"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/MiniCAD"
        VS_DEBUGGER_WORKING_DIRECTORY    "${CMAKE_BINARY_DIR}/MiniCAD"
    )

    if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
        add_custom_command(TARGET MiniCAD POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_SOURCE_DIR}/assets"
                    "$<TARGET_FILE_DIR:MiniCAD>"
            COMMENT "Copy assets contents for MiniCAD"
        )
    endif()
endif()

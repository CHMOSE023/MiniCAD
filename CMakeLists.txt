cmake_minimum_required(VERSION 3.16)
project(LearnMiniCAD) 

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_definitions("-DUNICODE" "-D_UNICODE")                                   # Windows API 使用宽字符版本
    add_compile_options(/utf-8)                                                 # 源文件按 UTF-8 解析
endif()
    
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

file(GLOB_RECURSE SRC  "${CMAKE_SOURCE_DIR}/src/*.cpp")   

add_executable(MiniCAD  ${SRC} ) 

target_include_directories(MiniCAD PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/thirdParty"
)

target_link_libraries(MiniCAD  PRIVATE Imgui d3d11 dxgi D3DCompiler) 

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

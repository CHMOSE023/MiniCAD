cmake_minimum_required(VERSION 3.16)
project(MiniCAD) 
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
 
if(MSVC)
    add_definitions("-DUNICODE" "-D_UNICODE")  # Windows API 使用宽字符版本
    add_compile_options(/utf-8)                # 源文件按 UTF-8 解析
endif()
# include 路径
include_directories("${CMAKE_SOURCE_DIR}/src")

find_package(directxtk CONFIG REQUIRED)

file(GLOB_RECURSE SRC  "${CMAKE_SOURCE_DIR}/src/*.cpp"  
                       "${CMAKE_SOURCE_DIR}/src/*.hpp")    
file(GLOB_RECURSE ASSETS "${CMAKE_SOURCE_DIR}/src/*.hlsl") 

add_executable(MiniCAD  ${SRC} )
target_link_libraries(MiniCAD  PRIVATE  Microsoft::DirectXTK d3d11 dxgi D3DCompiler) 

# 输出目录
set_target_properties(MiniCAD PROPERTIES  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/MiniCAD")

add_custom_command(TARGET MiniCAD  POST_BUILD           
            COMMAND ${CMAKE_COMMAND} -E copy_if_different  ${ASSETS}  "$<TARGET_FILE_DIR:MiniCAD>"
            COMMENT "Copy assets for MiniCAD"
) 
 


 
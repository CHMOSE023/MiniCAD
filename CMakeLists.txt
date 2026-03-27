cmake_minimum_required(VERSION 3.16)
project(MiniCAD) 
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
 
if(MSVC)
    add_definitions("-DUNICODE" "-D_UNICODE")  # Windows API 使用宽字符版本
    add_compile_options(/utf-8)                # 源文件按 UTF-8 解析
endif()

find_package(directxtk CONFIG REQUIRED)

file(GLOB_RECURSE SRC "${CMAKE_SOURCE_DIR}/src/*.cpp")    
 
add_executable(MiniCAD  ${SRC} )
 
target_link_libraries(MiniCAD  PRIVATE  Microsoft::DirectXTK d3d11 dxgi) 

 

 
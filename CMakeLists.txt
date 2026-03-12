cmake_minimum_required(VERSION 3.16)
project(TwinEngine)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


if(MSVC)
    add_definitions("-DUNICODE" "-D_UNICODE")  # Windows API 使用宽字符版本
    add_compile_options(/utf-8)                # 源文件按 UTF-8 解析
endif()

# ── include 路径 ──────────────────────────────
include_directories("${CMAKE_SOURCE_DIR}/src")

# ── 只收集 src/ 下的 .cpp，绝不扫到 out/ ──────
file(GLOB_RECURSE APP_SRC "${CMAKE_SOURCE_DIR}/src/app/*.cpp")
file(GLOB_RECURSE CORE_SRC "${CMAKE_SOURCE_DIR}/src/core/*.cpp")
file(GLOB_RECURSE DOC_SRC  "${CMAKE_SOURCE_DIR}/src/doc/*.cpp")
  
set(MINICAD_CORE_SOURCES
    ${APP_SRC}
    ${CORE_SRC} 
    ${DOC_SRC}
)

# 排除 Main.cpp，避免 UnitTests 出现重复 main 符号
list(REMOVE_ITEM MINICAD_CORE_SOURCES
    "${CMAKE_SOURCE_DIR}/src/app/Main.cpp"
)

# ── 主程序 ────────────────────────────────────
add_executable(MiniCAD
    ${MINICAD_CORE_SOURCES}
    "${CMAKE_SOURCE_DIR}/src/app/Main.cpp"
)

# ── 单元测试 ──────────────────────────────────
find_package(GTest CONFIG REQUIRED)
file(GLOB TEST_SOURCES "${CMAKE_SOURCE_DIR}/tests/*.cpp")
 
add_executable(UnitTests
    ${TEST_SOURCES}
    ${MINICAD_CORE_SOURCES}
)
target_link_libraries(UnitTests PRIVATE GTest::gtest GTest::gtest_main)

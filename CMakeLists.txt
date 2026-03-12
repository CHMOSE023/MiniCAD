cmake_minimum_required(VERSION 3.16)

include_directories( 
    "${CMAKE_CURRENT_SOURCE_DIR}/src" 
)
 
find_package(GTest CONFIG REQUIRED)

file(GLOB TEST_SOURCES tests/*.cpp)

#单元测试
add_executable(UnitTests ${TEST_SOURCES})

target_link_libraries(UnitTests  PRIVATE  GTest::gtest  GTest::gtest_main )
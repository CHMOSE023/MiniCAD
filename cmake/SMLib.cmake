# ── SMLib（NURBS BRep 实体建模内核，源码在 3rd/SMLib）────────────────────
# 来自 NVIDIA usd-brep 1.0.0（Apache-2.0），只含内核 SMLib 与高层接口 SM_API，说明与本地修改见 3rd/SMLib/README.md。
# 编成静态库并定义 SM_STATIC：不导入 / 导出符号，也不依赖 DllMain 初始化线程局部存储。
# SMLib 的头文件带有 ULONG、TRUE 等宏，只应在适配层的 .cpp 里包含，不要出现在 MiniCAD 的公开头文件里。
 
# SMLib Web版 实体功能 -DMINICAD_WITH_SOLID=OFF
option(MINICAD_WITH_SOLID "Build the SMLib solid modeling kernel" ON)

set(SMLIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/3rd/SMLib")

# 第三方源码整目录编译，用通配收集（MiniCAD 自己的源文件仍逐个列出）。
# SmDllMain.cpp 只用于 DLL 构建；StdAfx.cpp 由 target_precompile_headers 取代
file(GLOB SMLIB_SOURCES CONFIGURE_DEPENDS "${SMLIB_ROOT}/SMLib/src/*.cpp")
file(GLOB SMAPI_SOURCES CONFIGURE_DEPENDS "${SMLIB_ROOT}/SM_API/src/*.cpp")
list(FILTER SMLIB_SOURCES EXCLUDE REGEX "/(SmDllMain|StdAfx)[.]cpp$")
list(FILTER SMAPI_SOURCES EXCLUDE REGEX "/StdAfx[.]cpp$")

function(minicad_smlib_options target)
    # 与 MiniCAD 一样按 C++20 编译（上游为 C++17，适配所做的修改见 3rd/SMLib/README.md）；沿用全局的 /MT 运行库
    target_compile_definitions(${target} PUBLIC SM_STATIC NOMINMAX PRIVATE _SCL_SECURE_NO_WARNINGS _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
        # 第三方代码只保留最严重的警告；/bigobj：SmBrep.cpp 等单个文件很大
        target_compile_options(${target} PRIVATE /utf-8 /W1 /bigobj /EHsc)
    else()
        # Clang（网页版的 Emscripten 也是）：关掉上游代码里大量出现、不影响正确性的两类警告（GCC 没有这两类）
        target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-inconsistent-missing-override -Wno-parentheses-equality>)
    endif()
endfunction()

if(MINICAD_WITH_SOLID)
    add_library(SMLib STATIC ${SMLIB_SOURCES})
    # SYSTEM：引用它的适配层（src/Solid/SMLib）不报 SMLib 头文件里的警告
    target_include_directories(SMLib SYSTEM PUBLIC "${SMLIB_ROOT}/SMLib/inc")
    target_precompile_headers(SMLib PRIVATE "${SMLIB_ROOT}/SMLib/inc/StdAfx.h")
    minicad_smlib_options(SMLib)

    add_library(SM_API STATIC ${SMAPI_SOURCES})
    # SM_API/inc 必须排在 SMLib/inc 前面：两边都有 StdAfx.h
    target_include_directories(SM_API SYSTEM PUBLIC "${SMLIB_ROOT}/SM_API/inc")
    target_link_libraries(SM_API PUBLIC SMLib)
    target_precompile_headers(SM_API PRIVATE "${SMLIB_ROOT}/SM_API/inc/StdAfx.h")
    minicad_smlib_options(SM_API)
endif()

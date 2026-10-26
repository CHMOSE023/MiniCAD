# ── 渲染后端（源码在 apps/Render，头文件以它为根："D3D11/D3D11Backend.h"、"IGraphicsDevice.h"）──
# 每个平台只编译自己的图形接口（见 docs/Render/PLAN.md）：
#   Windows → D3D12 + D3D11    网页（Emscripten）→ WebGPU（浏览器支持时）+ GLES3（WebGL2，回退）
# 界面后端（MiniGUI::IRenderBackend）各是一个小静态库，截图对比测试和控件展示也单独链接它们；
# MiniCADRender = CAD 视口渲染器 + 宿主用的图形设备（IGraphicsDevice），按平台选源文件。

set(MINICAD_RENDER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/apps/Render")

# ── 界面后端 ─────────────────────────────────────────────────────────
if(WIN32 AND NOT EMSCRIPTEN)
    add_library(MiniGUI_D3D11 STATIC
        "${MINICAD_RENDER_SRC}/D3D11/D3D11Backend.cpp"
    )
    target_include_directories(MiniGUI_D3D11 PUBLIC ${MINICAD_RENDER_SRC})
    target_link_libraries(MiniGUI_D3D11
        PUBLIC  MiniGUI
        PRIVATE d3d11 D3DCompiler
    )
    if(MSVC)
        target_compile_options(MiniGUI_D3D11 PRIVATE /utf-8 /W4)
        target_compile_definitions(MiniGUI_D3D11 PRIVATE
            UNICODE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
    endif()
endif()

# D3D12 界面后端；D3D12Context（设备、命令列表、上传缓冲、描述符）也在这里，CAD 视口渲染器共用它
if(WIN32 AND NOT EMSCRIPTEN)
    add_library(MiniGUI_D3D12 STATIC
        "${MINICAD_RENDER_SRC}/D3D12/D3D12Backend.cpp"
        "${MINICAD_RENDER_SRC}/D3D12/D3D12Context.cpp"
    )
    target_include_directories(MiniGUI_D3D12 PUBLIC ${MINICAD_RENDER_SRC})
    target_link_libraries(MiniGUI_D3D12
        PUBLIC  MiniGUI d3d12 dxgi
        PRIVATE D3DCompiler
    )
    if(MSVC)
        target_compile_options(MiniGUI_D3D12 PRIVATE /utf-8 /W4)
        target_compile_definitions(MiniGUI_D3D12 PRIVATE
            UNICODE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
    endif()
endif()

if(EMSCRIPTEN)
    add_library(MiniGUI_GLES3 STATIC
        "${MINICAD_RENDER_SRC}/GLES3/GLES3Backend.cpp"
    )
    target_include_directories(MiniGUI_GLES3 PUBLIC ${MINICAD_RENDER_SRC})
    target_link_libraries(MiniGUI_GLES3 PUBLIC MiniGUI)

    # WebGPU：浏览器自带的接口，经 Emscripten 自带的 emdawnwebgpu 绑定（标准 webgpu.h，不引入 Dawn 库）。
    # --use-port 编译和链接都要加：编译时提供头文件，链接时加入 JS 实现与 webgpu.cpp；
    # 第一次使用时 Emscripten 从 GitHub 下载固定版本的端口包到 emsdk 的缓存目录
    set(MINICAD_EMDAWNWEBGPU "--use-port=emdawnwebgpu:cpp_bindings=false")
    add_library(MiniGUI_WebGPU STATIC
        "${MINICAD_RENDER_SRC}/WebGPU/WebGPUBackend.cpp"
    )
    target_include_directories(MiniGUI_WebGPU PUBLIC ${MINICAD_RENDER_SRC})
    target_link_libraries(MiniGUI_WebGPU PUBLIC MiniGUI)
    target_compile_options(MiniGUI_WebGPU PUBLIC ${MINICAD_EMDAWNWEBGPU})
    target_link_options(MiniGUI_WebGPU PUBLIC ${MINICAD_EMDAWNWEBGPU})
endif()

# 软件光栅后端（与平台无关，用于截图对比测试）
if(NOT EMSCRIPTEN)
    add_library(MiniGUI_Software STATIC
        "${MINICAD_RENDER_SRC}/Software/SoftwareBackend.cpp"
    )
    target_include_directories(MiniGUI_Software PUBLIC ${MINICAD_RENDER_SRC})
    target_link_libraries(MiniGUI_Software PUBLIC MiniGUI)
    if(MSVC)
        target_compile_options(MiniGUI_Software PRIVATE /utf-8 /W4)
    endif()
endif()

# ── CAD 视口渲染器 + 图形设备 ─────────────────────────────────────────
add_library(MiniCADRender STATIC
    "${MINICAD_RENDER_SRC}/GraphicsDevice.cpp"
)
target_include_directories(MiniCADRender PUBLIC ${MINICAD_RENDER_SRC})
target_link_libraries(MiniCADRender PUBLIC MiniCAD MiniGUI)

if(WIN32 AND NOT EMSCRIPTEN)
    target_sources(MiniCADRender PRIVATE
        "${MINICAD_RENDER_SRC}/D3D11/D3D11GraphicsDevice.cpp"
        "${MINICAD_RENDER_SRC}/D3D11/D3D11Renderer.cpp"
        "${MINICAD_RENDER_SRC}/D3D11/D3D11RenderTarget.cpp"
        "${MINICAD_RENDER_SRC}/D3D11/Device.cpp"
        "${MINICAD_RENDER_SRC}/D3D11/Shader.cpp"
        "${MINICAD_RENDER_SRC}/D3D11/SwapChain.cpp"
    )
    target_compile_definitions(MiniCADRender PRIVATE MINICAD_RENDER_D3D11)
    target_link_libraries(MiniCADRender PRIVATE MiniGUI_D3D11 d3d11 dxgi D3DCompiler)

    target_sources(MiniCADRender PRIVATE
        "${MINICAD_RENDER_SRC}/D3D12/D3D12GraphicsDevice.cpp"
        "${MINICAD_RENDER_SRC}/D3D12/D3D12Renderer.cpp"
        "${MINICAD_RENDER_SRC}/D3D12/D3D12RenderTarget.cpp"
    )
    target_compile_definitions(MiniCADRender PRIVATE MINICAD_RENDER_D3D12)
    target_link_libraries(MiniCADRender PRIVATE MiniGUI_D3D12 d3d12)
endif()

if(EMSCRIPTEN)
    # 两个都编：先异步试 WebGPU，不支持或网址带 ?webgl 时用 WebGL2（宿主 WebApp 决定）
    target_sources(MiniCADRender PRIVATE
        "${MINICAD_RENDER_SRC}/GLES3/GLES3GraphicsDevice.cpp"
        "${MINICAD_RENDER_SRC}/GLES3/GLES3Renderer.cpp"
        "${MINICAD_RENDER_SRC}/GLES3/GLRenderTarget.cpp"
        "${MINICAD_RENDER_SRC}/WebGPU/WebGPUGraphicsDevice.cpp"
        "${MINICAD_RENDER_SRC}/WebGPU/WebGPURenderer.cpp"
        "${MINICAD_RENDER_SRC}/WebGPU/WebGPURenderTarget.cpp"
    )
    target_compile_definitions(MiniCADRender PRIVATE MINICAD_RENDER_GLES3 MINICAD_RENDER_WEBGPU)
    target_link_libraries(MiniCADRender PRIVATE MiniGUI_GLES3 MiniGUI_WebGPU)
endif()

if(MSVC)
    target_compile_options(MiniCADRender PRIVATE
        /utf-8
        /Z7                  # 调试信息嵌入 .obj，避免共享 PDB 锁定问题
    )
    target_compile_definitions(MiniCADRender PRIVATE
        UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

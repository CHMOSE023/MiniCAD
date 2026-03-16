// ============================================================
// MiniCAD — render/D3D11/ShaderManager.cpp
// 职责：Shader 编译 / 缓存 / 绑定 + 动态顶点缓冲管理
// 依赖：render/D3D11/ShaderManager.h
// 约束：D3D11 API 严禁泄漏到头文件
// ============================================================

#include "render/D3D11/ShaderManager.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#include <cassert>
#include <cstring>
#include <cstdint>

namespace MiniCAD {

    // HLSL Shader 路径约定（相对于工作目录）
    static constexpr const char* PATH_LINE_HLSL      = "render/D3D11/shaders/Line.hlsl";
    static constexpr const char* PATH_FILL_HLSL      = "render/D3D11/shaders/Fill.hlsl";
    static constexpr const char* PATH_HIGHLIGHT_HLSL = "render/D3D11/shaders/Highlight.hlsl";

    // 初始动态顶点缓冲大小（字节），按需扩容
    static constexpr uint32_t INITIAL_VB_SIZE = 1024 * 1024;   // 1 MB

    // ============================================================
    // 顶点输入布局描述（Position + Color，与 GpuVertex 对齐）
    // ============================================================
    static const D3D11_INPUT_ELEMENT_DESC k_inputDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    // ============================================================
    // ShaderManager
    // ============================================================

    ShaderManager& ShaderManager::Instance() {
        static ShaderManager s_instance;
        return s_instance;
    }

    void ShaderManager::Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
        assert(!m_initialized);
        assert(device && context);
        (void)context;

        CompileShader(device, PATH_LINE_HLSL, "VS", "PS", &m_lineVS, &m_linePS, &m_lineLayout);

        CompileShader(device, PATH_FILL_HLSL, "VS", "PS", &m_fillVS, &m_fillPS, &m_fillLayout);

        CompileShader(device, PATH_HIGHLIGHT_HLSL, "VS", "PS", &m_hlVS, &m_hlPS, &m_hlLayout);

        EnsureDynamicBuffer(device, INITIAL_VB_SIZE);

        m_initialized = true;
    }

    void ShaderManager::Shutdown() {
        if (!m_initialized) return;

        auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };

        SafeRelease(m_lineVS);     SafeRelease(m_linePS);     SafeRelease(m_lineLayout);
        SafeRelease(m_fillVS);     SafeRelease(m_fillPS);     SafeRelease(m_fillLayout);
        SafeRelease(m_hlVS);       SafeRelease(m_hlPS);       SafeRelease(m_hlLayout);
        SafeRelease(m_dynamicVB);

        m_dynamicVBSize = 0;
        m_initialized = false;
    }

    // ============================================================
    // Shader 绑定
    // ============================================================

    void ShaderManager::BindLineShader(ID3D11DeviceContext* context) {
        context->VSSetShader(m_lineVS, nullptr, 0);
        context->PSSetShader(m_linePS, nullptr, 0);
        context->IASetInputLayout(m_lineLayout);
    }

    void ShaderManager::BindFillShader(ID3D11DeviceContext* context) {
        context->VSSetShader(m_fillVS, nullptr, 0);
        context->PSSetShader(m_fillPS, nullptr, 0);
        context->IASetInputLayout(m_fillLayout);
    }

    void ShaderManager::BindHighlightShader(ID3D11DeviceContext* context) {
        context->VSSetShader(m_hlVS, nullptr, 0);
        context->PSSetShader(m_hlPS, nullptr, 0);
        context->IASetInputLayout(m_hlLayout);
    }

    // ============================================================
    // 顶点上传 & 绘制
    // ============================================================

    void ShaderManager::UploadAndDraw(ID3D11Device* device,
        ID3D11DeviceContext* context,
        const void* vertexData,
        uint32_t            vertexCount,
        uint32_t            vertexStride,
        unsigned int        topology) {
        const uint32_t required = vertexCount * vertexStride;
        EnsureDynamicBuffer(device, required);

        // Map → 写入 → Unmap
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = context->Map(m_dynamicVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) return;

        memcpy(mapped.pData, vertexData, required);
        context->Unmap(m_dynamicVB, 0);

        // 绑定顶点缓冲
        UINT offset = 0;
        context->IASetVertexBuffers(0, 1, &m_dynamicVB, &vertexStride, &offset);
        context->IASetPrimitiveTopology(
            static_cast<D3D11_PRIMITIVE_TOPOLOGY>(topology));

        context->Draw(vertexCount, 0);
    }

    // ============================================================
    // 动态顶点缓冲管理
    // ============================================================

    void ShaderManager::EnsureDynamicBuffer(ID3D11Device* device,
        uint32_t      requiredBytes) {
        if (m_dynamicVB && m_dynamicVBSize >= requiredBytes) return;

        // 扩容策略：至少翻倍
        uint32_t newSize = m_dynamicVBSize ? m_dynamicVBSize * 2 : INITIAL_VB_SIZE;
        while (newSize < requiredBytes) newSize *= 2;

        if (m_dynamicVB) {
            m_dynamicVB->Release();
            m_dynamicVB = nullptr;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = newSize;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = device->CreateBuffer(&desc, nullptr, &m_dynamicVB);
        assert(SUCCEEDED(hr) && "ShaderManager: failed to create dynamic vertex buffer");
        if (SUCCEEDED(hr)) m_dynamicVBSize = newSize;
    }

    // ============================================================
    // Shader 编译
    // ============================================================

    bool ShaderManager::CompileShader(ID3D11Device* device,
        const std::string& srcPath,
        const std::string& entryVS,
        const std::string& entryPS,
        ID3D11VertexShader** outVS,
        ID3D11PixelShader** outPS,
        ID3D11InputLayout** outLayout) {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        auto wSrcPath = std::wstring(srcPath.begin(), srcPath.end());

        // 编译 VS
        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* errBlob = nullptr;
        HRESULT hr = D3DCompileFromFile(wSrcPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryVS.c_str(), "vs_5_0", flags, 0, &vsBlob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) errBlob->Release();
            return false;
        }

        hr = device->CreateVertexShader(vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(), nullptr, outVS);
        if (FAILED(hr)) { vsBlob->Release(); return false; }

        // 创建输入布局（VS 字节码提供反射信息）
        hr = device->CreateInputLayout(k_inputDesc, 2,
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(), outLayout);
        vsBlob->Release();
        if (FAILED(hr)) return false;

        // 编译 PS
        ID3DBlob* psBlob = nullptr;
        hr = D3DCompileFromFile(wSrcPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entryPS.c_str(), "ps_5_0", flags, 0, &psBlob, &errBlob);
        if (FAILED(hr)) {
            if (errBlob) errBlob->Release();
            return false;
        }

        hr = device->CreatePixelShader(psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(), nullptr, outPS);
        psBlob->Release();

        return SUCCEEDED(hr);
    }

    // ============================================================
    // Debug 热重载
    // ============================================================
#ifdef _DEBUG
    void ShaderManager::HotReload(ID3D11Device* device, ID3D11DeviceContext* context) {
        (void)context;
        Shutdown();
        // 重新编译所有 Shader（保留已有 VB 容量）
        CompileShader(device, PATH_LINE_HLSL, "VS", "PS",  &m_lineVS, &m_linePS, &m_lineLayout);
        CompileShader(device, PATH_FILL_HLSL, "VS", "PS",  &m_fillVS, &m_fillPS, &m_fillLayout);
        CompileShader(device, PATH_HIGHLIGHT_HLSL, "VS", "PS",  &m_hlVS, &m_hlPS, &m_hlLayout);
        EnsureDynamicBuffer(device, INITIAL_VB_SIZE);
        m_initialized = true;
    }
#endif

} // namespace MiniCAD

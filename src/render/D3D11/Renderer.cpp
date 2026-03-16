// ============================================================
// MiniCAD — render/D3D11/Renderer.cpp
// d3d11.h 只在此文件出现
// ============================================================
#include "render/D3D11/Renderer.h"
#include "render/D3D11/RenderState.h"
#include "render/D3D11/DrawPrimitives.h"
#include "render/D3D11/ShaderManager.h"

#include <d3d11.h>
#include <dxgi.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include <cassert>
#include <cstring>

namespace MiniCAD {

    struct D3D11Renderer::D3D11Impl {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        IDXGISwapChain* swapChain = nullptr;
        ID3D11RenderTargetView* renderTargetView = nullptr;
        StateCache              stateCache;

        void Release() {
            if (renderTargetView) { renderTargetView->Release(); renderTargetView = nullptr; }
            if (swapChain) { swapChain->Release();        swapChain = nullptr; }
            if (context) { context->Release();          context = nullptr; }
            if (device) { device->Release();           device = nullptr; }
        }
    };

    D3D11Renderer::D3D11Renderer() : m_impl(new D3D11Impl{}) {}
    D3D11Renderer::~D3D11Renderer() { Shutdown(); delete m_impl; m_impl = nullptr; }

    bool D3D11Renderer::Initialize(void* hwnd, int width, int height) {
        assert(!m_initialized && "D3D11Renderer::Initialize called twice");

        DXGI_SWAP_CHAIN_DESC scDesc = {};
        scDesc.BufferCount = 1;
        scDesc.BufferDesc.Width = static_cast<UINT>(width);
        scDesc.BufferDesc.Height = static_cast<UINT>(height);
        scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.BufferDesc.RefreshRate.Numerator = 60;
        scDesc.BufferDesc.RefreshRate.Denominator = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.OutputWindow = static_cast<HWND>(hwnd);
        scDesc.SampleDesc.Count = 1;
        scDesc.SampleDesc.Quality = 0;
        scDesc.Windowed = TRUE;

        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
        D3D_FEATURE_LEVEL outFeature = {};
        UINT createFlags = 0;
#ifdef _DEBUG
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
            &featureLevel, 1, D3D11_SDK_VERSION,
            &scDesc, &m_impl->swapChain,
            &m_impl->device, &outFeature, &m_impl->context);
        if (FAILED(hr)) return false;

        ID3D11Texture2D* backBuffer = nullptr;
        hr = m_impl->swapChain->GetBuffer(
            0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
        if (FAILED(hr)) { m_impl->Release(); return false; }

        hr = m_impl->device->CreateRenderTargetView(
            backBuffer, nullptr, &m_impl->renderTargetView);
        backBuffer->Release();
        if (FAILED(hr)) { m_impl->Release(); return false; }

        m_impl->context->OMSetRenderTargets(1, &m_impl->renderTargetView, nullptr);

        D3D11_VIEWPORT vp = {};
        vp.Width = static_cast<float>(width);
        vp.Height = static_cast<float>(height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_impl->context->RSSetViewports(1, &vp);

        ShaderManager::Instance().Initialize(m_impl->device, m_impl->context);

        m_width = width; m_height = height; m_initialized = true;
        return true;
    }

    void D3D11Renderer::Shutdown() {
        if (!m_initialized) return;
        ShaderManager::Instance().Shutdown();
        m_impl->Release();
        m_initialized = false;
    }

    void D3D11Renderer::BeginFrame(const Vec4& clearColor) {
        assert(m_initialized);
        float color[4] = {
            static_cast<float>(clearColor.x), static_cast<float>(clearColor.y),
            static_cast<float>(clearColor.z), static_cast<float>(clearColor.w)
        };
        m_impl->context->ClearRenderTargetView(m_impl->renderTargetView, color);
        m_impl->stateCache.Reset();
    }

    void D3D11Renderer::EndFrame() {
        assert(m_initialized);
        m_impl->swapChain->Present(1, 0);
    }

    void D3D11Renderer::Submit(const RenderItem& item) {
        assert(m_initialized);
        if (!item.state.layerVisible || item.vertices.empty()) return;
        DrawItem(item);
    }

    void D3D11Renderer::DrawItem(const RenderItem& item) {
        bool stateChanged = m_impl->stateCache.Apply(item.state);
        (void)stateChanged;
        DrawPrimitives::Draw(m_impl->device, m_impl->context,
            item.vertices, item.topology, item.state);
    }

    void D3D11Renderer::Resize(int width, int height) {
        if (!m_initialized || (width == m_width && height == m_height)) return;

        m_impl->context->OMSetRenderTargets(0, nullptr, nullptr);
        if (m_impl->renderTargetView) {
            m_impl->renderTargetView->Release();
            m_impl->renderTargetView = nullptr;
        }

        HRESULT hr = m_impl->swapChain->ResizeBuffers(
            0, static_cast<UINT>(width), static_cast<UINT>(height),
            DXGI_FORMAT_UNKNOWN, 0);

        if (SUCCEEDED(hr)) {
            ID3D11Texture2D* backBuffer = nullptr;
            hr = m_impl->swapChain->GetBuffer(
                0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
            if (SUCCEEDED(hr)) {
                m_impl->device->CreateRenderTargetView(
                    backBuffer, nullptr, &m_impl->renderTargetView);
                backBuffer->Release();
            }
        }

        m_impl->context->OMSetRenderTargets(1, &m_impl->renderTargetView, nullptr);

        D3D11_VIEWPORT vp = {};
        vp.Width = static_cast<float>(width); vp.Height = static_cast<float>(height);
        vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
        m_impl->context->RSSetViewports(1, &vp);

        m_width = width; m_height = height;
    }

    // ★ 修复：返回 void*，d3d11.h 类型只在 .cpp 内部使用
    void* D3D11Renderer::GetDevice() const {
        return m_impl ? m_impl->device : nullptr;
    }

    void* D3D11Renderer::GetDeviceContext() const {
        return m_impl ? m_impl->context : nullptr;
    }

} // namespace MiniCAD
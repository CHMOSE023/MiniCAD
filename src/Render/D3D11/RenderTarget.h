#pragma once 
#include "pch.h"

namespace MiniCAD
{
    class RenderTarget
    {
    public:
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
        D3D11_VIEWPORT viewport{};

    public:
        void Bind(ID3D11DeviceContext* ctx) const
        {
            ID3D11RenderTargetView* rt = rtv.Get();
            ID3D11DepthStencilView* ds = dsv.Get();

            ctx->OMSetRenderTargets(1, &rt, ds);
            ctx->RSSetViewports(1, &viewport);
        }

        void Clear(ID3D11DeviceContext* ctx, const float color[4]) const
        {
            ctx->ClearRenderTargetView(rtv.Get(), color);

            if (dsv)
            {
                ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
            }
        }
    };
}

#include "Shader.h"
#include <d3dcompiler.h>
#include <format>
#include <wrl/client.h>
#include <d3dcommon.h>
#include <d3d11.h>
#include "Core/Log.h"
namespace MiniCAD
{
    ShaderProgram CreateShader(ID3D11Device* device, const char* hlsl)
    {
        ShaderProgram sp;

        ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

        auto compile = [&](const char* code, const char* entry, const char* target, ID3DBlob** outBlob)
            {
                HRESULT hr = D3DCompile(code, strlen(code), nullptr, nullptr, nullptr, entry, target, 0, 0, outBlob, errorBlob.GetAddressOf());

                if (FAILED(hr))
                {
                    const char* msg = errorBlob ? (const char*)errorBlob->GetBufferPointer() : "unknown error"; 
                    LOG_ERROR("Shader compile failed (%s -> %s): %s", entry, target, msg);
                    return false;
                }

                return true;
            };

        if (!compile(hlsl, "VSMain", "vs_5_0", vsBlob.GetAddressOf()))
            return {};

        if (!compile(hlsl, "PSMain", "ps_5_0", psBlob.GetAddressOf()))
            return {};

        device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, sp.vs.GetAddressOf());
        device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, sp.ps.GetAddressOf());

        sp.vsBlob = vsBlob;
        return sp;
    }

    ComPtr<ID3D11InputLayout> CreateLayout(ID3D11Device* device, D3D11_INPUT_ELEMENT_DESC* desc, UINT count, ID3DBlob* vsBlob)
    {
        ComPtr<ID3D11InputLayout> layout;

        device->CreateInputLayout(desc, count, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), layout.GetAddressOf());

        return layout;
    }

}


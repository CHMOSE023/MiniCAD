#include <Windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <string>
#include <wrl/client.h>  

// 链接lib文件 
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "D3DCompiler.lib")
#pragma comment(lib, "winmm.lib")
using namespace DirectX;
class   DX11Context
{ 
public:
	int                     m_winWidth;
	int                     m_winHeight;
public:
	template <class T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	HWND                            m_hWnd;
	ComPtr<IDXGISwapChain>	        m_SwapChain;
	ComPtr<ID3D11Device>            m_d3d11Device;
	ComPtr<ID3D11DeviceContext>     m_d3d11DevCon;
	ComPtr<ID3D11RenderTargetView>  m_renderTargetView;

	ComPtr<ID3D11Buffer>            m_constantBuffer;
public:
	DX11Context() {}
	~DX11Context()
	{

	}

	/**
	*   初始化 DX11
	*/
	bool    Init(HWND hWnd, HDC hDC)
	{

		RECT    rt = { 0, 0, 0, 0 };
		GetClientRect(hWnd, &rt);
		m_winWidth = rt.right - rt.left;
		m_winHeight = rt.bottom - rt.top;

		HRESULT hr = S_OK;

		DXGI_MODE_DESC bufferDesc;
		ZeroMemory(&bufferDesc, sizeof(DXGI_MODE_DESC));

		bufferDesc.Width = m_winWidth;
		bufferDesc.Height = m_winHeight;
		bufferDesc.RefreshRate.Numerator = 60;
		bufferDesc.RefreshRate.Denominator = 1;
		bufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		bufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		bufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;


		DXGI_SWAP_CHAIN_DESC swapChainDesc;
		ZeroMemory(&swapChainDesc, sizeof(DXGI_SWAP_CHAIN_DESC));

		swapChainDesc.BufferDesc = bufferDesc;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 1;
		swapChainDesc.OutputWindow = hWnd;
		swapChainDesc.Windowed = TRUE;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;


		hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, NULL, NULL, NULL, D3D11_SDK_VERSION, &swapChainDesc, m_SwapChain.GetAddressOf(), m_d3d11Device.GetAddressOf(), NULL, m_d3d11DevCon.GetAddressOf());


		if (FAILED(hr))
		{
			MessageBox(0, L"D3D11CreateDevice Failed.", 0, 0);
			return false;
		}

		ID3D11Texture2D* backBuffer = nullptr;
		hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);

		if (nullptr != backBuffer)
		{
			hr = m_d3d11Device->CreateRenderTargetView(backBuffer, NULL, m_renderTargetView.GetAddressOf());
		}
		else
		{
			MessageBox(0, L"CreateRenderTargetView Failed.", 0, 0);
			return false;
		}

		backBuffer->Release();
		backBuffer = nullptr;
		m_d3d11DevCon->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), NULL);

		struct CameraCB { XMFLOAT4X4 view; };

		D3D11_BUFFER_DESC cbd = {};
		cbd.Usage = D3D11_USAGE_DYNAMIC;
		cbd.ByteWidth = sizeof(CameraCB);
		cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		m_d3d11Device->CreateBuffer(&cbd, nullptr, m_constantBuffer.GetAddressOf());

		return true;

	}


	void UpdateCameraBuffer(const XMMATRIX& viewMatrix, const XMMATRIX& projMatrix)
	{
		struct CameraData { XMFLOAT4X4 view; XMFLOAT4X4 projection; };

		D3D11_MAPPED_SUBRESOURCE mapped;
		if (SUCCEEDED(m_d3d11DevCon->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			CameraData* cb = (CameraData*)mapped.pData;
			XMStoreFloat4x4(&cb->view, XMMatrixTranspose(viewMatrix));         // HLSL 默认列主序
			XMStoreFloat4x4(&cb->projection, XMMatrixTranspose(projMatrix));   // 转置
			m_d3d11DevCon->Unmap(m_constantBuffer.Get(), 0);
		}
		m_d3d11DevCon->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

	}


	/**
	*   销毁 DX11
	*/
	void    Shutdown()
	{
		m_SwapChain.Reset();
		m_d3d11Device.Reset();
		m_d3d11DevCon.Reset();
	}

	/**
	*   交换缓冲区
	*/
	void    SwapBuffer()
	{
		m_SwapChain->Present(1, 0);
	}

};
 
#pragma once 
#include"pch.h"
#include <memory>  
#include "Render/D3D11/Device.h"
#include "Render/D3D11/SwapChain.h"
#include "Render/IRenderer.h"             
#include "Render/IRenderTarget.h"
#include "Editor/Input/InputSystem.h" 
#include "Editor/Input/ViewportInputAdapter.h"
#include "UI/UIManager.h" 
#include "Document/DocumentManager.h"
#include "Text/FontSystem.h"
namespace MiniCAD
{
	class MainWindow
	{

	public :
		MainWindow();
		~MainWindow();
		bool Initialize(const wchar_t* title, int width, int height);
		void Run(); 

	private:
		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		LRESULT EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		bool    InitWindow(const wchar_t* title, int width, int height);
		bool    InitD3D11(int width, int height);  
		bool    InitDocument( int width, int height);

		void DocumentInput();
		// ── 渲染 ──────────────────────────────────────────────
		void RenderFrame(); 
		 
	private:
		// 窗口
		HWND m_hwnd;

		// ── D3D11 专属，只有 Windows 才有 ──────────────────
		std::unique_ptr<Device>       m_device;
		std::unique_ptr<SwapChain>    m_swapChain;

		// ── 接口层，跨平台 ──────────────────────────────────
		std::unique_ptr<IRenderer>    m_renderer;
		 
		FontSystem                    m_fontSystem;
		DocumentManager               m_docManager; 
		ViewportInputAdapter          m_viewportInputAdapter;
		UIManager                     m_uiManager; 
	};
}  

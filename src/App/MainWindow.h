#pragma once
#include "pch.h"
#include "Render/D3D11/Device.h"	
#include "Render/D3D11/SwapChain.h"	
#include "Render/D3D11/Renderer.h"	 
#include "Render/Viewport/Viewport.h"	 
#include "App/Document/Document.h" 
#include "App/Input/InputSystem.h" 

namespace MiniCAD 
{
	class MainWindow
	{
	public:
		MainWindow();
		~MainWindow(); 
		bool Initialize( const wchar_t* title, int width, int height);
		void Run();

	private:
		// ── Win32 ─────────────────────────────────────────────
		static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
		LRESULT EventProc(HWND, UINT, WPARAM, LPARAM);

		// ── 初始化分步 ────────────────────────────────────────
		bool InitWindow(const wchar_t* title, int width, int height);
		bool InitD3D11(int width, int height);
		bool InitViewportAndDocument(int width, int height);

		// ── 渲染 ──────────────────────────────────────────────
		void RenderFrame(); 
  
	private:
		// 窗口
		HWND m_hwnd;

		// ── D3D11 层（硬件资源）──────────────────────────────
		std::unique_ptr<Device>       m_device;
		std::unique_ptr<SwapChain>    m_swapChain;
		std::unique_ptr<Renderer>     m_renderer; 

		// ── 应用层 ────────────────────────────────────────────
		std::unique_ptr<Viewport>     m_viewport;
		std::unique_ptr<Document>     m_document; 

		InputSystem                   m_input;
		 
		// ── UI 层（预留）─────────────────────────────────────
		// std::unique_ptr<IUILayer>  m_uiLayer;
		POINT m_lastMousePos = { 0, 0 }; 

	};
}
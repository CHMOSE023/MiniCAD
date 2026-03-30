#pragma once
#include "pch.h"
#include "Render/D3D11/Device.h"	
#include "Render/D3D11/SwapChain.h"	
#include "Render/D3D11/Renderer.h"	
#include "Render/Viewport/Camera.h"	
#include "Render/Viewport/Viewport.h"	
#include "App/Scene/Scene.h"	

namespace MiniCAD 
{
	class MainWindow
	{
	public:
		MainWindow();
		~MainWindow(); 
		bool Initialize( const wchar_t* title, int width, int height);
		void AddEntity();
		void Run();

	private:
		void RenderFrame();
		static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
		LRESULT EventProc(HWND, UINT, WPARAM, LPARAM);
		HWND m_hwnd;

		std::unique_ptr<Device>       m_device;
		std::unique_ptr<SwapChain>    m_swapChain;
		std::unique_ptr<Renderer>     m_renderer; 
		std::unique_ptr<Scene>        m_scene;
		std::unique_ptr<Viewport>     m_viewport;

		POINT m_lastMousePos = { 0, 0 }; 

	};
}
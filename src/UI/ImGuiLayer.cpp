#include "ImGuiLayer.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h> 
namespace MiniCAD
{
	bool ImGuiLayer::Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
	{
		m_hwnd    = hwnd;
        m_device  = device;
        m_context = context;

		IMGUI_CHECKVERSION();  
		ImGui::CreateContext(); 

		ImGui_ImplWin32_Init(hwnd);
		ImGui_ImplDX11_Init(device, context);

		return true;

	}

	void ImGuiLayer::Shutdown()
	{
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	void ImGuiLayer::Begin()
	{
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
	} 

	void ImGuiLayer::End()
	{
		ImGui::Render();

		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		ImGuiIO& io = ImGui::GetIO();
	 
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}
	}

}
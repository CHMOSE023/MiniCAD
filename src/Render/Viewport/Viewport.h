#pragma once
#include "App/Scene/Scene.h"
#include "Render/D3D11/Renderer.h"
#include "Render/Viewport/Camera.h"
#include "Render/Viewport/Grid.h"  
#include <App/Input/IInputHandler.h>
namespace MiniCAD
{
	class Viewport : public IInputHandler
	{
	public:
		Viewport(Renderer* renderer, float width, float height) :
			m_renderer(renderer),
			m_grid(std::make_unique<Grid>(XMFLOAT3())),
			m_camera(std::make_unique<Camera>(width, height)),
			m_width(0.f),
			m_height(0.f),
			m_isPanning(false) 
		{
		}

		void Draw(const Scene& scene, const RenderTarget& target);

		Camera* GetCamera() const { return m_camera.get(); }
		Grid*   GetGrid()   const { return m_grid.get(); }

		// ── 交互操作 ── 
		void Resize(float width, float height);              
		void Pan(float dx, float dy);                       // 平移
		void Zoom(float delta, float mouseX, float mouseY); // 缩放
		void BeginPan();                                    // 开始平移
		void EndPan();                                      // 结束平移	

		bool OnInput(const InputEvent& e) override
		{
			switch (e.type)
			{
			case InputEventType::MouseWheel:
				Zoom(e.wheelDelta, e.mouseX, e.mouseY);
				return true;   // 消费
			case InputEventType::MouseButtonDown:
				if (e.button == MouseButton::Middle)
				{
					BeginPan(); 
					return true;
				}
				break;
			case InputEventType::MouseButtonUp:
				if (e.button == MouseButton::Middle) 
				{
					EndPan(); 
					return true;
				}
				break;
			case InputEventType::MouseMove:
				return false;  // 不消费，Editor 也需要 MouseMove
			}
			return false;
		}
		 
		XMFLOAT2 ScreenToWorld(int px, int py) const;

	private:
		void DrawObject(const Object* obj);

		std::unique_ptr<Grid>    m_grid;         // 辅助轴网
		std::unique_ptr<Camera>  m_camera;       // 持有 相机
		Renderer*                m_renderer;     // 借用 渲染 
		bool                     m_isPanning;    // 是否平移
		float                    m_width;  
		float                    m_height;
	};

	
}

#pragma once
#include "App/Scene/Scene.h"
#include "Render/D3D11/Renderer.h"
#include "Render/Viewport/Camera.h"
#include "Render/Viewport/Grid.h"  
#include "App/Abstractions/IInputHandler.h"
#include "App/Abstractions/IViewContext.h"
#include <App/Preview/PreviewPrimitive.h>
namespace MiniCAD
{
	class Viewport : public IInputHandler, public IViewContext
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

		Grid*   GetGrid()   const { return m_grid.get(); }

		DirectX::XMFLOAT3 ScreenToWorld(float px, float py) const override
		{
			return m_camera->ScreenToWorld(px, py);
		}

		Camera* GetCamera() const override
		{
			return m_camera.get();
		}

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

		// 绘制预览图元
		void SetPreview(PreviewPrimitive primitive) override  
		{
			m_toolPreview = std::move(primitive);
			m_hasToolPreview = true;
		}

		// 清空绘制预览图元
		void ClearPreview() override { m_hasToolPreview = false; }

		// Viewport.h 接口实现
		void SetHoverPreview(PreviewPrimitive primitive) override
		{
			m_hoverPreview = std::move(primitive);
			m_hasHoverPreview = true;
		}
		void ClearHoverPreview() override { m_hasHoverPreview = false; }

		void SetSelectPreview(std::vector<PreviewPrimitive> primitives) override
		{
			m_selectPreviews = std::move(primitives);
			m_hasSelectPreview = !m_selectPreviews.empty();
		}
		void ClearSelectPreview() override
		{
			m_selectPreviews.clear();
			m_hasSelectPreview = false;
		}
	private:
		void DrawObject(const Object* obj);

		void DrawPreviewPrimitive(const PreviewPrimitive& p); // 绘制预览图形

		std::unique_ptr<Grid>    m_grid;         // 辅助轴网
		std::unique_ptr<Camera>  m_camera;       // 持有 相机
		Renderer*                m_renderer;     // 借用 渲染 
		bool                     m_isPanning;    // 是否平移
		float                    m_width;  
		float                    m_height;

		// 预览
		// Viewport.h 私有成员
		PreviewPrimitive              m_toolPreview;           // 工具预览
		PreviewPrimitive              m_hoverPreview;          // 悬停预览
		std::vector<PreviewPrimitive> m_selectPreviews;        // 选中预览（多选）

		bool m_hasToolPreview   = false;
		bool m_hasHoverPreview  = false;
		bool m_hasSelectPreview = false;
	};

	
}

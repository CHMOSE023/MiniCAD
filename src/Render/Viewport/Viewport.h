#pragma once
#include "App/Scene/Scene.h"
#include "Render/D3D11/Renderer.h"
#include "Render/Viewport/Camera.h"
#include "Render/Viewport/Grid.h"  
#include "App/Abstractions/IInputHandler.h"
#include "App/Abstractions/IViewContext.h"
#include <App/Preview/PreviewPrimitive.h>
#include <unordered_set> 
namespace MiniCAD
{
	class Viewport : public IInputHandler, public IViewContext
	{
	public:
		Viewport(Renderer* renderer, float width, float height);
		 
		// void Draw(const Scene& scene, const RenderTarget& target);    

		void Draw(const Scene& scene,
			const std::unordered_set<Object::ObjectID>& selected,
			const std::unordered_set<Object::ObjectID>& hovered,
			const RenderTarget& target);
		 

		virtual void SetPreview(PreviewPrimitive primitive) override; // 绘制预览图元		
		virtual void ClearPreview() override;                         // 清空绘制预览图元

		virtual DirectX::XMFLOAT3 ScreenToWorld(float px, float py) const override;
		virtual Camera* GetCamera() const override;
		virtual	bool OnInput(const InputEvent& e) override;         // 输入

		// ── 交互操作 ── 
		void Resize(float width, float height);              
		void Pan(float dx, float dy);                       // 平移
		void Zoom(float delta, float mouseX, float mouseY); // 缩放
		void BeginPan();                                    // 开始平移
		void EndPan();                                      // 结束平移		

	private:
		void DrawObject(const Object* obj, const Scene& scene);
		void DrawObject(const Object* obj, const DirectX::XMFLOAT4& color);
		void DrawPreviewPrimitive(const PreviewPrimitive& p); // 绘制预览图形 

		std::unique_ptr<Grid>    m_grid;         // 轴网
		std::unique_ptr<Camera>  m_camera;       // 相机
		Renderer*                m_renderer;     // 渲染 
		bool                     m_isPanning;    // 平移
		float                    m_width;  
		float                    m_height;

		PreviewPrimitive    m_toolPreview; 	// 预览 	
		bool                m_hasToolPreview = false;
		 
	};

	
}

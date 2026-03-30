#include "Render/Viewport/Viewport.h"

namespace MiniCAD
{  
	void Viewport::Draw(const Scene& scene, const RenderTarget& target)
	{
		// 1. 取 MVP
		XMMATRIX mvp = m_camera->GetViewProj(); 

		m_renderer->Begin(target, mvp); 

		for (auto id : scene.GetAllIDs()) 
		{
			DrawObject(scene.GetEntity(id));
		}

		m_renderer->DrawGrad(*m_grid); // *m_grid 返回 Grid&
		m_renderer->End();
		 
	}

	void Viewport::DrawObject(const Object* obj) 
	{
		if (!obj) return;

		 if (obj->IsKindOf<LineEntity>()) {
		 	const auto* line = static_cast<const LineEntity*>(obj);
			const auto& attr = line->GetAttr();
		 	const auto& geo = line->GetLine();
		 	m_renderer->DrawLine(geo.Start, geo.End, attr.Color);
		 	return;
		 }
 
	}

	void Viewport::BeginPan() 
	{
		m_isPanning = true;
	}

	void Viewport::EndPan()
	{
		m_isPanning = false;
	}

	void Viewport::Pan(float dx, float dy)
	{
		if (!m_isPanning) 
			return;
		m_camera->Update(dx, dy, 0.f, true);
	}

	void Viewport::Zoom(float delta, float mouseX, float mouseY)
	{
		m_camera->Update(0.f, 0.f, delta, false, mouseX, mouseY);
	}

	void Viewport::Resize(float width, float height)
	{
		m_width = width;
		m_height = height;
		m_camera->Resize(width, height);
	}
}

#include "Render/Viewport/Viewport.h"
#include "App/Preview/PreviewPrimitive.h"

namespace MiniCAD
{  
	void Viewport::Draw(const Scene& scene, const RenderTarget& target)
	{
		// 1. 取 MVP
		XMMATRIX mvp = m_camera->GetViewProj(); 

		m_renderer->Begin(target, mvp); 

		// 1. 正常渲染 Scene 实体
		for (auto id : scene.GetAllIDs()) 
		{
			DrawObject(scene.GetEntity(id));
		}

		// 2. 渲染网格
		m_renderer->DrawGrad(*m_grid); // *m_grid 返回 Grid&

		// 3. 渲染预览层
		if (m_hasPreview && !m_preview.Points.empty())
		{
			DrawPreview();
		}

		m_renderer->End();
		 
	}

	void Viewport::DrawPreview()
	{
		const auto& pts = m_preview.Points;
		const auto& color = m_preview.Color;

		switch (m_preview.Type)
		{
		case PreviewPrimitiveType::LineList:
			// 每两个点一条线
			for (size_t i = 0; i + 1 < pts.size(); i += 2)
			{
				m_renderer->DrawLine(pts[i], pts[i + 1], color);
			}
			break;

		case PreviewPrimitiveType::LineStrip:
			// 相邻点连线
			for (size_t i = 0; i + 1 < pts.size(); ++i)
			{
				m_renderer->DrawLine(pts[i], pts[i + 1], color);
			}
			break;
		}
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

#include "Render/Viewport/Viewport.h"
#include "App/Preview/PreviewPrimitive.h"

namespace MiniCAD
{  
	void Viewport::Draw(const Scene& scene, const RenderTarget& target)
	{
		// 1. 取 MVP
		XMMATRIX mvp = m_camera->GetViewProj(); 

		m_renderer->Begin(target, mvp);  

		// 2. 选中高亮（青色，叠在实体上面）
		if (m_hasSelectPreview)
			for (auto& p : m_selectPreviews)
				DrawPreviewPrimitive(p);

		// 3. 悬停高亮（蓝色，叠在选中上面）
		if (m_hasHoverPreview)
			DrawPreviewPrimitive(m_hoverPreview);

		// 4. 工具预览（灰色，最顶层）
		if (m_hasToolPreview && !m_toolPreview.Points.empty())
			DrawPreviewPrimitive(m_toolPreview);

		// 1. 正常渲染 Scene 实体
		for (auto id : scene.GetAllIDs())
		{
			DrawObject(scene.GetEntity(id));
		}
		m_renderer->DrawGrad(*m_grid);  
		m_renderer->End();
		 
	}

	void Viewport::DrawPreviewPrimitive(const PreviewPrimitive& p)
	{
		const auto& pts = p.Points;
		switch (p.Type)
		{
		case PreviewPrimitiveType::LineList:
			for (size_t i = 0; i + 1 < pts.size(); i += 2)
				m_renderer->DrawLine(pts[i], pts[i + 1], p.Color);
			break;
		case PreviewPrimitiveType::LineStrip:
			for (size_t i = 0; i + 1 < pts.size(); ++i)
				m_renderer->DrawLine(pts[i], pts[i + 1], p.Color);
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

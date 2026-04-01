#include "Render/Viewport/Viewport.h"
#include "App/Preview/PreviewPrimitive.h"

namespace MiniCAD
{
	Viewport::Viewport(Renderer* renderer, float width, float height) :
		m_renderer(renderer),
		m_grid(std::make_unique<Grid>(XMFLOAT3())),
		m_camera(std::make_unique<Camera>(width, height)),
		m_width(0.f),
		m_height(0.f),
		m_isPanning(false)
	{
	} 

	void Viewport::Draw(const Scene& scene,
		const std::unordered_set<Object::ObjectID>& selected,
		const std::unordered_set<Object::ObjectID>& hovered,
		const RenderTarget& target)
	{	
		// 1. 取 MVP
		XMMATRIX mvp = m_camera->GetViewProj();

		m_renderer->Begin(target, mvp);
		  
		for (auto& id : scene.GetAllIDs())
		{
			auto obj = scene.GetEntity(id);

			if (selected.count(id))
			{
				DrawObject(obj, DirectX::XMFLOAT4(1, 1, 0, 1)); // 选中：黄
			}
			else if (hovered.count(id))
			{
				DrawObject(obj, DirectX::XMFLOAT4(0, 1, 1, 1)); // Hover：青
			}
			else
			{
				DrawObject(obj);
			}
		}

		if (m_hasToolPreview)
			DrawPreviewPrimitive(m_toolPreview);

		m_renderer->DrawGrad(*m_grid);
		m_renderer->End();
	}

	void Viewport::ClearPreview() { m_hasToolPreview = false; } 
	  
	DirectX::XMFLOAT3 Viewport::ScreenToWorld(float px, float py) const  { return m_camera->ScreenToWorld(px, py); }

	Camera* Viewport::GetCamera() const { return m_camera.get(); }
	   
	/// <summary>
	/// 绘制预览
	/// </summary>
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

		if (obj->IsKindOf<LineEntity>())
		{
			const auto* line = static_cast<const LineEntity*>(obj);
			const auto& attr = line->GetAttr();
			const auto& geo = line->GetLine();
			m_renderer->DrawLine(geo.Start, geo.End, attr.Color);
			return;
		}

	}

	void Viewport::DrawObject(const Object* obj, const DirectX::XMFLOAT4& color)
	{
		if (obj->IsKindOf<LineEntity>()) 
		{
			const auto* line = static_cast<const LineEntity*>(obj);		 
			const auto& geo = line->GetLine();
			m_renderer->DrawLine(geo.Start, geo.End, color);
			return;
		}
	}

	void Viewport::BeginPan() { m_isPanning = true; }

	void Viewport::EndPan() { m_isPanning = false; }

	bool Viewport::OnInput(const InputEvent& e)
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
	void Viewport::SetPreview(PreviewPrimitive primitive)
	{
		m_toolPreview = std::move(primitive);
		m_hasToolPreview = true;
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

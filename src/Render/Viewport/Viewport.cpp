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


	// 矩阵逆变换的通用写法
	XMFLOAT2 Viewport::ScreenToWorld(int px, int py) const
	{
		const Camera* cam = GetCamera();

		// 屏幕像素 → NDC（-1 ~ +1）
		float ndcX  = (2.f * px / cam->GetWidth()) - 1.f;
		float ndcY = -(2.f * py / cam->GetHeight()) + 1.f; // Y 轴翻转

		// NDC 点（z=0 对应近平面，正交投影里 z 无所谓，取 0）
		XMVECTOR ndcPos = XMVectorSet(ndcX, ndcY, 0.f, 1.f);

		// 逆变换矩阵
		XMMATRIX invViewProj = XMMatrixInverse(nullptr, cam->GetViewProj());

		// 变换到世界空间
		XMVECTOR worldPos = XMVector3TransformCoord(ndcPos, invViewProj);

		XMFLOAT3 result;
		XMStoreFloat3(&result, worldPos);
		return XMFLOAT2(result.x, result.y);
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

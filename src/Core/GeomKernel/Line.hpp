#pragma once 
#include <DirectXMath.h>
#include <algorithm>
#include "AABB.hpp"
using namespace DirectX;

namespace MiniCAD
{
	struct Line
	{ 
		Line() : Start(0.f, 0.f, 0.f)
			   , End(0.f, 0.f, 0.f)
			   , IsSegment(true)
		{
		}

		Line(const XMFLOAT3& start, const XMFLOAT3& end, bool isSegment = true)
			: Start(start)
			, End(end)
			, IsSegment(isSegment)
		{
		}

		XMFLOAT3 Start;
		XMFLOAT3 End;
		bool IsSegment = true;

		static constexpr float EPSILON = 1e-6f;

		// 基础属性
		// =============================
		XMVECTOR Direction() const
		{
			XMVECTOR s = XMLoadFloat3(&Start);
			XMVECTOR e = XMLoadFloat3(&End);
			return XMVector3Normalize(XMVectorSubtract(e, s));
		}

		float Length() const
		{
			return XMVectorGetX(XMVector3Length(Vector()));
		}

		float LengthSq() const
		{
			return XMVectorGetX(XMVector3LengthSq(Vector()));
		}


		XMFLOAT3 Midpoint() const
		{
			XMVECTOR mid = XMVectorScale(XMVectorAdd(XMLoadFloat3(&Start), XMLoadFloat3(&End)), 0.5f);
			XMFLOAT3 result;
			XMStoreFloat3(&result, mid);
			return result;
		}


		// =============================
		// 参数化表达
		// P(t) = Start + t*(End-Start)
		// =============================
		XMVECTOR PointAt(float t) const
		{
			XMVECTOR s = XMLoadFloat3(&Start);
			XMVECTOR dir = Vector();
			return XMVectorAdd(s, XMVectorScale(dir, t));
		}

		// 投影参数 t
		float ProjectParam(const XMFLOAT3& p) const
		{
			XMVECTOR s = XMLoadFloat3(&Start);
			XMVECTOR v = Vector();
			XMVECTOR pt = XMLoadFloat3(&p);

			float lenSq = XMVectorGetX(XMVector3LengthSq(v));

			if (lenSq < EPSILON)
				return 0.0f;

			float t = XMVectorGetX(XMVector3Dot(XMVectorSubtract(pt, s), v)) / lenSq;

			if (IsSegment) { t = std::clamp(t, 0.0f, 1.0f); }

			return t;
		}

		// =============================
		// 几何计算
		// =============================
		XMFLOAT3 ClosestPoint(const XMFLOAT3& p) const
		{
			float t = ProjectParam(p);
			XMVECTOR point = PointAt(t);

			XMFLOAT3 result;
			XMStoreFloat3(&result, point);
			return result;
		}

		float DistanceToPoint(const XMFLOAT3& p) const
		{
			XMFLOAT3 cp = ClosestPoint(p);           // 先存到具名变量
			XMVECTOR vcp = XMLoadFloat3(&cp);         // 再取地址
			XMVECTOR vpt = XMLoadFloat3(&p);
			return XMVectorGetX(XMVector3Length(XMVectorSubtract(vpt, vcp)));
		}

		// =============================
		// 包围盒
		// =============================
		AABB GetBounds() const
		{
			AABB box;
			box.Min = { std::min(Start.x, End.x), std::min(Start.y, End.y), std::min(Start.z, End.z) };
			box.Max = { std::max(Start.x, End.x), std::max(Start.y, End.y), std::max(Start.z, End.z) };
			return box;
		}

		// =============================
	   // 工具函数
	   // =============================
		bool IsValid() const
		{
			return LengthSq() > EPSILON;
		}
	 
		XMVECTOR Vector() const
		{
			return XMVectorSubtract(XMLoadFloat3(&End), XMLoadFloat3(&Start));
		}
	};
}

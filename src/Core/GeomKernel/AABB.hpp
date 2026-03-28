#pragma once
#include <DirectXMath.h>
#include <algorithm>

using namespace DirectX;

namespace MiniCAD
{
	struct AABB
	{
		XMFLOAT3 Min;
		XMFLOAT3 Max;

		AABB() = default;

		AABB(const XMFLOAT3& min, const XMFLOAT3& max) : Min(min), Max(max) {}

		XMFLOAT3 Center() const
		{
			return {
				(Min.x + Max.x) * 0.5f,
				(Min.y + Max.y) * 0.5f,
				(Min.z + Max.z) * 0.5f
			};
		}

		XMFLOAT3 Extents() const
		{
			return {
				(Max.x - Min.x) * 0.5f,
				(Max.y - Min.y) * 0.5f,
				(Max.z - Min.z) * 0.5f
			};
		}

		void Expand(const XMFLOAT3& p)
		{
			Min.x = std::min(Min.x, p.x);
			Min.y = std::min(Min.y, p.y);
			Min.z = std::min(Min.z, p.z);

			Max.x = std::max(Max.x, p.x);
			Max.y = std::max(Max.y, p.y);
			Max.z = std::max(Max.z, p.z);
		}

		bool Contains(const XMFLOAT3& p) const
		{
			return
				p.x >= Min.x && p.x <= Max.x &&
				p.y >= Min.y && p.y <= Max.y &&
				p.z >= Min.z && p.z <= Max.z;
		}
	};
}
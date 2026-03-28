#pragma once  
#include <DirectXMath.h> 
#include <DirectXColors.h>
#include <cstdint>
using namespace DirectX;

namespace MiniCAD
{
	enum class LineType : uint8_t
	{
		SOLID = 0,
		DASHED,
		DOTTED,
		DASH_DOT
	};

	class EntityAttr
	{
	public:
		EntityAttr()
		{
			XMStoreFloat4(&Color, Colors::White);
		}
	public:
		XMFLOAT4  Color;
		uint32_t  LayerId = 0;
		LineType  LineType = LineType::SOLID;
		float     LineWidth = 1.0;
		bool      Visible = true;
	};
}

#pragma once  
#include "pch.h"
#include <DirectXMath.h> 
#include <DirectXColors.h>
#include <cstdint>
using namespace DirectX;

namespace MiniCAD
{
	using LayerID = uint32_t;

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
		LayerID   LayerId = 0;
		LineType  LineType = LineType::SOLID;
		float     LineWidth = 1.0;
		bool      Visible = true;
	};
}

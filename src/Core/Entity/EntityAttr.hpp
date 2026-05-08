#pragma once   
#include <DirectXMath.h>  
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
		XMFLOAT4  Color = { 1.0, 1.0, 1.0, 1.0 };
		LayerID   LayerId = 0;
		LineType  LineType = LineType::SOLID;
		float     LineWidth = 1.0;
		bool      Visible = true;
	};


}
#pragma once  
#include <DirectXMath.h>
#include "Core/Object/Object.hpp"
#include "Core/Object/RuntimeType.hpp"
#include "Core/GeomKernel/Line.hpp"
#include "EntityAttr.hpp"
#include "Serialization/ISerializer.h"
using namespace DirectX;

namespace MiniCAD
{
	class LineEntity: public Object
	{ 
	public: 
		LineEntity(ObjectID id, const XMFLOAT3& start, const XMFLOAT3& end) : Object(id), m_line(start, end), m_attr{} {};

		const Line& GetLine() const { return m_line; }

		void SetLine(const Line& line) { m_line = line; }

		EntityAttr& GetAttr() { return m_attr; }             // 修改用
		const EntityAttr& GetAttr() const { return m_attr; } // 只读用
		void SetAttr(const EntityAttr& a) { m_attr = a; } 

		AABB GetBoundingBox() const { return m_line.GetBounds(); };

		void Serialize(ISerializer& s) const override
		{
			// 1. 写类型名
			s.WriteString("LineEntity");
			const auto& line = GetLine();
			// 2. 写 Line 数据
			s.WriteFloat(line.Start.x);
			s.WriteFloat(line.Start.y);
			s.WriteFloat(line.Start.z);
			s.WriteFloat(line.End.x);
			s.WriteFloat(line.End.y);
			s.WriteFloat(line.End.z);

			// 3. 写属性 
			const auto& attr = GetAttr();
			s.WriteFloat(attr.Color.x);
			s.WriteFloat(attr.Color.y);
			s.WriteFloat(attr.Color.z);
			s.WriteFloat(attr.Color.w);

			s.WriteUInt64(attr.LayerId);                // 使用 uint64_t 保持序列化一致
			s.WriteUInt64(static_cast<uint64_t>(attr.LineType));  // 枚举用整数保存
			s.WriteFloat(attr.LineWidth);
			s.WriteBool(attr.Visible);
			 
		};

		void Deserialize(ISerializer& s) override 
		{
			// 1. 读 Line 数据
			auto& line   = m_line;
			line.Start.x = s.ReadFloat();
			line.Start.y = s.ReadFloat();
			line.Start.z = s.ReadFloat();
			line.End.x = s.ReadFloat();
			line.End.y = s.ReadFloat();
			line.End.z = s.ReadFloat();

			// 2. 读属性
			auto& attr   = m_attr;
			attr.Color.x = s.ReadFloat();
			attr.Color.y = s.ReadFloat();
			attr.Color.z = s.ReadFloat();
			attr.Color.w = s.ReadFloat();

			attr.LayerId   = static_cast<uint32_t>(s.ReadUInt64());
			attr.LineType  = static_cast<LineType>(s.ReadUInt64());
			attr.LineWidth = s.ReadFloat();
			attr.Visible   = s.ReadBool();
		};

		DECLARE_RUNTIME_TYPE(LineEntity, Object)
	private:
		Line       m_line; 
		EntityAttr m_attr;
	};  


}
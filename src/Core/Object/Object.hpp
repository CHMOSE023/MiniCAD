#pragma once
#include "RuntimeType.hpp"
namespace MiniCAD 
{
	class ISerializer;

	class Object
	{
	public :
		using ObjectID = uint64_t;

		static constexpr ObjectID InvalidID = 0; 

		ObjectID GetID() const { return m_id; }

		void  SetID(ObjectID id) { m_id = id; }

		// --- Serialization 接口 ---
	    // 将对象写入序列化器
		virtual void Serialize(ISerializer& s) const = 0;

		// 从序列化器读取对象状态
		virtual void Deserialize(ISerializer& s) = 0;

		virtual const RuntimeTypeInfo* GetTypeInfo() const = 0;

		template<typename T>
		bool IsKindOf() const
		{
			return GetTypeInfo()->IsKindOf(&T::s_typeInfo);
		}

		inline static const RuntimeTypeInfo s_typeInfo{ "Object", nullptr };
	protected: 
		explicit Object(ObjectID id) : m_id(id) {} 	// 构造函数，指定对象 ID
	private:
		ObjectID m_id; // 对象唯一标识 
	};

}
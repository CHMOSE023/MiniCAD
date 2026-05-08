#pragma once
#include "RuntimeType.hpp"
#include <cstdint>   //  uint64_t
namespace MiniCAD
{
	class Object
	{
	public:
		using ObjectID = uint64_t;
		static constexpr ObjectID InvalidID = 0;

		ObjectID GetID() const { return m_id; }
		void     SetID(ObjectID id) { m_id = id; }


		virtual const RuntimeTypeInfo* GetTypeInfo() const = 0;

		template<typename T>
		bool IsKindOf() const noexcept
		{
			return GetTypeInfo()->IsKindOf(&T::TypeInfo);
		}

		inline static const RuntimeTypeInfo TypeInfo{ "Object", nullptr }; // 允许变量在多个翻译单元中出现定义，但它们被视为同一个实体

	protected:                                                             // 
		explicit Object(ObjectID id) : m_id(id) {} 	                       // 构造函数，指定对象 ID
	private:
		ObjectID m_id;
	};

}
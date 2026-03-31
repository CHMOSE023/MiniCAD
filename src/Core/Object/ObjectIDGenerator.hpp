#pragma once
#include "Object.hpp"
#include <atomic>
namespace MiniCAD
{
	class ObjectIDGenerator
	{
	public:
		// 全局单例
		static ObjectIDGenerator& Get()
		{
			static ObjectIDGenerator instance;
			return instance;
		}

		Object::ObjectID Next()
		{
			return m_counter.fetch_add(1, std::memory_order_relaxed);
		}

	private:
		ObjectIDGenerator() : m_counter(1) {} // 从 1 开始，0 保留给 InvalidID
		std::atomic<Object::ObjectID> m_counter;
	};
}
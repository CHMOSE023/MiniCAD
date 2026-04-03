#pragma once
#include "Core/Object/Object.hpp"
#include "Serialization/ISerializer.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

namespace MiniCAD
{
    class ObjectFactory
    {
    public:
        using Creator = std::function<std::unique_ptr<Object>(ISerializer&)>;

        static ObjectFactory& Get()
        {
            static ObjectFactory instance;
            return instance;
        }

        void Register(const std::string& typeName, Creator creator)
        {
            m_creators[typeName] = std::move(creator);
        }

        std::unique_ptr<Object> CreateFromSerializer(ISerializer& s) const
        {
            std::string type = s.ReadString();

            auto it = m_creators.find(type);
            if (it == m_creators.end())
            {
                // 跳过未知类型，而不是崩溃
                // TODO: 可以加日志
                return nullptr;
            }

            return it->second(s);
        }

    private:
        ObjectFactory() = default;
        std::unordered_map<std::string, Creator> m_creators;
    };
}
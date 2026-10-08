// 实体级序列化:按运行时类型名(RuntimeTypeInfo::Name)分发读写。
#pragma once
#include <memory>

namespace MiniCAD
{
    class ISerializer;
    class Entity;

    namespace EntityIO
    {
        // 把实体写入「当前对象作用域」(含 "type"/"id"/"attr" 与各类型负载)。
        // 不支持的类型返回 false(不写任何字段)。
        bool Write(ISerializer& s, const Entity& e);

        // 从「当前对象作用域」读出 "type" 并构造实体;未知类型/数据缺失返回 nullptr。
        std::unique_ptr<Entity> Read(ISerializer& s);
    }
}

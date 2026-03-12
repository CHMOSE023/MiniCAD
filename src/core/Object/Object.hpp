// ============================================================
// MiniCAD — core/Object/Object.hpp
// 职责：所有 CAD 对象的基类，提供 ID / 快照 / 序列化接口
// 依赖：core/Object/IDAllocator.h, core/Object/RuntimeType.h
// 约束：不持有 UndoRedo 引用；不感知命令栈
// ============================================================

#pragma once
#include "IDAllocator.hpp"
#include "RuntimeType.hpp"
#include <cstdint>
#include <vector>
#include <string_view>

namespace MiniCAD {

    // Forward declaration
    class ISerializer;

    // ============================================================
    // Object
    // 所有 CAD 对象的基类
    // 提供：
    //   1. 唯一 ID
    //   2. 快照 / 回滚接口
    //   3. 序列化 / 反序列化接口
    //   4. 轻量级运行时类型信息（替代 dynamic_cast）
    // ============================================================
    class Object {
    public:
        using ObjectID = uint64_t;

        // 无效对象 ID 常量
        static constexpr ObjectID INVALID_ID = 0;

        virtual ~Object() = default;

        // 返回对象唯一 ID
        ObjectID GetID() const { return m_id; }

        // --- Snapshot 接口（用于 Undo / Redo） ---
        // 返回对象当前状态的字节快照
        virtual std::vector<uint8_t> Snapshot() const = 0;

        // 用快照数据恢复对象状态
        virtual void RestoreSnapshot(const std::vector<uint8_t>& data) = 0;

        // --- Serialization 接口 ---
        // 将对象写入序列化器
        virtual void Serialize(ISerializer& s) const = 0;

        // 从序列化器读取对象状态
        virtual void Deserialize(ISerializer& s) = 0;

        // --- 运行时类型信息（C++17 inline static） ---
        // 根类型 Object，parent 为 nullptr
        inline static const RuntimeTypeInfo s_typeInfo{ "Object", nullptr };

        // 返回对象运行时类型信息
        virtual const RuntimeTypeInfo* GetTypeInfo() const { return &s_typeInfo; }

        // 检查对象是否为指定类型或其子类
        template<typename T>
        bool IsKindOf() const { return GetTypeInfo()->IsKindOf(&T::s_typeInfo); }

    protected:
        // 构造函数，指定对象 ID
        explicit Object(ObjectID id) : m_id(id) {}

        ObjectID m_id; // 对象唯一标识
    };

} // namespace MiniCAD
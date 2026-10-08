#pragma once
#include "Core/Entity/BlockEntity.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace MiniCAD
{
    class ISerializer;
    using BlockID = uint32_t;

    // 块表(对应 DXF BLOCK_RECORD 表)。按 BlockID 拥有全部块定义,并提供按名查找
    // (不区分大小写)。预置两个保留块:*Model_Space / *Paper_Space,与 AutoCAD 一致。
    class BlockTable
    {
    public:
        static constexpr BlockID ModelSpaceID = 0;          // *Model_Space
        static constexpr BlockID PaperSpaceID = 1;          // *Paper_Space
        static constexpr BlockID InvalidID    = 0xFFFFFFFFu; // 查找失败

        BlockTable();

        // 追加块定义,返回其 BlockID。
        //   · 若同名块已存在(不区分大小写),不追加,返回既有 ID(传入块被丢弃)。
        //   · 块的 ObjectID 字段不影响表索引,索引即 BlockID。
        BlockID Add(std::unique_ptr<BlockEntity> block);

        // 新建一个空块并加入表,返回其 BlockID;同名已存在则返回既有 ID。
        BlockID Create(const std::string& name, const Math::Point3& basePoint = { 0, 0, 0 });

        BlockEntity*       Find(BlockID id);
        const BlockEntity* Find(BlockID id) const;

        // 按名查找,未找到返回 InvalidID。
        BlockID            FindByName(const std::string& name) const;

        // 按名取块指针(便捷),未找到返回 nullptr。
        BlockEntity*       Get(const std::string& name);
        const BlockEntity* Get(const std::string& name) const;

        size_t Count() const { return m_blocks.size(); }

        void ForEach(const std::function<void(BlockID, const BlockEntity&)>& fn) const;

        // ── 序列化(Deserialize 整表替换,索引即 BlockID)──────────────────
        void Serialize(ISerializer& s) const;
        void Deserialize(ISerializer& s);

    private:
        std::vector<std::unique_ptr<BlockEntity>> m_blocks;   // 索引即 BlockID
    };
}

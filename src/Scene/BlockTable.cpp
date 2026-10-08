#include "BlockTable.h"
// 块子系统其余头文件(纯头实现)在此一并参与编译校验:
//   InsertEntity 已传递包含 BlockEntity / AttribEntity / TransformDrawSink。
#include "Core/Entity/InsertEntity.hpp"
#include "Serialization/ISerializer.h"
#include "Serialization/EntityIO.h"
#include <algorithm>
#include <cctype>

namespace MiniCAD
{
    namespace
    {
        std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }
    }

    BlockTable::BlockTable()
    {
        // 顺序必须与保留 ID 对齐:0=*Model_Space,1=*Paper_Space。
        // ObjectID 用 InvalidID 占位(块表索引才是身份,与场景实体 ID 解耦)。
        m_blocks.push_back(std::make_unique<BlockEntity>(Object::InvalidID, "*Model_Space"));
        m_blocks.push_back(std::make_unique<BlockEntity>(Object::InvalidID, "*Paper_Space"));
    }

    BlockID BlockTable::Add(std::unique_ptr<BlockEntity> block)
    {
        if (!block)
            return InvalidID;

        // 同名复用,避免重复(导入时块名是唯一键)。
        BlockID existing = FindByName(block->GetName());
        if (existing != InvalidID)
            return existing;

        m_blocks.push_back(std::move(block));
        return static_cast<BlockID>(m_blocks.size() - 1);
    }

    BlockID BlockTable::Create(const std::string& name, const Math::Point3& basePoint)
    {
        BlockID existing = FindByName(name);
        if (existing != InvalidID)
            return existing;

        m_blocks.push_back(std::make_unique<BlockEntity>(Object::InvalidID, name, basePoint));
        return static_cast<BlockID>(m_blocks.size() - 1);
    }

    BlockEntity* BlockTable::Find(BlockID id)
    {
        if (id < m_blocks.size()) return m_blocks[id].get();
        return nullptr;
    }

    const BlockEntity* BlockTable::Find(BlockID id) const
    {
        if (id < m_blocks.size()) return m_blocks[id].get();
        return nullptr;
    }

    BlockID BlockTable::FindByName(const std::string& name) const
    {
        std::string key = ToLower(name);
        for (size_t i = 0; i < m_blocks.size(); ++i)
            if (ToLower(m_blocks[i]->GetName()) == key)
                return static_cast<BlockID>(i);
        return InvalidID;
    }

    BlockEntity* BlockTable::Get(const std::string& name)
    {
        return Find(FindByName(name));
    }

    const BlockEntity* BlockTable::Get(const std::string& name) const
    {
        return Find(FindByName(name));
    }

    void BlockTable::ForEach(const std::function<void(BlockID, const BlockEntity&)>& fn) const
    {
        for (size_t i = 0; i < m_blocks.size(); ++i)
            fn(static_cast<BlockID>(i), *m_blocks[i]);
    }

    void BlockTable::Serialize(ISerializer& s) const
    {
        size_t n = m_blocks.size();
        if (!s.BeginArray("items", n))
            return;
        for (const auto& blk : m_blocks)
        {
            if (s.BeginElement(0))
            {
                EntityIO::Write(s, *blk);
                s.EndElement();
            }
        }
        s.EndArray();
    }

    void BlockTable::Deserialize(ISerializer& s)
    {
        size_t n = 0;
        if (!s.BeginArray("items", n))
            return;   // 旧文件无块表:保留构造预置的 *Model_Space / *Paper_Space

        std::vector<std::unique_ptr<BlockEntity>> blocks;
        blocks.reserve(n);
        for (size_t i = 0; i < n; ++i)
        {
            if (!s.BeginElement(i)) continue;
            auto e = EntityIO::Read(s);
            s.EndElement();
            if (e && e->IsKindOf<BlockEntity>())
                blocks.emplace_back(static_cast<BlockEntity*>(e.release()));
            else
                blocks.push_back(std::make_unique<BlockEntity>(Object::InvalidID, ""));   // 占位,保持索引==BlockID
        }
        s.EndArray();
        m_blocks = std::move(blocks);

        // 防御:保留块缺失时补回,保证 0/1 号索引语义不变。
        while (m_blocks.size() < 2)
            m_blocks.push_back(std::make_unique<BlockEntity>(Object::InvalidID,
                m_blocks.empty() ? "*Model_Space" : "*Paper_Space"));
    }
}

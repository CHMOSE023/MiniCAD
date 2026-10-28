#pragma once
#include "Core/Math/Point3.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include "Render/VertexTypes.hpp"
#include "Render/GlyphInstancing.hpp"
#include "Document/DeferredDash.hpp"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MiniCAD
{
    // ── 块定义级细分缓存（DrawContext::BeginBlockInstance）────────────────
    //
    // 天正图里作为块插入的建筑底图，同一个块定义（门窗、家具、线图案）插入几十到几千次，以前每次插入都克隆一遍块内容、
    // 重新细分一遍。现在同一块定义只细分一次，记下细分结果，之后的插入把它经平移、旋转接上，不再逐个画块内子实体。
    //
    // 能复用的前提是两次插入的输出只差一个平移 + 旋转：
    //   · 线型在插入变换之后按世界长度铺（DrawContext::EmitStyled），所以缩放、镜像不同的插入不能共用——
    //     插入变换去掉旋转后剩下的部分（缩放、镜像、错切）进键；
    //   · 块内 ByBlock 的颜色 / 线型 / 线宽与 0 层上的子实体随插入（InsertEntity::EnsureResolved），这几项也进键。
    // 记下的是插入变换去掉旋转、平移后的坐标（双精度），接上时再换到新位置：与直接细分只差双精度的舍入，
    // 落成 float 顶点后相同。按像素烘焙的（线宽、相对尺寸的点）、纹理文字、图像、绝对角度的线型符号不记
    struct BlockTessKey
    {
        const void* block       = nullptr;
        uint32_t    layer       = 0;
        uint32_t    lineType    = 0;
        int32_t     lineweight  = 0;
        uint32_t    colorMethod = 0;
        uint32_t    aci         = 0;
        uint64_t    rgba[4]     = {};
        // 插入变换去掉旋转后的线性部分：X 轴长度（按 40 位尾数取整），Y 轴与 Z 轴相对它的比值（按 1e-9 取整）。
        // 不同插入算出的同一个缩放只差最后几位，取整后落在同一个键里
        int64_t     scale = 0, yx = 0, yy = 0, zz = 0;

        bool operator==(const BlockTessKey&) const = default;
    };

    struct BlockTessKeyHash
    {
        size_t operator()(const BlockTessKey& k) const noexcept
        {
            uint64_t h = reinterpret_cast<uintptr_t>(k.block);
            auto mix = [&h](uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); };
            mix(k.layer);
            mix(k.lineType);
            mix(static_cast<uint32_t>(k.lineweight));
            mix(k.colorMethod);
            mix(k.aci);
            for (uint64_t c : k.rgba) mix(c);
            mix(static_cast<uint64_t>(k.scale));
            mix(static_cast<uint64_t>(k.yx));
            mix(static_cast<uint64_t>(k.yy));
            mix(static_cast<uint64_t>(k.zz));
            return static_cast<size_t>(h);
        }
    };

    // 记下的一个顶点 / 字形实例（双精度，接上时换到新位置再落成 float）
    struct BlockTessVertex
    {
        Math::Point3 p;
        VertexColor  color;
    };
    struct BlockTessGlyph
    {
        double      m00, m01, m10, m11;   // 同 GlyphInstance：局部 X / Y 轴在世界里的方向（含缩放）
        double      tx, ty, z;
        VertexColor color;
        uint32_t    mesh;
    };
    // 块内的区域覆盖：多边形，以及它出现时线 / 填充 / 点 / 字形实例已记了几个（接上时在同一位置调用 Wipe，裁剪的范围不变）
    struct BlockTessWipe
    {
        std::vector<Math::Point3> polygon;
        size_t lines = 0, fills = 0, points = 0, glyphs = 0;
    };

    struct BlockTessEntry
    {
        std::vector<BlockTessVertex> lines, fills, points;
        std::vector<BlockTessGlyph>  glyphs;
        std::vector<BlockTessWipe>   wipes;
        std::vector<DeferredDash>    dashes;
        size_t direct = 0;   // 块内直接子实体个数（BeginBlockChild 的调用次数）：分段细分的序号要跳过同样多个
        size_t leaves = 0;   // 画了几个叶子子实体（嵌套展开）：分段细分的进度
        bool   wide   = false;   // 有带几何宽度的线（DrawContext 把实体标成随视图变化，接上时也要标）
        // 块内画的线、三角形、文字锚点等输入的范围（块坐标）：块参照在裁剪框里时据此判断整个在框内 / 框外（ClipDrawSink::Classify）
        AABB   inputs = AABB::Empty();
        // 在裁剪框里与不裁剪时画法相同：没有区域覆盖、点、图像，也没问过裁剪框不转发的点样式、像素比例。
        // 为假时只有整个在框外（什么都不画）才能直接用
        bool   clipSafe = true;

        size_t Size() const { return lines.size() + fills.size() + points.size() + glyphs.size(); }
    };

    class BlockTessCache
    {
    public:
        // 统计每个块定义被展开几次（到 2 为止）。只展开一次的块（如整张底图作为一个块插入）记下来没用、还占内存，不记。
        // 第一次要判断时才调用（见 Worth），清空后重新统计
        using UseCounter = std::function<void(std::unordered_map<const void*, int>& uses)>;
        void SetUseCounter(UseCounter counter) { m_counter = std::move(counter); }

        // 记下的总顶点数上限：再多就不再记新的块（已记的照常复用）
        static constexpr size_t kMaxVertices = 8'000'000;

        void Clear()
        {
            if (m_entries.empty() && m_uses.empty() && m_seen.empty() && !m_counted) return;
            std::unordered_map<BlockTessKey, BlockTessEntry, BlockTessKeyHash>{}.swap(m_entries);
            std::unordered_map<const void*, int>{}.swap(m_uses);
            std::unordered_set<const void*>{}.swap(m_seen);
            m_counted  = false;
            m_vertices = 0;
            m_replays  = 0;
            m_skips    = 0;
        }

        const BlockTessEntry* Find(const BlockTessKey& key) const
        {
            const auto it = m_entries.find(key);
            return it != m_entries.end() ? &it->second : nullptr;
        }

        bool Full() const { return m_vertices >= kMaxVertices; }

        // 这个块值得记下来：展开不止一次。统计里没有的块（天正对象、标注等实体里面引用的块）第二次遇到才记
        bool Worth(const void* block)
        {
            if (!m_counted)
            {
                m_counted = true;
                if (m_counter) m_counter(m_uses);
            }
            if (const auto it = m_uses.find(block); it != m_uses.end())
                return it->second > 1;
            return !m_seen.insert(block).second;
        }

        void Insert(const BlockTessKey& key, BlockTessEntry&& entry)
        {
            const size_t n = entry.Size();
            if (m_entries.try_emplace(key, std::move(entry)).second)
                m_vertices += n;
        }

        // 统计（测试与排查用）：记下了几个块、多少顶点，接上过几次、因整个在裁剪框外跳过几次
        size_t EntryCount() const  { return m_entries.size(); }
        size_t VertexCount() const { return m_vertices; }
        size_t Replays() const     { return m_replays; }
        size_t Skips() const       { return m_skips; }
        void   NoteReplay()        { ++m_replays; }
        void   NoteSkip()          { ++m_skips; }

    private:
        std::unordered_map<BlockTessKey, BlockTessEntry, BlockTessKeyHash> m_entries;
        std::unordered_map<const void*, int> m_uses;
        std::unordered_set<const void*>      m_seen;
        UseCounter                           m_counter;
        bool                                 m_counted  = false;
        size_t                               m_vertices = 0;
        size_t                               m_replays  = 0;
        size_t                               m_skips    = 0;
    };
}

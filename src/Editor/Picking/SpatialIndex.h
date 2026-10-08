#pragma once
#include "Core/Object/Object.hpp"
#include "Core/Entity/Entity.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include "Scene/Scene.h"
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace MiniCAD
{
    // ─────────────────────────────────────────────────────────────
    //  SpatialIndex — 世界空间均匀网格 + 包围盒索引
    // ─────────────────────────────────────────────────────────────
    // 把场景实体按其世界 AABB 投入均匀网格单元；查询时只返回与查询窗口
    // (世界空间 AABB) 可能相交的候选实体，避免对全部 N 个实体逐一做精确
    // (含细分) 命中测试。
    //
    // 关键性质：索引只依赖实体几何，与相机无关 —— 平移/缩放不改变世界 AABB，
    // 因此只在“场景几何版本”变化时重建（见 Scene::GeometryVersion）。
    //
    // 结构（整表重建时生成）：
    //   - 网格：单元边长同时参考"实体数量"（平均每格约一个实体）和"实体中位尺寸的一半"
    //     （典型实体跨 4～9 格），取较大者；总单元数有上限。单元内容用连续数组存放
    //     （先计数、再前缀和、再填充），没有逐格的小数组和哈希表。
    //   - large：跨格过多的实体（例如图框），查询时直接比较包围盒
    //   - overflow：无限/退化包围盒(XLine / Ray 等)，始终作为候选
    //   - extra：整表重建之后增量新增 / 修改的实体，查询时直接比较包围盒；
    //     积累多了由 PreferRebuild() 提示调用方整表重建
    class SpatialIndex
    {
    public:
        using ObjectID = Object::ObjectID;

        // 统计信息（测试与调优用）
        struct Stats
        {
            size_t entries    = 0;      // 条目数（含墓碑）
            size_t gridRefs   = 0;      // 网格里的引用总数（一个实体跨几格就有几个）
            size_t large      = 0;
            size_t extra      = 0;
            size_t overflow   = 0;
            int    tombstones = 0;
            int    gridW      = 0;
            int    gridH      = 0;
            double cell       = 0.0;
        };

        bool Empty() const { return m_entries.empty() && m_overflow.empty(); }

        void Clear()
        {
            m_entries.clear();
            m_overflow.clear();
            m_cellStart.clear();
            m_cellItems.clear();
            m_large.clear();
            m_extra.clear();
            m_gw = m_gh = 0;
            m_stamp.clear();
            m_stampCounter = 0;
            m_byId.clear();
            m_tombstones = 0;
        }

        // ── 增量维护 ─────────────────────────────────────────
        // 单实体增/删/改时只更新该实体的索引项,避免几万实体的整表重建。
        // 删除走墓碑(条目失效但仍被网格引用);修改 = 墓碑旧条目 + 追加新条目到 extra。
        void Update(ObjectID id, const Scene& scene)
        {
            Remove(id);

            const Object* obj = scene.GetEntity(id);
            if (!obj || !obj->IsKindOf<Entity>()) return;   // 已删除
            const auto& ent = static_cast<const Entity&>(*obj);

            const AABB box = ent.GetBoundingBox();
            if (ent.IsBoundless() || !IsFiniteXY(box))
            {
                m_overflow.push_back(id);
                return;
            }

            const auto idx = static_cast<uint32_t>(m_entries.size());
            m_entries.push_back({ id, box });
            m_stamp.push_back(0);
            m_byId[id] = idx;
            m_extra.push_back(idx);
        }

        // 墓碑、溢出或增量条目占比过高:增量收益耗尽,调用方应整表重建
        bool PreferRebuild() const
        {
            const size_t n = m_entries.size();
            if (m_tombstones > 64 && static_cast<size_t>(m_tombstones) * 4 > n) return true;
            if (m_overflow.size() > 64 && m_overflow.size() * 4 > n) return true;
            if (m_extra.size() > std::max<size_t>(256, n / 16)) return true;
            return false;
        }

        void Rebuild(const Scene& scene)
        {
            Clear();

            // 1. 收集有限世界 AABB；无限/退化包围盒进 overflow
            AABB worldBounds = AABB::Empty();
            m_entries.reserve(static_cast<size_t>(scene.EntityCount()));
            scene.ForEachObject([&](const Object& obj)
            {
                if (!obj.IsKindOf<Entity>()) return;
                const auto& ent = static_cast<const Entity&>(obj);
                const AABB box = ent.GetBoundingBox();

                // 无界实体(XLine / Ray)或无限/非有限包围盒 → overflow,始终作为候选。
                // 注意:无界实体的包围盒退化为基点(有限),不能只靠 IsFiniteXY 判断,
                // 否则它们会被塞进网格,仅当查询窗口覆盖基点时才命中(反向框选漏选)。
                if (ent.IsBoundless() || !IsFiniteXY(box))
                {
                    m_overflow.push_back(obj.GetID());
                    return;
                }
                m_entries.push_back({ obj.GetID(), box });
                worldBounds.Expand(box);
            });

            const size_t n = m_entries.size();
            m_stamp.assign(n, 0);
            m_byId.reserve(n);
            for (size_t i = 0; i < n; ++i)
                m_byId[m_entries[i].Id] = static_cast<uint32_t>(i);
            if (n == 0)
                return;

            // 2. 单元边长：平均每格约一个实体（按面积），且不小于实体中位尺寸的一半
            //    （否则较大的实体会跨很多格，引用数随实体数超线性增长；
            //     取一半而不是整个：查询少扫一半候选，重建耗时几乎不变——见 tests/SpatialIndexTests.cpp）
            const double w = worldBounds.Max.x - worldBounds.Min.x;
            const double h = worldBounds.Max.y - worldBounds.Min.y;
            const double span = std::max(w, h);
            double byCount = (w > 0.0 && h > 0.0) ? std::sqrt(w * h / static_cast<double>(n))
                                                  : span / std::sqrt(static_cast<double>(n));
            std::vector<double> sizes(n);
            for (size_t i = 0; i < n; ++i)
            {
                const AABB& b = m_entries[i].Box;
                sizes[i] = std::max(b.Max.x - b.Min.x, b.Max.y - b.Min.y);
            }
            std::nth_element(sizes.begin(), sizes.begin() + static_cast<std::ptrdiff_t>(n / 2), sizes.end());
            const double bySize = sizes[n / 2] * 0.5;

            m_cell = std::max(byCount, bySize);
            if (!(m_cell > 0.0) || !std::isfinite(m_cell))
                m_cell = span > 0.0 ? span : 1.0;     // 所有实体重合：退化为单格

            // 总单元数不超过上限（单元起点数组的内存），超了就放大单元
            auto gridDim = [&](double extent) { return static_cast<int64_t>(std::floor(extent / m_cell)) + 1; };
            int64_t gw = gridDim(w), gh = gridDim(h);
            if (gw * gh > kMaxGridCells)
            {
                m_cell *= std::sqrt(static_cast<double>(gw * gh) / static_cast<double>(kMaxGridCells)) * 1.01;
                gw = gridDim(w);
                gh = gridDim(h);
            }
            m_originX = worldBounds.Min.x;
            m_originY = worldBounds.Min.y;
            m_gw = static_cast<int>(gw);
            m_gh = static_cast<int>(gh);

            // 3. 计数：每个单元有几个实体；跨格过多的实体放进 large
            const size_t cells = static_cast<size_t>(m_gw) * static_cast<size_t>(m_gh);
            m_cellStart.assign(cells + 1, 0);
            std::vector<uint8_t> inGrid(n, 0);
            for (size_t i = 0; i < n; ++i)
            {
                int cx0, cy0, cx1, cy1;
                ClampedRange(m_entries[i].Box, cx0, cy0, cx1, cy1);
                const int64_t covered = static_cast<int64_t>(cx1 - cx0 + 1) * (cy1 - cy0 + 1);
                if (covered > kMaxCellsPerEntity)
                {
                    m_large.push_back(static_cast<uint32_t>(i));
                    continue;
                }
                inGrid[i] = 1;
                for (int cy = cy0; cy <= cy1; ++cy)
                    for (int cx = cx0; cx <= cx1; ++cx)
                        ++m_cellStart[CellIndex(cx, cy) + 1];
            }

            // 4. 前缀和 → 每个单元在 m_cellItems 里的起点；再填充
            for (size_t c = 0; c < cells; ++c)
                m_cellStart[c + 1] += m_cellStart[c];
            m_cellItems.resize(m_cellStart[cells]);
            std::vector<uint32_t> cursor(m_cellStart.begin(), m_cellStart.end() - 1);
            for (size_t i = 0; i < n; ++i)
            {
                if (!inGrid[i])
                    continue;
                int cx0, cy0, cx1, cy1;
                ClampedRange(m_entries[i].Box, cx0, cy0, cx1, cy1);
                for (int cy = cy0; cy <= cy1; ++cy)
                    for (int cx = cx0; cx <= cx1; ++cx)
                        m_cellItems[cursor[CellIndex(cx, cy)]++] = static_cast<uint32_t>(i);
            }
        }

        // 查询世界空间窗口 q，返回候选 ObjectID（去重）。out 先被清空再填充。
        void Query(const AABB& q, std::vector<ObjectID>& out) const
        {
            out.clear();

            // overflow（无限实体）始终是候选
            out.insert(out.end(), m_overflow.begin(), m_overflow.end());

            if (m_entries.empty())
                return;

            if (++m_stampCounter == 0)            // 计数回绕：清零重来
            {
                std::fill(m_stamp.begin(), m_stamp.end(), 0u);
                m_stampCounter = 1;
            }
            auto consider = [&](uint32_t idx)
            {
                if (m_stamp[idx] == m_stampCounter) return;     // 已收集（实体跨多格）
                m_stamp[idx] = m_stampCounter;
                const Entry& e = m_entries[idx];
                if (e.Id == Object::InvalidID) return;          // 墓碑（增量删除 / 修改）
                if (e.Box.Intersects(q))                        // 网格单元是保守的，再用精确包围盒粗筛
                    out.push_back(e.Id);
            };

            if (m_gw > 0 && q.Max.x >= q.Min.x && q.Max.y >= q.Min.y)
            {
                int cx0, cy0, cx1, cy1;
                ClampedRange(q, cx0, cy0, cx1, cy1);
                if (cx0 <= cx1 && cy0 <= cy1)
                {
                    for (int cy = cy0; cy <= cy1; ++cy)
                    {
                        const size_t row = static_cast<size_t>(cy) * static_cast<size_t>(m_gw);
                        for (size_t c = row + static_cast<size_t>(cx0); c <= row + static_cast<size_t>(cx1); ++c)
                            for (uint32_t k = m_cellStart[c]; k < m_cellStart[c + 1]; ++k)
                                consider(m_cellItems[k]);
                    }
                }
            }
            for (uint32_t idx : m_large)
                consider(idx);
            for (uint32_t idx : m_extra)
                consider(idx);
        }

        Stats GetStats() const
        {
            Stats s;
            s.entries    = m_entries.size();
            s.gridRefs   = m_cellItems.size();
            s.large      = m_large.size();
            s.extra      = m_extra.size();
            s.overflow   = m_overflow.size();
            s.tombstones = m_tombstones;
            s.gridW      = m_gw;
            s.gridH      = m_gh;
            s.cell       = m_cell;
            return s;
        }

    private:
        struct Entry { ObjectID Id; AABB Box; };

        static constexpr int64_t kMaxGridCells      = int64_t(1) << 20;   // 单元起点数组最多 4 MB
        static constexpr int64_t kMaxCellsPerEntity = 64;                 // 跨格更多的实体放进 large

        // 摘除实体的现有索引项:条目置墓碑(网格 / large / extra 里的引用在查询时跳过),
        // overflow 中线性移除(无界实体极少)
        void Remove(ObjectID id)
        {
            if (auto it = std::find(m_overflow.begin(), m_overflow.end(), id);
                it != m_overflow.end())
                m_overflow.erase(it);

            if (auto it = m_byId.find(id); it != m_byId.end())
            {
                m_entries[it->second].Id = Object::InvalidID;
                m_byId.erase(it);
                ++m_tombstones;
            }
        }

        static bool IsFiniteXY(const AABB& b)
        {
            return std::isfinite(b.Min.x) && std::isfinite(b.Min.y) &&
                   std::isfinite(b.Max.x) && std::isfinite(b.Max.y);
        }

        // 包围盒覆盖的单元范围，夹紧到网格内（先在 double 里夹紧，远离网格的查询窗口不会整数溢出）
        void ClampedRange(const AABB& b, int& cx0, int& cy0, int& cx1, int& cy1) const
        {
            auto cell = [this](double v, double origin, int count)
            {
                const double f = std::floor((v - origin) / m_cell);
                return static_cast<int>(std::clamp(f, -1.0, static_cast<double>(count)));
            };
            cx0 = std::max(cell(b.Min.x, m_originX, m_gw), 0);
            cy0 = std::max(cell(b.Min.y, m_originY, m_gh), 0);
            cx1 = std::min(cell(b.Max.x, m_originX, m_gw), m_gw - 1);
            cy1 = std::min(cell(b.Max.y, m_originY, m_gh), m_gh - 1);
        }

        size_t CellIndex(int cx, int cy) const
        {
            return static_cast<size_t>(cy) * static_cast<size_t>(m_gw) + static_cast<size_t>(cx);
        }

    private:
        std::vector<Entry>    m_entries;    // 有限包围盒实体（含墓碑）
        std::vector<ObjectID> m_overflow;   // 无限/退化包围盒实体（始终候选）

        // 网格（整表重建时生成）：单元 c 的实体下标为 m_cellItems[m_cellStart[c] .. m_cellStart[c+1])
        double                m_originX = 0.0, m_originY = 0.0, m_cell = 1.0;
        int                   m_gw = 0, m_gh = 0;
        std::vector<uint32_t> m_cellStart;
        std::vector<uint32_t> m_cellItems;
        std::vector<uint32_t> m_large;      // 跨格过多的实体（逐个比较包围盒）
        std::vector<uint32_t> m_extra;      // 整表重建后增量加入的实体（逐个比较包围盒）

        mutable std::vector<uint32_t> m_stamp;         // 查询去重时间戳
        mutable uint32_t              m_stampCounter = 0;

        std::unordered_map<ObjectID, uint32_t> m_byId;  // id → m_entries 下标（增量维护）
        int                                    m_tombstones = 0;
    };
}

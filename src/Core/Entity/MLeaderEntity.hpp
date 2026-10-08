#pragma once
#include "Entity.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/Math/Constants.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <cmath>

namespace MiniCAD
{
    // 多重引线样式(对应 DXF MLEADERSTYLE)。承载箭头/基线/文字等外观参数。
    struct MLeaderStyle
    {
        double   ArrowSize     = 3.5;    // 箭头大小
        double   LandingGap    = 1.0;    // 基线与文字之间的间隙
        double   DoglegLength  = 6.0;    // 基线(dogleg)长度
        double   TextHeight    = 3.5;    // 文字字高
        uint32_t TextStyle     = 0;      // 文字样式（文字样式表 ID）
        bool     EnableLanding = true;   // 是否启用基线
        bool     EnableDogleg  = true;   // 是否启用 dogleg 折弯
    };

    using MLeaderStyleID = uint32_t;
    inline constexpr MLeaderStyleID MLeaderStyle_StandardID = 0;

    // 多重引线内容类型(DXF MULTILEADER 组码 172)。
    enum class MLeaderContent : uint8_t
    {
        None  = 0,
        Block = 1,
        MText = 2
    };

    // 多重引线实体(对应 DXF MULTILEADER / MLEADER)。多条引线汇聚到公共基线(landing),
    // 基线再经 dogleg 折弯连到内容(MTEXT 或块)。外观取自引用的 MLEADERSTYLE。
    class MLeaderEntity : public Entity
    {
    public:
        // 单条引线:折线顶点,points[0] = 箭头尖端,末点接入 dogleg 起点。
        struct LeaderLine
        {
            std::vector<Math::Point3> points;
            bool                      arrow = true;   // 该引线是否带箭头
        };

        explicit MLeaderEntity(ObjectID id, const MLeaderStyle& style = {})
            : Entity(id)
            , m_style(style)
        {}

        // ── 引线 ───────────────────────────────────────────────────────────
        void AddLeaderLine(LeaderLine line)            { m_lines.push_back(std::move(line)); }
        void AddLeaderLine(std::vector<Math::Point3> pts, bool arrow = true)
        {
            m_lines.push_back(LeaderLine{ std::move(pts), arrow });
        }
        const std::vector<LeaderLine>& GetLeaderLines() const { return m_lines; }
        std::vector<LeaderLine>&       GetLeaderLines()       { return m_lines; }

        // ── 基线 / dogleg ──────────────────────────────────────────────────
        // landing:内容侧基线端点;doglegDir:由内容指回引线侧的单位方向。
        const Math::Point3& GetLanding() const { return m_landing; }
        void SetLanding(const Math::Point3& p) { m_landing = p; }
        const Math::Vec3& GetDoglegDir() const { return m_doglegDir; }
        void SetDoglegDir(const Math::Vec3& d) { m_doglegDir = d.Normalized(); }

        // ── 内容 ───────────────────────────────────────────────────────────
        MLeaderContent GetContentType() const  { return m_content; }
        void SetContentType(MLeaderContent c)   { m_content = c; }

        const std::string& GetText() const { return m_text; }
        void SetText(std::string t)        { m_text = std::move(t); m_content = MLeaderContent::MText; }

        uint32_t GetBlockId() const   { return m_blockId; }
        void SetBlockId(uint32_t id)  { m_blockId = id; m_content = MLeaderContent::Block; }

        // ── 样式 ───────────────────────────────────────────────────────────
        MLeaderStyle&       GetStyle()       { return m_style; }
        const MLeaderStyle& GetStyle() const { return m_style; }
        void SetStyle(const MLeaderStyle& s) { m_style = s; }
        MLeaderStyleID GetStyleId() const    { return m_styleId; }
        void SetStyleId(MLeaderStyleID id)   { m_styleId = id; }

        // ── Entity 接口 ────────────────────────────────────────────────────
        AABB GetBoundingBox() const override
        {
            AABB box = AABB::Empty();
            for (const auto& ln : m_lines)
                for (const auto& p : ln.points)
                    box.Expand(p);
            box.Expand(m_landing);
            box.Expand(DoglegStart());
            if (!m_text.empty())
            {
                const Math::Point3 t = m_landing;
                const double w = static_cast<double>(m_text.size()) * m_style.TextHeight * 0.6;
                box.Expand({ t.x + w, t.y + m_style.TextHeight, t.z });
            }
            if (m_lines.empty() && m_text.empty())
                box.Expand(m_landing);
            return box;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const auto& attr = GetAttr();
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor
                                      : isHovered  ? IDrawSink::kHoverColor
                                                   : ResolveDrawColor(sink);

            const Math::Point3 doglegStart = DoglegStart();

            // 各引线折线 + 末点接到 dogleg 起点 + 起端箭头。
            for (const auto& ln : m_lines)
            {
                if (ln.points.empty()) continue;
                for (size_t i = 1; i < ln.points.size(); ++i)
                    sink.DrawLine(ln.points[i - 1], ln.points[i], color, false);
                sink.DrawLine(ln.points.back(), doglegStart, color, false);

                if (ln.arrow && ln.points.size() >= 2)
                {
                    Math::Vec3 d = (ln.points[0] - ln.points[1]).Normalized();
                    if (d.LengthSq() < 0.5) d = { 1, 0, 0 };
                    EmitArrow(sink, ln.points[0], d, color);
                }
                else if (ln.arrow && ln.points.size() == 1)
                {
                    Math::Vec3 d = (ln.points[0] - doglegStart).Normalized();
                    if (d.LengthSq() < 0.5) d = { 1, 0, 0 };
                    EmitArrow(sink, ln.points[0], d, color);
                }
            }

            // dogleg 基线(自 dogleg 起点到 landing)。
            if (m_style.EnableLanding && m_style.EnableDogleg)
                sink.DrawLine(doglegStart, m_landing, color, false);

            // 内容文字。
            if (m_content == MLeaderContent::MText && !m_text.empty())
            {
                Math::Point3 origin{ m_landing.x + m_doglegDir.x * m_style.LandingGap,
                                     m_landing.y + m_doglegDir.y * m_style.LandingGap,
                                     m_landing.z };
                sink.EmitMText(origin, m_text, m_style.TextStyle,
                               m_style.TextHeight, 0.0, 0.0, color);
            }
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<MLeaderEntity>(newId, m_style);
            e->SetAttr(GetAttr());
            e->m_lines     = m_lines;
            e->m_landing   = m_landing;
            e->m_doglegDir = m_doglegDir;
            e->m_content   = m_content;
            e->m_text      = m_text;
            e->m_blockId   = m_blockId;
            e->m_styleId   = m_styleId;
            return e;
        }

        DECLARE_RUNTIME_TYPE(MLeaderEntity, Entity)

    private:
        // dogleg 起点(引线汇聚处)= landing 沿 doglegDir 退一个 DoglegLength。
        Math::Point3 DoglegStart() const
        {
            if (!m_style.EnableDogleg) return m_landing;
            return { m_landing.x + m_doglegDir.x * m_style.DoglegLength,
                     m_landing.y + m_doglegDir.y * m_style.DoglegLength,
                     m_landing.z };
        }

        void EmitArrow(IDrawSink& sink, const Math::Point3& tip,
                       const Math::Vec3& outDir, const Math::Color4& color) const
        {
            const double L = m_style.ArrowSize;
            const double halfW = L * (1.0 / 6.0);
            Math::Vec3 n{ -outDir.y, outDir.x, 0.0 };
            Math::Point3 base{ tip.x - outDir.x * L, tip.y - outDir.y * L, tip.z };
            Math::Point3 w1{ base.x + n.x * halfW, base.y + n.y * halfW, base.z };
            Math::Point3 w2{ base.x - n.x * halfW, base.y - n.y * halfW, base.z };
            sink.FillTriangle(tip, w1, w2, color);
        }

        std::vector<LeaderLine> m_lines;
        Math::Point3            m_landing;
        Math::Vec3              m_doglegDir{ 1, 0, 0 };   // 由内容指向引线侧
        MLeaderContent          m_content = MLeaderContent::MText;
        std::string             m_text;
        uint32_t                m_blockId = 0;
        MLeaderStyle            m_style;
        MLeaderStyleID          m_styleId = MLeaderStyle_StandardID;
    };
}

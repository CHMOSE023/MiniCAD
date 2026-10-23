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

    // 文字附着点(DXF MULTILEADER context 组码 171)：1=左, 2=中, 3=右。
    //
    // 注意：**绘制不依赖它**。实测 20/20（`1#、2#厂房电气_t3.dwg`）发现
    // `ContentBasePoint`（组码 10）**恒为文字块的左下角**，与附着点无关：
    //   171=3(Right) 时 CBP.x = TextLocation.x − 字宽
    //   171=1(Left)  时 CBP.x = TextLocation.x − LandingGap
    //   两种情况 CBP.y 都 == 引线 ConnectionPoint.y
    // 所以画文字直接用 ContentBasePoint（= m_textOrigin）即可，不需要量字宽；
    // 附着点只用于**写出时无损回填** 171。
    enum class MLeaderTextAnchor : uint8_t
    {
        Left   = 1,
        Center = 2,
        Right  = 3
    };

    // 文字附着方式(DXF MULTILEADER context 组码 174/175 = TextAttachmentType)。
    // 这里只关心"是否属于**下划线族**"：
    //     5 = BottomLine                            （文字底线）
    //     6 = BottomOfTopLineUnderlineBottomLine    （首行底线 + 末行下划线）
    //     7 = BottomOfTopLineUnderlineTopLine
    //     8 = BottomofTopLineUnderlineAll
    // 这四种 AutoCAD 都会沿文字下沿画一条**通长下划线**，多重引线的基线
    // （dogleg + landing）就是这条下划线的一部分。
    inline constexpr bool IsUnderlineAttachment(int v) { return v >= 5 && v <= 8; }

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

        // 编辑用：landing 移到 p，文字块跟着平移（平移、拉伸、夹点、撤销快照都走这里）。
        // SetLanding 只改 landing 本身，供导入 / 读档按字段赋值。
        void MoveLanding(const Math::Point3& p)
        {
            if (m_hasTextOrigin)
                m_textOrigin = m_textOrigin + (p - m_landing);
            m_landing = p;
        }

        // 编辑用：改 dogleg 方向（旋转、镜像）。文字始终水平，内容从 landing 一侧换到另一侧时，
        // 文字块以 landing 为轴左右翻过去；翻两次回到原处，撤销按反向变换也能复原。
        void TurnDogleg(const Math::Vec3& d)
        {
            const bool wasLeft = m_doglegDir.x > 0.0;   // doglegDir 指向引线 ⇒ x > 0 时内容在左
            SetDoglegDir(d);
            if (m_hasTextOrigin && wasLeft != (m_doglegDir.x > 0.0))
                m_textOrigin.x = 2.0 * m_landing.x - m_textOrigin.x - EstimateTextWidth();
        }

        // ── 文字块左下角（AutoCAD 的 ContentBasePoint，组码 10）──────────
        // **绘制就用它**，不需要量字宽：
        //   实测 20/20（`1#、2#厂房电气_t3.dwg`）ContentBasePoint 恒为文字块的
        //   左下角，且 .y 恒等于引线的 ConnectionPoint.y（即 dogleg 所在的水平线）：
        //     171=1(Left)  时 CBP.x = doglegEnd.x                （文字向右展开）
        //     171=3(Right) 时 CBP.x = doglegEnd.x − 字宽          （文字向左展开）
        //   早先的实现把文字画在 landing + doglegDir×LandingGap 上，对 171=3
        //   的那批会整体**多走一个文字宽**（本图约 1700–2200，是字高的 4~5 倍），
        //   且全部偏高 1.2 字高 —— 这正是"文字位置与 CAD 出入较大"的原因。
        const Math::Point3& GetTextOrigin() const { return m_textOrigin; }
        void SetTextOrigin(const Math::Point3& p) { m_textOrigin = p; m_hasTextOrigin = true; }
        void ClearTextOrigin()                    { m_textOrigin = {}; m_hasTextOrigin = false; }
        bool HasTextOrigin() const                { return m_hasTextOrigin; }

        // 实际使用的文字左下角。没有 ContentBasePoint（新建、旧档案）时按 landing 与 dogleg 方向推出：
        // 文字离 landing 一个 LandingGap，排在引线的另一侧；内容在左时右缘对齐，要用到文字宽 textWidth。
        Math::Point3 ResolveTextOrigin(double textWidth) const
        {
            if (m_hasTextOrigin)
                return m_textOrigin;
            const double gap = m_style.LandingGap;
            const double x = m_doglegDir.x > 0.0 ? m_landing.x - gap - textWidth : m_landing.x + gap;
            return { x, m_landing.y, m_landing.z };
        }
        Math::Point3 ResolveTextOrigin() const { return ResolveTextOrigin(EstimateTextWidth()); }

        // 文字宽的估算（不依赖字体）：按 UTF-8 逐字符，ASCII 0.6 字高、其余 1.0 字高。
        // 不能按 m_text.size()（那是字节数，一个汉字 3 字节）。
        double EstimateTextWidth() const
        {
            double w = 0.0;
            for (size_t i = 0; i < m_text.size(); )
            {
                const unsigned char c = static_cast<unsigned char>(m_text[i]);
                const int len = (c < 0x80) ? 1 : ((c >> 5) == 0x6) ? 2 : ((c >> 4) == 0xE) ? 3 : ((c >> 3) == 0x1E) ? 4 : 1;
                w += (len == 1 ? 0.6 : 1.0) * m_style.TextHeight;
                i += static_cast<size_t>(len);
            }
            return w;
        }

        // 文字附着点（组码 171）。**只用于无损写回**，绘制不依赖它
        // （ContentBasePoint 已经把附着方式体现为具体坐标了）。
        MLeaderTextAnchor GetTextAnchor() const { return m_textAnchor; }
        void SetTextAnchor(MLeaderTextAnchor a) { m_textAnchor = a; }

        // 文字左/右附着方式（context 组码 174 / 175）。**绘制要用**：
        // 属"下划线族"时，landing 这条横线就是文字的通长下划线，
        // 必须画到文字的**外侧**那条边（见 Draw）。
        int  GetTextLeftAttachment() const  { return m_textLeftAttachment; }
        void SetTextLeftAttachment(int v)   { m_textLeftAttachment = v; }
        int  GetTextRightAttachment() const { return m_textRightAttachment; }
        void SetTextRightAttachment(int v)  { m_textRightAttachment = v; }
        bool HasUnderlineAttachment() const
        {
            return IsUnderlineAttachment(m_textLeftAttachment) ||
                   IsUnderlineAttachment(m_textRightAttachment);
        }

        // 文字阅读方向（context 组码 13）。实测本图 20/20 恒为 (1,0)，
        // 与引线方向（LEADER{11}）**不是一回事**（14/20 那条两者相反）。
        // 仅用于无损写回，不参与绘制。
        const Math::Vec3& GetTextDirection() const { return m_textDirection; }
        void SetTextDirection(const Math::Vec3& d) { m_textDirection = d; }

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
                // 文字块：左下角在 ResolveTextOrigin()，文字水平（rotation = 0）向 +x 排，宽度用估算值
                const double w = EstimateTextWidth();
                const Math::Point3 t = ResolveTextOrigin(w);
                box.Expand({ t.x,     t.y,                      t.z });
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
                // 末点接到 dogleg 起点。两者重合时（正常情形：末点就是 ConnectionPoint，
                // 而 DoglegStart() 也落在同一点）跳过，避免产出零长段。
                if ((ln.points.back() - doglegStart).LengthSq() > 1e-18)
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
            // 起点与 landing 重合时（DoglegLength=0）不画，避免零长段。
            if (m_style.EnableLanding && m_style.EnableDogleg &&
                (doglegStart - m_landing).LengthSq() > 1e-18)
                sink.DrawLine(doglegStart, m_landing, color, false);

            // 内容文字。**位置用 m_textOrigin（ContentBasePoint，文字块左下角）**，
            // 不是 landing —— 实测 20/20 证明 CBP 恒为左下角：
            //     171=1(Left)  CBP.x = doglegEnd.x          （向右展开）
            //     171=3(Right) CBP.x = doglegEnd.x − 字宽    （向左展开）
            // 早期实现画在 landing + doglegDir×LandingGap 上，Right 的那批会整体右移一个文字宽。
            if (m_content == MLeaderContent::MText && !m_text.empty())
            {
                // 基线补到**文字边缘**。
                //
                // 两处都得对准，缺一处就会留空隙：
                //  ① AutoCAD 记录的文字块宽与我们渲染出的宽度不同（字体度量差异）——
                //     PY-2-W04：AutoCAD 认为 1966.11，我们只画 1609.09 → 缺口 357.43。
                //  ② 即便用我们**自己的**宽度，也得分清"步进宽度"与"墨迹宽度"：
                //     步进宽度含字形右侧留白，实测 45~133 单位（0.11~0.44 个字高），
                //     画到步进边会离最后一个字还差这么一段 —— 用户报的
                //     「端点还是没在文字的外侧」就是这个。
                //
                // 所以优先用**墨迹边界**；取不到才退回步进宽度。
                // 文字在 Draw 里以 rotation=0 送出 ⇒ 沿自身 +x 展开，与 doglegDir 无关。
                double textMinX = 0.0, textMaxX = 0.0;
                bool haveInk = sink.MeasureMTextInkBounds(m_text, m_style.TextStyle,
                                                          m_style.TextHeight, 0.0,
                                                          textMinX, textMaxX);
                if (!haveInk)
                {
                    const double w = sink.MeasureMTextWidth(m_text, m_style.TextStyle,
                                                            m_style.TextHeight, 0.0);
                    if (w > 0.0) { textMinX = 0.0; textMaxX = w; haveInk = true; }
                }
                // 没有 ContentBasePoint 时，内容在左要按我们自己量出的宽度右对齐
                const Math::Point3 origin = ResolveTextOrigin(haveInk ? textMaxX : EstimateTextWidth());
                sink.EmitMText(origin, m_text, m_style.TextStyle,
                               m_style.TextHeight, 0.0, 0.0, color);
                if (haveInk)
                {
                    const double t0 = origin.x + textMinX;   // 文字墨迹左缘
                    const double t1 = origin.x + textMaxX;   // 文字墨迹右缘
                    const double lx = m_landing.x;           // 基线外端（引线侧）
                    // 横线该停在哪一端，取决于附着方式（组码 174/175）：
                    //
                    //  · **下划线族（5~8）**：landing 这条横线就是文字的下划线，
                    //    必须横穿整个文字、终止在**外侧**（远离引线）那条边。
                    //    实测本图 20/20 都是 174=175=6，且 landing.y ≡ CBP.y（Δ=0.0000）
                    //    —— landing 正落在文字底线上，下划线才画得出来。
                    //  · 普通族：横线只补到文字的**近侧**边缘即可（本来就是引线端点。
                    //    171=1 时 landing.x 恰等于文字左缘，无需再画）。
                    // 注意：变量名不能叫 `far` —— MSVC 仍把它当旧式关键字展开成空，
                    // 会报一串莫名其妙的 C2059「语法错误: =」。
                    double endX = 0.0;
                    bool   doDraw = false;
                    if (HasUnderlineAttachment())
                    {
                        if (lx >= t1)      { endX = t0; doDraw = true; }   // 引线在右，画到左缘
                        else if (lx <= t0) { endX = t1; doDraw = true; }   // 引线在左，画到右缘
                    }
                    else
                    {
                        if (lx > t1)       { endX = t1; doDraw = true; }
                        else if (lx < t0)  { endX = t0; doDraw = true; }
                    }
                    if (doDraw && std::abs(endX - lx) > 1e-12)
                        sink.DrawLine(m_landing, { endX, origin.y, origin.z }, color, false);
                }
            }
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<MLeaderEntity>(newId, m_style);
            e->SetAttr(GetAttr());
            e->m_lines     = m_lines;
            e->m_landing   = m_landing;
            e->m_doglegDir = m_doglegDir;
            e->m_textOrigin = m_textOrigin;
            e->m_hasTextOrigin = m_hasTextOrigin;
            e->m_textAnchor = m_textAnchor;
            e->m_textLeftAttachment  = m_textLeftAttachment;
            e->m_textRightAttachment = m_textRightAttachment;
            e->m_textDirection = m_textDirection;
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
        // 文字块左下角（AutoCAD ContentBasePoint / 组码 10）。
        // 未设置（m_hasTextOrigin = false）时由 ResolveTextOrigin 按 landing 推出（新建、旧档案）。
        Math::Point3            m_textOrigin;
        bool                    m_hasTextOrigin = false;
        MLeaderTextAnchor       m_textAnchor = MLeaderTextAnchor::Left;
        // 文字左/右附着方式（context 组码 174 / 175）。
        // 默认 -1 = "未设置"，此时按**普通族**处理（保持旧行为，不改变既有图纸观感）。
        int                     m_textLeftAttachment  = -1;
        int                     m_textRightAttachment = -1;
        Math::Vec3              m_textDirection{ 1, 0, 0 };   // context 组码 13
        MLeaderContent          m_content = MLeaderContent::MText;
        std::string             m_text;
        uint32_t                m_blockId = 0;
        MLeaderStyle            m_style;
        MLeaderStyleID          m_styleId = MLeaderStyle_StandardID;
    };
}

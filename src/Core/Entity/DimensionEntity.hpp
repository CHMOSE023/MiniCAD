#pragma once
#include "Entity.hpp"
#include "PolylineEntity.hpp"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/Math/MathUtils.hpp"
#include "Core/Math/Constants.hpp"
#include "Core/GeomKernel/AABB.hpp"
#include <string>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <memory>

namespace MiniCAD
{
    // 命名标注样式表 ID(对应 DXF DIMSTYLE,组码 3)。0 = 内置 "Standard"。
    // 头文件不依赖 DimStyleTable,避免循环包含;真正的表在 Core/Document/DimStyleTable.*。
    using DimStyleID = uint32_t;
    inline constexpr DimStyleID DimStyle_StandardID = 0;

    // 尺寸线终端形式
    //   Arrow   —— 实心箭头，用于机械制图（GB/T 4458.4）
    //   Oblique —— 45° 中粗斜短线，用于房屋建筑制图（GB/T 50001）
    enum class DimTerminator : uint8_t
    {
        Arrow = 0,
        Oblique
    };

    // 标注类型(对应 DXF DIMENSION 组码 70 低 4 位的语义子集)。
    //   Aligned   —— 对齐线性:尺寸线平行于两标注点连线
    //   Linear    —— 转角线性:尺寸线沿给定角度(水平/垂直/任意),测量该方向上的投影长
    //   Angular   —— 角度:顶点 + 两条边,在弧上标注夹角
    //   Radius    —— 半径:圆心 → 圆周,文字前缀 R
    //   Diameter  —— 直径:贯穿圆心两端点,文字前缀 ⌀
    //   Ordinate  —— 坐标:单个特征点相对原点的 X 或 Y 基准坐标
    //   ArcLength —— 弧长:圆弧的弧长,标注弧与被测弧同心,文字前缀 ⌒
    //   JoggedRadius —— 折弯半径:大圆弧的圆心在图外时,尺寸线从替代圆心经折弯连到弧上,文字前缀 R
    // 新类型只追加在末尾:存档里按数值保存。
    enum class DimType : uint8_t
    {
        Aligned = 0,
        Linear,
        Angular,
        Radius,
        Diameter,
        Ordinate,
        ArcLength,
        JoggedRadius
    };

    // 坐标标注测量的基准轴(仅 DimType::Ordinate 用)。
    enum class OrdinateAxis : uint8_t
    {
        X = 0,   // 测量 X 坐标(尺寸界线大致沿 Y 引出)
        Y        // 测量 Y 坐标
    };

    // 关联标注：标注的一个定义点 ↔ 某个几何对象上的特征点（同 AutoCAD DIMASSOC = 2）。
    // 几何对象被修改后，定义点跟到特征点的新位置（见 Document/DimAssoc）。
    //   Endpoint / Midpoint / Quadrant —— 对象的第 Index 个端点 / 中点 / 象限点（与对象捕捉的枚举顺序一致）
    //   Center       —— 圆 / 圆弧 / 椭圆的圆心
    //   Nearest      —— 曲线上参数 Param 处的点（ICurve 参数：直线为比例，圆 / 圆弧为角度，多段线为分段参数）
    //   Intersection —— 对象 Id 与 Id2 的交点（取离上次位置最近的一个）；Index = 1 时两条直线按无限长求交
    struct DimAssocRef
    {
        enum class Kind : uint8_t { None = 0, Endpoint, Midpoint, Quadrant, Center, Nearest, Intersection };

        Kind         RefKind = Kind::None;
        uint64_t     Id      = 0;
        uint64_t     Id2     = 0;
        int32_t      Index   = 0;
        double       Param   = 0.0;
        Math::Point3 Last;            // 上次同步时特征点的位置：用来区分「几何变了」还是「标注自己被改了」

        bool IsValid() const { return RefKind != Kind::None && Id != 0; }
    };

    // 可关联的定义点
    enum class DimAssocSlot : uint8_t { P1 = 0, P2, Center, Count };

    // 尺寸标注样式（单位与图形坐标一致，按 mm 设计；常用字高 3.5 / 5）
    // 默认值取自中国工程制图国家标准的常用取值。
    struct DimStyle
    {
        double        TextHeight      = 3.5;               // 尺寸数字字高（GB 常用 3.5 / 5）
        double        ArrowSize       = 3.5;               // 终端长度（箭头长 / 斜线长）
        double        ExtLineExtend   = 2.5;               // 尺寸界线超出尺寸线（GB 规定 2~3）
        double        ExtLineOffset   = 1.5;               // 尺寸界线起点与标注点的间隙
        double        TextGap         = 1.0;               // 尺寸数字与尺寸线的间距
        double        BaselineSpacing = 7.0;               // 基线标注相邻尺寸线间距（GB 7~10）
        int           Precision       = 0;                 // 线性小数位数（机械常用 0，单位 mm 不标）
        int           AnglePrecision  = 0;                 // 角度小数位数
        DimTerminator Terminator      = DimTerminator::Arrow;
        uint32_t      TextStyle       = 0;                 // 尺寸数字的文字样式（文字样式表 ID）
    };

    // 尺寸标注实体。涵盖 DXF 全部常见标注子类型(见 DimType)。
    //
    // 各类型对成员点的语义(均为 WCS):
    //   · Aligned / Linear : m_p1 / m_p2 = 两标注点(尺寸界线起点);
    //                        m_dimLinePoint = 尺寸线经过点。Linear 另用 m_linearAngle 指定尺寸线方向。
    //   · Angular          : m_centerPoint = 顶点;m_p1 / m_p2 = 两条边上各一点;
    //                        m_dimLinePoint = 标注弧经过点(定半径与标注侧)。
    //   · Radius           : m_centerPoint = 圆心;m_p1 = 圆周上的箭头落点。
    //   · Diameter         : m_p1 / m_p2 = 直径两端点(圆心取中点)。
    //   · Ordinate         : m_p1 = 特征点;m_dimLinePoint = 引线端点(文字位置);m_ordinateAxis 选 X/Y。
    //   · ArcLength        : m_centerPoint = 圆心;m_p1 / m_p2 = 弧两端;m_dimLinePoint = 标注弧经过点
    //                        (定标注弧半径;被测弧取 p1、p2 之间经过该点方向的那一段,与 Angular 同规则)。
    //   · JoggedRadius     : m_centerPoint = 真实圆心(只用于测量);m_p1 = 弧上箭头落点;
    //                        m_p2 = 替代圆心(尺寸线起点);m_dimLinePoint = 折弯位置。
    //
    // 组成均符合 GB 制图规范(尺寸界线 / 尺寸线 / 终端 / 尺寸数字)。
    class DimensionEntity : public Entity
    {
    public:
        // 兼容旧调用:默认构造对齐线性标注。
        DimensionEntity(ObjectID id,
                        const Math::Point3& p1,
                        const Math::Point3& p2,
                        const Math::Point3& dimLinePoint,
                        const DimStyle&     style = {})
            : Entity(id)
            , m_p1(p1)
            , m_p2(p2)
            , m_dimLinePoint(dimLinePoint)
            , m_style(style)
        {}

        // ── 工厂(各标注子类型)──────────────────────────────────────────────
        static std::unique_ptr<DimensionEntity> MakeAligned(
            ObjectID id, const Math::Point3& p1, const Math::Point3& p2,
            const Math::Point3& dimLinePoint, const DimStyle& style = {})
        {
            return std::make_unique<DimensionEntity>(id, p1, p2, dimLinePoint, style);
        }

        // angle: 尺寸线方向(弧度);0 = 水平,HalfPI = 垂直。
        static std::unique_ptr<DimensionEntity> MakeLinear(
            ObjectID id, const Math::Point3& p1, const Math::Point3& p2,
            const Math::Point3& dimLinePoint, double angle, const DimStyle& style = {})
        {
            auto e = std::make_unique<DimensionEntity>(id, p1, p2, dimLinePoint, style);
            e->m_type        = DimType::Linear;
            e->m_linearAngle = angle;
            return e;
        }

        static std::unique_ptr<DimensionEntity> MakeAngular(
            ObjectID id, const Math::Point3& vertex,
            const Math::Point3& p1, const Math::Point3& p2,
            const Math::Point3& arcPoint, const DimStyle& style = {})
        {
            auto e = std::make_unique<DimensionEntity>(id, p1, p2, arcPoint, style);
            e->m_type        = DimType::Angular;
            e->m_centerPoint = vertex;
            return e;
        }

        static std::unique_ptr<DimensionEntity> MakeRadius(
            ObjectID id, const Math::Point3& center, const Math::Point3& onCircle,
            const DimStyle& style = {})
        {
            auto e = std::make_unique<DimensionEntity>(id, onCircle, onCircle, onCircle, style);
            e->m_type        = DimType::Radius;
            e->m_centerPoint = center;
            return e;
        }

        static std::unique_ptr<DimensionEntity> MakeDiameter(ObjectID id, const Math::Point3& end1, const Math::Point3& end2, const DimStyle& style = {})
        {
            Math::Point3 mid{ (end1.x + end2.x) * 0.5, (end1.y + end2.y) * 0.5, (end1.z + end2.z) * 0.5 };
            auto e = std::make_unique<DimensionEntity>(id, end1, end2, mid, style);
            e->m_type        = DimType::Diameter;
            e->m_centerPoint = mid;
            return e;
        }

        static std::unique_ptr<DimensionEntity> MakeOrdinate(ObjectID id, const Math::Point3& feature, const Math::Point3& leaderEnd, OrdinateAxis axis, const DimStyle& style = {})
        {
            auto e = std::make_unique<DimensionEntity>(id, feature, feature, leaderEnd, style);
            e->m_type         = DimType::Ordinate;
            e->m_ordinateAxis = axis;
            return e;
        }

        // 弧长:arcStart / arcEnd 为弧两端,arcPoint 为标注弧经过点(其方向须落在被测弧的角度范围内)。
        static std::unique_ptr<DimensionEntity> MakeArcLength(
            ObjectID id, const Math::Point3& center,
            const Math::Point3& arcStart, const Math::Point3& arcEnd,
            const Math::Point3& arcPoint, const DimStyle& style = {})
        {
            auto e = std::make_unique<DimensionEntity>(id, arcStart, arcEnd, arcPoint, style);
            e->m_type        = DimType::ArcLength;
            e->m_centerPoint = center;
            return e;
        }

        // 折弯半径:center 为真实圆心,onArc 为弧上箭头落点,overrideCenter 为替代圆心,jogPoint 为折弯位置。
        static std::unique_ptr<DimensionEntity> MakeJoggedRadius(
            ObjectID id, const Math::Point3& center, const Math::Point3& onArc,
            const Math::Point3& overrideCenter, const Math::Point3& jogPoint, const DimStyle& style = {})
        {
            auto e = std::make_unique<DimensionEntity>(id, onArc, overrideCenter, jogPoint, style);
            e->m_type        = DimType::JoggedRadius;
            e->m_centerPoint = center;
            return e;
        }

        // ── 基线 / 连续标注(由已有线性标注派生一条续接的线性标注)──────────────
        // 连续标注:与 prev 首尾相接,新尺寸界线起点取 prev 的第二点,尺寸线与 prev 共线。
        static std::unique_ptr<DimensionEntity> MakeContinuous( ObjectID id, const DimensionEntity& prev, const Math::Point3& nextPoint)
        {
            auto e = std::make_unique<DimensionEntity>(
                id, prev.m_p2, nextPoint, prev.m_dimLinePoint, prev.m_style);
            e->m_type        = prev.m_type == DimType::Linear ? DimType::Linear : DimType::Aligned;
            e->m_linearAngle = prev.m_linearAngle;
            e->SetAttr(prev.GetAttr());
            return e;
        }

        // 基线标注:与 prev 共用第一标注点,尺寸线在外侧再偏移 index 个 BaselineSpacing。
        static std::unique_ptr<DimensionEntity> MakeBaseline(ObjectID id, const DimensionEntity& prev, const Math::Point3& nextPoint, int index = 1)
        {
            Geometry g = prev.ComputeLinearGeometry();
            const double step = prev.m_style.BaselineSpacing * (index < 1 ? 1 : index);
            Math::Point3 dimPt = prev.m_dimLinePoint + g.normal * step;

            auto e = std::make_unique<DimensionEntity>(id, prev.m_p1, nextPoint, dimPt, prev.m_style);
            e->m_type        = prev.m_type == DimType::Linear ? DimType::Linear : DimType::Aligned;
            e->m_linearAngle = prev.m_linearAngle;
            e->SetAttr(prev.GetAttr());
            return e;
        }

        // --- 赋值 ---
        void SetType(DimType t)                      { m_type = t; }
        void SetP1(const Math::Point3& p)            { m_p1 = p; }
        void SetP2(const Math::Point3& p)            { m_p2 = p; }
        void SetDimLinePoint(const Math::Point3& p)  { m_dimLinePoint = p; }
        void SetCenterPoint(const Math::Point3& p)   { m_centerPoint = p; }
        void SetLinearAngle(double a)                { m_linearAngle = a; }
        void SetOrdinateAxis(OrdinateAxis a)         { m_ordinateAxis = a; }
        void SetStyle(const DimStyle& s)             { m_style = s; }
        void SetStyleId(DimStyleID id)               { m_styleId = id; }
        // 文字替代：空串表示使用自动测量值；可填 "<>" 之外的任意文本（如带公差）
        void SetTextOverride(const std::string& t)   { m_textOverride = t; }
        // 文字位置：设定后脱离尺寸线自动定位，可自由放置
        void SetTextPosition(const Math::Point3& p)  { m_textPosition = p; m_useDefaultTextPos = false; }
        void ResetTextPosition()                     { m_useDefaultTextPos = true; }

        // --- 读取 ---
        DimType             GetType() const          { return m_type; }
        const Math::Point3& GetP1() const            { return m_p1; }
        const Math::Point3& GetP2() const            { return m_p2; }
        const Math::Point3& GetDimLinePoint() const  { return m_dimLinePoint; }
        const Math::Point3& GetCenterPoint() const   { return m_centerPoint; }
        double              GetLinearAngle() const   { return m_linearAngle; }
        OrdinateAxis        GetOrdinateAxis() const  { return m_ordinateAxis; }
        DimStyle&           GetStyle()               { return m_style; }
        const DimStyle&     GetStyle() const         { return m_style; }
        DimStyleID          GetStyleId() const       { return m_styleId; }
        const std::string&  GetTextOverride() const  { return m_textOverride; }
        bool IsUsingDefaultTextPosition() const      { return m_useDefaultTextPos; }

        // ── 夹点编辑辅助 ──────────────────────────────────────────────────
        // 暴露尺寸线(或标注弧)端点与文字基点，供 DimensionGripHandler
        // 放置夹点 / 绘制拖拽 Ghost 使用（内部的几何投影不必在外部重算）。
        // 线性/对齐 → 尺寸线两端;角度 → 标注弧两端(落在两条尺寸界线上)。
        Math::Point3 DimLineP1() const
        {
            if (IsArcType()) return AngularArcEnd(m_p1);
            if (m_type == DimType::JoggedRadius) return ComputeJog().a;
            return ComputeLinearGeometry().d1;
        }
        Math::Point3 DimLineP2() const
        {
            if (IsArcType()) return AngularArcEnd(m_p2);
            if (m_type == DimType::JoggedRadius) return ComputeJog().b;
            return ComputeLinearGeometry().d2;
        }
        Math::Point3 TextPosition() const { return m_useDefaultTextPos ? TextAnchor() : m_textPosition; }
        Math::Point3 DefaultTextPosition() const { return TextAnchor(); }

        // ── 关联 ──────────────────────────────────────────────────────────
        const DimAssocRef& GetAssoc(DimAssocSlot s) const   { return m_assoc[static_cast<size_t>(s)]; }
        void SetAssoc(DimAssocSlot s, const DimAssocRef& r) { m_assoc[static_cast<size_t>(s)] = r; }
        void ClearAssoc(DimAssocSlot s)                     { m_assoc[static_cast<size_t>(s)] = {}; }
        void ClearAllAssoc()                                { for (auto& r : m_assoc) r = {}; }
        bool IsAssociative() const
        {
            for (const auto& r : m_assoc) if (r.IsValid()) return true;
            return false;
        }
        // 定义点按槽位读写
        Math::Point3 SlotPoint(DimAssocSlot s) const
        {
            switch (s)
            {
            case DimAssocSlot::P1:     return m_p1;
            case DimAssocSlot::P2:     return m_p2;
            default:                   return m_centerPoint;
            }
        }
        void SetSlotPoint(DimAssocSlot s, const Math::Point3& p)
        {
            switch (s)
            {
            case DimAssocSlot::P1:     m_p1 = p; break;
            case DimAssocSlot::P2:     m_p2 = p; break;
            default:                   m_centerPoint = p; break;
            }
        }

        // 用另一条标注的全部数据（含关联）覆盖自身，ID 不变；关联更新原位改写时用，不改变绘制次序
        void CopyFrom(const DimensionEntity& o)
        {
            SetAttr(o.GetAttr());
            m_type              = o.m_type;
            m_p1                = o.m_p1;
            m_p2                = o.m_p2;
            m_dimLinePoint      = o.m_dimLinePoint;
            m_centerPoint       = o.m_centerPoint;
            m_linearAngle       = o.m_linearAngle;
            m_ordinateAxis      = o.m_ordinateAxis;
            m_style             = o.m_style;
            m_styleId           = o.m_styleId;
            m_textOverride      = o.m_textOverride;
            m_textPosition      = o.m_textPosition;
            m_useDefaultTextPos = o.m_useDefaultTextPos;
            m_assoc             = o.m_assoc;
        }

        // 整体平移：所有定义点同时位移（用于"移动整条标注"夹点 / 命令）。
        void Translate(const Math::Vec3& d)
        {
            m_p1           = m_p1 + d;
            m_p2           = m_p2 + d;
            m_dimLinePoint = m_dimLinePoint + d;
            m_centerPoint  = m_centerPoint + d;
            if (!m_useDefaultTextPos)
                m_textPosition = m_textPosition + d;
        }

        // 测量值(单位随类型):线性 = 长度;角度 = 弧度;半径/直径 = 长度;坐标 = 轴坐标值。
        double Measurement() const
        {
            switch (m_type)
            {
            case DimType::Linear:
            {
                Math::Vec3 u{ std::cos(m_linearAngle), std::sin(m_linearAngle), 0.0 };
                Math::Vec3 d = m_p2 - m_p1;
                return std::abs(d.x * u.x + d.y * u.y + d.z * u.z);
            }
            case DimType::Angular:
            {
                Math::Vec3 a = (m_p1 - m_centerPoint).Normalized();
                Math::Vec3 b = (m_p2 - m_centerPoint).Normalized();
                double dot = a.x * b.x + a.y * b.y + a.z * b.z;
                dot = dot < -1.0 ? -1.0 : (dot > 1.0 ? 1.0 : dot);
                return std::acos(dot);
            }
            case DimType::ArcLength:
                return (m_p1 - m_centerPoint).Length() * std::abs(ArcSpan());
            case DimType::Radius:
            case DimType::JoggedRadius:
                return (m_p1 - m_centerPoint).Length();
            case DimType::Diameter:
                return (m_p2 - m_p1).Length();
            case DimType::Ordinate:
                return m_ordinateAxis == OrdinateAxis::X ? m_p1.x : m_p1.y;
            case DimType::Aligned:
            default:
                return (m_p2 - m_p1).Length();
            }
        }

        // --- Entity 接口 ---
        AABB GetBoundingBox() const override
        {
            AABB box = AABB::Empty();
            box.Expand(m_p1);
            box.Expand(m_p2);
            if (m_type != DimType::JoggedRadius)
                box.Expand(m_centerPoint);
            box.Expand(m_dimLinePoint);
            if (IsArcType())        // 标注弧外凸部分
            {
                box.Expand(AngularArcEnd(m_p1));
                box.Expand(AngularArcEnd(m_p2));
                const Math::Point3& c = m_centerPoint;
                const double r = (m_dimLinePoint - c).Length() + m_style.ExtLineExtend;
                const double a1 = std::atan2(m_p1.y - c.y, m_p1.x - c.x), span = ArcSpan();
                for (int i = 0; i <= 8; ++i)
                {
                    const double a = a1 + span * i / 8.0;
                    box.Expand({ c.x + std::cos(a) * r, c.y + std::sin(a) * r, c.z });
                }
            }

            // 文字所占范围(粗略按字数估算宽度,留半字高余量)
            const std::string txt = FormatText();
            const double textW = static_cast<double>(txt.size()) * m_style.TextHeight * 0.6;
            const double halfW = textW * 0.5 + m_style.TextHeight;
            const Math::Point3 tc = TextPosition();
            box.Expand({ tc.x - halfW, tc.y - halfW, tc.z });
            box.Expand({ tc.x + halfW, tc.y + halfW, tc.z });
            return box;
        }

        void Draw(IDrawSink& sink, bool isSelected, bool isHovered) const override
        {
            const auto& attr = GetAttr();
            const Math::Color4& color = isSelected ? IDrawSink::kSelectionColor
                                      : isHovered  ? IDrawSink::kHoverColor
                                                   : ResolveDrawColor(sink);
            switch (m_type)
            {
            case DimType::Angular:  DrawAngular(sink, color);   break;
            case DimType::ArcLength: DrawArcLength(sink, color); break;
            case DimType::JoggedRadius: DrawJoggedRadius(sink, color); break;
            case DimType::Radius:   DrawRadius(sink, color);    break;
            case DimType::Diameter: DrawDiameter(sink, color);  break;
            case DimType::Ordinate: DrawOrdinate(sink, color);  break;
            case DimType::Aligned:
            case DimType::Linear:
            default:                DrawLinear(sink, color);    break;
            }
        }

        std::unique_ptr<Entity> Clone(ObjectID newId) const override
        {
            auto e = std::make_unique<DimensionEntity>(newId, m_p1, m_p2, m_dimLinePoint, m_style);
            e->SetAttr(GetAttr());
            e->m_type              = m_type;
            e->m_centerPoint       = m_centerPoint;
            e->m_linearAngle       = m_linearAngle;
            e->m_ordinateAxis      = m_ordinateAxis;
            e->m_styleId           = m_styleId;
            e->m_textOverride      = m_textOverride;
            e->m_textPosition      = m_textPosition;
            e->m_useDefaultTextPos = m_useDefaultTextPos;
            e->m_assoc             = m_assoc;     // 复制命令需经 DimAssoc::RemapCopy 改指向副本
            return e;
        }

        DECLARE_RUNTIME_TYPE(DimensionEntity, Entity)

    private:
        // 线性(对齐 / 转角)预计算几何
        struct Geometry
        {
            Math::Vec3   dir;          // 尺寸线方向单位向量
            Math::Vec3   normal;       // 偏移侧法线(自标注点指向尺寸线)单位向量
            Math::Vec3   normal2;      // 第二条尺寸界线的法线(转角标注两侧可不同)
            Math::Point3 d1, d2;       // 尺寸线端点
            Math::Point3 ext1Start, ext1End;   // 尺寸界线 1
            Math::Point3 ext2Start, ext2End;   // 尺寸界线 2
            Math::Point3 textPos;      // 文字居中基点(位于尺寸线上方 TextGap 处)
            double       textRotation = 0.0;
            double       measure = 0.0;
        };

        // 把 p 投影到「过 base、方向 u」的直线上。
        static Math::Point3 ProjectOnLine(const Math::Point3& p, const Math::Point3& base, const Math::Vec3& u)
        {
            Math::Vec3 w = p - base;
            double t = w.x * u.x + w.y * u.y + w.z * u.z;
            return base + u * t;
        }

        Geometry ComputeLinearGeometry() const
        {
            Geometry g;

            // 尺寸线方向:转角标注取给定角度,否则取两标注点连线方向。
            Math::Vec3 u;
            if (m_type == DimType::Linear)
            {
                u = Math::Vec3{ std::cos(m_linearAngle), std::sin(m_linearAngle), 0.0 };
            }
            else
            {
                Math::Vec3 d = m_p2 - m_p1;
                double len = d.Length();
                u = (len < Math::LengthEPS) ? Math::Vec3{ 1, 0, 0 } : (d / len);
            }
            g.dir = u;

            // 把两标注点投影到「过 DimLinePoint、方向 u」的尺寸线上。
            g.d1 = ProjectOnLine(m_p1, m_dimLinePoint, u);
            g.d2 = ProjectOnLine(m_p2, m_dimLinePoint, u);
            g.measure = (g.d2 - g.d1).Length();

            // 尺寸界线法线:由标注点指向尺寸线;退化时取 u 的左法线。
            Math::Vec3 e1 = g.d1 - m_p1; double e1l = e1.Length();
            Math::Vec3 e2 = g.d2 - m_p2; double e2l = e2.Length();
            Math::Vec3 leftN{ -u.y, u.x, 0.0 };
            g.normal  = (e1l > Math::LengthEPS) ? (e1 / e1l) : leftN;
            g.normal2 = (e2l > Math::LengthEPS) ? (e2 / e2l) : g.normal;

            g.ext1Start = m_p1 + g.normal  * m_style.ExtLineOffset;
            g.ext2Start = m_p2 + g.normal2 * m_style.ExtLineOffset;
            g.ext1End   = g.d1 + g.normal  * m_style.ExtLineExtend;
            g.ext2End   = g.d2 + g.normal2 * m_style.ExtLineExtend;

            Math::Point3 mid{
                (g.d1.x + g.d2.x) * 0.5,
                (g.d1.y + g.d2.y) * 0.5,
                (g.d1.z + g.d2.z) * 0.5
            };

            // 文字方向随尺寸线;GB 要求数字字头朝上,避免倒置(角度落在 (90°,270°) 时翻转)。
            double ang = std::atan2(u.y, u.x);
            if (ang > Math::HalfPI || ang <= -Math::HalfPI)
                ang += Math::PI;
            g.textRotation = ang;

            // 文字位于尺寸线上方(GB):"上方"按读字方向的上,即 textRotation 的 +90° 侧。
            // 原来沿 normal 偏移,标注放在标注点下方时 normal 朝下,文字会落到线下。
            // EmitDimText 的 center 是文字底边中点,故仅留 TextGap 间隙。
            Math::Vec3 textUp{ -std::sin(ang), std::cos(ang), 0.0 };
            g.textPos = mid + textUp * m_style.TextGap;
            return g;
        }

        void DrawLinear(IDrawSink& sink, const Math::Color4& color) const
        {
            Geometry g = ComputeLinearGeometry();

            // 尺寸界线(细实线)
            sink.DrawLine(g.ext1Start, g.ext1End, color, false);
            sink.DrawLine(g.ext2Start, g.ext2End, color, false);

            // 尺寸线(细实线)。斜线终端(建筑标注)时尺寸线两端各延长 ExtLineExtend。
            if (m_style.Terminator == DimTerminator::Oblique)
            {
                const double ext = m_style.ExtLineExtend;
                Math::Point3 l1{ g.d1.x - g.dir.x * ext, g.d1.y - g.dir.y * ext, g.d1.z };
                Math::Point3 l2{ g.d2.x + g.dir.x * ext, g.d2.y + g.dir.y * ext, g.d2.z };
                sink.DrawLine(l1, l2, color, false);

                EmitOblique(sink, g.d1, g.dir, g.normal, color);
                EmitOblique(sink, g.d2, g.dir, g.normal2, color);
            }
            else
            {
                sink.DrawLine(g.d1, g.d2, color, false);

                // 箭头指向尺寸界线(尖端在端点处,指向外侧)
                EmitArrow(sink, g.d1, -g.dir, g.normal, color);
                EmitArrow(sink, g.d2,  g.dir, g.normal2, color);
            }

            // 居中方向用读字方向(textRotation 可能比 dir 翻转了 π,
            // 用 dir 会把文字往反方向偏出去一个字宽)。
            const Math::Vec3 readDir{ std::cos(g.textRotation), std::sin(g.textRotation), 0.0 };
            const Math::Point3 tp = m_useDefaultTextPos ? g.textPos : m_textPosition;
            EmitDimText(sink, tp, readDir, g.textRotation, color);
        }

        void DrawAngular(IDrawSink& sink, const Math::Color4& color) const
        {
            const Math::Point3& c = m_centerPoint;
            Math::Vec3 d1 = (m_p1 - c).Normalized();
            Math::Vec3 d2 = (m_p2 - c).Normalized();
            if (d1.LengthSq() < 0.5) d1 = { 1, 0, 0 };
            if (d2.LengthSq() < 0.5) d2 = { 0, 1, 0 };

            const double r = (m_dimLinePoint - c).Length();
            double a1 = std::atan2(d1.y, d1.x);
            double a2 = std::atan2(d2.y, d2.x);
            double aD = std::atan2(m_dimLinePoint.y - c.y, m_dimLinePoint.x - c.x);

            // 选取从 a1 到 a2、且经过 aD 的弧:默认逆时针(span>0),否则顺时针(span<0)。
            double spanCCW = NormalizePositive(a2 - a1);   // (0, 2π) 逆时针跨度
            double toD     = NormalizePositive(aD - a1);
            double arcSpan = (toD <= spanCCW) ? spanCCW : (spanCCW - Math::TwoPI);

            Math::Point3 r1 = c + d1 * r;
            Math::Point3 r2 = c + d2 * r;

            // 尺寸界线:由标注点(边上)延伸到弧外侧。
            double l1 = (m_p1 - c).Length();
            double l2 = (m_p2 - c).Length();
            Math::Point3 s1 = c + d1 * (l1 + m_style.ExtLineOffset);
            Math::Point3 s2 = c + d2 * (l2 + m_style.ExtLineOffset);
            sink.DrawLine(s1, c + d1 * (r + m_style.ExtLineExtend), color, false);
            sink.DrawLine(s2, c + d2 * (r + m_style.ExtLineExtend), color, false);

            // 标注弧(分段折线近似)。
            const int segs = 48;
            Math::Point3 prev = r1;
            for (int i = 1; i <= segs; ++i)
            {
                double a = a1 + arcSpan * (static_cast<double>(i) / segs);
                Math::Point3 cur{ c.x + std::cos(a) * r, c.y + std::sin(a) * r, c.z };
                sink.DrawLine(prev, cur, color, false);
                prev = cur;
            }

            // 弧端箭头沿切向指向彼此(箭头朝弧内)。
            double sd = arcSpan >= 0.0 ? 1.0 : -1.0;
            Math::Vec3 t1{ -std::sin(a1) * sd, std::cos(a1) * sd, 0.0 };   // a1 端切向(指向 a2)
            Math::Vec3 t2{ std::sin(a2) * sd, -std::cos(a2) * sd, 0.0 };   // a2 端切向(指向 a1)
            EmitArrowAuto(sink, r1, t1, color);
            EmitArrowAuto(sink, r2, t2, color);

            // 文字置于弧中点外侧(与 AngularDefaultTextPos 同一计算)。
            const Math::Point3 tp = m_useDefaultTextPos ? AngularDefaultTextPos() : m_textPosition;
            EmitDimText(sink, tp, { 1, 0, 0 }, 0.0, color);
        }

        bool IsArcType() const { return m_type == DimType::Angular || m_type == DimType::ArcLength; }

        // 从 p1 方向转到 p2 方向、经过 m_dimLinePoint 方向的有符号角跨度(逆时针为正)。
        double ArcSpan() const
        {
            const Math::Point3& c = m_centerPoint;
            const double a1 = std::atan2(m_p1.y - c.y, m_p1.x - c.x);
            const double a2 = std::atan2(m_p2.y - c.y, m_p2.x - c.x);
            const double aD = std::atan2(m_dimLinePoint.y - c.y, m_dimLinePoint.x - c.x);
            const double spanCCW = NormalizePositive(a2 - a1);
            const double toD     = NormalizePositive(aD - a1);
            return (toD <= spanCCW) ? spanCCW : (spanCCW - Math::TwoPI);
        }

        // 弧长:尺寸界线沿径向从被测弧引到标注弧,标注弧与被测弧同心;标注弧在弧内侧时界线向内引。
        void DrawArcLength(IDrawSink& sink, const Math::Color4& color) const
        {
            const Math::Point3& c = m_centerPoint;
            Math::Vec3 d1 = (m_p1 - c).Normalized();
            Math::Vec3 d2 = (m_p2 - c).Normalized();
            if (d1.LengthSq() < 0.5) d1 = { 1, 0, 0 };
            if (d2.LengthSq() < 0.5) d2 = { 0, 1, 0 };

            const double R    = (m_p1 - c).Length();
            const double r    = (m_dimLinePoint - c).Length();
            const double side = r >= R ? 1.0 : -1.0;
            const double R2   = (m_p2 - c).Length();
            sink.DrawLine(c + d1 * (R  + side * m_style.ExtLineOffset), c + d1 * (r + side * m_style.ExtLineExtend), color, false);
            sink.DrawLine(c + d2 * (R2 + side * m_style.ExtLineOffset), c + d2 * (r + side * m_style.ExtLineExtend), color, false);

            const double a1   = std::atan2(d1.y, d1.x);
            const double a2   = std::atan2(d2.y, d2.x);
            const double span = ArcSpan();
            const int    segs = std::max(8, static_cast<int>(std::ceil(std::abs(span) / Math::TwoPI * 96.0)));
            Math::Point3 prev = c + d1 * r;
            for (int i = 1; i <= segs; ++i)
            {
                const double a = a1 + span * (static_cast<double>(i) / segs);
                Math::Point3 cur{ c.x + std::cos(a) * r, c.y + std::sin(a) * r, c.z };
                sink.DrawLine(prev, cur, color, false);
                prev = cur;
            }

            const double sd = span >= 0.0 ? 1.0 : -1.0;
            EmitArrowAuto(sink, c + d1 * r, Math::Vec3{ -std::sin(a1) * sd,  std::cos(a1) * sd, 0.0 }, color);
            EmitArrowAuto(sink, c + d2 * r, Math::Vec3{  std::sin(a2) * sd, -std::cos(a2) * sd, 0.0 }, color);

            const Math::Point3 tp = m_useDefaultTextPos ? AngularDefaultTextPos() : m_textPosition;
            EmitDimText(sink, tp, { 1, 0, 0 }, 0.0, color);
        }

        // 折弯半径几何:径向线(过圆心与箭头点)上取折弯点 a,替代圆心所在平行线上取 b,
        // b→a 为 45° 折弯段(沿径向错开的距离 = 两平行线间距)。
        struct JogGeometry
        {
            Math::Point3 o;       // 替代圆心(尺寸线起点)
            Math::Point3 b;       // 平行线上的折弯点
            Math::Point3 a;       // 径向线上的折弯点
            Math::Point3 tip;     // 弧上箭头点
            Math::Vec3   dir;     // 径向单位向量(圆心 → 箭头点)
        };

        JogGeometry ComputeJog() const
        {
            JogGeometry g;
            const Math::Point3& c = m_centerPoint;
            g.tip = m_p1;
            g.o   = m_p2;
            g.dir = (m_p1 - c).Normalized();
            if (g.dir.LengthSq() < 0.5) g.dir = { 1, 0, 0 };

            const double R     = (m_p1 - c).Length();
            const double tO    = Math::Dot(m_p2 - c, g.dir);
            const Math::Vec3 perp = (m_p2 - c) - g.dir * tO;          // 替代圆心偏离径向线的垂直分量
            const double w     = perp.Length();
            double tA = Math::Dot(m_dimLinePoint - c, g.dir);
            tA = std::clamp(tA, std::min(tO + w, R), R);              // 折弯落在替代圆心与弧之间
            g.a = c + g.dir * tA;
            g.b = c + g.dir * (tA - w) + perp;
            return g;
        }

        void DrawJoggedRadius(IDrawSink& sink, const Math::Color4& color) const
        {
            const JogGeometry g = ComputeJog();
            sink.DrawLine(g.o, g.b, color, false);
            sink.DrawLine(g.b, g.a, color, false);
            sink.DrawLine(g.a, g.tip, color, false);
            EmitArrow(sink, g.tip, g.dir, Math::Vec3{ -g.dir.y, g.dir.x, 0.0 }, color);

            const Math::Point3 tp = m_useDefaultTextPos ? JogDefaultTextPos(g) : m_textPosition;
            double rot = std::atan2(g.dir.y, g.dir.x);
            if (rot > Math::HalfPI || rot <= -Math::HalfPI) rot += Math::PI;
            EmitDimText(sink, tp, { std::cos(rot), std::sin(rot), 0.0 }, rot, color);
        }

        // 折弯半径默认文字位置:径向段(折弯点 → 箭头)中点上方
        Math::Point3 JogDefaultTextPos(const JogGeometry& g) const
        {
            double rot = std::atan2(g.dir.y, g.dir.x);
            if (rot > Math::HalfPI || rot <= -Math::HalfPI) rot += Math::PI;
            const Math::Vec3 up{ -std::sin(rot), std::cos(rot), 0.0 };
            const Math::Point3 mid{ (g.a.x + g.tip.x) * 0.5, (g.a.y + g.tip.y) * 0.5, (g.a.z + g.tip.z) * 0.5 };
            return mid + up * m_style.TextGap;
        }

        void DrawRadius(IDrawSink& sink, const Math::Color4& color) const
        {
            const Math::Point3& c  = m_centerPoint;
            const Math::Point3& pe = m_p1;
            Math::Vec3 d = (pe - c).Normalized();
            if (d.LengthSq() < 0.5) d = { 1, 0, 0 };

            sink.DrawLine(c, pe, color, false);              // 圆心 → 圆周
            EmitArrow(sink, pe, d, Math::Vec3{ -d.y, d.x, 0.0 }, color);

            // 文字置于尺寸线上方(读字方向的上侧),字头朝上避免倒置。
            Math::Point3 mid{ (c.x + pe.x) * 0.5, (c.y + pe.y) * 0.5, (c.z + pe.z) * 0.5 };
            double rot = std::atan2(d.y, d.x);
            if (rot > Math::HalfPI || rot <= -Math::HalfPI) rot += Math::PI;
            const Math::Vec3 readDir{ std::cos(rot), std::sin(rot), 0.0 };
            const Math::Vec3 up{ -std::sin(rot), std::cos(rot), 0.0 };
            Math::Point3 defTextPos = mid + up * m_style.TextGap;
            const Math::Point3 tp = m_useDefaultTextPos ? defTextPos : m_textPosition;
            EmitDimText(sink, tp, readDir, rot, color);
        }

        void DrawDiameter(IDrawSink& sink, const Math::Color4& color) const
        {
            const Math::Point3& a = m_p1;
            const Math::Point3& b = m_p2;
            Math::Point3 mid{ (a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5 };
            Math::Vec3 d = (b - a).Normalized();
            if (d.LengthSq() < 0.5) d = { 1, 0, 0 };

            sink.DrawLine(a, b, color, false);               // 贯穿圆心
            EmitArrow(sink, a, -d, Math::Vec3{ -d.y, d.x, 0.0 }, color);
            EmitArrow(sink, b,  d, Math::Vec3{ -d.y, d.x, 0.0 }, color);

            double rot = std::atan2(d.y, d.x);
            if (rot > Math::HalfPI || rot <= -Math::HalfPI) rot += Math::PI;
            const Math::Vec3 readDir{ std::cos(rot), std::sin(rot), 0.0 };
            const Math::Vec3 up{ -std::sin(rot), std::cos(rot), 0.0 };
            Math::Point3 defTextPos = mid + up * m_style.TextGap;
            const Math::Point3 tp = m_useDefaultTextPos ? defTextPos : m_textPosition;
            EmitDimText(sink, tp, readDir, rot, color);
        }

        void DrawOrdinate(IDrawSink& sink, const Math::Color4& color) const
        {
            const Math::Point3& f  = m_p1;             // 特征点
            const Math::Point3& le = m_dimLinePoint;   // 引线端点(文字)
            sink.DrawLine(f, le, color, false);        // 引线(GB 可折,简化为直引线)

            // 文字置于引线端点外侧少许(沿引线方向)。
            Math::Vec3 d = (le - f).Normalized();
            if (d.LengthSq() < 0.5) d = { 0, 1, 0 };
            Math::Point3 defTextPos = le + d * (m_style.TextGap + m_style.TextHeight * 0.5);
            const Math::Point3 tp = m_useDefaultTextPos ? defTextPos : m_textPosition;
            EmitDimText(sink, tp, { 1, 0, 0 }, 0.0, color);
        }

        // 角度标注:边方向与标注弧的交点(弧端,落在尺寸界线上)。
        Math::Point3 AngularArcEnd(const Math::Point3& edgePt) const
        {
            const Math::Point3& c = m_centerPoint;
            Math::Vec3 d = (edgePt - c).Normalized();
            if (d.LengthSq() < 0.5) d = { 1, 0, 0 };
            const double r = (m_dimLinePoint - c).Length();
            return c + d * r;
        }

        // 角度标注:默认文字位置(弧中点外侧,与 DrawAngular 一致)。
        Math::Point3 AngularDefaultTextPos() const
        {
            const Math::Point3& c = m_centerPoint;
            Math::Vec3 d1 = (m_p1 - c).Normalized(); if (d1.LengthSq() < 0.5) d1 = { 1, 0, 0 };
            Math::Vec3 d2 = (m_p2 - c).Normalized(); if (d2.LengthSq() < 0.5) d2 = { 0, 1, 0 };
            const double r = (m_dimLinePoint - c).Length();

            double a1 = std::atan2(d1.y, d1.x);
            double a2 = std::atan2(d2.y, d2.x);
            double aD = std::atan2(m_dimLinePoint.y - c.y, m_dimLinePoint.x - c.x);
            double spanCCW = NormalizePositive(a2 - a1);
            double toD     = NormalizePositive(aD - a1);
            double arcSpan = (toD <= spanCCW) ? spanCCW : (spanCCW - Math::TwoPI);

            double aM = a1 + arcSpan * 0.5;
            Math::Vec3 dM{ std::cos(aM), std::sin(aM), 0.0 };
            const bool inside = m_type == DimType::ArcLength && r < (m_p1 - c).Length();
            const double off = m_style.TextGap + m_style.TextHeight * 0.5;
            return c + dM * (inside ? r - off : r + off);
        }

        // 文字基点 anchor(用于包围盒估算):各类型给一个代表位置。
        Math::Point3 TextAnchor() const
        {
            switch (m_type)
            {
            case DimType::Angular:
            case DimType::ArcLength:
                return AngularDefaultTextPos();
            case DimType::JoggedRadius:
                return JogDefaultTextPos(ComputeJog());
            case DimType::Ordinate:
                return m_dimLinePoint;
            case DimType::Radius:
                return { (m_centerPoint.x + m_p1.x) * 0.5,
                         (m_centerPoint.y + m_p1.y) * 0.5,
                         (m_centerPoint.z + m_p1.z) * 0.5 };
            case DimType::Diameter:
                return m_centerPoint;
            case DimType::Aligned:
            case DimType::Linear:
            default:
                return ComputeLinearGeometry().textPos;
            }
        }

        // 把 [-π,π) 角差规整到 [0, 2π)。
        static double NormalizePositive(double a)
        {
            while (a < 0.0)        a += Math::TwoPI;
            while (a >= Math::TwoPI) a -= Math::TwoPI;
            return a;
        }

        // 自动测量值或文字替代,按类型加前/后缀并按精度格式化。
        std::string FormatText() const
        {
            if (!m_textOverride.empty())
                return m_textOverride;

            char buf[64];
            switch (m_type)
            {
            case DimType::Angular:
            {
                int prec = m_style.AnglePrecision < 0 ? 0 : m_style.AnglePrecision;
                double deg = Measurement() * (180.0 / Math::PI);
                std::snprintf(buf, sizeof(buf), "%.*f\xC2\xB0", prec, deg);   // 度符号 °(UTF-8)
                return std::string(buf);
            }
            case DimType::Radius:
            case DimType::JoggedRadius:
            {
                int prec = m_style.Precision < 0 ? 0 : m_style.Precision;
                std::snprintf(buf, sizeof(buf), "R%.*f", prec, Measurement());
                return std::string(buf);
            }
            case DimType::ArcLength:
            {
                int prec = m_style.Precision < 0 ? 0 : m_style.Precision;
                std::snprintf(buf, sizeof(buf), "\xE2\x8C\x92%.*f", prec, Measurement());  // 弧长符号 ⌒(UTF-8)
                return std::string(buf);
            }
            case DimType::Diameter:
            {
                int prec = m_style.Precision < 0 ? 0 : m_style.Precision;
                std::snprintf(buf, sizeof(buf), "\xE2\x8C\x80%.*f", prec, Measurement());  // 直径符号 ⌀(UTF-8)
                return std::string(buf);
            }
            case DimType::Ordinate:
            case DimType::Aligned:
            case DimType::Linear:
            default:
            {
                int prec = m_style.Precision < 0 ? 0 : m_style.Precision;
                std::snprintf(buf, sizeof(buf), "%.*f", prec, Measurement());
                return std::string(buf);
            }
            }
        }

        // 沿 along 方向居中放置尺寸数字(EmitMText 以左下角为原点)。
        void EmitDimText(IDrawSink& sink, const Math::Point3& center, const Math::Vec3& along, double rotation, const Math::Color4& color) const
        {
            const std::string txt = FormatText();
            if (txt.empty()) return;
            const double textW = static_cast<double>(txt.size()) * m_style.TextHeight * 0.6;
            Math::Point3 origin{
                center.x - along.x * textW * 0.5,
                center.y - along.y * textW * 0.5,
                center.z
            };
            sink.EmitMText(origin, txt, m_style.TextStyle,
                           m_style.TextHeight, rotation, 0.0, color);
        }

        // 实心箭头:尖端在 tip,沿 alongOut 方向(指向尺寸界线外侧)展开。
        void EmitArrow(IDrawSink& sink, const Math::Point3& tip, const Math::Vec3& alongOut, const Math::Vec3& normal, const Math::Color4& color) const
        {
            const double L = m_style.ArrowSize;
            const double halfW = L * (1.0 / 6.0);   // GB 箭头宽:长 ≈ 1:3
            Math::Point3 base{ tip.x - alongOut.x * L, tip.y - alongOut.y * L, tip.z };
            Math::Point3 w1{ base.x + normal.x * halfW, base.y + normal.y * halfW, base.z };
            Math::Point3 w2{ base.x - normal.x * halfW, base.y - normal.y * halfW, base.z };
            sink.FillTriangle(tip, w1, w2, color);
        }

        // 自动求法线的箭头(用于角度/半径/直径终端)。
        void EmitArrowAuto(IDrawSink& sink, const Math::Point3& tip,  const Math::Vec3& alongOut, const Math::Color4& color) const
        {
            Math::Vec3 d = alongOut.Normalized();
            if (d.LengthSq() < 0.5) d = { 1, 0, 0 };
            EmitArrow(sink, tip, d, Math::Vec3{ -d.y, d.x, 0.0 }, color);
        }

        // 45° 斜短线(建筑标注终端):过端点、方向为 (dir+normal)。
        // 以线宽 1 的 PolylineEntity 绘制,复用其粗线(三角形填充)渲染 → GB 中粗短斜线。
        void EmitOblique(IDrawSink& sink, const Math::Point3& at,  const Math::Vec3& dir, const Math::Vec3& normal,  const Math::Color4& color) const
        {
            Math::Vec3 obl = (dir + normal).Normalized();
            const double half = m_style.ArrowSize * 0.5;
            Math::Point3 a{ at.x - obl.x * half, at.y - obl.y * half, at.z };
            Math::Point3 b{ at.x + obl.x * half, at.y + obl.y * half, at.z };

            PolylineEntity tick(GetID(), std::vector<Math::Point3>{ a, b });
            EntityAttr attr;
            attr.Color = color;
            tick.SetAttr(attr);
            tick.SetWidth(1.0);   // 标注端线按 1.0 模型单位粗线绘制(默认值)
            tick.Draw(sink, false, false);
        }

        DimType      m_type = DimType::Aligned;
        Math::Point3 m_p1;
        Math::Point3 m_p2;
        Math::Point3 m_dimLinePoint;
        Math::Point3 m_centerPoint;                 // 角度顶点 / 半径·直径圆心
        double       m_linearAngle = 0.0;           // 转角线性尺寸线方向(弧度)
        OrdinateAxis m_ordinateAxis = OrdinateAxis::X;
        DimStyle     m_style;
        DimStyleID   m_styleId = DimStyle_StandardID;
        std::string  m_textOverride;
        Math::Point3 m_textPosition;                // 手动放置的文字位置（仅 !m_useDefaultTextPos 时有效）
        bool         m_useDefaultTextPos = true;    // true = 文字自动定位于尺寸线上
        std::array<DimAssocRef, static_cast<size_t>(DimAssocSlot::Count)> m_assoc{};   // 关联（按 DimAssocSlot）
    };
}

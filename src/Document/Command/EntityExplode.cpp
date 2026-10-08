#include "EntityExplode.h"
#include "EntityMirror.h"
#include "EntityRotate.h"
#include "EntityScale.h"
#include "EntityTranslate.h"
#include "Document/DimAssoc.h"

#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/BlockEntity.hpp"
#include "Core/Entity/ImageEntity.hpp"
#include "Core/Entity/SolidEntity.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Core/Math/Constants.hpp"
#include "Scene/LineTypeTable.h"

#include <cmath>

namespace MiniCAD
{
    namespace
    {
        void Fail(std::string* reason, const char* text)
        {
            if (reason) *reason = text;
        }

        std::unique_ptr<Entity> MakeLine(const Entity& src, const Math::Point3& a, const Math::Point3& b)
        {
            auto e = std::make_unique<LineEntity>(0, a, b);
            e->SetAttr(src.GetAttr());
            return e;
        }

        bool ExplodePolyline(const PolylineEntity& pl, std::vector<std::unique_ptr<Entity>>& out)
        {
            const Polyline& p = pl.GetPolyline();
            const int n = p.SegCount();
            if (n < 1) return false;

            for (int i = 0; i < n; ++i)
            {
                const Math::Point3& a = p.Points[static_cast<size_t>(i)];
                const Math::Point3& b = p.Points[static_cast<size_t>(i) + 1];
                if (p.SegIsArc(i))
                {
                    const auto g = Polyline::ComputeArc(a, b, p.SegBulge(i));
                    if (g.Radius > Math::LengthEPS)
                    {
                        // ArcEntity 只有逆时针弧：顺时针（SweepAngle < 0）的弧交换起止角
                        const double s0 = g.StartAngle, s1 = g.StartAngle + g.SweepAngle;
                        auto arc = g.SweepAngle >= 0
                            ? std::make_unique<ArcEntity>(0, g.Center, g.Radius, s0, s1)
                            : std::make_unique<ArcEntity>(0, g.Center, g.Radius, s1, s0);
                        arc->SetAttr(pl.GetAttr());
                        out.push_back(std::move(arc));
                        continue;
                    }
                }
                out.push_back(MakeLine(pl, a, b));
            }
            return true;
        }

        bool ExplodeRectangle(const RectangleEntity& re, std::vector<std::unique_ptr<Entity>>& out)
        {
            const Rectangle& r = re.GetRectangle();
            out.push_back(MakeLine(re, r.P1, r.P2));
            out.push_back(MakeLine(re, r.P2, r.P3));
            out.push_back(MakeLine(re, r.P3, r.P4));
            out.push_back(MakeLine(re, r.P4, r.P1));
            return true;
        }

        bool ExplodeInsert(const InsertEntity& ins, std::vector<std::unique_ptr<Entity>>& out, std::string* reason)
        {
            const BlockEntity* block = ins.GetBlock();
            if (!block)
            {
                Fail(reason, "块定义不存在");
                return false;
            }
            if (!ins.HasDefaultExtrusion())
            {
                Fail(reason, "块插入带挤出方向（不在 XY 平面），暂不支持分解");
                return false;
            }

            const Math::Vec3 sc = ins.GetScale();
            const double ax = std::abs(sc.x), ay = std::abs(sc.y);
            if (ax < 1e-12 || ay < 1e-12 || std::abs(ax - ay) > 1e-9 * std::max(ax, ay))
            {
                Fail(reason, "块插入的 X / Y 缩放不相等，分解会把圆变成椭圆，暂不支持");
                return false;
            }

            const Math::Point3 base = block->GetBasePoint();
            const EntityAttr& host = ins.GetAttr();
            const Math::Point3 origin{ 0, 0, 0 };
            const MirrorAxis flipX{ { 0, 0, 0 }, { 0, 1, 0 } };     // 沿 Y 轴翻转（X 缩放为负）
            const MirrorAxis flipY{ { 0, 0, 0 }, { 1, 0, 0 } };     // 沿 X 轴翻转（Y 缩放为负）

            const size_t before = out.size();
            for (int row = 0; row < ins.GetRowCount(); ++row)
                for (int col = 0; col < ins.GetColumnCount(); ++col)
                {
                    // 阵列偏移沿（旋转后的）列 / 行方向，与 InsertEntity::CellMatrix 一致
                    const double c = std::cos(ins.GetRotation()), s = std::sin(ins.GetRotation());
                    const double ox = ins.GetColumnSpacing() * col, oy = ins.GetRowSpacing() * row;
                    const Math::Vec3 cellPos{ ins.GetPosition().x + c * ox - s * oy,
                                              ins.GetPosition().y + s * ox + c * oy,
                                              ins.GetPosition().z };

                    for (const auto& src : block->GetEntities())
                    {
                        auto e = src->Clone(0);
                        DimAssoc::Strip(*e);         // 块里的标注关联的是块内对象，分解后不再关联

                        // BYBLOCK 的属性继承插入对象
                        EntityAttr a = e->GetAttr();
                        if (a.Color.Method == ColorMethod::ByBlock) a.Color = host.Color;
                        if (a.Lineweight == Lineweight::ByBlock)     a.Lineweight = host.Lineweight;
                        if (a.LineType == LineTypeTable::ByBlockID)  a.LineType = host.LineType;
                        e->SetAttr(a);

                        // 块坐标 → 世界坐标：减基点 → 翻转 → 缩放 → 旋转 → 平移（与 CellMatrix 同序）
                        TranslateEntityInPlace(*e, Math::Vec3{ -base.x, -base.y, -base.z });
                        if (sc.x < 0) MirrorEntityInPlace(*e, flipX);
                        if (sc.y < 0) MirrorEntityInPlace(*e, flipY);
                        if (std::abs(ax - 1.0) > 1e-12) ScaleEntityInPlace(*e, origin, ax);
                        if (std::abs(ins.GetRotation()) > 1e-12) RotateEntityInPlace(*e, origin, ins.GetRotation());
                        TranslateEntityInPlace(*e, cellPos);
                        out.push_back(std::move(e));
                    }
                }
            if (out.size() == before)
            {
                Fail(reason, "块里没有对象");
                return false;
            }
            return true;
        }
    }

    bool ExplodeEntity(const Entity& e, std::vector<std::unique_ptr<Entity>>& out, std::string* reason)
    {
        std::vector<std::unique_ptr<Entity>> result;
        bool ok = false;

        if (e.IsKindOf<InsertEntity>())
            ok = ExplodeInsert(static_cast<const InsertEntity&>(e), result, reason);
        else if (e.IsKindOf<PolylineEntity>() && !e.IsKindOf<WipeoutEntity>())
            ok = ExplodePolyline(static_cast<const PolylineEntity&>(e), result);
        else if (e.IsKindOf<RectangleEntity>() && !e.IsKindOf<ImageEntity>() && !e.IsKindOf<SolidEntity>())
            ok = ExplodeRectangle(static_cast<const RectangleEntity&>(e), result);
        else
            Fail(reason, "该类型的对象不能分解");

        if (!ok || result.empty())
            return false;
        for (auto& r : result)
            out.push_back(std::move(r));
        return true;
    }
}

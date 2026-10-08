// ── 关联标注测试：几何修改后标注跟随、一起变换、单独修改解除关联、删除、复制、撤销重做、序列化 ──
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Document/Command/AddEntityCommand.h"
#include "Document/Command/CopyCommand.h"
#include "Document/Command/DragEntitiesCommand.h"
#include "Document/Command/GeometryEditCommand.h"
#include "Document/Command/MoveCommand.h"
#include "Document/Command/RotateMoveCommand.h"
#include "Document/DimAssoc.h"
#include "Document/Document.h"
#include "Serialization/EntityIO.h"
#include "Serialization/JsonSerializer.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace MiniCAD;

namespace
{
    int g_failures = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok)
            ++g_failures;
    }

    bool Near(double a, double b, double eps = 1e-6) { return std::abs(a - b) <= eps; }
    bool Near(const Math::Point3& a, const Math::Point3& b, double eps = 1e-6) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps); }

    DimensionEntity& Dim(Scene& s, Object::ObjectID id) { return *static_cast<DimensionEntity*>(s.GetEntity(id)); }

    Object::ObjectID AddLine(Document& doc, Math::Point3 a, Math::Point3 b)
    {
        Scene& s = doc.GetScene();
        const auto id = s.NextObjectID();
        doc.GetCommandStack().Execute(std::make_unique<AddEntityCommand>(std::make_unique<LineEntity>(id, a, b)), s);
        return id;
    }

    void Add(Document& doc, std::unique_ptr<Entity> e)
    {
        doc.GetCommandStack().Execute(std::make_unique<AddEntityCommand>(std::move(e)), doc.GetScene());
    }

    // 用 GeometryEditCommand 把对象替换成 after（after 为空 = 删除）
    void Replace(Document& doc, Object::ObjectID id, std::unique_ptr<Entity> after)
    {
        Scene& s = doc.GetScene();
        GeometryEditCommand::Item it;
        it.id     = id;
        it.before = static_cast<Entity*>(s.GetEntity(id))->Clone(id);
        it.after  = std::move(after);
        std::vector<GeometryEditCommand::Item> items;
        items.push_back(std::move(it));
        doc.GetCommandStack().Execute(std::make_unique<GeometryEditCommand>("edit", std::move(items)), s);
    }

    void Move(Document& doc, std::vector<Object::ObjectID> ids, Math::Vec3 d)
    {
        Scene& s = doc.GetScene();
        doc.GetCommandStack().Execute(std::make_unique<MoveCommand>(ids, d, s), s);
    }

    // 两端关联到直线两个端点的对齐标注，尺寸线在直线上方 10
    Object::ObjectID AddAlignedOnLine(Document& doc, Object::ObjectID line)
    {
        Scene& s = doc.GetScene();
        const auto& l = static_cast<LineEntity*>(s.GetEntity(line))->GetLine();
        const auto id = s.NextObjectID();
        auto dim = DimensionEntity::MakeAligned(id, l.Start, l.End, Math::Midpoint(l.Start, l.End) + Math::Vec3{ 0, 10, 0 });
        dim->SetAssoc(DimAssocSlot::P1, DimAssoc::Feature(s, line, FeatureKind::Endpoint, l.Start));
        dim->SetAssoc(DimAssocSlot::P2, DimAssoc::Feature(s, line, FeatureKind::Endpoint, l.End));
        Add(doc, std::move(dim));
        return id;
    }
}

int RunDimAssocTests()
{
    // ── 建立关联 ─────────────────────────────────────────────
    {
        Document doc;
        Scene& s = doc.GetScene();
        const auto line = AddLine(doc, { 0, 0, 0 }, { 100, 0, 0 });
        Check(DimAssoc::Feature(s, line, FeatureKind::Endpoint, { 100, 0, 0 }).Index == 1, "端点关联：终点是第 2 个端点");
        Check(!DimAssoc::Feature(s, line, FeatureKind::Endpoint, { 50, 0, 0 }).IsValid(), "点不在端点上：不关联");
        Check(DimAssoc::Feature(s, line, FeatureKind::Midpoint, { 50, 0, 0 }).IsValid(), "中点关联");
        const auto n = DimAssoc::Nearest(s, line, { 25, 0, 0 });
        Check(n.IsValid() && Near(n.Param, 0.25), "最近点关联：直线按比例参数记录");
        Check(!DimAssoc::Nearest(s, line, { 25, 1, 0 }).IsValid(), "点不在曲线上：不关联");
        Check(!DimAssoc::Feature(s, 999, FeatureKind::Endpoint, { 0, 0, 0 }).IsValid(), "对象不存在：不关联");
    }

    // ── 几何移动，标注跟随；撤销 / 重做 ─────────────────────────
    {
        Document doc;
        Scene& s = doc.GetScene();
        const auto line = AddLine(doc, { 0, 0, 0 }, { 100, 0, 0 });
        const auto dim  = AddAlignedOnLine(doc, line);
        Check(Dim(s, dim).IsAssociative(), "新建的标注是关联的");

        Move(doc, { line }, { 5, 7, 0 });
        Check(Near(Dim(s, dim).GetP1(), { 5, 7, 0 }) && Near(Dim(s, dim).GetP2(), { 105, 7, 0 }), "只移动直线：标注点跟到新端点");
        Check(Near(Dim(s, dim).GetDimLinePoint().y, 17), "尺寸线与直线保持 10 的距离");
        Check(Dim(s, dim).IsAssociative(), "跟随后仍关联");

        doc.Undo();
        Check(Near(Dim(s, dim).GetP1(), { 0, 0, 0 }) && Near(Dim(s, dim).GetDimLinePoint().y, 10), "撤销：直线与标注一起回到原位");
        doc.Redo();
        Check(Near(Dim(s, dim).GetP2(), { 105, 7, 0 }) && Near(Dim(s, dim).GetDimLinePoint().y, 17), "重做：标注再次跟随");
    }

    // ── 夹点拖拽（先改几何再 Push）：标注跟随；撤销时被拖实体与标注都标脏，显示能刷新 ──
    {
        Document doc;
        Scene& s = doc.GetScene();
        const auto line = AddLine(doc, { 0, 0, 0 }, { 100, 0, 0 });
        const auto dim  = AddAlignedOnLine(doc, line);

        DragEntityEntry e{};
        e.Id         = line;
        e.Kind       = DragEntityEntry::Kind::Line;
        e.BeforeLine = { { 0, 0, 0 }, { 100, 0, 0 } };
        e.AfterLine  = { { 0, 0, 0 }, { 120, 0, 0 } };
        static_cast<LineEntity*>(s.GetEntity(line))->SetLine(e.AfterLine);   // 拖动期间已直接改好
        doc.GetCommandStack().Push(std::make_unique<DragEntitiesCommand>(std::vector<DragEntityEntry>{ e }));
        Check(Near(Dim(s, dim).Measurement(), 120), "拖夹点把直线拉长到 120：松开后标注跟随");

        s.ClearDirty();
        doc.Undo();
        Check(Near(Dim(s, dim).Measurement(), 100), "撤销拖动：标注回到 100");
        Check(s.GetDirtyEntities().contains(line) && s.GetDirtyEntities().contains(dim), "撤销拖动：直线与标注都标脏（显示会刷新）");
        s.ClearDirty();
        doc.Redo();
        Check(s.GetDirtyEntities().contains(line) && Near(Dim(s, dim).Measurement(), 120), "重做拖动：直线标脏、标注再次跟随");
    }

    // ── 改变一端：对齐标注长度更新，尺寸线保持偏移 ─────────────────
    {
        Document doc;
        Scene& s = doc.GetScene();
        const auto line = AddLine(doc, { 0, 0, 0 }, { 100, 0, 0 });
        const auto dim  = AddAlignedOnLine(doc, line);
        Replace(doc, line, std::make_unique<LineEntity>(line, Math::Point3{ 0, 0, 0 }, Math::Point3{ 0, 60, 0 }));
        const auto& d = Dim(s, dim);
        Check(Near(d.Measurement(), 60), "直线端点改到 (0,60)：标注值变为 60");
        Check(Near(d.GetDimLinePoint().x, -10) && Near(d.GetDimLinePoint().y, 30), "尺寸线仍在连线左侧 10 处（随连线转过 90°）");
    }

    // ── 转角标注：只动一端时尺寸线位置不变 ─────────────────────
    {
        Document doc;
        Scene& s = doc.GetScene();
        const auto line = AddLine(doc, { 0, 0, 0 }, { 100, 0, 0 });
        const auto id = s.NextObjectID();
        auto dim = DimensionEntity::MakeLinear(id, { 0, 0, 0 }, { 100, 0, 0 }, { 50, -15, 0 }, 0.0);
        dim->SetAssoc(DimAssocSlot::P1, DimAssoc::Feature(s, line, FeatureKind::Endpoint, { 0, 0, 0 }));
        dim->SetAssoc(DimAssocSlot::P2, DimAssoc::Feature(s, line, FeatureKind::Endpoint, { 100, 0, 0 }));
        Add(doc, std::move(dim));
        Replace(doc, line, std::make_unique<LineEntity>(line, Math::Point3{ 0, 0, 0 }, Math::Point3{ 80, 20, 0 }));
        Check(Near(Dim(s, id).Measurement(), 80) && Near(Dim(s, id).GetDimLinePoint(), { 50, -15, 0 }), "水平标注：终点移到 (80,20)，标注值 80、尺寸线位置不变");
    }

    // ── 标注与几何一起移动：只移动一次，仍关联 ───────────────────
    {
        Document doc;
        Scene& s = doc.GetScene();
        const auto line = AddLine(doc, { 0, 0, 0 }, { 100, 0, 0 });
        const auto dim  = AddAlignedOnLine(doc, line);
        Move(doc, { line, dim }, { 0, 50, 0 });
        Check(Near(Dim(s, dim).GetP1(), { 0, 50, 0 }) && Near(Dim(s, dim).GetDimLinePoint().y, 60), "一起移动：标注只平移一次");
        Check(Dim(s, dim).GetAssoc(DimAssocSlot::P1).IsValid() && Dim(s, dim).GetAssoc(DimAssocSlot::P2).IsValid(), "一起移动：仍关联");
        Move(doc, { line }, { 0, 1, 0 });
        Check(Near(Dim(s, dim).GetP1(), { 0, 51, 0 }), "之后再单独移动直线：标注继续跟随");
    }

    // ── 只移动标注：解除关联；撤销恢复关联 ───────────────────────
    {
        Document doc;
        Scene& s = doc.GetScene();
        const auto line = AddLine(doc, { 0, 0, 0 }, { 100, 0, 0 });
        const auto dim  = AddAlignedOnLine(doc, line);
        Move(doc, { dim }, { 0, 30, 0 });
        Check(!Dim(s, dim).IsAssociative() && Near(Dim(s, dim).GetP1(), { 0, 30, 0 }), "只移动标注：标注留在新位置并解除关联");
        doc.Undo();
        Check(Dim(s, dim).IsAssociative() && Near(Dim(s, dim).GetP1(), { 0, 0, 0 }), "撤销：标注回原位并恢复关联");
        Move(doc, { line }, { 0, -5, 0 });
        Check(Near(Dim(s, dim).GetP1(), { 0, -5, 0 }), "恢复关联后，移动直线标注跟随");
    }

    // ── 删除被关联对象：解除关联；撤销后重新关联 ──────────────────
    {
        Document doc;
        Scene& s = doc.GetScene();
        const auto line = AddLine(doc, { 0, 0, 0 }, { 100, 0, 0 });
        const auto dim  = AddAlignedOnLine(doc, line);
        Replace(doc, line, nullptr);
        Check(!s.Has(line) && !Dim(s, dim).IsAssociative() && Near(Dim(s, dim).Measurement(), 100), "删除直线：标注保留、解除关联");
        doc.Undo();
        Check(s.Has(line) && Dim(s, dim).IsAssociative(), "撤销删除：直线回来，标注重新关联");
    }

    // ── 半径标注：圆移动并放大，圆心与箭头点跟随，方向不变 ──────────
    {
        Document doc;
        Scene& s = doc.GetScene();
        const auto circle = s.NextObjectID();
        Add(doc, std::make_unique<CircleEntity>(circle, Math::Point3{ 0, 0, 0 }, 10.0));
        const auto id = s.NextObjectID();
        const Math::Point3 on{ 10 * std::cos(Math::PI / 4), 10 * std::sin(Math::PI / 4), 0 };
        auto dim = DimensionEntity::MakeRadius(id, { 0, 0, 0 }, on);
        dim->SetAssoc(DimAssocSlot::Center, DimAssoc::Center(s, circle));
        dim->SetAssoc(DimAssocSlot::P1, DimAssoc::Nearest(s, circle, on));
        Add(doc, std::move(dim));

        Replace(doc, circle, std::make_unique<CircleEntity>(circle, Math::Point3{ 20, 0, 0 }, 15.0));
        const auto& d = Dim(s, id);
        Check(Near(d.GetCenterPoint(), { 20, 0, 0 }) && Near(d.Measurement(), 15), "圆移动并放大：半径标注圆心跟随、值变为 15");
        const Math::Vec3 dir = d.GetP1() - d.GetCenterPoint();
        Check(Near(std::atan2(dir.y, dir.x), Math::PI / 4), "箭头仍在 45° 方向");
    }

    // ── 圆与半径标注一起旋转：按新位置重新挂接，之后再动圆仍跟随 ──────
    {
        Document doc;
        Scene& s = doc.GetScene();
        const auto circle = s.NextObjectID();
        Add(doc, std::make_unique<CircleEntity>(circle, Math::Point3{ 20, 0, 0 }, 10.0));
        const auto id = s.NextObjectID();
        auto dim = DimensionEntity::MakeRadius(id, { 20, 0, 0 }, { 30, 0, 0 });
        dim->SetAssoc(DimAssocSlot::Center, DimAssoc::Center(s, circle));
        dim->SetAssoc(DimAssocSlot::P1, DimAssoc::Nearest(s, circle, { 30, 0, 0 }));
        Add(doc, std::move(dim));

        doc.GetCommandStack().Execute(std::make_unique<RotateMoveCommand>(std::vector<Object::ObjectID>{ circle, id }, Math::Point3{ 0, 0, 0 }, Math::HalfPI, s), s);
        const auto& d = Dim(s, id);
        Check(Near(d.GetCenterPoint(), { 0, 20, 0 }) && Near(d.GetP1(), { 0, 30, 0 }), "一起旋转 90°：标注随之旋转，箭头点在 (0,30)");
        Check(d.GetAssoc(DimAssocSlot::P1).IsValid() && Near(d.GetAssoc(DimAssocSlot::P1).Param, Math::HalfPI), "一起旋转：箭头点按新角度 90° 重新挂接");
        Replace(doc, circle, std::make_unique<CircleEntity>(circle, Math::Point3{ 0, 20, 0 }, 5.0));
        Check(Near(Dim(s, id).GetP1(), { 0, 25, 0 }), "之后把圆改小：箭头点沿 90° 方向跟随");
    }

    // ── 直径标注：两端跟随，圆心取中点 ───────────────────────────
    {
        Document doc;
        Scene& s = doc.GetScene();
        const auto circle = s.NextObjectID();
        Add(doc, std::make_unique<CircleEntity>(circle, Math::Point3{ 0, 0, 0 }, 10.0));
        const auto id = s.NextObjectID();
        auto dim = DimensionEntity::MakeDiameter(id, { -10, 0, 0 }, { 10, 0, 0 });
        dim->SetAssoc(DimAssocSlot::P1, DimAssoc::Nearest(s, circle, { -10, 0, 0 }));
        dim->SetAssoc(DimAssocSlot::P2, DimAssoc::Nearest(s, circle, { 10, 0, 0 }));
        Add(doc, std::move(dim));
        Replace(doc, circle, std::make_unique<CircleEntity>(circle, Math::Point3{ 5, 5, 0 }, 4.0));
        Check(Near(Dim(s, id).Measurement(), 8) && Near(Dim(s, id).GetCenterPoint(), { 5, 5, 0 }), "圆改小并移动：直径标注值 8，圆心为两端中点");
    }

    // ── 弧长标注：改变圆弧包角，弧长更新，标注弧仍在弧外同样距离 ──────
    {
        Document doc;
        Scene& s = doc.GetScene();
        const auto arcId = s.NextObjectID();
        Add(doc, std::make_unique<ArcEntity>(arcId, Math::Point3{ 0, 0, 0 }, 10.0, 0.0, Math::HalfPI));
        const auto& arc = static_cast<ArcEntity*>(s.GetEntity(arcId))->GetArc();
        const auto id = s.NextObjectID();
        const Math::Point3 through{ 15 * std::cos(Math::PI / 4), 15 * std::sin(Math::PI / 4), 0 };
        auto dim = DimensionEntity::MakeArcLength(id, arc.Center, arc.StartPoint(), arc.EndPoint(), through);
        dim->SetAssoc(DimAssocSlot::Center, DimAssoc::Center(s, arcId));
        dim->SetAssoc(DimAssocSlot::P1, DimAssoc::Feature(s, arcId, FeatureKind::Endpoint, arc.StartPoint()));
        dim->SetAssoc(DimAssocSlot::P2, DimAssoc::Feature(s, arcId, FeatureKind::Endpoint, arc.EndPoint()));
        Add(doc, std::move(dim));
        Check(Dim(s, id).GetAssoc(DimAssocSlot::P2).IsValid(), "弧长标注三个定义点都关联");

        Replace(doc, arcId, std::make_unique<ArcEntity>(arcId, Math::Point3{ 0, 0, 0 }, 20.0, 0.0, Math::PI));
        const auto& d = Dim(s, id);
        Check(Near(d.Measurement(), 20 * Math::PI), "圆弧改为半径 20、包角 180°：弧长 20π");
        Check(Near((d.GetDimLinePoint() - d.GetCenterPoint()).Length(), 25), "标注弧仍在弧外 5 处");
        const Math::Vec3 v = d.GetDimLinePoint() - d.GetCenterPoint();
        Check(Near(std::atan2(v.y, v.x), Math::HalfPI), "标注弧经过点保持在弧的中间方向");
    }

    // ── 两直线角度标注：旋转一条直线，顶点与角度更新 ─────────────────
    {
        Document doc;
        Scene& s = doc.GetScene();
        const auto l1 = AddLine(doc, { 0, 0, 0 }, { 10, 0, 0 });
        const auto l2 = AddLine(doc, { 0, 0, 0 }, { 0, 10, 0 });
        const auto id = s.NextObjectID();
        auto dim = DimensionEntity::MakeAngular(id, { 0, 0, 0 }, { 10, 0, 0 }, { 0, 10, 0 }, { 5, 5, 0 });
        dim->SetAssoc(DimAssocSlot::Center, DimAssoc::Intersection(s, l1, l2, { 0, 0, 0 }, true));
        dim->SetAssoc(DimAssocSlot::P1, DimAssoc::Nearest(s, l1, { 10, 0, 0 }));
        dim->SetAssoc(DimAssocSlot::P2, DimAssoc::Nearest(s, l2, { 0, 10, 0 }));
        Add(doc, std::move(dim));
        Check(Dim(s, id).GetAssoc(DimAssocSlot::Center).IsValid(), "角度标注顶点关联到两直线交点");

        Replace(doc, l2, std::make_unique<LineEntity>(l2, Math::Point3{ 0, 0, 0 }, Math::Point3{ 10, 10, 0 }));
        Check(Near(Dim(s, id).Measurement(), Math::PI / 4), "第二条直线转到 45°：角度标注值 45°");

        // 两条线都平移：顶点（按无限长直线求交）跟随
        Move(doc, { l1, l2 }, { 3, 4, 0 });
        Check(Near(Dim(s, id).GetCenterPoint(), { 3, 4, 0 }) && Near(Dim(s, id).Measurement(), Math::PI / 4), "两直线一起平移：顶点跟随，角度不变");
    }

    // ── 复制：一起复制的标注关联到副本；单独复制的不关联 ───────────────
    {
        Document doc;
        Scene& s = doc.GetScene();
        const auto line = AddLine(doc, { 0, 0, 0 }, { 100, 0, 0 });
        const auto dim  = AddAlignedOnLine(doc, line);

        auto copyCmd = std::make_unique<CopyCommand>(std::vector<Object::ObjectID>{ line, dim }, Math::Vec3{ 0, 100, 0 });
        auto* copyRaw = copyCmd.get();
        doc.GetCommandStack().Execute(std::move(copyCmd), s);
        const auto lineCopy = copyRaw->GetNewIds()[0], dimCopy = copyRaw->GetNewIds()[1];
        Check(Dim(s, dimCopy).GetAssoc(DimAssocSlot::P1).Id == lineCopy, "一起复制：副本标注关联到副本直线");

        Move(doc, { lineCopy }, { 10, 0, 0 });
        Check(Near(Dim(s, dimCopy).GetP1(), { 10, 100, 0 }) && Near(Dim(s, dim).GetP1(), { 0, 0, 0 }), "移动副本直线：只有副本标注跟随");

        auto copyDim = std::make_unique<CopyCommand>(std::vector<Object::ObjectID>{ dim }, Math::Vec3{ 0, -50, 0 });
        auto* rawDim = copyDim.get();
        doc.GetCommandStack().Execute(std::move(copyDim), s);
        Check(!Dim(s, rawDim->GetNewIds()[0]).IsAssociative(), "单独复制标注：副本不关联");
    }

    // ── 序列化往返 ─────────────────────────────────────────────
    {
        DimensionEntity d(1, { 0, 0, 0 }, { 10, 0, 0 }, { 5, 5, 0 });
        DimAssocRef r;
        r.RefKind = DimAssocRef::Kind::Intersection;
        r.Id = 7; r.Id2 = 8; r.Index = 1; r.Param = 0.5; r.Last = { 1, 2, 0 };
        d.SetAssoc(DimAssocSlot::P2, r);
        JsonSerializer w;
        EntityIO::Write(w, d);
        JsonSerializer rd;
        auto e = rd.Parse(w.Dump()) ? EntityIO::Read(rd) : nullptr;
        const auto* back = e ? static_cast<const DimensionEntity*>(e.get()) : nullptr;
        const auto& rb = back ? back->GetAssoc(DimAssocSlot::P2) : DimAssocRef{};
        Check(back && !back->GetAssoc(DimAssocSlot::P1).IsValid() && rb.RefKind == r.RefKind && rb.Id == 7 && rb.Id2 == 8
              && rb.Index == 1 && Near(rb.Param, 0.5) && Near(rb.Last, r.Last), "序列化往返：关联引用完整保留");
    }

    return g_failures;
}

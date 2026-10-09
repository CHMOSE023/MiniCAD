#include "EntityIO.h"
#include "ISerializer.h"
#include "SerializeHelpers.hpp"
#include <algorithm>

#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/SolidEntity.hpp"
#include "Core/Entity/Face3DEntity.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Core/Entity/ImageEntity.hpp"
#include "Core/Entity/MLineEntity.hpp"
#include "Core/Entity/TableEntity.hpp"
#include "Core/Entity/RegionEntity.hpp"
#include "Core/Entity/RayEntity.hpp"
#include "Core/Entity/XLineEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/LeaderEntity.hpp"
#include "Core/Entity/MLeaderEntity.hpp"
#include "Core/Entity/ToleranceEntity.hpp"
#include "Core/Entity/BlockEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/AttribEntity.hpp"

#include <cstring>

namespace MiniCAD::EntityIO
{
    namespace
    {
        using Math::Point3;
        using Math::Vec3;

        // ── Hatch 边界 / 图案 ────────────────────────────────────────────────
        void HatchPatternObj(ISerializer& s, const char* key, HatchPattern& pat)
        {
            if (!s.BeginObject(key)) return;
            s.Value("name", pat.Name);
            s.Value("solid", pat.Solid);
            size_t n = pat.Families.size();
            if (s.BeginArray("families", n))
            {
                if (s.IsLoading()) pat.Families.resize(n);
                for (size_t i = 0; i < n; ++i)
                {
                    if (!s.BeginElement(i)) continue;
                    HatchLineFamily& f = pat.Families[i];
                    s.Value("angle", f.AngleDeg);
                    s.Value("spacing", f.Spacing);
                    s.Value("ox", f.OffsetX);
                    s.Value("oy", f.OffsetY);
                    // 错位与虚线只在非默认时写入；旧文件没有这两项，读入即连续线、无错位
                    if (s.IsLoading() || f.DeltaX != 0.0)
                        s.Value("dx", f.DeltaX);
                    if (s.IsLoading() || !f.Dashes.empty())
                        s.Value("dashes", f.Dashes);
                    s.EndElement();
                }
                s.EndArray();
            }
            s.EndObject();
        }

        void HatchLoops(ISerializer& s, std::vector<HatchLoop>& loops)
        {
            size_t nLoop = loops.size();
            if (!s.BeginArray("loops", nLoop)) return;
            if (s.IsLoading()) loops.resize(nLoop);
            for (size_t i = 0; i < nLoop; ++i)
            {
                if (!s.BeginElement(i)) continue;
                size_t nEdge = loops[i].Edges.size();
                if (s.BeginArray("edges", nEdge))
                {
                    if (s.IsLoading()) loops[i].Edges.resize(nEdge);
                    for (size_t k = 0; k < nEdge; ++k)
                    {
                        if (!s.BeginElement(k)) continue;
                        HatchEdge& e = loops[i].Edges[k];
                        Ser::Enum(s, "kind", e.Type);
                        switch (e.Type)
                        {
                        case HatchEdge::Kind::Poly:
                            Ser::PolylineGeom(s, "poly", e.Poly);
                            break;
                        case HatchEdge::Kind::EllipseArc:
                            Ser::EllipseGeom(s, "ellipse", e.Ell);
                            s.Value("a0", e.A0);
                            s.Value("a1", e.A1);
                            break;
                        case HatchEdge::Kind::Spline:
                            Ser::SplineGeom(s, "spline", e.Spl);
                            break;
                        }
                        s.EndElement();
                    }
                    s.EndArray();
                }
                s.EndElement();
            }
            s.EndArray();
        }

        // ── ATTDEF / ATTRIB 公共字段 ─────────────────────────────────────────
        struct AttribFields
        {
            std::string tag, text;
            Point3      pos;
            double      height = 2.5, rotation = 0.0;
            uint32_t    flags = 0;
        };

        void WriteAttribBase(ISerializer& s, const AttribBase& a)
        {
            std::string tag = a.GetTag();      s.Value("tag", tag);
            std::string text = a.GetText();    s.Value("text", text);
            Point3 pos = a.GetPosition();      Ser::Value(s, "position", pos);
            double h = a.GetHeight();          s.Value("height", h);
            double r = a.GetRotation();        s.Value("rotation", r);
            uint32_t flags = a.GetFlags();     s.Value("flags", flags);
        }

        AttribFields ReadAttribFields(ISerializer& s)
        {
            AttribFields f;
            s.Value("tag", f.tag);
            s.Value("text", f.text);
            Ser::Value(s, "position", f.pos);
            s.Value("height", f.height);
            s.Value("rotation", f.rotation);
            s.Value("flags", f.flags);
            return f;
        }

        // ── 各类型负载 ───────────────────────────────────────────────────────

        void WriteLine(ISerializer& s, const LineEntity& e)
        {
            Point3 a = e.GetLine().Start, b = e.GetLine().End;
            Ser::Value(s, "start", a);
            Ser::Value(s, "end", b);
        }
        std::unique_ptr<Entity> ReadLine(ISerializer& s, Object::ObjectID id)
        {
            Point3 a, b;
            Ser::Value(s, "start", a);
            Ser::Value(s, "end", b);
            return std::make_unique<LineEntity>(id, a, b);
        }

        void WritePoint(ISerializer& s, const PointEntity& e)
        {
            Point3 p = e.GetPoint().Position;
            Ser::Value(s, "position", p);
        }
        std::unique_ptr<Entity> ReadPoint(ISerializer& s, Object::ObjectID id)
        {
            Point3 p;
            Ser::Value(s, "position", p);
            return std::make_unique<PointEntity>(id, p);
        }

        void WriteCircle(ISerializer& s, const CircleEntity& e)
        {
            Point3 c = e.GetCircle().Center;
            double r = e.GetCircle().Radius;
            Ser::Value(s, "center", c);
            s.Value("radius", r);
        }
        std::unique_ptr<Entity> ReadCircle(ISerializer& s, Object::ObjectID id)
        {
            Point3 c; double r = 0.0;
            Ser::Value(s, "center", c);
            s.Value("radius", r);
            return std::make_unique<CircleEntity>(id, c, r);
        }

        void WriteArc(ISerializer& s, const ArcEntity& e)
        {
            const Arc& a = e.GetArc();
            Point3 c = a.Center; double r = a.Radius, a0 = a.StartAngle, a1 = a.EndAngle;
            Ser::Value(s, "center", c);
            s.Value("radius", r);
            s.Value("startAngle", a0);
            s.Value("endAngle", a1);
        }
        std::unique_ptr<Entity> ReadArc(ISerializer& s, Object::ObjectID id)
        {
            Point3 c; double r = 0.0, a0 = 0.0, a1 = 0.0;
            Ser::Value(s, "center", c);
            s.Value("radius", r);
            s.Value("startAngle", a0);
            s.Value("endAngle", a1);
            return std::make_unique<ArcEntity>(id, c, r, a0, a1);
        }

        // 椭圆弧的起止参数写在实体层(不进 EllipseGeom,填充边界的椭圆几何格式不变);
        // 整椭圆不写,旧文件读入即整椭圆。
        void WriteEllipse(ISerializer& s, const EllipseEntity& e)
        {
            Ellipse el = e.GetEllipse();
            Ser::EllipseGeom(s, "ellipse", el);
            if (!el.IsFull())
            {
                s.Value("startParam", el.StartParam);
                s.Value("endParam", el.EndParam);
            }
        }
        std::unique_ptr<Entity> ReadEllipse(ISerializer& s, Object::ObjectID id)
        {
            Ellipse el;
            Ser::EllipseGeom(s, "ellipse", el);
            if (s.Has("startParam") && s.Has("endParam"))
            {
                double a0 = 0.0, a1 = 0.0;
                s.Value("startParam", a0);
                s.Value("endParam", a1);
                el.SetParams(a0, a1);
            }
            return std::make_unique<EllipseEntity>(id, el);
        }

        void WriteRectangle(ISerializer& s, const RectangleEntity& e)
        {
            Rectangle r = e.GetRectangle();
            Ser::Value(s, "p1", r.P1);
            Ser::Value(s, "p2", r.P2);
            Ser::Value(s, "p3", r.P3);
            Ser::Value(s, "p4", r.P4);
        }
        std::unique_ptr<Entity> ReadRectangle(ISerializer& s, Object::ObjectID id)
        {
            Rectangle r;
            Ser::Value(s, "p1", r.P1);
            Ser::Value(s, "p2", r.P2);
            Ser::Value(s, "p3", r.P3);
            Ser::Value(s, "p4", r.P4);
            return std::make_unique<RectangleEntity>(id, r.P1, r.P2, r.P3, r.P4);
        }

        void WriteSolid(ISerializer& s, const SolidEntity& e) { WriteRectangle(s, e); }
        void WriteFace3D(ISerializer& s, const Face3DEntity& e)
        {
            WriteRectangle(s, e);
            auto flags = e.GetInvisibleEdges(); s.Value("invisibleEdges", flags);
        }
        std::unique_ptr<Entity> ReadFace3D(ISerializer& s, Object::ObjectID id)
        {
            Rectangle r;
            Ser::Value(s, "p1", r.P1); Ser::Value(s, "p2", r.P2);
            Ser::Value(s, "p3", r.P3); Ser::Value(s, "p4", r.P4);
            std::uint32_t flags = 0; s.Value("invisibleEdges", flags);
            return std::make_unique<Face3DEntity>(id, r.P1, r.P2, r.P3, r.P4, flags);
        }
        std::unique_ptr<Entity> ReadSolid(ISerializer& s, Object::ObjectID id)
        {
            Rectangle r;
            Ser::Value(s, "p1", r.P1);
            Ser::Value(s, "p2", r.P2);
            Ser::Value(s, "p3", r.P3);
            Ser::Value(s, "p4", r.P4);
            return std::make_unique<SolidEntity>(id, r.P1, r.P2, r.P3, r.P4);
        }

        void WriteRay(ISerializer& s, const RayEntity& e)
        {
            Point3 o = e.GetOrigin(); Vec3 d = e.GetDirection();
            Ser::Value(s, "origin", o);
            Ser::Value(s, "direction", d);
        }
        std::unique_ptr<Entity> ReadRay(ISerializer& s, Object::ObjectID id)
        {
            Point3 o; Vec3 d{ 1, 0, 0 };
            Ser::Value(s, "origin", o);
            Ser::Value(s, "direction", d);
            return std::make_unique<RayEntity>(id, o, d);
        }

        void WriteXLine(ISerializer& s, const XLineEntity& e)
        {
            Point3 o = e.GetOrigin(); Vec3 d = e.GetDirection();
            Ser::Value(s, "origin", o);
            Ser::Value(s, "direction", d);
        }
        std::unique_ptr<Entity> ReadXLine(ISerializer& s, Object::ObjectID id)
        {
            Point3 o; Vec3 d{ 1, 0, 0 };
            Ser::Value(s, "origin", o);
            Ser::Value(s, "direction", d);
            return std::make_unique<XLineEntity>(id, o, d);
        }

        void WriteText(ISerializer& s, const TextEntity& e)
        {
            std::string t = e.GetText();    s.Value("text", t);
            Point3 p = e.GetPosition();     Ser::Value(s, "position", p);
            float h = e.GetHeight();        s.Value("height", h);
            float r = e.GetRotation();      s.Value("rotation", r);
            uint32_t st = e.GetStyleId();   s.Value("styleId", st);
        }
        std::unique_ptr<Entity> ReadText(ISerializer& s, Object::ObjectID id)
        {
            std::string t; Point3 p; float h = 2.5f, r = 0.f;
            s.Value("text", t);
            Ser::Value(s, "position", p);
            s.Value("height", h);
            s.Value("rotation", r);
            uint32_t st = 0;                // 旧文件无此项：0 = Standard
            s.Value("styleId", st);
            return std::make_unique<TextEntity>(id, p, t, h, r, st);
        }

        void WriteMText(ISerializer& s, const MTextEntity& e)
        {
            std::string text = e.GetText();           s.Value("text", text);
            uint32_t style = e.GetStyleId();          s.Value("styleId", style);
            Point3 pos = e.GetPosition();             Ser::Value(s, "position", pos);
            double h = e.GetHeight();                 s.Value("height", h);
            double r = e.GetRotation();               s.Value("rotation", r);
            double w = e.GetBoxWidth();               s.Value("boxWidth", w);
            auto attach = e.GetAttachment();          Ser::Enum(s, "attachment", attach);
            auto dir = e.GetDrawingDirection();       Ser::Enum(s, "drawDir", dir);
            auto lss = e.GetLineSpacingStyle();       Ser::Enum(s, "lineSpacingStyle", lss);
            double lsf = e.GetLineSpacingFactor();    s.Value("lineSpacingFactor", lsf);
            double dh = e.GetDefinedHeight();         s.Value("definedHeight", dh);
            auto ct = e.GetColumnType();              Ser::Enum(s, "columnType", ct);
            int32_t cc = e.GetColumnCount();          s.Value("columnCount", cc);
            double cw = e.GetColumnWidth();           s.Value("columnWidth", cw);
            double cg = e.GetColumnGutter();          s.Value("columnGutter", cg);
            std::vector<double> chs = e.GetColumnHeights(); s.Value("columnHeights", chs);
        }
        std::unique_ptr<Entity> ReadMText(ISerializer& s, Object::ObjectID id)
        {
            auto e = std::make_unique<MTextEntity>(id);
            std::string text;                              s.Value("text", text);              e->SetText(text);
            uint32_t style = 0;                            s.Value("styleId", style);          e->SetStyleId(style);
            Point3 pos;                                    Ser::Value(s, "position", pos);     e->SetPosition(pos);
            double h = 1.0;                                s.Value("height", h);               e->SetHeight(h);
            double r = 0.0;                                s.Value("rotation", r);             e->SetRotation(r);
            double w = 0.0;                                s.Value("boxWidth", w);             e->SetBoxWidth(w);
            auto attach = MTextAttachment::TopLeft;        Ser::Enum(s, "attachment", attach); e->SetAttachment(attach);
            auto dir = MTextDrawingDirection::ByStyle;     Ser::Enum(s, "drawDir", dir);       e->SetDrawingDirection(dir);
            auto lss = MTextLineSpacing::AtLeast;          Ser::Enum(s, "lineSpacingStyle", lss);
            double lsf = 1.0;                              s.Value("lineSpacingFactor", lsf);  e->SetLineSpacing(lss, lsf);
            double dh = 0.0;                               s.Value("definedHeight", dh);       e->SetDefinedHeight(dh);
            auto ct = MTextColumnType::None;               Ser::Enum(s, "columnType", ct);
            int32_t cc = 1;                                s.Value("columnCount", cc);
            double cw = 0.0;                               s.Value("columnWidth", cw);
            double cg = 0.0;                               s.Value("columnGutter", cg);        e->SetColumns(ct, cc, cw, cg);
            std::vector<double> chs;                       s.Value("columnHeights", chs);      e->SetColumnHeights(std::move(chs));
            return e;
        }

        void WritePolyline(ISerializer& s, const PolylineEntity& e)
        {
            Polyline pl = e.GetPolyline();
            Ser::PolylineGeom(s, "polyline", pl);
            double w = e.GetWidth();
            s.Value("width", w);
        }
        std::unique_ptr<Entity> ReadPolyline(ISerializer& s, Object::ObjectID id)
        {
            Polyline pl;
            Ser::PolylineGeom(s, "polyline", pl);
            double w = 1.0;
            s.Value("width", w);
            auto e = std::make_unique<PolylineEntity>(id, std::move(pl));
            e->SetWidth(w);
            return e;
        }

        void WriteTable(ISerializer& s, const TableEntity& e)
        {
            Point3 pos = e.GetPosition();            Ser::Value(s, "position", pos);
            double rot = e.GetRotation();            s.Value("rotation", rot);
            double th  = e.GetHeight();              s.Value("textHeight", th);
            uint32_t style = e.GetStyleId();         s.Value("styleId", style);
            double margin = e.Margin();              s.Value("margin", margin);
            std::vector<double> cols = e.ColWidths();   s.Value("colWidths", cols);
            std::vector<double> rows = e.RowHeights();  s.Value("rowHeights", rows);

            size_t n = rows.size() * cols.size();
            if (s.BeginArray("cells", n))
            {
                for (size_t i = 0; i < n; ++i)
                {
                    if (!s.BeginElement(i)) continue;
                    const TableCell& c = e.Cell(i / cols.size(), i % cols.size());
                    std::string text = c.Text;           s.Value("text", text);
                    int32_t align = static_cast<int32_t>(c.Align);   s.Value("align", align);
                    s.EndElement();
                }
                s.EndArray();
            }
        }
        std::unique_ptr<Entity> ReadTable(ISerializer& s, Object::ObjectID id)
        {
            Point3 pos;                              Ser::Value(s, "position", pos);
            double rot = 0.0;                        s.Value("rotation", rot);
            double th = 2.5;                         s.Value("textHeight", th);
            uint32_t style = 0;                      s.Value("styleId", style);
            double margin = 0.4 * th;                s.Value("margin", margin);
            std::vector<double> cols, rows;          s.Value("colWidths", cols);  s.Value("rowHeights", rows);
            if (cols.empty()) cols.push_back(12.0 * th);
            if (rows.empty()) rows.push_back(3.2 * th);

            auto e = std::make_unique<TableEntity>(id, pos, cols, rows, th, style);
            e->SetRotation(rot);
            e->SetMargin(margin);

            size_t n = 0;
            if (s.BeginArray("cells", n))
            {
                for (size_t i = 0; i < n && i < e->RowCount() * e->ColCount(); ++i)
                {
                    if (!s.BeginElement(i)) continue;
                    std::string text;                s.Value("text", text);
                    int32_t align = 0;               s.Value("align", align);
                    const size_t r = i / e->ColCount(), c = i % e->ColCount();
                    e->SetCellText(r, c, std::move(text));
                    e->SetCellAlign(r, c, static_cast<TableAlign>(std::clamp(align, 0, 2)));
                    s.EndElement();
                }
                s.EndArray();
            }
            return e;
        }

        void WriteMLine(ISerializer& s, const MLineEntity& e)
        {
            auto verts = e.GetVertices();                          Ser::Value(s, "vertices", verts);
            int32_t just = static_cast<int32_t>(e.GetJustify());   s.Value("justify", just);
            double scale = e.GetScale();                           s.Value("scale", scale);
            bool closed = e.IsClosed();                            s.Value("closed", closed);
            MLineStyleRecord st = e.GetMLineStyle();
            if (s.BeginObject("style")) { MLineStyleTable::SerializeRecord(s, st); s.EndObject(); }
        }
        std::unique_ptr<Entity> ReadMLine(ISerializer& s, Object::ObjectID id)
        {
            std::vector<Point3> verts;                             Ser::Value(s, "vertices", verts);
            int32_t just = 1;     s.Value("justify", just);
            double scale = 1.0;   s.Value("scale", scale);
            bool closed = false;  s.Value("closed", closed);
            MLineStyleRecord st = MLineStyleTable().Resolve(MLineStyleTable::StandardID);
            if (s.BeginObject("style")) { MLineStyleTable::SerializeRecord(s, st); s.EndObject(); }
            if (st.Elements.empty())
                st = MLineStyleTable().Resolve(MLineStyleTable::StandardID);
            return std::make_unique<MLineEntity>(id, std::move(verts), std::move(st),
                                                 static_cast<MLineJustify>(std::clamp(just, 0, 2)), scale, closed);
        }

        void WriteImage(ISerializer& s, const ImageEntity& e)
        {
            std::string path = e.GetPath();
            s.Value("path", path);
            WriteRectangle(s, e);
            bool frame = e.GetShowFrame();
            s.Value("showFrame", frame);
        }
        std::unique_ptr<Entity> ReadImage(ISerializer& s, Object::ObjectID id)
        {
            std::string path;
            s.Value("path", path);
            Rectangle r;
            Ser::Value(s, "p1", r.P1);
            Ser::Value(s, "p2", r.P2);
            Ser::Value(s, "p3", r.P3);
            Ser::Value(s, "p4", r.P4);
            bool frame = true;
            s.Value("showFrame", frame);
            auto e = std::make_unique<ImageEntity>(id, std::move(path), r.P1, r.P2, r.P3, r.P4);
            e->SetShowFrame(frame);
            return e;
        }

        void WriteWipeout(ISerializer& s, const WipeoutEntity& e)
        {
            Polyline pl = e.GetPolyline();
            Ser::PolylineGeom(s, "polyline", pl);
            bool frame = e.GetShowFrame();
            s.Value("showFrame", frame);
        }
        std::unique_ptr<Entity> ReadWipeout(ISerializer& s, Object::ObjectID id)
        {
            Polyline pl;
            Ser::PolylineGeom(s, "polyline", pl);
            bool frame = true;
            s.Value("showFrame", frame);
            auto e = std::make_unique<WipeoutEntity>(id, std::move(pl.Points));
            e->SetShowFrame(frame);
            return e;
        }

        void WriteSpline(ISerializer& s, const SplineEntity& e)
        {
            Spline sp = e.GetSpline();
            Ser::SplineGeom(s, "spline", sp);
        }
        std::unique_ptr<Entity> ReadSpline(ISerializer& s, Object::ObjectID id)
        {
            Spline sp;
            Ser::SplineGeom(s, "spline", sp);
            return std::make_unique<SplineEntity>(id, std::move(sp));
        }

        void WriteHatch(ISerializer& s, const HatchEntity& e)
        {
            auto loops = e.GetLoops();          // 拷贝:读写合一接口需要非 const
            HatchLoops(s, loops);
            HatchPattern pat = e.GetPattern();
            HatchPatternObj(s, "pattern", pat);
            double sc = e.GetScale();  s.Value("scale", sc);
            double an = e.GetAngle();  s.Value("angle", an);
        }
        std::unique_ptr<Entity> ReadHatch(ISerializer& s, Object::ObjectID id)
        {
            std::vector<HatchLoop> loops;
            HatchLoops(s, loops);
            HatchPattern pat;
            HatchPatternObj(s, "pattern", pat);
            double sc = 1.0, an = 0.0;
            s.Value("scale", sc);
            s.Value("angle", an);
            auto e = std::make_unique<HatchEntity>(id, std::move(loops), std::move(pat));
            e->SetScale(sc);
            e->SetAngle(an);
            return e;
        }

        void WriteRegion(ISerializer& s, const RegionEntity& e)
        {
            auto loops = e.GetLoops();
            HatchLoops(s, loops);
            bool fill = e.GetShowFill();   s.Value("showFill", fill);
        }
        std::unique_ptr<Entity> ReadRegion(ISerializer& s, Object::ObjectID id)
        {
            std::vector<HatchLoop> loops;
            HatchLoops(s, loops);
            bool fill = false;             s.Value("showFill", fill);
            auto e = std::make_unique<RegionEntity>(id, std::move(loops));
            e->SetShowFill(fill);
            return e;
        }

        // 关联标注的关联引用：只写有效的槽位，没有关联时不写 "assoc"
        void RefFields(ISerializer& s, int32_t& slot, int32_t& kind, DimAssocRef& r)
        {
            s.Value("slot", slot);
            s.Value("kind", kind);
            s.Value("id", r.Id);
            s.Value("id2", r.Id2);
            s.Value("index", r.Index);
            s.Value("param", r.Param);
            Ser::Value(s, "last", r.Last);
        }

        void WriteDimAssoc(ISerializer& s, const DimensionEntity& e)
        {
            constexpr DimAssocSlot kSlots[] = { DimAssocSlot::P1, DimAssocSlot::P2, DimAssocSlot::Center };
            size_t n = 0;
            for (auto slot : kSlots) if (e.GetAssoc(slot).IsValid()) ++n;
            if (n == 0 || !s.BeginArray("assoc", n)) return;
            size_t i = 0;
            for (auto slot : kSlots)
            {
                DimAssocRef r = e.GetAssoc(slot);
                if (!r.IsValid() || !s.BeginElement(i++)) continue;
                int32_t sl = static_cast<int32_t>(slot), kind = static_cast<int32_t>(r.RefKind);
                RefFields(s, sl, kind, r);
                s.EndElement();
            }
            s.EndArray();
        }

        void ReadDimAssoc(ISerializer& s, DimensionEntity& e)
        {
            size_t n = 0;
            if (!s.BeginArray("assoc", n)) return;
            for (size_t i = 0; i < n; ++i)
            {
                if (!s.BeginElement(i)) continue;
                int32_t slot = -1, kind = 0;
                DimAssocRef r;
                RefFields(s, slot, kind, r);
                s.EndElement();
                if (slot < 0 || slot >= static_cast<int32_t>(DimAssocSlot::Count)) continue;
                if (kind <= 0 || kind > static_cast<int32_t>(DimAssocRef::Kind::Intersection)) continue;
                r.RefKind = static_cast<DimAssocRef::Kind>(kind);
                e.SetAssoc(static_cast<DimAssocSlot>(slot), r);
            }
            s.EndArray();
        }

        void WriteDimension(ISerializer& s, const DimensionEntity& e)
        {
            auto type = e.GetType();             Ser::Enum(s, "dimType", type);
            Point3 p1 = e.GetP1();               Ser::Value(s, "p1", p1);
            Point3 p2 = e.GetP2();               Ser::Value(s, "p2", p2);
            Point3 dlp = e.GetDimLinePoint();    Ser::Value(s, "dimLinePoint", dlp);
            Point3 cp = e.GetCenterPoint();      Ser::Value(s, "centerPoint", cp);
            double la = e.GetLinearAngle();      s.Value("linearAngle", la);
            auto axis = e.GetOrdinateAxis();     Ser::Enum(s, "ordinateAxis", axis);
            DimStyle st = e.GetStyle();          Ser::DimStyleObj(s, "style", st);
            uint32_t sid = e.GetStyleId();       s.Value("styleId", sid);
            std::string ovr = e.GetTextOverride(); s.Value("textOverride", ovr);
            bool defPos = e.IsUsingDefaultTextPosition();
            s.Value("defaultTextPos", defPos);
            if (!defPos)
            {
                Point3 tp = e.TextPosition();
                Ser::Value(s, "textPosition", tp);
            }
            WriteDimAssoc(s, e);
        }
        std::unique_ptr<Entity> ReadDimension(ISerializer& s, Object::ObjectID id)
        {
            auto type = DimType::Aligned;        Ser::Enum(s, "dimType", type);
            Point3 p1, p2, dlp, cp;
            Ser::Value(s, "p1", p1);
            Ser::Value(s, "p2", p2);
            Ser::Value(s, "dimLinePoint", dlp);
            Ser::Value(s, "centerPoint", cp);
            double la = 0.0;                     s.Value("linearAngle", la);
            auto axis = OrdinateAxis::X;         Ser::Enum(s, "ordinateAxis", axis);
            DimStyle st;                         Ser::DimStyleObj(s, "style", st);
            uint32_t sid = DimStyle_StandardID;  s.Value("styleId", sid);
            std::string ovr;                     s.Value("textOverride", ovr);

            auto e = std::make_unique<DimensionEntity>(id, p1, p2, dlp, st);
            e->SetType(type);
            e->SetCenterPoint(cp);
            e->SetLinearAngle(la);
            e->SetOrdinateAxis(axis);
            e->SetStyleId(sid);
            e->SetTextOverride(ovr);

            bool defPos = true;
            s.Value("defaultTextPos", defPos);
            if (!defPos)
            {
                Point3 tp;
                Ser::Value(s, "textPosition", tp);
                e->SetTextPosition(tp);
            }
            ReadDimAssoc(s, *e);
            return e;
        }

        void WriteLeader(ISerializer& s, const LeaderEntity& e)
        {
            auto verts = e.GetVertices();        Ser::Value(s, "vertices", verts);
            bool arrow = e.GetArrowEnabled();    s.Value("arrow", arrow);
            auto path = e.GetPath();             Ser::Enum(s, "path", path);
            bool hook = e.GetHookline();         s.Value("hookline", hook);
            auto anno = e.GetAnnotationType();   Ser::Enum(s, "annotation", anno);
            std::string text = e.GetText();      s.Value("text", text);
            double th = e.GetTextHeight();       s.Value("textHeight", th);
            DimStyle st = e.GetStyle();          Ser::DimStyleObj(s, "style", st);
            uint32_t sid = e.GetStyleId();       s.Value("styleId", sid);
        }
        std::unique_ptr<Entity> ReadLeader(ISerializer& s, Object::ObjectID id)
        {
            std::vector<Point3> verts;           Ser::Value(s, "vertices", verts);
            DimStyle st;                         Ser::DimStyleObj(s, "style", st);
            auto e = std::make_unique<LeaderEntity>(id, std::move(verts), st);

            bool arrow = true;                   s.Value("arrow", arrow);          e->SetArrowEnabled(arrow);
            auto path = LeaderPath::Straight;    Ser::Enum(s, "path", path);       e->SetPath(path);
            bool hook = true;                    s.Value("hookline", hook);        e->SetHookline(hook);
            auto anno = LeaderAnnotation::Text;  Ser::Enum(s, "annotation", anno); e->SetAnnotationType(anno);
            std::string text;                    s.Value("text", text);            e->SetText(std::move(text));
            double th = 3.5;                     s.Value("textHeight", th);        e->SetTextHeight(th);
            uint32_t sid = DimStyle_StandardID;  s.Value("styleId", sid);          e->SetStyleId(sid);
            return e;
        }

        void WriteMLeader(ISerializer& s, const MLeaderEntity& e)
        {
            auto lines = e.GetLeaderLines();     // 拷贝
            size_t n = lines.size();
            if (s.BeginArray("lines", n))
            {
                for (size_t i = 0; i < n; ++i)
                {
                    if (!s.BeginElement(i)) continue;
                    Ser::Value(s, "points", lines[i].points);
                    s.Value("arrow", lines[i].arrow);
                    s.EndElement();
                }
                s.EndArray();
            }
            Point3 landing = e.GetLanding();     Ser::Value(s, "landing", landing);
            Vec3 dogleg = e.GetDoglegDir();      Ser::Value(s, "doglegDir", dogleg);
            auto content = e.GetContentType();   Ser::Enum(s, "content", content);
            std::string text = e.GetText();      s.Value("text", text);
            uint32_t blk = e.GetBlockId();       s.Value("blockId", blk);
            MLeaderStyle st = e.GetStyle();      Ser::MLeaderStyleObj(s, "style", st);
            uint32_t sid = e.GetStyleId();       s.Value("styleId", sid);
        }
        std::unique_ptr<Entity> ReadMLeader(ISerializer& s, Object::ObjectID id)
        {
            MLeaderStyle st;
            Ser::MLeaderStyleObj(s, "style", st);
            auto e = std::make_unique<MLeaderEntity>(id, st);

            size_t n = 0;
            if (s.BeginArray("lines", n))
            {
                for (size_t i = 0; i < n; ++i)
                {
                    if (!s.BeginElement(i)) continue;
                    MLeaderEntity::LeaderLine ln;
                    Ser::Value(s, "points", ln.points);
                    s.Value("arrow", ln.arrow);
                    e->AddLeaderLine(std::move(ln));
                    s.EndElement();
                }
                s.EndArray();
            }
            Point3 landing;                       Ser::Value(s, "landing", landing);   e->SetLanding(landing);
            Vec3 dogleg{ 1, 0, 0 };               Ser::Value(s, "doglegDir", dogleg);  e->SetDoglegDir(dogleg);
            std::string text;                     s.Value("text", text);               e->SetText(std::move(text));
            uint32_t blk = 0;                     s.Value("blockId", blk);             if (blk) e->SetBlockId(blk);
            uint32_t sid = MLeaderStyle_StandardID; s.Value("styleId", sid);           e->SetStyleId(sid);
            // SetText/SetBlockId 会改写内容类型,最后以档案值为准。
            auto content = MLeaderContent::MText; Ser::Enum(s, "content", content);    e->SetContentType(content);
            return e;
        }

        void WriteTolerance(ISerializer& s, const ToleranceEntity& e)
        {
            Point3 ins = e.GetInsertion();       Ser::Value(s, "insertion", ins);
            Vec3 dir = e.GetDirection();         Ser::Value(s, "direction", dir);
            std::string text = e.GetText();      s.Value("text", text);
            DimStyle st = e.GetStyle();          Ser::DimStyleObj(s, "style", st);
            uint32_t sid = e.GetStyleId();       s.Value("styleId", sid);
        }
        std::unique_ptr<Entity> ReadTolerance(ISerializer& s, Object::ObjectID id)
        {
            Point3 ins;                          Ser::Value(s, "insertion", ins);
            std::string text;                    s.Value("text", text);
            DimStyle st;                         Ser::DimStyleObj(s, "style", st);
            auto e = std::make_unique<ToleranceEntity>(id, ins, std::move(text), st);
            Vec3 dir{ 1, 0, 0 };                 Ser::Value(s, "direction", dir);   e->SetDirection(dir);
            uint32_t sid = DimStyle_StandardID;  s.Value("styleId", sid);           e->SetStyleId(sid);
            return e;
        }

        void WriteAttDef(ISerializer& s, const AttDefEntity& e)
        {
            WriteAttribBase(s, e);
            std::string prompt = e.GetPrompt();
            s.Value("prompt", prompt);
        }
        std::unique_ptr<Entity> ReadAttDef(ISerializer& s, Object::ObjectID id)
        {
            AttribFields f = ReadAttribFields(s);
            auto e = std::make_unique<AttDefEntity>(id, std::move(f.tag), std::move(f.text), f.pos, f.height, f.rotation);
            e->SetFlags(static_cast<uint16_t>(f.flags));
            std::string prompt;
            s.Value("prompt", prompt);
            e->SetPrompt(std::move(prompt));
            return e;
        }

        void WriteAttrib(ISerializer& s, const AttribEntity& e)
        {
            WriteAttribBase(s, e);
        }
        std::unique_ptr<AttribEntity> ReadAttribTyped(ISerializer& s, Object::ObjectID id)
        {
            AttribFields f = ReadAttribFields(s);
            auto e = std::make_unique<AttribEntity>(id, std::move(f.tag), std::move(f.text), f.pos, f.height, f.rotation);
            e->SetFlags(static_cast<uint16_t>(f.flags));
            return e;
        }

        void WriteBlock(ISerializer& s, const BlockEntity& e)
        {
            std::string name = e.GetName();      s.Value("name", name);
            Point3 base = e.GetBasePoint();      Ser::Value(s, "basePoint", base);
            uint32_t flags = e.GetFlags();       s.Value("flags", flags);
            std::string desc = e.GetDescription(); s.Value("description", desc);
            std::string xref = e.GetXRefPath();  s.Value("xrefPath", xref);

            size_t n = e.GetEntities().size();
            if (s.BeginArray("entities", n))
            {
                for (const auto& child : e.GetEntities())
                {
                    if (!child) continue;
                    if (s.BeginElement(0))
                    {
                        Write(s, *child);
                        s.EndElement();
                    }
                }
                s.EndArray();
            }
        }
        std::unique_ptr<Entity> ReadBlock(ISerializer& s, Object::ObjectID id)
        {
            std::string name;                    s.Value("name", name);
            Point3 base;                         Ser::Value(s, "basePoint", base);
            auto e = std::make_unique<BlockEntity>(id, std::move(name), base);

            uint32_t flags = 0;                  s.Value("flags", flags);          e->SetFlags(static_cast<uint16_t>(flags));
            std::string desc;                    s.Value("description", desc);     e->SetDescription(std::move(desc));
            std::string xref;                    s.Value("xrefPath", xref);        e->SetXRefPath(std::move(xref));

            size_t n = 0;
            if (s.BeginArray("entities", n))
            {
                for (size_t i = 0; i < n; ++i)
                {
                    if (!s.BeginElement(i)) continue;
                    if (auto child = Read(s))
                        e->AddEntity(std::move(child));
                    s.EndElement();
                }
                s.EndArray();
            }
            return e;
        }

        void WriteInsert(ISerializer& s, const InsertEntity& e)
        {
            uint64_t blockId = e.GetBlockId();   s.Value("blockId", blockId);
            std::string bname = e.GetBlockName(); s.Value("blockName", bname);
            Point3 pos = e.GetPosition();        Ser::Value(s, "position", pos);
            Vec3 scale = e.GetScale();           Ser::Value(s, "scale", scale);
            double rot = e.GetRotation();        s.Value("rotation", rot);
            int32_t cols = e.GetColumnCount();   s.Value("cols", cols);
            int32_t rows = e.GetRowCount();      s.Value("rows", rows);
            double cs = e.GetColumnSpacing();    s.Value("colSpacing", cs);
            double rs = e.GetRowSpacing();       s.Value("rowSpacing", rs);

            size_t n = e.GetAttribs().size();
            if (s.BeginArray("attribs", n))
            {
                for (const auto& a : e.GetAttribs())
                {
                    if (!a) continue;
                    if (s.BeginElement(0))
                    {
                        uint64_t aid = a->GetID();
                        s.Value("id", aid);
                        EntityAttr attr = a->GetAttr();
                        Ser::Attr(s, attr);
                        WriteAttrib(s, *a);
                        s.EndElement();
                    }
                }
                s.EndArray();
            }
        }
        std::unique_ptr<Entity> ReadInsert(ISerializer& s, Object::ObjectID id)
        {
            uint64_t blockId = 0;                s.Value("blockId", blockId);
            std::string bname;                   s.Value("blockName", bname);
            Point3 pos;                          Ser::Value(s, "position", pos);
            auto e = std::make_unique<InsertEntity>(id, blockId, bname, pos);

            Vec3 scale{ 1, 1, 1 };               Ser::Value(s, "scale", scale);    e->SetScale(scale);
            double rot = 0.0;                    s.Value("rotation", rot);         e->SetRotation(rot);
            int32_t cols = 1, rows = 1;
            double cs = 0.0, rs = 0.0;
            s.Value("cols", cols);
            s.Value("rows", rows);
            s.Value("colSpacing", cs);
            s.Value("rowSpacing", rs);
            e->SetArray(cols, rows, cs, rs);

            size_t n = 0;
            if (s.BeginArray("attribs", n))
            {
                for (size_t i = 0; i < n; ++i)
                {
                    if (!s.BeginElement(i)) continue;
                    uint64_t aid = Object::InvalidID;
                    s.Value("id", aid);
                    auto a = ReadAttribTyped(s, aid);
                    EntityAttr attr = a->GetAttr();
                    Ser::Attr(s, attr);
                    a->SetAttr(attr);
                    e->AddAttrib(std::move(a));
                    s.EndElement();
                }
                s.EndArray();
            }
            return e;
        }

        // ── 类型分发表 ───────────────────────────────────────────────────────
        using WriterFn = void (*)(ISerializer&, const Entity&);
        using ReaderFn = std::unique_ptr<Entity>(*)(ISerializer&, Object::ObjectID);

        struct TypeEntry
        {
            const char* name;
            WriterFn    write;
            ReaderFn    read;
        };

        template<typename T, void (*Fn)(ISerializer&, const T&)>
        void WriteThunk(ISerializer& s, const Entity& e)
        {
            Fn(s, static_cast<const T&>(e));
        }

        const TypeEntry kTypes[] = {
            { "Face3DEntity", &WriteThunk<Face3DEntity, &WriteFace3D>, &ReadFace3D },
            { "LineEntity",      &WriteThunk<LineEntity, &WriteLine>,           &ReadLine      },
            { "PointEntity",     &WriteThunk<PointEntity, &WritePoint>,         &ReadPoint     },
            { "CircleEntity",    &WriteThunk<CircleEntity, &WriteCircle>,       &ReadCircle    },
            { "ArcEntity",       &WriteThunk<ArcEntity, &WriteArc>,             &ReadArc       },
            { "EllipseEntity",   &WriteThunk<EllipseEntity, &WriteEllipse>,     &ReadEllipse   },
            { "RectangleEntity", &WriteThunk<RectangleEntity, &WriteRectangle>, &ReadRectangle },
            { "TableEntity",     &WriteThunk<TableEntity, &WriteTable>,         &ReadTable     },
            { "MLineEntity",     &WriteThunk<MLineEntity, &WriteMLine>,         &ReadMLine     },
            { "ImageEntity",     &WriteThunk<ImageEntity, &WriteImage>,         &ReadImage     },
            { "WipeoutEntity",   &WriteThunk<WipeoutEntity, &WriteWipeout>,     &ReadWipeout   },
            { "SolidEntity",   &WriteThunk<SolidEntity, &WriteSolid>,         &ReadSolid     },
            { "RayEntity",       &WriteThunk<RayEntity, &WriteRay>,             &ReadRay       },
            { "XLineEntity",     &WriteThunk<XLineEntity, &WriteXLine>,         &ReadXLine     },
            { "TextEntity",      &WriteThunk<TextEntity, &WriteText>,           &ReadText      },
            { "MTextEntity",     &WriteThunk<MTextEntity, &WriteMText>,         &ReadMText     },
            { "PolylineEntity",  &WriteThunk<PolylineEntity, &WritePolyline>,   &ReadPolyline  },
            { "SplineEntity",    &WriteThunk<SplineEntity, &WriteSpline>,       &ReadSpline    },
            { "HatchEntity",     &WriteThunk<HatchEntity, &WriteHatch>,         &ReadHatch     },
            { "RegionEntity",    &WriteThunk<RegionEntity, &WriteRegion>,       &ReadRegion    },
            { "DimensionEntity", &WriteThunk<DimensionEntity, &WriteDimension>, &ReadDimension },
            { "LeaderEntity",    &WriteThunk<LeaderEntity, &WriteLeader>,       &ReadLeader    },
            { "MLeaderEntity",   &WriteThunk<MLeaderEntity, &WriteMLeader>,     &ReadMLeader   },
            { "ToleranceEntity", &WriteThunk<ToleranceEntity, &WriteTolerance>, &ReadTolerance },
            { "AttDefEntity",    &WriteThunk<AttDefEntity, &WriteAttDef>,       &ReadAttDef    },
            { "AttribEntity",    &WriteThunk<AttribEntity, &WriteAttrib>,
              [](ISerializer& s, Object::ObjectID id) -> std::unique_ptr<Entity> { return ReadAttribTyped(s, id); } },
            { "BlockEntity",     &WriteThunk<BlockEntity, &WriteBlock>,         &ReadBlock     },
            { "InsertEntity",    &WriteThunk<InsertEntity, &WriteInsert>,       &ReadInsert    },
        };

        const TypeEntry* FindType(const char* name)
        {
            for (const auto& t : kTypes)
                if (std::strcmp(t.name, name) == 0) return &t;
            return nullptr;
        }
    } // namespace

    bool Write(ISerializer& s, const Entity& e)
    {
        const char* name = e.GetTypeInfo()->Name;
        const TypeEntry* entry = FindType(name);
        if (!entry)
            return false;

        std::string type = name;
        s.Value("type", type);
        uint64_t id = e.GetID();
        s.Value("id", id);

        EntityAttr attr = e.GetAttr();
        Ser::Attr(s, attr);

        entry->write(s, e);
        return true;
    }

    std::unique_ptr<Entity> Read(ISerializer& s)
    {
        std::string type;
        s.Value("type", type);
        const TypeEntry* entry = FindType(type.c_str());
        if (!entry)
            return nullptr;

        uint64_t id = Object::InvalidID;
        s.Value("id", id);

        auto e = entry->read(s, id);
        if (!e)
            return nullptr;

        EntityAttr attr = e->GetAttr();
        Ser::Attr(s, attr);
        e->SetAttr(attr);
        return e;
    }
}

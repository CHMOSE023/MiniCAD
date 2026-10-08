// DXF 写入：各实体（BLOCKS 段与 ENTITIES 段共用）
#include "Dxf/Write/DxfWriterImpl.h"
#include "Database/DimensionMeasurement.h"
#include <algorithm>
#include <cmath>

namespace MiniDWG::DxfWrite
{
    namespace
    {
        XYZ Sub(const XYZ& a, const XYZ& b) { return { a.X - b.X, a.Y - b.Y, a.Z - b.Z }; }
        double Dot(const XYZ& a, const XYZ& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
        double Length(const XYZ& a) { return std::sqrt(Dot(a, a)); }
        bool IsZero(const XYZ& a) { return a.X == 0.0 && a.Y == 0.0 && a.Z == 0.0; }

        double AngleBetween(const XYZ& a, const XYZ& b)
        {
            const double la = Length(a), lb = Length(b);
            if (la == 0.0 || lb == 0.0)
                return 0.0;
            return std::acos(std::clamp(Dot(a, b) / (la * lb), -1.0, 1.0));
        }

        // 标注类型（组码 70 的低 3 位）；圆弧标注没有对应的类型码，保留原值
        int DimensionTypeCode(const Dimension& dim)
        {
            if (dynamic_cast<const DimensionLinear*>(&dim)) return 0;
            if (dynamic_cast<const DimensionAligned*>(&dim)) return 1;
            if (dynamic_cast<const DimensionAngular2Line*>(&dim)) return 2;
            if (dynamic_cast<const DimensionDiameter*>(&dim)) return 3;
            if (dynamic_cast<const DimensionRadius*>(&dim)) return 4;
            if (dynamic_cast<const DimensionAngular3Pt*>(&dim)) return 5;
            if (dynamic_cast<const DimensionOrdinate*>(&dim)) return 6;
            return -1;
        }

        // 引线是否有钩线：最后一段与水平方向同向（ACadSharp Leader.HasHookline）
        bool HasHookline(const Leader& leader)
        {
            const std::size_t n = leader.Vertices.size();
            if (n <= 1)
                return false;
            const XYZ last = Sub(leader.Vertices[n - 2], leader.Vertices[n - 1]);
            return std::abs(AngleBetween(last, leader.HorizontalDirection)) < 1e-12 && !IsZero(last);
        }
    }

    bool Writer::CanWriteRaw(const CadObject& object)
    {
        const RawObjectData* raw = RawDataOf(object);
        if (raw == nullptr || CanWriteRawDxf(*raw, m_version))
            return true;
        NotifyOnce("raw:" + raw->DxfName, NotificationType::Info,
                   "未建模的 " + raw->DxfName + " 只能原样写回同一版本的 DXF，未写出");
        return false;
    }

    void Writer::WriteRawGroups(const RawDxfData& raw)
    {
        for (const RawDxfGroup& g : raw.Groups)
        {
            m_out->WriteValue(g.Code, g.Value);
            if (GroupCodeTypeOf(g.Code) == GroupCodeType::Handle && g.Code != 330)
                Enqueue(g.Value.AsHandle());
        }
    }

    bool Writer::IsEntitySupported(const Entity& entity)
    {
        if (!CanWriteRaw(entity))
            return false;
        if (dynamic_cast<const Shape*>(&entity) != nullptr)
        {
            // 与 ACadSharp 默认配置一致：形的名称依赖 SHX 文件，没有建模，不写出
            NotifyOnce("entity:SHAPE", NotificationType::Info, "SHAPE 实体未写出");
            return false;
        }
        if (const auto* insert = dynamic_cast<const Insert*>(&entity);
            insert != nullptr && m_db.FindAs<BlockRecord>(insert->BlockHandle) == nullptr)
        {
            NotifyOnce("insert:" + std::to_string(entity.ObjectHandle), NotificationType::Warning,
                       "块参照 " + DxfValue(HandleValue{ entity.ObjectHandle }).AsString() + " 引用的块不存在，未写出");
            return false;
        }
        return true;
    }

    void Writer::WriteEntity(const Entity& entity, Handle owner, bool paperSpace)
    {
        // BLOCK / ENDBLK / SEQEND / VERTEX / ATTRIB 由块记录或父实体写出，不能单独出现
        if (dynamic_cast<const Block*>(&entity) || dynamic_cast<const BlockEnd*>(&entity)
            || dynamic_cast<const Seqend*>(&entity)
            || ((dynamic_cast<const Vertex*>(&entity) || dynamic_cast<const AttributeEntity*>(&entity))
                && m_db.FindAs<BlockRecord>(owner) != nullptr))
            return;
        if (const auto* table = dynamic_cast<const TableEntity*>(&entity);
            table != nullptr && !CanWriteRawDxf(table->Raw, m_version))
        {
            if (m_db.FindAs<BlockRecord>(table->BlockHandle) == nullptr)
                return;
            NotifyOnce("table-insert", NotificationType::Info,
                       "表格（ACAD_TABLE）只能原样写回同一版本的 DXF，写为引用其匿名块的块参照");
            const auto insert = MakeTableInsert(*table);
            WriteEntity(*insert, owner, paperSpace);
            return;
        }
        if (!IsEntitySupported(entity))
            return;

        WriteString(0, entity.GetDxfName());
        WriteCommonObjectData(entity, owner);
        WriteCommonEntityData(entity, paperSpace);

        if (const auto* unknown = dynamic_cast<const UnknownEntity*>(&entity))
            WriteRawGroups(unknown->Raw.Dxf);
        else if (const auto* arc = dynamic_cast<const Arc*>(&entity))
            WriteArc(*arc);
        else if (const auto* circle = dynamic_cast<const Circle*>(&entity))
            WriteCircle(*circle);
        else if (const auto* dim = dynamic_cast<const Dimension*>(&entity))
            WriteDimension(*dim);
        else if (const auto* ellipse = dynamic_cast<const Ellipse*>(&entity))
            WriteEllipse(*ellipse);
        else if (const auto* face = dynamic_cast<const Face3D*>(&entity))
            WriteFace3D(*face);
        else if (const auto* hatch = dynamic_cast<const Hatch*>(&entity))
            WriteHatch(*hatch);
        else if (const auto* insert = dynamic_cast<const Insert*>(&entity))
            WriteInsert(*insert);
        else if (const auto* leader = dynamic_cast<const Leader*>(&entity))
            WriteLeader(*leader);
        else if (const auto* line = dynamic_cast<const Line*>(&entity))
            WriteLine(*line);
        else if (const auto* lw = dynamic_cast<const LwPolyline*>(&entity))
            WriteLwPolyline(*lw);
        else if (const auto* mline = dynamic_cast<const MLine*>(&entity))
            WriteMLine(*mline);
        else if (const auto* mtext = dynamic_cast<const MText*>(&entity))
            WriteMText(*mtext);
        else if (const auto* ml = dynamic_cast<const MultiLeader*>(&entity))
            WriteMultiLeader(*ml);
        else if (const auto* point = dynamic_cast<const Point*>(&entity))
            WritePoint(*point);
        else if (const auto* pl = dynamic_cast<const Polyline*>(&entity))
            WritePolyline(*pl);
        else if (const auto* image = dynamic_cast<const CadWipeoutBase*>(&entity))
            WriteImage(*image);
        else if (const auto* ray = dynamic_cast<const Ray*>(&entity))
            WriteRay(*ray);
        else if (const auto* solid = dynamic_cast<const Solid*>(&entity))
            WriteSolid(*solid);
        else if (const auto* spline = dynamic_cast<const Spline*>(&entity))
            WriteSpline(*spline);
        else if (const auto* text = dynamic_cast<const TextEntity*>(&entity))
            WriteText(*text);
        else if (const auto* tolerance = dynamic_cast<const Tolerance*>(&entity))
            WriteTolerance(*tolerance);
        else if (const auto* vertex = dynamic_cast<const Vertex*>(&entity))
            WriteVertex(*vertex);
        else if (const auto* vp = dynamic_cast<const Viewport*>(&entity))
            WriteViewport(*vp);
        else if (const auto* xline = dynamic_cast<const XLine*>(&entity))
            WriteXLine(*xline);

        // 扩展数据属于本实体，必须在子实体（ATTRIB / VERTEX）之前写
        WriteExtendedData(entity);

        if (const auto* insert = dynamic_cast<const Insert*>(&entity))
            WriteInsertChildren(*insert, paperSpace);
        else if (const auto* pl = dynamic_cast<const Polyline*>(&entity))
        {
            for (Handle h : pl->Vertices)
            {
                if (const auto* v = m_db.FindAs<Vertex>(h))
                    WriteEntity(*v, pl->ObjectHandle, paperSpace);
            }
            if (const auto* mesh = dynamic_cast<const PolyfaceMesh*>(pl))
            {
                for (Handle h : mesh->Faces)
                {
                    if (const auto* f = m_db.FindAs<VertexFaceRecord>(h))
                        WriteEntity(*f, pl->ObjectHandle, paperSpace);
                }
            }
            WriteSeqend(*pl, paperSpace);
        }
    }

    void Writer::WriteSeqend(const Entity& parent, bool paperSpace)
    {
        const Handle h = SeqendOf(parent);
        const auto* seqend = m_db.FindAs<Seqend>(h);

        WriteString(0, "SEQEND");
        if (seqend != nullptr)
        {
            WriteCommonObjectData(*seqend, parent.ObjectHandle);
            WriteCommonEntityData(*seqend, paperSpace);
            WriteExtendedData(*seqend);
            return;
        }
        // 补的 SEQEND：图层与父实体相同（与 AutoCAD 一致）
        WriteHandle(5, h);
        WriteHandle(330, parent.ObjectHandle);
        WriteSubclass("AcDbEntity");
        if (paperSpace)
            WriteInt(67, 1);
        WriteString(8, NameOf(parent.LayerHandle, "0"));
    }

    void Writer::WriteArc(const Arc& arc)
    {
        WriteCircle(arc);
        WriteSubclass("AcDbArc");
        WriteAngle(50, arc.StartAngle);
        WriteAngle(51, arc.EndAngle);
    }

    void Writer::WriteCircle(const Circle& circle)
    {
        WriteSubclass("AcDbCircle");
        WriteRealIfNot(39, circle.Thickness, 0.0);
        WriteXYZ(10, circle.Center);
        WriteReal(40, circle.Radius);
        WriteXYZIfNot(210, circle.Normal, XYZ::AxisZ());
    }

    void Writer::WriteDimension(const Dimension& dim)
    {
        WriteSubclass("AcDbDimension");
        if (AtLeast(CadVersion::AC1024))
            WriteInt(280, dim.Version);

        const bool hasBlock = m_db.FindAs<BlockRecord>(dim.BlockHandle) != nullptr;
        if (hasBlock)
            WriteString(2, NameOf(dim.BlockHandle));
        WriteXYZ(10, dim.DefinitionPoint);
        WriteXYZ(11, dim.TextMiddlePoint);
        if (!IsZero(dim.InsertionPoint))
            WriteXYZ(12, dim.InsertionPoint);

        int flags = static_cast<int>(dim.Flags);
        if (const int type = DimensionTypeCode(dim); type >= 0)
            flags = (flags & ~0x07) | type;
        if (hasBlock)
            flags |= static_cast<int>(DimensionType::BlockReference);
        WriteInt(70, flags);

        WriteInt(71, dim.AttachmentPoint);
        WriteInt(72, dim.LineSpacingStyle);
        WriteReal(41, dim.LineSpacingFactor);
        WriteReal(42, dim.Measurement != 0.0 ? dim.Measurement : ComputeDimensionMeasurement(dim));
        if (AtLeast(CadVersion::AC1024))
        {
            WriteInt(74, dim.FlipArrow1 ? 1 : 0);
            WriteInt(75, dim.FlipArrow2 ? 1 : 0);
        }
        if (!dim.Text.empty())
            WriteString(1, dim.Text);
        if (dim.TextRotation != 0.0)
            WriteAngle(53, dim.TextRotation);
        if (dim.HorizontalDirection != 0.0)
            WriteAngle(51, dim.HorizontalDirection);
        WriteXYZIfNot(210, dim.Normal, XYZ::AxisZ());
        WriteString(3, NameOf(dim.StyleHandle, "Standard"));

        if (const auto* aligned = dynamic_cast<const DimensionAligned*>(&dim))
        {
            WriteSubclass("AcDbAlignedDimension");
            WriteXYZ(13, aligned->FirstPoint);
            WriteXYZ(14, aligned->SecondPoint);
            if (aligned->ExtLineRotation != 0.0)
                WriteReal(52, aligned->ExtLineRotation);
            if (const auto* linear = dynamic_cast<const DimensionLinear*>(&dim))
            {
                WriteAngle(50, linear->Rotation);
                WriteSubclass("AcDbRotatedDimension");
            }
        }
        else if (const auto* radius = dynamic_cast<const DimensionRadius*>(&dim))
        {
            WriteSubclass("AcDbRadialDimension");
            WriteXYZ(15, radius->AngleVertex);
            WriteReal(40, radius->LeaderLength);
        }
        else if (const auto* diameter = dynamic_cast<const DimensionDiameter*>(&dim))
        {
            WriteSubclass("AcDbDiametricDimension");
            WriteXYZ(15, diameter->AngleVertex);
            WriteReal(40, diameter->LeaderLength);
        }
        else if (const auto* angular2 = dynamic_cast<const DimensionAngular2Line*>(&dim))
        {
            WriteSubclass("AcDb2LineAngularDimension");
            WriteXYZ(13, angular2->FirstPoint);
            WriteXYZ(14, angular2->SecondPoint);
            WriteXYZ(15, angular2->AngleVertex);
            WriteXYZ(16, angular2->DimensionArc);
        }
        else if (const auto* angular3 = dynamic_cast<const DimensionAngular3Pt*>(&dim))
        {
            WriteSubclass("AcDb3PointAngularDimension");
            WriteXYZ(13, angular3->FirstPoint);
            WriteXYZ(14, angular3->SecondPoint);
            WriteXYZ(15, angular3->AngleVertex);
        }
        else if (const auto* ordinate = dynamic_cast<const DimensionOrdinate*>(&dim))
        {
            WriteSubclass("AcDbOrdinateDimension");
            WriteXYZ(13, ordinate->FeatureLocation);
            WriteXYZ(14, ordinate->LeaderEndpoint);
        }
        else if (const auto* arc = dynamic_cast<const DimensionArc*>(&dim))
        {
            WriteSubclass("AcDbArcDimension");
            WriteXYZ(13, arc->FirstPoint);
            WriteXYZ(14, arc->SecondPoint);
            WriteXYZ(15, arc->Center);
            WriteInt(70, arc->IsPartial ? 1 : 0);
            WriteReal(40, arc->StartAngle);
            WriteReal(41, arc->EndAngle);
            WriteInt(71, arc->HasLeader ? 1 : 0);
            if (arc->HasLeader)
            {
                WriteXYZ(16, arc->LeaderPoint1);
                WriteXYZ(17, arc->LeaderPoint2);
            }
        }
    }

    void Writer::WriteEllipse(const Ellipse& ellipse)
    {
        WriteSubclass("AcDbEllipse");
        WriteXYZ(10, ellipse.Center);
        WriteXYZ(11, ellipse.MajorAxisEndPoint);
        WriteXYZ(210, ellipse.Normal);
        WriteReal(40, ellipse.RadiusRatio);
        WriteReal(41, ellipse.StartParameter);
        WriteReal(42, ellipse.EndParameter);
    }

    void Writer::WriteFace3D(const Face3D& face)
    {
        WriteSubclass("AcDbFace");
        WriteXYZ(10, face.FirstCorner);
        WriteXYZ(11, face.SecondCorner);
        WriteXYZ(12, face.ThirdCorner);
        WriteXYZ(13, face.FourthCorner);
        if (face.Flags != InvisibleEdgeFlags{})
            WriteInt(70, face.Flags);
    }

    void Writer::WriteHatch(const Hatch& hatch)
    {
        WriteSubclass("AcDbHatch");
        WriteReal(10, 0.0);
        WriteReal(20, 0.0);
        WriteReal(30, hatch.Elevation);
        WriteXYZ(210, hatch.Normal);
        WriteString(2, hatch.Pattern.Name.empty() && hatch.IsSolid ? std::string_view("SOLID")
                                                                   : std::string_view(hatch.Pattern.Name));
        WriteInt(70, hatch.IsSolid ? 1 : 0);
        WriteInt(71, hatch.IsAssociative ? 1 : 0);
        WriteInt(91, hatch.Paths.size());
        for (const HatchBoundaryPath& path : hatch.Paths)
            WriteHatchBoundaryPath(path);

        WriteInt(75, hatch.Style);
        WriteInt(76, hatch.PatternType);
        if (!hatch.IsSolid)
        {
            WriteAngle(52, hatch.PatternAngle);
            WriteReal(41, hatch.PatternScale);
            WriteInt(77, hatch.IsDouble ? 1 : 0);
            WriteInt(78, hatch.Pattern.Lines.size());
            for (const HatchPatternLine& line : hatch.Pattern.Lines)
            {
                WriteAngle(53, line.Angle);
                WriteReal(43, line.BasePoint.X);
                WriteReal(44, line.BasePoint.Y);
                WriteReal(45, line.Offset.X);
                WriteReal(46, line.Offset.Y);
                WriteInt(79, line.DashLengths.size());
                for (double dash : line.DashLengths)
                    WriteReal(49, dash);
            }
        }

        const bool derived = std::any_of(hatch.Paths.begin(), hatch.Paths.end(), [](const HatchBoundaryPath& p) {
            return HasFlag(p.Flags, BoundaryPathFlags::Derived);
        });
        // 像素尺寸只用于"拾取点"生成的边界；其他情况 AutoCAD 不接受 47（要求紧接 98）
        if (derived)
            WriteReal(47, hatch.PixelSize);

        WriteInt(98, hatch.SeedPoints.size());
        for (const XY& seed : hatch.SeedPoints)
            WriteXY(10, seed);

        const HatchGradientPattern& gradient = hatch.GradientColor;
        // 只写启用的渐变：DWG 中未启用的填充也带渐变数据，写成 450=0 加渐变组码时 AutoCAD 报"对象提前结束"并放弃整张图
        if (AtLeast(CadVersion::AC1018) && gradient.Enabled)
        {
            // 顺序与 AutoCAD 一致（AutoCAD 按顺序读，452 在 461 之后）
            WriteInt(450, gradient.Enabled ? 1 : 0);
            WriteInt(451, gradient.Reserved);
            WriteReal(460, gradient.Angle);
            WriteReal(461, gradient.Shift);
            WriteInt(452, gradient.IsSingleColorGradient ? 1 : 0);
            WriteReal(462, gradient.ColorTint);
            WriteInt(453, gradient.Colors.size());
            for (const GradientColor& c : gradient.Colors)
            {
                WriteReal(463, c.Value);
                WriteInt(63, c.Color.ApproxIndex());
                if (c.Color.IsTrueColor())
                    WriteInt(421, c.Color.TrueColor());
            }
            WriteString(470, gradient.Name);
        }
    }

    void Writer::WriteHatchBoundaryPath(const HatchBoundaryPath& path)
    {
        WriteInt(92, path.Flags);
        if (!HasFlag(path.Flags, BoundaryPathFlags::Polyline))
            WriteInt(93, path.Edges.size());
        for (const auto& edge : path.Edges)
        {
            if (edge)
                WriteHatchEdge(*edge);
        }

        std::vector<Handle> sources;
        for (Handle h : path.Entities)
        {
            if (Exists(h))
                sources.push_back(h);
        }
        WriteInt(97, sources.size());
        for (Handle h : sources)
            WriteHandle(330, h);
    }

    void Writer::WriteHatchAngles(double startAngle, double endAngle)
    {
        WriteAngle(50, startAngle);
        WriteAngle(51, endAngle);
    }

    void Writer::WriteHatchEdge(const HatchBoundaryPathEdge& edge)
    {
        if (const auto* pl = dynamic_cast<const HatchBoundaryPathPolyline*>(&edge))
        {
            // 顶点的 Z 是凸度
            const bool hasBulge = std::any_of(pl->Vertices.begin(), pl->Vertices.end(),
                                              [](const XYZ& v) { return v.Z != 0.0; });
            WriteInt(72, hasBulge ? 1 : 0);
            WriteInt(73, pl->IsClosed ? 1 : 0);
            WriteInt(93, pl->Vertices.size());
            for (const XYZ& v : pl->Vertices)
            {
                WriteXY(10, XY{ v.X, v.Y });
                if (hasBulge)
                    WriteReal(42, v.Z);
            }
            return;
        }

        WriteInt(72, edge.GetType());
        if (const auto* line = dynamic_cast<const HatchBoundaryPathLine*>(&edge))
        {
            WriteXY(10, line->Start);
            WriteXY(11, line->End);
        }
        else if (const auto* arc = dynamic_cast<const HatchBoundaryPathArc*>(&edge))
        {
            WriteXY(10, arc->Center);
            WriteReal(40, arc->Radius);
            WriteHatchAngles(arc->StartAngle, arc->EndAngle);
            WriteInt(73, arc->CounterClockWise ? 1 : 0);
        }
        else if (const auto* ellipse = dynamic_cast<const HatchBoundaryPathEllipse*>(&edge))
        {
            WriteXY(10, ellipse->Center);
            WriteXY(11, ellipse->MajorAxisEndPoint);
            WriteReal(40, ellipse->RadiusRatio);
            WriteHatchAngles(ellipse->StartAngle, ellipse->EndAngle);
            WriteInt(73, ellipse->CounterClockWise ? 1 : 0);
        }
        else if (const auto* spline = dynamic_cast<const HatchBoundaryPathSpline*>(&edge))
        {
            WriteInt(94, spline->Degree);
            WriteInt(73, spline->IsRational ? 1 : 0);
            WriteInt(74, spline->IsPeriodic ? 1 : 0);
            WriteInt(95, spline->Knots.size());
            WriteInt(96, spline->ControlPoints.size());
            for (double knot : spline->Knots)
                WriteReal(40, knot);
            for (const XYZ& p : spline->ControlPoints)
            {
                WriteReal(10, p.X);
                WriteReal(20, p.Y);
                if (spline->IsRational)
                    WriteReal(42, p.Z);     // 权重
            }
            // 拟合数据从 R2010 开始才有
            if (AtLeast(CadVersion::AC1024))
            {
                WriteInt(97, spline->FitPoints.size());
                for (const XY& p : spline->FitPoints)
                    WriteXY(11, p);
                if (!spline->FitPoints.empty())
                {
                    WriteXY(12, spline->StartTangent);
                    WriteXY(13, spline->EndTangent);
                }
            }
        }
    }

    void Writer::WriteInsert(const Insert& insert)
    {
        const bool isArray = insert.RowCount > 1 || insert.ColumnCount > 1;
        WriteSubclass(isArray ? "AcDbMInsertBlock" : "AcDbBlockReference");

        const bool hasAttributes = std::any_of(insert.Attributes.begin(), insert.Attributes.end(),
            [&](Handle a) { return m_db.FindAs<AttributeEntity>(a) != nullptr; });
        if (hasAttributes)
            WriteInt(66, 1);
        WriteString(2, NameOf(insert.BlockHandle));
        WriteXYZ(10, insert.InsertPoint);
        WriteRealIfNot(41, insert.XScale, 1.0);
        WriteRealIfNot(42, insert.YScale, 1.0);
        WriteRealIfNot(43, insert.ZScale, 1.0);
        if (insert.Rotation != 0.0)
            WriteAngle(50, insert.Rotation);
        if (insert.ColumnCount != 1)
            WriteInt(70, insert.ColumnCount);
        if (insert.RowCount != 1)
            WriteInt(71, insert.RowCount);
        WriteRealIfNot(44, insert.ColumnSpacing, 0.0);
        WriteRealIfNot(45, insert.RowSpacing, 0.0);
        WriteXYZIfNot(210, insert.Normal, XYZ::AxisZ());
    }

    void Writer::WriteInsertChildren(const Insert& insert, bool paperSpace)
    {
        bool any = false;
        for (Handle h : insert.Attributes)
        {
            if (const auto* att = m_db.FindAs<AttributeEntity>(h))
            {
                WriteEntity(*att, insert.ObjectHandle, paperSpace);
                any = true;
            }
        }
        if (any)
            WriteSeqend(insert, paperSpace);
    }

    void Writer::WriteLeader(const Leader& leader)
    {
        WriteSubclass("AcDbLeader");
        WriteString(3, NameOf(leader.StyleHandle, "Standard"));
        WriteInt(71, leader.ArrowHeadEnabled ? 1 : 0);
        WriteInt(72, leader.PathType);
        WriteInt(73, leader.CreationType);
        WriteInt(74, leader.HookLineDirection);
        WriteInt(75, HasHookline(leader) ? 1 : 0);
        WriteReal(40, leader.TextHeight);
        WriteReal(41, leader.TextWidth);
        WriteInt(76, leader.Vertices.size());
        for (const XYZ& v : leader.Vertices)
            WriteXYZ(10, v);
        WriteRef(340, leader.AssociatedAnnotationHandle);
        WriteXYZIfNot(210, leader.Normal, XYZ::AxisZ());
        WriteXYZIfNot(211, leader.HorizontalDirection, XYZ::AxisX());
        WriteXYZIfNot(212, leader.BlockOffset, XYZ{});
        WriteXYZIfNot(213, leader.AnnotationOffset, XYZ{});
    }

    void Writer::WriteLine(const Line& line)
    {
        WriteSubclass("AcDbLine");
        WriteRealIfNot(39, line.Thickness, 0.0);
        WriteXYZ(10, line.StartPoint);
        WriteXYZ(11, line.EndPoint);
        WriteXYZIfNot(210, line.Normal, XYZ::AxisZ());
    }

    void Writer::WriteLwPolyline(const LwPolyline& pl)
    {
        WriteSubclass("AcDbPolyline");
        WriteInt(90, pl.Vertices.size());
        WriteInt(70, pl.Flags);
        WriteReal(43, pl.ConstantWidth);
        WriteRealIfNot(38, pl.Elevation, 0.0);
        WriteRealIfNot(39, pl.Thickness, 0.0);
        for (const LwPolylineVertex& v : pl.Vertices)
        {
            WriteXY(10, v.Location);
            if (AtLeast(CadVersion::AC1024) && v.Id != 0)
                WriteInt(91, v.Id);
            if (v.StartWidth != 0.0 || v.EndWidth != 0.0)
            {
                WriteReal(40, v.StartWidth);
                WriteReal(41, v.EndWidth);
            }
            if (v.Bulge != 0.0)
                WriteReal(42, v.Bulge);
        }
        WriteXYZIfNot(210, pl.Normal, XYZ::AxisZ());
    }

    void Writer::WriteMLine(const MLine& mline)
    {
        const auto* style = m_db.FindAs<MLineStyle>(mline.StyleHandle);
        std::size_t elements = style != nullptr ? style->Elements.size() : 0;
        if (style == nullptr && !mline.Vertices.empty())
            elements = mline.Vertices.front().Segments.size();

        WriteSubclass("AcDbMline");
        WriteString(2, style != nullptr && !style->Name.empty() ? std::string_view(style->Name) : std::string_view("STANDARD"));
        WriteRefOrNull(340, mline.StyleHandle);
        WriteReal(40, mline.ScaleFactor);
        WriteInt(70, mline.Justification);
        WriteInt(71, mline.Flags);
        WriteInt(72, mline.Vertices.size());
        WriteInt(73, elements);
        WriteXYZ(10, mline.StartPoint);
        WriteXYZ(210, mline.Normal);
        for (const MLineVertex& v : mline.Vertices)
        {
            WriteXYZ(11, v.Position);
            WriteXYZ(12, v.Direction);
            WriteXYZ(13, v.Miter);
            for (const MLineVertexSegment& s : v.Segments)
            {
                WriteInt(74, s.Parameters.size());
                for (double p : s.Parameters)
                    WriteReal(41, p);
                WriteInt(75, s.AreaFillParameters.size());
                for (double p : s.AreaFillParameters)
                    WriteReal(42, p);
            }
        }
    }

    void Writer::WriteMText(const MText& mtext)
    {
        WriteSubclass("AcDbMText");
        WriteXYZ(10, mtext.InsertPoint);
        WriteReal(40, mtext.Height);
        WriteReal(41, mtext.RectangleWidth);
        if (AtLeast(CadVersion::AC1021))
            WriteReal(46, mtext.RectangleHeight);
        WriteInt(71, mtext.AttachmentPoint);
        WriteInt(72, mtext.DrawingDirection);
        WriteLongText(1, 3, mtext.Value);
        if (const auto* style = m_db.FindAs<TextStyle>(mtext.StyleHandle))
            WriteString(7, style->Name);
        WriteXYZIfNot(210, mtext.Normal, XYZ::AxisZ());
        WriteXYZ(11, mtext.AlignmentPoint);
        WriteReal(42, mtext.HorizontalWidth);
        WriteReal(43, mtext.VerticalHeight);
        WriteInt(73, mtext.LineSpacingStyle);
        WriteReal(44, mtext.LineSpacing);

        if (AtLeast(CadVersion::AC1018) && mtext.BackgroundFillFlags != BackgroundFillFlags::None)
        {
            WriteInt(90, mtext.BackgroundFillFlags);
            WriteInt(63, mtext.BackgroundColor.ApproxIndex());
            if (mtext.BackgroundColor.IsTrueColor())
                WriteInt(421, mtext.BackgroundColor.TrueColor());
            WriteReal(45, mtext.BackgroundScale);
            WriteInt(441, mtext.BackgroundTransparency.IsByLayer()
                              ? 0 : Transparency::ToAlphaValue(mtext.BackgroundTransparency));
        }

        const MTextTextColumnData& col = mtext.ColumnData;
        if (AtLeast(CadVersion::AC1032) && col.ColumnType != ColumnType::NoColumns)
        {
            WriteString(101, "Embedded Object");
            WriteInt(70, 1);
            WriteXYZ(10, mtext.AlignmentPoint);
            WriteXYZ(11, mtext.InsertPoint);
            WriteReal(40, mtext.RectangleWidth);
            WriteReal(41, mtext.RectangleHeight);
            WriteReal(42, mtext.HorizontalWidth);
            WriteReal(43, mtext.VerticalHeight);
            WriteInt(71, col.ColumnType);
            WriteInt(72, col.ColumnCount);
            WriteReal(44, col.Width);
            WriteReal(45, col.Gutter);
            WriteInt(73, col.AutoHeight ? 1 : 0);
            WriteInt(74, col.FlowReversed ? 1 : 0);
            for (double h : col.Heights)
                WriteReal(46, h);
        }
    }

    void Writer::WritePoint(const Point& point)
    {
        WriteSubclass("AcDbPoint");
        WriteXYZ(10, point.Location);
        WriteRealIfNot(39, point.Thickness, 0.0);
        WriteXYZIfNot(210, point.Normal, XYZ::AxisZ());
        if (point.Rotation != 0.0)
            WriteAngle(50, point.Rotation);
    }

    void Writer::WritePolyline(const Polyline& pl)
    {
        if (const auto* mesh = dynamic_cast<const PolyfaceMesh*>(&pl))
        {
            WriteSubclass("AcDbPolyFaceMesh");
            WriteInt(66, 1);
            WriteXYZ(10, XYZ{ 0.0, 0.0, pl.Elevation });
            WriteRealIfNot(39, pl.Thickness, 0.0);
            WriteInt(70, static_cast<int>(pl.Flags) | static_cast<int>(PolylineFlags::PolyfaceMesh));
            WriteInt(71, mesh->Vertices.size());
            WriteInt(72, mesh->Faces.size());
            WriteXYZIfNot(210, pl.Normal, XYZ::AxisZ());
            return;
        }
        if (const auto* mesh = dynamic_cast<const PolygonMesh*>(&pl))
        {
            WriteSubclass("AcDbPolygonMesh");
            WriteInt(66, 1);
            WriteXYZ(10, XYZ{ 0.0, 0.0, pl.Elevation });
            WriteRealIfNot(39, pl.Thickness, 0.0);
            WriteInt(70, static_cast<int>(pl.Flags) | static_cast<int>(PolylineFlags::PolygonMesh));
            WriteInt(71, mesh->MVertexCount);
            WriteInt(72, mesh->NVertexCount);
            WriteInt(73, mesh->MSmoothSurfaceDensity);
            WriteInt(74, mesh->NSmoothSurfaceDensity);
            WriteInt(75, pl.SmoothSurface);
            WriteXYZIfNot(210, pl.Normal, XYZ::AxisZ());
            return;
        }
        const bool is3D = dynamic_cast<const Polyline3D*>(&pl) != nullptr;
        WriteSubclass(is3D ? "AcDb3dPolyline" : "AcDb2dPolyline");
        WriteInt(66, 1);    // R13 之前要求的"后跟顶点"标志，新版本忽略
        WriteReal(10, 0.0);
        WriteReal(20, 0.0);
        WriteReal(30, pl.Elevation);
        WriteRealIfNot(39, pl.Thickness, 0.0);

        int flags = static_cast<int>(pl.Flags);
        if (is3D)
            flags |= static_cast<int>(PolylineFlags::Polyline3D);
        else
            flags &= ~static_cast<int>(PolylineFlags::Polyline3D);
        WriteInt(70, flags);
        WriteRealIfNot(40, pl.StartWidth, 0.0);
        WriteRealIfNot(41, pl.EndWidth, 0.0);
        if (pl.SmoothSurface != SmoothSurfaceType{})
            WriteInt(75, pl.SmoothSurface);
        WriteXYZIfNot(210, pl.Normal, XYZ::AxisZ());
    }

    void Writer::WriteRay(const Ray& ray)
    {
        WriteSubclass("AcDbRay");
        WriteXYZ(10, ray.StartPoint);
        WriteXYZ(11, ray.Direction);
    }

    void Writer::WriteSolid(const Solid& solid)
    {
        WriteSubclass("AcDbTrace");
        WriteXYZ(10, solid.FirstCorner);
        WriteXYZ(11, solid.SecondCorner);
        WriteXYZ(12, solid.ThirdCorner);
        WriteXYZ(13, solid.FourthCorner);
        WriteRealIfNot(39, solid.Thickness, 0.0);
        WriteXYZIfNot(210, solid.Normal, XYZ::AxisZ());
    }

    void Writer::WriteSpline(const Spline& spline)
    {
        WriteSubclass("AcDbSpline");
        if (HasFlag(spline.Flags, SplineFlags::Planar))
            WriteXYZ(210, spline.Normal);
        WriteInt(70, spline.Flags);
        WriteInt(71, spline.Degree);
        WriteInt(72, spline.Knots.size());
        WriteInt(73, spline.ControlPoints.size());
        WriteInt(74, spline.FitPoints.size());
        WriteReal(42, spline.KnotTolerance);
        WriteReal(43, spline.ControlPointTolerance);
        if (!spline.FitPoints.empty())
            WriteReal(44, spline.FitTolerance);
        if (!IsZero(spline.StartTangent))
            WriteXYZ(12, spline.StartTangent);
        if (!IsZero(spline.EndTangent))
            WriteXYZ(13, spline.EndTangent);
        for (double knot : spline.Knots)
            WriteReal(40, knot);
        for (double weight : spline.Weights)
            WriteReal(41, weight);
        for (const XYZ& p : spline.ControlPoints)
            WriteXYZ(10, p);
        for (const XYZ& p : spline.FitPoints)
            WriteXYZ(11, p);
    }

    void Writer::WriteText(const TextEntity& text)
    {
        WriteSubclass("AcDbText");
        WriteRealIfNot(39, text.Thickness, 0.0);
        WriteXYZ(10, text.InsertPoint);
        WriteReal(40, text.Height);
        WriteString(1, text.Value);
        if (text.Rotation != 0.0)
            WriteAngle(50, text.Rotation);
        WriteRealIfNot(41, text.WidthFactor, 1.0);
        if (text.ObliqueAngle != 0.0)
            WriteAngle(51, text.ObliqueAngle);
        if (const auto* style = m_db.FindAs<TextStyle>(text.StyleHandle))
            WriteString(7, style->Name);
        if (text.Mirror != TextMirrorFlag::None)
            WriteInt(71, text.Mirror);
        if (text.HorizontalAlignment != TextHorizontalAlignment::Left)
            WriteInt(72, text.HorizontalAlignment);
        WriteXYZ(11, text.AlignmentPoint);
        WriteXYZIfNot(210, text.Normal, XYZ::AxisZ());

        const bool verticalAligned = text.VerticalAlignment != TextVerticalAlignmentType::Baseline;
        if (const auto* att = dynamic_cast<const AttributeBase*>(&text))
        {
            const auto* def = dynamic_cast<const AttributeDefinition*>(&text);
            WriteSubclass(def != nullptr ? "AcDbAttributeDefinition" : "AcDbAttribute");
            // R2010 起开头的 280 是版本号（只能为 0），末尾的 280 是"锁定位置"；R2007 只有末尾的 280。
            // 读取时两者都进 Version，最后读到的锁定标志为准
            if (AtLeast(CadVersion::AC1024))
                WriteInt(280, 0);
            if (att->AttributeType != AttributeType::SingleLine)
                NotifyOnce("attribute:multiline", NotificationType::Info, "多行属性（内嵌 MTEXT 尚未建模）按单行写出");
            if (def != nullptr)
                WriteString(3, def->Prompt);
            WriteString(2, att->Tag);
            WriteInt(70, att->Flags);
            if (verticalAligned)
                WriteInt(74, text.VerticalAlignment);
            if (AtLeast(CadVersion::AC1021))
                WriteInt(280, att->Version != 0 ? 1 : 0);
        }
        else
        {
            WriteSubclass("AcDbText");
            if (verticalAligned)
                WriteInt(73, text.VerticalAlignment);
        }
    }

    void Writer::WriteTolerance(const Tolerance& tolerance)
    {
        WriteSubclass("AcDbFcf");
        WriteString(3, NameOf(tolerance.StyleHandle, "Standard"));
        WriteXYZ(10, tolerance.InsertionPoint);
        WriteString(1, tolerance.Text);
        WriteXYZIfNot(210, tolerance.Normal, XYZ::AxisZ());
        WriteXYZ(11, tolerance.Direction);
    }

    void Writer::WriteVertex(const Vertex& vertex)
    {
        // 网格的顶点与面（与 AutoCAD 一致：面记录没有 AcDbVertex）
        if (const auto* face = dynamic_cast<const VertexFaceRecord*>(&vertex))
        {
            WriteSubclass("AcDbFaceRecord");
            WriteXYZ(10, XYZ{});
            WriteInt(70, static_cast<int>(vertex.Flags) | static_cast<int>(VertexFlags::PolyFaceMeshVertex));
            WriteInt(71, face->Index1);
            WriteInt(72, face->Index2);
            WriteInt(73, face->Index3);
            if (face->Index4 != 0)
                WriteInt(74, face->Index4);
            return;
        }
        if (dynamic_cast<const VertexFaceMesh*>(&vertex) != nullptr || dynamic_cast<const PolygonMeshVertex*>(&vertex) != nullptr)
        {
            const bool pface = dynamic_cast<const VertexFaceMesh*>(&vertex) != nullptr;
            WriteSubclass("AcDbVertex");
            WriteSubclass(pface ? "AcDbPolyFaceMeshVertex" : "AcDbPolygonMeshVertex");
            WriteXYZ(10, vertex.Location);
            int flags = static_cast<int>(vertex.Flags) | static_cast<int>(VertexFlags::PolygonMesh3D);
            if (pface)
                flags |= static_cast<int>(VertexFlags::PolyFaceMeshVertex);
            WriteInt(70, flags);
            return;
        }
        const bool is3D = dynamic_cast<const Vertex3D*>(&vertex) != nullptr;
        WriteSubclass("AcDbVertex");
        WriteSubclass(is3D ? "AcDb3dPolylineVertex" : "AcDb2dVertex");
        WriteXYZ(10, vertex.Location);
        WriteRealIfNot(40, vertex.StartWidth, 0.0);
        WriteRealIfNot(41, vertex.EndWidth, 0.0);
        WriteRealIfNot(42, vertex.Bulge, 0.0);

        int flags = static_cast<int>(vertex.Flags);
        if (is3D)
            flags |= static_cast<int>(VertexFlags::PolylineVertex3D);
        WriteInt(70, flags);
        if (HasFlag(vertex.Flags, VertexFlags::CurveFitTangent) || vertex.CurveTangent != 0.0)
            WriteAngle(50, vertex.CurveTangent);
        if (AtLeast(CadVersion::AC1024) && vertex.Id != 0)
            WriteInt(91, vertex.Id);
    }

    void Writer::WriteViewport(const Viewport& vp)
    {
        // 视口编号：所在块中视口的次序，从 1 开始（第一个是整张图纸的视口）
        int id = 1;
        if (const auto* record = m_db.FindAs<BlockRecord>(vp.OwnerHandle))
        {
            for (Handle h : record->Entities)
            {
                if (h == vp.ObjectHandle)
                    break;
                if (m_db.FindAs<Viewport>(h) != nullptr)
                    ++id;
            }
        }

        WriteSubclass("AcDbViewport");
        WriteXYZ(10, vp.Center);
        WriteReal(40, vp.Width);
        WriteReal(41, vp.Height);
        WriteInt(68, vp.ActiveStatus);
        WriteInt(69, id);
        WriteXY(12, vp.ViewCenter);
        WriteXY(13, vp.SnapBase);
        WriteXY(14, vp.SnapSpacing);
        WriteXY(15, vp.GridSpacing);
        WriteXYZ(16, vp.ViewDirection);
        WriteXYZ(17, vp.ViewTarget);
        WriteReal(42, vp.LensLength);
        WriteReal(43, vp.FrontClipPlane);
        WriteReal(44, vp.BackClipPlane);
        WriteReal(45, vp.ViewHeight);
        WriteAngle(50, vp.SnapAngle);
        WriteAngle(51, vp.TwistAngle);
        WriteInt(72, vp.CircleZoomPercent);
        for (Handle layer : vp.FrozenLayers)
            WriteRef(331, layer);
        WriteInt(90, vp.Status);
        WriteRef(340, vp.BoundaryHandle);
        WriteString(1, vp.StyleSheetName);
        WriteInt(281, vp.RenderMode);
        WriteInt(71, vp.UcsPerViewport ? 1 : 0);
        WriteInt(74, vp.DisplayUcsIcon ? 1 : 0);
        WriteXYZ(110, vp.UcsOrigin);
        WriteXYZ(111, vp.UcsXAxis);
        WriteXYZ(112, vp.UcsYAxis);
        WriteInt(79, vp.UcsOrthographicType);
        WriteReal(146, vp.Elevation);
        if (!AtLeast(CadVersion::AC1021))
            return;
        WriteInt(170, vp.ShadePlotMode);
        WriteInt(61, vp.MajorGridLineFrequency);
        WriteRef(348, vp.VisualStyleHandle);
        WriteInt(292, vp.UseDefaultLighting ? 1 : 0);
        WriteInt(282, vp.DefaultLightingType);
        WriteReal(141, vp.Brightness);
        WriteReal(142, vp.Contrast);
        WriteInt(63, vp.AmbientLightColor.ApproxIndex());
        if (vp.AmbientLightColor.IsTrueColor())
            WriteInt(421, vp.AmbientLightColor.TrueColor());
    }

    void Writer::WriteImage(const CadWipeoutBase& image)
    {
        WriteSubclass(image.GetSubclassMarker());
        WriteInt(90, image.ClassVersion);
        WriteXYZ(10, image.InsertPoint);
        WriteXYZ(11, image.UVector);
        WriteXYZ(12, image.VVector);
        WriteXY(13, image.Size);
        WriteRefOrNull(340, image.DefinitionHandle);
        WriteInt(70, image.Flags);
        WriteInt(280, image.ClippingState ? 1 : 0);
        WriteInt(281, image.Brightness);
        WriteInt(282, image.Contrast);
        WriteInt(283, image.Fade);
        if (AtLeast(CadVersion::AC1024))
            WriteBool(290, image.ClipMode != ClipMode::Outside);
        // 图像定义反应器不在字典中，由图像引用并随之写出
        Handle reactor = Ref(image.DefinitionReactorHandle);
        if (reactor == kNullHandle)
        {
            auto it = m_imageReactors.find(image.ObjectHandle);
            reactor = it != m_imageReactors.end() ? it->second : kNullHandle;
        }
        WriteHandle(360, reactor);
        Enqueue(reactor);
        // 裁剪类型：两个点是矩形（1），多于两个是多边形（2）
        // DXF 中多边形边界首尾相同（AutoCAD 写出的样子），DWG 中不重复首点：读自 DWG 的补上闭合点
        const auto& clip = image.ClipBoundaryVertices;
        const bool polygon = clip.size() > 2;
        const bool addClosing = polygon && !(clip.front() == clip.back());
        WriteInt(71, polygon ? 2 : 1);
        WriteInt(91, clip.size() + (addClosing ? 1 : 0));
        for (const XY& v : clip)
            WriteXY(14, v);
        if (addClosing)
            WriteXY(14, clip.front());
    }

    void Writer::WriteXLine(const XLine& xline)
    {
        WriteSubclass("AcDbXline");
        WriteXYZ(10, xline.FirstPoint);
        WriteXYZ(11, xline.Direction);
    }
}

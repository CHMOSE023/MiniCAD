// DWG 读取：实体（对应 ACadSharp DwgObjectReader 中的实体部分）
#include "Dwg/Read/DwgReaderImpl.h"
#include <algorithm>
#include <cmath>

namespace MiniDWG::DwgRead
{
    namespace
    {
        XYZ Sub(const XYZ& a, const XYZ& b) { return { a.X - b.X, a.Y - b.Y, a.Z - b.Z }; }
        XYZ Cross(const XYZ& a, const XYZ& b)
        {
            return { a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X };
        }
        double Dot(const XYZ& a, const XYZ& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
        double Length(const XYZ& a) { return std::sqrt(Dot(a, a)); }

        // 样条的平面、直线标志（DXF 组码 70 的 8、16 位）DWG 中不存，AutoCAD 按定义点算出；
        // 平面时 normal 返回平面法向
        SplineFlags SplineShapeFlags(const std::vector<XYZ>& points, XYZ& normal)
        {
            if (points.empty())
                return SplineFlags::None;
            double size = 0.0;
            for (const XYZ& p : points)
                size = std::max(size, Length(Sub(p, points[0])));
            const double tol = 1e-10 * std::max(1.0, size);

            // 离首点最远的点定方向，再找离该直线最远的点定平面
            XYZ dir{};
            for (const XYZ& p : points)
            {
                if (Length(Sub(p, points[0])) > Length(dir))
                    dir = Sub(p, points[0]);
            }
            if (Length(dir) <= tol)
                return SplineFlags::Planar | SplineFlags::Linear;
            XYZ n{};
            for (const XYZ& p : points)
            {
                const XYZ c = Cross(dir, Sub(p, points[0]));
                if (Length(c) > Length(n))
                    n = c;
            }
            if (Length(n) <= tol * Length(dir))
                return SplineFlags::Planar | SplineFlags::Linear;
            const double len = Length(n);
            n = { n.X / len, n.Y / len, n.Z / len };
            for (const XYZ& p : points)
            {
                if (std::abs(Dot(n, Sub(p, points[0]))) > tol)
                    return SplineFlags::None;
            }
            if (n.Z < 0.0 || (n.Z == 0.0 && (n.Y < 0.0 || (n.Y == 0.0 && n.X < 0.0))))
                n = { -n.X, -n.Y, -n.Z };
            // 去掉 -0
            normal = { n.X + 0.0, n.Y + 0.0, n.Z + 0.0 };
            return SplineFlags::Planar;
        }
    }

    // ── 公共数据 ───────────────────────────────────────────────────

    void Reader::ReadCommonEntityData(Entity& entity, ObjectInfo& info)
    {
        ReadCommonData(entity);

        // 代理图形：只有未建模的实体保留（ReadUnknown 取走）
        m_lastProxyGraphics.clear();
        if (m_objReader.ReadBit())
        {
            const std::int64_t size = R2010Plus() ? m_objReader.ReadBitLongLong() : m_objReader.ReadRawLong();
            if (m_objReader.CheckCount(size, 8))
                m_lastProxyGraphics = m_objReader.ReadBytes(static_cast<std::size_t>(size));
        }
        if (R13_14Only())
            UpdateHandleReader();
        ReadEntityMode(entity, info);
    }

    void Reader::ReadEntityMode(Entity& entity, ObjectInfo& info)
    {
        DwgBitReader& r = m_objReader;
        info.EntityMode = r.Read2Bits();
        if (info.EntityMode == 0)
            entity.OwnerHandle = m_handleReader.ReadHandle(entity.ObjectHandle);

        ReadReactorsAndXDictionary(entity);

        if (R13_14Only())
        {
            entity.LayerHandle = HandleRef();
            if (!r.ReadBit())
                entity.LineTypeHandle = HandleRef();
        }

        // R13～R2000：前后实体句柄（Nolinks 为 0 时存在，为 1 时就是句柄 ±1）
        if (!R2004Plus())
        {
            if (!r.ReadBit())
            {
                info.HasLinks = true;
                info.PrevEntity = HandleRef(entity.ObjectHandle);
                info.NextEntity = HandleRef(entity.ObjectHandle);
            }
            else
            {
                for (Handle h : { entity.ObjectHandle - 1, entity.ObjectHandle + 1 })
                {
                    if (m_visited.count(h) == 0)
                        m_queue.push_back(h);
                }
            }
        }

        Transparency transparency;
        bool bookColor = false;
        entity.Color = r.ReadEnColor(transparency, bookColor);
        entity.Transparency = transparency;
        if (R2004Plus() && bookColor)
            entity.BookColorHandle = HandleRef();
        entity.LineTypeScale = r.ReadBitDouble();

        if (!R2000Plus())
        {
            entity.IsInvisible = (r.ReadBitShort() & 1) != 0;
            return;
        }

        entity.LayerHandle = HandleRef();
        info.LtypeFlags = r.Read2Bits();
        if (info.LtypeFlags == 3)
            entity.LineTypeHandle = HandleRef();
        if (R2007Plus())
        {
            info.MaterialFlags = r.Read2Bits();
            if (info.MaterialFlags == 3)
                entity.MaterialHandle = HandleRef();
            r.ReadByte();   // 阴影
        }
        if (r.Read2Bits() == 3)
            HandleRef();    // 打印样式
        if (R2010Plus())
        {
            // 视觉样式：完整、面、边
            for (int i = 0; i < 3; ++i)
            {
                if (r.ReadBit())
                    HandleRef();
            }
        }
        entity.IsInvisible = (r.ReadBitShort() & 1) != 0;
        entity.LineWeight = LineWeightFromIndex(r.ReadByte());
    }

    // ── 简单实体 ───────────────────────────────────────────────────

    std::unique_ptr<CadObject> Reader::ReadSimpleEntity(std::int16_t type, ObjectInfo& info)
    {
        DwgBitReader& r = m_objReader;
        using T = CadObjectType;
        switch (static_cast<T>(type))
        {
        case T::BLOCK:
        {
            auto block = std::make_unique<Block>();
            ReadCommonEntityData(*block, info);
            block->Name = m_s.ReadVariableText();
            return block;
        }
        case T::ENDBLK:
        {
            auto end = std::make_unique<BlockEnd>();
            ReadCommonEntityData(*end, info);
            return end;
        }
        case T::SEQEND:
        {
            auto seqend = std::make_unique<Seqend>();
            ReadCommonEntityData(*seqend, info);
            return seqend;
        }
        case T::ARC:
        case T::CIRCLE:
        {
            std::unique_ptr<Circle> circle = static_cast<T>(type) == T::ARC ? std::make_unique<Arc>() : std::make_unique<Circle>();
            ReadCommonEntityData(*circle, info);
            circle->Center = r.Read3BitDouble();
            const double radius = r.ReadBitDouble();
            circle->Radius = radius <= 0 ? 1e-12 : radius;
            circle->Thickness = r.ReadBitThickness();
            circle->Normal = r.ReadBitExtrusion();
            if (auto* arc = dynamic_cast<Arc*>(circle.get()))
            {
                arc->StartAngle = r.ReadBitDouble();
                arc->EndAngle = r.ReadBitDouble();
            }
            return circle;
        }
        case T::LINE:
        {
            auto line = std::make_unique<Line>();
            ReadCommonEntityData(*line, info);
            if (R13_14Only())
            {
                line->StartPoint = r.Read3BitDouble();
                line->EndPoint = r.Read3BitDouble();
            }
            else
            {
                const bool zIsZero = r.ReadBit();
                const double sx = r.ReadRawDouble();
                const double ex = r.ReadBitDoubleWithDefault(sx);
                const double sy = r.ReadRawDouble();
                const double ey = r.ReadBitDoubleWithDefault(sy);
                double sz = 0.0, ez = 0.0;
                if (!zIsZero)
                {
                    sz = r.ReadRawDouble();
                    ez = r.ReadBitDoubleWithDefault(sz);
                }
                line->StartPoint = { sx, sy, sz };
                line->EndPoint = { ex, ey, ez };
            }
            line->Thickness = r.ReadBitThickness();
            line->Normal = r.ReadBitExtrusion();
            return line;
        }
        case T::POINT:
        {
            auto point = std::make_unique<Point>();
            ReadCommonEntityData(*point, info);
            point->Location = r.Read3BitDouble();
            point->Thickness = r.ReadBitThickness();
            point->Normal = r.ReadBitExtrusion();
            point->Rotation = r.ReadBitDouble();
            return point;
        }
        case T::FACE3D:
        {
            auto face = std::make_unique<Face3D>();
            ReadCommonEntityData(*face, info);
            if (R13_14Only())
            {
                face->FirstCorner = r.Read3BitDouble();
                face->SecondCorner = r.Read3BitDouble();
                face->ThirdCorner = r.Read3BitDouble();
                face->FourthCorner = r.Read3BitDouble();
                face->Flags = static_cast<InvisibleEdgeFlags>(r.ReadBitShort());
            }
            else
            {
                const bool noFlags = r.ReadBit();
                const bool zIsZero = r.ReadBit();
                const double x = r.ReadRawDouble();
                const double y = r.ReadRawDouble();
                const double z = zIsZero ? 0.0 : r.ReadRawDouble();
                face->FirstCorner = { x, y, z };
                face->SecondCorner = r.Read3BitDoubleWithDefault(face->FirstCorner);
                face->ThirdCorner = r.Read3BitDoubleWithDefault(face->SecondCorner);
                face->FourthCorner = r.Read3BitDoubleWithDefault(face->ThirdCorner);
                if (!noFlags)
                    face->Flags = static_cast<InvisibleEdgeFlags>(r.ReadBitShort());
            }
            return face;
        }
        case T::SOLID:
        case T::TRACE:
        {
            auto solid = std::make_unique<Solid>();
            ReadCommonEntityData(*solid, info);
            solid->Thickness = r.ReadBitThickness();
            const double elevation = r.ReadBitDouble();
            for (XYZ* corner : { &solid->FirstCorner, &solid->SecondCorner, &solid->ThirdCorner, &solid->FourthCorner })
            {
                const XY p = r.Read2RawDouble();
                *corner = { p.X, p.Y, elevation };
            }
            solid->Normal = r.ReadBitExtrusion();
            return solid;
        }
        case T::SHAPE:
        {
            auto shape = std::make_unique<Shape>();
            ReadCommonEntityData(*shape, info);
            shape->InsertionPoint = r.Read3BitDouble();
            shape->Size = r.ReadBitDouble();
            shape->Rotation = r.ReadBitDouble();
            shape->RelativeXScale = r.ReadBitDouble();
            shape->ObliqueAngle = r.ReadBitDouble();
            shape->Thickness = r.ReadBitDouble();
            r.ReadBitShort();   // 形号：未建模
            shape->Normal = r.Read3BitDouble();
            shape->ShapeStyleHandle = HandleRef();
            return shape;
        }
        case T::ELLIPSE:
        {
            auto ellipse = std::make_unique<Ellipse>();
            ReadCommonEntityData(*ellipse, info);
            ellipse->Center = r.Read3BitDouble();
            ellipse->MajorAxisEndPoint = r.Read3BitDouble();
            ellipse->Normal = r.Read3BitDouble();
            ellipse->RadiusRatio = r.ReadBitDouble();
            ellipse->StartParameter = r.ReadBitDouble();
            ellipse->EndParameter = r.ReadBitDouble();
            return ellipse;
        }
        case T::RAY:
        {
            auto ray = std::make_unique<Ray>();
            ReadCommonEntityData(*ray, info);
            ray->StartPoint = r.Read3BitDouble();
            ray->Direction = r.Read3BitDouble();
            return ray;
        }
        case T::XLINE:
        {
            auto xline = std::make_unique<XLine>();
            ReadCommonEntityData(*xline, info);
            xline->FirstPoint = r.Read3BitDouble();
            xline->Direction = r.Read3BitDouble();
            return xline;
        }
        default:
            return nullptr;
        }
    }

    // ── 文字与属性 ─────────────────────────────────────────────────

    void Reader::ReadCommonTextData(TextEntity& text, ObjectInfo& info)
    {
        ReadCommonEntityData(text, info);
        DwgBitReader& r = m_objReader;
        if (R13_14Only())
        {
            const double elevation = r.ReadBitDouble();
            XY p = r.Read2RawDouble();
            text.InsertPoint = { p.X, p.Y, elevation };
            p = r.Read2RawDouble();
            text.AlignmentPoint = { p.X, p.Y, elevation };
            text.Normal = r.Read3BitDouble();
            text.Thickness = r.ReadBitDouble();
            text.ObliqueAngle = r.ReadBitDouble();
            text.Rotation = r.ReadBitDouble();
            text.Height = r.ReadBitDouble();
            text.WidthFactor = r.ReadBitDouble();
            text.Value = m_s.ReadVariableText();
            text.Mirror = static_cast<TextMirrorFlag>(r.ReadBitShort());
            text.HorizontalAlignment = static_cast<TextHorizontalAlignment>(r.ReadBitShort());
            text.VerticalAlignment = static_cast<TextVerticalAlignmentType>(r.ReadBitShort());
            text.StyleHandle = HandleRef();
            return;
        }

        // 数据标志：各位为 1 表示对应的值取默认值、没有存储
        const std::uint8_t flags = r.ReadByte();
        double elevation = 0.0;
        if ((flags & 0x01) == 0)
            elevation = r.ReadRawDouble();
        const XY p = r.Read2RawDouble();
        text.InsertPoint = { p.X, p.Y, elevation };
        if ((flags & 0x02) == 0)
        {
            const double x = r.ReadBitDoubleWithDefault(p.X);
            const double y = r.ReadBitDoubleWithDefault(p.Y);
            text.AlignmentPoint = { x, y, elevation };
        }
        text.Normal = r.ReadBitExtrusion();
        text.Thickness = r.ReadBitThickness();
        if ((flags & 0x04) == 0)
            text.ObliqueAngle = r.ReadRawDouble();
        if ((flags & 0x08) == 0)
            text.Rotation = r.ReadRawDouble();
        text.Height = r.ReadRawDouble();
        if ((flags & 0x10) == 0)
            text.WidthFactor = r.ReadRawDouble();
        text.Value = m_s.ReadVariableText();
        if ((flags & 0x20) == 0)
            text.Mirror = static_cast<TextMirrorFlag>(r.ReadBitShort());
        if ((flags & 0x40) == 0)
            text.HorizontalAlignment = static_cast<TextHorizontalAlignment>(r.ReadBitShort());
        if ((flags & 0x80) == 0)
            text.VerticalAlignment = static_cast<TextVerticalAlignmentType>(r.ReadBitShort());
        text.StyleHandle = HandleRef();
    }

    void Reader::ReadCommonAttData(AttributeBase& att)
    {
        DwgBitReader& r = m_objReader;
        if (R2010Plus())
            att.Version = r.ReadByte();
        if (R2018Plus())
            att.AttributeType = static_cast<AttributeType>(r.ReadByte());
        if (att.AttributeType == AttributeType::MultiLine || att.AttributeType == AttributeType::ConstantMultiLine)
        {
            // 内嵌的 MTEXT：尚未建模，读出后丢弃
            MText mtext;
            ObjectInfo mtextInfo;
            ReadEntityMode(mtext, mtextInfo);
            ReadMTextBody(mtext);
            const std::int16_t dataSize = r.ReadBitShort();
            if (dataSize > 0)
            {
                r.Advance(static_cast<std::uint64_t>(dataSize));
                HandleRef();
                r.ReadBitShort();
            }
        }
        att.Tag = m_s.ReadVariableText();
        r.ReadBitShort();   // 字段长度
        att.Flags = static_cast<AttributeFlags>(r.ReadByte());
        if (R2007Plus())
        {
            // 锁定位置：DXF 中末尾的 280，读取时与版本号共用 Version（见 DXF 读取）
            if (r.ReadBit())
                att.Version = 1;
        }
    }

    std::unique_ptr<CadObject> Reader::ReadText(std::int16_t type, ObjectInfo& info)
    {
        switch (static_cast<CadObjectType>(type))
        {
        case CadObjectType::ATTRIB:
        {
            auto att = std::make_unique<AttributeEntity>();
            ReadCommonTextData(*att, info);
            ReadCommonAttData(*att);
            return att;
        }
        case CadObjectType::ATTDEF:
        {
            auto def = std::make_unique<AttributeDefinition>();
            ReadCommonTextData(*def, info);
            ReadCommonAttData(*def);
            if (R2010Plus())
                m_objReader.ReadByte();     // 版本
            def->Prompt = m_s.ReadVariableText();
            return def;
        }
        default:
        {
            auto text = std::make_unique<TextEntity>();
            ReadCommonTextData(*text, info);
            return text;
        }
        }
    }

    // ── 块参照与多段线 ─────────────────────────────────────────────

    std::unique_ptr<CadObject> Reader::ReadInsert(bool minsert, ObjectInfo& info)
    {
        auto insert = std::make_unique<Insert>();
        ReadCommonEntityData(*insert, info);

        DwgBitReader& r = m_objReader;
        insert->InsertPoint = r.Read3BitDouble();
        if (R13_14Only())
        {
            const XYZ scale = r.Read3BitDouble();
            insert->XScale = scale.X;
            insert->YScale = scale.Y;
            insert->ZScale = scale.Z;
        }
        else
        {
            switch (r.Read2Bits())
            {
            case 0:
                insert->XScale = r.ReadRawDouble();
                insert->YScale = r.ReadBitDoubleWithDefault(insert->XScale);
                insert->ZScale = r.ReadBitDoubleWithDefault(insert->XScale);
                break;
            case 1:
                insert->XScale = 1.0;
                insert->YScale = r.ReadBitDoubleWithDefault(1.0);
                insert->ZScale = r.ReadBitDoubleWithDefault(1.0);
                break;
            case 2:
                insert->XScale = insert->YScale = insert->ZScale = r.ReadRawDouble();
                break;
            default:
                insert->XScale = insert->YScale = insert->ZScale = 1.0;
                break;
            }
        }
        insert->Rotation = r.ReadBitDouble();
        insert->Normal = r.Read3BitDouble();
        const bool hasAttributes = r.ReadBit();
        std::int32_t owned = 0;
        if (R2004Plus() && hasAttributes)
            owned = r.ReadBitLong();

        if (minsert)
        {
            insert->ColumnCount = static_cast<std::uint16_t>(r.ReadBitShort());
            insert->RowCount = static_cast<std::uint16_t>(r.ReadBitShort());
            insert->ColumnSpacing = r.ReadBitDouble();
            insert->RowSpacing = r.ReadBitDouble();
        }

        insert->BlockHandle = HandleRef();
        if (!hasAttributes)
            return insert;
        if (R13_15Only())
        {
            info.FirstChild = HandleRef();
            info.LastChild = HandleRef();
        }
        else if (r.CheckCount(owned))
        {
            for (int i = 0; i < owned; ++i)
                info.Owned.push_back(HandleRef());
        }
        insert->SeqendHandle = HandleRef();
        return insert;
    }

    std::unique_ptr<CadObject> Reader::ReadPolyline2D(ObjectInfo& info)
    {
        auto pl = std::make_unique<Polyline2D>();
        ReadCommonEntityData(*pl, info);
        DwgBitReader& r = m_objReader;
        pl->Flags = static_cast<PolylineFlags>(r.ReadBitShort());
        pl->SmoothSurface = static_cast<SmoothSurfaceType>(r.ReadBitShort());
        pl->StartWidth = r.ReadBitDouble();
        pl->EndWidth = r.ReadBitDouble();
        pl->Thickness = r.ReadBitThickness();
        pl->Elevation = r.ReadBitDouble();
        pl->Normal = r.ReadBitExtrusion();
        ReadPolylineChildren(*pl, info);
        return pl;
    }

    // R2004 起：BL 个数 + 拥有的顶点；R13～R2000：首尾顶点；之后是 SEQEND
    void Reader::ReadPolylineChildren(Polyline& pl, ObjectInfo& info)
    {
        DwgBitReader& r = m_objReader;
        if (R2004Plus())
        {
            const std::int32_t owned = r.ReadBitLong();
            if (r.CheckCount(owned))
            {
                for (int i = 0; i < owned; ++i)
                    info.Owned.push_back(HandleRef());
            }
        }
        if (R13_15Only())
        {
            info.FirstChild = HandleRef();
            info.LastChild = HandleRef();
        }
        pl.SeqendHandle = HandleRef();
    }

    // 多面网格：BS 顶点数、BS 面数（由子实体统计，不保存）
    std::unique_ptr<CadObject> Reader::ReadPolyfaceMesh(ObjectInfo& info)
    {
        auto mesh = std::make_unique<PolyfaceMesh>();
        ReadCommonEntityData(*mesh, info);
        m_objReader.ReadBitShort();
        m_objReader.ReadBitShort();
        mesh->Flags = PolylineFlags::PolyfaceMesh;
        ReadPolylineChildren(*mesh, info);
        return mesh;
    }

    // 多边形网格：BS 标志、BS 曲面类型、BS M/N 顶点数、BS M/N 光滑密度
    std::unique_ptr<CadObject> Reader::ReadPolygonMesh(ObjectInfo& info)
    {
        auto mesh = std::make_unique<PolygonMesh>();
        ReadCommonEntityData(*mesh, info);
        DwgBitReader& r = m_objReader;
        mesh->Flags = static_cast<PolylineFlags>(r.ReadBitShort() | static_cast<int>(PolylineFlags::PolygonMesh));
        mesh->SmoothSurface = static_cast<SmoothSurfaceType>(r.ReadBitShort());
        mesh->MVertexCount = r.ReadBitShort();
        mesh->NVertexCount = r.ReadBitShort();
        mesh->MSmoothSurfaceDensity = r.ReadBitShort();
        mesh->NSmoothSurfaceDensity = r.ReadBitShort();
        ReadPolylineChildren(*mesh, info);
        return mesh;
    }

    // 多面网格的面：4 个 BS 顶点序号（从 1 开始，负数表示该边不可见）
    std::unique_ptr<CadObject> Reader::ReadFaceRecord(ObjectInfo& info)
    {
        auto face = std::make_unique<VertexFaceRecord>();
        ReadCommonEntityData(*face, info);
        DwgBitReader& r = m_objReader;
        face->Flags = VertexFlags::PolyFaceMeshVertex;
        face->Index1 = r.ReadBitShort();
        face->Index2 = r.ReadBitShort();
        face->Index3 = r.ReadBitShort();
        face->Index4 = r.ReadBitShort();
        return face;
    }

    std::unique_ptr<CadObject> Reader::ReadPolyline3D(ObjectInfo& info)
    {
        auto pl = std::make_unique<Polyline3D>();
        ReadCommonEntityData(*pl, info);
        DwgBitReader& r = m_objReader;
        const std::uint8_t splineFlags = r.ReadByte();
        int flags = static_cast<int>(PolylineFlags::Polyline3D);
        if (splineFlags & 1)
            pl->SmoothSurface = SmoothSurfaceType::Quadratic;
        else if (splineFlags & 2)
            pl->SmoothSurface = SmoothSurfaceType::Cubic;
        if (splineFlags & 3)
            flags |= static_cast<int>(PolylineFlags::SplineFit);
        if (r.ReadByte() & 1)
            flags |= static_cast<int>(PolylineFlags::ClosedPolylineOrClosedPolygonMeshInM);
        pl->Flags = static_cast<PolylineFlags>(flags);
        if (R2004Plus())
        {
            const std::int32_t owned = r.ReadBitLong();
            if (r.CheckCount(owned))
            {
                for (int i = 0; i < owned; ++i)
                    info.Owned.push_back(HandleRef());
            }
        }
        if (R13_15Only())
        {
            info.FirstChild = HandleRef();
            info.LastChild = HandleRef();
        }
        pl->SeqendHandle = HandleRef();
        return pl;
    }

    std::unique_ptr<CadObject> Reader::ReadVertex2D(ObjectInfo& info)
    {
        auto v = std::make_unique<Vertex2D>();
        ReadCommonEntityData(*v, info);
        DwgBitReader& r = m_objReader;
        v->Flags = static_cast<VertexFlags>(r.ReadByte());
        v->Location = r.Read3BitDouble();
        const double width = r.ReadBitDouble();
        if (width < 0.0)
        {
            v->StartWidth = -width;
            v->EndWidth = -width;
        }
        else
        {
            v->StartWidth = width;
            v->EndWidth = r.ReadBitDouble();
        }
        v->Bulge = r.ReadBitDouble();
        if (R2010Plus())
            v->Id = r.ReadBitLong();
        v->CurveTangent = r.ReadBitDouble();
        return v;
    }

    // 三维多段线、多面网格、多边形网格的顶点：EC 标志 + 3BD
    std::unique_ptr<CadObject> Reader::ReadVertex3D(std::int16_t type, ObjectInfo& info)
    {
        std::unique_ptr<Vertex> v;
        switch (static_cast<CadObjectType>(type))
        {
        case CadObjectType::VERTEX_PFACE: v = std::make_unique<VertexFaceMesh>(); break;
        case CadObjectType::VERTEX_MESH: v = std::make_unique<PolygonMeshVertex>(); break;
        default: v = std::make_unique<Vertex3D>(); break;
        }
        ReadCommonEntityData(*v, info);
        v->Flags = static_cast<VertexFlags>(m_objReader.ReadByte());
        v->Location = m_objReader.Read3BitDouble();
        return v;
    }

    // ── 标注 ───────────────────────────────────────────────────────

    void Reader::ReadCommonDimensionData(Dimension& dim, ObjectInfo& info)
    {
        ReadCommonEntityData(dim, info);
        DwgBitReader& r = m_objReader;
        if (R2010Plus())
            dim.Version = r.ReadByte();
        dim.Normal = r.Read3BitDouble();
        const XY mid = r.Read2RawDouble();
        const double elevation = r.ReadBitDouble();
        dim.TextMiddlePoint = { mid.X, mid.Y, elevation };

        // 标志 1：位 0 为 70 组码第 128 位（文字位置由用户指定）的反，位 1 同第 32 位（有匿名块）
        const std::uint8_t flags = r.ReadByte();
        int f = static_cast<int>(dim.Flags);
        if ((flags & 1) == 0)
            f |= static_cast<int>(DimensionType::TextUserDefinedLocation);
        if (flags & 2)
            f |= static_cast<int>(DimensionType::BlockReference);
        dim.Flags = static_cast<DimensionType>(f);

        dim.Text = m_s.ReadVariableText();
        dim.TextRotation = r.ReadBitDouble();
        dim.HorizontalDirection = r.ReadBitDouble();
        r.Read3BitDouble();     // 匿名块的插入比例（未公开）
        r.ReadBitDouble();      // 匿名块的插入旋转
        if (R2000Plus())
        {
            dim.AttachmentPoint = static_cast<AttachmentPointType>(r.ReadBitShort());
            dim.LineSpacingStyle = static_cast<LineSpacingStyleType>(r.ReadBitShort());
            dim.LineSpacingFactor = r.ReadBitDouble();
            dim.Measurement = r.ReadBitDouble();
        }
        else
        {
            // R14 没有这些字段：取 AutoCAD 打开 R14 文件时的值（文字居中，测量值 -1 表示未计算）
            dim.AttachmentPoint = AttachmentPointType::MiddleCenter;
            dim.LineSpacingStyle = LineSpacingStyleType::AtLeast;
            dim.LineSpacingFactor = 1.0;
            dim.Measurement = -1.0;
        }
        if (R2007Plus())
        {
            r.ReadBit();
            dim.FlipArrow1 = r.ReadBit();
            dim.FlipArrow2 = r.ReadBit();
        }
        const XY p = r.Read2RawDouble();
        dim.InsertionPoint = { p.X, p.Y, elevation };
    }

    std::unique_ptr<CadObject> Reader::ReadDimension(std::int16_t type, ObjectInfo& info)
    {
        DwgBitReader& r = m_objReader;
        std::unique_ptr<Dimension> dim;
        int typeCode = 0;
        switch (static_cast<CadObjectType>(type))
        {
        case CadObjectType::DIMENSION_LINEAR: dim = std::make_unique<DimensionLinear>(); typeCode = 0; break;
        case CadObjectType::DIMENSION_ALIGNED: dim = std::make_unique<DimensionAligned>(); typeCode = 1; break;
        case CadObjectType::DIMENSION_ANG_2_Ln: dim = std::make_unique<DimensionAngular2Line>(); typeCode = 2; break;
        case CadObjectType::DIMENSION_DIAMETER: dim = std::make_unique<DimensionDiameter>(); typeCode = 3; break;
        case CadObjectType::DIMENSION_RADIUS: dim = std::make_unique<DimensionRadius>(); typeCode = 4; break;
        case CadObjectType::DIMENSION_ANG_3_Pt: dim = std::make_unique<DimensionAngular3Pt>(); typeCode = 5; break;
        default: dim = std::make_unique<DimensionOrdinate>(); typeCode = 6; break;
        }
        dim->Flags = static_cast<DimensionType>(typeCode);
        ReadCommonDimensionData(*dim, info);

        if (auto* aligned = dynamic_cast<DimensionAligned*>(dim.get()))
        {
            aligned->FirstPoint = r.Read3BitDouble();
            aligned->SecondPoint = r.Read3BitDouble();
            aligned->DefinitionPoint = r.Read3BitDouble();
            aligned->ExtLineRotation = r.ReadBitDouble();
            if (auto* linear = dynamic_cast<DimensionLinear*>(dim.get()))
                linear->Rotation = r.ReadBitDouble();
        }
        else if (auto* a3 = dynamic_cast<DimensionAngular3Pt*>(dim.get()))
        {
            a3->DefinitionPoint = r.Read3BitDouble();
            a3->FirstPoint = r.Read3BitDouble();
            a3->SecondPoint = r.Read3BitDouble();
            a3->AngleVertex = r.Read3BitDouble();
        }
        else if (auto* a2 = dynamic_cast<DimensionAngular2Line*>(dim.get()))
        {
            const XY arc = r.Read2RawDouble();
            a2->DimensionArc = { arc.X, arc.Y, a2->TextMiddlePoint.Z };
            a2->FirstPoint = r.Read3BitDouble();
            a2->SecondPoint = r.Read3BitDouble();
            a2->AngleVertex = r.Read3BitDouble();
            a2->DefinitionPoint = r.Read3BitDouble();
        }
        else if (auto* diameter = dynamic_cast<DimensionDiameter*>(dim.get()))
        {
            diameter->AngleVertex = r.Read3BitDouble();
            diameter->DefinitionPoint = r.Read3BitDouble();
            diameter->LeaderLength = r.ReadBitDouble();
        }
        else if (auto* radius = dynamic_cast<DimensionRadius*>(dim.get()))
        {
            radius->DefinitionPoint = r.Read3BitDouble();
            radius->AngleVertex = r.Read3BitDouble();
            radius->LeaderLength = r.ReadBitDouble();
        }
        else if (auto* ordinate = dynamic_cast<DimensionOrdinate*>(dim.get()))
        {
            ordinate->DefinitionPoint = r.Read3BitDouble();
            ordinate->FeatureLocation = r.Read3BitDouble();
            ordinate->LeaderEndpoint = r.Read3BitDouble();
            if (r.ReadByte() & 1)
                ordinate->Flags = static_cast<DimensionType>(static_cast<int>(ordinate->Flags)
                                                             | static_cast<int>(DimensionType::OrdinateTypeX));
        }

        dim->StyleHandle = HandleRef();
        dim->BlockHandle = HandleRef();
        return dim;
    }

    std::unique_ptr<CadObject> Reader::ReadDimensionArc(ObjectInfo& info)
    {
        DwgBitReader& r = m_objReader;
        auto dim = std::make_unique<DimensionArc>();
        ReadCommonDimensionData(*dim, info);
        dim->DefinitionPoint = r.Read3BitDouble();
        dim->FirstPoint = r.Read3BitDouble();
        dim->SecondPoint = r.Read3BitDouble();
        dim->Center = r.Read3BitDouble();
        dim->IsPartial = r.ReadBit();
        dim->StartAngle = r.ReadBitDouble();
        dim->EndAngle = r.ReadBitDouble();
        dim->HasLeader = r.ReadBit();
        dim->LeaderPoint1 = r.Read3BitDouble();
        dim->LeaderPoint2 = r.Read3BitDouble();
        dim->StyleHandle = HandleRef();
        dim->BlockHandle = HandleRef();
        return dim;
    }

    // ── 多行文字 ───────────────────────────────────────────────────

    void Reader::ReadMTextBody(MText& mtext)
    {
        DwgBitReader& r = m_objReader;
        mtext.InsertPoint = r.Read3BitDouble();
        mtext.Normal = r.Read3BitDouble();
        mtext.AlignmentPoint = r.Read3BitDouble();
        mtext.RectangleWidth = r.ReadBitDouble();
        if (R2007Plus())
            mtext.RectangleHeight = r.ReadBitDouble();
        mtext.Height = r.ReadBitDouble();
        mtext.AttachmentPoint = static_cast<AttachmentPointType>(r.ReadBitShort());
        mtext.DrawingDirection = static_cast<DrawingDirectionType>(r.ReadBitShort());
        // 文字范围的高度、宽度：DXF 中没有（42/43 是另外的含义），与 ACadSharp 一样不保留
        r.ReadBitDouble();
        r.ReadBitDouble();
        mtext.Value = m_s.ReadVariableText();
        mtext.StyleHandle = HandleRef();
        if (R2000Plus())
        {
            mtext.LineSpacingStyle = static_cast<LineSpacingStyleType>(r.ReadBitShort());
            mtext.LineSpacing = r.ReadBitDouble();
            r.ReadBit();
        }
        else
        {
            // R14 没有行距：取 AutoCAD 升级时的默认值（行距样式 0 在 AutoCAD 中无效）
            mtext.LineSpacingStyle = LineSpacingStyleType::AtLeast;
            mtext.LineSpacing = 1.0;
        }
        if (R2004Plus())
        {
            mtext.BackgroundFillFlags = static_cast<BackgroundFillFlags>(r.ReadBitLong());
            const int flags = static_cast<int>(mtext.BackgroundFillFlags);
            if ((flags & static_cast<int>(BackgroundFillFlags::UseBackgroundFillColor)) != 0
                || (m_version > CadVersion::AC1027 && (flags & static_cast<int>(BackgroundFillFlags::TextFrame)) != 0))
            {
                mtext.BackgroundScale = r.ReadBitDouble();
                mtext.BackgroundColor = m_s.ReadCmColor();
                mtext.BackgroundTransparency = Transparency::FromAlphaValue(r.ReadBitLong());
            }
        }
        if (!R2018Plus())
            return;

        // R2018：位为 0 表示注释性，没有下面的分栏数据
        if (!r.ReadBit())
            return;
        r.ReadBitShort();
        r.ReadBit();
        HandleRef();
        r.ReadBitLong();
        r.Read3BitDouble();
        r.Read3BitDouble();
        r.ReadBitDouble();
        r.ReadBitDouble();
        r.ReadBitDouble();
        r.ReadBitDouble();
        MTextTextColumnData& col = mtext.ColumnData;
        col.ColumnType = static_cast<ColumnType>(r.ReadBitShort());
        if (col.ColumnType == ColumnType::NoColumns)
            return;
        const std::int32_t count = r.ReadBitLong();
        col.ColumnCount = count;
        col.Width = r.ReadBitDouble();
        col.Gutter = r.ReadBitDouble();
        col.AutoHeight = r.ReadBit();
        col.FlowReversed = r.ReadBit();
        if (!col.AutoHeight && col.ColumnType == ColumnType::DynamicColumns && r.CheckCount(count, 2))
        {
            for (int i = 0; i < count; ++i)
                col.Heights.push_back(r.ReadBitDouble());
        }
    }

    std::unique_ptr<CadObject> Reader::ReadMText(ObjectInfo& info)
    {
        auto mtext = std::make_unique<MText>();
        ReadCommonEntityData(*mtext, info);
        ReadMTextBody(*mtext);
        return mtext;
    }

    // ── 轻量多段线、样条、填充 ─────────────────────────────────────

    std::unique_ptr<CadObject> Reader::ReadLwPolyline(ObjectInfo& info)
    {
        auto pl = std::make_unique<LwPolyline>();
        ReadCommonEntityData(*pl, info);
        DwgBitReader& r = m_objReader;
        const std::int16_t flags = r.ReadBitShort();
        int f = 0;
        if (flags & 0x100) f |= static_cast<int>(LwPolylineFlags::Plinegen);
        if (flags & 0x200) f |= static_cast<int>(LwPolylineFlags::Closed);
        pl->Flags = static_cast<LwPolylineFlags>(f);
        if (flags & 0x4) pl->ConstantWidth = r.ReadBitDouble();
        if (flags & 0x8) pl->Elevation = r.ReadBitDouble();
        if (flags & 0x2) pl->Thickness = r.ReadBitDouble();
        if (flags & 0x1) pl->Normal = r.Read3BitDouble();

        const std::int32_t count = r.ReadBitLong();
        const std::int32_t bulges = (flags & 0x10) ? r.ReadBitLong() : 0;
        const std::int32_t ids = (flags & 0x400) ? r.ReadBitLong() : 0;
        const std::int32_t widths = (flags & 0x20) ? r.ReadBitLong() : 0;
        if (!r.CheckCount(count, 2) || bulges > count || ids > count || widths > count || bulges < 0 || ids < 0 || widths < 0)
        {
            r.Fail();
            return pl;
        }

        pl->Vertices.resize(static_cast<std::size_t>(count));
        if (R13_14Only())
        {
            for (auto& v : pl->Vertices)
                v.Location = r.Read2RawDouble();
        }
        else if (count > 0)
        {
            XY p = r.Read2RawDouble();
            pl->Vertices[0].Location = p;
            for (int i = 1; i < count; ++i)
            {
                p = r.Read2BitDoubleWithDefault(p);
                pl->Vertices[i].Location = p;
            }
        }
        for (int i = 0; i < bulges; ++i)
            pl->Vertices[i].Bulge = r.ReadBitDouble();
        for (int i = 0; i < ids; ++i)
            pl->Vertices[i].Id = r.ReadBitLong();
        for (int i = 0; i < widths; ++i)
        {
            pl->Vertices[i].StartWidth = r.ReadBitDouble();
            pl->Vertices[i].EndWidth = r.ReadBitDouble();
        }
        return pl;
    }

    std::unique_ptr<CadObject> Reader::ReadSpline(ObjectInfo& info)
    {
        auto spline = std::make_unique<Spline>();
        ReadCommonEntityData(*spline, info);
        DwgBitReader& r = m_objReader;

        int flags = 0;
        int flags1 = 0;
        std::int32_t scenario = r.ReadBitLong();
        if (R2013Plus())
        {
            flags1 = r.ReadBitLong();
            const std::int32_t knotParametrization = r.ReadBitLong();
            spline->KnotParametrization = static_cast<KnotParametrization>(knotParametrization);
            if (flags1 & static_cast<int>(SplineFlags1::Closed))
                flags |= static_cast<int>(SplineFlags::Closed);
            // 15 = 自定义节点参数化
            scenario = (knotParametrization == 15 || (flags1 & static_cast<int>(SplineFlags1::UseKnotParameter)) == 0) ? 1 : 2;
        }
        else if (scenario == 2)
        {
            // R2013 之前的拟合点样条：AutoCAD 读入后同样标为"按拟合点、用节点参数化"
            flags1 |= static_cast<int>(SplineFlags1::MethodFitPoints | SplineFlags1::UseKnotParameter);
        }
        spline->Degree = r.ReadBitLong();

        std::int32_t fitCount = 0, knotCount = 0, controlCount = 0;
        bool weighted = false;
        if (scenario == 1)
        {
            if (r.ReadBit()) flags |= static_cast<int>(SplineFlags::Rational);
            if (r.ReadBit())
            {
                flags |= static_cast<int>(SplineFlags::Closed);
                flags1 |= static_cast<int>(SplineFlags1::Closed);
            }
            if (r.ReadBit()) flags |= static_cast<int>(SplineFlags::Periodic);
            spline->KnotTolerance = r.ReadBitDouble();
            spline->ControlPointTolerance = r.ReadBitDouble();
            knotCount = r.ReadBitLong();
            controlCount = r.ReadBitLong();
            weighted = r.ReadBit();
        }
        else if (scenario == 2)
        {
            spline->FitTolerance = r.ReadBitDouble();
            spline->StartTangent = r.Read3BitDouble();
            spline->EndTangent = r.Read3BitDouble();
            fitCount = r.ReadBitLong();
        }
        spline->Flags = static_cast<SplineFlags>(flags);
        spline->Flags1 = static_cast<SplineFlags1>(flags1);

        if (!r.CheckCount(knotCount, 2) || !r.CheckCount(controlCount, 6) || !r.CheckCount(fitCount, 6))
            return spline;
        for (int i = 0; i < knotCount; ++i)
            spline->Knots.push_back(r.ReadBitDouble());
        for (int i = 0; i < controlCount; ++i)
        {
            spline->ControlPoints.push_back(r.Read3BitDouble());
            if (weighted)
                spline->Weights.push_back(r.ReadBitDouble());
        }
        for (int i = 0; i < fitCount; ++i)
            spline->FitPoints.push_back(r.Read3BitDouble());

        // DXF 组码 70 = 标志 | 平面/直线位 | Flags1 << 7（与 AutoCAD 写出的 DXF 一致）
        // 只有拟合数据时，起止切向也决定形状
        std::vector<XYZ> points = spline->ControlPoints;
        if (points.empty() && !spline->FitPoints.empty())
        {
            points = spline->FitPoints;
            const XYZ& first = spline->FitPoints.front();
            const XYZ& last = spline->FitPoints.back();
            const XYZ& t0 = spline->StartTangent;
            const XYZ& t1 = spline->EndTangent;
            points.push_back({ first.X + t0.X, first.Y + t0.Y, first.Z + t0.Z });
            points.push_back({ last.X + t1.X, last.Y + t1.Y, last.Z + t1.Z });
        }
        XYZ normal = XYZ::AxisZ();
        const SplineFlags shape = SplineShapeFlags(points, normal);
        spline->Flags = static_cast<SplineFlags>(static_cast<int>(spline->Flags) | static_cast<int>(shape) | (flags1 << 7));
        if (HasFlag(shape, SplineFlags::Planar))
            spline->Normal = normal;
        return spline;
    }

    std::unique_ptr<CadObject> Reader::ReadHatch(ObjectInfo& info)
    {
        auto hatch = std::make_unique<Hatch>();
        ReadCommonEntityData(*hatch, info);
        DwgBitReader& r = m_objReader;

        if (R2004Plus())
        {
            HatchGradientPattern& g = hatch->GradientColor;
            g.Enabled = r.ReadBitLong() != 0;
            g.Reserved = r.ReadBitLong();
            g.Angle = r.ReadBitDouble();
            g.Shift = r.ReadBitDouble();
            g.IsSingleColorGradient = r.ReadBitLong() != 0;
            g.ColorTint = r.ReadBitDouble();
            const std::int32_t colors = r.ReadBitLong();
            if (!r.CheckCount(colors, 4))
                return hatch;
            for (int i = 0; i < colors; ++i)
            {
                GradientColor c;
                c.Value = r.ReadBitDouble();
                c.Color = m_s.ReadCmColor();
                g.Colors.push_back(c);
            }
            g.Name = m_s.ReadVariableText();
        }

        hatch->Elevation = r.ReadBitDouble();
        hatch->Normal = r.Read3BitDouble();
        hatch->Pattern.Name = m_s.ReadVariableText();
        hatch->IsSolid = r.ReadBit();
        hatch->IsAssociative = r.ReadBit();

        const std::int32_t pathCount = r.ReadBitLong();
        if (!r.CheckCount(pathCount, 4))
            return hatch;
        bool derived = false;
        for (int i = 0; i < pathCount && !r.Failed(); ++i)
        {
            HatchBoundaryPath path;
            path.Flags = static_cast<BoundaryPathFlags>(r.ReadBitLong());
            derived |= HasFlag(path.Flags, BoundaryPathFlags::Derived);
            if (!HasFlag(path.Flags, BoundaryPathFlags::Polyline))
            {
                const std::int32_t edges = r.ReadBitLong();
                if (!r.CheckCount(edges, 8))
                    return hatch;
                for (int j = 0; j < edges && !r.Failed(); ++j)
                {
                    switch (r.ReadByte())
                    {
                    case 1:
                    {
                        auto line = std::make_unique<HatchBoundaryPathLine>();
                        line->Start = r.Read2RawDouble();
                        line->End = r.Read2RawDouble();
                        path.Edges.push_back(std::move(line));
                        break;
                    }
                    case 2:
                    {
                        auto arc = std::make_unique<HatchBoundaryPathArc>();
                        arc->Center = r.Read2RawDouble();
                        arc->Radius = r.ReadBitDouble();
                        arc->StartAngle = r.ReadBitDouble();
                        arc->EndAngle = r.ReadBitDouble();
                        arc->CounterClockWise = r.ReadBit();
                        path.Edges.push_back(std::move(arc));
                        break;
                    }
                    case 3:
                    {
                        auto ellipse = std::make_unique<HatchBoundaryPathEllipse>();
                        ellipse->Center = r.Read2RawDouble();
                        ellipse->MajorAxisEndPoint = r.Read2RawDouble();
                        ellipse->RadiusRatio = r.ReadBitDouble();
                        ellipse->StartAngle = r.ReadBitDouble();
                        ellipse->EndAngle = r.ReadBitDouble();
                        ellipse->CounterClockWise = r.ReadBit();
                        path.Edges.push_back(std::move(ellipse));
                        break;
                    }
                    case 4:
                    {
                        auto spline = std::make_unique<HatchBoundaryPathSpline>();
                        spline->Degree = r.ReadBitLong();
                        spline->IsRational = r.ReadBit();
                        spline->IsPeriodic = r.ReadBit();
                        const std::int32_t knots = r.ReadBitLong();
                        const std::int32_t controls = r.ReadBitLong();
                        if (!r.CheckCount(knots, 2) || !r.CheckCount(controls, 16))
                            return hatch;
                        for (int k = 0; k < knots; ++k)
                            spline->Knots.push_back(r.ReadBitDouble());
                        for (int k = 0; k < controls; ++k)
                        {
                            const XY p = r.Read2RawDouble();
                            // 权重放在 Z（非有理时为 1，与 DXF 读取一致）
                            const double weight = spline->IsRational ? r.ReadBitDouble() : 1.0;
                            spline->ControlPoints.push_back({ p.X, p.Y, weight });
                        }
                        if (R2010Plus())
                        {
                            const std::int32_t fits = r.ReadBitLong();
                            if (fits > 0 && r.CheckCount(fits, 128))
                            {
                                for (int k = 0; k < fits; ++k)
                                    spline->FitPoints.push_back(r.Read2RawDouble());
                                spline->StartTangent = r.Read2RawDouble();
                                spline->EndTangent = r.Read2RawDouble();
                            }
                        }
                        path.Edges.push_back(std::move(spline));
                        break;
                    }
                    default:
                        r.Fail();
                        return hatch;
                    }
                }
            }
            else
            {
                auto pl = std::make_unique<HatchBoundaryPathPolyline>();
                const bool hasBulge = r.ReadBit();
                pl->IsClosed = r.ReadBit();
                const std::int32_t n = r.ReadBitLong();
                if (!r.CheckCount(n, 128))
                    return hatch;
                for (int k = 0; k < n; ++k)
                {
                    const XY p = r.Read2RawDouble();
                    const double bulge = hasBulge ? r.ReadBitDouble() : 0.0;
                    pl->Vertices.push_back({ p.X, p.Y, bulge });
                }
                path.Edges.push_back(std::move(pl));
            }

            const std::int32_t sources = r.ReadBitLong();
            if (!r.CheckCount(sources))
                return hatch;
            for (int k = 0; k < sources; ++k)
                path.Entities.push_back(HandleRef());
            hatch->Paths.push_back(std::move(path));
        }

        hatch->Style = static_cast<HatchStyleType>(r.ReadBitShort());
        hatch->PatternType = static_cast<HatchPatternType>(r.ReadBitShort());
        if (!hatch->IsSolid)
        {
            hatch->PatternAngle = r.ReadBitDouble();
            hatch->PatternScale = r.ReadBitDouble();
            hatch->IsDouble = r.ReadBit();
            const std::int16_t lines = r.ReadBitShort();
            for (int i = 0; i < lines && !r.Failed(); ++i)
            {
                HatchPatternLine line;
                line.Angle = r.ReadBitDouble();
                line.BasePoint = r.Read2BitDouble();
                line.Offset = r.Read2BitDouble();
                const std::int16_t dashes = r.ReadBitShort();
                for (int d = 0; d < dashes && !r.Failed(); ++d)
                    line.DashLengths.push_back(r.ReadBitDouble());
                hatch->Pattern.Lines.push_back(std::move(line));
            }
        }
        if (derived)
            hatch->PixelSize = r.ReadBitDouble();
        const std::int32_t seeds = r.ReadBitLong();
        if (r.CheckCount(seeds, 128))
        {
            for (int i = 0; i < seeds; ++i)
                hatch->SeedPoints.push_back(r.Read2RawDouble());
        }
        return hatch;
    }

    // ── 其他实体 ───────────────────────────────────────────────────

    std::unique_ptr<CadObject> Reader::ReadLeader(ObjectInfo& info)
    {
        auto leader = std::make_unique<Leader>();
        ReadCommonEntityData(*leader, info);
        DwgBitReader& r = m_objReader;
        r.ReadBit();
        leader->CreationType = static_cast<LeaderCreationType>(r.ReadBitShort());
        leader->PathType = static_cast<LeaderPathType>(r.ReadBitShort());
        const std::int32_t count = r.ReadBitLong();
        if (!r.CheckCount(count, 6))
            return leader;
        for (int i = 0; i < count; ++i)
            leader->Vertices.push_back(r.Read3BitDouble());
        r.Read3BitDouble();     // 末端投影点
        leader->Normal = r.Read3BitDouble();
        leader->HorizontalDirection = r.Read3BitDouble();
        leader->BlockOffset = r.Read3BitDouble();
        if (m_version >= CadVersion::AC1014)
            leader->AnnotationOffset = r.Read3BitDouble();
        if (R13_14Only())
            r.ReadBitDouble();  // DIMGAP
        if (m_version <= CadVersion::AC1021)
        {
            leader->TextHeight = r.ReadBitDouble();
            leader->TextWidth = r.ReadBitDouble();
        }
        leader->HookLineDirection = r.ReadBit() ? HookLineDirection::Same : HookLineDirection::Opposite;
        leader->ArrowHeadEnabled = r.ReadBit();
        if (R13_14Only())
        {
            r.ReadBitShort();
            r.ReadBitDouble();
            r.ReadBit();
            r.ReadBit();
            r.ReadBitShort();
            r.ReadBitShort();
            r.ReadBit();
            r.ReadBit();
        }
        if (R2000Plus())
        {
            r.ReadBitShort();
            r.ReadBit();
            r.ReadBit();
        }
        leader->AssociatedAnnotationHandle = HandleRef();
        leader->StyleHandle = HandleRef();
        return leader;
    }

    std::unique_ptr<CadObject> Reader::ReadTolerance(ObjectInfo& info)
    {
        auto tolerance = std::make_unique<Tolerance>();
        ReadCommonEntityData(*tolerance, info);
        DwgBitReader& r = m_objReader;
        if (R13_14Only())
        {
            r.ReadBitShort();
            r.ReadBitDouble();
            r.ReadBitDouble();
        }
        tolerance->InsertionPoint = r.Read3BitDouble();
        tolerance->Direction = r.Read3BitDouble();
        tolerance->Normal = r.Read3BitDouble();
        tolerance->Text = m_s.ReadVariableText();
        tolerance->StyleHandle = HandleRef();
        return tolerance;
    }

    std::unique_ptr<CadObject> Reader::ReadMLine(ObjectInfo& info)
    {
        auto mline = std::make_unique<MLine>();
        ReadCommonEntityData(*mline, info);
        DwgBitReader& r = m_objReader;
        mline->ScaleFactor = r.ReadBitDouble();
        mline->Justification = static_cast<MLineJustification>(r.ReadByte());
        mline->StartPoint = r.Read3BitDouble();
        mline->Normal = r.Read3BitDouble();
        // 就是 DXF 的 71 标志（1 有顶点、2 闭合、4/8 不画起点/终点封口），ACadSharp 只区分了开闭
        mline->Flags = static_cast<MLineFlags>(r.ReadBitShort());
        const int lines = r.ReadByte();
        const std::int16_t vertices = r.ReadBitShort();
        if (!r.CheckCount(vertices, 6))
            return mline;
        for (int i = 0; i < vertices && !r.Failed(); ++i)
        {
            MLineVertex v;
            v.Position = r.Read3BitDouble();
            v.Direction = r.Read3BitDouble();
            v.Miter = r.Read3BitDouble();
            for (int j = 0; j < lines && !r.Failed(); ++j)
            {
                MLineVertexSegment s;
                const std::int16_t parameters = r.ReadBitShort();
                for (int k = 0; k < parameters && !r.Failed(); ++k)
                    s.Parameters.push_back(r.ReadBitDouble());
                const std::int16_t fills = r.ReadBitShort();
                for (int k = 0; k < fills && !r.Failed(); ++k)
                    s.AreaFillParameters.push_back(r.ReadBitDouble());
                v.Segments.push_back(std::move(s));
            }
            mline->Vertices.push_back(std::move(v));
        }
        mline->StyleHandle = HandleRef();
        return mline;
    }

    std::unique_ptr<CadObject> Reader::ReadViewport(ObjectInfo& info)
    {
        auto vp = std::make_unique<Viewport>();
        ReadCommonEntityData(*vp, info);
        DwgBitReader& r = m_objReader;
        vp->Center = r.Read3BitDouble();
        vp->Width = r.ReadBitDouble();
        vp->Height = r.ReadBitDouble();
        if (R2000Plus())
        {
            vp->ViewTarget = r.Read3BitDouble();
            vp->ViewDirection = r.Read3BitDouble();
            vp->TwistAngle = r.ReadBitDouble();
            vp->ViewHeight = r.ReadBitDouble();
            vp->LensLength = r.ReadBitDouble();
            vp->FrontClipPlane = r.ReadBitDouble();
            vp->BackClipPlane = r.ReadBitDouble();
            vp->SnapAngle = r.ReadBitDouble();
            vp->ViewCenter = r.Read2RawDouble();
            vp->SnapBase = r.Read2RawDouble();
            vp->SnapSpacing = r.Read2RawDouble();
            vp->GridSpacing = r.Read2RawDouble();
            vp->CircleZoomPercent = r.ReadBitShort();
        }
        if (R2007Plus())
            vp->MajorGridLineFrequency = r.ReadBitShort();
        std::int32_t frozen = 0;
        if (R2000Plus())
        {
            frozen = r.ReadBitLong();
            vp->Status = static_cast<ViewportStatusFlags>(r.ReadBitLong());
            vp->StyleSheetName = m_s.ReadVariableText();
            vp->RenderMode = static_cast<RenderMode>(r.ReadByte());
            vp->DisplayUcsIcon = r.ReadBit();
            vp->UcsPerViewport = r.ReadBit();
            vp->UcsOrigin = r.Read3BitDouble();
            vp->UcsXAxis = r.Read3BitDouble();
            vp->UcsYAxis = r.Read3BitDouble();
            vp->Elevation = r.ReadBitDouble();
            vp->UcsOrthographicType = static_cast<OrthographicType>(r.ReadBitShort());
        }
        if (R2004Plus())
            vp->ShadePlotMode = static_cast<ShadePlotMode>(r.ReadBitShort());
        if (R2007Plus())
        {
            vp->UseDefaultLighting = r.ReadBit();
            vp->DefaultLightingType = static_cast<LightingType>(r.ReadByte());
            vp->Brightness = r.ReadBitDouble();
            vp->Contrast = r.ReadBitDouble();
            vp->AmbientLightColor = m_s.ReadCmColor();
        }
        if (R13_14Only())
            HandleRef();    // 视口实体头
        if (R2000Plus())
        {
            if (!r.CheckCount(frozen))
                return vp;
            for (int i = 0; i < frozen; ++i)
                vp->FrozenLayers.push_back(HandleRef());
            vp->BoundaryHandle = HandleRef();
        }
        if (m_version == CadVersion::AC1015)
            HandleRef();    // 视口实体头
        if (R2000Plus())
        {
            HandleRef();    // 命名 UCS
            HandleRef();    // 基准 UCS
        }
        if (R2007Plus())
        {
            HandleRef();    // 背景
            vp->VisualStyleHandle = HandleRef();
            HandleRef();    // 着色打印
            HandleRef();    // 太阳
        }
        return vp;
    }

    std::unique_ptr<CadObject> Reader::ReadImage(bool wipeout, ObjectInfo& info)
    {
        std::unique_ptr<CadWipeoutBase> image;
        if (wipeout)
            image = std::make_unique<Wipeout>();
        else
            image = std::make_unique<RasterImage>();
        ReadCommonEntityData(*image, info);
        DwgBitReader& r = m_objReader;
        image->ClassVersion = r.ReadBitLong();
        image->InsertPoint = r.Read3BitDouble();
        image->UVector = r.Read3BitDouble();
        image->VVector = r.Read3BitDouble();
        image->Size = r.Read2RawDouble();
        image->Flags = static_cast<ImageDisplayFlags>(r.ReadBitShort());
        image->ClippingState = r.ReadBit();
        image->Brightness = r.ReadByte();
        image->Contrast = r.ReadByte();
        image->Fade = r.ReadByte();
        if (R2010Plus())
            image->ClipMode = r.ReadBit() ? ClipMode::Inside : ClipMode::Outside;
        switch (r.ReadBitShort())
        {
        case 1:     // 矩形
            image->ClipBoundaryVertices.push_back(r.Read2RawDouble());
            image->ClipBoundaryVertices.push_back(r.Read2RawDouble());
            break;
        case 2:     // 多边形
        {
            const std::int32_t n = r.ReadBitLong();
            if (!r.CheckCount(n, 128))
                return image;
            for (int i = 0; i < n; ++i)
                image->ClipBoundaryVertices.push_back(r.Read2RawDouble());
            break;
        }
        default:
            break;
        }
        image->DefinitionHandle = HandleRef();
        image->DefinitionReactorHandle = HandleRef();
        return image;
    }
}

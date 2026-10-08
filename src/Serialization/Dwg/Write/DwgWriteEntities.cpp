// DWG 写入：实体（对应 ACadSharp DwgObjectWriter.Entities；字段顺序与 DwgReadEntities.cpp 逐项对应）
#include "Dwg/Write/DwgWriterImpl.h"
#include "Database/DimensionMeasurement.h"
#include <algorithm>
#include <cstring>

namespace MiniDWG::DwgWrite
{
    namespace
    {
        bool EqualsIgnoreCase(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                char x = a[i], y = b[i];
                if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 'a' + 'A');
                if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 'a' + 'A');
                if (x != y)
                    return false;
            }
            return true;
        }

        // 按位比较（-0.0 与 0.0 不同）
        bool IsZero(double v)
        {
            std::uint64_t bits;
            std::memcpy(&bits, &v, sizeof(bits));
            return bits == 0;
        }

        bool IsAxisZ(const XYZ& v) { return v.X == 0.0 && v.Y == 0.0 && v.Z == 1.0; }
    }

    // ── 块与实体链 ─────────────────────────────────────────────────

    void Writer::WriteBlockEntities(const BlockRecord& record)
    {
        if (const auto* block = FindAs<Block>(BlockBeginOf(record)))
            WriteEntity(*block, record.ObjectHandle, kNullHandle, kNullHandle);
        const std::vector<Handle>& list = m_blockEntities[record.ObjectHandle];
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            const auto* entity = FindAs<Entity>(list[i]);
            if (entity == nullptr)
                continue;
            const Handle prev = i > 0 ? list[i - 1] : kNullHandle;
            const Handle next = i + 1 < list.size() ? list[i + 1] : kNullHandle;
            WriteEntity(*entity, record.ObjectHandle, prev, next);
        }
        if (const auto* end = FindAs<BlockEnd>(BlockEndOf(record)))
            WriteEntity(*end, record.ObjectHandle, kNullHandle, kNullHandle);
    }

    void Writer::WriteEntity(const Entity& entity, Handle owner, Handle prev, Handle next)
    {
        if (!BeginObject(entity))
            return;
        const RawObjectData* raw = RawDataOf(entity);
        m_dataStoreBit = raw != nullptr && raw->Dwg.HasDataStore && m_writeDataStore;
        WriteCommonEntityData(entity, owner, prev, next);
        m_dataStoreBit = false;

        const std::vector<Handle>& children = ChildrenOf(entity.ObjectHandle);
        Handle seqend = kNullHandle;
        if (raw != nullptr)
            WriteRawData(raw->Dwg);
        else if (const auto* insert = dynamic_cast<const Insert*>(&entity))
        {
            seqend = children.empty() ? kNullHandle : SeqendOf(entity);
            WriteInsert(*insert, children, seqend);
        }
        else if (const auto* pl = dynamic_cast<const Polyline*>(&entity))
        {
            seqend = SeqendOf(entity);
            WritePolyline(*pl, children, seqend);
        }
        else if (const auto* vertex = dynamic_cast<const Vertex*>(&entity))
            WriteVertex(*vertex);
        else if (const auto* text = dynamic_cast<const TextEntity*>(&entity))
        {
            WriteCommonTextData(*text);
            if (const auto* att = dynamic_cast<const AttributeBase*>(text))
            {
                WriteCommonAttData(*att);
                if (const auto* def = dynamic_cast<const AttributeDefinition*>(att))
                {
                    if (R2010Plus())
                        M().WriteByte(0);   // 版本
                    Text(def->Prompt);
                }
            }
        }
        else if (const auto* dim = dynamic_cast<const Dimension*>(&entity))
            WriteDimension(*dim);
        else if (const auto* mtext = dynamic_cast<const MText*>(&entity))
            WriteMTextBody(*mtext);
        else if (const auto* ml = dynamic_cast<const MultiLeader*>(&entity))
            WriteMultiLeader(*ml);
        else if (const auto* lw = dynamic_cast<const LwPolyline*>(&entity))
            WriteLwPolyline(*lw);
        else if (const auto* spline = dynamic_cast<const Spline*>(&entity))
            WriteSpline(*spline);
        else if (const auto* hatch = dynamic_cast<const Hatch*>(&entity))
            WriteHatch(*hatch);
        else if (const auto* leader = dynamic_cast<const Leader*>(&entity))
            WriteLeader(*leader);
        else if (const auto* tolerance = dynamic_cast<const Tolerance*>(&entity))
            WriteTolerance(*tolerance);
        else if (const auto* mline = dynamic_cast<const MLine*>(&entity))
            WriteMLine(*mline);
        else if (const auto* vp = dynamic_cast<const Viewport*>(&entity))
            WriteViewport(*vp);
        else if (const auto* image = dynamic_cast<const CadWipeoutBase*>(&entity))
            WriteImage(*image);
        else
            WriteSimpleEntity(entity);
        EndObject(entity.ObjectHandle);

        if (!children.empty() || seqend != kNullHandle)
            WriteChildren(children, seqend, entity.ObjectHandle);
    }

    // 顶点、属性及其 SEQEND：R2000 中它们自成一条前后链
    void Writer::WriteChildren(const std::vector<Handle>& children, Handle seqend, Handle owner)
    {
        for (std::size_t i = 0; i < children.size(); ++i)
        {
            const auto* child = FindAs<Entity>(children[i]);
            if (child == nullptr)
                continue;
            const Handle prev = i > 0 ? children[i - 1] : kNullHandle;
            const Handle next = i + 1 < children.size() ? children[i + 1] : kNullHandle;
            WriteEntity(*child, owner, prev, next);
        }
        if (const auto* end = FindAs<Seqend>(seqend))
            WriteEntity(*end, owner, children.empty() ? kNullHandle : children.back(), kNullHandle);
    }

    // ── 公共数据 ───────────────────────────────────────────────────

    void Writer::WriteCommonEntityData(const Entity& entity, Handle owner, Handle prev, Handle next)
    {
        // 代理图形：只有原样保留的未建模实体带有
        const auto* unknown = dynamic_cast<const UnknownEntity*>(&entity);
        if (unknown != nullptr && !unknown->ProxyGraphics.empty())
        {
            M().WriteBit(true);
            if (R2010Plus())
                M().WriteBitLongLong(static_cast<std::int64_t>(unknown->ProxyGraphics.size()));
            else
                M().WriteRawLong(static_cast<std::int32_t>(unknown->ProxyGraphics.size()));
            M().WriteBytes(unknown->ProxyGraphics);
        }
        else
        {
            M().WriteBit(false);
        }
        WriteEntityMode(entity, owner, prev, next);
    }

    // 原始数据接在重新编码的公共数据之后：数据流、R2007 起的字符串流、句柄流各自原样追加
    void Writer::WriteRawData(const RawDwgData& raw)
    {
        m_main.AppendRawBits(raw.Main, raw.MainBits);
        if (R2007Plus())
            m_text.AppendRawBits(raw.Text, raw.TextBits);
        m_hnd.AppendRawBits(raw.Handles, raw.HandleBits);
    }

    void Writer::WriteEntityMode(const Entity& entity, Handle owner, Handle prev, Handle next)
    {
        // 00：有所有者句柄（顶点、属性、SEQEND、BLOCK、ENDBLK 与普通块中的实体）；01：图纸空间；10：模型空间
        std::uint8_t mode = 0;
        const bool child = dynamic_cast<const Block*>(&entity) || dynamic_cast<const BlockEnd*>(&entity)
            || dynamic_cast<const Seqend*>(&entity) || dynamic_cast<const Vertex*>(&entity)
            || dynamic_cast<const AttributeEntity*>(&entity);
        if (!child)
        {
            if (m_db.ModelSpace() != nullptr && owner == m_db.ModelSpace()->ObjectHandle)
                mode = 2;
            else if (m_db.PaperSpace() != nullptr && owner == m_db.PaperSpace()->ObjectHandle)
                mode = 1;
        }
        M().Write2Bits(mode);
        if (mode == 0)
            H(DwgRef::SoftPointer, owner);

        WriteReactorsAndXDictionary(entity);

        // R2000：前后实体句柄，恰好是句柄 ±1 时省略
        if (R2004Pre())
        {
            const Handle h = entity.ObjectHandle;
            const bool noLinks = prev == h - 1 && next == h + 1;
            M().WriteBit(noLinks);
            if (!noLinks)
            {
                H(DwgRef::SoftPointer, prev);
                H(DwgRef::SoftPointer, next);
            }
        }

        // 颜色簿颜色（DBCOLOR 原样保留时）
        const bool bookColor = R2004Plus() && Written(entity.BookColorHandle);
        M().WriteEnColor(entity.Color, entity.Transparency, bookColor);
        if (bookColor)
            H(DwgRef::HardPointer, entity.BookColorHandle);
        M().WriteBitDouble(entity.LineTypeScale);

        // 图层：找不到时用 0 图层
        Handle layer = entity.LayerHandle;
        if (!Written(layer) || FindAs<Layer>(layer) == nullptr)
            layer = TableEntryByName(m_db.Layers(), "0");
        H(DwgRef::HardPointer, layer);

        // 线型：00 ByLayer、01 ByBlock、10 Continuous、11 后跟句柄
        const auto* lineType = FindAs<LineType>(entity.LineTypeHandle);
        if (lineType == nullptr || !Written(entity.LineTypeHandle) || EqualsIgnoreCase(lineType->Name, "ByLayer"))
            M().Write2Bits(0);
        else if (EqualsIgnoreCase(lineType->Name, "ByBlock"))
            M().Write2Bits(1);
        else if (EqualsIgnoreCase(lineType->Name, "Continuous"))
            M().Write2Bits(2);
        else
        {
            M().Write2Bits(3);
            H(DwgRef::HardPointer, entity.LineTypeHandle);
        }
        if (R2007Plus())
        {
            // 材质（原样保留的对象写回同一版本时才有）：00 ByLayer、01 ByBlock、11 后跟句柄
            const CadDictionary* materials = m_db.FindNamedDictionary("ACAD_MATERIAL");
            const CadObject* byBlock = materials != nullptr ? m_db.FindDictionaryEntry(materials, "ByBlock") : nullptr;
            const CadObject* byLayer = materials != nullptr ? m_db.FindDictionaryEntry(materials, "ByLayer") : nullptr;
            if (byBlock != nullptr && entity.MaterialHandle == byBlock->ObjectHandle)
            {
                M().Write2Bits(1);
            }
            else if (byLayer != nullptr && entity.MaterialHandle == byLayer->ObjectHandle)
            {
                M().Write2Bits(0);
            }
            else if (Written(entity.MaterialHandle))
            {
                M().Write2Bits(3);
                H(DwgRef::HardPointer, entity.MaterialHandle);
            }
            else
            {
                M().Write2Bits(0);
            }
            M().WriteByte(0);   // 阴影
        }
        M().Write2Bits(0);      // 打印样式 ByLayer
        if (R2010Plus())
        {
            M().WriteBit(false);    // 视觉样式：完整、面、边
            M().WriteBit(false);
            M().WriteBit(false);
        }
        M().WriteBitShort(entity.IsInvisible ? 1 : 0);
        M().WriteByte(LineWeightToIndex(entity.LineWeight));
    }

    // ── 简单实体 ───────────────────────────────────────────────────

    void Writer::WriteSimpleEntity(const Entity& entity)
    {
        DwgBitWriter& m = M();
        if (const auto* block = dynamic_cast<const Block*>(&entity))
        {
            // 完整名称（*U12、*Paper_Space0）以块记录为准
            const auto* record = m_db.FindAs<BlockRecord>(block->OwnerHandle);
            Text(record != nullptr ? record->Name : block->Name);
        }
        else if (const auto* circle = dynamic_cast<const Circle*>(&entity))
        {
            m.Write3BitDouble(circle->Center);
            m.WriteBitDouble(circle->Radius);
            m.WriteBitThickness(circle->Thickness);
            m.WriteBitExtrusion(circle->Normal);
            if (const auto* arc = dynamic_cast<const Arc*>(circle))
            {
                m.WriteBitDouble(arc->StartAngle);
                m.WriteBitDouble(arc->EndAngle);
            }
        }
        else if (const auto* line = dynamic_cast<const Line*>(&entity))
        {
            const bool zIsZero = IsZero(line->StartPoint.Z) && IsZero(line->EndPoint.Z);
            m.WriteBit(zIsZero);
            m.WriteRawDouble(line->StartPoint.X);
            m.WriteBitDoubleWithDefault(line->StartPoint.X, line->EndPoint.X);
            m.WriteRawDouble(line->StartPoint.Y);
            m.WriteBitDoubleWithDefault(line->StartPoint.Y, line->EndPoint.Y);
            if (!zIsZero)
            {
                m.WriteRawDouble(line->StartPoint.Z);
                m.WriteBitDoubleWithDefault(line->StartPoint.Z, line->EndPoint.Z);
            }
            m.WriteBitThickness(line->Thickness);
            m.WriteBitExtrusion(line->Normal);
        }
        else if (const auto* point = dynamic_cast<const Point*>(&entity))
        {
            m.Write3BitDouble(point->Location);
            m.WriteBitThickness(point->Thickness);
            m.WriteBitExtrusion(point->Normal);
            m.WriteBitDouble(point->Rotation);
        }
        else if (const auto* face = dynamic_cast<const Face3D*>(&entity))
        {
            const bool noFlags = static_cast<int>(face->Flags) == 0;
            const bool zIsZero = IsZero(face->FirstCorner.Z);
            m.WriteBit(noFlags);
            m.WriteBit(zIsZero);
            m.WriteRawDouble(face->FirstCorner.X);
            m.WriteRawDouble(face->FirstCorner.Y);
            if (!zIsZero)
                m.WriteRawDouble(face->FirstCorner.Z);
            m.Write3BitDoubleWithDefault(face->FirstCorner, face->SecondCorner);
            m.Write3BitDoubleWithDefault(face->SecondCorner, face->ThirdCorner);
            m.Write3BitDoubleWithDefault(face->ThirdCorner, face->FourthCorner);
            if (!noFlags)
                m.WriteBitShort(static_cast<std::int16_t>(face->Flags));
        }
        else if (const auto* solid = dynamic_cast<const Solid*>(&entity))
        {
            m.WriteBitThickness(solid->Thickness);
            m.WriteBitDouble(solid->FirstCorner.Z);
            for (const XYZ* corner : { &solid->FirstCorner, &solid->SecondCorner, &solid->ThirdCorner, &solid->FourthCorner })
                m.Write2RawDouble({ corner->X, corner->Y });
            m.WriteBitExtrusion(solid->Normal);
        }
        else if (const auto* ellipse = dynamic_cast<const Ellipse*>(&entity))
        {
            m.Write3BitDouble(ellipse->Center);
            m.Write3BitDouble(ellipse->MajorAxisEndPoint);
            m.Write3BitDouble(ellipse->Normal);
            m.WriteBitDouble(ellipse->RadiusRatio);
            m.WriteBitDouble(ellipse->StartParameter);
            m.WriteBitDouble(ellipse->EndParameter);
        }
        else if (const auto* ray = dynamic_cast<const Ray*>(&entity))
        {
            m.Write3BitDouble(ray->StartPoint);
            m.Write3BitDouble(ray->Direction);
        }
        else if (const auto* xline = dynamic_cast<const XLine*>(&entity))
        {
            m.Write3BitDouble(xline->FirstPoint);
            m.Write3BitDouble(xline->Direction);
        }
        // ENDBLK、SEQEND 没有数据
    }

    // ── 文字与属性 ─────────────────────────────────────────────────

    void Writer::WriteCommonTextData(const TextEntity& text)
    {
        DwgBitWriter& m = M();
        const double elevation = text.InsertPoint.Z;
        // 数据标志：各位为 1 表示对应的值取默认值、不存储
        std::uint8_t flags = 0;
        const bool noAlignment = IsZero(text.AlignmentPoint.X) && IsZero(text.AlignmentPoint.Y) && IsZero(text.AlignmentPoint.Z);
        if (IsZero(elevation)) flags |= 0x01;
        if (noAlignment) flags |= 0x02;
        if (IsZero(text.ObliqueAngle)) flags |= 0x04;
        if (IsZero(text.Rotation)) flags |= 0x08;
        if (text.WidthFactor == 1.0) flags |= 0x10;
        if (static_cast<int>(text.Mirror) == 0) flags |= 0x20;
        if (static_cast<int>(text.HorizontalAlignment) == 0) flags |= 0x40;
        if (static_cast<int>(text.VerticalAlignment) == 0) flags |= 0x80;

        m.WriteByte(flags);
        if ((flags & 0x01) == 0)
            m.WriteRawDouble(elevation);
        m.Write2RawDouble({ text.InsertPoint.X, text.InsertPoint.Y });
        if ((flags & 0x02) == 0)
        {
            m.WriteBitDoubleWithDefault(text.InsertPoint.X, text.AlignmentPoint.X);
            m.WriteBitDoubleWithDefault(text.InsertPoint.Y, text.AlignmentPoint.Y);
        }
        m.WriteBitExtrusion(text.Normal);
        m.WriteBitThickness(text.Thickness);
        if ((flags & 0x04) == 0)
            m.WriteRawDouble(text.ObliqueAngle);
        if ((flags & 0x08) == 0)
            m.WriteRawDouble(text.Rotation);
        m.WriteRawDouble(text.Height);
        if ((flags & 0x10) == 0)
            m.WriteRawDouble(text.WidthFactor);
        Text(text.Value);
        if ((flags & 0x20) == 0)
            m.WriteBitShort(static_cast<std::int16_t>(text.Mirror));
        if ((flags & 0x40) == 0)
            m.WriteBitShort(static_cast<std::int16_t>(text.HorizontalAlignment));
        if ((flags & 0x80) == 0)
            m.WriteBitShort(static_cast<std::int16_t>(text.VerticalAlignment));
        H(DwgRef::HardPointer, text.StyleHandle);
    }

    void Writer::WriteCommonAttData(const AttributeBase& att)
    {
        DwgBitWriter& m = M();
        if (R2010Plus())
            m.WriteByte(0);     // 版本（0 = R2010）
        if (R2018Plus())
        {
            // 多行属性的内嵌 MTEXT 未建模：写为单行
            if (att.AttributeType != AttributeType::SingleLine)
                NotifyOnce("att:multiline", NotificationType::Info, "多行属性写为单行（内嵌 MTEXT 未建模）");
            m.WriteByte(static_cast<std::uint8_t>(AttributeType::SingleLine));
        }
        Text(att.Tag);
        m.WriteBitShort(0);     // 字段长度
        m.WriteByte(static_cast<std::uint8_t>(att.Flags));
        if (R2007Plus())
            m.WriteBit(att.Version != 0);   // 锁定位置（读取时与版本号共用 Version）
    }

    // ── 块参照与多段线 ─────────────────────────────────────────────

    void Writer::WriteInsert(const Insert& insert, const std::vector<Handle>& attributes, Handle seqend)
    {
        DwgBitWriter& m = M();
        m.Write3BitDouble(insert.InsertPoint);
        const double x = insert.XScale, y = insert.YScale, z = insert.ZScale;
        if (x == 1.0 && y == 1.0 && z == 1.0)
        {
            m.Write2Bits(3);
        }
        else if (x == y && x == z)
        {
            m.Write2Bits(2);
            m.WriteRawDouble(x);
        }
        else if (x == 1.0)
        {
            m.Write2Bits(1);
            m.WriteBitDoubleWithDefault(1.0, y);
            m.WriteBitDoubleWithDefault(1.0, z);
        }
        else
        {
            m.Write2Bits(0);
            m.WriteRawDouble(x);
            m.WriteBitDoubleWithDefault(x, y);
            m.WriteBitDoubleWithDefault(x, z);
        }
        m.WriteBitDouble(insert.Rotation);
        m.Write3BitDouble(insert.Normal);
        const bool hasAttributes = !attributes.empty();
        m.WriteBit(hasAttributes);
        if (R2004Plus() && hasAttributes)
            m.WriteBitLong(static_cast<std::int32_t>(attributes.size()));
        if (insert.ColumnCount > 1 || insert.RowCount > 1)
        {
            m.WriteBitShort(static_cast<std::int16_t>(insert.ColumnCount));
            m.WriteBitShort(static_cast<std::int16_t>(insert.RowCount));
            m.WriteBitDouble(insert.ColumnSpacing);
            m.WriteBitDouble(insert.RowSpacing);
        }
        H(DwgRef::HardPointer, insert.BlockHandle);
        if (!hasAttributes)
            return;
        if (R2004Pre())
        {
            H(DwgRef::SoftPointer, attributes.front());
            H(DwgRef::SoftPointer, attributes.back());
        }
        else
        {
            for (Handle a : attributes)
                H(DwgRef::HardOwnership, a);
        }
        H(DwgRef::HardOwnership, seqend);
    }

    void Writer::WritePolyline(const Polyline& pl, const std::vector<Handle>& vertices, Handle seqend)
    {
        DwgBitWriter& m = M();
        if (dynamic_cast<const PolyfaceMesh*>(&pl) != nullptr)
        {
            std::int16_t vertexCount = 0, faceCount = 0;
            for (Handle v : vertices)
                ++(FindAs<VertexFaceRecord>(v) != nullptr ? faceCount : vertexCount);
            m.WriteBitShort(vertexCount);
            m.WriteBitShort(faceCount);
        }
        else if (const auto* mesh = dynamic_cast<const PolygonMesh*>(&pl))
        {
            m.WriteBitShort(static_cast<std::int16_t>(pl.Flags));
            m.WriteBitShort(static_cast<std::int16_t>(pl.SmoothSurface));
            m.WriteBitShort(mesh->MVertexCount);
            m.WriteBitShort(mesh->NVertexCount);
            m.WriteBitShort(mesh->MSmoothSurfaceDensity);
            m.WriteBitShort(mesh->NSmoothSurfaceDensity);
        }
        else if (dynamic_cast<const Polyline3D*>(&pl) != nullptr)
        {
            std::uint8_t splineFlags = 0;
            if (HasFlag(pl.Flags, PolylineFlags::SplineFit))
                splineFlags = pl.SmoothSurface == SmoothSurfaceType::Quadratic ? 1 : 2;
            m.WriteByte(splineFlags);
            m.WriteByte(HasFlag(pl.Flags, PolylineFlags::ClosedPolylineOrClosedPolygonMeshInM) ? 1 : 0);
        }
        else
        {
            m.WriteBitShort(static_cast<std::int16_t>(pl.Flags));
            m.WriteBitShort(static_cast<std::int16_t>(pl.SmoothSurface));
            m.WriteBitDouble(pl.StartWidth);
            m.WriteBitDouble(pl.EndWidth);
            m.WriteBitThickness(pl.Thickness);
            m.WriteBitDouble(pl.Elevation);
            m.WriteBitExtrusion(pl.Normal);
        }
        if (R2004Plus())
        {
            m.WriteBitLong(static_cast<std::int32_t>(vertices.size()));
            for (Handle v : vertices)
                H(DwgRef::HardOwnership, v);
        }
        else
        {
            H(DwgRef::SoftPointer, vertices.empty() ? kNullHandle : vertices.front());
            H(DwgRef::SoftPointer, vertices.empty() ? kNullHandle : vertices.back());
        }
        H(DwgRef::HardOwnership, seqend);
    }

    void Writer::WriteVertex(const Vertex& v)
    {
        DwgBitWriter& m = M();
        if (const auto* face = dynamic_cast<const VertexFaceRecord*>(&v))
        {
            m.WriteBitShort(face->Index1);
            m.WriteBitShort(face->Index2);
            m.WriteBitShort(face->Index3);
            m.WriteBitShort(face->Index4);
            return;
        }
        m.WriteByte(static_cast<std::uint8_t>(v.Flags));
        m.Write3BitDouble(v.Location);
        if (dynamic_cast<const Vertex2D*>(&v) == nullptr)
            return;
        // 起止宽度相同且不为 0 时写一个负值
        if (v.StartWidth == v.EndWidth && v.StartWidth > 0.0)
        {
            m.WriteBitDouble(-v.StartWidth);
        }
        else
        {
            m.WriteBitDouble(v.StartWidth);
            m.WriteBitDouble(v.EndWidth);
        }
        m.WriteBitDouble(v.Bulge);
        if (R2010Plus())
            m.WriteBitLong(v.Id);
        m.WriteBitDouble(v.CurveTangent);
    }

    // ── 标注 ───────────────────────────────────────────────────────

    void Writer::WriteCommonDimensionData(const Dimension& dim)
    {
        DwgBitWriter& m = M();
        if (R2010Plus())
            m.WriteByte(dim.Version);
        m.Write3BitDouble(dim.Normal);
        m.Write2RawDouble({ dim.TextMiddlePoint.X, dim.TextMiddlePoint.Y });
        m.WriteBitDouble(dim.TextMiddlePoint.Z);
        std::uint8_t flags = HasFlag(dim.Flags, DimensionType::TextUserDefinedLocation) ? 0 : 1;
        // 有匿名块时总是置上（与 DXF 写出一致，R12 文件中没有这一位）
        if (HasFlag(dim.Flags, DimensionType::BlockReference) || Written(dim.BlockHandle))
            flags |= 2;
        m.WriteByte(flags);
        Text(dim.Text);
        m.WriteBitDouble(dim.TextRotation);
        m.WriteBitDouble(dim.HorizontalDirection);
        m.Write3BitDouble({ 1.0, 1.0, 1.0 });   // 匿名块的插入比例（未公开）
        m.WriteBitDouble(0.0);                  // 匿名块的插入旋转
        if (R2000Plus())
        {
            m.WriteBitShort(static_cast<std::int16_t>(dim.AttachmentPoint));
            m.WriteBitShort(dim.LineSpacingStyle == LineSpacingStyleType::None
                                ? static_cast<std::int16_t>(LineSpacingStyleType::AtLeast)
                                : static_cast<std::int16_t>(dim.LineSpacingStyle));
            m.WriteBitDouble(dim.LineSpacingFactor);
            // R12 文件没有测量值（读入为 0）：由定义点计算（与 DXF 写出一致）
            m.WriteBitDouble(dim.Measurement != 0.0 ? dim.Measurement : ComputeDimensionMeasurement(dim));
        }
        if (R2007Plus())
        {
            m.WriteBit(false);
            m.WriteBit(dim.FlipArrow1);
            m.WriteBit(dim.FlipArrow2);
        }
        m.Write2RawDouble({ dim.InsertionPoint.X, dim.InsertionPoint.Y });
    }

    void Writer::WriteDimension(const Dimension& dim)
    {
        DwgBitWriter& m = M();
        WriteCommonDimensionData(dim);
        if (const auto* aligned = dynamic_cast<const DimensionAligned*>(&dim))
        {
            m.Write3BitDouble(aligned->FirstPoint);
            m.Write3BitDouble(aligned->SecondPoint);
            m.Write3BitDouble(aligned->DefinitionPoint);
            m.WriteBitDouble(aligned->ExtLineRotation);
            if (const auto* linear = dynamic_cast<const DimensionLinear*>(&dim))
                m.WriteBitDouble(linear->Rotation);
        }
        else if (const auto* a3 = dynamic_cast<const DimensionAngular3Pt*>(&dim))
        {
            m.Write3BitDouble(a3->DefinitionPoint);
            m.Write3BitDouble(a3->FirstPoint);
            m.Write3BitDouble(a3->SecondPoint);
            m.Write3BitDouble(a3->AngleVertex);
        }
        else if (const auto* a2 = dynamic_cast<const DimensionAngular2Line*>(&dim))
        {
            m.Write2RawDouble({ a2->DimensionArc.X, a2->DimensionArc.Y });
            m.Write3BitDouble(a2->FirstPoint);
            m.Write3BitDouble(a2->SecondPoint);
            m.Write3BitDouble(a2->AngleVertex);
            m.Write3BitDouble(a2->DefinitionPoint);
        }
        else if (const auto* diameter = dynamic_cast<const DimensionDiameter*>(&dim))
        {
            m.Write3BitDouble(diameter->AngleVertex);
            m.Write3BitDouble(diameter->DefinitionPoint);
            m.WriteBitDouble(diameter->LeaderLength);
        }
        else if (const auto* radius = dynamic_cast<const DimensionRadius*>(&dim))
        {
            m.Write3BitDouble(radius->DefinitionPoint);
            m.Write3BitDouble(radius->AngleVertex);
            m.WriteBitDouble(radius->LeaderLength);
        }
        else if (const auto* ordinate = dynamic_cast<const DimensionOrdinate*>(&dim))
        {
            m.Write3BitDouble(ordinate->DefinitionPoint);
            m.Write3BitDouble(ordinate->FeatureLocation);
            m.Write3BitDouble(ordinate->LeaderEndpoint);
            m.WriteByte(HasFlag(ordinate->Flags, DimensionType::OrdinateTypeX) ? 1 : 0);
        }
        else if (const auto* arc = dynamic_cast<const DimensionArc*>(&dim))
        {
            m.Write3BitDouble(arc->DefinitionPoint);
            m.Write3BitDouble(arc->FirstPoint);
            m.Write3BitDouble(arc->SecondPoint);
            m.Write3BitDouble(arc->Center);
            m.WriteBit(arc->IsPartial);
            m.WriteBitDouble(arc->StartAngle);
            m.WriteBitDouble(arc->EndAngle);
            m.WriteBit(arc->HasLeader);
            m.Write3BitDouble(arc->LeaderPoint1);
            m.Write3BitDouble(arc->LeaderPoint2);
        }
        H(DwgRef::HardPointer, dim.StyleHandle);
        H(DwgRef::HardPointer, dim.BlockHandle);
    }

    // ── 多行文字 ───────────────────────────────────────────────────

    void Writer::WriteMTextBody(const MText& mtext)
    {
        DwgBitWriter& m = M();
        m.Write3BitDouble(mtext.InsertPoint);
        m.Write3BitDouble(mtext.Normal);
        m.Write3BitDouble(mtext.AlignmentPoint);
        m.WriteBitDouble(mtext.RectangleWidth);
        if (R2007Plus())
            m.WriteBitDouble(mtext.RectangleHeight);
        m.WriteBitDouble(mtext.Height);
        m.WriteBitShort(static_cast<std::int16_t>(mtext.AttachmentPoint));
        m.WriteBitShort(static_cast<std::int16_t>(mtext.DrawingDirection));
        // 文字范围的高度、宽度：未建模（AutoCAD 打开时重算）
        m.WriteBitDouble(0.0);
        m.WriteBitDouble(0.0);
        Text(mtext.Value);
        H(DwgRef::HardPointer, mtext.StyleHandle);
        if (R2000Plus())
        {
            // 行距样式只能是 1（至少）或 2（精确），0 时 AutoCAD 核查报错
            m.WriteBitShort(mtext.LineSpacingStyle == LineSpacingStyleType::None
                                ? static_cast<std::int16_t>(LineSpacingStyleType::AtLeast)
                                : static_cast<std::int16_t>(mtext.LineSpacingStyle));
            m.WriteBitDouble(mtext.LineSpacing);
            m.WriteBit(false);
        }
        if (R2004Plus())
        {
            const int flags = static_cast<int>(mtext.BackgroundFillFlags);
            m.WriteBitLong(flags);
            if ((flags & static_cast<int>(BackgroundFillFlags::UseBackgroundFillColor)) != 0
                || (m_version > CadVersion::AC1027 && (flags & static_cast<int>(BackgroundFillFlags::TextFrame)) != 0))
            {
                m.WriteBitDouble(mtext.BackgroundScale);
                Cmc(mtext.BackgroundColor);
                m.WriteBitLong(Transparency::ToAlphaValue(mtext.BackgroundTransparency));
            }
        }
        if (!R2018Plus())
            return;

        // R2018：位为 1 表示不是注释性，后面是重复的几何数据与分栏数据
        m.WriteBit(true);
        m.WriteBitShort(4);         // 版本
        m.WriteBit(true);           // 默认
        H(DwgRef::HardPointer, kNullHandle);    // 注册的应用程序
        m.WriteBitLong(static_cast<std::int32_t>(mtext.AttachmentPoint));
        m.Write3BitDouble(mtext.AlignmentPoint);
        m.Write3BitDouble(mtext.InsertPoint);
        m.WriteBitDouble(mtext.RectangleWidth);
        m.WriteBitDouble(mtext.RectangleHeight);
        m.WriteBitDouble(0.0);      // 文字范围宽度
        m.WriteBitDouble(0.0);      // 文字范围高度
        const MTextTextColumnData& col = mtext.ColumnData;
        m.WriteBitShort(static_cast<std::int16_t>(col.ColumnType));
        if (col.ColumnType == ColumnType::NoColumns)
            return;
        m.WriteBitLong(col.ColumnCount);
        m.WriteBitDouble(col.Width);
        m.WriteBitDouble(col.Gutter);
        m.WriteBit(col.AutoHeight);
        m.WriteBit(col.FlowReversed);
        if (!col.AutoHeight && col.ColumnType == ColumnType::DynamicColumns)
        {
            for (std::int32_t i = 0; i < col.ColumnCount; ++i)
                m.WriteBitDouble(i < static_cast<std::int32_t>(col.Heights.size()) ? col.Heights[i] : 0.0);
        }
    }

    // ── 轻量多段线、样条、填充 ─────────────────────────────────────

    void Writer::WriteLwPolyline(const LwPolyline& pl)
    {
        DwgBitWriter& m = M();
        const auto& vs = pl.Vertices;
        const bool bulges = std::any_of(vs.begin(), vs.end(), [](const LwPolylineVertex& v) { return !IsZero(v.Bulge); });
        const bool ids = R2010Plus() && std::any_of(vs.begin(), vs.end(), [](const LwPolylineVertex& v) { return v.Id != 0; });
        const bool widths = std::any_of(vs.begin(), vs.end(), [](const LwPolylineVertex& v) {
            return !IsZero(v.StartWidth) || !IsZero(v.EndWidth);
        });
        std::int16_t flags = 0;
        if (HasFlag(pl.Flags, LwPolylineFlags::Plinegen)) flags |= 0x100;
        if (HasFlag(pl.Flags, LwPolylineFlags::Closed)) flags |= 0x200;
        if (!IsZero(pl.ConstantWidth)) flags |= 0x4;
        if (!IsZero(pl.Elevation)) flags |= 0x8;
        if (!IsZero(pl.Thickness)) flags |= 0x2;
        if (!IsAxisZ(pl.Normal)) flags |= 0x1;
        if (bulges) flags |= 0x10;
        if (ids) flags |= 0x400;
        if (widths) flags |= 0x20;
        m.WriteBitShort(flags);
        if (flags & 0x4) m.WriteBitDouble(pl.ConstantWidth);
        if (flags & 0x8) m.WriteBitDouble(pl.Elevation);
        if (flags & 0x2) m.WriteBitDouble(pl.Thickness);
        if (flags & 0x1) m.Write3BitDouble(pl.Normal);

        const auto count = static_cast<std::int32_t>(vs.size());
        m.WriteBitLong(count);
        if (bulges) m.WriteBitLong(count);
        if (ids) m.WriteBitLong(count);
        if (widths) m.WriteBitLong(count);
        for (std::size_t i = 0; i < vs.size(); ++i)
        {
            if (i == 0)
                m.Write2RawDouble(vs[0].Location);
            else
                m.Write2BitDoubleWithDefault(vs[i - 1].Location, vs[i].Location);
        }
        if (bulges)
        {
            for (const LwPolylineVertex& v : vs)
                m.WriteBitDouble(v.Bulge);
        }
        if (ids)
        {
            for (const LwPolylineVertex& v : vs)
                m.WriteBitLong(v.Id);
        }
        if (widths)
        {
            for (const LwPolylineVertex& v : vs)
            {
                m.WriteBitDouble(v.StartWidth);
                m.WriteBitDouble(v.EndWidth);
            }
        }
    }

    void Writer::WriteSpline(const Spline& spline)
    {
        DwgBitWriter& m = M();
        // Flags1 也并在 DXF 组码 70 的高位（DXF 读入的样条只有这里有）
        int flags1 = static_cast<int>(spline.Flags1) | ((static_cast<int>(spline.Flags) >> 7) & 0xF);
        // 场景 2 只存拟合数据（AutoCAD 打开时重算控制点），1 存控制点与节点
        const bool fitData = !spline.FitPoints.empty()
            && (spline.ControlPoints.empty() || (flags1 & static_cast<int>(SplineFlags1::MethodFitPoints)) != 0);
        const int scenario = fitData ? 2 : 1;
        if (R2013Plus())
        {
            if (scenario == 2)
                flags1 |= static_cast<int>(SplineFlags1::MethodFitPoints | SplineFlags1::UseKnotParameter);
            else
                flags1 &= ~static_cast<int>(SplineFlags1::UseKnotParameter);
            // 与 AutoCAD 一致：R2013 起场景字段总是 1（读取时由标志与节点参数化判断）；
            // 控制点样条的节点参数化为 15（自定义），拟合点样条取模型中的值
            KnotParametrization knots = spline.KnotParametrization;
            if (scenario == 1)
                knots = KnotParametrization::Custom;
            else if (knots == KnotParametrization::Custom)
                knots = KnotParametrization::Chord;
            m.WriteBitLong(1);
            m.WriteBitLong(flags1);
            m.WriteBitLong(static_cast<std::int32_t>(knots));
        }
        else
        {
            m.WriteBitLong(scenario);
        }
        m.WriteBitLong(spline.Degree);
        if (scenario == 1)
        {
            const bool weighted = !spline.Weights.empty() && spline.Weights.size() >= spline.ControlPoints.size();
            m.WriteBit(HasFlag(spline.Flags, SplineFlags::Rational));
            m.WriteBit(HasFlag(spline.Flags, SplineFlags::Closed));
            m.WriteBit(HasFlag(spline.Flags, SplineFlags::Periodic));
            m.WriteBitDouble(spline.KnotTolerance);
            m.WriteBitDouble(spline.ControlPointTolerance);
            m.WriteBitLong(static_cast<std::int32_t>(spline.Knots.size()));
            m.WriteBitLong(static_cast<std::int32_t>(spline.ControlPoints.size()));
            m.WriteBit(weighted);
            for (double k : spline.Knots)
                m.WriteBitDouble(k);
            for (std::size_t i = 0; i < spline.ControlPoints.size(); ++i)
            {
                m.Write3BitDouble(spline.ControlPoints[i]);
                if (weighted)
                    m.WriteBitDouble(spline.Weights[i]);
            }
        }
        else
        {
            m.WriteBitDouble(spline.FitTolerance);
            m.Write3BitDouble(spline.StartTangent);
            m.Write3BitDouble(spline.EndTangent);
            m.WriteBitLong(static_cast<std::int32_t>(spline.FitPoints.size()));
            for (const XYZ& p : spline.FitPoints)
                m.Write3BitDouble(p);
        }
    }

    void Writer::WriteHatch(const Hatch& hatch)
    {
        DwgBitWriter& m = M();
        if (R2004Plus())
        {
            const HatchGradientPattern& g = hatch.GradientColor;
            m.WriteBitLong(g.Enabled ? 1 : 0);
            m.WriteBitLong(g.Reserved);
            m.WriteBitDouble(g.Angle);
            m.WriteBitDouble(g.Shift);
            m.WriteBitLong(g.IsSingleColorGradient ? 1 : 0);
            m.WriteBitDouble(g.ColorTint);
            m.WriteBitLong(static_cast<std::int32_t>(g.Colors.size()));
            for (const GradientColor& c : g.Colors)
            {
                m.WriteBitDouble(c.Value);
                Cmc(c.Color);
            }
            Text(g.Name);
        }
        m.WriteBitDouble(hatch.Elevation);
        m.Write3BitDouble(hatch.Normal);
        Text(hatch.Pattern.Name);
        m.WriteBit(hatch.IsSolid);
        m.WriteBit(hatch.IsAssociative);

        m.WriteBitLong(static_cast<std::int32_t>(hatch.Paths.size()));
        bool derived = false;
        for (const HatchBoundaryPath& path : hatch.Paths)
        {
            // 多段线边界：路径只有一条多段线边
            const HatchBoundaryPathPolyline* polyline = nullptr;
            if (path.Edges.size() == 1)
                polyline = dynamic_cast<const HatchBoundaryPathPolyline*>(path.Edges.front().get());
            int flags = static_cast<int>(path.Flags);
            if (polyline != nullptr)
                flags |= static_cast<int>(BoundaryPathFlags::Polyline);
            else
                flags &= ~static_cast<int>(BoundaryPathFlags::Polyline);
            derived |= (flags & static_cast<int>(BoundaryPathFlags::Derived)) != 0;
            m.WriteBitLong(flags);
            if (polyline == nullptr)
            {
                std::int32_t edges = 0;
                for (const auto& edge : path.Edges)
                {
                    if (dynamic_cast<const HatchBoundaryPathPolyline*>(edge.get()) == nullptr)
                        ++edges;
                }
                m.WriteBitLong(edges);
                for (const auto& edge : path.Edges)
                {
                    if (const auto* line = dynamic_cast<const HatchBoundaryPathLine*>(edge.get()))
                    {
                        m.WriteByte(1);
                        m.Write2RawDouble(line->Start);
                        m.Write2RawDouble(line->End);
                    }
                    else if (const auto* arc = dynamic_cast<const HatchBoundaryPathArc*>(edge.get()))
                    {
                        m.WriteByte(2);
                        m.Write2RawDouble(arc->Center);
                        m.WriteBitDouble(arc->Radius);
                        m.WriteBitDouble(arc->StartAngle);
                        m.WriteBitDouble(arc->EndAngle);
                        m.WriteBit(arc->CounterClockWise);
                    }
                    else if (const auto* ellipse = dynamic_cast<const HatchBoundaryPathEllipse*>(edge.get()))
                    {
                        m.WriteByte(3);
                        m.Write2RawDouble(ellipse->Center);
                        m.Write2RawDouble(ellipse->MajorAxisEndPoint);
                        m.WriteBitDouble(ellipse->RadiusRatio);
                        m.WriteBitDouble(ellipse->StartAngle);
                        m.WriteBitDouble(ellipse->EndAngle);
                        m.WriteBit(ellipse->CounterClockWise);
                    }
                    else if (const auto* spline = dynamic_cast<const HatchBoundaryPathSpline*>(edge.get()))
                    {
                        m.WriteByte(4);
                        m.WriteBitLong(spline->Degree);
                        m.WriteBit(spline->IsRational);
                        m.WriteBit(spline->IsPeriodic);
                        m.WriteBitLong(static_cast<std::int32_t>(spline->Knots.size()));
                        m.WriteBitLong(static_cast<std::int32_t>(spline->ControlPoints.size()));
                        for (double k : spline->Knots)
                            m.WriteBitDouble(k);
                        for (const XYZ& p : spline->ControlPoints)
                        {
                            // 权重放在 Z
                            m.Write2RawDouble({ p.X, p.Y });
                            if (spline->IsRational)
                                m.WriteBitDouble(p.Z);
                        }
                        if (R2010Plus())
                        {
                            m.WriteBitLong(static_cast<std::int32_t>(spline->FitPoints.size()));
                            if (!spline->FitPoints.empty())
                            {
                                for (const XY& p : spline->FitPoints)
                                    m.Write2RawDouble(p);
                                m.Write2RawDouble(spline->StartTangent);
                                m.Write2RawDouble(spline->EndTangent);
                            }
                        }
                    }
                }
            }
            else
            {
                const bool hasBulge = std::any_of(polyline->Vertices.begin(), polyline->Vertices.end(),
                                                  [](const XYZ& v) { return !IsZero(v.Z); });
                m.WriteBit(hasBulge);
                m.WriteBit(polyline->IsClosed);
                m.WriteBitLong(static_cast<std::int32_t>(polyline->Vertices.size()));
                for (const XYZ& v : polyline->Vertices)
                {
                    m.Write2RawDouble({ v.X, v.Y });
                    if (hasBulge)
                        m.WriteBitDouble(v.Z);
                }
            }

            std::vector<Handle> sources;
            for (Handle h : path.Entities)
            {
                if (Written(h))
                    sources.push_back(h);
            }
            m.WriteBitLong(static_cast<std::int32_t>(sources.size()));
            for (Handle h : sources)
                H(DwgRef::SoftPointer, h);
        }

        m.WriteBitShort(static_cast<std::int16_t>(hatch.Style));
        m.WriteBitShort(static_cast<std::int16_t>(hatch.PatternType));
        if (!hatch.IsSolid)
        {
            m.WriteBitDouble(hatch.PatternAngle);
            m.WriteBitDouble(hatch.PatternScale);
            m.WriteBit(hatch.IsDouble);
            m.WriteBitShort(static_cast<std::int16_t>(hatch.Pattern.Lines.size()));
            for (const HatchPatternLine& line : hatch.Pattern.Lines)
            {
                m.WriteBitDouble(line.Angle);
                m.Write2BitDouble(line.BasePoint);
                m.Write2BitDouble(line.Offset);
                m.WriteBitShort(static_cast<std::int16_t>(line.DashLengths.size()));
                for (double d : line.DashLengths)
                    m.WriteBitDouble(d);
            }
        }
        if (derived)
            m.WriteBitDouble(hatch.PixelSize);
        m.WriteBitLong(static_cast<std::int32_t>(hatch.SeedPoints.size()));
        for (const XY& p : hatch.SeedPoints)
            m.Write2RawDouble(p);
    }

    // ── 其他实体 ───────────────────────────────────────────────────

    void Writer::WriteLeader(const Leader& leader)
    {
        DwgBitWriter& m = M();
        m.WriteBit(false);
        m.WriteBitShort(static_cast<std::int16_t>(leader.CreationType));
        m.WriteBitShort(static_cast<std::int16_t>(leader.PathType));
        m.WriteBitLong(static_cast<std::int32_t>(leader.Vertices.size()));
        for (const XYZ& v : leader.Vertices)
            m.Write3BitDouble(v);
        m.Write3BitDouble(leader.Vertices.empty() ? XYZ{} : leader.Vertices.front());   // 引线平面原点
        m.Write3BitDouble(leader.Normal);
        m.Write3BitDouble(leader.HorizontalDirection);
        m.Write3BitDouble(leader.BlockOffset);
        m.Write3BitDouble(leader.AnnotationOffset);
        if (m_version <= CadVersion::AC1021)
        {
            m.WriteBitDouble(leader.TextHeight);
            m.WriteBitDouble(leader.TextWidth);
        }
        m.WriteBit(leader.HookLineDirection == HookLineDirection::Same);
        m.WriteBit(leader.ArrowHeadEnabled);
        if (R2000Plus())
        {
            m.WriteBitShort(0);
            m.WriteBit(false);
            m.WriteBit(false);
        }
        H(DwgRef::HardPointer, leader.AssociatedAnnotationHandle);
        H(DwgRef::HardPointer, leader.StyleHandle);
    }

    void Writer::WriteTolerance(const Tolerance& tolerance)
    {
        DwgBitWriter& m = M();
        m.Write3BitDouble(tolerance.InsertionPoint);
        m.Write3BitDouble(tolerance.Direction);
        m.Write3BitDouble(tolerance.Normal);
        Text(tolerance.Text);
        H(DwgRef::HardPointer, tolerance.StyleHandle);
    }

    void Writer::WriteMLine(const MLine& mline)
    {
        DwgBitWriter& m = M();
        m.WriteBitDouble(mline.ScaleFactor);
        m.WriteByte(static_cast<std::uint8_t>(mline.Justification));
        m.Write3BitDouble(mline.StartPoint);
        m.Write3BitDouble(mline.Normal);
        m.WriteBitShort(static_cast<std::int16_t>(mline.Flags));
        const std::size_t lines = mline.Vertices.empty() ? 0 : mline.Vertices.front().Segments.size();
        m.WriteByte(static_cast<std::uint8_t>(lines));
        m.WriteBitShort(static_cast<std::int16_t>(mline.Vertices.size()));
        for (const MLineVertex& v : mline.Vertices)
        {
            m.Write3BitDouble(v.Position);
            m.Write3BitDouble(v.Direction);
            m.Write3BitDouble(v.Miter);
            for (std::size_t j = 0; j < lines; ++j)
            {
                static const MLineVertexSegment kEmpty;
                const MLineVertexSegment& s = j < v.Segments.size() ? v.Segments[j] : kEmpty;
                m.WriteBitShort(static_cast<std::int16_t>(s.Parameters.size()));
                for (double p : s.Parameters)
                    m.WriteBitDouble(p);
                m.WriteBitShort(static_cast<std::int16_t>(s.AreaFillParameters.size()));
                for (double p : s.AreaFillParameters)
                    m.WriteBitDouble(p);
            }
        }
        H(DwgRef::HardPointer, mline.StyleHandle);
    }

    void Writer::WriteViewport(const Viewport& vp)
    {
        DwgBitWriter& m = M();
        m.Write3BitDouble(vp.Center);
        m.WriteBitDouble(vp.Width);
        m.WriteBitDouble(vp.Height);
        if (R2000Plus())
        {
            m.Write3BitDouble(vp.ViewTarget);
            m.Write3BitDouble(vp.ViewDirection);
            m.WriteBitDouble(vp.TwistAngle);
            m.WriteBitDouble(vp.ViewHeight);
            m.WriteBitDouble(vp.LensLength);
            m.WriteBitDouble(vp.FrontClipPlane);
            m.WriteBitDouble(vp.BackClipPlane);
            m.WriteBitDouble(vp.SnapAngle);
            m.Write2RawDouble(vp.ViewCenter);
            m.Write2RawDouble(vp.SnapBase);
            m.Write2RawDouble(vp.SnapSpacing);
            m.Write2RawDouble(vp.GridSpacing);
            m.WriteBitShort(vp.CircleZoomPercent);
        }
        if (R2007Plus())
            m.WriteBitShort(vp.MajorGridLineFrequency);
        std::vector<Handle> frozen;
        for (Handle h : vp.FrozenLayers)
        {
            if (Written(h))
                frozen.push_back(h);
        }
        if (R2000Plus())
        {
            m.WriteBitLong(static_cast<std::int32_t>(frozen.size()));
            m.WriteBitLong(static_cast<std::int32_t>(vp.Status));
            Text(vp.StyleSheetName);
            m.WriteByte(static_cast<std::uint8_t>(vp.RenderMode));
            m.WriteBit(vp.DisplayUcsIcon);
            m.WriteBit(vp.UcsPerViewport);
            m.Write3BitDouble(vp.UcsOrigin);
            m.Write3BitDouble(vp.UcsXAxis);
            m.Write3BitDouble(vp.UcsYAxis);
            m.WriteBitDouble(vp.Elevation);
            m.WriteBitShort(static_cast<std::int16_t>(vp.UcsOrthographicType));
        }
        if (R2004Plus())
            m.WriteBitShort(static_cast<std::int16_t>(vp.ShadePlotMode));
        if (R2007Plus())
        {
            m.WriteBit(vp.UseDefaultLighting);
            m.WriteByte(static_cast<std::uint8_t>(vp.DefaultLightingType));
            m.WriteBitDouble(vp.Brightness);
            m.WriteBitDouble(vp.Contrast);
            Cmc(vp.AmbientLightColor);
        }
        if (R2000Plus())
        {
            for (Handle h : frozen)
                H(DwgRef::HardPointer, h);
            H(DwgRef::HardPointer, vp.BoundaryHandle);
        }
        if (m_version == CadVersion::AC1015)
        {
            auto vx = m_viewportVx.find(vp.ObjectHandle);
            H(DwgRef::HardPointer, vx != m_viewportVx.end() ? vx->second : kNullHandle);   // 视口实体头
        }
        if (R2000Plus())
        {
            H(DwgRef::HardPointer, kNullHandle);    // 命名 UCS
            H(DwgRef::HardPointer, kNullHandle);    // 基准 UCS
        }
        if (R2007Plus())
        {
            H(DwgRef::SoftPointer, kNullHandle);    // 背景
            H(DwgRef::HardPointer, vp.VisualStyleHandle);
            H(DwgRef::SoftPointer, kNullHandle);    // 着色打印
            H(DwgRef::HardOwnership, kNullHandle);  // 太阳
        }
    }

    void Writer::WriteImage(const CadWipeoutBase& image)
    {
        DwgBitWriter& m = M();
        m.WriteBitLong(image.ClassVersion);
        m.Write3BitDouble(image.InsertPoint);
        m.Write3BitDouble(image.UVector);
        m.Write3BitDouble(image.VVector);
        m.Write2RawDouble(image.Size);
        m.WriteBitShort(static_cast<std::int16_t>(image.Flags));
        m.WriteBit(image.ClippingState);
        m.WriteByte(image.Brightness);
        m.WriteByte(image.Contrast);
        m.WriteByte(image.Fade);
        if (R2010Plus())
            m.WriteBit(image.ClipMode == ClipMode::Inside);

        // 裁剪边界：两个点是矩形（1），多于两个是多边形（2，DWG 中不重复首点）；没有时为整幅图像
        std::vector<XY> clip = image.ClipBoundaryVertices;
        if (clip.size() > 3 && clip.front() == clip.back())
            clip.pop_back();
        if (clip.size() > 2)
        {
            m.WriteBitShort(2);
            m.WriteBitLong(static_cast<std::int32_t>(clip.size()));
            for (const XY& p : clip)
                m.Write2RawDouble(p);
        }
        else
        {
            m.WriteBitShort(1);
            if (clip.size() < 2)
                clip = { { -0.5, -0.5 }, { image.Size.X - 0.5, image.Size.Y - 0.5 } };
            m.Write2RawDouble(clip[0]);
            m.Write2RawDouble(clip[1]);
        }

        Handle reactor = image.DefinitionReactorHandle;
        if (!Written(reactor))
        {
            auto it = m_imageReactors.find(image.ObjectHandle);
            reactor = it != m_imageReactors.end() ? it->second : kNullHandle;
        }
        H(DwgRef::HardPointer, image.DefinitionHandle);
        H(DwgRef::HardOwnership, reactor);
    }
}

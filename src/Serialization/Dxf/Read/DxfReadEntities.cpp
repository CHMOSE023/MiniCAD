// DXF 读取：BLOCKS 段、ENTITIES 段与各实体的专门处理
#include "Database/DxfAssign.hpp"
#include "Dxf/Read/DxfReaderImpl.h"
#include <cmath>

namespace MiniDWG::DxfRead
{
    namespace
    {
        constexpr double kDegToRad = kPi / 180.0;

        bool HasMarker(const Record& r, std::string_view marker)
        {
            for (const DxfGroup& g : r.Groups)
            {
                if (g.Code == 100 && g.Value.AsString() == marker)
                    return true;
            }
            return false;
        }

        std::int64_t IntOf(const Record& r, int code, std::int64_t fallback = 0)
        {
            for (const DxfGroup& g : r.Groups)
            {
                if (g.Code == code)
                    return g.Value.AsInt();
            }
            return fallback;
        }

        bool IsKind(const Record* r, std::string_view type)
        {
            return r != nullptr && r->Type == type;
        }

        // ── Hatch ──

        // 读多段线边界（组码 72 有无凸度、73 闭合、93 顶点数，之后 10/20[/42]）
        std::unique_ptr<HatchBoundaryPathPolyline> ReadPolylineBoundary(const Record& r, std::size_t& i)
        {
            auto pl = std::make_unique<HatchBoundaryPathPolyline>();
            bool hasBulge = false;
            for (++i; i < r.Groups.size(); ++i)
            {
                const DxfGroup& g = r.Groups[i];
                if (g.Code == 72)
                    hasBulge = g.Value.AsBool();
                else if (g.Code == 73)
                    pl->IsClosed = g.Value.AsBool();
                else if (g.Code == 93)
                {
                    const std::int64_t n = g.Value.AsInt();
                    for (std::int64_t k = 0; k < n && i + 2 < r.Groups.size(); ++k)
                    {
                        XYZ v{ r.Groups[i + 1].Value.AsDouble(), r.Groups[i + 2].Value.AsDouble(), 0 };
                        i += 2;
                        if (hasBulge && i + 1 < r.Groups.size() && r.Groups[i + 1].Code == 42)
                            v.Z = r.Groups[++i].Value.AsDouble();
                        pl->Vertices.push_back(v);
                    }
                }
                else
                    break;
            }
            return pl;
        }

        // 读一条边（72 边类型开头），i 返回时指向边之后的第一个组码
        std::unique_ptr<HatchBoundaryPathEdge> ReadEdge(const Record& r, std::size_t& i)
        {
            const std::int64_t type = r.Groups[i].Value.AsInt();
            ++i;
            auto valueAt = [&](std::size_t k) { return r.Groups[k].Value.AsDouble(); };
            switch (type)
            {
            case 1:
            {
                auto line = std::make_unique<HatchBoundaryPathLine>();
                for (; i < r.Groups.size(); ++i)
                {
                    switch (r.Groups[i].Code)
                    {
                    case 10: line->Start.X = valueAt(i); break;
                    case 20: line->Start.Y = valueAt(i); break;
                    case 11: line->End.X = valueAt(i); break;
                    case 21: line->End.Y = valueAt(i); break;
                    default: return line;
                    }
                }
                return line;
            }
            case 2:
            {
                auto arc = std::make_unique<HatchBoundaryPathArc>();
                for (; i < r.Groups.size(); ++i)
                {
                    switch (r.Groups[i].Code)
                    {
                    case 10: arc->Center.X = valueAt(i); break;
                    case 20: arc->Center.Y = valueAt(i); break;
                    case 40: arc->Radius = valueAt(i); break;
                    case 50: arc->StartAngle = valueAt(i) * kDegToRad; break;
                    case 51: arc->EndAngle = valueAt(i) * kDegToRad; break;
                    case 73: arc->CounterClockWise = r.Groups[i].Value.AsBool(); break;
                    default: return arc;
                    }
                }
                return arc;
            }
            case 3:
            {
                auto ellipse = std::make_unique<HatchBoundaryPathEllipse>();
                for (; i < r.Groups.size(); ++i)
                {
                    switch (r.Groups[i].Code)
                    {
                    case 10: ellipse->Center.X = valueAt(i); break;
                    case 20: ellipse->Center.Y = valueAt(i); break;
                    case 11: ellipse->MajorAxisEndPoint.X = valueAt(i); break;
                    case 21: ellipse->MajorAxisEndPoint.Y = valueAt(i); break;
                    case 40: ellipse->RadiusRatio = valueAt(i); break;
                    case 50: ellipse->StartAngle = valueAt(i) * kDegToRad; break;
                    case 51: ellipse->EndAngle = valueAt(i) * kDegToRad; break;
                    case 73: ellipse->CounterClockWise = r.Groups[i].Value.AsBool(); break;
                    default: return ellipse;
                    }
                }
                return ellipse;
            }
            case 4:
            {
                auto spline = std::make_unique<HatchBoundaryPathSpline>();
                bool fitCountSeen = false;
                for (; i < r.Groups.size(); ++i)
                {
                    const DxfGroup& g = r.Groups[i];
                    switch (g.Code)
                    {
                    case 94: spline->Degree = static_cast<std::int32_t>(g.Value.AsInt()); break;
                    case 73: spline->IsRational = g.Value.AsBool(); break;
                    case 74: spline->IsPeriodic = g.Value.AsBool(); break;
                    case 95:
                    case 96: break;     // 节点数、控制点数
                    case 40: spline->Knots.push_back(g.Value.AsDouble()); break;
                    case 10: spline->ControlPoints.push_back(XYZ{ g.Value.AsDouble(), 0, 1 }); break;
                    case 20:
                        if (!spline->ControlPoints.empty())
                            spline->ControlPoints.back().Y = g.Value.AsDouble();
                        break;
                    case 42:
                        if (!spline->ControlPoints.empty())
                            spline->ControlPoints.back().Z = g.Value.AsDouble();     // 权重
                        break;
                    case 97:
                        // 样条边里的 97 是拟合点数；再次出现则是边界的源对象数，边结束
                        if (fitCountSeen)
                            return spline;
                        fitCountSeen = true;
                        break;
                    case 11: spline->FitPoints.push_back(XY{ g.Value.AsDouble(), 0 }); break;
                    case 21:
                        if (!spline->FitPoints.empty())
                            spline->FitPoints.back().Y = g.Value.AsDouble();
                        break;
                    case 12: spline->StartTangent.X = g.Value.AsDouble(); break;
                    case 22: spline->StartTangent.Y = g.Value.AsDouble(); break;
                    case 13: spline->EndTangent.X = g.Value.AsDouble(); break;
                    case 23: spline->EndTangent.Y = g.Value.AsDouble(); break;
                    default: return spline;
                    }
                }
                return spline;
            }
            default:
                return nullptr;
            }
        }

        // 读一个边界（92 标志开头），i 返回时指向边界之后的第一个组码
        HatchBoundaryPath ReadLoop(const Record& r, std::size_t& i)
        {
            HatchBoundaryPath path;
            path.Flags = static_cast<BoundaryPathFlags>(r.Groups[i].Value.AsInt());
            if (HasFlag(path.Flags, BoundaryPathFlags::Polyline))
            {
                path.Edges.push_back(ReadPolylineBoundary(r, i));
            }
            else
            {
                ++i;
                if (i < r.Groups.size() && r.Groups[i].Code == 93)
                {
                    const std::int64_t n = r.Groups[i].Value.AsInt();
                    ++i;
                    for (std::int64_t k = 0; k < n && i < r.Groups.size() && r.Groups[i].Code == 72; ++k)
                    {
                        if (auto edge = ReadEdge(r, i))
                            path.Edges.push_back(std::move(edge));
                    }
                }
            }
            // 97 源对象个数，之后是 330 源对象句柄
            for (; i < r.Groups.size(); ++i)
            {
                if (r.Groups[i].Code == 97)
                    continue;
                if (r.Groups[i].Code == 330)
                    path.Entities.push_back(r.Groups[i].Value.AsHandle());
                else
                    break;
            }
            return path;
        }

        // 图案线：53 角度、43/44 基点、45/46 偏移、79 虚线段数，之后 49 段长
        void ReadPatternLines(HatchPattern& pattern, std::int64_t count, const Record& r, std::size_t& i)
        {
            ++i;
            for (std::int64_t n = 0; n < count && i < r.Groups.size(); ++n)
            {
                HatchPatternLine line;
                bool seen[100] = {};
                for (; i < r.Groups.size(); ++i)
                {
                    const DxfGroup& g = r.Groups[i];
                    if (g.Code < 100 && seen[g.Code] && g.Code != 49)
                        break;      // 下一条图案线
                    if (g.Code < 100)
                        seen[g.Code] = true;
                    bool handled = true;
                    switch (g.Code)
                    {
                    case 53: line.Angle = g.Value.AsDouble() * kDegToRad; break;
                    case 43: line.BasePoint.X = g.Value.AsDouble(); break;
                    case 44: line.BasePoint.Y = g.Value.AsDouble(); break;
                    case 45: line.Offset.X = g.Value.AsDouble(); break;
                    case 46: line.Offset.Y = g.Value.AsDouble(); break;
                    case 79: break;
                    case 49: line.DashLengths.push_back(g.Value.AsDouble()); break;
                    default: handled = false; break;
                    }
                    if (!handled)
                        break;
                }
                pattern.Lines.push_back(std::move(line));
            }
            --i;    // 外层循环会 ++i
        }
    }

    // ── BLOCKS ─────────────────────────────────────────────────────

    void Reader::ReadBlocks()
    {
        Record r;
        while (const Record* next = m_records.Peek())
        {
            if (next->Type == "ENDSEC" || next->Type == "EOF")
            {
                m_records.Next(r);
                break;
            }
            if (next->Type != "BLOCK")
            {
                NotifyOnce("blocks:" + next->Type, NotificationType::Warning, "BLOCKS 段中出现意外的 " + next->Type);
                m_records.Next(r);
                continue;
            }

            m_records.Next(r);
            BlockDef def;
            def.Begin = AddItem(std::make_unique<Block>());
            ProcessGeneric(m_items[def.Begin], r);
            auto* block = static_cast<Block*>(m_items[def.Begin].Object.get());
            def.RecordHandle = block->OwnerHandle;
            // R12 用 $MODEL_SPACE / $PAPER_SPACE
            if (EqualsIgnoreCaseAscii(block->Name, "$MODEL_SPACE"))
                block->Name = "*Model_Space";
            else if (EqualsIgnoreCaseAscii(block->Name, "$PAPER_SPACE"))
                block->Name = "*Paper_Space";
            def.Name = block->Name;

            while (const Record* inner = m_records.Peek())
            {
                if (inner->Type == "ENDBLK")
                {
                    m_records.Next(r);
                    def.End = AddItem(std::make_unique<BlockEnd>());
                    ProcessGeneric(m_items[*def.End], r);
                    break;
                }
                if (inner->Type == "ENDSEC" || inner->Type == "EOF" || inner->Type == "BLOCK")
                    break;  // 缺少 ENDBLK
                if (auto entity = ReadEntity(m_records))
                    def.Entities.push_back(*entity);
            }
            m_blocks.push_back(std::move(def));
        }
    }

    // ── ENTITIES ───────────────────────────────────────────────────

    void Reader::ReadEntitiesSection()
    {
        Record r;
        while (const Record* next = m_records.Peek())
        {
            if (next->Type == "ENDSEC" || next->Type == "EOF")
            {
                m_records.Next(r);
                break;
            }
            if (auto entity = ReadEntity(m_records))
                m_entities.push_back(*entity);
        }
    }

    std::unique_ptr<CadObject> Reader::CreateEntity(const Record& r)
    {
        const std::string& type = r.Type;

        if (type == "DIMENSION" || type == "ARC_DIMENSION")
        {
            if (type == "ARC_DIMENSION" || HasMarker(r, "AcDbArcDimension")) return std::make_unique<DimensionArc>();
            if (HasMarker(r, "AcDbRotatedDimension")) return std::make_unique<DimensionLinear>();
            if (HasMarker(r, "AcDbAlignedDimension")) return std::make_unique<DimensionAligned>();
            if (HasMarker(r, "AcDb3PointAngularDimension")) return std::make_unique<DimensionAngular3Pt>();
            if (HasMarker(r, "AcDb2LineAngularDimension")) return std::make_unique<DimensionAngular2Line>();
            if (HasMarker(r, "AcDbDiametricDimension")) return std::make_unique<DimensionDiameter>();
            if (HasMarker(r, "AcDbRadialDimension")) return std::make_unique<DimensionRadius>();
            if (HasMarker(r, "AcDbOrdinateDimension")) return std::make_unique<DimensionOrdinate>();
            // R12：没有子类标记，按 70 的低 3 位判断
            switch (IntOf(r, 70) & 0x07)
            {
            case 0: return std::make_unique<DimensionLinear>();
            case 1: return std::make_unique<DimensionAligned>();
            case 2: return std::make_unique<DimensionAngular2Line>();
            case 3: return std::make_unique<DimensionDiameter>();
            case 4: return std::make_unique<DimensionRadius>();
            case 5: return std::make_unique<DimensionAngular3Pt>();
            case 6: return std::make_unique<DimensionOrdinate>();
            default: return nullptr;
            }
        }
        if (type == "POLYLINE")
        {
            if (HasMarker(r, "AcDb2dPolyline")) return std::make_unique<Polyline2D>();
            if (HasMarker(r, "AcDb3dPolyline")) return std::make_unique<Polyline3D>();
            if (HasMarker(r, "AcDbPolyFaceMesh")) return std::make_unique<PolyfaceMesh>();
            if (HasMarker(r, "AcDbPolygonMesh")) return std::make_unique<PolygonMesh>();
            const std::int64_t flags = IntOf(r, 70);
            if (flags & 64) return std::make_unique<PolyfaceMesh>();
            if (flags & 16) return std::make_unique<PolygonMesh>();
            if (flags & 8) return std::make_unique<Polyline3D>();
            return std::make_unique<Polyline2D>();
        }
        if (type == "VERTEX")
        {
            if (HasMarker(r, "AcDb2dVertex")) return std::make_unique<Vertex2D>();
            if (HasMarker(r, "AcDb3dPolylineVertex")) return std::make_unique<Vertex3D>();
            if (HasMarker(r, "AcDbPolyFaceMeshVertex")) return std::make_unique<VertexFaceMesh>();
            if (HasMarker(r, "AcDbFaceRecord")) return std::make_unique<VertexFaceRecord>();
            if (HasMarker(r, "AcDbPolygonMeshVertex")) return std::make_unique<PolygonMeshVertex>();
            // 没有子类标记（R12）：按标志区分。128 多面网格（同时有 64 为顶点，否则为面），64 多边形网格
            const std::int64_t flags = IntOf(r, 70);
            if ((flags & 128) && (flags & 64)) return std::make_unique<VertexFaceMesh>();
            if (flags & 128) return std::make_unique<VertexFaceRecord>();
            if (flags & 64) return std::make_unique<PolygonMeshVertex>();
            if (flags & 32) return std::make_unique<Vertex3D>();
            return std::make_unique<Vertex2D>();
        }
        if (type == "TRACE")
            return std::make_unique<Solid>();

        auto it = m_classByDxfName.find(type);
        if (it == m_classByDxfName.end() || it->second == nullptr || it->second->Create == nullptr)
            return nullptr;
        auto object = it->second->Create();
        if (dynamic_cast<Entity*>(object.get()) == nullptr)
            return nullptr;     // 实体段里出现了非实体
        return object;
    }

    std::optional<std::size_t> Reader::ReadEntity(RecordStream& records)
    {
        Record r;
        if (!records.Next(r))
            return std::nullopt;

        auto object = CreateEntity(r);
        // 未建模的实体原样保留；多面网格等带子实体的 POLYLINE 与其顶点仍然跳过
        if (!object && r.Type != "POLYLINE" && r.Type != "VERTEX" && r.Type != "SEQEND" && r.Type != "ATTRIB"
            && r.Type != "INSERT" && !r.Type.empty())
        {
            std::unique_ptr<UnknownEntity> unknown;
            if (r.Type == "ACAD_TABLE")
                unknown = std::make_unique<TableEntity>();
            else
                unknown = std::make_unique<UnknownEntity>();
            UnknownEntity* entity = unknown.get();
            const std::size_t index = AddItem(std::move(unknown));
            entity->Raw.DxfName = r.Type;
            ReadUnknownGroups(m_items[index], r, entity->Raw, true);
            // 表格：AcDbBlockReference 子类中的块参照部分（块名在建库时换成句柄）
            if (auto* table = dynamic_cast<TableEntity*>(entity))
            {
                std::string subclass;
                for (const RawDxfGroup& g : entity->Raw.Dxf.Groups)
                {
                    if (g.Code == 100)
                    {
                        subclass = g.Value.AsString();
                        continue;
                    }
                    // AcDbTable 中的 343 是块记录句柄（比块名可靠：匿名块名可能重新编号）
                    if (subclass == "AcDbTable" && g.Code == 343)
                        table->BlockHandle = g.Value.AsHandle();
                    if (subclass != "AcDbBlockReference")
                        continue;
                    switch (g.Code)
                    {
                    case 2: table->BlockName = g.Value.AsString(); break;
                    case 10: table->InsertPoint.X = g.Value.AsDouble(); break;
                    case 20: table->InsertPoint.Y = g.Value.AsDouble(); break;
                    case 30: table->InsertPoint.Z = g.Value.AsDouble(); break;
                    case 41: table->XScale = g.Value.AsDouble(); break;
                    case 42: table->YScale = g.Value.AsDouble(); break;
                    case 43: table->ZScale = g.Value.AsDouble(); break;
                    case 50: table->Rotation = g.Value.AsDouble() * kDegToRad; break;
                    case 210: table->Normal.X = g.Value.AsDouble(); break;
                    case 220: table->Normal.Y = g.Value.AsDouble(); break;
                    case 230: table->Normal.Z = g.Value.AsDouble(); break;
                    default: break;
                    }
                }
            }
            ++m_preserved[r.Type];
            return index;
        }
        if (!object)
        {
            ++m_skipped[r.Type];
            NotifyOnce("entity:" + r.Type, NotificationType::Info, "跳过未支持的实体 " + r.Type);
            // 跳过它的子实体
            if (r.Type == "POLYLINE" || r.Type == "INSERT")
            {
                Record child;
                while (IsKind(records.Peek(), "VERTEX") || IsKind(records.Peek(), "ATTRIB"))
                    records.Next(child);
                if (IsKind(records.Peek(), "SEQEND"))
                    records.Next(child);
            }
            return std::nullopt;
        }

        ApplyOmittedDefaults(*object);
        const std::size_t index = AddItem(std::move(object));
        ReadEntityGroups(m_items[index], r);

        // POLYLINE 后跟 VERTEX …… SEQEND；INSERT 后跟 ATTRIB …… SEQEND
        const bool isPolyline = dynamic_cast<Polyline*>(m_items[index].Object.get()) != nullptr;
        const bool isInsert = dynamic_cast<Insert*>(m_items[index].Object.get()) != nullptr;
        if (isPolyline || isInsert)
        {
            const std::string_view childType = isPolyline ? "VERTEX" : "ATTRIB";
            bool hasChildren = false;
            while (IsKind(records.Peek(), childType))
            {
                hasChildren = true;
                if (auto child = ReadEntity(records))
                {
                    m_items[*child].IsChild = true;
                    m_items[index].Children.push_back(*child);
                }
            }
            if (IsKind(records.Peek(), "SEQEND") && (hasChildren || isPolyline || IntOf(r, 66) != 0))
            {
                Record seqendRecord;
                records.Next(seqendRecord);
                const std::size_t seqend = AddItem(std::make_unique<Seqend>());
                ProcessGeneric(m_items[seqend], seqendRecord);
                m_items[seqend].IsChild = true;
                m_items[index].Seqend = seqend;
            }
        }
        return index;
    }

    void Reader::ReadEntityGroups(ReadItem& item, const Record& r)
    {
        CadObject* obj = item.Object.get();

        if (auto* pl = dynamic_cast<LwPolyline*>(obj))
        {
            Process(item, r, [&](const DxfGroup& g, std::size_t&, const ParseState&) -> bool {
                LwPolylineVertex* last = pl->Vertices.empty() ? nullptr : &pl->Vertices.back();
                switch (g.Code)
                {
                case 10:
                    pl->Vertices.emplace_back();
                    pl->Vertices.back().Location.X = g.Value.AsDouble();
                    return true;
                case 20: if (last) last->Location.Y = g.Value.AsDouble(); return true;
                case 40: if (last) last->StartWidth = g.Value.AsDouble(); return true;
                case 41: if (last) last->EndWidth = g.Value.AsDouble(); return true;
                case 42: if (last) last->Bulge = g.Value.AsDouble(); return true;
                case 50: if (last) last->CurveTangent = g.Value.AsDouble(); return true;
                case 91: if (last) last->Id = static_cast<std::int32_t>(g.Value.AsInt()); return true;
                case 90:
                case 66: return true;
                default: return false;
                }
            });
            return;
        }

        if (auto* mtext = dynamic_cast<MText*>(obj))
        {
            Process(item, r, [&](const DxfGroup& g, std::size_t& i, const ParseState&) -> bool {
                switch (g.Code)
                {
                case 1:
                case 3:
                    mtext->Value += g.Value.AsString();     // 3 是前面的分块，1 是最后一块
                    return true;
                case 50:
                {
                    const double angle = g.Value.AsDouble() * kDegToRad;
                    mtext->AlignmentPoint = XYZ{ std::cos(angle), std::sin(angle), 0.0 };
                    return true;
                }
                case 101:
                    // 分栏数据：到记录末尾（或扩展数据）为止
                    for (++i; i < r.Groups.size() && r.Groups[i].Code != 1001; ++i)
                    {
                        const DxfGroup& c = r.Groups[i];
                        auto& col = mtext->ColumnData;
                        switch (c.Code)
                        {
                        case 71: col.ColumnType = static_cast<ColumnType>(c.Value.AsInt()); break;
                        case 72: col.ColumnCount = static_cast<std::int32_t>(c.Value.AsInt()); break;
                        case 44: col.Width = c.Value.AsDouble(); break;
                        case 45: col.Gutter = c.Value.AsDouble(); break;
                        case 46: col.Heights.push_back(c.Value.AsDouble()); break;
                        case 73: col.AutoHeight = c.Value.AsBool(); break;
                        case 74: col.FlowReversed = c.Value.AsBool(); break;
                        default: break;
                        }
                    }
                    --i;
                    return true;
                default:
                    return false;
                }
            });
            return;
        }

        if (dynamic_cast<AttributeBase*>(obj) != nullptr)
        {
            Process(item, r, [&](const DxfGroup& g, std::size_t& i, const ParseState&) -> bool {
                switch (g.Code)
                {
                case 44:
                case 46:
                    return true;
                case 101:
                    // 多行属性内嵌的 MTEXT：尚未建模，跳过
                    for (++i; i < r.Groups.size() && r.Groups[i].Code != 1001; ++i)
                    {
                    }
                    --i;
                    return true;
                default:
                    return false;
                }
            });
            return;
        }

        if (auto* spline = dynamic_cast<Spline*>(obj))
        {
            Process(item, r, [&](const DxfGroup& g, std::size_t&, const ParseState&) -> bool {
                switch (g.Code)
                {
                case 10: spline->ControlPoints.push_back(XYZ{ g.Value.AsDouble(), 0, 0 }); return true;
                case 20: if (!spline->ControlPoints.empty()) spline->ControlPoints.back().Y = g.Value.AsDouble(); return true;
                case 30: if (!spline->ControlPoints.empty()) spline->ControlPoints.back().Z = g.Value.AsDouble(); return true;
                case 11: spline->FitPoints.push_back(XYZ{ g.Value.AsDouble(), 0, 0 }); return true;
                case 21: if (!spline->FitPoints.empty()) spline->FitPoints.back().Y = g.Value.AsDouble(); return true;
                case 31: if (!spline->FitPoints.empty()) spline->FitPoints.back().Z = g.Value.AsDouble(); return true;
                case 40: spline->Knots.push_back(g.Value.AsDouble()); return true;
                case 41: spline->Weights.push_back(g.Value.AsDouble()); return true;
                case 72:
                case 73:
                case 74: return true;
                default: return false;
                }
            });
            return;
        }

        if (auto* leader = dynamic_cast<Leader*>(obj))
        {
            Process(item, r, [&](const DxfGroup& g, std::size_t&, const ParseState&) -> bool {
                switch (g.Code)
                {
                case 10: leader->Vertices.push_back(XYZ{ g.Value.AsDouble(), 0, 0 }); return true;
                case 20: if (!leader->Vertices.empty()) leader->Vertices.back().Y = g.Value.AsDouble(); return true;
                case 30: if (!leader->Vertices.empty()) leader->Vertices.back().Z = g.Value.AsDouble(); return true;
                case 75:
                case 76: return true;
                default: return false;
                }
            });
            return;
        }

        if (dynamic_cast<Hatch*>(obj) != nullptr)
        {
            ReadHatch(item, r);
            return;
        }

        if (dynamic_cast<MLine*>(obj) != nullptr)
        {
            ReadMLine(item, r);
            return;
        }

        if (dynamic_cast<MultiLeader*>(obj) != nullptr)
        {
            ReadMultiLeader(item, r);
            return;
        }

        if (auto* wipeout = dynamic_cast<CadWipeoutBase*>(obj))
        {
            Process(item, r, [&](const DxfGroup& g, std::size_t& i, const ParseState&) -> bool {
                switch (g.Code)
                {
                case 91:
                {
                    const std::int64_t n = g.Value.AsInt();
                    for (std::int64_t k = 0; k < n && i + 2 < r.Groups.size(); ++k)
                    {
                        wipeout->ClipBoundaryVertices.push_back(
                            XY{ r.Groups[i + 1].Value.AsDouble(), r.Groups[i + 2].Value.AsDouble() });
                        i += 2;
                    }
                    return true;
                }
                case 71:    // 裁剪类型：由边界点数决定
                    return true;
                default:
                    return false;
                }
            });
            return;
        }

        if (dynamic_cast<Polyline*>(obj) != nullptr)
        {
            // 10/20 是固定为 0 的占位点（30 为标高）
            Process(item, r, [](const DxfGroup& g, std::size_t&, const ParseState&) {
                return g.Code == 10 || g.Code == 20 || g.Code == 66;
            });
            return;
        }

        if (dynamic_cast<Dimension*>(obj) != nullptr)
        {
            Process(item, r, [](const DxfGroup& g, std::size_t&, const ParseState&) {
                return g.Code == 73 || g.Code == 90 || g.Code == 361;
            });
            return;
        }

        if (dynamic_cast<Shape*>(obj) != nullptr)
        {
            Process(item, r, [](const DxfGroup& g, std::size_t&, const ParseState&) { return g.Code == 2; });
            return;
        }

        ProcessGeneric(item, r);
    }

    void Reader::ReadHatch(ReadItem& item, const Record& r)
    {
        auto* hatch = static_cast<Hatch*>(item.Object.get());
        bool seedsStarted = false;
        GradientColor* lastGradient = nullptr;

        Process(item, r, [&](const DxfGroup& g, std::size_t& i, const ParseState&) -> bool {
            switch (g.Code)
            {
            case 2:
                hatch->Pattern.Name = g.Value.AsString();
                return true;
            case 10:
                // 98 之前的 10/20/30 是标高点，之后是种子点
                if (seedsStarted)
                    hatch->SeedPoints.push_back(XY{ g.Value.AsDouble(), 0 });
                return true;
            case 20:
                if (seedsStarted && !hatch->SeedPoints.empty())
                    hatch->SeedPoints.back().Y = g.Value.AsDouble();
                return true;
            case 30:
                hatch->Elevation = g.Value.AsDouble();
                return true;
            case 98:
                seedsStarted = true;
                return true;
            case 91:
            {
                const std::int64_t n = g.Value.AsInt();
                ++i;
                for (std::int64_t k = 0; k < n && i < r.Groups.size() && r.Groups[i].Code == 92; ++k)
                    hatch->Paths.push_back(ReadLoop(r, i));
                --i;
                return true;
            }
            case 78:
                ReadPatternLines(hatch->Pattern, g.Value.AsInt(), r, i);
                return true;
            case 450: hatch->GradientColor.Enabled = g.Value.AsBool(); return true;
            case 451: hatch->GradientColor.Reserved = static_cast<std::int32_t>(g.Value.AsInt()); return true;
            case 452: hatch->GradientColor.IsSingleColorGradient = g.Value.AsBool(); return true;
            case 453: return true;
            case 460: hatch->GradientColor.Angle = g.Value.AsDouble(); return true;
            case 461: hatch->GradientColor.Shift = g.Value.AsDouble(); return true;
            case 462: hatch->GradientColor.ColorTint = g.Value.AsDouble(); return true;
            case 463:
                hatch->GradientColor.Colors.emplace_back();
                lastGradient = &hatch->GradientColor.Colors.back();
                lastGradient->Value = g.Value.AsDouble();
                return true;
            case 63:
                if (lastGradient != nullptr)
                {
                    Assign(lastGradient->Color, g.Code, g.Value);
                    return true;
                }
                return false;
            case 421:
                if (lastGradient != nullptr)
                {
                    Assign(lastGradient->Color, g.Code, g.Value);
                    return true;
                }
                return false;
            case 470: hatch->GradientColor.Name = g.Value.AsString(); return true;
            default:
                return false;
            }
        });
    }

    void Reader::ReadMLine(ReadItem& item, const Record& r)
    {
        auto* mline = static_cast<MLine*>(item.Object.get());
        MLineVertex* vertex = nullptr;

        Process(item, r, [&](const DxfGroup& g, std::size_t& i, const ParseState&) -> bool {
            switch (g.Code)
            {
            case 2:     // 样式名：R13 起以 340 句柄为准
            case 72:
            case 73:
                return true;
            case 11:
                mline->Vertices.emplace_back();
                vertex = &mline->Vertices.back();
                vertex->Position.X = g.Value.AsDouble();
                return true;
            case 21: if (vertex) vertex->Position.Y = g.Value.AsDouble(); return true;
            case 31: if (vertex) vertex->Position.Z = g.Value.AsDouble(); return true;
            case 12: if (vertex) vertex->Direction.X = g.Value.AsDouble(); return true;
            case 22: if (vertex) vertex->Direction.Y = g.Value.AsDouble(); return true;
            case 32: if (vertex) vertex->Direction.Z = g.Value.AsDouble(); return true;
            case 13: if (vertex) vertex->Miter.X = g.Value.AsDouble(); return true;
            case 23: if (vertex) vertex->Miter.Y = g.Value.AsDouble(); return true;
            case 33: if (vertex) vertex->Miter.Z = g.Value.AsDouble(); return true;
            case 74:
            {
                if (vertex == nullptr)
                    return true;
                // 每个元素：74 参数个数 + 41 参数，75 填充参数个数 + 42 参数
                MLineVertexSegment segment;
                const std::int64_t n = g.Value.AsInt();
                for (std::int64_t k = 0; k < n && i + 1 < r.Groups.size() && r.Groups[i + 1].Code == 41; ++k)
                    segment.Parameters.push_back(r.Groups[++i].Value.AsDouble());
                if (i + 1 < r.Groups.size() && r.Groups[i + 1].Code == 75)
                {
                    const std::int64_t m = r.Groups[++i].Value.AsInt();
                    for (std::int64_t k = 0; k < m && i + 1 < r.Groups.size() && r.Groups[i + 1].Code == 42; ++k)
                        segment.AreaFillParameters.push_back(r.Groups[++i].Value.AsDouble());
                }
                vertex->Segments.push_back(std::move(segment));
                return true;
            }
            default:
                return false;
            }
        });
    }
}

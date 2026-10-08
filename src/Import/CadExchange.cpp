#include "Import/CadExchange.h"

#include "Core/Log.h"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Scene/Scene.h"

#include "Database/CadDatabase.h"
#include "Database/CadFileFormat.h"
#include "Dwg/Read/DwgReader.h"
#include "Dwg/Write/DwgWriter.h"
#include "Dxf/Read/DxfReader.h"
#include "Dxf/Write/DxfWriter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>

namespace Dwg = ::MiniDWG;

namespace MiniCAD
{
    namespace
    {
        constexpr double kPi    = 3.14159265358979323846;
        constexpr double kTwoPi = 2.0 * kPi;

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        bool EqualsNoCase(const std::string& a, const char* b) { return Lower(a) == Lower(b); }

        Math::Point3 ToPoint(const Dwg::XYZ& p) { return { p.X, p.Y, p.Z }; }
        Dwg::XYZ     ToXYZ(const Math::Point3& p) { return { p.x, p.y, p.z }; }

        bool IsDefaultNormal(const Dwg::XYZ& n) { return std::abs(n.X) < 1e-12 && std::abs(n.Y) < 1e-12 && n.Z > 0.0; }

        Math::Color4 RgbToColor4(std::uint32_t rgb)
        {
            return { ((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0, 1.0 };
        }

        // 图层颜色：索引色取 ACI 色板，真彩色直接取值；索引 <= 0 或 256 视为白色
        Math::Color4 LayerColorToColor4(const Dwg::Color& c)
        {
            if (c.IsTrueColor())
                return RgbToColor4(static_cast<std::uint32_t>(c.TrueColor()));
            int idx = std::abs(static_cast<int>(c.Index()));
            if (idx < 1 || idx > 255)
                return Math::Color4::White();
            return RgbToColor4(Dwg::Color::IndexRgb(static_cast<std::int16_t>(idx)));
        }

        EntityColor ToEntityColor(const Dwg::Color& c)
        {
            if (c.IsTrueColor())
            {
                const Math::Color4 col = RgbToColor4(static_cast<std::uint32_t>(c.TrueColor()));
                return EntityColor::FromRgb(col.r, col.g, col.b);
            }
            if (c.IsByBlock())
                return EntityColor::ByBlock();
            const int idx = c.Index();
            if (idx >= 1 && idx <= 255)
            {
                EntityColor e = EntityColor::FromAci(static_cast<std::uint16_t>(idx));
                e.Rgba = RgbToColor4(Dwg::Color::IndexRgb(static_cast<std::int16_t>(idx)));
                return e;
            }
            return EntityColor::ByLayer();
        }

        std::uint8_t ToByte(double v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0), 0L, 255L)); }

        Dwg::Color FromEntityColor(const EntityColor& c)
        {
            switch (c.Method)
            {
            case ColorMethod::ByBlock: return Dwg::Color::ByBlock();
            case ColorMethod::ByAci:   return Dwg::Color(static_cast<std::int16_t>(c.Aci));
            case ColorMethod::ByRgb:   return Dwg::Color::FromRgb(ToByte(c.Rgba.r), ToByte(c.Rgba.g), ToByte(c.Rgba.b));
            default:                   return Dwg::Color::ByLayer();
            }
        }

        Dwg::Color FromColor4(const Math::Color4& c)
        {
            return Dwg::Color::FromRgb(ToByte(c.r), ToByte(c.g), ToByte(c.b));
        }

        Dwg::NotificationHandler MakeNotify(std::string* firstError)
        {
            return [firstError](Dwg::NotificationType type, std::string_view msg)
            {
                const std::string text(msg);
                if (type == Dwg::NotificationType::Error)
                {
                    LOG_ERROR("MiniDWG: %s", text.c_str());
                    if (firstError && firstError->empty())
                        *firstError = text;
                }
                else if (type == Dwg::NotificationType::Warning)
                    LOG_WARN("MiniDWG: %s", text.c_str());
            };
        }

        // ════════════════════════════════════════════════════════════════
        //  导入：CadDatabase → Scene
        // ════════════════════════════════════════════════════════════════
        class Importer
        {
        public:
            Importer(const Dwg::CadDatabase& db, Scene& scene) : m_db(db), m_scene(scene) {}

            void Run()
            {
                ImportLineTypes();
                ImportLayers();
                ImportTextStyles();
                ImportBlocks();
                ImportModelSpace();
                m_scene.ResolveAllInserts();
                m_scene.MarkDirty();

                for (const auto& [name, count] : m_skipped)
                    LOG_WARN("DWG/DXF 导入：不支持的实体 %s × %d 已跳过", name.c_str(), count);
            }

        private:
            // ── 表 ──────────────────────────────────────────────────────
            template <class T, class Fn>
            void ForEachEntry(const Dwg::CadTable* table, Fn&& fn)
            {
                if (table == nullptr)
                    return;
                for (Dwg::Handle h : table->Entries)
                    if (const T* e = m_db.FindAs<T>(h))
                        fn(*e);
            }

            void ImportLineTypes()
            {
                LineTypeTable& table = m_scene.GetLineTypeTable();
                ForEachEntry<Dwg::LineType>(m_db.LineTypes(), [&](const Dwg::LineType& lt)
                {
                    LineTypeID id;
                    if (EqualsNoCase(lt.Name, "ByLayer"))
                        id = LineTypeTable::ByLayerID;
                    else if (EqualsNoCase(lt.Name, "ByBlock"))
                        id = LineTypeTable::ByBlockID;
                    else if (EqualsNoCase(lt.Name, "Continuous"))
                        id = LineTypeTable::ContinuousID;
                    else
                    {
                        LineTypeRecord rec;
                        rec.Name        = lt.Name;
                        rec.Description = lt.Description;
                        for (const auto& seg : lt.Segments)
                        {
                            if (seg.Flags != Dwg::LineTypeShapeFlags::None)
                                continue;   // 含文字 / 形的复杂线型：只保留纯线段部分
                            rec.Pattern.push_back(seg.Length);
                            rec.PatternLength += std::abs(seg.Length);
                        }
                        id = table.Add(std::move(rec));
                    }
                    m_lineTypes[lt.ObjectHandle] = id;
                });
            }

            LineTypeID LookupLineType(Dwg::Handle h) const
            {
                auto it = m_lineTypes.find(h);
                return it == m_lineTypes.end() ? LineTypeTable::ByLayerID : it->second;
            }

            void ImportLayers()
            {
                LayerManager& layers = m_scene.GetLayerManager();
                ForEachEntry<Dwg::Layer>(m_db.Layers(), [&](const Dwg::Layer& src)
                {
                    LayerID id;
                    if (src.Name == "0")
                    {
                        id = Layer::DefaultLayerID;
                        layers.GetLayer(id)->SetName("0");
                    }
                    else
                        id = layers.AddLayer(src.Name);

                    if (Layer* dst = layers.GetLayer(id))
                    {
                        dst->SetColor(LayerColorToColor4(src.Color));
                        dst->SetVisible(src.IsOn && src.Color.Index() >= 0);
                        LineTypeID lt = LookupLineType(src.LineTypeHandle);
                        if (lt == LineTypeTable::ByLayerID || lt == LineTypeTable::ByBlockID)
                            lt = LineTypeTable::ContinuousID;   // 图层线型不允许 ByLayer / ByBlock
                        dst->SetLineType(lt);
                        dst->SetLineweight(LineweightFromRaw(static_cast<std::int16_t>(src.LineWeight)));
                    }
                    m_layers[src.ObjectHandle] = id;
                });
            }

            void ImportTextStyles()
            {
                TextStyleTable& table = m_scene.GetTextStyleTable();
                ForEachEntry<Dwg::TextStyle>(m_db.TextStyles(), [&](const Dwg::TextStyle& st)
                {
                    TextStyleID id = table.FindByName(st.Name);
                    if (id == TextStyleTable::InvalidID)
                    {
                        auto withExt = [](std::string f)
                        {
                            if (!f.empty() && f.find('.') == std::string::npos)
                                f += ".shx";   // DWG 里的 SHX 字体名常不带扩展名
                            return f;
                        };
                        TextStyleRecord rec;
                        rec.Name        = st.Name;
                        rec.FontFile    = withExt(st.Filename);
                        rec.BigFontFile = st.BigFontFilename.empty() ? std::string() : withExt(st.BigFontFilename);
                        rec.Height      = st.Height;
                        rec.WidthFactor = st.Width > 0.0 ? st.Width : 1.0;
                        rec.ObliqueDeg  = st.ObliqueAngle * 180.0 / kPi;
                        id = table.Add(std::move(rec));
                    }
                    m_styles[st.ObjectHandle] = (id == TextStyleTable::InvalidID) ? TextStyleTable::StandardID : id;
                });
            }

            TextStyleID LookupStyle(Dwg::Handle h) const
            {
                auto it = m_styles.find(h);
                return it == m_styles.end() ? TextStyleTable::StandardID : it->second;
            }

            // ── 块 ──────────────────────────────────────────────────────
            static bool IsLayoutBlock(const std::string& name)
            {
                const std::string n = Lower(name);
                return n.rfind("*model_space", 0) == 0 || n.rfind("*paper_space", 0) == 0;
            }

            void ImportBlocks()
            {
                BlockTable& table = m_scene.GetBlockTable();

                // 先建全部块壳，嵌套插入才能找到目标
                ForEachEntry<Dwg::BlockRecord>(m_db.BlockRecords(), [&](const Dwg::BlockRecord& rec)
                {
                    if (IsLayoutBlock(rec.Name))
                        return;
                    Math::Point3 base{ 0, 0, 0 };
                    if (const auto* b = m_db.FindAs<Dwg::Block>(rec.BlockEntityHandle))
                        base = ToPoint(b->BasePoint);
                    const BlockID id = table.Create(rec.Name, base);
                    m_blocks[rec.ObjectHandle] = id;
                    if (BlockEntity* blk = table.Find(id))
                        if (const auto* b = m_db.FindAs<Dwg::Block>(rec.BlockEntityHandle))
                            blk->SetDescription(b->Comments);
                });

                ForEachEntry<Dwg::BlockRecord>(m_db.BlockRecords(), [&](const Dwg::BlockRecord& rec)
                {
                    auto it = m_blocks.find(rec.ObjectHandle);
                    if (it == m_blocks.end())
                        return;
                    BlockEntity* blk = table.Find(it->second);
                    if (blk == nullptr || !blk->IsEmpty())
                        return;
                    for (Dwg::Handle h : rec.Entities)
                        if (const auto* e = m_db.FindAs<Dwg::Entity>(h))
                            if (auto conv = Convert(*e))
                            {
                                if (conv->IsKindOf<InsertEntity>())
                                    m_scene.ResolveInsert(static_cast<InsertEntity&>(*conv));
                                blk->AddEntity(std::move(conv));
                            }
                });
            }

            void ImportModelSpace()
            {
                const Dwg::BlockRecord* model = m_db.ModelSpace();
                if (model == nullptr)
                    return;
                for (Dwg::Handle h : model->Entities)
                    if (const auto* e = m_db.FindAs<Dwg::Entity>(h))
                        if (auto conv = Convert(*e))
                            m_scene.AddEntity(std::move(conv));
            }

            // ── 实体 ────────────────────────────────────────────────────
            EntityAttr MakeAttr(const Dwg::Entity& src) const
            {
                EntityAttr a;
                a.Color = ToEntityColor(src.Color);
                if (auto it = m_layers.find(src.LayerHandle); it != m_layers.end())
                    a.LayerId = it->second;
                a.LineType      = LookupLineType(src.LineTypeHandle);
                a.LinetypeScale = src.LineTypeScale;
                a.Lineweight    = LineweightFromRaw(static_cast<std::int16_t>(src.LineWeight));
                a.Visible       = !src.IsInvisible;
                return a;
            }

            template <class T, class... Args>
            std::unique_ptr<Entity> Make(const Dwg::Entity& src, const Dwg::XYZ* normal, Args&&... args)
            {
                auto e = std::make_unique<T>(m_scene.NextObjectID(), std::forward<Args>(args)...);
                e->SetAttr(MakeAttr(src));
                if (normal && !IsDefaultNormal(*normal))
                    e->SetExtrusion({ normal->X, normal->Y, normal->Z });
                return e;
            }

            std::unique_ptr<Entity> Convert(const Dwg::Entity& src)
            {
                if (auto* e = dynamic_cast<const Dwg::Line*>(&src))
                    return Make<LineEntity>(src, &e->Normal, ToPoint(e->StartPoint), ToPoint(e->EndPoint));

                if (auto* e = dynamic_cast<const Dwg::Arc*>(&src))   // Arc 派生自 Circle，须先判断
                    return Make<ArcEntity>(src, &e->Normal, ToPoint(e->Center), e->Radius, e->StartAngle, e->EndAngle);

                if (auto* e = dynamic_cast<const Dwg::Circle*>(&src))
                    return Make<CircleEntity>(src, &e->Normal, ToPoint(e->Center), e->Radius);

                if (auto* e = dynamic_cast<const Dwg::Ellipse*>(&src))
                {
                    const double mx = e->MajorAxisEndPoint.X, my = e->MajorAxisEndPoint.Y;
                    const double a  = std::hypot(mx, my);
                    if (a < 1e-12)
                        return nullptr;
                    return Make<EllipseEntity>(src, nullptr,
                        Ellipse(ToPoint(e->Center), a, a * e->RadiusRatio, std::atan2(my, mx),
                                e->StartParameter, e->EndParameter));
                }

                if (auto* e = dynamic_cast<const Dwg::Point*>(&src))
                    return Make<PointEntity>(src, &e->Normal, ToPoint(e->Location));

                if (auto* e = dynamic_cast<const Dwg::LwPolyline*>(&src))
                    return ConvertLwPolyline(*e);

                if (auto* e = dynamic_cast<const Dwg::Polyline*>(&src))
                    return ConvertOldPolyline(*e);

                if (dynamic_cast<const Dwg::AttributeBase*>(&src))
                    return Skip(src);   // 属性定义 / 属性：当前场景没有对应的实体

                if (auto* e = dynamic_cast<const Dwg::TextEntity*>(&src))
                {
                    const bool aligned = e->HorizontalAlignment != Dwg::TextHorizontalAlignment::Left
                                      || e->VerticalAlignment != Dwg::TextVerticalAlignmentType::Baseline;
                    return Make<TextEntity>(src, &e->Normal, ToPoint(aligned ? e->AlignmentPoint : e->InsertPoint),
                        e->Value, static_cast<float>(e->Height), static_cast<float>(e->Rotation), LookupStyle(e->StyleHandle));
                }

                if (auto* e = dynamic_cast<const Dwg::MText*>(&src))
                {
                    double rot = 0.0;
                    if (std::hypot(e->AlignmentPoint.X, e->AlignmentPoint.Y) > 1e-12)
                        rot = std::atan2(e->AlignmentPoint.Y, e->AlignmentPoint.X);
                    auto ent = Make<MTextEntity>(src, &e->Normal, LookupStyle(e->StyleHandle), e->Value,
                                                 ToPoint(e->InsertPoint), e->Height, rot, e->RectangleWidth);
                    auto& mt = static_cast<MTextEntity&>(*ent);
                    const int att = static_cast<int>(e->AttachmentPoint);
                    if (att >= 1 && att <= 9)
                        mt.SetAttachment(static_cast<MTextAttachment>(att));
                    return ent;
                }

                if (auto* e = dynamic_cast<const Dwg::Insert*>(&src))
                {
                    auto it = m_blocks.find(e->BlockHandle);
                    if (it == m_blocks.end())
                        return Skip(src);
                    const BlockEntity* blk = m_scene.GetBlockTable().Find(it->second);
                    auto ent = Make<InsertEntity>(src, &e->Normal, static_cast<InsertEntity::BlockID>(it->second),
                                                  blk ? blk->GetName() : std::string(), ToPoint(e->InsertPoint));
                    auto& ins = static_cast<InsertEntity&>(*ent);
                    ins.SetScale({ e->XScale, e->YScale, e->ZScale });
                    ins.SetRotation(e->Rotation);
                    if (e->ColumnCount > 1 || e->RowCount > 1)
                        ins.SetArray(e->ColumnCount, e->RowCount, e->ColumnSpacing, e->RowSpacing);
                    return ent;
                }

                // 标注：用它的匿名块（*D…）还原外观，不再是可编辑的标注
                if (auto* e = dynamic_cast<const Dwg::Dimension*>(&src))
                {
                    auto it = m_blocks.find(e->BlockHandle);
                    if (it == m_blocks.end())
                        return Skip(src);
                    const BlockEntity* blk = m_scene.GetBlockTable().Find(it->second);
                    return Make<InsertEntity>(src, nullptr, static_cast<InsertEntity::BlockID>(it->second),
                                              blk ? blk->GetName() : std::string(), Math::Point3{ 0, 0, 0 });
                }

                return Skip(src);
            }

            std::unique_ptr<Entity> Skip(const Dwg::Entity& src)
            {
                ++m_skipped[std::string(src.GetDxfName())];
                return nullptr;
            }

            std::unique_ptr<Entity> ConvertLwPolyline(const Dwg::LwPolyline& e)
            {
                if (e.Vertices.size() < 2)
                    return nullptr;
                std::vector<Math::Point3> pts;
                std::vector<double>       bulges;
                for (const auto& v : e.Vertices)
                {
                    pts.push_back({ v.Location.X, v.Location.Y, e.Elevation });
                    bulges.push_back(v.Bulge);
                }
                if (Any(e.Flags & Dwg::LwPolylineFlags::Closed))
                    pts.push_back(pts.front());   // 闭合：末点回到起点，最后一个 bulge 属于闭合段
                else
                    bulges.pop_back();
                auto ent = Make<PolylineEntity>(e, &e.Normal, std::move(pts), std::move(bulges));
                static_cast<PolylineEntity&>(*ent).SetWidth(e.ConstantWidth);
                return ent;
            }

            // 旧式 POLYLINE（R12 的 DXF）：二维 / 三维折线；网格不支持
            std::unique_ptr<Entity> ConvertOldPolyline(const Dwg::Polyline& e)
            {
                if (Any(e.Flags & (Dwg::PolylineFlags::PolygonMesh | Dwg::PolylineFlags::PolyfaceMesh)))
                    return Skip(e);

                std::vector<Math::Point3> pts;
                std::vector<double>       bulges;
                for (Dwg::Handle h : e.Vertices)
                {
                    const auto* v = m_db.FindAs<Dwg::Vertex>(h);
                    if (v == nullptr || Any(v->Flags & Dwg::VertexFlags::SplineFrameControlPoint))   // 样条拟合的控制点不画
                        continue;
                    pts.push_back({ v->Location.X, v->Location.Y, v->Location.Z });
                    bulges.push_back(v->Bulge);
                }
                if (pts.size() < 2)
                    return nullptr;
                if (Any(e.Flags & Dwg::PolylineFlags::ClosedPolylineOrClosedPolygonMeshInM))
                    pts.push_back(pts.front());
                else
                    bulges.pop_back();
                auto ent = Make<PolylineEntity>(e, &e.Normal, std::move(pts), std::move(bulges));
                static_cast<PolylineEntity&>(*ent).SetWidth(e.StartWidth);
                return ent;
            }

            template <class E>
            static bool Any(E flags) { return static_cast<std::int64_t>(flags) != 0; }

        private:
            const Dwg::CadDatabase& m_db;
            Scene&                  m_scene;

            std::map<Dwg::Handle, LayerID>     m_layers;
            std::map<Dwg::Handle, LineTypeID>  m_lineTypes;
            std::map<Dwg::Handle, TextStyleID> m_styles;
            std::map<Dwg::Handle, BlockID>     m_blocks;
            std::map<std::string, int>         m_skipped;
        };

        // ════════════════════════════════════════════════════════════════
        //  导出：Scene → CadDatabase
        // ════════════════════════════════════════════════════════════════
        class Exporter
        {
        public:
            Exporter(const Scene& scene, Dwg::CadDatabase& db, Dwg::CadVersion version)
                : m_scene(scene), m_db(db), m_version(version) {}

            void Run()
            {
                m_db.SetVersion(m_version);
                m_db.CreateDefaults();

                // 头变量里的 UCS 轴默认是零向量，AutoCAD 打开会提示"非单一的 UCS X/Y 轴。正常化。"，须设成世界坐标系的轴
                Dwg::CadHeader& h = m_db.Header;
                h.ModelSpaceXAxis = Dwg::XYZ::AxisX();
                h.ModelSpaceYAxis = Dwg::XYZ::AxisY();
                h.PaperSpaceUcsXAxis = Dwg::XYZ::AxisX();
                h.PaperSpaceUcsYAxis = Dwg::XYZ::AxisY();

                ExportLineTypes();
                ExportLayers();
                ExportTextStyles();
                ExportBlocks();

                Dwg::BlockRecord* model = m_db.ModelSpace();
                m_scene.ForEachObject([&](const Object& o)
                {
                    if (!o.IsKindOf<Entity>())
                        return;
                    if (auto conv = Convert(static_cast<const Entity&>(o)))
                        m_db.AddEntity(model, std::move(conv));
                });

                for (const auto& [name, count] : m_skipped)
                    LOG_WARN("DWG/DXF 导出：不支持的实体 %s × %d 已跳过", name.c_str(), count);
            }

        private:
            void ExportLineTypes()
            {
                m_lineTypeHandles[LineTypeTable::ContinuousID] = HandleOf<Dwg::LineType>(m_db.LineTypes(), "Continuous");
                m_lineTypeHandles[LineTypeTable::ByBlockID]    = HandleOf<Dwg::LineType>(m_db.LineTypes(), "ByBlock");
                m_lineTypeHandles[LineTypeTable::ByLayerID]    = Dwg::kNullHandle;

                const auto& records = m_scene.GetLineTypeTable().Records();
                for (size_t i = LineTypeTable::ContinuousID + 1; i < records.size(); ++i)
                {
                    auto lt = std::make_unique<Dwg::LineType>();
                    lt->Name        = records[i].Name;
                    lt->Description = records[i].Description;
                    for (double v : records[i].Pattern)
                    {
                        Dwg::LineTypeSegment seg;
                        seg.Length = v;
                        lt->Segments.push_back(seg);
                    }
                    if (auto* added = m_db.AddTableEntry(m_db.LineTypes(), std::move(lt)))
                        m_lineTypeHandles[static_cast<LineTypeID>(i)] = added->ObjectHandle;
                }
            }

            void ExportLayers()
            {
                const LayerManager& layers = m_scene.GetLayerManager();
                std::vector<LayerID> ids = layers.GetAllLayerIDs();
                std::sort(ids.begin(), ids.end());
                for (LayerID id : ids)
                {
                    const Layer* src = layers.GetLayer(id);
                    if (src == nullptr)
                        continue;

                    Dwg::Layer* dst = nullptr;
                    if (id == Layer::DefaultLayerID)
                        dst = m_db.FindTableEntry<Dwg::Layer>(m_db.Layers(), "0");
                    else
                    {
                        auto layer = std::make_unique<Dwg::Layer>();
                        layer->Name = src->GetName();
                        dst = m_db.AddTableEntry(m_db.Layers(), std::move(layer));
                    }
                    if (dst == nullptr)
                        continue;

                    dst->Color      = FromColor4(src->GetColor());
                    dst->IsOn       = src->IsVisible();
                    dst->LineWeight = static_cast<Dwg::LineWeightType>(LineweightToRaw(src->GetLineweight()));
                    dst->LineTypeHandle = LineTypeHandle(src->GetLineType());
                    m_layerHandles[id] = dst->ObjectHandle;
                }
            }

            void ExportTextStyles()
            {
                for (const auto& rec : m_scene.GetTextStyleTable().Records())
                {
                    Dwg::TextStyle* dst = m_db.FindTableEntry<Dwg::TextStyle>(m_db.TextStyles(), rec.Name);
                    if (dst == nullptr)
                    {
                        auto st = std::make_unique<Dwg::TextStyle>();
                        st->Name = rec.Name;
                        dst = m_db.AddTableEntry(m_db.TextStyles(), std::move(st));
                    }
                    if (dst == nullptr)
                        continue;
                    dst->Filename        = rec.FontFile;
                    dst->BigFontFilename = rec.BigFontFile;
                    dst->Height          = rec.Height;
                    dst->Width           = rec.WidthFactor;
                    dst->ObliqueAngle    = rec.ObliqueDeg * kPi / 180.0;
                    m_styleHandles[rec.Id] = dst->ObjectHandle;
                }
            }

            void ExportBlocks()
            {
                const BlockTable& table = m_scene.GetBlockTable();

                // 先建全部块记录，嵌套插入才能找到目标
                std::vector<std::pair<Dwg::BlockRecord*, const BlockEntity*>> created;
                table.ForEach([&](BlockID id, const BlockEntity& blk)
                {
                    if (id == BlockTable::ModelSpaceID || id == BlockTable::PaperSpaceID)
                        return;
                    if (Dwg::BlockRecord* rec = m_db.CreateBlockRecord(blk.GetName()))
                    {
                        if (auto* b = m_db.FindAs<Dwg::Block>(rec->BlockEntityHandle))
                        {
                            b->BasePoint = ToXYZ(blk.GetBasePoint());
                            b->Comments  = blk.GetDescription();
                        }
                        m_blockRecords[id] = rec;
                        created.emplace_back(rec, &blk);
                    }
                });

                for (auto& [rec, blk] : created)
                    for (const auto& e : blk->GetEntities())
                        if (auto conv = Convert(*e))
                            m_db.AddEntity(rec, std::move(conv));
            }

            // ── 实体 ────────────────────────────────────────────────────
            template <class T>
            Dwg::Handle HandleOf(const Dwg::CadTable* table, const char* name) const
            {
                const auto* e = m_db.FindTableEntry<T>(table, name);
                return e ? e->ObjectHandle : Dwg::kNullHandle;
            }

            Dwg::Handle LineTypeHandle(LineTypeID id) const
            {
                auto it = m_lineTypeHandles.find(id);
                return it == m_lineTypeHandles.end() ? Dwg::kNullHandle : it->second;
            }

            void ApplyAttr(const Entity& src, Dwg::Entity& dst) const
            {
                const EntityAttr& a = src.GetAttr();
                dst.Color = FromEntityColor(a.Color);
                if (auto it = m_layerHandles.find(a.LayerId); it != m_layerHandles.end())
                    dst.LayerHandle = it->second;
                dst.LineTypeHandle = LineTypeHandle(a.LineType);
                dst.LineTypeScale  = a.LinetypeScale;
                dst.LineWeight     = static_cast<Dwg::LineWeightType>(LineweightToRaw(a.Lineweight));
                dst.IsInvisible    = !a.Visible;
            }

            static Dwg::XYZ NormalOf(const Entity& e)
            {
                const Math::Vec3& n = e.GetExtrusion();
                return { n.x, n.y, n.z };
            }

            Dwg::Handle StyleHandle(TextStyleID id) const
            {
                auto it = m_styleHandles.find(id);
                if (it != m_styleHandles.end())
                    return it->second;
                return HandleOf<Dwg::TextStyle>(m_db.TextStyles(), "Standard");
            }

            template <class T>
            std::unique_ptr<Dwg::Entity> Finish(const Entity& src, std::unique_ptr<T> dst) const
            {
                ApplyAttr(src, *dst);
                return dst;
            }

            std::unique_ptr<Dwg::Entity> Convert(const Entity& e)
            {
                if (auto* s = dynamic_cast<const LineEntity*>(&e))
                {
                    auto d = std::make_unique<Dwg::Line>();
                    d->StartPoint = ToXYZ(s->GetLine().Start);
                    d->EndPoint   = ToXYZ(s->GetLine().End);
                    d->Normal     = NormalOf(e);
                    return Finish(e, std::move(d));
                }
                if (auto* s = dynamic_cast<const ArcEntity*>(&e))
                {
                    auto d = std::make_unique<Dwg::Arc>();
                    d->Center     = ToXYZ(s->GetArc().Center);
                    d->Radius     = s->GetArc().Radius;
                    d->StartAngle = s->GetArc().StartAngle;
                    d->EndAngle   = s->GetArc().EndAngle;
                    d->Normal     = NormalOf(e);
                    return Finish(e, std::move(d));
                }
                if (auto* s = dynamic_cast<const CircleEntity*>(&e))
                {
                    auto d = std::make_unique<Dwg::Circle>();
                    d->Center = ToXYZ(s->GetCircle().Center);
                    d->Radius = s->GetCircle().Radius;
                    d->Normal = NormalOf(e);
                    return Finish(e, std::move(d));
                }
                if (auto* s = dynamic_cast<const EllipseEntity*>(&e))
                    return ConvertEllipse(e, s->GetEllipse());
                if (auto* s = dynamic_cast<const PointEntity*>(&e))
                {
                    auto d = std::make_unique<Dwg::Point>();
                    d->Location = ToXYZ(s->GetPoint().Position);
                    d->Normal   = NormalOf(e);
                    return Finish(e, std::move(d));
                }
                if (auto* s = dynamic_cast<const PolylineEntity*>(&e))
                    return ConvertPolyline(e, s->GetPolyline(), s->GetWidth());
                if (auto* s = dynamic_cast<const RectangleEntity*>(&e))
                {
                    const auto& r = s->GetRectangle();
                    Polyline pl(std::vector<Math::Point3>{ r.P1, r.P2, r.P3, r.P4, r.P1 });
                    return ConvertPolyline(e, pl, 0.0);
                }
                if (auto* s = dynamic_cast<const TextEntity*>(&e))
                {
                    auto d = std::make_unique<Dwg::TextEntity>();
                    d->InsertPoint = ToXYZ(s->GetPosition());
                    d->Value       = s->GetText();
                    d->Height      = s->GetHeight();
                    d->Rotation    = s->GetRotation();
                    d->StyleHandle = StyleHandle(s->GetStyleId());
                    d->Normal      = NormalOf(e);
                    return Finish(e, std::move(d));
                }
                if (auto* s = dynamic_cast<const MTextEntity*>(&e))
                {
                    auto d = std::make_unique<Dwg::MText>();
                    d->InsertPoint    = ToXYZ(s->GetPosition());
                    d->Value          = s->GetText();
                    d->Height         = s->GetHeight();
                    d->RectangleWidth = s->GetBoxWidth();
                    d->AlignmentPoint = { std::cos(s->GetRotation()), std::sin(s->GetRotation()), 0.0 };
                    d->AttachmentPoint = static_cast<Dwg::AttachmentPointType>(static_cast<int>(s->GetAttachment()));
                    d->StyleHandle    = StyleHandle(s->GetStyleId());
                    d->Normal         = NormalOf(e);
                    return Finish(e, std::move(d));
                }
                if (auto* s = dynamic_cast<const InsertEntity*>(&e))
                {
                    std::string name = s->GetBlockName();
                    if (const BlockEntity* blk = m_scene.GetBlockTable().Find(static_cast<BlockID>(s->GetBlockId())))
                        name = blk->GetName();
                    const auto* rec = m_db.FindTableEntry<Dwg::BlockRecord>(m_db.BlockRecords(), name);
                    if (rec == nullptr)
                        return Skip(e);
                    auto d = std::make_unique<Dwg::Insert>();
                    d->BlockHandle  = rec->ObjectHandle;
                    d->InsertPoint  = ToXYZ(s->GetPosition());
                    d->XScale       = s->GetScale().x;
                    d->YScale       = s->GetScale().y;
                    d->ZScale       = s->GetScale().z;
                    d->Rotation     = s->GetRotation();
                    d->ColumnCount  = static_cast<std::uint16_t>(s->GetColumnCount());
                    d->RowCount     = static_cast<std::uint16_t>(s->GetRowCount());
                    d->ColumnSpacing = s->GetColumnSpacing();
                    d->RowSpacing    = s->GetRowSpacing();
                    d->Normal       = NormalOf(e);
                    return Finish(e, std::move(d));
                }
                return Skip(e);
            }

            std::unique_ptr<Dwg::Entity> Skip(const Entity& e)
            {
                ++m_skipped[e.GetTypeInfo()->Name];
                return nullptr;
            }

            std::unique_ptr<Dwg::Entity> ConvertEllipse(const Entity& e, const Ellipse& el)
            {
                auto d = std::make_unique<Dwg::Ellipse>();
                d->Center = ToXYZ(el.Center);
                const double c = std::cos(el.Rotation), s = std::sin(el.Rotation);
                if (el.RadiusX >= el.RadiusY)
                {
                    d->MajorAxisEndPoint = { c * el.RadiusX, s * el.RadiusX, 0.0 };
                    d->RadiusRatio       = el.RadiusX > 0.0 ? el.RadiusY / el.RadiusX : 1.0;
                    d->StartParameter    = el.StartParam;
                    d->EndParameter      = el.EndParam;
                }
                else
                {
                    // 短轴在 Rotation 方向：长轴取 Y 半轴的反方向，参数整体平移 π/2
                    d->MajorAxisEndPoint = { s * el.RadiusY, -c * el.RadiusY, 0.0 };
                    d->RadiusRatio       = el.RadiusY > 0.0 ? el.RadiusX / el.RadiusY : 1.0;
                    d->StartParameter    = el.StartParam + kPi / 2.0;
                    d->EndParameter      = el.EndParam + kPi / 2.0;
                }
                // 参数规整到 [0, 2π)
                auto wrap = [](double t) { t = std::fmod(t, kTwoPi); return t < 0.0 ? t + kTwoPi : t; };
                const double sweep = el.EndParam - el.StartParam;
                if (std::abs(sweep - kTwoPi) < 1e-9)
                {
                    d->StartParameter = 0.0;
                    d->EndParameter   = kTwoPi;
                }
                else
                {
                    d->StartParameter = wrap(d->StartParameter);
                    d->EndParameter   = wrap(d->EndParameter);
                }
                return Finish(e, std::move(d));
            }

            std::unique_ptr<Dwg::Entity> ConvertPolyline(const Entity& e, const Polyline& pl, double width)
            {
                const auto& pts = pl.Points;
                if (pts.size() < 2)
                    return Skip(e);

                auto d = std::make_unique<Dwg::LwPolyline>();
                size_t count = pts.size();
                const double dx = pts.front().x - pts.back().x, dy = pts.front().y - pts.back().y;
                const bool closed = count >= 3 && std::hypot(dx, dy) < 1e-9;
                if (closed)
                {
                    --count;   // 末点与起点重合：用闭合标志表示，最后一段的 bulge 落到末顶点上
                    d->Flags = Dwg::LwPolylineFlags::Closed;
                }
                for (size_t i = 0; i < count; ++i)
                {
                    Dwg::LwPolylineVertex v;
                    v.Location = { pts[i].x, pts[i].y };
                    v.Bulge    = i < pl.Bulges.size() ? pl.Bulges[i] : 0.0;
                    d->Vertices.push_back(v);
                }
                d->Elevation     = pts.front().z;
                d->ConstantWidth = width >= 1.0 ? width : 0.0;
                d->Normal        = NormalOf(e);
                return Finish(e, std::move(d));
            }

        private:
            const Scene&      m_scene;
            Dwg::CadDatabase& m_db;
            Dwg::CadVersion   m_version;

            std::map<LayerID, Dwg::Handle>     m_layerHandles;
            std::map<LineTypeID, Dwg::Handle>  m_lineTypeHandles;
            std::map<TextStyleID, Dwg::Handle> m_styleHandles;
            std::map<BlockID, Dwg::BlockRecord*> m_blockRecords;
            std::map<std::string, int>         m_skipped;
        };
    }

    CadFileKind CadKindFromPath(const std::string& path)
    {
        const size_t dot = path.find_last_of('.');
        if (dot == std::string::npos)
            return CadFileKind::None;
        const std::string ext = Lower(path.substr(dot));
        if (ext == ".dwg") return CadFileKind::Dwg;
        if (ext == ".dxf") return CadFileKind::Dxf;
        return CadFileKind::None;
    }

    bool ImportCad(const std::vector<std::uint8_t>& data, Scene& scene, std::string* error, CadSaveVersion* version)
    {
        auto fail = [&](const std::string& msg)
        {
            if (error)
                *error = msg;
            LOG_ERROR("DWG/DXF 导入失败：%s", msg.c_str());
            return false;
        };

        const size_t headLen = std::min<size_t>(data.size(), 1024);
        const Dwg::CadFileInfo info = Dwg::DetectFileFormat({ data.data(), headLen });

        std::string firstError;
        std::unique_ptr<Dwg::CadDatabase> db;
        if (info.format == Dwg::CadFileFormat::Dwg)
        {
            Dwg::DwgReadOptions opt;
            opt.Notify = MakeNotify(&firstError);
            db = Dwg::ReadDwg(data, opt);
        }
        else if (info.format == Dwg::CadFileFormat::DxfAscii || info.format == Dwg::CadFileFormat::DxfBinary)
        {
            Dwg::DxfReadOptions opt;
            opt.Notify = MakeNotify(&firstError);
            db = Dwg::ReadDxf(data, opt);
        }
        else
            return fail("不是 DWG / DXF 文件");

        if (!db)
            return fail(firstError.empty() ? "文件已损坏或版本不受支持" : firstError);

        if (version)
            *version = db->GetVersion() == Dwg::CadVersion::AC1027 ? CadSaveVersion::R2013 : CadSaveVersion::R2018;
        Importer(*db, scene).Run();
        return true;
    }

    std::vector<std::uint8_t> ExportCad(const Scene& scene, CadFileKind kind, CadSaveVersion version, std::string* error)
    {
        std::string firstError;
        auto db = std::make_unique<Dwg::CadDatabase>();
        Exporter(scene, *db, version == CadSaveVersion::R2013 ? Dwg::CadVersion::AC1027 : Dwg::CadVersion::AC1032).Run();

        std::vector<std::uint8_t> out;
        if (kind == CadFileKind::Dwg)
        {
            Dwg::DwgWriteOptions opt;
            opt.Notify = MakeNotify(&firstError);
            out = Dwg::WriteDwg(*db, opt);
        }
        else if (kind == CadFileKind::Dxf)
        {
            Dwg::DxfWriteOptions opt;
            opt.Notify = MakeNotify(&firstError);
            out = Dwg::WriteDxf(*db, opt);
        }

        if (out.empty())
        {
            if (error)
                *error = firstError.empty() ? "写出失败" : firstError;
            LOG_ERROR("DWG/DXF 导出失败：%s", firstError.c_str());
        }
        return out;
    }
}

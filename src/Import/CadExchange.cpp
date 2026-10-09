#include "Import/CadExchange.h"
#include "Import/TchWall.h"
#include "Import/TchBlockInsert.h"
#include "Import/TchProxyGraphics.h"
#include "Import/TchOpening.h"
#include "Import/TchColumn.h"
#include "Import/TchDimension.h"
#include "Import/TchStair.h"
#include "Import/TchSymbol.h"

#include "Core/Log.h"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/Face3DEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/SolidEntity.hpp"
#include "Scene/Scene.h"
#include "Serialization/EntityIO.h"
#include "Serialization/JsonSerializer.h"

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
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>

namespace Dwg = ::MiniDWG;

namespace MiniCAD
{
    struct CadSource
    {
        struct Link
        {
            Dwg::Handle Handle = Dwg::kNullHandle;
            std::string Signature;  // 导入完成时场景实体的序列化文本；另存时不同即视为修改过
        };

        std::vector<std::uint8_t> Bytes;
        Dwg::CadFileFormat        Format  = Dwg::CadFileFormat::Unknown;
        Dwg::CadVersion           Version = Dwg::CadVersion::Unknown;
        std::map<Object::ObjectID, Link>   Links;            // 模型空间：场景实体 → 原图实体
        std::map<std::string, std::string> LayerSignatures;  // 图层名 → 导入时的属性
        std::map<std::string, std::string> StyleSignatures;  // 文字样式名 → 导入时的属性
        int TchCount = 0;                                    // 模型空间的 TCH_* 对象（含无法显示的）
    };

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

        std::string EntitySignature(const Entity& e)
        {
            JsonSerializer s;
            return EntityIO::Write(s, e) ? s.Dump() : std::string();
        }

        std::string LayerSignature(const Layer& layer)
        {
            JsonSerializer s;
            layer.Serialize(s);
            return s.Dump();
        }

        std::string StyleSignature(const TextStyleRecord& r)
        {
            std::ostringstream os;
            os.precision(17);
            os << r.FontFile << '|' << r.BigFontFile << '|' << r.Height << '|' << r.WidthFactor << '|' << r.ObliqueDeg;
            return os.str();
        }

        bool IsTch(const Dwg::CadObject& o)
        {
            const Dwg::RawObjectData* raw = Dwg::RawDataOf(o);
            return raw != nullptr && raw->DxfName.starts_with("TCH_");
        }

        // 读入 DWG / DXF；format 为 Unknown 时按内容识别
        std::unique_ptr<Dwg::CadDatabase> ReadDatabase(const std::vector<std::uint8_t>& data, Dwg::CadFileFormat format,
                                                       Dwg::NotificationHandler notify)
        {
            if (format == Dwg::CadFileFormat::Dwg)
            {
                Dwg::DwgReadOptions opt;
                opt.Notify = std::move(notify);
                return Dwg::ReadDwg(data, opt);
            }
            if (format == Dwg::CadFileFormat::DxfAscii || format == Dwg::CadFileFormat::DxfBinary)
            {
                Dwg::DxfReadOptions opt;
                opt.Notify = std::move(notify);
                return Dwg::ReadDxf(data, opt);
            }
            return nullptr;
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
            Importer(const Dwg::CadDatabase& db, Scene& scene, CadSource* source)
                : m_db(db), m_scene(scene), m_source(source) {}

            void Run()
            {
                ImportLineTypes();
                ImportLayers();
                ImportTextStyles();
                ImportBlocks();
                ImportModelSpace();
                m_scene.ResolveAllInserts();
                m_scene.MarkDirty();
                RecordSource();

                for (const auto& [name, count] : m_skipped)
                    LOG_WARN("DWG/DXF 导入：不支持的实体 %s × %d 已跳过", name.c_str(), count);
                if (m_tianzhengWalls > 0)
                    LOG_WARN("TCH_WALL × %d 显示为普通轮廓；不修改时另存为原格式原版本可保留天正对象", m_tianzhengWalls);
                for (const auto& [name, count] : m_tchConverted)
                    LOG_WARN("%s × %d 显示为普通几何；不修改时另存为原格式原版本可保留天正对象", name.c_str(), count);
                for (const auto& [name, reason] : m_tchFailures)
                    LOG_WARN("%s 无法恢复显示：%s", name.c_str(), reason.c_str());
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
                        // True-color Index() is -1 as a sentinel, not a negative ACI (off layer).
                        dst->SetVisible(src.IsOn && (src.Color.IsTrueColor() || src.Color.Index() >= 0));
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
                CollectOpenings(*model);
                for (Dwg::Handle h : model->Entities)
                {
                    const auto* e = m_db.FindAs<Dwg::Entity>(h);
                    if (e == nullptr)
                        continue;
                    if (m_source != nullptr && IsTch(*e))
                        ++m_source->TchCount;
                    if (auto conv = Convert(*e))
                    {
                        const Object::ObjectID id = conv->GetID();
                        m_scene.AddEntity(std::move(conv));
                        if (m_source != nullptr)
                            m_source->Links[id].Handle = h;
                    }
                }
            }

            // 门窗：先全部解码，生成墙体时据此在侧线上留出断口
            void CollectOpenings(const Dwg::BlockRecord& model)
            {
                for (Dwg::Handle h : model.Entities)
                {
                    const auto* unknown = m_db.FindAs<Dwg::UnknownEntity>(h);
                    Tch::TchOpening opening;
                    std::string reason;
                    if (unknown == nullptr || unknown->Raw.DxfName != "TCH_OPENING")
                        continue;
                    if (!Tch::DecodeOpening(*unknown, opening, &reason))
                    {
                        m_tchFailures.try_emplace("TCH_OPENING", reason);
                        continue;
                    }
                    m_openings[h] = opening;
                    m_wallOpenings[opening.Wall].push_back(opening);
                }
            }

            // 记下导入完成时的实体、图层、文字样式状态，另存时据此判断哪些改过
            void RecordSource()
            {
                if (m_source == nullptr)
                    return;
                for (auto& [id, link] : m_source->Links)
                    if (const Object* o = m_scene.GetEntity(id); o != nullptr && o->IsKindOf<Entity>())
                        link.Signature = EntitySignature(static_cast<const Entity&>(*o));
                const LayerManager& layers = m_scene.GetLayerManager();
                for (LayerID id : layers.GetAllLayerIDs())
                    if (const Layer* layer = layers.GetLayer(id))
                        m_source->LayerSignatures[layer->GetName()] = LayerSignature(*layer);
                for (const auto& rec : m_scene.GetTextStyleTable().Records())
                    m_source->StyleSignatures[rec.Name] = StyleSignature(rec);
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
                if (const auto* unknown = dynamic_cast<const Dwg::UnknownEntity*>(&src))
                {
                    Tch::TchBlockInsert insert;
                    if (Tch::DecodeBlockInsert(*unknown, insert))
                    {
                        const auto it = m_blocks.find(insert.BlockHandle);
                        if (it != m_blocks.end())
                        {
                            const auto* block = m_scene.GetBlockTable().Find(it->second);
                            auto result = Make<InsertEntity>(src, &insert.Normal,
                                static_cast<InsertEntity::BlockID>(it->second), block ? block->GetName() : std::string(), ToPoint(insert.Point));
                            auto& geometry = static_cast<InsertEntity&>(*result);
                            geometry.SetScale({insert.XScale, insert.YScale, insert.ZScale});
                            geometry.SetRotation(insert.Rotation);
                            ++m_tchConverted[unknown->Raw.DxfName];
                            return result;
                        }
                    }
                    if (auto it = m_openings.find(src.ObjectHandle); it != m_openings.end())
                        if (auto converted = ConvertOpening(src, it->second))
                        {
                            ++m_tchConverted[unknown->Raw.DxfName];
                            return converted;
                        }
                    std::string nativeReason;
                    // 天正尺寸文字按宽高比 0.7 显示（与标注样式文字样式的宽度因子无关，与天正 2014 截图核对）
                    Tch::TchDimension dimension;
                    if (Tch::DecodeDimension(*unknown, dimension, &nativeReason))
                    {
                        TextStyleID textStyle = TextStyleTable::StandardID;
                        const Tch::TchDimStyle style = DimStyleOf(dimension.DimStyle, textStyle);
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildDimension(dimension, style), WidthVariant(textStyle, 0.7));
                    }
                    Tch::TchRadiusDim radiusDim;
                    if (Tch::DecodeRadiusDim(*unknown, radiusDim, &nativeReason))
                    {
                        TextStyleID textStyle = TextStyleTable::StandardID;
                        const Tch::TchDimStyle style = DimStyleOf(radiusDim.DimStyle, textStyle);
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildRadiusDim(radiusDim, style), WidthVariant(textStyle, 0.7));
                    }
                    Tch::TchCoord coord;
                    if (Tch::DecodeCoord(*unknown, coord, &nativeReason))
                    {
                        // 天正分解出的坐标文字宽度因子为 0.6 / 0.85
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildCoord(coord), WidthVariant(LookupStyle(coord.TextStyle), 0.6 / 0.85));
                    }
                    Tch::TchArrow arrow;
                    if (Tch::DecodeArrow(*unknown, arrow, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildArrow(arrow), LookupStyle(arrow.TextStyle));
                    }
                    Tch::TchSection section;
                    if (Tch::DecodeSection(*unknown, section, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildSection(section), WidthVariant(LookupStyle(section.TextStyle), 0.6 / 0.85));
                    }
                    Tch::TchIndexPointer indexPointer;
                    if (Tch::DecodeIndexPointer(*unknown, indexPointer, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildIndexPointer(indexPointer), LookupStyle(indexPointer.TextStyle),
                                               WidthVariant(LookupStyle(indexPointer.NoteStyle), 0.6 / 0.85));
                    }
                    Tch::TchDrawingName drawingName;
                    if (Tch::DecodeDrawingName(*unknown, drawingName, &nativeReason))
                    {
                        // 天正按原图文字样式的字体（textbox）计算宽度并居中排布，所以按 DWG 中的样式估算
                        //（场景里同名的 Standard 等样式可能是 MiniCAD 自己的设置）
                        const TextStyleID nameStyle = LookupStyle(drawingName.NameStyle), scaleStyle = LookupStyle(drawingName.ScaleStyle);
                        auto extent = [&](const std::string& text, double height, bool scaleText) {
                            const auto* st = m_db.FindAs<Dwg::TextStyle>(scaleText ? drawingName.ScaleStyle : drawingName.NameStyle);
                            const double factor = st != nullptr && st->Width > 0 ? st->Width : 1.0;
                            auto e = Tch::Detail::EstimateExtent(st ? st->Filename : std::string(), text);
                            return Tch::Detail::TextExtent{e.Width * height * factor, e.Bottom * height};
                        };
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildDrawingName(drawingName, extent), nameStyle, scaleStyle);
                    }
                    Tch::TchRectStair stair;
                    if (Tch::DecodeRectStair(*unknown, stair, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildRectStair(stair), LookupStyle(stair.TextStyle), std::nullopt, stair.ArrowLayer);
                    }
                    Tch::TchAxisLabel axisLabel;
                    if (Tch::DecodeAxisLabel(*unknown, axisLabel, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildAxisLabel(axisLabel), LookupStyle(axisLabel.TextStyle));
                    }
                    Tch::TchColumn column;
                    if (Tch::DecodeColumn(*unknown, column, &nativeReason))
                    {
                        // 柱：闭合轮廓多段线（天正 2014 显示为轮廓线，无填充）
                        std::vector<Math::Point3> pts;
                        std::vector<double> bulges;
                        for (const auto& v : column.Outline)
                        {
                            pts.push_back({v.X, v.Y, column.Position.Z});
                            bulges.push_back(v.Bulge);
                        }
                        pts.push_back(pts.front());
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return Make<PolylineEntity>(src, nullptr, std::move(pts), std::move(bulges));
                    }
                    Tch::TchWall wall;
                    if (Tch::DecodeWall(*unknown, wall, &nativeReason))
                    {
                        const double dx = wall.End.X - wall.Start.X, dy = wall.End.Y - wall.Start.Y;
                        const double length = std::hypot(dx, dy);
                        const double nx = -dy / length, ny = dx / length;
                        const double ux = dx / length, uy = dy / length;
                        std::vector<Math::Point3> points{
                            {wall.Start.X + nx * wall.LeftWidth + ux * wall.Joins[0], wall.Start.Y + ny * wall.LeftWidth + uy * wall.Joins[0], wall.Start.Z},
                            {wall.End.X + nx * wall.LeftWidth - ux * wall.Joins[1], wall.End.Y + ny * wall.LeftWidth - uy * wall.Joins[1], wall.End.Z},
                            {wall.End.X - nx * wall.RightWidth - ux * wall.Joins[3], wall.End.Y - ny * wall.RightWidth - uy * wall.Joins[3], wall.End.Z},
                            {wall.Start.X - nx * wall.RightWidth + ux * wall.Joins[2], wall.Start.Y - ny * wall.RightWidth + uy * wall.Joins[2], wall.Start.Z}};
                        // Only the two side edges have verified geometry. Adding
                        // per-wall end caps creates false seams at connected nodes.
                        std::string name = "*U_TCH_" + std::to_string(src.ObjectHandle);
                        while (m_scene.GetBlockTable().Get(name)) name += "_";
                        const auto blockId = m_scene.GetBlockTable().Create(name);
                        auto* block = m_scene.GetBlockTable().Find(blockId);
                        block->SetFlags(BlockEntity::Anonymous);
                        // 门窗处断开：区间为门窗中点在轴线上的投影 ± 半宽（与天正 2014 分解结果核对）
                        std::vector<std::pair<double, double>> cuts;
                        if (auto it = m_wallOpenings.find(src.ObjectHandle); it != m_wallOpenings.end())
                            for (const auto& o : it->second)
                            {
                                const double t = (o.Position.X - wall.Start.X) * ux + (o.Position.Y - wall.Start.Y) * uy;
                                cuts.emplace_back(t - o.Width / 2, t + o.Width / 2);
                            }
                        std::sort(cuts.begin(), cuts.end());
                        auto addSide = [&](const Math::Point3& a, const Math::Point3& b, double from, double to)
                        {
                            const Math::Point3 d{(b.x - a.x) / (to - from), (b.y - a.y) / (to - from), 0};
                            auto at = [&](double t) { return Math::Point3{a.x + d.x * (t - from), a.y + d.y * (t - from), a.z}; };
                            double cursor = from;
                            for (const auto& [c0, c1] : cuts)
                            {
                                if (c1 <= cursor || c0 >= to) continue;
                                if (c0 > cursor) block->AddEntity(Make<LineEntity>(src, nullptr, at(cursor), at(c0)));
                                cursor = std::max(cursor, c1);
                            }
                            if (cursor < to) block->AddEntity(Make<LineEntity>(src, nullptr, at(cursor), at(to)));
                        };
                        addSide(points[0], points[1], wall.Joins[0], length - wall.Joins[1]);
                        addSide(points[3], points[2], wall.Joins[2], length - wall.Joins[3]);
                        ++m_tianzhengWalls;
                        return Make<InsertEntity>(src, nullptr, static_cast<InsertEntity::BlockID>(blockId), name, Math::Point3{0, 0, 0});
                    }
                    if (unknown->Raw.DxfName.starts_with("TCH_"))
                    {
                        Tch::TchProxyGraphics proxy;
                        std::string reason;
                        if (Tch::DecodeProxyGraphics(*unknown, proxy, &reason))
                        {
                            std::vector<std::unique_ptr<Entity>> geometry;
                            for (const auto& primitive : proxy.Entities)
                                if (auto converted = Convert(*primitive)) geometry.push_back(std::move(converted));
                            if (geometry.size() == proxy.Entities.size())
                            {
                                std::string name = "*U_TCH_PROXY_" + std::to_string(src.ObjectHandle);
                                while (m_scene.GetBlockTable().Get(name)) name += "_";
                                const auto blockId = m_scene.GetBlockTable().Create(name);
                                auto* block = m_scene.GetBlockTable().Find(blockId);
                                block->SetFlags(BlockEntity::Anonymous);
                                for (auto& primitive : geometry) block->AddEntity(std::move(primitive));
                                ++m_tchConverted[unknown->Raw.DxfName];
                                return Make<InsertEntity>(src, nullptr, static_cast<InsertEntity::BlockID>(blockId), name, Math::Point3{0, 0, 0});
                            }
                            reason = "proxy primitive cannot be mapped to Scene";
                        }
                        if (!nativeReason.empty()) reason = nativeReason + "; " + reason;
                        m_tchFailures.try_emplace(unknown->Raw.DxfName, reason);
                    }
                }
                if (auto* e = dynamic_cast<const Dwg::Face3D*>(&src))
                {
                    const std::array points{e->FirstCorner, e->SecondCorner, e->ThirdCorner, e->FourthCorner};
                    for (const auto& p : points)
                        if (!std::isfinite(p.X) || !std::isfinite(p.Y) || !std::isfinite(p.Z)) return Skip(src);
                    return Make<Face3DEntity>(src, nullptr, ToPoint(e->FirstCorner), ToPoint(e->SecondCorner),
                        ToPoint(e->ThirdCorner), ToPoint(e->FourthCorner), static_cast<std::uint32_t>(e->Flags));
                }
                if (auto* e = dynamic_cast<const Dwg::Solid*>(&src))
                    return Make<SolidEntity>(src, &e->Normal, ToPoint(e->FirstCorner), ToPoint(e->SecondCorner),
                                             ToPoint(e->FourthCorner), ToPoint(e->ThirdCorner));
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

            // 门窗显示：二维图块的块参照。X 缩放为宽度；Y 缩放门为宽度、窗为墙厚；镜像取负；旋转沿墙
            // （天正 2014 分解样例：窗 (1800, -240)、门 (900, -900)，插入点 Z 为 0）
            // 天正标注用到的标注样式值；找不到样式时用天正默认值（_TCH_ARCH）
            Tch::TchDimStyle DimStyleOf(Dwg::Handle handle, TextStyleID& textStyle) const
            {
                Tch::TchDimStyle style;
                if (const auto* ds = m_db.FindAs<Dwg::DimensionStyle>(handle))
                {
                    style.ExtOffset = ds->ExtensionLineOffset;
                    style.ExtExtension = ds->ExtensionLineExtension;
                    style.ArrowSize = ds->ArrowSize;
                    style.TextHeight = ds->TextHeight;
                    style.Gap = std::abs(ds->DimensionLineGap);
                    style.Decimals = ds->DecimalPlaces;
                    style.AngularDecimals = ds->AngularDecimalPlaces;
                    textStyle = LookupStyle(ds->StyleHandle);
                }
                return style;
            }

            // 宽度因子不同的同名派生样式（名字加 _W 与百分比），没有时按原样式复制一份
            TextStyleID WidthVariant(TextStyleID base, double width)
            {
                TextStyleTable& table = m_scene.GetTextStyleTable();
                const TextStyleRecord* rec = table.Find(base);
                if (rec == nullptr || std::abs(rec->WidthFactor - width) < 1e-9)
                    return base;
                TextStyleRecord copy = *rec;
                copy.Name += "_W" + std::to_string(static_cast<int>(std::lround(width * 100)));
                copy.WidthFactor = width;
                if (const TextStyleID id = table.FindByName(copy.Name); id != TextStyleTable::InvalidID)
                    return id;
                return table.Add(std::move(copy));
            }

            // 天正标注、轴号的图元放进匿名块：线随对象的图层与颜色，文字为 7 号色（与天正 2014 分解结果一致）。
            // Layer = 1 的图元放到对象引用的符号图层 symbolLayer（如楼梯箭头的 DIM_SYMB），颜色随层
            std::unique_ptr<Entity> MakeTchGraphics(const Dwg::Entity& src, const Tch::TchGraphics& g, TextStyleID textStyle,
                                                    std::optional<TextStyleID> secondStyle = std::nullopt,
                                                    Dwg::Handle symbolLayer = Dwg::kNullHandle)
            {
                auto onLayer = [&](Entity& e, int layer) {
                    if (layer != 1) return;
                    const auto it = m_layers.find(symbolLayer);
                    if (it == m_layers.end()) return;
                    EntityAttr attr = e.GetAttr();
                    attr.LayerId = it->second;
                    attr.Color = EntityColor::ByLayer();
                    e.SetAttr(attr);
                };
                const double z = 0;
                auto pt = [&](const Tch::TchGraphics::Point& p) { return Math::Point3{p.X, p.Y, z}; };
                std::string name = "*U_TCH_" + std::to_string(src.ObjectHandle);
                while (m_scene.GetBlockTable().Get(name)) name += "_";
                const auto blockId = m_scene.GetBlockTable().Create(name);
                auto* block = m_scene.GetBlockTable().Find(blockId);
                block->SetFlags(BlockEntity::Anonymous);
                for (const auto& l : g.Lines)
                    block->AddEntity(Make<LineEntity>(src, nullptr, pt(l.A), pt(l.B)));
                for (const auto& t : g.Ticks)
                {
                    auto tick = Make<PolylineEntity>(src, nullptr, std::vector<Math::Point3>{pt(t.A), pt(t.B)});
                    static_cast<PolylineEntity&>(*tick).SetWidth(t.Width);
                    block->AddEntity(std::move(tick));
                }
                for (const auto& a : g.Arcs)
                    block->AddEntity(Make<ArcEntity>(src, nullptr, pt(a.Center), a.Radius, a.Start, a.End));
                for (const auto& c : g.Circles)
                    block->AddEntity(Make<CircleEntity>(src, nullptr, pt(c.Center), c.Radius));
                for (const auto& t : g.Arrows)
                {
                    auto solid = Make<SolidEntity>(src, nullptr, pt(t.A), pt(t.B), pt(t.C));
                    onLayer(*solid, t.Layer);
                    block->AddEntity(std::move(solid));
                }
                for (const auto& path : g.Paths)
                {
                    std::vector<Math::Point3> pts;
                    std::vector<double> bulges;
                    for (const auto& p : path.Points) { pts.push_back(pt(p.P)); bulges.push_back(p.Bulge); }
                    auto pl = Make<PolylineEntity>(src, nullptr, std::move(pts), std::move(bulges));
                    static_cast<PolylineEntity&>(*pl).SetWidth(path.Width);
                    onLayer(*pl, path.Layer);
                    block->AddEntity(std::move(pl));
                }
                for (const auto& t : g.Texts)
                {
                    const TextStyleID style = t.Style == 1 ? secondStyle.value_or(textStyle) : textStyle;
                    auto text = Make<MTextEntity>(src, nullptr, style, t.Value, pt(t.Center), t.Height, t.Rotation, 0.0);
                    auto& mt = static_cast<MTextEntity&>(*text);
                    mt.SetAttachment(static_cast<MTextAttachment>(t.Attachment));
                    onLayer(mt, t.Layer);
                    EntityAttr attr = mt.GetAttr();
                    attr.Color = t.Color == 0 ? EntityColor::ByBlock() : EntityColor::FromAci(static_cast<std::uint16_t>(t.Color));
                    mt.SetAttr(attr);
                    block->AddEntity(std::move(text));
                }
                return Make<InsertEntity>(src, nullptr, static_cast<InsertEntity::BlockID>(blockId), name, Math::Point3{0, 0, 0});
            }

            std::unique_ptr<Entity> ConvertOpening(const Dwg::Entity& src, const Tch::TchOpening& o)
            {
                const auto block = m_blocks.find(o.Block2D);
                if (block == m_blocks.end())
                {
                    m_tchFailures.try_emplace("TCH_OPENING", "opening 2D block is missing");
                    return nullptr;
                }
                const auto* host = m_db.FindAs<Dwg::UnknownEntity>(o.Wall);
                Tch::TchWall wall;
                const bool hasWall = host != nullptr && Tch::DecodeWall(*host, wall);
                double depth = o.Width;
                if (o.Type == Tch::TchOpening::Kind::Window)
                {
                    if (!hasWall)
                    {
                        m_tchFailures.try_emplace("TCH_OPENING", "window host wall thickness is unknown");
                        return nullptr;
                    }
                    depth = wall.LeftWidth + wall.RightWidth;
                }
                const auto* def = m_scene.GetBlockTable().Find(block->second);
                auto result = Make<InsertEntity>(src, nullptr, static_cast<InsertEntity::BlockID>(block->second),
                    def ? def->GetName() : std::string(), Math::Point3{o.Position.X, o.Position.Y, 0});
                auto& insert = static_cast<InsertEntity&>(*result);
                const double yScale = o.MirrorY() ? -depth : depth;
                insert.SetScale({o.MirrorX() ? -o.Width : o.Width, yScale, o.Width});
                insert.SetRotation(o.Angle);
                if (o.Type != Tch::TchOpening::Kind::Door || !hasWall)
                    return result;

                // 门：门扇所在一侧的对面墙面上，横跨门洞画一条线（天正 2014 的显示与分解结果都有这条线，
                // 长度等于门宽，位置在墙面上）
                const double ux = std::cos(o.Angle), uy = std::sin(o.Angle);
                const double side = yScale > 0 ? -1.0 : 1.0;                 // 门扇对面：块 -y 方向
                const double ox = -uy * side, oy = ux * side;                  // 门槛线所在一侧的法向
                const double wl = std::hypot(wall.End.X - wall.Start.X, wall.End.Y - wall.Start.Y);
                const double wallLeftX = -(wall.End.Y - wall.Start.Y) / wl, wallLeftY = (wall.End.X - wall.Start.X) / wl;
                const double face = (ox * wallLeftX + oy * wallLeftY) > 0 ? wall.LeftWidth : wall.RightWidth;
                const Math::Point3 c{o.Position.X + ox * face, o.Position.Y + oy * face, 0};
                const double hw = o.Width / 2;
                std::string name = "*U_TCH_OPENING_" + std::to_string(src.ObjectHandle);
                while (m_scene.GetBlockTable().Get(name)) name += "_";
                const auto blockId = m_scene.GetBlockTable().Create(name);
                auto* group = m_scene.GetBlockTable().Find(blockId);
                group->SetFlags(BlockEntity::Anonymous);
                m_scene.ResolveInsert(insert);
                group->AddEntity(std::move(result));
                group->AddEntity(Make<LineEntity>(src, nullptr, Math::Point3{c.x - ux * hw, c.y - uy * hw, 0},
                                                  Math::Point3{c.x + ux * hw, c.y + uy * hw, 0}));
                return Make<InsertEntity>(src, nullptr, static_cast<InsertEntity::BlockID>(blockId), name, Math::Point3{0, 0, 0});
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
            CadSource*              m_source;

            std::map<Dwg::Handle, LayerID>     m_layers;
            std::map<Dwg::Handle, LineTypeID>  m_lineTypes;
            std::map<Dwg::Handle, TextStyleID> m_styles;
            std::map<Dwg::Handle, BlockID>     m_blocks;
            std::map<Dwg::Handle, Tch::TchOpening> m_openings;                      // 门窗句柄 → 解码结果
            std::map<Dwg::Handle, std::vector<Tch::TchOpening>> m_wallOpenings;    // 墙句柄 → 墙上的门窗
            std::map<std::string, int>         m_skipped;
            int m_tianzhengWalls = 0;
            std::map<std::string, int> m_tchConverted;
            std::map<std::string, std::string> m_tchFailures;
        };

        // ════════════════════════════════════════════════════════════════
        //  导出：Scene → CadDatabase
        // ════════════════════════════════════════════════════════════════
        class Exporter
        {
        public:
            // source 非空：db 是重新读出的原图，以它为底只改写变化的部分（见 CadSource）；为空：db 是空库，重新生成
            Exporter(const Scene& scene, Dwg::CadDatabase& db, Dwg::CadVersion version, const CadSource* source)
                : m_scene(scene), m_db(db), m_version(version), m_source(source) {}

            void Run()
            {
                if (m_source == nullptr)
                {
                    m_db.SetVersion(m_version);
                    m_db.CreateDefaults();

                    // 头变量里的 UCS 轴默认是零向量，AutoCAD 打开会提示"非单一的 UCS X/Y 轴。正常化。"，须设成世界坐标系的轴
                    Dwg::CadHeader& h = m_db.Header;
                    h.ModelSpaceXAxis = Dwg::XYZ::AxisX();
                    h.ModelSpaceYAxis = Dwg::XYZ::AxisY();
                    h.PaperSpaceUcsXAxis = Dwg::XYZ::AxisX();
                    h.PaperSpaceUcsYAxis = Dwg::XYZ::AxisY();
                }

                ExportLineTypes();
                ExportLayers();
                ExportTextStyles();
                ExportBlocks();

                if (m_source != nullptr)
                    PatchModelSpace();
                else
                {
                    Dwg::BlockRecord* model = m_db.ModelSpace();
                    m_scene.ForEachObject([&](const Object& o)
                    {
                        if (!o.IsKindOf<Entity>())
                            return;
                        if (auto conv = Convert(static_cast<const Entity&>(o)))
                            m_db.AddEntity(model, std::move(conv));
                    });
                }

                for (const auto& [name, count] : m_skipped)
                    LOG_WARN("DWG/DXF 导出：不支持的实体 %s × %d 已跳过", name.c_str(), count);
            }

        private:
            // 以原图为底写模型空间：没改过的实体保留原样（天正等自定义对象因此能写回）；改过的按场景重新写出，
            // 普通实体沿用原句柄（引用它的对象仍然有效），自定义对象换新句柄（引用它的对象不会把普通几何当成原对象）；
            // 场景里删掉的从原图移除；导入时跳过、场景里没有对应实体的原样保留
            void PatchModelSpace()
            {
                Dwg::BlockRecord* model = m_db.ModelSpace();
                if (model == nullptr)
                    return;

                std::set<Object::ObjectID> alive;
                std::set<Dwg::Handle> removed;
                std::vector<std::unique_ptr<Dwg::Entity>> appended;
                int kept = 0, keptTch = 0, rewrittenTch = 0;
                m_scene.ForEachObject([&](const Object& o)
                {
                    if (!o.IsKindOf<Entity>())
                        return;
                    const auto& e = static_cast<const Entity&>(o);
                    alive.insert(e.GetID());
                    const auto link = m_source->Links.find(e.GetID());
                    const Dwg::Entity* original = link != m_source->Links.end()
                        ? m_db.FindAs<Dwg::Entity>(link->second.Handle) : nullptr;
                    if (original != nullptr && !link->second.Signature.empty() && link->second.Signature == EntitySignature(e))
                    {
                        ++kept;
                        keptTch += IsTch(*original) ? 1 : 0;
                        return;
                    }

                    auto conv = Convert(e);
                    if (original == nullptr)
                    {
                        if (conv)
                            appended.push_back(std::move(conv));
                        return;
                    }
                    const Dwg::Handle h = link->second.Handle;
                    rewrittenTch += IsTch(*original) ? 1 : 0;
                    const bool custom = Dwg::RawDataOf(*original) != nullptr;
                    m_db.RemoveObject(h);
                    if (conv && !custom)
                    {
                        conv->ObjectHandle = h;     // 位置不变：模型空间的实体列表里仍是这个句柄
                        if (Dwg::Entity* added = m_db.AddEntity<Dwg::Entity>(nullptr, std::move(conv)))
                            added->OwnerHandle = model->ObjectHandle;
                        return;
                    }
                    removed.insert(h);
                    if (conv)
                        appended.push_back(std::move(conv));
                });

                for (const auto& [id, link] : m_source->Links)
                {
                    if (alive.count(id) == 0 && m_db.Find(link.Handle) != nullptr)
                    {
                        m_db.RemoveObject(link.Handle);
                        removed.insert(link.Handle);
                    }
                }
                std::erase_if(model->Entities, [&](Dwg::Handle h) { return removed.count(h) != 0; });
                for (auto& e : appended)
                    m_db.AddEntity(model, std::move(e));

                LOG_INFO("DWG/DXF 导出：以原图为底，%d 个未修改的实体原样写回（其中天正对象 %d 个）", kept, keptTch);
                if (rewrittenTch > 0)
                    LOG_WARN("DWG/DXF 导出：%d 个修改过的天正对象写为普通几何", rewrittenTch);
            }

            void ExportLineTypes()
            {
                m_lineTypeHandles[LineTypeTable::ContinuousID] = HandleOf<Dwg::LineType>(m_db.LineTypes(), "Continuous");
                m_lineTypeHandles[LineTypeTable::ByBlockID]    = HandleOf<Dwg::LineType>(m_db.LineTypes(), "ByBlock");
                m_lineTypeHandles[LineTypeTable::ByLayerID]    = Dwg::kNullHandle;

                const auto& records = m_scene.GetLineTypeTable().Records();
                for (size_t i = LineTypeTable::ContinuousID + 1; i < records.size(); ++i)
                {
                    if (m_source != nullptr)
                    {
                        if (const auto* existing = m_db.FindTableEntry<Dwg::LineType>(m_db.LineTypes(), records[i].Name))
                        {
                            m_lineTypeHandles[static_cast<LineTypeID>(i)] = existing->ObjectHandle;
                            continue;
                        }
                    }
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
                    else if (m_source != nullptr)
                        dst = m_db.FindTableEntry<Dwg::Layer>(m_db.Layers(), src->GetName());
                    const bool existed = dst != nullptr;
                    if (dst == nullptr && id != Layer::DefaultLayerID)
                    {
                        auto layer = std::make_unique<Dwg::Layer>();
                        layer->Name = src->GetName();
                        dst = m_db.AddTableEntry(m_db.Layers(), std::move(layer));
                    }
                    if (dst == nullptr)
                        continue;
                    m_layerHandles[id] = dst->ObjectHandle;
                    // 原图的图层没改过就保持原样（颜色索引、冻结、打印等 MiniCAD 不保存的属性不丢）
                    if (existed && Unchanged(m_source ? &m_source->LayerSignatures : nullptr, src->GetName(), LayerSignature(*src)))
                        continue;

                    dst->Color      = FromColor4(src->GetColor());
                    dst->IsOn       = src->IsVisible();
                    dst->LineWeight = static_cast<Dwg::LineWeightType>(LineweightToRaw(src->GetLineweight()));
                    dst->LineTypeHandle = LineTypeHandle(src->GetLineType());
                }
            }

            void ExportTextStyles()
            {
                for (const auto& rec : m_scene.GetTextStyleTable().Records())
                {
                    Dwg::TextStyle* dst = m_db.FindTableEntry<Dwg::TextStyle>(m_db.TextStyles(), rec.Name);
                    if (dst != nullptr && Unchanged(m_source ? &m_source->StyleSignatures : nullptr, rec.Name, StyleSignature(rec)))
                    {
                        m_styleHandles[rec.Id] = dst->ObjectHandle;
                        continue;
                    }
                    if (dst == nullptr && m_source != nullptr)
                        continue;           // 以原图为底：原图没有的样式等写出的实体用到时再建（StyleHandle）
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

            static bool Unchanged(const std::map<std::string, std::string>* signatures, const std::string& name,
                                  const std::string& now)
            {
                if (signatures == nullptr)
                    return false;
                const auto it = signatures->find(name);
                return it != signatures->end() && it->second == now;
            }

            // 以原图为底时块定义按需建立（见 BlockRecordFor）：原图已有的同名块原样保留，
            // 只有新块才写出；天正对象原样写回时，导入为它生成的显示块因此不会写出
            void ExportBlocks()
            {
                if (m_source != nullptr)
                    return;
                const BlockTable& table = m_scene.GetBlockTable();

                // 先建全部块记录，嵌套插入才能找到目标
                std::vector<std::pair<Dwg::BlockRecord*, const BlockEntity*>> created;
                table.ForEach([&](BlockID id, const BlockEntity& blk)
                {
                    if (id == BlockTable::ModelSpaceID || id == BlockTable::PaperSpaceID)
                        return;
                    if (Dwg::BlockRecord* rec = CreateBlock(blk))
                        created.emplace_back(rec, &blk);
                });

                for (auto& [rec, blk] : created)
                    FillBlock(rec, *blk);
            }

            Dwg::BlockRecord* CreateBlock(const BlockEntity& blk)
            {
                Dwg::BlockRecord* rec = m_db.CreateBlockRecord(blk.GetName());
                if (rec != nullptr)
                {
                    if (auto* b = m_db.FindAs<Dwg::Block>(rec->BlockEntityHandle))
                    {
                        b->BasePoint = ToXYZ(blk.GetBasePoint());
                        b->Comments  = blk.GetDescription();
                    }
                }
                return rec;
            }

            void FillBlock(Dwg::BlockRecord* rec, const BlockEntity& blk)
            {
                for (const auto& e : blk.GetEntities())
                    if (auto conv = Convert(*e))
                        m_db.AddEntity(rec, std::move(conv));
            }

            // 块参照指向的块记录：按名称查找；以原图为底时找不到就现建（先建记录再填内容，嵌套引用自身也能找到）
            const Dwg::BlockRecord* BlockRecordFor(BlockID id, const std::string& name)
            {
                if (const auto* rec = m_db.FindTableEntry<Dwg::BlockRecord>(m_db.BlockRecords(), name))
                    return rec;
                const BlockEntity* blk = m_source != nullptr ? m_scene.GetBlockTable().Find(id) : nullptr;
                if (blk == nullptr)
                    return nullptr;
                Dwg::BlockRecord* rec = CreateBlock(*blk);
                if (rec != nullptr)
                    FillBlock(rec, *blk);
                return rec;
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

            Dwg::Handle StyleHandle(TextStyleID id)
            {
                auto it = m_styleHandles.find(id);
                if (it != m_styleHandles.end())
                    return it->second;
                if (const TextStyleRecord* rec = m_scene.GetTextStyleTable().Find(id); rec != nullptr && m_source != nullptr)
                {
                    auto st = std::make_unique<Dwg::TextStyle>();
                    st->Name            = rec->Name;
                    st->Filename        = rec->FontFile;
                    st->BigFontFilename = rec->BigFontFile;
                    st->Height          = rec->Height;
                    st->Width           = rec->WidthFactor;
                    st->ObliqueAngle    = rec->ObliqueDeg * kPi / 180.0;
                    if (Dwg::TextStyle* dst = m_db.AddTableEntry(m_db.TextStyles(), std::move(st)))
                        return m_styleHandles[id] = dst->ObjectHandle;
                }
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
                if (auto* s = dynamic_cast<const SolidEntity*>(&e))
                {
                    // SolidEntity 按周边顺序保存，DWG 按 1-2-4-3 的顺序
                    const auto& r = s->GetRectangle();
                    auto d = std::make_unique<Dwg::Solid>();
                    d->FirstCorner = ToXYZ(r.P1); d->SecondCorner = ToXYZ(r.P2);
                    d->ThirdCorner = ToXYZ(r.P4); d->FourthCorner = ToXYZ(r.P3);
                    d->Normal = NormalOf(e);
                    return Finish(e, std::move(d));
                }
                if (auto* s = dynamic_cast<const Face3DEntity*>(&e))
                {
                    const auto& r = s->GetRectangle();
                    auto d = std::make_unique<Dwg::Face3D>();
                    d->FirstCorner = ToXYZ(r.P1); d->SecondCorner = ToXYZ(r.P2);
                    d->ThirdCorner = ToXYZ(r.P3); d->FourthCorner = ToXYZ(r.P4);
                    d->Flags = static_cast<Dwg::InvisibleEdgeFlags>(s->GetInvisibleEdges());
                    return Finish(e, std::move(d));
                }
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
                    const auto* rec = BlockRecordFor(static_cast<BlockID>(s->GetBlockId()), name);
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
            const CadSource*  m_source;

            std::map<LayerID, Dwg::Handle>     m_layerHandles;
            std::map<LineTypeID, Dwg::Handle>  m_lineTypeHandles;
            std::map<TextStyleID, Dwg::Handle> m_styleHandles;
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

    bool ImportCad(const std::vector<std::uint8_t>& data, Scene& scene, std::string* error, CadSaveVersion* version,
                   std::shared_ptr<const CadSource>* source)
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

        if (info.format == Dwg::CadFileFormat::Unknown)
            return fail("不是 DWG / DXF 文件");
        std::string firstError;
        std::unique_ptr<Dwg::CadDatabase> db = ReadDatabase(data, info.format, MakeNotify(&firstError));

        if (!db)
            return fail(firstError.empty() ? "文件已损坏或版本不受支持" : firstError);

        if (version)
            *version = db->GetVersion() == Dwg::CadVersion::AC1027 ? CadSaveVersion::R2013 : CadSaveVersion::R2018;
        std::shared_ptr<CadSource> src;
        if (source != nullptr)
        {
            src = std::make_shared<CadSource>();
            src->Bytes   = data;
            src->Format  = info.format;
            src->Version = db->GetVersion();
        }
        Importer(*db, scene, src.get()).Run();
        if (source != nullptr)
            *source = std::move(src);
        return true;
    }

    int CadSourceTchCount(const CadSource* source)
    {
        return source != nullptr ? source->TchCount : 0;
    }

    std::vector<std::uint8_t> ExportCad(const Scene& scene, CadFileKind kind, CadSaveVersion version, std::string* error,
                                        const CadSource* source)
    {
        std::string firstError;
        const Dwg::CadVersion target = version == CadSaveVersion::R2013 ? Dwg::CadVersion::AC1027 : Dwg::CadVersion::AC1032;

        // 格式与版本都与原图相同：以重新读出的原图为底（未建模对象的原始数据只能写回同一格式、同一版本）
        std::unique_ptr<Dwg::CadDatabase> db;
        const CadSource* base = nullptr;
        if (source != nullptr && source->Version == target
            && (kind == CadFileKind::Dwg ? source->Format == Dwg::CadFileFormat::Dwg
                                         : source->Format == Dwg::CadFileFormat::DxfAscii || source->Format == Dwg::CadFileFormat::DxfBinary))
        {
            db = ReadDatabase(source->Bytes, source->Format, nullptr);
            if (db)
                base = source;
        }
        if (!db)
        {
            db = std::make_unique<Dwg::CadDatabase>();
            if (CadSourceTchCount(source) > 0)
                LOG_WARN("DWG/DXF 导出：格式或版本与原图不同，天正对象 × %d 不能原样写回，只写出显示用的普通几何",
                         CadSourceTchCount(source));
        }
        Exporter(scene, *db, target, base).Run();

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

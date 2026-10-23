#include "Import/CadExchange.h"
#include "Import/TchWall.h"
#include "Import/TchBlockInsert.h"
#include "Import/TchProxyGraphics.h"
#include "Import/TchOpening.h"
#include "Import/TchColumn.h"
#include "Import/TchDimension.h"
#include "Import/TchStair.h"
#include "Import/TchPathArray.h"
#include "Import/TchText.h"
#include "Import/TchAnnotation.h"
#include "Import/TchShape.h"
#include "Import/TchMultiStair.h"
#include "Import/TchSymbol.h"
#include "Import/TchPipe.h"
#include "Import/TchElec.h"
#include "Import/TchHvac.h"

#include "Core/Log.h"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/AttribEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/DimensionEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/Face3DEntity.hpp"
#include "Core/Entity/Solid3DEntity.hpp"
#include "Core/Entity/MeshEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/SolidEntity.hpp"
#include "Core/Entity/SplineEntity.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Core/Entity/ViewportEntity.hpp"
#include "Core/Entity/MLineEntity.hpp"
#include "Core/Math/OCS.hpp"
#include "Scene/HatchPatternLibrary.h"
#include "Scene/Scene.h"
#include "Serialization/EntityIO.h"
#include "Serialization/JsonSerializer.h"

#include "Database/CadDatabase.h"
#include "Database/CadFileFormat.h"
#include "Database/UnknownObjects.h"
#include "Dwg/Read/DwgBitReader.h"
#include "Dwg/Read/DwgReader.h"
#include "Dwg/Write/DwgBitWriter.h"
#include "Dwg/Write/DwgWriter.h"
#include "Dxf/Read/DxfReader.h"
#include "Dxf/Write/DxfWriter.h"

#include <chrono>
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

        // 凸度的方向约定：DWG/DXF 的组码 42 与 Polyline::ComputeArc 现在**同一套**约定
        //（正凸度 = 逆时针弧 = 弧向右鼓，圆心在 A→B 左侧），所以这里不需要取反。
        //
        // ⚠ 曾经因为 `Polyline::ComputeArc` 把圆心与包角的方向双双写反，这里用 `-x`
        //   把弧掰回来；ComputeArc 修正后（沿弦镜像缺陷，与 V0.1.0 同一问题），
        //   取反反而会把弧再次镜像。**改 ComputeArc 的约定时必须同步这里。**
        double BulgeFromCad(double cadBulge) { return cadBulge; }
        double BulgeToCad(double bulge) { return bulge; }

        // 填充等二维实体的 OCS → 世界坐标；法向为 +Z 时不变，为 -Z 时左右镜像
        struct OcsMap
        {
            Math::OCS Ocs;
            bool      Identity  = true;
            double    Elevation = 0.0;

            OcsMap(const Dwg::XYZ& normal, double elevation)
                : Ocs(Math::BuildOCS({ normal.X, normal.Y, normal.Z })), Identity(IsDefaultNormal(normal)), Elevation(elevation) {}

            Math::Point3 Point(double x, double y) const
            {
                const Math::Point3 p{ x, y, Elevation };
                return Identity ? p : Math::OcsToWcs(Ocs, p);
            }

            // 向量在世界 XY 平面内的投影
            std::pair<double, double> Vector(double x, double y) const
            {
                if (Identity) return { x, y };
                return { Ocs.Ux.x * x + Ocs.Uy.x * y, Ocs.Ux.y * x + Ocs.Uy.y * y };
            }

            bool Mirrored() const { return !Identity && Ocs.Ux.x * Ocs.Uy.y - Ocs.Ux.y * Ocs.Uy.x < 0.0; }

            // DWG 凸度 → 世界坐标下 MiniCAD 的凸度（镜像时弧的转向相反）
            double Bulge(double cadBulge) const { return Mirrored() ? cadBulge : BulgeFromCad(cadBulge); }
        };

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
                                                       Dwg::NotificationHandler notify, ImportProgress progress = nullptr)
        {
            if (format == Dwg::CadFileFormat::Dwg)
            {
                Dwg::DwgReadOptions opt;
                opt.Notify   = std::move(notify);
                opt.Progress = std::move(progress);
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
            Importer(const Dwg::CadDatabase& db, Scene& scene, CadSource* source, ImportProgress progress = nullptr)
                : m_db(db), m_scene(scene), m_source(source), m_progress(std::move(progress)) {}

            // 块参照的 XCLIP 裁剪边界（扩展字典 ACAD_FILTER → SPATIAL 的 SPATIAL_FILTER），换算到块坐标系；
            // 未裁剪、裁剪未启用或无法解析时返回空。
            // SPATIAL_FILTER 数据：BS 点数、2RD 点（2 个点为矩形对角）、3BD 法向、3BD 原点、BS 启用、BS 前裁剪 [BD]、BS 后裁剪 [BD]、
            // 12 BD 插入逆矩阵、12 BD 边界变换（均为 3×4 行主序）；块坐标 = 插入逆矩阵 × 边界变换 × 点。
            // 前后裁剪平面与反向裁剪（R2015 起）不处理
            std::vector<Math::Point3> ReadClipBoundary(const Dwg::Entity& insert) const
            {
                auto entry = [&](Dwg::Handle dict, std::string_view name) -> Dwg::Handle {
                    const auto* d = m_db.FindAs<Dwg::CadDictionary>(dict);
                    if (!d) return Dwg::kNullHandle;
                    for (std::size_t i = 0; i < d->EntryNames.size() && i < d->EntryHandles.size(); ++i)
                        if (d->EntryNames[i] == name) return d->EntryHandles[i];
                    return Dwg::kNullHandle;
                };
                if (insert.XDictionaryHandle == Dwg::kNullHandle) return {};
                const auto* filter = m_db.FindAs<Dwg::UnknownObject>(entry(entry(insert.XDictionaryHandle, "ACAD_FILTER"), "SPATIAL"));
                if (!filter || filter->Raw.DxfName != "SPATIAL_FILTER") return {};

                std::vector<Dwg::XY> points;
                bool enabled = false;
                std::vector<double> matrices;   // 插入逆矩阵 + 边界变换，共 24 个
                const auto& raw = filter->Raw;
                if (raw.Dwg.Version != Dwg::CadVersion::Unknown && !raw.Dwg.Main.empty())
                {
                    Dwg::DwgBitReader r(raw.Dwg.Main, raw.Dwg.Version, Dwg::Codec::CodePage::Gbk);
                    const int count = r.ReadBitShort();
                    if (count < 2 || count > 100000) return {};
                    for (int i = 0; i < count; ++i) points.push_back(r.Read2RawDouble());
                    r.Read3BitDouble();
                    r.Read3BitDouble();
                    enabled = r.ReadBitShort() != 0;
                    if (r.ReadBitShort() != 0) r.ReadBitDouble();
                    if (r.ReadBitShort() != 0) r.ReadBitDouble();
                    for (int i = 0; i < 24; ++i) matrices.push_back(r.ReadBitDouble());
                    if (r.PositionInBits() > raw.Dwg.MainBits) return {};
                }
                else if (raw.Dxf.Version != Dwg::CadVersion::Unknown)
                {
                    bool front = false;
                    for (const auto& g : raw.Dxf.Groups)
                        switch (g.Code)
                        {
                        case 10:
                            if (g.Value.Is<Dwg::XYZ>()) { const auto p = g.Value.AsXYZ(); points.push_back({ p.X, p.Y }); }
                            else points.push_back({ g.Value.AsDouble(), 0.0 });
                            break;
                        case 20: if (!points.empty()) points.back().Y = g.Value.AsDouble(); break;
                        case 71: enabled = g.Value.AsInt() != 0; break;
                        case 72: front = g.Value.AsInt() != 0; break;
                        case 40:
                            if (front) front = false;   // 前裁剪距离
                            else matrices.push_back(g.Value.AsDouble());
                            break;
                        default: break;
                        }
                }
                if (!enabled || points.size() < 2 || matrices.size() != 24) return {};
                if (points.size() == 2)
                {
                    const Dwg::XY a = points[0], b = points[1];
                    points = { a, { b.X, a.Y }, b, { a.X, b.Y } };
                }
                // 3×4 行主序矩阵作用于点
                auto apply = [](const double* m, double& x, double& y, double& z) {
                    const double nx = m[0] * x + m[1] * y + m[2] * z + m[3];
                    const double ny = m[4] * x + m[5] * y + m[6] * z + m[7];
                    const double nz = m[8] * x + m[9] * y + m[10] * z + m[11];
                    x = nx; y = ny; z = nz;
                };
                auto valid = [](const double* m) {
                    return std::all_of(m, m + 12, [](double v) { return std::isfinite(v); }) && std::abs(m[0] * m[5] - m[1] * m[4]) > 1e-12;
                };
                const bool invOk = valid(matrices.data()), clipOk = valid(matrices.data() + 12);
                std::vector<Math::Point3> result;
                for (const auto& p : points)
                {
                    double x = p.X, y = p.Y, z = 0;
                    if (clipOk) apply(matrices.data() + 12, x, y, z);
                    if (invOk) apply(matrices.data(), x, y, z);
                    result.push_back({ x, y, 0.0 });
                }
                return result;
            }

            void Run()
            {
                ImportLineTypes();
                ImportMLineStyles();     // 多线样式（MLINE 的组码 340 引用它）
                ImportLayers();
                ImportTextStyles();
                ImportPointStyle();
                ImportLineTypeScale();
                // 门窗：先全部解码（模型空间与块内，天正图常把墙和门窗整体做成块），生成墙体时据此留出断口
                ForEachEntry<Dwg::BlockRecord>(m_db.BlockRecords(), [&](const Dwg::BlockRecord& rec) { CollectOpenings(rec); });
                if (const Dwg::BlockRecord* model = m_db.ModelSpace())
                    CollectOpenings(*model);
                ScanObjects();
                if (m_progress)
                {
                    ForEachEntry<Dwg::BlockRecord>(m_db.BlockRecords(), [&](const Dwg::BlockRecord& rec)
                    {
                        if (!IsLayoutBlock(rec.Name))
                            m_progressTotal += rec.Entities.size();
                    });
                    if (const Dwg::BlockRecord* model = m_db.ModelSpace())
                        m_progressTotal += model->Entities.size();
                }
                ImportBlocks();
                ImportLayouts();          // LAYOUT 表：布局名 + 纸张范围 + 归属键
                ImportModelSpace();
                ImportPaperSpaceEntities();  // 图纸空间实体（含 VIEWPORT）
                m_scene.ResolveAllInserts();
                m_scene.MarkDirty();
                RecordSource();

                for (const auto& [name, count] : m_skipped)
                    LOG_WARN("DWG/DXF 导入：不支持的实体 %s × %d 已跳过", name.c_str(), count);
                if (m_tianzhengWalls > 0)
                    LOG_WARN("TCH_WALL × %d 显示为普通轮廓；不修改时另存为原格式原版本可保留天正对象", m_tianzhengWalls);
                for (const auto& [name, count] : m_tchConverted)
                    LOG_WARN("%s × %d 显示为普通几何；不修改时另存为原格式原版本可保留天正对象", name.c_str(), count);
                for (const auto& [name, reasons] : m_tchFailures)
                    for (const auto& [reason, handles] : reasons)
                        LOG_WARN("%s × %d 无法恢复显示（如句柄 %llX）：%s", name.c_str(), static_cast<int>(handles.size()),
                                 static_cast<unsigned long long>(*handles.begin()), reason.c_str());
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

            // $PDMODE / $PDSIZE：POINT 实体的显示完全由这两个图纸级变量决定。
            //
            // 为什么必须读：POINT 实体自身**只有坐标**（组码 10/20/30 + 可选 50），
            // 不带任何"画成什么样、多大"的信息。不读这两个就只画得出写死的十字
            // （实测 sample_AC1032.dwg：$PDMODE=34、$PDSIZE=0.1，
            //  CAD 里是 ⊕（圆+十字），我们画出来是个尺寸也不对的十字）。
            void ImportPointStyle()
            {
                const Dwg::CadHeader& h = m_db.Header;
                m_scene.SetPointDisplayMode(static_cast<int>(h.PointDisplayMode));
                m_scene.SetPointDisplaySize(h.PointDisplaySize);
            }

            // 全局线型比例（$LTSCALE）：虚线实际周期的三层乘数之一。
            // 不读它，大尺寸图纸的虚线会密到退化成实线（见 Scene::SetLineTypeScale）。
            void ImportLineTypeScale()
            {
                const double lts = m_db.Header.LineTypeScale;

                // 护栏：某些版本的头部段位流解出来是错位的垃圾值（V0.0.1 实测 AC1021/AC1024
                // 得到过 1.5e-231 / 1.7e-77，只有 AC1018 给出真值）。垃圾值乘进周期比不读更糟，
                // 所以落在合理区间外就按 1 处理。
                // 区间取 [1e-4, 1e7]：正常图纸的 $LTSCALE 极少超出（1 是默认，上千很常见）。
                constexpr double kMin = 1e-4;
                constexpr double kMax = 1e7;
                if (!(lts >= kMin && lts <= kMax))
                {
                    if (lts != 0.0)
                        LOG_WARN("DWG/DXF 导入：$LTSCALE=%.6g 超出合理范围，按 1 处理", lts);
                    return;                       // Scene 默认 1.0
                }
                m_scene.SetLineTypeScale(lts);
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
                        // 冻结（标志位 1）与关闭一样不显示；MiniCAD 不区分冻结与关闭
                        const bool frozen = (static_cast<int>(src.Flags) & 1) != 0;
                        dst->SetVisible(src.IsOn && !frozen && (src.Color.IsTrueColor() || src.Color.Index() >= 0));
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

            // ── 多线样式（MLINESTYLE）────────────────────────────────────
            //
            // 每个多线实体用组码 340 引用一个 MLINESTYLE 对象，样式里存的是
            // 各条元素线相对中心线的偏移（组码 49）与颜色 / 线型。
            // 不导入样式的话，多线只能用内置 Standard —— 用户的墙线样式
            // （比如 5 条元素线）会被画成 2 条线。
            void ImportMLineStyles()
            {
                MLineStyleTable& table = m_scene.GetMLineStyleTable();
                for (const auto& [handle, object] : m_db.Objects())
                {
                    const auto* st = dynamic_cast<const Dwg::MLineStyle*>(object.get());
                    if (st == nullptr)
                        continue;

                    MLineStyleRecord rec;
                    rec.Name        = st->Name;
                    rec.Description = st->Description;
                    // 封口 / 连接：Flags 位（1 = 起点封口、2 = 终点封口、4 = 顶点连接线）
                    const int flags = static_cast<int>(st->Flags);
                    rec.StartCap = (flags & 1) != 0;
                    rec.EndCap   = (flags & 2) != 0;
                    rec.Joints   = (flags & 4) != 0;

                    for (const auto& el : st->Elements)
                    {
                        MLineElement e;
                        e.Offset = el.Offset;
                        // 只有元素线自己指定了颜色（非 ByLayer / ByBlock）才覆盖实体色
                        if (!el.Color.IsByLayer() && !el.Color.IsByBlock())
                        {
                            e.UseColor = true;
                            e.Color    = LayerColorToColor4(el.Color);
                        }
                        rec.Elements.push_back(e);
                    }
                    rec.Normalize();      // MLineEntity 依赖「偏移从大到小」
                    if (rec.Elements.empty())
                        continue;

                    const MLineStyleID id = table.Add(std::move(rec));
                    m_mlineStyles[handle] = id;
                }
                m_mlineStyles[Dwg::kNullHandle] = MLineStyleTable::StandardID;
            }

            MLineStyleID LookupMLineStyle(Dwg::Handle h) const
            {
                auto it = m_mlineStyles.find(h);
                return it == m_mlineStyles.end() ? MLineStyleTable::StandardID : it->second;
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

                ForEachEntry<Dwg::BlockRecord>(m_db.BlockRecords(), [&](const Dwg::BlockRecord& rec) { FillBlock(rec); });
            }

            // 转换块内实体。线图案等需要先知道所引用块的内容，会提前调用它，所以按句柄只填一次
            void FillBlock(const Dwg::BlockRecord& rec)
            {
                auto it = m_blocks.find(rec.ObjectHandle);
                if (it == m_blocks.end() || !m_filledBlocks.insert(rec.ObjectHandle).second)
                    return;
                BlockEntity* blk = m_scene.GetBlockTable().Find(it->second);
                if (blk == nullptr || !blk->IsEmpty())
                    return;
                for (Dwg::Handle h : DrawOrder(rec))
                    if (const auto* e = m_db.FindAs<Dwg::Entity>(h))
                        if (Tick(); auto conv = Convert(*e))
                        {
                            if (conv->IsKindOf<InsertEntity>())
                                m_scene.ResolveInsert(static_cast<InsertEntity&>(*conv));
                            blk->AddEntity(std::move(conv));
                        }
            }

            void ImportModelSpace()
            {
                const Dwg::BlockRecord* model = m_db.ModelSpace();
                if (model == nullptr)
                    return;
                for (Dwg::Handle h : DrawOrder(*model))
                {
                    const auto* e = m_db.FindAs<Dwg::Entity>(h);
                    if (e == nullptr)
                        continue;
                    Tick();
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

            // ── 图纸布局（LAYOUT）────────────────────────────────────────
            //
            // AutoCAD 的模型空间与每张布局各对应一个 BLOCK_RECORD：
            //   *Model_Space            模型页
            //   *Paper_Space            第 1 张布局
            //   *Paper_Space0、*Paper_Space1 …  第 2、3… 张
            // LAYOUT 对象（OBJECTS 段）把「布局名」与「图纸空间块记录」关联起来：
            //   AssociatedBlockHandle(330) → 该布局的图纸空间 BLOCK_RECORD
            //   PlotSettings.MaxExtents/… → 纸张范围
            //
            // 之前导入只处理模型空间，图纸空间实体（含 VIEWPORT）全部被丢弃 ——
            // 布局页自然是空白。
            void ImportLayouts()
            {
                std::vector<DrawingLayout> layouts;
                layouts.push_back(DrawingLayout{});      // 0 = 模型页

                // Layout 对象按 LayoutOrder 排；与块记录的对应关系靠 330。
                // 模型页在 DXF 里也可能是一个 LAYOUT 对象（组码 71 = 0，关联 *Model_Space），
                // 必须排除 —— 否则它会被当成一张普通布局，导出时又写一遍（实测会多出一张 "Model"）。
                std::vector<const Dwg::Layout*> layoutObjs;
                for (const auto& [handle, object] : m_db.Objects())
                {
                    const auto* lay = dynamic_cast<const Dwg::Layout*>(object.get());
                    if (lay == nullptr || lay->Name.empty())
                        continue;
                    // 关联块记录是 *Model_Space → 这是模型页，跳过
                    if (const auto* rec = m_db.FindAs<Dwg::BlockRecord>(lay->AssociatedBlockHandle);
                        rec != nullptr && Lower(rec->Name).rfind("*model_space", 0) == 0)
                        continue;
                    if (EqualsNoCase(lay->Name, "Model") || EqualsNoCase(lay->Name, "模型"))
                        continue;
                    layoutObjs.push_back(lay);
                }
                std::sort(layoutObjs.begin(), layoutObjs.end(),
                          [](const Dwg::Layout* a, const Dwg::Layout* b)
                          { return a->TabOrder < b->TabOrder; });


                for (const Dwg::Layout* lay : layoutObjs)
                {
                    // 去重：DXF 读取时会先 `CreateDefaults()` 预置 "Model" 与 "Layout1"，
                    // 文件里的 LAYOUT 再追加进来 —— 同名就会出现两次
                    // （实测：导出 3 个 LAYOUT，读回却得到 4 个，多出的是默认的 Layout1）。
                    //
                    // 保留哪一个：默认布局关联的 `*Paper_Space` **没有实体**，
                    // 文件里的布局关联的块才有内容。所以同名时保留块内实体数多的那个。
                    auto entityCountOf = [&](Dwg::Handle blk) -> size_t
                    {
                        const auto* rec = m_db.FindAs<Dwg::BlockRecord>(blk);
                        return rec != nullptr ? rec->Entities.size() : 0;
                    };
                    const bool dup = std::any_of(layouts.begin(), layouts.end(),
                        [&](const DrawingLayout& x) { return x.Name == lay->Name; });
                    if (dup)
                    {
                        // 已有同名项：若本条关联的块内容更多，替换掉原来那条
                        for (auto& x : layouts)
                        {
                            if (x.Name != lay->Name) continue;
                            if (entityCountOf(lay->AssociatedBlockHandle) > entityCountOf(static_cast<Dwg::Handle>(x.OwnerKey)))
                            {
                                x.OwnerKey = static_cast<uint64_t>(lay->AssociatedBlockHandle);
                                if (lay->MaxExtents.X > 0.0 && lay->MaxExtents.Y > 0.0)
                                {
                                    // MinExtents 可能是负数，不能强行归零（否则布局原点偏移）
                                    x.MinX = std::min(lay->MinExtents.X, 0.0);
                                    x.MinY = std::min(lay->MinExtents.Y, 0.0);
                                    x.MaxX = lay->MaxExtents.X; x.MaxY = lay->MaxExtents.Y;
                                }
                            }
                            break;
                        }
                        continue;
                    }

                    DrawingLayout dl;
                    dl.Name = lay->Name;
                    dl.Model = false;
                    dl.Order = static_cast<int>(layouts.size());
                    // 纸张范围：Layout 的 MaxExtents(15/25) 是纸张外框，
                    // 缺失时用 PlotSettings 的 PaperSize 兜底，再不行用 A3。
                    //
                    // ⚠ MinExtents **可能是负数**（实测「B地块地下室」图纸为
                    // (-3736.52, -1500.00)）—— 之前一律当成 0，布局原点就偏了
                    // 三千多单位，整张图看起来「显示不全」。必须按实际值取。
                    if (lay->MaxExtents.X > 0.0 && lay->MaxExtents.Y > 0.0)
                    {
                        dl.MinX = std::min(lay->MinExtents.X, 0.0);
                        dl.MinY = std::min(lay->MinExtents.Y, 0.0);
                        dl.MaxX = lay->MaxExtents.X;
                        dl.MaxY = lay->MaxExtents.Y;
                    }
                    // 归属键：图纸空间实体的 owner（330）就是这张布局的块记录句柄
                    dl.OwnerKey = static_cast<uint64_t>(lay->AssociatedBlockHandle);
                    layouts.push_back(std::move(dl));
                }

                if (layouts.size() > 1)
                    m_scene.SetLayouts(std::move(layouts));
            }

            // 导入图纸空间实体：遍历每个 *Paper_Space* 块记录，把其中的实体加进场景，
            // 并标记 InPaperSpace + LayoutKey（owner 句柄），供布局过滤使用。
            void ImportPaperSpaceEntities()
            {
                for (const auto& rec : m_db.BlockRecords()->Entries)
                {
                    const auto* record = m_db.FindAs<Dwg::BlockRecord>(rec);
                    if (record == nullptr)
                        continue;
                    const std::string lower = Lower(record->Name);
                    if (lower.rfind("*paper_space", 0) != 0)
                        continue;      // 只看图纸空间块

                    for (Dwg::Handle h : DrawOrder(*record))
                    {
                        const auto* e = m_db.FindAs<Dwg::Entity>(h);
                        if (e == nullptr)
                            continue;

                        // 跳过“代表纸张本身”的主视口（ShowsModel() == false）。
                        //
                        // 两个来源都靠这条清掉：
                        //   1. `CadDatabase::CreateDefaults()` 自动补的整页视口 ——
                        //      读取 DXF 时它在解析 OBJECTS 段之前就跑了，那时还没有 LAYOUT，
                        //      于是补了一个覆盖整张纸的视口。它不是文件内容，
                        //      算进来会让视口数比原图多 1（实测 6 → 7）。
                        //   2. 文件里本来就有的图纸空间主视口 —— 它代表纸张、
                        //      不显示模型内容，AutoCAD 里也不作为可见对象。
                        //
                        // 判据用**几何特征**（视图中心与框中心重合、视图高 = 框高），
                        // 与 ViewportEntity::ShowsModel 保持一致。
                        if (const auto* vp = dynamic_cast<const Dwg::Viewport*>(e))
                        {
                            constexpr double kEps = 1.0e-6;
                            // 判据一：视图中心与框中心重合、视图高 = 框高（文件里的纸张主视口）
                            const bool paperSized =
                                vp->Width > 0.0 && vp->Height > 0.0 && vp->ViewHeight > 0.0 &&
                                std::abs(vp->ViewCenter.X - vp->Center.X) <= kEps &&
                                std::abs(vp->ViewCenter.Y - vp->Center.Y) <= kEps &&
                                std::abs(vp->ViewHeight - vp->Height) <= kEps;
                            // 判据二：`CreateDefaults` 补的那个视口只设了 Width/Height/Center，
                            // 视图中心与视图高都是默认 0 —— 同样不是文件内容，必须跳过。
                            // 没有这条时，往返一轮视口会多 1（实测 3 → 4）。
                            const bool defaultStub =
                                vp->Width > 0.0 && vp->Height > 0.0 &&
                                std::abs(vp->ViewCenter.X) <= kEps &&
                                std::abs(vp->ViewCenter.Y) <= kEps &&
                                std::abs(vp->ViewHeight) <= kEps;
                            if (paperSized || defaultStub)
                                continue;
                        }

                        Tick();
                        if (auto conv = Convert(*e))
                        {
                            // 标记图纸空间归属：渲染 / 拾取据此只显示当前布局的实体
                            EntityAttr attr = conv->GetAttr();
                            attr.InPaperSpace = true;
                            attr.LayoutKey    = static_cast<uint64_t>(record->ObjectHandle);
                            conv->SetAttr(attr);

                            const Object::ObjectID id = conv->GetID();
                            m_scene.AddEntity(std::move(conv));
                            if (m_source != nullptr)
                                m_source->Links[id].Handle = h;
                        }
                    }
                }
            }

            // 遍历一遍非图形对象：各块的绘制顺序表（SORTENTSTABLE）、区域覆盖的边框开关（WIPEOUTVARIABLES）
            void ScanObjects()
            {
                for (const auto& [handle, object] : m_db.Objects())
                {
                    if (const auto* sort = dynamic_cast<const Dwg::SortEntitiesTable*>(object.get()))
                    {
                        auto& keys = m_sortKeys[sort->BlockOwnerHandle];
                        for (const auto& entry : sort->Entries)
                            keys[entry.EntityHandle] = entry.SortHandle;
                    }
                    else if (const auto* unknown = dynamic_cast<const Dwg::UnknownObject*>(object.get());
                             unknown != nullptr && unknown->Raw.DxfName == "WIPEOUTVARIABLES")
                        ReadWipeoutFrame(unknown->Raw);
                }
            }

            // WIPEOUTVARIABLES 只有一个字段：显示边框（BS / DXF 组码 70），即 WIPEOUTFRAME
            void ReadWipeoutFrame(const Dwg::RawObjectData& raw)
            {
                if (raw.Dxf.Version != Dwg::CadVersion::Unknown)
                {
                    for (const auto& g : raw.Dxf.Groups)
                        if (g.Code == 70)
                            m_wipeoutFrame = g.Value.AsInt() != 0;
                    return;
                }
                const auto& d = raw.Dwg.Main;
                if (raw.Dwg.MainBits < 2 || d.size() * 8 < raw.Dwg.MainBits)
                    return;
                auto bits = [&](std::uint64_t from, int count)
                {
                    int v = 0;
                    for (int i = 0; i < count; ++i)
                    {
                        const std::uint64_t k = from + i;
                        v = (v << 1) | ((d[k >> 3] >> (7 - (k & 7))) & 1);
                    }
                    return v;
                };
                switch (bits(0, 2))
                {
                case 0:   // 16 位，低字节在前
                    if (raw.Dwg.MainBits >= 18) m_wipeoutFrame = (bits(2, 8) | (bits(10, 8) << 8)) != 0;
                    break;
                case 1:   // 8 位
                    if (raw.Dwg.MainBits >= 10) m_wipeoutFrame = bits(2, 8) != 0;
                    break;
                case 2:  m_wipeoutFrame = false; break;   // 0
                default: m_wipeoutFrame = true;  break;   // 256
                }
            }

            // 绘制顺序：块（含模型空间）有 SORTENTSTABLE 时按排序句柄从小到大（表里没有的实体以自身句柄排序），
            // 否则按实体列表。区域覆盖只盖住先画的实体，顺序决定它遮住什么
            std::vector<Dwg::Handle> DrawOrder(const Dwg::BlockRecord& rec) const
            {
                const auto it = m_sortKeys.find(rec.ObjectHandle);
                if (it == m_sortKeys.end() || it->second.empty())
                    return rec.Entities;
                std::vector<std::pair<Dwg::Handle, Dwg::Handle>> keyed;
                keyed.reserve(rec.Entities.size());
                for (Dwg::Handle h : rec.Entities)
                {
                    const auto k = it->second.find(h);
                    keyed.emplace_back(k == it->second.end() ? h : k->second, h);
                }
                std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                std::vector<Dwg::Handle> order;
                order.reserve(keyed.size());
                for (const auto& [key, h] : keyed)
                    order.push_back(h);
                return order;
            }

            void CollectOpenings(const Dwg::BlockRecord& record)
            {
                for (Dwg::Handle h : record.Entities)
                {
                    const auto* unknown = m_db.FindAs<Dwg::UnknownEntity>(h);
                    Tch::TchOpening opening;
                    std::string reason;
                    if (unknown == nullptr || unknown->Raw.DxfName != "TCH_OPENING" || m_openings.contains(h))
                        continue;
                    if (!Tch::DecodeOpening(*unknown, opening, &reason))
                    {
                        m_tchFailures["TCH_OPENING"][reason].insert(h);
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

            // ── 文字锚点 + 对齐码的统一换算（TEXT / ATTRIB / ATTDEF 共用）──────
            //
            // DXF 语义：组码 72/73 非 0 时**以 11/21（第二对齐点）为准**，10/20 被忽略；
            // 其中 72 = 3(Aligned) / 5(Fit) 是**两端对齐** —— 文字在 10 与 11 **之间**拉伸铺满，
            // 视觉中心是两点**中点**。
            //
            // 【实测 · `test0.dwg` 轴号块 `_AXISO` 的 ATTRIB（全部 72=5）】
            //     10 = -2333969.50（圆心 +0.283 块单位）
            //     11 = -2334648.32（圆心 −0.283）
            //     中点 = -2334308.91 = **圆心** ✓
            // 取中点即与 CAD 观感一致；若像居中那样直接取 11，会偏 0.8 × 半径（V0.0.1 的 R67）。
            //
            // ⚠ 换算锚点**只是事情的一半**：对齐码必须同时交给排版引擎，否则文字仍从锚点
            //   **向右**排 ⇒ 偏出半个字宽（实测轴号 6/12 跑出圆圈）。
            struct TextPlacement
            {
                Math::Point3 pos;      // 交给实体的锚点
                int          hAlign;   // 组码 72 归到 MiniCAD 的 0 左 / 1 中 / 2 右
                int          vAlign;   // 组码 73/74 归到 0 基线 / 1 下 / 2 中 / 3 上
            };

            static TextPlacement ResolveTextPlacement(const Dwg::XYZ& insertPt,
                                                      const Dwg::XYZ& alignPt,
                                                      Dwg::TextHorizontalAlignment h,
                                                      Dwg::TextVerticalAlignmentType v)
            {
                TextPlacement r;
                const int h72 = static_cast<int>(h);
                const int v73 = static_cast<int>(v);
                // 4 = 中间（水平居中 + 竖直居中）；3 / 5 是两端对齐，锚点取中点后按居中显示
                r.hAlign = h72 == 1 || h72 == 3 || h72 == 4 || h72 == 5 ? 1 : h72 == 2 ? 2 : 0;
                r.vAlign = h72 == 4 ? 2 : h72 == 3 || h72 == 5 ? 0 : std::clamp(v73, 0, 3);

                const Math::Point3 ip = ToPoint(insertPt);
                const Math::Point3 ap = ToPoint(alignPt);

                const bool twoPoint = h == Dwg::TextHorizontalAlignment::Aligned ||
                                      h == Dwg::TextHorizontalAlignment::Fit;
                if (twoPoint)
                {
                    // 两端对齐 ⇒ 取中点（不做逐字拉伸，那要动排版引擎）
                    r.pos = { (ip.x + ap.x) * 0.5, (ip.y + ap.y) * 0.5, (ip.z + ap.z) * 0.5 };
                }
                else if (h != Dwg::TextHorizontalAlignment::Left ||
                         v != Dwg::TextVerticalAlignmentType::Baseline)
                {
                    // 72/73 非 0 ⇒ 以 11 为准。残缺文件会把 11 留成 (0,0)，
                    // 照字面生效会把文字摆到图纸原点，此时视为缺失、退回插入点。
                    const bool alignMissing =
                        std::abs(ap.x) < 1e-12 && std::abs(ap.y) < 1e-12 &&
                        (std::abs(ip.x) > 1e-12 || std::abs(ip.y) > 1e-12);
                    r.pos = alignMissing ? ip : ap;
                }
                else
                {
                    r.pos = ip;
                }
                return r;
            }

            // 属性定义 / 属性实例（ATTDEF / ATTRIB）的公共取值。
            // 属性文字不跟随样式，Core 侧固定用 Standard（与 V0.1.0 的显示一致）
            template <class T>
            std::unique_ptr<Entity> MakeAttrib(const Dwg::AttributeBase& a, std::string text)
            {
                const TextPlacement pl = ResolveTextPlacement(a.InsertPoint, a.AlignmentPoint,
                                                             a.HorizontalAlignment, a.VerticalAlignment);
                auto e = Make<T>(a, &a.Normal, a.Tag, std::move(text), pl.pos,
                                 static_cast<double>(a.Height), static_cast<double>(a.Rotation));
                auto& attrib = static_cast<T&>(*e);
                attrib.SetFlags(static_cast<std::uint16_t>(static_cast<int>(a.Flags)));
                attrib.SetAlign(pl.hAlign, pl.vAlign);
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
                        // 天正按原图字体的 textbox 排布；MiniCAD 显示时用的是场景中的样式（同名的 Standard 是 MiniCAD 自己的设置，
                        // 缺失的字体另有替代），按原图字体排布会让比例文字压到图名上，所以按实际显示的字体估算宽度
                        const TextStyleID nameStyle = LookupStyle(drawingName.NameStyle), scaleStyle = LookupStyle(drawingName.ScaleStyle);
                        auto extent = [&](const std::string& text, double height, bool scaleText) {
                            const TextStyleRecord& rec = m_scene.GetTextStyleTable().Resolve(scaleText ? scaleStyle : nameStyle);
                            const double factor = rec.WidthFactor > 0 ? rec.WidthFactor : 1.0;
                            auto e = Tch::Detail::DisplayExtent(rec.FontFile, text);
                            return Tch::Detail::TextExtent{e.Width * height * factor, e.Bottom * height};
                        };
                        ++m_tchConverted[unknown->Raw.DxfName];
                        const bool nameTtf = !m_scene.GetTextStyleTable().Resolve(nameStyle).IsShx();
                        const bool scaleTtf = !m_scene.GetTextStyleTable().Resolve(scaleStyle).IsShx();
                        return MakeTchGraphics(src, Tch::BuildDrawingName(drawingName, extent, nameTtf, scaleTtf), nameStyle, scaleStyle);
                    }
                    Tch::TchRectStair stair;
                    if (Tch::DecodeRectStair(*unknown, stair, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildRectStair(stair), LookupStyle(stair.TextStyle), std::nullopt, stair.ArrowLayer);
                    }
                    Tch::TchElevation elevation;
                    if (Tch::DecodeElevation(*unknown, elevation, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildElevation(elevation), LookupStyle(elevation.Style));
                    }
                    Tch::TchMultiLeader leader;
                    if (Tch::DecodeMultiLeader(*unknown, leader, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        const TextStyleID style = LookupStyle(leader.Style);
                        return MakeTchGraphics(src, Tch::BuildMultiLeader(leader, TchTextRules(leader.Style, style)), style);
                    }
                    Tch::TchComposing composing;
                    if (Tch::DecodeComposing(*unknown, composing, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildComposing(composing), LookupStyle(composing.Style));
                    }
                    Tch::TchDrawingIndex drawingIndex;
                    if (Tch::DecodeDrawingIndex(*unknown, drawingIndex, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildDrawingIndex(drawingIndex), LookupStyle(drawingIndex.Style), LookupStyle(drawingIndex.ScaleStyle));
                    }
                    Tch::TchNorthThumb north;
                    if (Tch::DecodeNorthThumb(*unknown, north, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildNorthThumb(north), LookupStyle(north.Style));
                    }
                    Tch::TchCut cut;
                    if (Tch::DecodeCut(*unknown, cut, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildCut(cut), TextStyleTable::StandardID);
                    }
                    Tch::TchSymmetry symmetry;
                    if (Tch::DecodeSymmetry(*unknown, symmetry, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildSymmetry(symmetry), TextStyleTable::StandardID);
                    }
                    Tch::TchRect tchRect;
                    if (Tch::DecodeRect(*unknown, tchRect, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildRect(tchRect), TextStyleTable::StandardID);
                    }
                    Tch::TchSlab slab;
                    if (Tch::DecodeSlab(*unknown, slab, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildSlab(slab), TextStyleTable::StandardID);
                    }
                    Tch::TchHandRail handRail;
                    if (Tch::DecodeHandRail(*unknown, handRail, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildHandRail(handRail), TextStyleTable::StandardID);
                    }
                    Tch::TchApron apron;
                    if (Tch::DecodeApron(*unknown, apron, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildApron(apron), TextStyleTable::StandardID);
                    }
                    Tch::TchCornerWindow cornerWindow;
                    if (Tch::DecodeCornerWindow(*unknown, cornerWindow, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildCornerWindow(cornerWindow), LookupStyle(cornerWindow.Style));
                    }
                    Tch::TchLineStair lineStair;
                    if (Tch::DecodeLineStair(*unknown, lineStair, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildLineStair(lineStair), TextStyleTable::StandardID);
                    }
                    Tch::TchArcStair arcStair;
                    if (Tch::DecodeArcStair(*unknown, arcStair, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildArcStair(arcStair), TextStyleTable::StandardID);
                    }
                    Tch::TchAscent ascent;
                    if (Tch::DecodeAscent(*unknown, ascent, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildAscent(ascent), TextStyleTable::StandardID);
                    }
                    Tch::TchBalcony balcony;
                    if (Tch::DecodeBalcony(*unknown, balcony, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildBalcony(balcony), TextStyleTable::StandardID);
                    }
                    Tch::TchCurveStair curveStair;
                    if (Tch::DecodeCurveStair(*unknown, curveStair, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildCurveStair(curveStair), TextStyleTable::StandardID);
                    }
                    Tch::TchModi modi;
                    if (Tch::DecodeModi(*unknown, modi, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildModi(modi), TextStyleTable::StandardID);
                    }
                    Tch::TchParallelStair parallelStair;
                    if (Tch::DecodeParallelStair(*unknown, parallelStair, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildParallelStair(parallelStair), LookupStyle(parallelStair.TextStyle), std::nullopt, parallelStair.ArrowLayer);
                    }
                    Tch::TchCornerStair cornerStair;
                    if (Tch::DecodeCornerStair(*unknown, cornerStair, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildCornerStair(cornerStair), LookupStyle(cornerStair.TextStyle), std::nullopt, cornerStair.ArrowLayer);
                    }
                    Tch::TchDoubleMulStair doubleMulStair;
                    if (Tch::DecodeDoubleMulStair(*unknown, doubleMulStair, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildDoubleMulStair(doubleMulStair), LookupStyle(doubleMulStair.TextStyle), std::nullopt, doubleMulStair.ArrowLayer);
                    }
                    Tch::TchScissorsStair scissorsStair;
                    if (Tch::DecodeScissorsStair(*unknown, scissorsStair, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildScissorsStair(scissorsStair), LookupStyle(scissorsStair.TextStyle), std::nullopt, scissorsStair.ArrowLayer);
                    }
                    Tch::TchCrossStair crossStair;
                    if (Tch::DecodeCrossStair(*unknown, crossStair, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildCrossStair(crossStair), LookupStyle(crossStair.TextStyle), std::nullopt, crossStair.ArrowLayer);
                    }
                    Tch::TchTriangleStair triangleStair;
                    if (Tch::DecodeTriangleStair(*unknown, triangleStair, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildTriangleStair(triangleStair), LookupStyle(triangleStair.TextStyle), std::nullopt, triangleStair.ArrowLayer);
                    }
                    Tch::TchRectCornerStair rectCornerStair;
                    if (Tch::DecodeRectCornerStair(*unknown, rectCornerStair, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildRectCornerStair(rectCornerStair), LookupStyle(rectCornerStair.TextStyle), std::nullopt, rectCornerStair.ArrowLayer);
                    }
                    Tch::TchAutoStair autoStair;
                    if (Tch::DecodeAutoStair(*unknown, autoStair, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildAutoStair(autoStair), LookupStyle(autoStair.TextStyle), std::nullopt, autoStair.ArrowLayer);
                    }
                    Tch::TchMountRoof mountRoof;
                    if (Tch::DecodeMountRoof(*unknown, mountRoof, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildMountRoof(mountRoof), TextStyleTable::StandardID);
                    }
                    Tch::TchCuspRoof cuspRoof;
                    if (Tch::DecodeCuspRoof(*unknown, cuspRoof, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildCuspRoof(cuspRoof), TextStyleTable::StandardID);
                    }
                    Tch::TchSlopeRoof slopeRoof;
                    if (Tch::DecodeSlopeRoof(*unknown, slopeRoof, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildSlopeRoof(slopeRoof), TextStyleTable::StandardID);
                    }
                    Tch::TchDualRoof dualRoof;
                    if (Tch::DecodeDualRoof(*unknown, dualRoof, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildDualRoof(dualRoof), TextStyleTable::StandardID);
                    }
                    Tch::TchOpeningSlot openingSlot;
                    if (Tch::DecodeOpeningSlot(*unknown, openingSlot, &nativeReason))
                    {
                        // 门窗套：按所属门窗的位置与所在墙的两侧墙面画截面矩形
                        const auto it = m_openings.find(openingSlot.Opening);
                        const auto* host = it == m_openings.end() ? nullptr : m_db.FindAs<Dwg::UnknownEntity>(it->second.Wall);
                        Tch::TchWall slotWall;
                        if (host != nullptr && Tch::DecodeWall(*host, slotWall))
                        {
                            const auto& o = it->second;
                            // 门窗方向与墙方向相同时，门窗左侧即墙的左侧
                            const double wa = std::atan2(slotWall.End.Y - slotWall.Start.Y, slotWall.End.X - slotWall.Start.X);
                            const bool same = std::cos(o.Angle - wa) >= 0;
                            const double faceLeft = same ? slotWall.LeftWidth : slotWall.RightWidth;
                            const double faceRight = same ? slotWall.RightWidth : slotWall.LeftWidth;
                            ++m_tchConverted[unknown->Raw.DxfName];
                            return MakeTchGraphics(src, Tch::BuildOpeningSlot(openingSlot, o, faceLeft, faceRight), TextStyleTable::StandardID);
                        }
                        nativeReason = "opening slot owner or host wall is unknown";
                    }
                    Tch::TchStep tchStep;
                    if (Tch::DecodeStep(*unknown, tchStep, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildStep(tchStep), TextStyleTable::StandardID);
                    }
                    Tch::TchWallPatch wallPatch;
                    if (Tch::DecodeWallPatch(*unknown, wallPatch, &nativeReason))
                    {
                        // 所附着墙的墙体四边形：轮廓落在墙内的部分不画
                        std::vector<std::array<Tch::TchGraphics::Point, 4>> bodies;
                        for (Dwg::Handle h : wallPatch.Walls)
                        {
                            const auto* host = m_db.FindAs<Dwg::UnknownEntity>(h);
                            Tch::TchWall w;
                            if (host == nullptr || !Tch::DecodeWall(*host, w) || w.Angle != 0) continue;   // 弧墙未处理
                            const double dx = w.End.X - w.Start.X, dy = w.End.Y - w.Start.Y, len = std::hypot(dx, dy);
                            if (len < 1e-9) continue;
                            const double nx = -dy / len, ny = dx / len;
                            bodies.push_back({Tch::TchGraphics::Point{w.Start.X + nx * w.LeftWidth, w.Start.Y + ny * w.LeftWidth},
                                              {w.End.X + nx * w.LeftWidth, w.End.Y + ny * w.LeftWidth},
                                              {w.End.X - nx * w.RightWidth, w.End.Y - ny * w.RightWidth},
                                              {w.Start.X - nx * w.RightWidth, w.Start.Y - ny * w.RightWidth}});
                        }
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildWallPatch(wallPatch, bodies), TextStyleTable::StandardID);
                    }
                    Tch::TchText tchText;
                    if (Tch::DecodeText(*unknown, tchText, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        const TextStyleID textStyle = LookupStyle(tchText.Style);
                        const auto rules = TchTextRules(tchText.Style, textStyle);
                        return MakeTchGraphics(src, tchText.Multiline ? Tch::BuildMultilineText(tchText, rules)
                                                                      : Tch::BuildText(tchText, rules), textStyle);
                    }
                    // 天正给排水：管线、管件、设备、阀门、井（代理图形都是空的，按私有布局与天正给排水的分解结果显示）
                    Tch::TchPipe pipe;
                    if (Tch::DecodePipe(*unknown, pipe, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        if (pipe.Width > 0)                         // 双线管
                            return MakeTchGraphics(src, Tch::BuildDoubleLinePipe(pipe), TextStyleTable::StandardID, std::nullopt, pipe.CenterLayer);
                        const TextStyleID labelStyle = LookupStyle(pipe.LabelStyle);
                        const auto rules = TchTextRules(pipe.LabelStyle, labelStyle);
                        return MakeTchGraphics(src, Tch::BuildPipe(pipe, rules), labelStyle,
                                               std::nullopt, pipe.LabelLayer);
                    }
                    if (Tch::DecodePipeFitting(*unknown, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return nullptr;                             // 管件不显示（分解结果为空）
                    }
                    Tch::TchPipeBlock pipeBlock;
                    if (Tch::DecodeEquipment(*unknown, pipeBlock, &nativeReason) || Tch::DecodePipeValve(*unknown, pipeBlock, &nativeReason))
                    {
                        if (auto converted = ConvertPipeBlock(src, pipeBlock, nativeReason))
                        {
                            ++m_tchConverted[unknown->Raw.DxfName];
                            return converted;
                        }
                    }
                    if (auto converted = ConvertPipeAnnotation(src, *unknown, nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return converted;
                    }
                    // 天正电气：桥架、桥架配件（边线在对象自身的图层，中心线在 CABLETRAY_DOTE 图层）；折断线
                    if (Tch::TchCableTray tray; Tch::DecodeCableTray(*unknown, tray, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildCableTray(tray), TextStyleTable::StandardID, std::nullopt, LayerByName("CABLETRAY_DOTE"));
                    }
                    if (Tch::TchCableFitting fitting; Tch::DecodeCableFitting(*unknown, fitting, &nativeReason))
                    {
                        std::vector<Tch::TchGraphics::Segment> trays;
                        for (Dwg::Handle h : fitting.Connected)
                        {
                            Tch::TchCableTray t;
                            if (const auto* e = m_db.FindAs<Dwg::UnknownEntity>(h); e && Tch::DecodeCableTray(*e, t))
                                trays.push_back({{t.Start.X, t.Start.Y}, {t.End.X, t.End.Y}});
                        }
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildCableFitting(fitting, trays), TextStyleTable::StandardID, std::nullopt,
                                               LayerByName("CABLETRAY_DOTE"));
                    }
                    // 天正暖通：风管、风管管件（边线在自身图层，中心线、管端线在由自身图层名推出的图层）
                    if (Tch::TchHvacDuct duct; Tch::DecodeHvacDuct(*unknown, duct, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        const auto [center, endLine] = DuctLayers(src);
                        return MakeTchGraphics(src, Tch::BuildHvacDuct(duct), TextStyleTable::StandardID, std::nullopt, center, endLine);
                    }
                    if (Tch::TchHvacFitting ductFitting; Tch::DecodeHvacFitting(*unknown, ductFitting, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        const auto [center, endLine] = DuctLayers(src);
                        return MakeTchGraphics(src, Tch::BuildHvacFitting(ductFitting), TextStyleTable::StandardID, std::nullopt, center, endLine);
                    }
                    if (Tch::TchRupture rupture; Tch::DecodeRupture(*unknown, rupture, &nativeReason))
                    {
                        ++m_tchConverted[unknown->Raw.DxfName];
                        return MakeTchGraphics(src, Tch::BuildRupture(rupture), TextStyleTable::StandardID);
                    }
                    Tch::TchPathArray pathArray;
                    if (Tch::DecodePathArray(*unknown, pathArray, &nativeReason))
                    {
                        if (auto converted = ConvertPathArray(src, pathArray, nativeReason))
                        {
                            ++m_tchConverted[unknown->Raw.DxfName];
                            return converted;
                        }
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
                        double arcX = 0, arcY = 0, arcR = 0;
                        if (wall.ArcCenter(arcX, arcY, arcR))
                        {
                            // 弧墙：两侧为同心圆弧（逆时针弧左侧在内），门窗处按轴线弧长断开；端部连接量未用（样例为 0）
                            std::string arcName = "*U_TCH_" + std::to_string(src.ObjectHandle);
                            while (m_scene.GetBlockTable().Get(arcName)) arcName += "_";
                            const auto arcBlockId = m_scene.GetBlockTable().Create(arcName);
                            auto* arcBlock = m_scene.GetBlockTable().Find(arcBlockId);
                            arcBlock->SetFlags(BlockEntity::Anonymous);
                            const double a0 = std::atan2(wall.Start.Y - arcY, wall.Start.X - arcX);
                            const double ccw = wall.Angle > 0 ? 1.0 : -1.0;
                            // 轴线上自起点的角度区间 [0, |Angle|]，门窗按中点角度 ± 半宽 / 半径断开
                            std::vector<std::pair<double, double>> arcCuts;
                            if (auto it = m_wallOpenings.find(src.ObjectHandle); it != m_wallOpenings.end())
                                for (const auto& o : it->second)
                                {
                                    const double t = std::remainder((std::atan2(o.Position.Y - arcY, o.Position.X - arcX) - a0) * ccw, 2 * std::numbers::pi);
                                    const double half = o.Width / 2 / arcR;
                                    arcCuts.emplace_back(t - half, t + half);
                                }
                            std::sort(arcCuts.begin(), arcCuts.end());
                            const double span = std::abs(wall.Angle);
                            for (const double radius : {arcR - ccw * wall.LeftWidth, arcR + ccw * wall.RightWidth})
                            {
                                if (radius <= 0) continue;
                                auto addArc = [&](double t0, double t1) {
                                    double s0 = a0 + ccw * t0, s1 = a0 + ccw * t1;
                                    if (ccw < 0) std::swap(s0, s1);
                                    arcBlock->AddEntity(Make<ArcEntity>(src, nullptr, Math::Point3{arcX, arcY, wall.Start.Z}, radius, s0, s1));
                                };
                                double cursor = 0;
                                for (const auto& [c0, c1] : arcCuts)
                                {
                                    if (c1 <= cursor || c0 >= span) continue;
                                    if (c0 > cursor) addArc(cursor, c0);
                                    cursor = std::max(cursor, c1);
                                }
                                if (cursor < span) addArc(cursor, span);
                            }
                            ++m_tianzhengWalls;
                            return Make<InsertEntity>(src, nullptr, static_cast<InsertEntity::BlockID>(arcBlockId), arcName, Math::Point3{0, 0, 0});
                        }
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
                            if (to - from < 1e-9) return;   // 连接量之和不小于墙长：该侧边退化为点
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
                    if (Tch::TchWall curtain; unknown->Raw.DxfName == "TCH_CURTAIN_WALL" && Tch::DecodeWallPrefix(*unknown, curtain))
                    {
                        // 幕墙（代理图形为空，按天正 2014 分解结果近似）：墙标志含 0x4 时为玻璃幕墙，沿墙厚三等分画 4 条线、两端封口；
                        // 否则按普通墙画两条侧边线。都在门窗处断开；与相邻墙的交接（斜接、连接量）不处理
                        const bool glass = (curtain.Flags & 0x4) != 0;
                        const double width = curtain.LeftWidth + curtain.RightWidth;
                        std::vector<double> offsets;    // 相对轴线的偏移，左侧为正
                        if (glass)
                            for (int k = 0; k <= 3; ++k) offsets.push_back(curtain.LeftWidth - width * k / 3);
                        else
                            offsets = {curtain.LeftWidth, -curtain.RightWidth};
                        std::string name = "*U_TCH_" + std::to_string(src.ObjectHandle);
                        while (m_scene.GetBlockTable().Get(name)) name += "_";
                        const auto blockId = m_scene.GetBlockTable().Create(name);
                        auto* block = m_scene.GetBlockTable().Find(blockId);
                        block->SetFlags(BlockEntity::Anonymous);
                        const double z = curtain.Start.Z;
                        double arcX = 0, arcY = 0, arcR = 0;
                        const bool arc = curtain.ArcCenter(arcX, arcY, arcR);
                        const double a0 = arc ? std::atan2(curtain.Start.Y - arcY, curtain.Start.X - arcX) : 0;
                        const double ccw = curtain.Angle > 0 ? 1.0 : -1.0;
                        const double dx = curtain.End.X - curtain.Start.X, dy = curtain.End.Y - curtain.Start.Y;
                        const double length = std::hypot(dx, dy);
                        const double ux = dx / length, uy = dy / length;
                        // 沿轴线的参数：直墙为距起点的长度，弧墙为自起点的圆心角
                        const double span = arc ? std::abs(curtain.Angle) : length;
                        std::vector<std::pair<double, double>> cuts;
                        if (auto it = m_wallOpenings.find(src.ObjectHandle); it != m_wallOpenings.end())
                            for (const auto& o : it->second)
                            {
                                if (arc)
                                {
                                    const double t = std::remainder((std::atan2(o.Position.Y - arcY, o.Position.X - arcX) - a0) * ccw, 2 * std::numbers::pi);
                                    cuts.emplace_back(t - o.Width / 2 / arcR, t + o.Width / 2 / arcR);
                                }
                                else
                                {
                                    const double t = (o.Position.X - curtain.Start.X) * ux + (o.Position.Y - curtain.Start.Y) * uy;
                                    cuts.emplace_back(t - o.Width / 2, t + o.Width / 2);
                                }
                            }
                        std::sort(cuts.begin(), cuts.end());
                        // 轴线参数 t、偏移 off 处的点
                        auto at = [&](double t, double off) {
                            if (arc)
                            {
                                const double r = arcR - ccw * off, a = a0 + ccw * t;
                                return Math::Point3{arcX + r * std::cos(a), arcY + r * std::sin(a), z};
                            }
                            return Math::Point3{curtain.Start.X + ux * t - uy * off, curtain.Start.Y + uy * t + ux * off, z};
                        };
                        auto addPiece = [&](double off, double t0, double t1) {
                            if (t1 - t0 < 1e-9) return;
                            if (!arc)
                            {
                                block->AddEntity(Make<LineEntity>(src, nullptr, at(t0, off), at(t1, off)));
                                return;
                            }
                            const double r = arcR - ccw * off;
                            if (r <= 0) return;
                            double s0 = a0 + ccw * t0, s1 = a0 + ccw * t1;
                            if (ccw < 0) std::swap(s0, s1);
                            block->AddEntity(Make<ArcEntity>(src, nullptr, Math::Point3{arcX, arcY, z}, r, s0, s1));
                        };
                        for (const double off : offsets)
                        {
                            double cursor = 0;
                            for (const auto& [c0, c1] : cuts)
                            {
                                if (c1 <= cursor || c0 >= span) continue;
                                if (c0 > cursor) addPiece(off, cursor, c0);
                                cursor = std::max(cursor, c1);
                            }
                            if (cursor < span) addPiece(off, cursor, span);
                        }
                        if (glass)
                            for (const double t : {0.0, span})
                                block->AddEntity(Make<LineEntity>(src, nullptr, at(t, curtain.LeftWidth), at(t, -curtain.RightWidth)));
                        ++m_tchConverted[unknown->Raw.DxfName];
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
                        m_tchFailures[unknown->Raw.DxfName][reason].insert(src.ObjectHandle);
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
                {
                    // 角点是 OCS 坐标（Z 为标高）。SolidEntity 不处理法向，这里换到世界坐标：
                    // 法向为 -Z 时左右镜像，不换算会画到关于 Y 轴对称的位置（远离原点的图纸会把全图范围撑得极大）
                    if (!IsDefaultNormal(e->Normal))
                    {
                        const OcsMap map(e->Normal, e->FirstCorner.Z);
                        auto P = [&](const Dwg::XYZ& p) { return map.Point(p.X, p.Y); };
                        return Make<SolidEntity>(src, nullptr, P(e->FirstCorner), P(e->SecondCorner), P(e->FourthCorner), P(e->ThirdCorner));
                    }
                    return Make<SolidEntity>(src, &e->Normal, ToPoint(e->FirstCorner), ToPoint(e->SecondCorner),
                                             ToPoint(e->FourthCorner), ToPoint(e->ThirdCorner));
                }
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

                if (auto* e = dynamic_cast<const Dwg::Hatch*>(&src))
                    return ConvertHatch(*e);

                if (auto* e = dynamic_cast<const Dwg::Wipeout*>(&src))
                    return ConvertWipeout(*e);

                if (auto* e = dynamic_cast<const Dwg::Spline*>(&src))
                    return ConvertSpline(*e);

                // 属性定义（ATTDEF）：块定义里的可填字段。此前一律 Skip() ⇒ 带属性的块
                // 打开后看不到字段（V54 的轴号块 `_AXISO` 就是块定义里的圆 + 一个 ATTDEF）。
                // ⚠ ATTRIB（INSERT 上的具体取值）不在这里：它在 INSERT 分支里随块引用一起读出。
                if (auto* e = dynamic_cast<const Dwg::AttributeDefinition*>(&src))
                    return MakeAttrib<AttDefEntity>(*e, e->Value);

                if (dynamic_cast<const Dwg::AttributeEntity*>(&src))
                    return Skip(src);   // 游离的 ATTRIB（不在 INSERT 里）：无归属，跳过

                if (auto* e = dynamic_cast<const Dwg::TextEntity*>(&src))
                {
                    // 对齐方式：左 / 中 / 右与竖直四种按对齐点显示（MiniCAD 按自己的字宽对齐）；
                    // “中间”（72 = 4）按水平居中、竖直居中；“对齐”“布满”（3、5）从插入点左对齐近似
                    const int h72 = static_cast<int>(e->HorizontalAlignment);
                    const int v73 = static_cast<int>(e->VerticalAlignment);
                    const int hAlign = h72 == 1 || h72 == 4 ? 1 : h72 == 2 ? 2 : 0;
                    const int vAlign = h72 == 4 ? 2 : h72 == 3 || h72 == 5 ? 0 : std::clamp(v73, 0, 3);
                    const bool aligned = hAlign != 0 || vAlign != 0;
                    auto text = Make<TextEntity>(src, &e->Normal, ToPoint(aligned ? e->AlignmentPoint : e->InsertPoint),
                        e->Value, static_cast<float>(e->Height), static_cast<float>(e->Rotation), LookupStyle(e->StyleHandle));
                    if (text)
                    {
                        auto& t = static_cast<TextEntity&>(*text);
                        t.SetAlignment(hAlign, vAlign);
                        t.SetWidthFactor(e->WidthFactor);   // CAD 里 TEXT 以自身的宽度因子为准，样式的只是新建时的默认值
                    }
                    return text;
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
                    if (auto clip = ReadClipBoundary(*e); !clip.empty())
                    {
                        ins.SetClipBoundary(std::move(clip));
                        // $XCLIPFRAME：是否**显示**裁剪框。
                        //   None = 0            ⇒ 不显示
                        //   DisplayAndPlot = 1  ⇒ 显示并打印
                        //   DisplayNotPlot = 2  ⇒ **显示**但不打印
                        // 只有 None 才不显示 —— 这里容易理解反：判定的是"显示"维度，
                        // 不是"打印"维度（曾经写成 != DisplayNotPlot 导致边框整批不显示）。
                        ins.SetClipFrameVisible(m_db.Header.ExternalReferenceClippingBoundaryType !=
                                                Dwg::XClipFrameType::None);
                    }

                    // ── ATTRIB：块引用的属性文字（轴号、图签字段等）────────────────
                    //
                    // 此前这里**完全没处理** `e->Attributes`，而 `Convert` 又把
                    // `Dwg::AttributeBase`（ATTDEF/ATTRIB）整个 Skip() 掉 ⇒ 属性文字**整批消失**。
                    // 【实测 · `test0.dwg` 的轴号块 `_AXISO`（圆 + 一个属性）】
                    //   | 侧 | _AXISO 实例 | 带 ATTRIB 的 |
                    //   | DWG 真值 | 417 | 417（100%）|
                    //   | 导入后（修前）| 241 | 0 |
                    //   属性值正是轴号本身（「1」「24」「E」…）⇒ 界面上只剩一个个空圆圈。
                    //
                    // 【坐标】ATTRIB 的 `InsertPoint`/`AlignmentPoint` 存的已经是**世界坐标**
                    // （AutoCAD 在 INSERT 里就把属性定位算好了），所以这里**不能再做块变换**，
                    // `InsertEntity::Draw` 也因此按 sink 直接画它（见那里的注释）。
                    for (Dwg::Handle ah : e->Attributes)
                    {
                        const auto* a = m_db.FindAs<Dwg::AttributeEntity>(ah);
                        if (a == nullptr)
                            continue;
                        ins.AddAttrib(std::unique_ptr<AttribEntity>(
                            static_cast<AttribEntity*>(MakeAttrib<AttribEntity>(*a, a->Value).release())));
                    }
                    return ent;
                }

                // 标注：用它的匿名块（*D…）还原外观，不再是可编辑的标注
                if (auto* e = dynamic_cast<const Dwg::Dimension*>(&src))
                {
                    auto it = m_blocks.find(e->BlockHandle);
                    if (it == m_blocks.end())
                        return Skip(src);
                    const BlockEntity* blk = m_scene.GetBlockTable().Find(it->second);
                    // 组码 12（InsertionPoint）是标注匿名块的**平移量**：
                    // 匿名几何建在世界坐标上，AutoCAD 靠 `local = worldGeom + code12`
                    // 把它挪回该出现的位置。写死 (0,0,0) 会把整条标注画在匿名块自身的
                    // 坐标上（实测《总说明.dwg》：实绘宽度 910056 而非 7525，错位 92 万单位）。
                    const Math::Point3 insPoint = ToPoint(e->InsertionPoint);
                    return Make<InsertEntity>(src, nullptr, static_cast<InsertEntity::BlockID>(it->second),
                                              blk ? blk->GetName() : std::string(), insPoint);
                }

                // 图纸空间视口（VIEWPORT）：布局页里靠它把模型内容显示出来
                if (auto* e = dynamic_cast<const Dwg::Viewport*>(&src))
                {
                    auto base = Make<ViewportEntity>(src, nullptr, ToPoint(e->Center),
                                                     e->Width, e->Height);
                    auto* vp = static_cast<ViewportEntity*>(base.get());
                    // ⚠ 组码 12/22（ViewCenter）是 **DCS** 坐标，不是 WCS。
                    //    DCS 相对 WCS 旋转了 +TwistAngle，必须反向旋转 -Twist
                    //    才是模型里的真实位置。直接当 WCS 用时，Twist≠0 的
                    //    视口内容会被画到几十万单位外 —— 视口是空框
                    //    （实测 `01-曲陵路.dwg` 5 个 Twist=95.7° 的浮动视口）。
                    vp->SetTwist(e->TwistAngle);
                    vp->SetViewCenterDcs(Math::Point3{ e->ViewCenter.X, e->ViewCenter.Y, 0.0 },
                                         e->TwistAngle);
                    vp->SetViewHeight(e->ViewHeight);
                    vp->SetViewportId(e->ActiveStatus);
                    return base;
                }

                // 多线（MLINE）
                //
                // 结构：组码 72 = 顶点数、73 = 元素线数；每个顶点后跟
                //   11/21/31 位置（中心线上的点）、12/22/32 方向、13/23/33 斜接，
                //   然后是**每条元素线一段**：74 = 参数个数、41 × n = 参数、75 = 面积填充参数。
                //
                // 实测 `sample_AC1015_ascii.dxf` 的 MLINE：
                //   72=8（顶点）、73=2（元素线），每段 74=2、41=0.75 / 41=0.0
                //   ⇒ 41 的第一个值是**该元素线相对中心线的偏移距离**（0.75 / -0.75）。
                // 所以可以直接沿用「路径 + 样式偏移」的模型，不必逐顶点存偏移。
                if (auto* e = dynamic_cast<const Dwg::MLine*>(&src))
                {
                    if (e->Vertices.size() < 2)
                        return Skip(src);

                    // 中心线路径 = 各顶点的 11/21/31
                    std::vector<Math::Point3> path;
                    path.reserve(e->Vertices.size());
                    for (const auto& v : e->Vertices)
                        path.push_back(Math::Point3{ v.Position.X, v.Position.Y, v.Position.Z });

                    // 样式：优先用文件里的 MLINESTYLE（组码 340 指向的对象）
                    MLineStyleRecord style = m_scene.GetMLineStyleTable().Resolve(
                        LookupMLineStyle(e->StyleHandle));

                    // 若顶点里带的元素线数与样式不符，以顶点为准重建（偏移取第一个顶点的参数）
                    const auto& segs0 = e->Vertices.front().Segments;
                    if (!segs0.empty() && style.Elements.size() != segs0.size())
                    {
                        style.Elements.clear();
                        for (const auto& sg : segs0)
                        {
                            MLineElement el;
                            el.Offset = sg.Parameters.empty() ? 0.0 : sg.Parameters.front();
                            style.Elements.push_back(el);
                        }
                        style.Normalize();
                    }
                    if (style.Elements.size() < 2)
                        return Skip(src);       // 少于 2 条元素线画不出多线

                    const bool closed = (static_cast<int>(e->Flags) & 2) != 0;
                    const MLineJustify justify =
                        (e->Justification == Dwg::MLineJustification::Top)      ? MLineJustify::Top
                      : (e->Justification == Dwg::MLineJustification::Bottom)   ? MLineJustify::Bottom
                                                                                : MLineJustify::Zero;
                    auto g = Make<MLineEntity>(src, &e->Normal, std::move(path), std::move(style),
                                               justify, e->ScaleFactor > 0.0 ? e->ScaleFactor : 1.0,
                                               closed);

                    // ── 保留每顶点每元素线的精确参数 ──────────────────────────
                    //
                    // 这些参数带着 AutoCAD 早已算好的斜接信息。最关键的是
                    // **T 形 / 拐角接头的端头推移**：实测 sample_AC1032.dwg 的
                    // 横线起点，两条元素线沿路径方向各被推出 2.1309（= 竖线宽度），
                    // 正好停在竖线内侧边上。只用「路径 + 样式偏移」重算会丢掉这个推移，
                    // 横线就一直穿到竖线中心线上 —— 看起来是「穿透」而不是「接头」。
                    if (auto* ml = static_cast<MLineEntity*>(g.get()))
                    {
                        MLineEntity::VertexParams vp;
                        vp.reserve(e->Vertices.size());
                        for (const auto& v : e->Vertices)
                        {
                            std::vector<std::vector<double>> perElem;
                            perElem.reserve(v.Segments.size());
                            for (const auto& sg : v.Segments)
                                perElem.push_back(sg.Parameters);
                            vp.push_back(std::move(perElem));
                        }
                        ml->SetVertexParams(std::move(vp));
                    }
                    return g;
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
            // Layer = 1 的图元放到对象引用的符号图层 symbolLayer（如楼梯箭头的 DIM_SYMB），Layer = 2 的放到 secondLayer，颜色随层
            std::unique_ptr<Entity> MakeTchGraphics(const Dwg::Entity& src, const Tch::TchGraphics& g, TextStyleID textStyle,
                                                    std::optional<TextStyleID> secondStyle = std::nullopt,
                                                    Dwg::Handle symbolLayer = Dwg::kNullHandle, Dwg::Handle secondLayer = Dwg::kNullHandle)
            {
                auto onLayer = [&](Entity& e, int layer) {
                    if (layer != 1 && layer != 2) return;
                    const auto it = m_layers.find(layer == 1 ? symbolLayer : secondLayer);
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
                {
                    auto line = Make<LineEntity>(src, nullptr, pt(l.A), pt(l.B));
                    if (g.LineColor != 256)
                    {
                        EntityAttr attr = line->GetAttr();
                        attr.Color = EntityColor::FromAci(static_cast<std::uint16_t>(g.LineColor));
                        line->SetAttr(attr);
                    }
                    block->AddEntity(std::move(line));
                }
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
                    TextStyleID style = t.Style == 1 ? secondStyle.value_or(textStyle) : textStyle;
                    if (t.WidthFactor > 0) style = WidthVariant(style, t.WidthFactor);
                    auto text = Make<MTextEntity>(src, nullptr, style, t.Value, pt(t.Center), t.Height, t.Rotation, 0.0);
                    auto& mt = static_cast<MTextEntity&>(*text);
                    mt.SetAttachment(static_cast<MTextAttachment>(t.Attachment));
                    onLayer(mt, t.Layer);
                    EntityAttr attr = mt.GetAttr();
                    attr.Color = t.Color == 0 ? EntityColor::ByBlock() : t.Color == 256 ? EntityColor::ByLayer()
                                                                     : EntityColor::FromAci(static_cast<std::uint16_t>(t.Color));
                    mt.SetAttr(attr);
                    block->AddEntity(std::move(text));
                }
                return Make<InsertEntity>(src, nullptr, static_cast<InsertEntity::BlockID>(blockId), name, Math::Point3{0, 0, 0});
            }

            // 天正暖通风管类的中心线、管端线图层：自身图层名（如 DUCT-排烟管）去掉末尾的“管”后加“中心线”“管端线”
            std::pair<Dwg::Handle, Dwg::Handle> DuctLayers(const Dwg::Entity& src)
            {
                const auto* layer = m_db.FindAs<Dwg::Layer>(src.LayerHandle);
                if (!layer) return {Dwg::kNullHandle, Dwg::kNullHandle};
                std::string base = layer->Name;
                if (const std::string tail = "管"; base.ends_with(tail)) base.erase(base.size() - tail.size());
                return {LayerByName(base + "中心线"), LayerByName(base + "管端线")};
            }

            // 原图中按名称找图层（如天正电气桥架中心线所在的 CABLETRAY_DOTE）；找不到时为空句柄，按对象自身的图层显示
            Dwg::Handle LayerByName(const std::string& name)
            {
                if (name.empty()) return Dwg::kNullHandle;
                const auto* layer = m_db.FindTableEntry<Dwg::Layer>(m_db.Layers(), name);
                return layer ? layer->ObjectHandle : Dwg::kNullHandle;
            }

            // 天正文字（TCH_TEXT 与给排水的标注）的显示规则，按原图样式的字体与 _TCH 扩展数据判断（场景中同名的 Standard 是 MiniCAD 自己的设置）
            Tch::TchTextStyleRule TchTextRules(Dwg::Handle styleHandle, TextStyleID textStyle)
            {
                const TextStyleRecord& rec = m_scene.GetTextStyleTable().Resolve(textStyle);
                const auto* dwgStyle = m_db.FindAs<Dwg::TextStyle>(styleHandle);
                std::string font;
                for (char ch : dwgStyle ? dwgStyle->Filename : rec.FontFile) font.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                Tch::TchTextStyleRule rule;
                rule.TrueType = !rec.IsShx();
                rule.SymbolFont = !font.starts_with("gbenor") && !font.starts_with("gbeitc");
                rule.StyleWidth = dwgStyle && dwgStyle->Width > 0 ? dwgStyle->Width : rec.WidthFactor > 0 ? rec.WidthFactor : 1.0;
                std::string bigFont;
                for (char ch : dwgStyle ? dwgStyle->BigFontFilename : std::string()) bigFont.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                if (bigFont.starts_with("hztxt")) rule.WideHeight = 1.0;
                // 样式上 _TCH 扩展数据的前两个实数：西文字高比例、西文宽度比例
                if (dwgStyle)
                    for (const auto& x : dwgStyle->ExtendedDataList)
                    {
                        const auto* app = m_db.FindAs<Dwg::AppId>(x.AppIdHandle);
                        if (!app || app->Name != "_TCH") continue;
                        std::vector<double> reals;
                        for (const auto& r : x.Records)
                            if (const auto* v = std::get_if<double>(&r.Value); v && r.Code == 1040) reals.push_back(*v);
                        if (reals.size() >= 2 && reals[0] > 0 && reals[1] > 0 && reals[0] < 10 && reals[1] < 10)
                        {
                            rule.AsciiHeight = reals[0];
                            rule.AsciiWidth = reals[1];
                        }
                    }
                return rule;
            }

            // 天正给排水的标注类（管线文字、立管标注、井标注、多管标注）与法兰；不是这些类型时返回空且不改 reason
            std::unique_ptr<Entity> ConvertPipeAnnotation(const Dwg::Entity& src, const Dwg::UnknownEntity& unknown, std::string& reason)
            {
                auto rule = [&](Dwg::Handle style, TextStyleID id) { return TchTextRules(style, id); };
                auto pipeAt = [&](Dwg::Handle h, Tch::TchPipe& pipe) {
                    const auto* e = m_db.FindAs<Dwg::UnknownEntity>(h);
                    return e != nullptr && Tch::DecodePipe(*e, pipe);
                };
                if (Tch::TchPipeText t; Tch::DecodePipeText(unknown, t, &reason))
                {
                    const TextStyleID style = LookupStyle(t.Style);
                    return MakeTchGraphics(src, Tch::BuildPipeText(t, rule(t.Style, style)), style);
                }
                if (Tch::TchVPipeDim d; Tch::DecodeVPipeDim(unknown, d, &reason))
                {
                    const TextStyleID style = LookupStyle(d.Style);
                    return MakeTchGraphics(src, Tch::BuildVPipeDim(d, rule(d.Style, style)), style);
                }
                if (Tch::TchWellDim d; Tch::DecodeWellDim(unknown, d, &reason))
                {
                    const auto* wellEntity = m_db.FindAs<Dwg::UnknownEntity>(d.Well);
                    Tch::TchPipeBlock well;
                    if (!wellEntity || !Tch::DecodeEquipment(*wellEntity, well) || wellEntity->Raw.DxfName != "TCH_WELL")
                    {
                        reason = "well dimension target well is missing";
                        return nullptr;
                    }
                    const TextStyleID style = LookupStyle(d.Style);
                    return MakeTchGraphics(src, Tch::BuildWellDim(d, well.WellGround, well.WellInvert, rule(d.Style, style)), style);
                }
                if (Tch::TchPipeMultDim d; Tch::DecodePipeMultDim(unknown, d, &reason))
                {
                    const double len = std::hypot(d.P2.X - d.P1.X, d.P2.Y - d.P1.Y);
                    const double ux = (d.P2.X - d.P1.X) / len, uy = (d.P2.Y - d.P1.Y) / len;
                    std::vector<std::string> labels;
                    std::vector<std::optional<Tch::TchGraphics::Point>> crossings;
                    for (const Dwg::Handle h : d.Pipes)
                    {
                        Tch::TchPipe pipe;
                        if (!pipeAt(h, pipe)) continue;
                        char number[32];
                        std::snprintf(number, sizeof number, "%g", pipe.Diameter);
                        labels.push_back(pipe.LabelPrefix + number + pipe.LabelSuffix);
                        crossings.push_back(Tch::PipeCrossing(pipe, d.P1, ux, uy));
                    }
                    const TextStyleID style = LookupStyle(d.Style);
                    return MakeTchGraphics(src, Tch::BuildPipeMultDim(d, labels, crossings, rule(d.Style, style)), style);
                }
                if (Tch::TchFlange f; Tch::DecodeFlange(unknown, f, &reason))
                {
                    // 管宽与中心线图层取自所连的双线管
                    Tch::TchPipe pipe;
                    for (const Dwg::Handle h : f.Pipes)
                        if (pipeAt(h, pipe) && pipe.Width > 0) break;
                    return MakeTchGraphics(src, Tch::BuildFlange(f, pipe.Width), TextStyleTable::StandardID, std::nullopt, pipe.CenterLayer);
                }
                return nullptr;
            }

            // 天正给排水的设备、阀门、井：所引用图块的一个块参照，块内图层 0 的图元随对象图层
            std::unique_ptr<Entity> ConvertPipeBlock(const Dwg::Entity& src, const Tch::TchPipeBlock& b, std::string& reason)
            {
                for (const Dwg::Handle h : b.Blocks)
                {
                    const auto* rec = m_db.FindAs<Dwg::BlockRecord>(h);
                    if (!rec) continue;
                    FillBlock(*rec);                                // 图块可能排在所在块之后
                    const auto it = m_blocks.find(h);
                    const auto* def = it == m_blocks.end() ? nullptr : m_scene.GetBlockTable().Find(it->second);
                    if (!def) continue;
                    auto unit = Make<InsertEntity>(src, nullptr, static_cast<InsertEntity::BlockID>(it->second), def->GetName(),
                                                   Math::Point3{b.X, b.Y, 0});
                    auto& insert = static_cast<InsertEntity&>(*unit);
                    insert.SetScale({b.ScaleX, b.ScaleY, std::abs(b.ScaleX)});
                    insert.SetRotation(b.Rotation);
                    m_scene.ResolveInsert(insert);
                    // 与其他天正对象一致，放进以对象句柄命名的匿名块
                    std::string name = "*U_TCH_" + std::to_string(src.ObjectHandle);
                    while (m_scene.GetBlockTable().Get(name)) name += "_";
                    const auto blockId = m_scene.GetBlockTable().Create(name);
                    auto* group = m_scene.GetBlockTable().Find(blockId);
                    group->SetFlags(BlockEntity::Anonymous);
                    group->AddEntity(std::move(unit));
                    return Make<InsertEntity>(src, nullptr, static_cast<InsertEntity::BlockID>(blockId), name, Math::Point3{0, 0, 0});
                }
                reason = "pipe object block is missing";
                return nullptr;
            }

            // 线图案：每个单元是图案块的一个块参照（缩放不等时块内圆弧显示为椭圆弧，与天正分解结果相同），合成一个匿名块
            std::unique_ptr<Entity> ConvertPathArray(const Dwg::Entity& src, const Tch::TchPathArray& a, std::string& reason)
            {
                if (const auto* rec = m_db.FindAs<Dwg::BlockRecord>(a.Block)) FillBlock(*rec);   // 图案块可能排在所在块之后
                const auto block = m_blocks.find(a.Block);
                const auto* def = block == m_blocks.end() ? nullptr : m_scene.GetBlockTable().Find(block->second);
                if (!def) { reason = "path array pattern block is missing"; return nullptr; }
                const AABB box = def->GetBoundingBox();
                if (!(box.Max.x > box.Min.x && box.Max.y > box.Min.y)) { reason = "path array pattern block is empty"; return nullptr; }
                const auto units = Tch::BuildPathArray(a, box.Min.x, box.Min.y, box.Max.x, box.Max.y);
                // 每段都短于一个单元时天正不显示任何内容（8#楼各图中的挡土墙线图案，天正 2014 分解结果为空），生成空块
                std::string name = "*U_TCH_" + std::to_string(src.ObjectHandle);
                while (m_scene.GetBlockTable().Get(name)) name += "_";
                const auto blockId = m_scene.GetBlockTable().Create(name);
                auto* group = m_scene.GetBlockTable().Find(blockId);
                group->SetFlags(BlockEntity::Anonymous);
                for (const auto& u : units)
                {
                    auto unit = Make<InsertEntity>(src, nullptr, static_cast<InsertEntity::BlockID>(block->second), def->GetName(),
                                                   Math::Point3{u.X, u.Y, 0});
                    auto& insert = static_cast<InsertEntity&>(*unit);
                    insert.SetScale({u.ScaleX, u.ScaleY, 1});
                    insert.SetRotation(u.Rotation);
                    m_scene.ResolveInsert(insert);
                    group->AddEntity(std::move(unit));
                }
                return Make<InsertEntity>(src, nullptr, static_cast<InsertEntity::BlockID>(blockId), name, Math::Point3{0, 0, 0});
            }

            std::unique_ptr<Entity> ConvertOpening(const Dwg::Entity& src, const Tch::TchOpening& o)
            {
                if (o.Type == Tch::TchOpening::Kind::Group)
                {
                    // 组合门窗：各子门窗按普通门窗显示，另加组合编号（天正 2014 分解结果核对：两樘门与编号文字）
                    std::vector<std::unique_ptr<Entity>> parts;
                    for (const auto& part : o.Parts)
                        if (auto converted = ConvertOpening(src, part)) parts.push_back(std::move(converted));
                    if (!o.Label.empty())
                    {
                        Tch::TchGraphics label;
                        const double nominal = o.LabelHeight * o.Scale;
                        Tch::Detail::AddRuns(label, o.Label, nominal, o.LabelX, o.LabelY - 0.425 * nominal, 0, 1);
                        if (auto text = MakeTchGraphics(src, label, LookupStyle(o.Style))) parts.push_back(std::move(text));
                    }
                    if (parts.empty()) return nullptr;
                    std::string name = "*U_TCH_OPENING_" + std::to_string(src.ObjectHandle);
                    while (m_scene.GetBlockTable().Get(name)) name += "_";
                    const auto blockId = m_scene.GetBlockTable().Create(name);
                    auto* group = m_scene.GetBlockTable().Find(blockId);
                    group->SetFlags(BlockEntity::Anonymous);
                    for (auto& part : parts)
                    {
                        if (auto* insert = dynamic_cast<InsertEntity*>(part.get())) m_scene.ResolveInsert(*insert);
                        group->AddEntity(std::move(part));
                    }
                    return Make<InsertEntity>(src, nullptr, static_cast<InsertEntity::BlockID>(blockId), name, Math::Point3{0, 0, 0});
                }
                if (o.Type == Tch::TchOpening::Kind::Band)
                {
                    const auto* bandHost = m_db.FindAs<Dwg::UnknownEntity>(o.Wall);
                    Tch::TchWall bandWall;
                    if (bandHost == nullptr || !Tch::DecodeWall(*bandHost, bandWall))
                    {
                        m_tchFailures["TCH_OPENING"]["band window host wall is unknown"].insert(src.ObjectHandle);
                        return nullptr;
                    }
                    const double angle = std::atan2(bandWall.End.Y - bandWall.Start.Y, bandWall.End.X - bandWall.Start.X);
                    return MakeTchGraphics(src, Tch::BuildBandWindow(o, angle, bandWall.LeftWidth, bandWall.RightWidth), LookupStyle(o.Style));
                }
                const auto* host = m_db.FindAs<Dwg::UnknownEntity>(o.Wall);
                Tch::TchWall wall;
                const bool hasWall = host != nullptr && Tch::DecodeWall(*host, wall);
                // 幕墙上的门窗：幕墙与墙共用前缀，从中取墙厚（8#楼平面图、详图的幕墙窗与天正 2014 分解结果核对）
                Tch::TchWall curtain;
                const bool hasCurtain = !hasWall && host != nullptr && Tch::DecodeWallPrefix(*host, curtain);
                const Tch::TchWall* sides = hasWall ? &wall : hasCurtain ? &curtain : nullptr;
                if (o.Type == Tch::TchOpening::Kind::Hole)
                {
                    // 洞口（8#楼详图的分解结果核对）：门洞两端各一条横穿墙厚的线；没有所在墙时按门窗自身的厚度居中
                    const double ux = std::cos(o.Angle), uy = std::sin(o.Angle);
                    double nx = -uy, ny = ux, left = o.Thickness / 2, right = o.Thickness / 2;
                    if (sides != nullptr)
                    {
                        const double wl = std::hypot(sides->End.X - sides->Start.X, sides->End.Y - sides->Start.Y);
                        nx = -(sides->End.Y - sides->Start.Y) / wl; ny = (sides->End.X - sides->Start.X) / wl;
                        left = sides->LeftWidth; right = sides->RightWidth;
                    }
                    Tch::TchGraphics g;
                    for (const double t : {-o.Width / 2, o.Width / 2})
                    {
                        const double x = o.Position.X + ux * t, y = o.Position.Y + uy * t;
                        g.Lines.push_back({{x + nx * left, y + ny * left}, {x - nx * right, y - ny * right}});
                    }
                    return MakeTchGraphics(src, g, LookupStyle(o.Style));
                }
                const auto block = m_blocks.find(o.Block2D);
                if (block == m_blocks.end())
                {
                    m_tchFailures["TCH_OPENING"]["opening 2D block is missing"].insert(src.ObjectHandle);
                    return nullptr;
                }
                double depth = o.Width;
                if (o.Type == Tch::TchOpening::Kind::Window)
                {
                    // 窗的 Y 缩放为墙厚；没有所在墙（句柄为空）时为门窗自身的厚度（8#楼平面图独立窗的分解结果核对）
                    if (sides != nullptr) depth = sides->LeftWidth + sides->RightWidth;
                    else if (o.Wall == Dwg::kNullHandle && o.Thickness > 0) depth = o.Thickness;
                    else
                    {
                        m_tchFailures["TCH_OPENING"]["window host wall thickness is unknown"].insert(src.ObjectHandle);
                        return nullptr;
                    }
                }
                const auto* def = m_scene.GetBlockTable().Find(block->second);
                auto result = Make<InsertEntity>(src, nullptr, static_cast<InsertEntity::BlockID>(block->second),
                    def ? def->GetName() : std::string(), Math::Point3{o.Position.X, o.Position.Y, 0});
                auto& insert = static_cast<InsertEntity&>(*result);
                const double yScale = o.MirrorY() ? -depth : depth;
                insert.SetScale({o.MirrorX() ? -o.Width : o.Width, yScale, o.Width});
                insert.SetRotation(o.Angle);
                if (o.Type != Tch::TchOpening::Kind::Door || !hasWall || o.InGroup)
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
                // 逐顶点起止宽度（DXF 组码 40/41）：顶点 i 的宽度作用于「i → i+1」这一段，
                // 两者不同即为锥形段（如 `sample_AC1032.dwg` 句柄 35B：首段 0.2→0.1、末段 0.1→0.5）。
                // 此前只取 `ConstantWidth`（组码 43）⇒ 这些宽度整批丢失，整条多段线画成等宽。
                std::vector<double>       startW;
                std::vector<double>       endW;
                for (const auto& v : e.Vertices)
                {
                    pts.push_back({ v.Location.X, v.Location.Y, e.Elevation });
                    bulges.push_back(v.Bulge);
                    startW.push_back(v.StartWidth);
                    endW.push_back(v.EndWidth);
                }
                const bool closed = Any(e.Flags & Dwg::LwPolylineFlags::Closed);
                if (closed)
                    pts.push_back(pts.front());   // 闭合：末点回到起点，最后一个 bulge 属于闭合段
                else
                    bulges.pop_back();
                auto ent = Make<PolylineEntity>(e, &e.Normal, std::move(pts), std::move(bulges));
                auto& pl = static_cast<PolylineEntity&>(*ent);
                pl.SetWidth(e.ConstantWidth);

                // 数组按段数对齐即可：顶点 i 的宽度正好对应第 i 段。
                //   · 开放：段数 = 顶点数-1，末顶点的宽度被丢弃（它后面没有段）——与 DXF 语义一致。
                //   · 闭合：补了重复末点后段数 = 顶点数，最后一段（原末顶点 → 首顶点）
                //     正好取原末顶点的宽度，也就是 DXF 里「末点宽度作用于闭合段」的约定。
                startW.resize(static_cast<size_t>(pl.GetPolyline().SegCount()), 0.0);
                endW.resize(static_cast<size_t>(pl.GetPolyline().SegCount()), 0.0);
                pl.SetSegmentWidths(std::move(startW), std::move(endW));
                return ent;
            }

            // 填充：边界是 OCS 坐标（标高 Elevation），按法向换到世界坐标。图案线族用填充自带的定义——
            // 它已按填充的比例、角度换算到 OCS（天正“钢筋混凝土”等自定义图案在图案库里没有），所以场景里比例取 1、角度取 0
            std::unique_ptr<Entity> ConvertHatch(const Dwg::Hatch& h)
            {
                const OcsMap map(h.Normal, h.Elevation);
                std::vector<HatchLoop> loops;
                for (const auto& path : h.Paths)
                {
                    HatchLoop loop = ConvertHatchPath(path, map);
                    if (!loop.Empty())
                        loops.push_back(std::move(loop));
                }
                if (loops.empty())
                    return Skip(h);

                HatchPattern pattern;
                pattern.Name        = h.Pattern.Name.empty() ? std::string("SOLID") : h.Pattern.Name;
                pattern.Description = h.Pattern.Description;
                pattern.Solid       = h.IsSolid || h.GradientColor.Enabled;   // 渐变色填充按实心显示
                double scale = 1.0, angleDeg = 0.0;
                if (!pattern.Solid)
                {
                    for (const auto& line : h.Pattern.Lines)
                        pattern.Families.push_back(FamilyOf(line, map));
                    if (pattern.Families.empty())
                    {
                        // 没有存线族（少见）：按名称查图案库，比例与角度用填充的
                        const HatchPattern* lib = HatchPatternLibrary::Instance().Find(pattern.Name);
                        if (lib == nullptr)
                            return Skip(h);
                        pattern  = *lib;
                        scale    = h.PatternScale;
                        angleDeg = h.PatternAngle * 180.0 / kPi;
                    }
                }
                auto ent = Make<HatchEntity>(h, nullptr, std::move(loops), std::move(pattern));
                auto& hatch = static_cast<HatchEntity&>(*ent);
                hatch.SetScale(scale);
                hatch.SetAngle(angleDeg);
                return ent;
            }

            // 图案线：角度、基点、偏移都是 OCS 里的最终值（偏移是相邻两条线之间的平移向量，不是 .pat 里沿线 / 垂直线的分量）
            static HatchLineFamily FamilyOf(const Dwg::HatchPatternLine& line, const OcsMap& map)
            {
                auto [dx, dy] = map.Vector(std::cos(line.Angle), std::sin(line.Angle));
                const double len = std::hypot(dx, dy);
                if (len > 1e-12) { dx /= len; dy /= len; }
                const auto [ox, oy] = map.Vector(line.Offset.X, line.Offset.Y);
                const Math::Point3 base = map.Point(line.BasePoint.X, line.BasePoint.Y);
                HatchLineFamily f;
                f.AngleDeg = std::atan2(dy, dx) * 180.0 / kPi;
                f.OffsetX  = base.x;
                f.OffsetY  = base.y;
                f.DeltaX   = ox * dx + oy * dy;
                f.Spacing  = -ox * dy + oy * dx;
                f.Dashes   = line.DashLengths;
                return f;
            }

            // 一条边界路径 → 一个边界环：相接的直线、圆弧、椭圆弧合成一条多段线边，样条单独成边
            static HatchLoop ConvertHatchPath(const Dwg::HatchBoundaryPath& path, const OcsMap& map)
            {
                HatchLoop loop;
                std::vector<Math::Point3> pts;
                std::vector<double>       bulges;
                auto flush = [&]
                {
                    if (pts.size() >= 2)
                        loop.Edges.push_back(HatchEdge::MakePoly(Polyline(std::move(pts), std::move(bulges))));
                    pts.clear();
                    bulges.clear();
                };
                auto finite = [](const Math::Point3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
                // 追加一段（世界坐标、MiniCAD 凸度）；起点与上一段终点不重合时用直线接上
                auto add = [&](const Math::Point3& a, const Math::Point3& b, double bulge)
                {
                    if (!finite(a) || !finite(b) || !std::isfinite(bulge))
                        return;
                    if (pts.empty() || std::abs(pts.back().x - a.x) > 1e-9 || std::abs(pts.back().y - a.y) > 1e-9)
                    {
                        if (!pts.empty())
                            bulges.push_back(0.0);
                        pts.push_back(a);
                    }
                    bulges.push_back(bulge);
                    pts.push_back(b);
                };
                // 从 s 转过 sweep（逆时针为正）的圆弧，超过半圆时分两段
                auto addArc = [&](double cx, double cy, double r, double s, double sweep)
                {
                    const int n = std::abs(sweep) > kPi ? 2 : 1;
                    const double step = sweep / n;
                    for (int i = 0; i < n; ++i)
                    {
                        const double a0 = s + step * i, a1 = a0 + step;
                        add(map.Point(cx + r * std::cos(a0), cy + r * std::sin(a0)),
                            map.Point(cx + r * std::cos(a1), cy + r * std::sin(a1)), map.Bulge(std::tan(step / 4.0)));
                    }
                };
                // 弧的起止角：逆时针时从 start 到 end；顺时针时 DWG 里存的是角度的相反数（ezdxf 同此约定，样例图核对过首尾相接）
                auto sweepOf = [](double start, double end, bool ccw, double& s) -> double
                {
                    s = ccw ? start : -start;
                    const double e = ccw ? end : -end;
                    double d = std::fmod(ccw ? e - s : s - e, kTwoPi);
                    if (d < 0.0) d += kTwoPi;
                    if (d < 1e-9) d = kTwoPi;   // 起止相同：整圆
                    return ccw ? d : -d;
                };

                for (const auto& edge : path.Edges)
                {
                    if (const auto* pl = dynamic_cast<const Dwg::HatchBoundaryPathPolyline*>(edge.get()))
                    {
                        const auto& v = pl->Vertices;   // Z 为凸度
                        for (std::size_t i = 0; i + 1 < v.size(); ++i)
                            add(map.Point(v[i].X, v[i].Y), map.Point(v[i + 1].X, v[i + 1].Y), map.Bulge(v[i].Z));
                        if (pl->IsClosed && v.size() >= 2)
                            add(map.Point(v.back().X, v.back().Y), map.Point(v.front().X, v.front().Y), map.Bulge(v.back().Z));
                    }
                    else if (const auto* l = dynamic_cast<const Dwg::HatchBoundaryPathLine*>(edge.get()))
                        add(map.Point(l->Start.X, l->Start.Y), map.Point(l->End.X, l->End.Y), 0.0);
                    else if (const auto* a = dynamic_cast<const Dwg::HatchBoundaryPathArc*>(edge.get()))
                    {
                        double s = 0.0;
                        const double sweep = sweepOf(a->StartAngle, a->EndAngle, a->CounterClockWise, s);
                        if (a->Radius > 1e-12)
                            addArc(a->Center.X, a->Center.Y, a->Radius, s, sweep);
                    }
                    else if (const auto* el = dynamic_cast<const Dwg::HatchBoundaryPathEllipse*>(edge.get()))
                    {
                        // 椭圆弧离散成折线（起止值按参数角处理）
                        double s = 0.0;
                        const double sweep = sweepOf(el->StartAngle, el->EndAngle, el->CounterClockWise, s);
                        const double mx = el->MajorAxisEndPoint.X, my = el->MajorAxisEndPoint.Y;
                        const double ra = std::hypot(mx, my), rb = ra * el->RadiusRatio;
                        if (ra < 1e-12)
                            continue;
                        const double ca = mx / ra, sa = my / ra;
                        auto at = [&](double t)
                        {
                            const double x = ra * std::cos(t), y = rb * std::sin(t);
                            return map.Point(el->Center.X + x * ca - y * sa, el->Center.Y + x * sa + y * ca);
                        };
                        const int n = std::max(8, static_cast<int>(std::ceil(std::abs(sweep) / (kPi / 36.0))));
                        for (int i = 0; i < n; ++i)
                            add(at(s + sweep * i / n), at(s + sweep * (i + 1) / n), 0.0);
                    }
                    else if (const auto* sp = dynamic_cast<const Dwg::HatchBoundaryPathSpline*>(edge.get()))
                    {
                        flush();
                        Spline spline;
                        spline.Degree = sp->Degree;
                        for (const auto& p : sp->ControlPoints)
                        {
                            spline.ControlPoints.push_back(map.Point(p.X, p.Y));
                            if (sp->IsRational)
                                spline.Weights.push_back(p.Z);   // 有理样条的权重放在 Z
                        }
                        spline.Knots = sp->Knots;
                        if (sp->IsRational)
                            spline.Flags |= SplineFlag_Rational;
                        if (sp->IsPeriodic)
                            spline.Flags |= SplineFlag_Periodic;
                        if (!spline.HasControlData())
                        {
                            if (sp->FitPoints.size() < 2)
                                continue;
                            std::vector<Math::Point3> fit;
                            for (const auto& p : sp->FitPoints)
                                fit.push_back(map.Point(p.X, p.Y));
                            spline = Spline(std::move(fit));
                        }
                        loop.Edges.push_back(HatchEdge::MakeSpline(std::move(spline)));
                    }
                }
                flush();
                return loop;
            }

            // 区域覆盖：边界是图像坐标（像素中心为整数，原点在左上角、Y 向下，整幅图像为 Size 个像素），
            // 世界坐标 = 插入点 + UVector·(x + 0.5) + VVector·(高 − y − 0.5)。没有裁剪边界时为整幅图像
            std::unique_ptr<Entity> ConvertWipeout(const Dwg::Wipeout& w)
            {
                const double sx = w.Size.X > 0.0 ? w.Size.X : 1.0;
                const double sy = w.Size.Y > 0.0 ? w.Size.Y : 1.0;
                std::vector<Dwg::XY> clip = w.ClipBoundaryVertices;
                if (clip.size() > 3 && clip.front().X == clip.back().X && clip.front().Y == clip.back().Y)
                    clip.pop_back();
                if (!w.ClippingState || clip.size() < 2)
                    clip = { { -0.5, -0.5 }, { sx - 0.5, sy - 0.5 } };
                if (clip.size() == 2)   // 两个点是矩形的对角
                    clip = { clip[0], { clip[1].X, clip[0].Y }, clip[1], { clip[0].X, clip[1].Y } };

                std::vector<Math::Point3> pts;
                pts.reserve(clip.size());
                for (const auto& p : clip)
                {
                    const double u = p.X + 0.5, v = sy - 0.5 - p.Y;
                    const Math::Point3 q{ w.InsertPoint.X + w.UVector.X * u + w.VVector.X * v,
                                          w.InsertPoint.Y + w.UVector.Y * u + w.VVector.Y * v,
                                          w.InsertPoint.Z + w.UVector.Z * u + w.VVector.Z * v };
                    if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z))
                        return Skip(w);
                    pts.push_back(q);
                }
                auto ent = Make<WipeoutEntity>(w, nullptr, std::move(pts));
                static_cast<WipeoutEntity&>(*ent).SetShowFrame(m_wipeoutFrame);
                return ent;
            }

            // 样条：有控制点与节点时按 NURBS 显示（拟合点一并保留），只有拟合点时按拟合点插值
            std::unique_ptr<Entity> ConvertSpline(const Dwg::Spline& s)
            {
                Spline sp;
                sp.Degree = s.Degree;
                sp.Flags  = static_cast<int>(s.Flags) & (SplineFlag_Closed | SplineFlag_Periodic | SplineFlag_Planar | SplineFlag_Linear);
                for (const auto& p : s.ControlPoints)
                    sp.ControlPoints.push_back(ToPoint(p));
                sp.Knots = s.Knots;
                if (!s.Weights.empty() && s.Weights.size() == s.ControlPoints.size()
                    && (Any(s.Flags & Dwg::SplineFlags::Rational)
                        || std::any_of(s.Weights.begin(), s.Weights.end(), [](double w) { return std::abs(w - 1.0) > 1e-12; })))
                {
                    sp.Weights = s.Weights;
                    sp.Flags |= SplineFlag_Rational;
                }
                for (const auto& p : s.FitPoints)
                    sp.FitPoints.push_back(ToPoint(p));
                sp.StartTangent  = ToPoint(s.StartTangent);
                sp.EndTangent    = ToPoint(s.EndTangent);
                sp.FitTolerance  = s.FitTolerance;
                sp.KnotTolerance = s.KnotTolerance;
                sp.CtrlTolerance = s.ControlPointTolerance;
                if (!sp.HasControlData())
                {
                    if (sp.FitPoints.size() < 2)
                        return Skip(s);
                    sp.ControlPoints.clear();
                    sp.Knots.clear();
                    sp.Weights.clear();
                    auto nonZero = [](const Math::Point3& t) { return std::abs(t.x) + std::abs(t.y) + std::abs(t.z) > 1e-12; };
                    sp.Boundary = Any(s.Flags & Dwg::SplineFlags::Closed) ? SplineBoundary::Closed
                                : nonZero(sp.StartTangent) && nonZero(sp.EndTangent) ? SplineBoundary::Clamped
                                                                                     : SplineBoundary::Natural;
                }
                if (!sp.FitPoints.empty())
                    sp.Build();
                return Make<SplineEntity>(s, nullptr, std::move(sp));
            }

            // 多面网格（POLYLINE，AcDbPolyFaceMesh）→ MeshEntity：四边形面拆成两个三角形，面记录的负索引（边不可见）按绝对值
            std::unique_ptr<Entity> ConvertPolyface(const Dwg::Polyline& e)
            {
                Solid3DEntity::SurfaceMesh mesh;
                std::vector<const Dwg::VertexFaceRecord*> faces;
                auto take = [&](Dwg::Handle h)
                {
                    const auto* v = m_db.FindAs<Dwg::Vertex>(h);
                    if (!v)
                        return;
                    if (const auto* f = dynamic_cast<const Dwg::VertexFaceRecord*>(v))
                        faces.push_back(f);
                    else
                        mesh.Positions.push_back({ v->Location.X, v->Location.Y, v->Location.Z });
                };
                for (Dwg::Handle h : e.Vertices)
                    take(h);
                if (const auto* pf = dynamic_cast<const Dwg::PolyfaceMesh*>(&e))
                    for (Dwg::Handle h : pf->Faces)
                        take(h);
                const int n = static_cast<int>(mesh.Positions.size());
                for (const auto* f : faces)
                {
                    const int raw[4] = { std::abs(f->Index1), std::abs(f->Index2), std::abs(f->Index3), std::abs(f->Index4) };
                    std::vector<uint32_t> idx;
                    for (int r : raw)
                        if (r >= 1 && r <= n)
                            idx.push_back(static_cast<uint32_t>(r - 1));
                    if (idx.size() >= 3)
                        mesh.Indices.insert(mesh.Indices.end(), { idx[0], idx[1], idx[2] });
                    if (idx.size() == 4)
                        mesh.Indices.insert(mesh.Indices.end(), { idx[0], idx[2], idx[3] });
                }
                if (mesh.Indices.empty())
                    return Skip(e);
                return Make<MeshEntity>(e, nullptr, std::move(mesh), Math::Mat4::Identity());
            }

            // 旧式 POLYLINE（R12 的 DXF）：二维 / 三维折线、多面网格；多边形网格（M × N）不支持
            std::unique_ptr<Entity> ConvertOldPolyline(const Dwg::Polyline& e)
            {
                if (Any(e.Flags & Dwg::PolylineFlags::PolyfaceMesh))
                    return ConvertPolyface(e);
                if (Any(e.Flags & Dwg::PolylineFlags::PolygonMesh))
                    return Skip(e);

                std::vector<Math::Point3> pts;
                std::vector<double>       bulges;
                // 旧式 POLYLINE 的顶点同样带起止宽度（VERTEX 的组码 40/41），一并保留
                std::vector<double>       startW;
                std::vector<double>       endW;
                for (Dwg::Handle h : e.Vertices)
                {
                    const auto* v = m_db.FindAs<Dwg::Vertex>(h);
                    if (v == nullptr || Any(v->Flags & Dwg::VertexFlags::SplineFrameControlPoint))   // 样条拟合的控制点不画
                        continue;
                    pts.push_back({ v->Location.X, v->Location.Y, v->Location.Z });
                    bulges.push_back(v->Bulge);
                    startW.push_back(v->StartWidth);
                    endW.push_back(v->EndWidth);
                }
                if (pts.size() < 2)
                    return nullptr;
                if (Any(e.Flags & Dwg::PolylineFlags::ClosedPolylineOrClosedPolygonMeshInM))
                    pts.push_back(pts.front());
                else
                    bulges.pop_back();
                auto ent = Make<PolylineEntity>(e, &e.Normal, std::move(pts), std::move(bulges));
                auto& pl = static_cast<PolylineEntity&>(*ent);
                pl.SetWidth(e.StartWidth);

                // 旧式 POLYLINE 另有一套全局起止宽度（组码 40/41，位于 POLYLINE 自身），
                // 顶点宽度缺省时用它；两者都没有时回退到 SetWidth 的恒定值。
                const size_t segs = static_cast<size_t>(pl.GetPolyline().SegCount());
                if (startW.size() < segs) startW.resize(segs, 0.0);
                if (endW.size()   < segs) endW.resize(segs, 0.0);
                startW.resize(segs);
                endW.resize(segs);
                for (size_t i = 0; i < segs; ++i)
                {
                    if (startW[i] == 0.0) startW[i] = e.StartWidth;
                    if (endW[i]   == 0.0) endW[i]   = e.EndWidth;
                }
                pl.SetSegmentWidths(std::move(startW), std::move(endW));
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
            std::map<Dwg::Handle, MLineStyleID> m_mlineStyles;   // MLINESTYLE 句柄 → 样式表 ID
            std::map<Dwg::Handle, BlockID>     m_blocks;
            // 转换进度：已转换 / 全部块与模型空间的实体数
            void Tick()
            {
                if (m_progress && m_progressTotal > 0 && (++m_progressDone & 255) == 0)
                    m_progress(std::min(1.0f, static_cast<float>(m_progressDone) / static_cast<float>(m_progressTotal)));
            }
            ImportProgress                     m_progress;
            std::size_t                        m_progressTotal = 0;
            std::size_t                        m_progressDone  = 0;
            std::set<Dwg::Handle>              m_filledBlocks;
            std::map<Dwg::Handle, Tch::TchOpening> m_openings;                      // 门窗句柄 → 解码结果
            std::map<Dwg::Handle, std::vector<Tch::TchOpening>> m_wallOpenings;    // 墙句柄 → 墙上的门窗
            std::map<std::string, int>         m_skipped;
            std::map<Dwg::Handle, std::map<Dwg::Handle, Dwg::Handle>> m_sortKeys;   // 块记录 → 实体 → 排序句柄
            bool m_wipeoutFrame = true;                                              // WIPEOUTFRAME（没有该对象时 AutoCAD 默认显示）
            int m_tianzhengWalls = 0;
            std::map<std::string, int> m_tchConverted;
            std::map<std::string, std::map<std::string, std::set<Dwg::Handle>>> m_tchFailures;   // 类型 → 原因 → 句柄
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
                    // 2007 以前的文字按 $DWGCODEPAGE 编码（2007 起为 Unicode），默认的 ANSI_1252 写不出中文
                    if (m_version < Dwg::CadVersion::AC1021)
                        h.CodePage = "ANSI_936";

                    // 点样式写回（$PDMODE / $PDSIZE）。**必须与导入成对**：
                    // 只导入不写出，用户一存盘点样式就退回 AutoCAD 默认（0 = 一像素小点），
                    // 整张图的 POINT 看起来全变了。
                    h.PointDisplayMode = static_cast<std::int16_t>(m_scene.GetPointDisplayMode());
                    h.PointDisplaySize = m_scene.GetPointDisplaySize();

                    // 全局线型比例写回（$LTSCALE）。**同样必须与导入成对**：
                    // 不写就会被 AutoCAD 当成 1，大尺寸图纸的虚线周期会小几百上千倍，
                    // 密到看不出线型（与点样式同一类教训）。
                    h.LineTypeScale = m_scene.GetLineTypeScale();
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
                        const auto& ent = static_cast<const Entity&>(o);
                        m_pendingClips.clear();
                        if (auto conv = Convert(ent))
                        {
                            // 图纸空间实体挂到它所属布局的 *Paper_Space* 块记录上。
                            // 写出侧按**所有者**判断是否写组码 67（IsPaperSpaceOwner），
                            // 挂对了所有者，67 与 330 都会自动正确 —— 不需要额外字段。
                            Dwg::BlockRecord* owner = model;
                            if (const EntityAttr& a = ent.GetAttr(); a.InPaperSpace)
                            {
                                owner = PaperSpaceRecordFor(a.LayoutKey);
                                if (owner == nullptr)
                                    owner = model;   // 找不到对应布局时退回模型空间，至少不丢图元
                            }

                            const std::vector<PendingClip> pending = std::move(m_pendingClips);
                            m_pendingClips.clear();
                            Dwg::Entity* added = m_db.AddEntity(owner, std::move(conv));
                            if (added != nullptr)
                                for (const auto& c : pending)
                                    AttachClipBoundary(c, added->ObjectHandle,
                                                       dynamic_cast<Dwg::Insert*>(added));
                        }
                        m_pendingClips.clear();
                    });
                }

                // LAYOUT 对象：把布局名 / 纸张范围 / 对应的图纸空间块记录写进 OBJECTS 段。
                // 不写的话，另存为后布局标签会整批消失（实体会留着，但无处可去）。
                ExportLayouts();

                for (const auto& [name, count] : m_skipped)
                    LOG_WARN("DWG/DXF 导出：不支持的实体 %s × %d 已跳过", name.c_str(), count);
                if (m_solidNoMesh > 0)
                    LOG_WARN("DWG/DXF 导出：%d 个三维实体没有网格数据（早期版本保存），已跳过", m_solidNoMesh);
                if (m_solidTooLarge > 0)
                    LOG_WARN("DWG/DXF 导出：%d 个三维实体的网格超过 32767 个顶点或面，已跳过", m_solidTooLarge);
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

            // ── 布局导出 ──────────────────────────────────────────────────
            //
            // 布局与图纸空间块记录的对应关系：
            //   第 1 张布局 → *Paper_Space
            //   第 2 张     → *Paper_Space0
            //   第 3 张     → *Paper_Space1  …（AutoCAD 的命名约定）
            // LAYOUT 对象的 AssociatedBlockHandle(330) 指向该块记录，TabOrder(71) 定顺序。
            //
            // 图纸空间实体按 LayoutKey 找到所属布局，挂到对应块记录上 ——
            // 写出侧据所有者自动写组码 67，实体与布局的归属关系随之保留。
            Dwg::BlockRecord* PaperSpaceRecordFor(uint64_t layoutKey)
            {
                // 先按 LayoutKey（= 导入时的块记录句柄）找已存在的记录
                if (layoutKey != 0)
                    if (auto* rec = m_db.FindAs<Dwg::BlockRecord>(static_cast<Dwg::Handle>(layoutKey)))
                        return rec;

                // 找不到（新建文档、或布局换了序号）：按布局名另建 / 复用一个
                const auto& layouts = m_scene.Layouts();
                int paperIndex = 0;
                for (size_t i = 0; i < layouts.size(); ++i)
                {
                    if (layouts[i].Model) continue;
                    if (layouts[i].OwnerKey == layoutKey)
                        break;
                    ++paperIndex;
                }
                const std::string name = paperIndex == 0 ? "*Paper_Space"
                                                         : "*Paper_Space" + std::to_string(paperIndex - 1);
                if (auto* existing = m_db.FindTableEntry<Dwg::BlockRecord>(m_db.BlockRecords(), name))
                    return existing;
                return m_db.CreateBlockRecord(name);
            }

            // 把 Scene 的布局表写成 LAYOUT 对象。
            // 模型页在 DXF / DWG 里不是一个 LAYOUT 对象（它由 *Model_Space 表达），
            // 所以这里只导出非模型布局。
            void ExportLayouts()
            {
                const auto& layouts = m_scene.Layouts();
                if (layouts.size() <= 1)
                    return;      // 只有模型页：不需要 LAYOUT

                // LAYOUT 挂在根字典的 "ACAD_LAYOUT" 字典下
                Dwg::CadDictionary* root = m_db.RootDictionary();
                if (root == nullptr)
                    return;
                Dwg::CadDictionary* layoutDict = nullptr;
                for (size_t i = 0; i < root->EntryNames.size() && i < root->EntryHandles.size(); ++i)
                    if (root->EntryNames[i] == "ACAD_LAYOUT")
                        layoutDict = m_db.FindAs<Dwg::CadDictionary>(root->EntryHandles[i]);
                if (layoutDict == nullptr)
                {
                    auto dict = std::make_unique<Dwg::CadDictionary>();
                    layoutDict = m_db.Add(std::move(dict));
                    if (layoutDict == nullptr)
                        return;
                    root->EntryNames.push_back("ACAD_LAYOUT");
                    root->EntryHandles.push_back(layoutDict->ObjectHandle);
                }

                int order = 0;
                // ⚠ 先清空字典里已有的布局项、并删掉旧 LAYOUT 对象，再按 Scene 的布局表重建。
                //   两层原因：
                //     1. `CadDatabase::CreateDefaults()` 会预置一个 "Layout1"，不删就重复；
                //     2. 以原图为底时读出的原库也带着原图的 LAYOUT，同样要按当前场景重写。
                //   只清字典项不够 —— 对象本身还在 m_db.Objects() 里，写出时会作为
                //   「未被引用但所有者已写出」的对象再写一遍（实测 4 张布局读回变 5 张）。
                // ⚠ 删掉**所有**已有 LAYOUT 对象，再按 Scene 的布局表重建。
                //   两层来源都要清：
                //     1. `CadDatabase::CreateDefaults()` 预置的 "Layout1"（关联 *Paper_Space）；
                //     2. 以原图为底时从原库读出的原图 LAYOUT。
                //   只按字典项删除不够 —— 实测 CreateDefaults 的那个不在字典项里，
                //   漏掉就导致「Layout1」重复（4 张布局读回变 5 张）。
                {
                    std::vector<Dwg::Handle> stale;
                    for (const auto& [h, obj] : m_db.Objects())
                        if (dynamic_cast<const Dwg::Layout*>(obj.get()) != nullptr)
                            stale.push_back(h);
                    for (Dwg::Handle h : stale)
                        m_db.RemoveObject(h);
                    // 块记录上还挂着 LayoutHandle 反向引用（CreateLayout 里设的），
                    // 不清掉的话写出时会顺着它把已删的布局再写一遍。
                    if (const Dwg::CadTable* recs = m_db.BlockRecords())
                        for (Dwg::Handle rh : recs->Entries)
                            if (auto* rec = m_db.FindAs<Dwg::BlockRecord>(rh))
                                rec->LayoutHandle = Dwg::kNullHandle;
                }
                layoutDict->EntryNames.clear();
                layoutDict->EntryHandles.clear();

                for (const DrawingLayout& dl : layouts)
                {
                    if (dl.Model) { ++order; continue; }

                    Dwg::BlockRecord* rec = PaperSpaceRecordFor(dl.OwnerKey);
                    if (rec == nullptr) { ++order; continue; }

                    auto lay = std::make_unique<Dwg::Layout>();
                    lay->Name          = dl.Name;
                    lay->TabOrder      = order;
                    lay->AssociatedBlockHandle = rec->ObjectHandle;
                    lay->MaxExtents    = Dwg::XYZ{ dl.MaxX, dl.MaxY, 0.0 };
                    lay->MinExtents    = Dwg::XYZ{ dl.MinX, dl.MinY, 0.0 };
                    lay->MaxLimits     = Dwg::XY{ dl.MaxX, dl.MaxY };
                    lay->MinLimits     = Dwg::XY{ dl.MinX, dl.MinY };
                    lay->InsertionBasePoint = Dwg::XYZ{ 0.0, 0.0, 0.0 };

                    if (Dwg::Layout* added = m_db.Add(std::move(lay)))
                    {
                        layoutDict->EntryNames.push_back(dl.Name);
                        layoutDict->EntryHandles.push_back(added->ObjectHandle);
                    }
                    ++order;
                }
            }

            void ExportLineTypes()            {
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
                {
                    // 每个实体入库前清空待挂接队列：Convert 里最多为一个 INSERT 记一项
                    m_pendingClips.clear();
                    if (auto conv = Convert(*e))
                    {
                        Dwg::Insert* ins = dynamic_cast<Dwg::Insert*>(conv.get());
                        const std::vector<PendingClip> pending = std::move(m_pendingClips);
                        m_pendingClips.clear();
                        Dwg::Entity* added = m_db.AddEntity(rec, std::move(conv));
                        if (added != nullptr && ins != nullptr)
                            for (const auto& c : pending)
                                AttachClipBoundary(c, added->ObjectHandle, static_cast<Dwg::Insert*>(added));
                    }
                    m_pendingClips.clear();
                }
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
                // 注：图纸空间标志（组码 67）不在这里写 ——
                // DWG / DXF 写出侧按**实体的所有者**判断（IsPaperSpaceOwner），
                // 只要把实体挂到对应的 *Paper_Space* 块记录上就会自动带上 67。
                // 组码 330（布局归属）同样由所有者关系表达，无需额外字段。
            }

            static Dwg::XYZ NormalOf(const Entity& e)
            {
                const Math::Vec3& n = e.GetExtrusion();
                return { n.x, n.y, n.z };
            }

            // 块裁剪（XCLIP）写回：把待写对象暂存，等实体入库拿到句柄后再挂接。
            //
            // 为什么必须延后：SPATIAL_FILTER 的字典链要从**实体自己的句柄**挂起
            // （d1->OwnerHandle = insertHandle、INSERT 的 XDictionaryHandle = d1），
            // 而句柄是 m_db.AddEntity / Add 时才分配的，Convert 阶段还没有。
            // 待写回的块裁剪边界（Convert 阶段记下，实体入库拿到句柄后再建对象链）
            struct PendingClip
            {
                std::vector<Dwg::XY> World;        // 边界（世界坐标）
                Math::Mat4           BlockToWorld; // 插入变换（块坐标 → 世界坐标）
            };

            // 块裁剪（XCLIP）写回：把待写对象暂存，等实体入库拿到句柄后再挂接。
            //
            // 为什么必须延后：SPATIAL_FILTER 的字典链要从**实体自己的句柄**挂起
            // （d1->OwnerHandle = insertHandle、INSERT 的 XDictionaryHandle = d1），
            // 而句柄是 m_db.AddEntity / Add 时才分配的，Convert 阶段还没有。
            void AttachClipBoundary(const PendingClip& clip, Dwg::Handle insertHandle,
                                    Dwg::Insert* insert)
            {
                if (insert == nullptr || clip.World.size() < 2)
                    return;

                const auto& world = clip.World;
                double x0 = world[0].X, x1 = x0, y0 = world[0].Y, y1 = y0;
                for (const auto& p : world)
                {
                    x0 = std::min(x0, p.X); x1 = std::max(x1, p.X);
                    y0 = std::min(y0, p.Y); y1 = std::max(y1, p.Y);
                }

                // 逆块变换的 3×4 行主序：块坐标 = M⁻¹ × 世界坐标。
                // 导入侧读的就是这 12 个 double（见 ReadClipBoundary 的 apply：
                //   nx = m[0]*x + m[1]*y + m[2]*z + m[3]
                //   ny = m[4]*x + m[5]*y + m[6]*z + m[7]  ← 平移在**第 4 列**）
                //
                // ⚠ 本工程的 `Mat4` 是**行向量约定**（P' = P·M，见 Mat4.hpp），
                //   平移分量在 m[12]/m[13]/m[14]，不是 m[3]/m[7]/m[11]。
                //   而 SPATIAL_FILTER 的 3×4 是「行主序 + 平移在第 4 列」的经典形式，
                //   两者布局不同，必须显式转置 —— 直接搬 m[] 会让边界整体偏移一个插入点。
                const Math::Mat4 inv = Math::Mat4::Inverse(clip.BlockToWorld);
                const double* mv = inv.Data();
                // inv 的列向量约定视角：x' = x*m[0]+y*m[4]+z*m[8]+m[12]
                //   ⇒ 3×4 行主序应为 [m0 m4 m8 m12; m1 m5 m9 m13; m2 m6 m10 m14]
                double inv3x4[12] = {
                    mv[0], mv[4], mv[8],  mv[12],
                    mv[1], mv[5], mv[9],  mv[13],
                    mv[2], mv[6], mv[10], mv[14],
                };

                // 对象链：D1(ACAD_FILTER) → D2(SPATIAL) → SPATIAL_FILTER
                auto* d2 = m_db.Add(std::make_unique<Dwg::CadDictionary>());
                auto* d1 = m_db.Add(std::make_unique<Dwg::CadDictionary>());
                if (d1 == nullptr || d2 == nullptr)
                    return;

                auto filter = std::make_unique<Dwg::UnknownObject>();
                filter->Raw.DxfName = "SPATIAL_FILTER";
                filter->Raw.FixedType = 0;

                // ── DXF 组码 ───────────────────────────────────────────────
                // 与样例 sample_AC1015_ascii.dxf 的 SPATIAL_FILTER 逐项对齐
                filter->Raw.Dxf.Version = m_version;
                auto& g = filter->Raw.Dxf.Groups;
                auto add = [&g](std::int16_t code, Dwg::DxfValue v) { g.push_back({ code, std::move(v) }); };
                add(100, Dwg::DxfValue(std::string("AcDbFilter")));
                add(100, Dwg::DxfValue(std::string("AcDbSpatialFilter")));
                add(70,  Dwg::DxfValue(static_cast<std::int64_t>(2)));   // 边界点数
                add(10,  Dwg::DxfValue(x0));
                add(20,  Dwg::DxfValue(y0));
                add(10,  Dwg::DxfValue(x1));
                add(20,  Dwg::DxfValue(y1));
                add(210, Dwg::DxfValue(0.0));
                add(220, Dwg::DxfValue(0.0));
                add(230, Dwg::DxfValue(1.0));                            // 法向
                add(11,  Dwg::DxfValue(0.0));
                add(21,  Dwg::DxfValue(0.0));
                add(31,  Dwg::DxfValue(0.0));                            // 原点
                add(71,  Dwg::DxfValue(static_cast<std::int64_t>(1)));   // 启用裁剪
                add(72,  Dwg::DxfValue(static_cast<std::int64_t>(0)));   // 前裁剪
                add(73,  Dwg::DxfValue(static_cast<std::int64_t>(0)));   // 后裁剪
                // 24 个 double：逆块变换（12）+ 边界变换（12，单位阵）
                for (int i = 0; i < 12; ++i)
                    add(40, Dwg::DxfValue(inv3x4[i]));
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 4; ++c)
                        add(40, Dwg::DxfValue(r == c ? 1.0 : 0.0));

                // ── DWG 二进制 ─────────────────────────────────────────────
                // 未建模对象只能原样写回同一版本；版本不符时写回会跳过它（不写坏文件）。
                {
                    Dwg::DwgBitWriter w;
                    w.WriteBitShort(2);                              // 点数
                    w.Write2RawDouble({ x0, y0 });
                    w.Write2RawDouble({ x1, y1 });
                    w.Write3BitDouble({ 0.0, 0.0, 1.0 });            // 法向
                    w.Write3BitDouble({ 0.0, 0.0, 0.0 });            // 原点
                    w.WriteBitShort(1);                              // 启用
                    w.WriteBitShort(0);                              // 无前裁剪距离
                    w.WriteBitShort(0);                              // 无后裁剪距离
                    for (int i = 0; i < 12; ++i)
                        w.WriteBitDouble(inv3x4[i]);
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 4; ++c)
                            w.WriteBitDouble(r == c ? 1.0 : 0.0);    // 边界变换 = 单位阵
                    filter->Raw.Dwg.Version = m_version;
                    filter->Raw.Dwg.Main     = w.Data();
                    filter->Raw.Dwg.MainBits = w.PositionInBits();
                }

                auto* added = m_db.Add(std::move(filter));
                if (added == nullptr)
                    return;

                d1->OwnerHandle = insertHandle;
                d2->OwnerHandle = d1->ObjectHandle;
                added->OwnerHandle = d2->ObjectHandle;
                d1->EntryNames.push_back("ACAD_FILTER");
                d1->EntryHandles.push_back(d2->ObjectHandle);
                d2->EntryNames.push_back("SPATIAL");
                d2->EntryHandles.push_back(added->ObjectHandle);

                insert->XDictionaryHandle = d1->ObjectHandle;
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

            // 多线在某个顶点处的「方向」与「斜接方向」。
            //
            // 与 MLineEntity::OverlayVertexParams 用同一套算法：
            //   · 端点：dir = 相邻段方向，miter = dir 的左法线
            //   · 中间顶点：dir = 两段的角平分方向，miter = 平均方向的法线，
            //               长度按 1/cos(半角) 归一（直角处得到 (-0.707, 0.707)）
            //
            // 导出时用它把元素线的实际坐标反解成 MLINE 的两个参数；
            // 导入时用它还原坐标。两边一致，往返才不走样。
            static void VertexDirMiter(const std::vector<Math::Point3>& path, bool closed, size_t i,
                                       Math::Vec3& outDir, Math::Vec3& outMiter)
            {
                const size_t n = path.size();
                const size_t segCount = (closed && n >= 3) ? n : (n > 0 ? n - 1 : 0);
                if (segCount == 0)
                {
                    outDir = { 1.0, 0.0, 0.0 };
                    outMiter = { 0.0, 1.0, 0.0 };
                    return;
                }
                auto segDir = [&](size_t s) -> Math::Vec3
                {
                    const auto& a = path[s % n];
                    const auto& b = path[(s + 1) % n];
                    const double dx = b.x - a.x, dy = b.y - a.y;
                    const double len = std::sqrt(dx * dx + dy * dy);
                    return len > 1e-12 ? Math::Vec3{ dx / len, dy / len, 0.0 } : Math::Vec3{ 1.0, 0.0, 0.0 };
                };

                const bool isEnd = !closed && (i == 0 || i + 1 == n);
                if (isEnd)
                {
                    outDir = (i == 0) ? segDir(0) : segDir(segCount - 1);
                    outMiter = Math::Vec3{ -outDir.y, outDir.x, 0.0 };
                    return;
                }

                const Math::Vec3 d0 = segDir((i + segCount - 1) % segCount);   // 前一段
                const Math::Vec3 d1 = segDir(i % segCount);                    // 后一段
                Math::Vec3 bis{ d0.x + d1.x, d0.y + d1.y, 0.0 };
                const double bl = std::sqrt(bis.x * bis.x + bis.y * bis.y);
                if (bl < 1e-12) { outDir = d1; outMiter = Math::Vec3{ -d1.y, d1.x, 0.0 }; return; }

                outDir = Math::Vec3{ bis.x / bl, bis.y / bl, 0.0 };
                const Math::Vec3 nrm{ -d1.y, d1.x, 0.0 };
                const double cosHalf = nrm.x * (-d0.y) + nrm.y * d0.x;
                const double k = (std::abs(cosHalf) > 0.2) ? 1.0 / std::abs(cosHalf) : 1.0;
                outMiter = Math::Vec3{ nrm.x * k, nrm.y * k, 0.0 };
            }

            // 多线样式句柄：MultilineStyle 在 DWG 里是**根字典 ACAD_MLINESTYLE 下的条目**
            // （不是符号表），所以走字典查找 / 新建。
            Dwg::Handle MLineStyleHandle(const MLineStyleRecord& rec)
            {
                // 先按名字找已有的条目
                if (Dwg::CadDictionary* dict = m_db.FindNamedDictionary("ACAD_MLINESTYLE"))
                {
                    for (size_t i = 0; i < dict->EntryNames.size() && i < dict->EntryHandles.size(); ++i)
                        if (EqualsNoCase(dict->EntryNames[i], rec.Name.c_str()))
                            return dict->EntryHandles[i];
                }
                else
                {
                    // 还没有这个字典：建一个挂到根字典下
                    Dwg::CadDictionary* root = m_db.RootDictionary();
                    if (root == nullptr)
                        return Dwg::kNullHandle;
                    auto newDict = std::make_unique<Dwg::CadDictionary>();
                    Dwg::CadDictionary* addedDict = m_db.Add(std::move(newDict));
                    if (addedDict == nullptr)
                        return Dwg::kNullHandle;
                    root->EntryNames.push_back("ACAD_MLINESTYLE");
                    root->EntryHandles.push_back(addedDict->ObjectHandle);
                }

                auto st = std::make_unique<Dwg::MLineStyle>();
                st->Name        = rec.Name;
                st->Description = rec.Description;
                int flags = 0;
                if (rec.StartCap) flags |= 1;
                if (rec.EndCap)   flags |= 2;
                if (rec.Joints)   flags |= 4;
                st->Flags = static_cast<Dwg::MLineStyleFlags>(flags);
                for (const auto& el : rec.Elements)
                {
                    Dwg::MLineStyleElement e;
                    e.Offset = el.Offset;
                    if (el.UseColor)
                    {
                        // Math::Color4（0..1 浮点）→ 真彩色 24 位
                        const auto to255 = [](double v) {
                            return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
                        };
                        const std::uint32_t rgb = (to255(el.Color.r) << 16) |
                                                  (to255(el.Color.g) << 8)  |
                                                   to255(el.Color.b);
                        e.Color = Dwg::Color::FromTrueColor(rgb);
                    }
                    else
                    {
                        e.Color = Dwg::Color::ByLayer();
                    }
                    st->Elements.push_back(e);
                }
                if (Dwg::MLineStyle* added = m_db.Add(std::move(st)))
                {
                    auto* dict = m_db.FindNamedDictionary("ACAD_MLINESTYLE");
                    if (dict != nullptr)
                    {
                        dict->EntryNames.push_back(rec.Name);
                        dict->EntryHandles.push_back(added->ObjectHandle);
                    }
                    return added->ObjectHandle;
                }
                return Dwg::kNullHandle;
            }

            // ATTDEF / ATTRIB 的位置与对齐：导入时 Fit/Aligned 取过 10↔11 的中点、并把对齐码
            // 归到了 0/1/2，写出时还原为「插入点 = 对齐点 = 锚点 + 居中对齐」——
            // 往返后锚点与居中都一致（不做逐字拉伸，语义差一个 72=5 的标记，
            // 与 TEXT 的处理相同，见导入侧 ResolveTextPlacement 的注释）
            template <class T>
            static void SetAttribPlacement(const AttribBase& s, T& d)
            {
                d.InsertPoint = ToXYZ(s.GetPosition());
                d.Height      = s.GetHeight();
                d.Rotation    = s.GetRotation();
                if (s.GetHAlign() != 0 || s.GetVAlign() != 0)
                {
                    d.AlignmentPoint      = d.InsertPoint;
                    d.HorizontalAlignment = static_cast<Dwg::TextHorizontalAlignment>(s.GetHAlign());
                    d.VerticalAlignment   = static_cast<Dwg::TextVerticalAlignmentType>(s.GetVAlign());
                }
            }

            std::unique_ptr<Dwg::Entity> Convert(const Entity& e)
            {
                // 图纸空间视口（VIEWPORT）：布局页里靠它把模型内容显示出来。
                // 不导出的话另存为后布局页只剩白纸。
                if (auto* s = dynamic_cast<const ViewportEntity*>(&e))
                {
                    auto d = std::make_unique<Dwg::Viewport>();
                    d->Center     = ToXYZ(s->GetCenter());
                    d->Width      = s->GetWidth();
                    d->Height     = s->GetHeight();
                    // ViewCenter(12/22) 要写 **DCS** 值：内部存的是 WCS，
                    // 转回去（R(+Twist)）。Twist 为 0 时是恒等变换。
                    d->ViewCenter = Dwg::XY{ s->GetViewCenterDcs(s->GetTwist()).x,
                                             s->GetViewCenterDcs(s->GetTwist()).y };
                    d->ViewHeight = s->GetViewHeight();
                    d->TwistAngle = s->GetTwist();
                    d->ActiveStatus = static_cast<std::int16_t>(s->GetViewportId());
                    d->ViewDirection = Dwg::XYZ::AxisZ();
                    // ViewTarget(17/27) 是 WCS 下的观察目标点，直接用 WCS 值
                    d->ViewTarget = Dwg::XYZ{ s->GetViewCenter().x, s->GetViewCenter().y, 0.0 };
                    return Finish(e, std::move(d));
                }
                // 多线（MLINE）：导出中心线路径 + 每顶点每元素线的参数。
                // 只导入不导出，另存为后多线会整批消失。
                //
                // 顶点方向 / 斜接方向按**同一套算法**算（见 VertexDirMiter），
                // 与导入侧 MLineEntity::OverlayVertexParams 对应，
                // 否则写出的参数与重算方向对不上，往返后形状会走样。
                if (auto* s = dynamic_cast<const MLineEntity*>(&e))
                {
                    const auto geo = s->ComputeGeometry();
                    if (geo.Path.size() < 2 || geo.Elements.size() < 2)
                        return Skip(e);

                    auto d = std::make_unique<Dwg::MLine>();
                    d->ScaleFactor   = s->GetScale();
                    d->Normal        = NormalOf(e);
                    d->StartPoint    = ToXYZ(geo.Path.front());
                    d->Justification = (s->GetJustify() == MLineJustify::Top)    ? Dwg::MLineJustification::Top
                                     : (s->GetJustify() == MLineJustify::Bottom) ? Dwg::MLineJustification::Bottom
                                                                                 : Dwg::MLineJustification::Zero;
                    int flags = static_cast<int>(Dwg::MLineFlags::HasVertices);
                    if (s->IsClosed())
                        flags |= static_cast<int>(Dwg::MLineFlags::Closed);
                    d->Flags = static_cast<Dwg::MLineFlags>(flags);
                    // 样式句柄：导出时按样式名找 / 建 MLINESTYLE
                    d->StyleHandle = MLineStyleHandle(s->GetMLineStyle());

                    // 每个顶点：位置 + 方向 + 斜接 + 每元素线参数
                    //
                    // 参数语义（与导入侧一致）：
                    //   参数[0] = 沿该顶点 **miter 方向** 的偏移（斜接后的横向位置）
                    //   参数[1] = 沿该顶点 **dir 方向** 的额外延伸（T 形/拐角接头的端头推移）
                    // 写出时按「元素线实际坐标」反解这两个分量，
                    // 这样导出再导入能完整还原接头形状。
                    const size_t n = geo.Path.size();
                    const size_t segCount = s->IsClosed() ? n : n - 1;
                    for (size_t i = 0; i < n; ++i)
                    {
                        Dwg::MLineVertex v;
                        v.Position = ToXYZ(geo.Path[i]);

                        // 顶点方向 dir 与斜接方向 miter（与 MLineEntity::OverlayVertexParams 同一套算法，
                        // 否则反解出的参数与重算的方向对不上）
                        Math::Vec3 vdir, vmiter;
                        VertexDirMiter(geo.Path, s->IsClosed(), i, vdir, vmiter);
                        v.Direction = Dwg::XYZ{ vdir.x, vdir.y, 0.0 };
                        v.Miter     = Dwg::XYZ{ vmiter.x, vmiter.y, 0.0 };

                        for (size_t k = 0; k < s->GetMLineStyle().Elements.size() && k < geo.Elements.size(); ++k)
                        {
                            Dwg::MLineVertexSegment sg;
                            const Math::Point3& ep = geo.Elements[k][i];
                            const Math::Point3& cp = geo.Path[i];
                            const double ox = ep.x - cp.x, oy = ep.y - cp.y;
                            // 投影到 miter / dir 两个基上
                            const double pMiter = ox * vmiter.x + oy * vmiter.y;
                            const double pDir   = ox * vdir.x   + oy * vdir.y;
                            sg.Parameters.push_back(pMiter);
                            sg.Parameters.push_back(pDir);
                            v.Segments.push_back(std::move(sg));
                        }                        d->Vertices.push_back(std::move(v));
                    }
                    (void)segCount;
                    return Finish(e, std::move(d));
                }
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
                if (auto* s = dynamic_cast<const Solid3DEntity*>(&e))
                    return ConvertSolid3D(e, *s);
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
                if (auto* s = dynamic_cast<const WipeoutEntity*>(&e))   // 派生自 PolylineEntity，须先判断
                    return ConvertWipeout(e, *s);
                if (auto* s = dynamic_cast<const PolylineEntity*>(&e))
                    // 锥形段（逐顶点起止宽度）一并写出，否则另存后宽度整批丢失
                    return ConvertPolyline(e, s->GetPolyline(), s->GetWidth(),
                                           &s->GetSegmentStartWidths(), &s->GetSegmentEndWidths());
                if (auto* s = dynamic_cast<const HatchEntity*>(&e))
                    return ConvertHatch(e, *s);
                if (auto* s = dynamic_cast<const SplineEntity*>(&e))
                    return ConvertSpline(e, s->GetSpline());
                if (auto* s = dynamic_cast<const RectangleEntity*>(&e))
                {
                    const auto& r = s->GetRectangle();
                    Polyline pl(std::vector<Math::Point3>{ r.P1, r.P2, r.P3, r.P4, r.P1 });
                    return ConvertPolyline(e, pl, 0.0);
                }
                // 属性定义（ATTDEF）：块定义里的可填字段。**必须排在 TextEntity 之前**判
                //（AttribBase 与 TextEntity 是平行分支，但先判更稳），否则属性定义会被当成
                // 普通单行文字写出，丢掉 tag / 默认值 / 提示语。
                if (auto* s = dynamic_cast<const AttDefEntity*>(&e))
                {
                    auto d = std::make_unique<Dwg::AttributeDefinition>();
                    SetAttribPlacement(*s, *d);
                    d->Value  = s->GetDefaultValue();
                    d->Tag    = s->GetTag();
                    d->Prompt = s->GetPrompt();
                    d->Flags  = static_cast<Dwg::AttributeFlags>(s->GetFlags());
                    d->StyleHandle = StyleHandle(TextStyleTable::StandardID);
                    d->Normal = NormalOf(e);
                    return Finish(e, std::move(d));
                }
                if (auto* s = dynamic_cast<const TextEntity*>(&e))
                {
                    auto d = std::make_unique<Dwg::TextEntity>();
                    d->InsertPoint = ToXYZ(s->GetPosition());
                    if (s->GetHAlign() != 0 || s->GetVAlign() != 0)
                    {
                        // 非左对齐基线：对齐点为准，插入点由 CAD 重新计算
                        d->AlignmentPoint      = d->InsertPoint;
                        d->HorizontalAlignment = static_cast<Dwg::TextHorizontalAlignment>(s->GetHAlign());
                        d->VerticalAlignment   = static_cast<Dwg::TextVerticalAlignmentType>(s->GetVAlign());
                    }
                    d->Value       = s->GetText();
                    d->Height      = s->GetHeight();
                    d->Rotation    = s->GetRotation();
                    d->StyleHandle = StyleHandle(s->GetStyleId());
                    if (s->GetWidthFactor() > 0.0)
                        d->WidthFactor = s->GetWidthFactor();
                    else if (const TextStyleRecord* rec = m_scene.GetTextStyleTable().Find(s->GetStyleId()))
                        d->WidthFactor = rec->WidthFactor > 0.0 ? rec->WidthFactor : 1.0;
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

                    // ATTRIB 与导入成对：只导入不导出，属性文字在另存后整批消失（轴号、图签字段）。
                    // 属性是 INSERT 的**子实体**（66=1 + ATTRIB… + SEQEND），不是块内成员，
                    // 所以在这里建对象、挂到 d->Attributes，块记录里不再出现。
                    for (const auto& a : s->GetAttribs())
                    {
                        if (!a) continue;
                        auto att = std::make_unique<Dwg::AttributeEntity>();
                        SetAttribPlacement(*a, *att);
                        att->Value = a->GetValue();
                        att->Tag   = a->GetTag();
                        att->Flags = static_cast<Dwg::AttributeFlags>(a->GetFlags());
                        att->StyleHandle = StyleHandle(TextStyleTable::StandardID);
                        att->Normal = NormalOf(*a);
                        ApplyAttr(*a, *att);
                        if (Dwg::AttributeEntity* added = m_db.Add(std::move(att)))
                            d->Attributes.push_back(added->ObjectHandle);
                    }

                    // 块裁剪（XCLIP）：边界是**块坐标**，SPATIAL_FILTER 里要**世界坐标**，
                    // 所以先经插入变换映射。句柄此时还没有（Add 时才分配），
                    // 先记下待挂接项，实体入库后再建对象链。
                    if (s->HasClipBoundary() && s->GetClipBoundary().size() >= 3)
                    {
                        PendingClip pending;
                        pending.BlockToWorld = s->CellMatrix(0, 0);
                        pending.World.reserve(s->GetClipBoundary().size());
                        for (const auto& p : s->GetClipBoundary())
                        {
                            const Math::Point3 w = pending.BlockToWorld * p;
                            pending.World.push_back({ w.x, w.y });
                        }
                        m_pendingClips.push_back(std::move(pending));
                    }

                    return Finish(e, std::move(d));
                }
                return Skip(e);
            }

            std::unique_ptr<Dwg::Entity> Skip(const Entity& e)
            {
                ++m_skipped[e.GetTypeInfo()->Name];
                return nullptr;
            }

            // 三维实体：DWG 的 3DSOLID 存的是 ACIS 数据，写不出来，退化为多面网格（POLYLINE，AcDbPolyFaceMesh）。
            // 网格是创建实体时离散的（局部坐标，这里施加变换）；面内部的边写成不可见（面记录的索引取负）。
            // 多面网格的顶点数、面数都是 16 位，超出时跳过
            std::unique_ptr<Dwg::Entity> ConvertSolid3D(const Entity& e, const Solid3DEntity& solid)
            {
                const auto& mesh = solid.GetLocalMesh();
                if (mesh.Empty())
                {
                    ++m_solidNoMesh;
                    return nullptr;
                }
                if (mesh.Positions.size() > 32767 || mesh.TriangleCount() > 32767)
                {
                    ++m_solidTooLarge;
                    return nullptr;
                }

                auto d = std::make_unique<Dwg::PolyfaceMesh>();
                ApplyAttr(e, *d);
                auto addVertex = [&](std::unique_ptr<Dwg::Vertex> v) {
                    ApplyAttr(e, *v);
                    if (Dwg::Vertex* added = m_db.Add(std::move(v)))
                        d->Vertices.push_back(added->ObjectHandle);
                };
                const Math::Mat4& m = solid.GetTransform();
                const bool allVisible = solid.IsKindOf<MeshEntity>();   // 网格：全部边可见（实体只画面的边界）
                for (const auto& p : mesh.Positions)
                {
                    auto v = std::make_unique<Dwg::VertexFaceMesh>();
                    v->Location = ToXYZ(m.TransformPoint(p));
                    v->Flags    = Dwg::VertexFlags::PolygonMesh3D | Dwg::VertexFlags::PolyFaceMeshVertex;
                    addVertex(std::move(v));
                }
                for (size_t t = 0; t < mesh.TriangleCount(); ++t)
                {
                    auto f = std::make_unique<Dwg::VertexFaceRecord>();
                    f->Flags = Dwg::VertexFlags::PolyFaceMeshVertex;
                    std::int16_t idx[3];
                    for (int i = 0; i < 3; ++i)
                    {
                        idx[i] = static_cast<std::int16_t>(mesh.Indices[t * 3 + i] + 1);   // 从 1 开始
                        if (!allVisible && (mesh.EdgeFlags[t] & (1u << i)))
                            idx[i] = static_cast<std::int16_t>(-idx[i]);                     // 负数：从该顶点出发的边不可见
                    }
                    f->Index1 = idx[0]; f->Index2 = idx[1]; f->Index3 = idx[2];
                    if (Dwg::VertexFaceRecord* added = m_db.Add(std::move(f)))
                    {
                        ApplyAttr(e, *added);
                        d->Faces.push_back(added->ObjectHandle);
                    }
                }
                return d;
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

            // segWidths 非空且带非 0 值时，按顶点写回逐段起止宽度（DXF 40/41）——
            // 与导入成对，否则「打开锥形段多段线 → 另存 → 再打开」宽度整批退化为等宽。
            std::unique_ptr<Dwg::Entity> ConvertPolyline(const Entity& e, const Polyline& pl, double width,
                                                         const std::vector<double>* segStartW = nullptr,
                                                         const std::vector<double>* segEndW = nullptr)
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
                const bool hasSegWidths = segStartW != nullptr || segEndW != nullptr;
                for (size_t i = 0; i < count; ++i)
                {
                    Dwg::LwPolylineVertex v;
                    v.Location = { pts[i].x, pts[i].y };
                    v.Bulge    = i < pl.Bulges.size() ? pl.Bulges[i] : 0.0;
                    if (hasSegWidths)
                    {
                        v.StartWidth = segStartW != nullptr && i < segStartW->size() ? (*segStartW)[i] : 0.0;
                        v.EndWidth   = segEndW   != nullptr && i < segEndW->size()   ? (*segEndW)[i]   : 0.0;
                    }
                    d->Vertices.push_back(v);
                }
                d->Elevation = pts.front().z;
                // 有逐段宽度时不再写恒定宽度（组码 43）：两者同时存在会让顶点宽度被覆盖
                d->ConstantWidth = (hasSegWidths && HasNonZero(*segStartW, *segEndW)) ? 0.0 : (width >= 1.0 ? width : 0.0);
                d->Normal        = NormalOf(e);
                return Finish(e, std::move(d));
            }

            static bool HasNonZero(const std::vector<double>& a, const std::vector<double>& b)
            {
                for (double v : a) if (v != 0.0) return true;
                for (double v : b) if (v != 0.0) return true;
                return false;
            }

            // 填充：只有一条多段线边的环写成多段线路径，其余写成直线 / 圆弧 / 椭圆弧 / 样条边；
            // 图案线族按场景里的比例、角度换算成 DWG 存的最终值（导入的逆过程，见 Importer::FamilyOf）
            std::unique_ptr<Dwg::Entity> ConvertHatch(const Entity& e, const HatchEntity& h)
            {
                auto d = std::make_unique<Dwg::Hatch>();
                bool hasElevation = false;
                auto elevation = [&](const Math::Point3& p)
                {
                    if (!hasElevation) { d->Elevation = p.z; hasElevation = true; }
                };
                auto addLine = [](Dwg::HatchBoundaryPath& path, const Math::Point3& a, const Math::Point3& b)
                {
                    auto line = std::make_unique<Dwg::HatchBoundaryPathLine>();
                    line->Start = { a.x, a.y };
                    line->End   = { b.x, b.y };
                    path.Edges.push_back(std::move(line));
                };

                for (const auto& loop : h.GetLoops())
                {
                    Dwg::HatchBoundaryPath path;
                    path.Flags = Dwg::BoundaryPathFlags::External;
                    if (loop.Edges.size() == 1 && loop.Edges[0].Type == HatchEdge::Kind::Poly)
                    {
                        const Polyline& pl = loop.Edges[0].Poly;
                        std::size_t count = pl.Points.size();
                        if (count < 2)
                            continue;
                        elevation(pl.Points.front());
                        if (count >= 3 && std::hypot(pl.Points.front().x - pl.Points.back().x, pl.Points.front().y - pl.Points.back().y) < 1e-9)
                            --count;   // 末点与起点重合：闭合段的凸度落到末顶点上
                        auto edge = std::make_unique<Dwg::HatchBoundaryPathPolyline>();
                        edge->IsClosed = true;
                        for (std::size_t i = 0; i < count; ++i)
                            edge->Vertices.push_back({ pl.Points[i].x, pl.Points[i].y, BulgeToCad(pl.SegBulge(static_cast<int>(i))) });
                        path.Flags = path.Flags | Dwg::BoundaryPathFlags::Polyline;
                        path.Edges.push_back(std::move(edge));
                    }
                    else
                    {
                        for (const auto& edge : loop.Edges)
                        {
                            if (edge.Type == HatchEdge::Kind::Poly)
                            {
                                const Polyline& pl = edge.Poly;
                                for (int i = 0; i + 1 < static_cast<int>(pl.Points.size()); ++i)
                                {
                                    const Math::Point3& a = pl.Points[i];
                                    const Math::Point3& b = pl.Points[i + 1];
                                    elevation(a);
                                    const ArcGeom g = Polyline::ComputeArc(a, b, pl.SegBulge(i));
                                    if (!pl.SegIsArc(i) || g.Radius <= 0.0)
                                    {
                                        addLine(path, a, b);
                                        continue;
                                    }
                                    // 顺时针弧存角度的相反数（见 Importer::ConvertHatchPath）
                                    auto arc = std::make_unique<Dwg::HatchBoundaryPathArc>();
                                    arc->Center = { g.Center.x, g.Center.y };
                                    arc->Radius = g.Radius;
                                    arc->CounterClockWise = g.SweepAngle > 0.0;
                                    arc->StartAngle = arc->CounterClockWise ? g.StartAngle : -g.StartAngle;
                                    arc->EndAngle   = arc->CounterClockWise ? g.StartAngle + g.SweepAngle : -(g.StartAngle + g.SweepAngle);
                                    path.Edges.push_back(std::move(arc));
                                }
                            }
                            else if (edge.Type == HatchEdge::Kind::EllipseArc)
                            {
                                const Ellipse& el = edge.Ell;
                                elevation(el.Center);
                                auto ell = std::make_unique<Dwg::HatchBoundaryPathEllipse>();
                                ell->Center = { el.Center.x, el.Center.y };
                                double rot = el.Rotation, a0 = edge.A0, a1 = edge.A1;
                                double major = el.RadiusX, minor = el.RadiusY;
                                if (minor > major)   // 长轴沿 Y：转 90°，参数角相应减 90°
                                {
                                    std::swap(major, minor);
                                    rot += kPi / 2.0;
                                    a0 -= kPi / 2.0;
                                    a1 -= kPi / 2.0;
                                }
                                ell->MajorAxisEndPoint = { major * std::cos(rot), major * std::sin(rot) };
                                ell->RadiusRatio       = major > 0.0 ? minor / major : 1.0;
                                ell->StartAngle        = a0;
                                ell->EndAngle          = a1;
                                ell->CounterClockWise  = true;
                                path.Edges.push_back(std::move(ell));
                            }
                            else if (edge.Spl.HasControlData())
                            {
                                const Spline& sp = edge.Spl;
                                elevation(sp.ControlPoints.front());
                                auto spl = std::make_unique<Dwg::HatchBoundaryPathSpline>();
                                spl->Degree     = sp.Degree;
                                spl->IsRational = sp.IsRational();
                                spl->IsPeriodic = (sp.Flags & SplineFlag_Periodic) != 0;
                                spl->Knots      = sp.Knots;
                                for (std::size_t i = 0; i < sp.ControlPoints.size(); ++i)   // 有理样条的权重放在 Z
                                    spl->ControlPoints.push_back({ sp.ControlPoints[i].x, sp.ControlPoints[i].y, spl->IsRational ? sp.Weights[i] : 0.0 });
                                path.Edges.push_back(std::move(spl));
                            }
                            else
                            {
                                // 只有拟合点的样条：离散成直线边
                                const std::vector<Math::Point3> pts = edge.Spl.Tessellate(24);
                                for (std::size_t i = 0; i + 1 < pts.size(); ++i)
                                {
                                    elevation(pts[i]);
                                    addLine(path, pts[i], pts[i + 1]);
                                }
                            }
                        }
                    }
                    if (!path.Edges.empty())
                        d->Paths.push_back(std::move(path));
                }
                if (d->Paths.empty())
                    return Skip(e);

                const HatchPattern& pat = h.GetPattern();
                d->Pattern.Name = pat.Solid ? std::string("SOLID") : pat.Name;
                d->IsSolid      = pat.Solid;
                d->PatternType  = Dwg::HatchPatternType::SolidFill;   // 组码 76 = 1：预定义图案（ACadSharp 的枚举名不准确）
                d->Style        = Dwg::HatchStyleType::Normal;
                if (!pat.Solid)
                {
                    const double scale = h.GetScale(), rad = h.GetAngle() * kPi / 180.0;
                    d->PatternScale = scale;
                    d->PatternAngle = rad;
                    for (const auto& f : pat.Families)
                    {
                        Dwg::HatchPatternLine line;
                        const double a = f.AngleDeg * kPi / 180.0 + rad;
                        const double bx = f.OffsetX * scale, by = f.OffsetY * scale;
                        const double dx = f.DeltaX * scale, dy = f.Spacing * scale;
                        line.Angle     = a;
                        line.BasePoint = { bx * std::cos(rad) - by * std::sin(rad), bx * std::sin(rad) + by * std::cos(rad) };
                        line.Offset    = { dx * std::cos(a) - dy * std::sin(a), dx * std::sin(a) + dy * std::cos(a) };
                        for (double dash : f.Dashes)
                            line.DashLengths.push_back(dash * scale);
                        d->Pattern.Lines.push_back(std::move(line));
                    }
                }
                return Finish(e, std::move(d));
            }

            // 区域覆盖：以多边形的外包矩形为整幅图像（1×1 像素），边界换成图像坐标（导入的逆过程，见 Importer::ConvertWipeout）
            std::unique_ptr<Dwg::Entity> ConvertWipeout(const Entity& e, const WipeoutEntity& w)
            {
                std::vector<Math::Point3> pts = w.GetPolyline().Points;
                if (pts.size() > 1 && pts.front().x == pts.back().x && pts.front().y == pts.back().y)
                    pts.pop_back();
                if (pts.size() < 3)
                    return Skip(e);
                double x0 = pts[0].x, y0 = pts[0].y, x1 = x0, y1 = y0;
                for (const auto& p : pts)
                {
                    x0 = std::min(x0, p.x); x1 = std::max(x1, p.x);
                    y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
                }
                const double width = x1 - x0, height = y1 - y0;
                if (width < 1e-9 || height < 1e-9)
                    return Skip(e);

                auto d = std::make_unique<Dwg::Wipeout>();
                d->InsertPoint   = { x0, y0, pts.front().z };
                d->UVector       = { width, 0.0, 0.0 };
                d->VVector       = { 0.0, height, 0.0 };
                d->Size          = { 1.0, 1.0 };
                d->Flags         = Dwg::ImageDisplayFlags::ShowImage | Dwg::ImageDisplayFlags::ShowNotAlignedImage
                                 | Dwg::ImageDisplayFlags::UseClippingBoundary;
                d->ClippingState = true;
                for (const auto& p : pts)
                    d->ClipBoundaryVertices.push_back({ (p.x - x0) / width - 0.5, 0.5 - (p.y - y0) / height });
                return Finish(e, std::move(d));
            }

            // 样条：有控制点时写控制点与节点（拟合点一并写出），否则只写拟合点（AutoCAD 打开时重算控制点）
            std::unique_ptr<Dwg::Entity> ConvertSpline(const Entity& e, const Spline& sp)
            {
                if (!sp.HasControlData() && sp.FitPoints.size() < 2)
                    return Skip(e);
                auto d = std::make_unique<Dwg::Spline>();
                d->Degree = sp.HasControlData() ? sp.Degree : 3;
                int flags = sp.Flags & (SplineFlag_Closed | SplineFlag_Periodic | SplineFlag_Planar | SplineFlag_Linear);
                if (sp.IsClosed())
                    flags |= SplineFlag_Closed;
                if (sp.HasControlData())
                {
                    for (const auto& p : sp.ControlPoints)
                        d->ControlPoints.push_back(ToXYZ(p));
                    d->Knots = sp.Knots;
                    if (sp.IsRational())
                    {
                        d->Weights = sp.Weights;
                        flags |= SplineFlag_Rational;
                    }
                }
                for (const auto& p : sp.FitPoints)
                    d->FitPoints.push_back(ToXYZ(p));
                if (!sp.HasControlData() && sp.Boundary == SplineBoundary::Clamped)
                {
                    d->StartTangent = ToXYZ(sp.StartTangent);
                    d->EndTangent   = ToXYZ(sp.EndTangent);
                }
                d->Flags                 = static_cast<Dwg::SplineFlags>(flags);
                d->FitTolerance          = sp.FitTolerance;
                d->KnotTolerance         = sp.KnotTolerance;
                d->ControlPointTolerance = sp.CtrlTolerance;
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
            int                                m_solidNoMesh   = 0;
            int                                m_solidTooLarge = 0;
            std::map<std::string, int>         m_skipped;
            std::vector<PendingClip>           m_pendingClips;   // Convert 产生、待实体入库后挂接的裁剪边界
        };
    }

    namespace
    {
        Dwg::CadVersion ToCadVersion(CadSaveVersion version)
        {
            switch (version)
            {
            case CadSaveVersion::R2000: return Dwg::CadVersion::AC1015;
            case CadSaveVersion::R2004: return Dwg::CadVersion::AC1018;
            case CadSaveVersion::R2007: return Dwg::CadVersion::AC1021;
            case CadSaveVersion::R2010: return Dwg::CadVersion::AC1024;
            case CadSaveVersion::R2013: return Dwg::CadVersion::AC1027;
            case CadSaveVersion::R2018: break;
            }
            return Dwg::CadVersion::AC1032;
        }

        // 文件版本 → 写出版本；2000 以前的旧格式按 2000 写出
        CadSaveVersion FromCadVersion(Dwg::CadVersion version)
        {
            switch (version)
            {
            case Dwg::CadVersion::AC1018: return CadSaveVersion::R2004;
            case Dwg::CadVersion::AC1021: return CadSaveVersion::R2007;
            case Dwg::CadVersion::AC1024: return CadSaveVersion::R2010;
            case Dwg::CadVersion::AC1027: return CadSaveVersion::R2013;
            case Dwg::CadVersion::AC1032: return CadSaveVersion::R2018;
            case Dwg::CadVersion::Unknown: return kDefaultCadSaveVersion;
            default:                       return CadSaveVersion::R2000;
            }
        }
    }

    const char* CadSaveVersionName(CadSaveVersion version)
    {
        switch (version)
        {
        case CadSaveVersion::R2000: return "2000";
        case CadSaveVersion::R2004: return "2004";
        case CadSaveVersion::R2007: return "2007";
        case CadSaveVersion::R2010: return "2010";
        case CadSaveVersion::R2013: return "2013";
        case CadSaveVersion::R2018: break;
        }
        return "2018";
    }

    bool CadSaveVersionSupported(CadFileKind kind, CadSaveVersion version)
    {
        // MiniDWG 没有实现 2007 DWG 的压缩与页格式（ACadSharp 也没有）
        return kind != CadFileKind::Dwg || version != CadSaveVersion::R2007;
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
                   std::shared_ptr<const CadSource>* source, const ImportProgress& progress)
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
        // 进度：读取占前 40%，转换占后 60%（大图纸上两者耗时大致如此）
        auto stage = [&progress](float from, float to) -> ImportProgress
        {
            if (!progress) return nullptr;
            return [&progress, from, to](float f) { progress(from + (to - from) * f); };
        };
        const auto t0 = std::chrono::steady_clock::now();
        std::unique_ptr<Dwg::CadDatabase> db = ReadDatabase(data, info.format, MakeNotify(&firstError), stage(0.0f, 0.4f));
        const auto t1 = std::chrono::steady_clock::now();

        if (!db)
            return fail(firstError.empty() ? "文件已损坏或版本不受支持" : firstError);

        if (version)
            *version = FromCadVersion(db->GetVersion());
        std::shared_ptr<CadSource> src;
        if (source != nullptr)
        {
            src = std::make_shared<CadSource>();
            src->Bytes   = data;
            src->Format  = info.format;
            src->Version = db->GetVersion();
        }
        if (progress) progress(0.4f);
        Importer(*db, scene, src.get(), stage(0.4f, 1.0f)).Run();
        if (progress) progress(1.0f);
        auto ms = [](auto d) { return std::chrono::duration<double, std::milli>(d).count(); };
        LOG_INFO("DWG/DXF 导入：读取 %.0f ms，转换 %.0f ms", ms(t1 - t0), ms(std::chrono::steady_clock::now() - t1));
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
        if (!CadSaveVersionSupported(kind, version))
        {
            LOG_WARN("DWG/DXF 导出：%s 格式的 DWG 不支持写出，改写为 2010 格式", CadSaveVersionName(version));
            version = CadSaveVersion::R2010;
        }
        const Dwg::CadVersion target = ToCadVersion(version);

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

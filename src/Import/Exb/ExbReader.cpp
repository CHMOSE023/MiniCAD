// EXB 顶层导入：容器 → 块流记录 → Scene 实体。
//
// 分层：
//   ExbContainer.cpp  容器 + 压缩信封（流路径 → 逻辑字节）
//   ExbGeometry.cpp   块流记录 → 中立几何（纯标准库，不碰 Scene）
//   本文件            中立几何 → MiniCAD 实体（唯一接触 Scene 的地方）
//
// 为什么分段：容器与实体格式会随 CAXA 版本分别演进，混在一个文件里改一处就会
// 牵动另一处；而且 ExbGeometry 能脱离 MiniCAD 单独编译成测试程序。
#include "Import/Exb/ExbReader.h"
#include "Import/Exb/ExbContainer.h"
#include "Import/Exb/ExbGeometry.h"

#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Entity/ImageEntity.hpp"
#include "Core/Entity/RayEntity.hpp"
#include "Core/Entity/XLineEntity.hpp"
#include "Core/Entity/BlockEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/Lineweight.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Scene/Scene.h"
#include "Text/Font/FontEngine.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <utility>

namespace MiniCAD::Exb
{
namespace
{
    bool EndsWithNoCase(const std::string& s, const std::string& tail)
    {
        if (s.size() < tail.size()) return false;
        for (std::size_t i = 0; i < tail.size(); ++i)
        {
            const unsigned char a = static_cast<unsigned char>(s[s.size() - tail.size() + i]);
            const unsigned char b = static_cast<unsigned char>(tail[i]);
            if (std::tolower(a) != std::tolower(b)) return false;
        }
        return true;
    }

    const StreamInfo* FindStream(const ExbReport& report, const char* name)
    {
        for (const StreamInfo& s : report.Streams)
            if (EndsWithNoCase(s.Path, name)) return &s;
        return nullptr;
    }

    // 线宽：EXB 存毫米（2024 版是 0 / 0.25 / 0.5，2013 版是 0.18 / 0.35），
    // MiniCAD 用 DXF 的 1/100 mm 枚举。取最接近的标准档；0 表示「随默认」。
    Lineweight SnapLineweight(double mm)
    {
        if (!(mm > 0.0)) return Lineweight::Default;
        static const Lineweight kCandidates[] = {
            Lineweight::W005, Lineweight::W009, Lineweight::W013, Lineweight::W015,
            Lineweight::W018, Lineweight::W020, Lineweight::W025, Lineweight::W030,
            Lineweight::W035, Lineweight::W040, Lineweight::W050, Lineweight::W053,
            Lineweight::W060, Lineweight::W070, Lineweight::W080, Lineweight::W090,
            Lineweight::W100, Lineweight::W106, Lineweight::W120, Lineweight::W140,
            Lineweight::W158, Lineweight::W200, Lineweight::W211,
        };
        const double target = mm * 100.0;
        Lineweight best = Lineweight::W025;
        double bestDiff = 1.0e18;
        for (Lineweight lw : kCandidates)
        {
            const double diff = std::fabs(static_cast<double>(LineweightToRaw(lw)) - target);
            if (diff < bestDiff) { bestDiff = diff; best = lw; }
        }
        return best;
    }

    // 模型空间块：句柄 -5，流名固定 /*BlockStg/*Blk_fffffffb。
    // 若样本没有这个名字，退回「最大的块流」——实体总在最大的那个里。
    const StreamInfo* FindEntityBlock(const ExbReport& report)
    {
        if (const StreamInfo* s = FindStream(report, "Blk_fffffffb")) return s;

        const StreamInfo* best = nullptr;
        for (const StreamInfo& s : report.Streams)
        {
            std::string lowered = s.Path;
            for (char& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lowered.find("/*blockstg/*blk_") == std::string::npos) continue;
            if (!best || s.LogicalSize > best->LogicalSize) best = &s;
        }
        return best;
    }

    Math::Point3 P3(const ExbPoint2& p, double z = 0.0)
    {
        return Math::Point3{p.X, p.Y, z};
    }
} // namespace

    bool IsExbPath(const std::string& path)
    {
        return IsExbExtension(path);
    }

    bool IsExbData(const std::uint8_t* data, std::size_t size)
    {
        return HasExbSignature(data, size);
    }

    bool IsLegacyExbData(const std::uint8_t* data, std::size_t size)
    {
        static const char kSignature[] = "CaxaEbForWindows";
        return data && size >= sizeof(kSignature) - 1 && std::memcmp(data, kSignature, sizeof(kSignature) - 1) == 0;
    }

    bool ImportExb(const std::vector<std::uint8_t>& data, Scene& scene,
                   std::string* error, ExbImportResult* result,
                   const std::function<void(float)>& progress,
                   const std::string& sourcePath)
    {

        if (error) error->clear();
        if (!HasExbSignature(data.data(), data.size()))
        {
            if (error) *error = "Not an EXB file: CFB/OLE2 signature is missing";
            return false;
        }

        ExbReport report;
        ReadOptions options;
        options.KeepLogicalBytes = true;   // 记录层要读块流字节
        if (!ProbeExb(data.data(), data.size(), report, error, options))
        {
            if (result) result->Report = std::move(report);
            return false;
        }
        if (progress) progress(0.45f);

        // ── 线型表 ──────────────────────────────────────────────────────
        // 样式流前半段是线型表、后半段是图层表，两者用各自的标记分隔，可以独立扫。
        // 图层的 LineTypeId 是**本文件的局部编号**（大图 −15 = 实线，旧代 pl3 是 −21），
        // 所以必须先把表本身读出来，再按 ID 对，不能按「−15 就是实线」硬编码。
        std::vector<ExbLineTypeDef> lineTypeDefs;
        if (const StreamInfo* style = FindStream(report, "StyleStream"))
            if (!style->Logical.empty())
                ParseLineTypeTable(style->Logical.data(), style->Logical.size(), lineTypeDefs);

        // EXB 线型 ID → 场景线型 ID。Pattern 要换成 MiniCAD 的**带符号**约定：
        //   >0 实线段长，<0 空白段长，0 画点。EXB 存的是无符号交替值（偶数位墨、
        //   奇数位空），所以这里逐位补上负号。Scale 是图案比例（2024 版把虚线的
        //   1.0 提到 6.0），乘进去才是实际铺出来的长度。
        std::map<std::int32_t, LineTypeID> lineTypeIds;
        for (const ExbLineTypeDef& def : lineTypeDefs)
        {
            LineTypeRecord rec;
            rec.Name        = def.Name;
            rec.Description = def.Alt.empty() ? def.Description : def.Alt;
            double period = 0.0;
            rec.Pattern.reserve(def.Dashes.size());
            for (std::size_t i = 0; i < def.Dashes.size(); ++i)
            {
                const double v = def.Dashes[i] * def.Scale;
                rec.Pattern.push_back((i % 2 == 0) ? v : -v);
                period += std::fabs(v);
            }
            rec.PatternLength = period;

            // 空图案的两条（ByLayer/ByBlock）在场景里也有保留 ID，直接对上，
            // 免得线型下拉框里出现叫 "ByLayer" 的自定义项。
            LineTypeID id;
            if (def.Alt == "ByLayer")
                id = LineTypeTable::ByLayerID;
            else if (def.Alt == "ByBlock")
                id = LineTypeTable::ByBlockID;
            else if (def.Dashes.empty() || def.Alt == "Continuous")
                id = LineTypeTable::ContinuousID;
            else
                id = scene.GetLineTypeTable().AddOrReplace(std::move(rec));

            lineTypeIds[def.Id] = id;
        }

        // ── 文字样式表 ──────────────────────────────────────────────────
        // 字体存的是字体名（Arial）或 .shx 文件名；字体名经 FontEngine::TrueTypeFileOf 换成文件名。
        // 与场景预置样式同名的（Standard）改写预置样式，不另建。
        std::map<std::int32_t, TextStyleID> textStyleIds;
        if (const StreamInfo* style = FindStream(report, "StyleStream"))
        {
            std::vector<ExbTextStyleDef> textStyleDefs;
            if (!style->Logical.empty())
                ParseTextStyles(style->Logical.data(), style->Logical.size(), textStyleDefs,
                                static_cast<unsigned>(std::strtoul(report.Info.Build.c_str(), nullptr, 10)));
            const auto isShx = [](const std::string& font)
            {
                return font.size() > 4 && (font.compare(font.size() - 4, 4, ".shx") == 0 ||
                                           font.compare(font.size() - 4, 4, ".SHX") == 0);
            };
            for (const ExbTextStyleDef& def : textStyleDefs)
            {
                if (def.Name.empty() || def.WesternFont.empty()) continue;
                TextStyleRecord rec;
                rec.Name        = def.Name;
                rec.FontFile    = isShx(def.WesternFont) ? def.WesternFont : FontEngine::TrueTypeFileOf(def.WesternFont);
                if (isShx(def.WesternFont) && isShx(def.CjkFont)) rec.BigFontFile = def.CjkFont;
                rec.Height      = def.Height > 0.0 ? def.Height : 0.0;
                rec.WidthFactor = def.WidthFactor > 0.0 ? def.WidthFactor : 1.0;
                rec.ObliqueDeg  = def.Oblique * 180.0 / 3.141592653589793;
                if (rec.FontFile.empty()) continue;

                TextStyleTable& table = scene.GetTextStyleTable();
                TextStyleID id = table.FindByName(rec.Name);
                if (id != TextStyleTable::InvalidID)
                    table.Update(id, rec);
                else
                    id = table.Add(rec);
                if (id != TextStyleTable::InvalidID) textStyleIds[def.Id] = id;
            }
        }

        // ── 图层表 ──────────────────────────────────────────────────────
        // 图层归属已经打通：实体记录头里的图层 ID 通过 ResolveLayerIndex 映射到样式流
        // 里的图层表。旧代小文件（pl3/lay6 那类）的句柄按定义顺序连续分配
        // （`句柄 = 首句柄 + 序号`），大图则混用「−20 句柄 + 45..53 正数持久 ID」，
        // 两种编码都在 ResolveLayerIndex 里处理，细节见 ExbGeometry.h。
        std::vector<ExbLayerDef> layerDefs;
        if (const StreamInfo* style = FindStream(report, "StyleStream"))
            if (!style->Logical.empty())
                ParseStyleStream(style->Logical.data(), style->Logical.size(), layerDefs);

        // EXB 的「0 Layer」在语义上就是默认层：复用场景里已有的 Default 层并改名，
        // 否则图层面板里会同时出现一个空的 "Default" 和一条扛着 7290 条实体的
        // "0 Layer"，用户改「Default」的显示属性时看不到任何变化。
        LayerID targetLayer = Layer::DefaultLayerID;
        bool hasTarget = false;
        std::vector<LayerID> layerIds;         // 与 layerDefs 同序，供实体分配用
        layerIds.reserve(layerDefs.size());
        for (const ExbLayerDef& def : layerDefs)
        {
            LayerID id = Layer::DefaultLayerID;
            Layer* defaultLayer = scene.GetLayerManager().GetLayer(Layer::DefaultLayerID);
            if (defaultLayer && !hasTarget && def.Name == "0 Layer")
            {
                defaultLayer->SetName(def.Name);
            }
            else
            {
                id = scene.GetLayerManager().AddLayer(def.Name);
            }

            // 图层颜色：样式流里存的是 0xBBGGRR（低 3 字节）。ByLayer 的实体据此显示，
            // 之前全部落到场景默认白，用户看到的颜色和 CAXA 不一致。
            if (Layer* created = scene.GetLayerManager().GetLayer(id))
            {
                const std::uint32_t w = def.ColorWord;
                if ((w & 0xFFFFFFu) != 0)
                    created->SetColor(EntityColor::FromRgb((w & 0xFFu) / 255.0,
                                                           ((w >> 8) & 0xFFu) / 255.0,
                                                           ((w >> 16) & 0xFFu) / 255.0));

                // 图层线型：按本文件的线型表把 EXB 局部 ID 换成场景 ID。
                // 换不到就留 Continuous —— 图层线型不允许 ByLayer/ByBlock。
                const auto lt = lineTypeIds.find(def.LineTypeId);
                if (lt != lineTypeIds.end() && lt->second > LineTypeTable::ContinuousID)
                    created->SetLineType(lt->second);

                // 图层线宽：EXB 存毫米（0 / 0.18 / 0.25 / 0.35 / 0.5），
                // MiniCAD 用 DXF 的 1/100 mm 枚举，取最接近的标准档。
                created->SetLineweight(SnapLineweight(def.LineWidth));
                created->SetVisible(def.Visible);
                created->SetLocked(def.Locked);
            }
            layerIds.push_back(id);
            if (!hasTarget)
            {
                targetLayer = id;
                hasTarget = true;
            }
        }

        // ── 几何 ────────────────────────────────────────────────────────
        const StreamInfo* block = FindEntityBlock(report);
        if (!block || block->Logical.empty())
        {
            if (error) *error = "EXB: model-space block stream is missing";
            if (result)
            {
                result->ContainerOk = true;
                result->Report = std::move(report);
            }
            return false;
        }

        // 版本号形如 12.5.0.5302；取不到主版本号时交给解析器从数据里看
        const unsigned productMajor = static_cast<unsigned>(std::strtoul(report.Info.Build.c_str(), nullptr, 10));

        ExbGeometryResult geo;
        if (!ParseBlockStream(block->Logical.data(), block->Logical.size(), geo, productMajor))
        {
            if (error) *error = "EXB: no entity record found in model-space block";
            if (result)
            {
                result->ContainerOk = true;
                result->Report = std::move(report);
            }
            return false;
        }
        if (progress) progress(0.75f);

        std::map<std::string, std::size_t> unclassified = geo.UnclassifiedByClass;

        // ── 图片定义 ────────────────────────────────────────────────────
        // 相对路径按图纸所在目录解析；没有相对路径或图纸路径时用保存时的绝对路径
        std::map<std::int32_t, std::string> imagePaths;
        for (const StreamInfo& s : report.Streams)
        {
            if (s.Logical.empty() || s.Path.find("/*ImageStg/*Img_") == std::string::npos) continue;
            ExbImageDef def;
            if (!ParseImageDef(s.Logical.data(), s.Logical.size(), def, productMajor)) continue;
            std::string path = def.Path;
            const std::size_t slash = sourcePath.find_last_of("/\\");
            if (!def.RelativePath.empty() && slash != std::string::npos)
            {
                std::string relative = def.RelativePath;
                if (relative.rfind(".\\", 0) == 0 || relative.rfind("./", 0) == 0) relative.erase(0, 2);
                path = sourcePath.substr(0, slash + 1) + relative;
            }
            imagePaths[def.Id] = std::move(path);
        }

        // ── 块定义 ──────────────────────────────────────────────────────
        // 全部块定义在 /*BlockStg/*GBlock 一个流里；模型空间以外的 /*BlockStg/*Blk_<句柄> 也按块读。
        std::vector<ExbBlockDef> blockDefs;
        for (const StreamInfo& s : report.Streams)
        {
            if (&s == block || s.Logical.empty()) continue;
            std::string lowered = s.Path;
            for (char& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lowered.find("/*blockstg/*gblock") == std::string::npos &&
                lowered.find("/*blockstg/*blk_") == std::string::npos) continue;
            std::vector<ExbBlockDef> defs;
            if (!ParseBlockDefinitions(s.Logical.data(), s.Logical.size(), defs, productMajor, &unclassified)) continue;
            for (ExbBlockDef& def : defs) blockDefs.push_back(std::move(def));
        }

        struct SceneBlock
        {
            BlockID      Id    = BlockTable::InvalidID;
            BlockEntity* Block = nullptr;
        };
        std::map<std::int32_t, SceneBlock> sceneBlocks;
        for (const ExbBlockDef& def : blockDefs)
        {
            if (sceneBlocks.count(def.Id)) continue;
            std::string name = def.Name.empty() ? "EXB_" + std::to_string(def.Id) : def.Name;
            while (scene.GetBlockTable().Get(name)) name += "_";
            SceneBlock entry;
            entry.Id    = scene.GetBlockTable().Create(name);
            entry.Block = scene.GetBlockTable().Find(entry.Id);
            if (!entry.Block) continue;
            if (!name.empty() && name.front() == '*') entry.Block->SetFlags(BlockEntity::Anonymous);
            sceneBlocks.emplace(def.Id, entry);
        }

        EntityAttr baseAttr;
        baseAttr.LayerId = targetLayer;

        // ── 未映射的图层 ID ─────────────────────────────────────────────
        // 样式流里没有的图层 ID 各建一个占位层，不并进第一个图层：否则关掉那个图层会连带隐藏它们。
        std::map<std::int32_t, LayerID> extraLayers;
        const std::function<void(const std::vector<ExbGeometryItem>&)> collectLayers =
            [&](const std::vector<ExbGeometryItem>& items)
        {
            for (const ExbGeometryItem& item : items)
            {
                collectLayers(item.Children);
                if (!item.Style.Valid) continue;
                const std::int32_t layerId = item.Style.LayerId;
                if (extraLayers.count(layerId)) continue;
                if (ResolveLayerIndex(layerId, layerDefs) >= 0) continue;
                extraLayers.emplace(layerId,
                                    scene.GetLayerManager().AddLayer("Layer " + std::to_string(layerId)));
            }
        };
        collectLayers(geo.Items);
        for (const ExbBlockDef& def : blockDefs) collectLayers(def.Items);

        auto attrFor = [&](const ExbGeometryItem& item)
        {
            EntityAttr a = baseAttr;
            const ExbGeometryStyle& style = item.Style;
            if (!style.Valid) return a;

            // 颜色字低三字节是 0xBBGGRR（与图层颜色同一种存法）
            if (style.ColorByBlock())
                a.Color = EntityColor::ByBlock();
            else if (!style.ColorByLayer())
                a.Color = EntityColor::FromRgb((style.ColorWord & 0xFFu) / 255.0,
                                               ((style.ColorWord >> 8) & 0xFFu) / 255.0,
                                               ((style.ColorWord >> 16) & 0xFFu) / 255.0);

            if (style.LineTypeMode == 1)
                a.LineType = LineTypeTable::ByBlockID;
            else if (style.LineTypeMode == 2)
            {
                const auto lt = lineTypeIds.find(style.LineTypeId);
                if (lt != lineTypeIds.end()) a.LineType = lt->second;
            }
            if (style.WidthMode == 1)
                a.Lineweight = Lineweight::ByBlock;
            else if (style.WidthMode == 2)
                a.Lineweight = SnapLineweight(style.LineWidth);

            const int idx = ResolveLayerIndex(style.LayerId, layerDefs);
            if (idx >= 0 && static_cast<std::size_t>(idx) < layerIds.size())
                a.LayerId = layerIds[static_cast<std::size_t>(idx)];
            else
            {
                const auto it = extraLayers.find(style.LayerId);
                if (it != extraLayers.end()) a.LayerId = it->second;
            }
            return a;
        };

        const auto styleOf = [&](const ExbGeometryItem& item)
        {
            const auto it = textStyleIds.find(item.StyleId);
            return it != textStyleIds.end() ? it->second : scene.GetCurrentTextStyle();
        };

        // 建不出实体（退化、空文字、块找不到）时返回空
        const auto makeEntity = [&](const ExbGeometryItem& item) -> std::unique_ptr<Entity>
        {
            std::unique_ptr<Entity> e;
            switch (item.Kind)
            {
            case ExbGeometryKind::Line:
            {
                if (item.Points.size() < 2) break;
                const ExbPoint2& a = item.Points.front();
                const ExbPoint2& b = item.Points.back();
                if (std::abs(a.X - b.X) < 1e-9 && std::abs(a.Y - b.Y) < 1e-9) break;
                e = std::make_unique<LineEntity>(scene.NextObjectID(), P3(a), P3(b));
                break;
            }
            case ExbGeometryKind::Polyline:
            {
                if (item.Points.size() < 2) break;
                std::vector<Math::Point3> pts;
                pts.reserve(item.Points.size());
                for (const ExbPoint2& p : item.Points) pts.push_back(P3(p));
                if (item.Bulges.size() != item.Points.size())
                {
                    // PolylineEntity 的恒定宽度默认是 1 个图形单位，不清零会画成宽带
                    auto plain = std::make_unique<PolylineEntity>(scene.NextObjectID(), std::move(pts));
                    plain->SetWidth(0.0);
                    e = std::move(plain);
                    break;
                }
                // MiniCAD 的多段线没有闭合标志：闭合的把起点再放一次
                if (item.Closed) pts.push_back(pts.front());
                const std::size_t segs = pts.size() - 1;
                std::vector<double> bulges(item.Bulges.begin(), item.Bulges.begin() + segs);
                std::vector<double> startW(item.StartWidths.begin(), item.StartWidths.begin() + segs);
                std::vector<double> endW(item.EndWidths.begin(), item.EndWidths.begin() + segs);
                auto pl = std::make_unique<PolylineEntity>(scene.NextObjectID(), std::move(pts), std::move(bulges));
                pl->SetWidth(0.0);
                const auto nonZero = [](double w) { return w != 0.0; };
                if (std::any_of(startW.begin(), startW.end(), nonZero) || std::any_of(endW.begin(), endW.end(), nonZero))
                    pl->SetSegmentWidths(std::move(startW), std::move(endW));
                e = std::move(pl);
                break;
            }
            case ExbGeometryKind::Circle:
                e = std::make_unique<CircleEntity>(scene.NextObjectID(), P3(item.Center), item.Radius);
                break;
            case ExbGeometryKind::Arc:
                e = std::make_unique<ArcEntity>(scene.NextObjectID(), P3(item.Center),
                                                item.Radius, item.StartAngle, item.EndAngle);
                break;
            case ExbGeometryKind::Point:
                if (item.Points.empty()) break;
                e = std::make_unique<PointEntity>(scene.NextObjectID(), P3(item.Points.front()));
                break;
            case ExbGeometryKind::Text:
                if (item.Plain.empty()) break;
                // 有换行宽度的是多行文字：内容里的格式码与 AutoCAD 的多行文字相同，原样交给 MTextEntity 排版
                if (item.BoxWidth > 0.0)
                {
                    auto mtext = std::make_unique<MTextEntity>(scene.NextObjectID(), styleOf(item), item.Raw,
                                                               P3(item.Insert), item.Height, item.Rotation, item.BoxWidth);
                    mtext->SetAttachment(static_cast<MTextAttachment>(item.Align + 1));
                    e = std::move(mtext);
                    break;
                }
                e = std::make_unique<TextEntity>(scene.NextObjectID(), P3(item.Insert), item.Plain,
                                                 static_cast<float>(item.Height), static_cast<float>(item.Rotation),
                                                 styleOf(item));
                break;
            case ExbGeometryKind::Ellipse:
            {
                const Ellipse ellipse(P3(item.Center), item.Radius, item.RadiusMinor, item.Rotation,
                                      item.StartAngle, item.EndAngle);
                e = std::make_unique<EllipseEntity>(scene.NextObjectID(), ellipse);
                break;
            }
            case ExbGeometryKind::Ray:
                if (item.Points.empty()) break;
                e = std::make_unique<RayEntity>(scene.NextObjectID(), P3(item.Points.front()),
                                                Math::Vec3{ item.Direction.X, item.Direction.Y, 0.0 });
                break;
            case ExbGeometryKind::XLine:
                if (item.Points.empty()) break;
                e = std::make_unique<XLineEntity>(scene.NextObjectID(), P3(item.Points.front()),
                                                  Math::Vec3{ item.Direction.X, item.Direction.Y, 0.0 });
                break;
            case ExbGeometryKind::Image:
            {
                if (item.Points.size() != 2) break;
                const auto path = imagePaths.find(item.ImageRef);
                const ExbPoint2& a = item.Points[0];
                const ExbPoint2& b = item.Points[1];
                auto image = std::make_unique<ImageEntity>(scene.NextObjectID(),
                                                           path != imagePaths.end() ? path->second : std::string{},
                                                           P3(a), P3(ExbPoint2{b.X, a.Y}), P3(b), P3(ExbPoint2{a.X, b.Y}));
                image->SetShowFrame(true);
                e = std::move(image);
                break;
            }
            case ExbGeometryKind::Insert:
            {
                const auto it = sceneBlocks.find(item.BlockRef);
                if (it == sceneBlocks.end()) break;
                auto insert = std::make_unique<InsertEntity>(scene.NextObjectID(), it->second.Id,
                                                             it->second.Block->GetName(), P3(item.Insert));
                insert->SetScale({ item.ScaleX, item.ScaleY, 1.0 });
                insert->SetRotation(item.Rotation);
                insert->SetBlock(it->second.Block);
                e = std::move(insert);
                break;
            }
            default:
                break;
            }
            if (e) e->SetAttr(attrFor(item));
            return e;
        };

        // 一个条目可以出多个实体：Group 只有子条目，块参照后面跟着它的属性文字
        const std::function<std::size_t(const ExbGeometryItem&, const std::function<void(std::unique_ptr<Entity>)>&)> emit =
            [&](const ExbGeometryItem& item, const std::function<void(std::unique_ptr<Entity>)>& sink) -> std::size_t
        {
            std::size_t count = 0;
            // 实心 / 渐变填充：建成填充实体，不再另画边界
            if (item.Kind == ExbGeometryKind::Group && item.Filled && !item.Loops.empty())
            {
                std::vector<Polyline> loops;
                for (const ExbFillLoop& loop : item.Loops)
                {
                    std::vector<Math::Point3> pts;
                    pts.reserve(loop.Points.size());
                    for (const ExbPoint2& p : loop.Points) pts.push_back(P3(p));
                    loops.emplace_back(std::move(pts), loop.Bulges);
                }
                auto hatch = std::make_unique<HatchEntity>(scene.NextObjectID(), std::move(loops));
                if (!item.GradientName.empty())
                {
                    const auto color = [](std::uint32_t word)
                    {
                        return Math::Color4{ (word & 0xFFu) / 255.0, ((word >> 8) & 0xFFu) / 255.0,
                                             ((word >> 16) & 0xFFu) / 255.0, 1.0 };
                    };
                    HatchGradient gradient;
                    gradient.Enabled     = true;
                    gradient.Name        = item.GradientName;
                    gradient.Type        = HatchGradient::ParseType(item.GradientName);
                    gradient.Angle       = item.GradientAngle * 180.0 / 3.141592653589793;
                    gradient.Shift       = item.GradientShift;
                    gradient.SingleColor = item.GradientSingleColor;
                    gradient.ColorTint   = item.GradientTint;
                    gradient.Stops       = { { 0.0, color(item.GradientColors[0]) }, { 1.0, color(item.GradientColors[1]) } };
                    hatch->SetGradient(std::move(gradient));
                }
                hatch->SetAttr(attrFor(item));
                sink(std::move(hatch));
                return 1;
            }
            if (item.Kind != ExbGeometryKind::Group)
            {
                if (auto e = makeEntity(item))
                {
                    sink(std::move(e));
                    ++count;
                }
            }
            for (const ExbGeometryItem& child : item.Children) count += emit(child, sink);
            return count;
        };

        std::size_t made = 0;
        std::size_t skipped = geo.UnclassifiedCount;
        for (const ExbBlockDef& def : blockDefs)
        {
            const auto it = sceneBlocks.find(def.Id);
            if (it == sceneBlocks.end()) continue;
            BlockEntity* target = it->second.Block;
            for (const ExbGeometryItem& item : def.Items)
                emit(item, [target](std::unique_ptr<Entity> e) { target->AddEntity(std::move(e)); });
        }
        for (const ExbGeometryItem& item : geo.Items)
        {
            const std::size_t count = emit(item, [&scene](std::unique_ptr<Entity> e) { scene.AddEntity(std::move(e)); });
            made += count;
            if (count == 0) ++skipped;
        }

        scene.ResolveAllInserts();
        scene.MarkDirty();
        if (progress) progress(1.0f);

        if (result)
        {
            result->ContainerOk    = true;
            result->EntityCount    = made;
            result->SkippedRecords = skipped;
            for (const auto& [classId, count] : unclassified)
            {
                const std::string name = UnsupportedClassName(classId);
                result->Unsupported[name.empty() ? "class " + classId.substr(0, 8) : name] += count;
            }
            result->Report         = std::move(report);
        }
        return true;
    }
}

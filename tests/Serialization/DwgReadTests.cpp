#include "Dwg/Read/DwgReader.h"
#include "Dxf/Read/DxfReader.h"
#include "Dxf/Write/DxfWriter.h"
#include "CompareDatabases.h"
#include "TestData.h"
#include "TestFramework.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace MiniDWG;
using namespace MiniDWG::Test;

TEST(DwgRead_AllSamples_NoWarnings)
{
    for (const char* v : { "AC1014", "AC1015", "AC1018", "AC1021", "AC1024", "AC1027", "AC1032" })
    {
        const auto data = ReadFileAll(SamplePath(std::string("sample_") + v + ".dwg"));
        std::vector<std::string> warnings;
        DwgReadOptions options;
        options.Notify = [&](NotificationType type, std::string_view message) {
            if (type != NotificationType::Info)
                warnings.emplace_back(message);
        };
        auto db = ReadDwg(data, options);
        CHECK(db != nullptr);
        if (!db)
            continue;
        CHECK(db->GetVersion() == ParseVersionString(v));
        CHECK(warnings.empty());
        for (const std::string& w : warnings)
            std::printf("    %s：%s\n", v, w.c_str());
        // 已建模的实体 155 个（另有原样保留的未建模实体）
        CHECK(db->ModelSpace() != nullptr);
        if (db->ModelSpace() != nullptr)
        {
            const auto& es = db->ModelSpace()->Entities;
            CHECK(std::count_if(es.begin(), es.end(), [&](Handle e) { return db->FindAs<UnknownEntity>(e) == nullptr; }) == 155);
        }
        for (const auto& [h, obj] : db->Objects())
        {
            CHECK(h < db->GetHandleSeed());
            if (auto* e = dynamic_cast<const Entity*>(obj.get()))
            {
                const bool ok = db->FindAs<Layer>(e->LayerHandle) != nullptr && db->Find(e->OwnerHandle) != nullptr;
                CHECK(ok);
                if (!ok)
                    std::printf("    %s：%s %s 的图层或所有者不存在\n", v, std::string(e->GetDxfName()).c_str(),
                                HandleText(h).c_str());
            }
        }
    }
}

TEST(DwgRead_TruncatedOrInvalid_NoCrash)
{
    const auto data = ReadFileAll(SamplePath("sample_AC1018.dwg"));
    for (std::size_t size : { std::size_t(0), std::size_t(100), data.size() / 3, data.size() / 2 })
    {
        std::vector<std::uint8_t> part(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(size));
        (void)ReadDwg(part);    // 只要求不崩溃
    }
    const std::string text = "0\nSECTION\n";
    CHECK(ReadDwg({ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() }) == nullptr);
}

namespace
{
    std::unique_ptr<CadDatabase> ReadDxfSample(const std::string& name)
    {
        const auto data = ReadFileAll(SamplePath(name));
        return ReadDxf(data);
    }

    // AutoCAD 按文件格式加的 ACAD 簿记扩展数据：去掉后再比较
    //   DWG（R2000 等旧格式）：RTTcAl（真彩色）、RTMaterial（材质）——旧格式表达不了的属性，供 AutoCAD 读回；
    //                         写 DXF 时 AutoCAD 不写这些
    //   DBCOLOR：DBColXD（颜色索引、RGB、名称），R2000 的 DXF 中没有
    //   DXF：DbSaveVer、AcDbSavedByObjectVersion——保存版本
    ExtendedData StripBookkeeping(const CadDatabase& db, const ExtendedData& x)
    {
        const auto* app = db.FindAs<AppId>(x.AppIdHandle);
        if (app == nullptr || app->Name != "ACAD")
            return x;
        ExtendedData out{ x.AppIdHandle, {} };
        for (std::size_t i = 0; i < x.Records.size(); ++i)
        {
            const auto* text = std::get_if<std::string>(&x.Records[i].Value);
            std::size_t skip = 0;
            if (x.Records[i].Code == 1000 && text != nullptr)
            {
                if (*text == "RTTcAl" || *text == "DBColXD")
                    skip = 3;
                else if (*text == "RTMaterial" || *text == "DbSaveVer" || *text == "AcDbSavedByObjectVersion")
                    skip = 1;
            }
            if (skip > 0)
            {
                i += skip;
                continue;
            }
            out.Records.push_back(x.Records[i]);
        }
        return out;
    }

    bool SameXY(const XY& a, const XY& b) { return Near(a, b); }

    // DWG 与 DXF 的已知差异（a 为 DXF 读入的对象，b 为 DWG 读入的对象）
    bool KnownDwgDxfDiff(const CadDatabase& dxf, const CadObject& a, const CadObject& b, std::string_view what,
                         CadVersion version)
    {
        auto starts = [&](std::string_view prefix) { return what.substr(0, prefix.size()) == prefix; };

        // 根字典中的保存信息（GUID、保存时的文件路径），每次保存都不同
        if (dynamic_cast<const XRecord*>(&a)
            && dxf.FindDictionaryEntry(dxf.RootDictionary(), "ACAD_LAST_SAVED_VERSION_INFO") == &a)
            return true;
        // R2004 之前的 DWG 中 CMC 颜色只有索引：多重引线文字背景的真彩色在 DXF 中有，DWG 中是 AutoCAD 选的索引色
        // （与 Color::ApproxIndex 的结果不一定相同，如 C8C8C8 存为 9）
        if (auto* ca = dynamic_cast<const MultiLeaderObjectContextData*>(&a);
            ca && version < CadVersion::AC1018 && starts("BackgroundFillColor"))
        {
            const Color& x = ca->BackgroundFillColor;
            const Color& y = static_cast<const MultiLeaderObjectContextData&>(b).BackgroundFillColor;
            return x.IsTrueColor() && !y.IsTrueColor();
        }
        // 多边形裁剪边界：DXF 中多一个与首点相同的闭合点
        if (auto* wa = dynamic_cast<const CadWipeoutBase*>(&a); wa && starts("ClipBoundaryVertices"))
        {
            const auto& va = wa->ClipBoundaryVertices;
            const auto& vb = static_cast<const CadWipeoutBase&>(b).ClipBoundaryVertices;
            return va.size() == vb.size() + 1 && vb.size() > 2 && SameXY(va.front(), va.back())
                && std::equal(vb.begin(), vb.end(), va.begin(), SameXY);
        }
        if (auto* la = dynamic_cast<const Leader*>(&a))
        {
            // R2010 起 DWG 不存文字框尺寸（AutoCAD 由注释算出）
            if (version >= CadVersion::AC1024 && (starts("TextHeight") || starts("TextWidth")))
                return true;
            // 钩线：AutoCAD 写 DXF 时在末点前插入钩线顶点（DWG 中不存，打开时按注释重算）
            if (starts("Vertices"))
            {
                const auto& va = la->Vertices;
                const auto& vb = static_cast<const Leader&>(b).Vertices;
                if (va.size() != vb.size() + 1 || vb.size() < 2)
                    return false;
                for (std::size_t i = 0; i + 1 < vb.size(); ++i)
                {
                    if (!Near(va[i], vb[i]))
                        return false;
                }
                const XYZ hook = va[va.size() - 2];
                const XYZ end = va.back();
                const XYZ d{ end.X - hook.X, end.Y - hook.Y, end.Z - hook.Z };
                const XYZ x = la->HorizontalDirection;
                const double cross = d.X * x.Y - d.Y * x.X;
                return Near(end, vb.back()) && std::abs(cross) <= 1e-9 * std::max(1.0, std::abs(d.X) + std::abs(d.Y));
            }
        }
        // 线型文字分段的 8 位（文字保持正向，R2013 引入）：DWG 各版本都存，AutoCAD 写 R2013 之前的 DXF 时去掉
        if (auto* lta = dynamic_cast<const LineType*>(&a); lta && version < CadVersion::AC1027 && starts("Segments"))
        {
            const auto& sa = lta->Segments;
            const auto& sb = static_cast<const LineType&>(b).Segments;
            if (sa.size() != sb.size())
                return false;
            for (std::size_t i = 0; i < sa.size(); ++i)
            {
                if ((static_cast<int>(sa[i].Flags) & ~8) != (static_cast<int>(sb[i].Flags) & ~8) || !Near(sa[i].Length, sb[i].Length)
                    || !Near(sa[i].Rotation, sb[i].Rotation) || !Near(sa[i].Scale, sb[i].Scale) || !Near(sa[i].Offset, sb[i].Offset)
                    || sa[i].Text != sb[i].Text)
                    return false;
            }
            return true;
        }
        // R2018 动态分栏的高度：DWG 中存的是文字范围的负值，AutoCAD 写 DXF 时为 0
        if (auto* ma = dynamic_cast<const MText*>(&a); ma && starts("ColumnData"))
        {
            const auto& ca = ma->ColumnData;
            const auto& cb = static_cast<const MText&>(b).ColumnData;
            return ca.ColumnType == cb.ColumnType && ca.ColumnCount == cb.ColumnCount && ca.Heights.size() == cb.Heights.size()
                && std::all_of(ca.Heights.begin(), ca.Heights.end(), [](double h) { return h == 0.0; })
                && std::all_of(cb.Heights.begin(), cb.Heights.end(), [](double h) { return h < 0.0; });
        }

        // 匿名块编号不存于 DWG，AutoCAD 打开时重编
        if ((dynamic_cast<const Block*>(&a) || dynamic_cast<const BlockRecord*>(&a)) && starts("Name"))
            return true;
        // 形文件样式名称为空，DXF 中无法按名称引用
        if (dynamic_cast<const Shape*>(&a) && starts("ShapeStyle"))
            return true;
        // 标注样式的颜色：DXF 只存近似的索引色，DWG 中是真彩色
        if (dynamic_cast<const DimensionStyle*>(&a) && (starts("DimensionLineColor") || starts("ExtensionLineColor") || starts("TextColor")))
            return true;
        if (auto* sb = dynamic_cast<const Spline*>(&b))
        {
            // 拟合点样条：DWG 中存拟合数据，AutoCAD 写 DXF 时换成算出的控制点（库不做几何运算）
            if (!sb->FitPoints.empty() && static_cast<const Spline&>(a).FitPoints.empty()
                && (starts("ControlPoints") || starts("FitPoints") || starts("KnotTolerance") || starts("ControlPointTolerance")
                    || starts("StartTangent") || starts("EndTangent")))
                return true;
            // R2013 之前的 DWG 没有 Flags1（DXF 组码 70 的高位）
            if (version < CadVersion::AC1027 && starts("Flags"))
            {
                const int fa = static_cast<int>(static_cast<const Spline&>(a).Flags);
                const int fb = static_cast<int>(sb->Flags);
                return (fa & 0x7F) == (fb & 0x7F);
            }
        }
        return false;
    }
}

// 同一张图的 DWG 与 ASCII DXF 读入后逐对象比较。两份文件各有一批句柄不同的对象（另存时重新分配），
// 只比较两边都有的
TEST(DwgRead_MatchesDxf)
{
    for (const char* v : { "AC1015", "AC1018", "AC1021", "AC1024", "AC1027", "AC1032" })
    {
        const auto dwgData = ReadFileAll(SamplePath(std::string("sample_") + v + ".dwg"));
        auto dwg = ReadDwg(dwgData);
        auto dxf = ReadDxfSample(std::string("sample_") + v + "_ascii.dxf");
        CHECK(dwg != nullptr && dxf != nullptr);
        if (!dwg || !dxf)
            continue;

        const CadVersion version = ParseVersionString(v);
        Comparer cmp{ *dxf, *dwg };
        cmp.IgnoreReferencedFlag = true;
        cmp.CommonOnly = true;
        cmp.MaxDiffs = 200;
        cmp.NormalizeXData = StripBookkeeping;
        cmp.KnownDiff = [&](const CadObject& a, const CadObject& b, std::string_view what) {
            return KnownDwgDxfDiff(*dxf, a, b, what, version);
        };
        cmp.Run();
        CHECK(cmp.Diffs.empty());
        for (const std::string& d : cmp.Diffs)
            std::printf("    %s 差异：%s\n", v, d.c_str());

        // 共同对象应占绝大多数（AC1015 的样例约有 200 个对象另存时换了句柄）
        std::size_t common = 0;
        for (const auto& [h, obj] : dxf->Objects())
            common += dwg->Find(h) != nullptr;
        CHECK(common * 4 >= dxf->Objects().size() * 3);
        if (common * 4 < dxf->Objects().size() * 3)
            std::printf("    %s 共同对象 %zu / %zu\n", v, common, dxf->Objects().size());
    }
}

// DWG 读入 → 写 DXF → 读回，逐对象比较（R14 写为 R2000）
TEST(DwgRead_WriteDxf_RoundTrip)
{
    for (const char* v : { "AC1014", "AC1015", "AC1018", "AC1021", "AC1024", "AC1027", "AC1032" })
    {
        for (bool binary : { false, true })
        {
            const auto dwgData = ReadFileAll(SamplePath(std::string("sample_") + v + ".dwg"));
            auto dwg = ReadDwg(dwgData);
            CHECK(dwg != nullptr);
            if (!dwg)
                continue;

            std::vector<std::string> warnings;
            DxfWriteOptions writeOptions;
            writeOptions.Binary = binary;
            writeOptions.WriteAllHeaderVariables = true;
            writeOptions.Notify = [&](NotificationType type, std::string_view message) {
                if (type != NotificationType::Info)
                    warnings.emplace_back(message);
            };
            const auto bytes = WriteDxf(*dwg, writeOptions);
            DxfReadOptions readOptions;
            readOptions.Notify = writeOptions.Notify;
            auto back = ReadDxf(bytes, readOptions);
            CHECK(back != nullptr);
            if (!back)
                continue;
            CHECK(warnings.empty());
            for (const std::string& w : warnings)
                std::printf("    %s 警告：%s\n", v, w.c_str());

            const CadVersion version = dwg->GetVersion();
            Comparer cmp{ *dwg, *back };
            cmp.RawNotWritten = true;   // DWG 中原样保留的对象写不进 DXF
            cmp.IgnoreReferencedFlag = true;
            cmp.MaxDiffs = 100;
            cmp.SkipRule = [version](const CadObject& a, const DxfPropertyInfo& p) {
                return SkipNotRoundTripped(a, p, version < CadVersion::AC1015);
            };
            cmp.KnownDiff = [version](const CadObject& a, const CadObject& b, std::string_view what) {
                auto starts = [&](std::string_view prefix) { return what.substr(0, prefix.size()) == prefix; };
                // 多边形裁剪边界写 DXF 时补上闭合点
                if (auto* wa = dynamic_cast<const CadWipeoutBase*>(&a); wa && starts("ClipBoundaryVertices"))
                {
                    const auto& va = wa->ClipBoundaryVertices;
                    const auto& vb = static_cast<const CadWipeoutBase&>(b).ClipBoundaryVertices;
                    return vb.size() == va.size() + 1 && va.size() > 2 && vb.back() == va.front()
                        && std::equal(va.begin(), va.end(), vb.begin());
                }
                // 标注样式的颜色组码只能存索引色：真彩色写成最接近的索引
                if (auto* da = dynamic_cast<const DimensionStyle*>(&a))
                {
                    auto* dbb = static_cast<const DimensionStyle*>(&b);
                    auto approx = [](const Color& x, const Color& y) { return x.IsTrueColor() && y == Color(x.ApproxIndex()); };
                    if (starts("DimensionLineColor"))
                        return approx(da->DimensionLineColor, dbb->DimensionLineColor);
                    if (starts("ExtensionLineColor"))
                        return approx(da->ExtensionLineColor, dbb->ExtensionLineColor);
                    if (starts("TextColor"))
                        return approx(da->TextColor, dbb->TextColor);
                }
                // R2013 之前的 DXF 不写线型分段标志的 8 位
                if (auto* lta = dynamic_cast<const LineType*>(&a); lta && version < CadVersion::AC1027 && starts("Segments"))
                {
                    const auto& sa = lta->Segments;
                    const auto& sb = static_cast<const LineType&>(b).Segments;
                    return sa.size() == sb.size()
                        && std::equal(sa.begin(), sa.end(), sb.begin(), [](const LineTypeSegment& x, const LineTypeSegment& y) {
                               return (static_cast<int>(x.Flags) & ~8) == static_cast<int>(y.Flags) && x.Length == y.Length
                                   && x.Text == y.Text;
                           });
                }
                return false;
            };
            cmp.Run();
            CHECK(cmp.Diffs.empty());
            for (const std::string& d : cmp.Diffs)
                std::printf("    %s%s 差异：%s\n", v, binary ? "（二进制）" : "", d.c_str());
            for (const auto& [cls, n] : cmp.Missing)
            {
                CHECK(cls == "Shape");
                if (cls != "Shape")
                    std::printf("    %s%s 丢失：%s × %d\n", v, binary ? "（二进制）" : "", cls.c_str(), n);
            }
        }
    }
}

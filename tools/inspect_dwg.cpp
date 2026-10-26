#include <chrono>
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
// Read-only DWG inventory for diagnosing unsupported entity display.
#include "Dwg/Read/DwgReader.h"
#include "Database/UnknownObjects.h"
#include "Document/Document.h"
#include "Core/GeomKernel/AABB.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/Face3DEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Document/TextStyleResolver.h"
#include "Core/Entity/SolidEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Draw/TransformDrawSink.hpp"
#include "Document/DrawContext.hpp"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include "Import/CadExchange.h"
#include "Import/TchWall.h"
#include "Import/TchText.h"
#include "Import/TchElec.h"
#undef far
#undef near
#include "Import/TchAnnotation.h"
#include "Text/Font/SHXFont.h"
#include "Text/Font/TTFFont.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

int CountSceneFaces(const MiniCAD::Scene& scene)
{
    int count = 0;
    scene.ForEachObject([&](const MiniCAD::Object& object) {
        count += dynamic_cast<const MiniCAD::Face3DEntity*>(&object) != nullptr;
    });
    scene.GetBlockTable().ForEach([&](MiniCAD::BlockID, const MiniCAD::BlockEntity& block) {
        for (const auto& entity : block.GetEntities())
            count += dynamic_cast<const MiniCAD::Face3DEntity*>(entity.get()) != nullptr;
    });
    return count;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) { std::fprintf(stderr, "Usage: MiniCADDwgInspect <dwg> [dump-directory]\n"); return 2; }
    // --advances 字体文件：打印 MiniCAD 排版用的字符步进（字高 1）
    if (argc >= 3 && std::wstring(argv[1]) == L"--advances")
    {
        const auto u8 = std::filesystem::path(argv[2]).u8string();
        const std::string file(u8.begin(), u8.end());
        if (file.ends_with(".ttf") || file.ends_with(".TTF"))
        {
            MiniCAD::TTFFont ttf("f", file, 1);
            std::printf("ttf height %g H %.5f 0 %.5f 图 %.5f\n", ttf.GetHeight(), ttf.GetAdvance('H'), ttf.GetAdvance('0'), ttf.GetAdvance(0x56fe));
            for (std::uint32_t c : {0x56feu, 0x8bf4u, 0x660eu, 0x5efau, 0x7b51u, 0x0041u, 0x0038u, 0x9f98u, 0x8b49u})
            {
                const auto g = ttf.GetGlyph(c);
                double outline = 0, filled = 0;
                for (const auto& l : g.Lines) outline += l.Start.x * l.End.y - l.End.x * l.Start.y;
                for (const auto& t : g.Triangles) filled += std::abs((t.b.x - t.a.x) * (t.c.y - t.a.y) - (t.c.x - t.a.x) * (t.b.y - t.a.y));
                {
                    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
                    for (const auto& l : g.Lines) { x0 = std::min({x0, l.Start.x, l.End.x}); x1 = std::max({x1, l.Start.x, l.End.x}); y0 = std::min({y0, l.Start.y, l.End.y}); y1 = std::max({y1, l.Start.y, l.End.y}); }
                    const int N = 1500; long inside = 0;
                    for (int iy = 0; iy < N && !g.Lines.empty(); ++iy)
                    {
                        const double py = y0 + (y1 - y0) * (iy + 0.5) / N;
                        std::vector<std::pair<double, int>> xs;
                        for (const auto& l : g.Lines)
                            if ((l.Start.y <= py) != (l.End.y <= py))
                                xs.push_back({ l.Start.x + (py - l.Start.y) / (l.End.y - l.Start.y) * (l.End.x - l.Start.x), l.End.y > l.Start.y ? 1 : -1 });
                        std::sort(xs.begin(), xs.end());
                        for (int ix = 0; ix < N; ++ix)
                        {
                            const double px = x0 + (x1 - x0) * (ix + 0.5) / N;
                            int w = 0; for (const auto& [x, d] : xs) { if (x < px) w += d; else break; }
                            if (w != 0) ++inside;
                        }
                    }
                    std::printf("sampled %.6f  ", g.Lines.empty() ? 0.0 : (x1 - x0) * (y1 - y0) * inside / double(N) / N);
                }
                std::printf("glyph U+%04X lines %zu triangles %zu outline %.6f filled %.6f\n", c, g.Lines.size(), g.Triangles.size(), outline * 0.5, filled * 0.5);
            }
            return 0;
        }
        MiniCAD::SHXFont font("f", file, 1);
        const double h = font.GetHeight();
        std::printf("height %g\n", h);
        for (std::uint32_t c = 32; c < 127; ++c)
            if (font.HasGlyph(c)) std::printf("%u %.5f\n", c, font.GetAdvance(c) / h);
        for (std::uint32_t c : {0x5929u, 0x6b63u, 0x56feu, 0x540du, 0x6807u, 0x6ce8u, 0x8f93u, 0x5165u, 0x4e0au, 0x4e0bu})
            if (font.HasGlyph(c)) std::printf("%u %.5f\n", c, font.GetAdvance(c) / h);
        return 0;
    }
    const std::filesystem::path path(argv[1]);
    std::ifstream file(path, std::ios::binary);
    if (!file) return 2;
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    std::printf("File bytes: %zu; header: %.6s\n", bytes.size(), reinterpret_cast<const char*>(bytes.data()));
    MiniDWG::DwgReadReport report;
    MiniDWG::DwgReadOptions options;
    options.Report = &report;
    options.Notify = [](auto level, std::string_view message) { std::printf("READER[%d] %.*s\n", int(level), int(message.size()), message.data()); };
    auto tRead0 = std::chrono::steady_clock::now();
    const auto db = MiniDWG::ReadDwg(bytes, options);
    std::printf("TIME ReadDwg %.1f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tRead0).count());
    if (!db) { std::printf("DWG parse failed\n"); return 1; }
    std::printf("Classes: %zu; objects: %zu; model entities: %zu\n", db->Classes.size(), db->Objects().size(), db->ModelSpace() ? db->ModelSpace()->Entities.size() : 0);
    // MINICAD_TABLES=1：打印 DWG 的 STYLE / LTYPE 表、布局与图纸空间块记录（导入后再打印场景里的两张表）
    if (std::getenv("MINICAD_TABLES"))
    {
        auto xdata = [&](const MiniDWG::CadObject& o)
        {
            for (const auto& x : o.ExtendedDataList)
            {
                const auto* app = db->FindAs<MiniDWG::AppId>(x.AppIdHandle);
                std::printf(" xdata[%s]", app ? app->Name.c_str() : "?");
                for (const auto& rec : x.Records)
                    std::visit([&](const auto& v) {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, std::string>) std::printf(" %d:%s", rec.Code, v.c_str());
                        else if constexpr (std::is_arithmetic_v<T>) std::printf(" %d:%g", rec.Code, static_cast<double>(v));
                        else std::printf(" %d:?", rec.Code);
                    }, rec.Value);
            }
        };
        for (MiniDWG::Handle h : db->TextStyles()->Entries)
            if (const auto* ts = db->FindAs<MiniDWG::TextStyle>(h))
            {
                std::printf("DWGSTYLE %llx %s font=[%s] big=[%s] h=%g w=%g ob=%g flags=%d tt=%d", static_cast<unsigned long long>(h), ts->Name.c_str(),
                            ts->Filename.c_str(), ts->BigFontFilename.c_str(), ts->Height, ts->Width, ts->ObliqueAngle, int(ts->Flags), int(ts->TrueType));
                xdata(*ts);
                std::printf("\n");
            }
        for (MiniDWG::Handle h : db->LineTypes()->Entries)
            if (const auto* lt = db->FindAs<MiniDWG::LineType>(h))
            {
                std::printf("DWGLTYPE %llx %s desc=[%s] segs=%zu:", static_cast<unsigned long long>(h), lt->Name.c_str(), lt->Description.c_str(), lt->Segments.size());
                for (const auto& seg : lt->Segments)
                {
                    std::printf(" %g", seg.Length);
                    if (seg.Flags != MiniDWG::LineTypeShapeFlags::None)
                        std::printf("(%s s=%g x=%g y=%g)", seg.Text.empty() ? "shape" : seg.Text.c_str(), seg.Scale, seg.Offset.X, seg.Offset.Y);
                }
                std::printf("\n");
            }
        for (MiniDWG::Handle h : db->BlockRecords()->Entries)
            if (const auto* br = db->FindAs<MiniDWG::BlockRecord>(h))
            {
                const auto* blk = db->FindAs<MiniDWG::Block>(br->BlockEntityHandle);
                const int flags = blk ? int(blk->Flags) : -1;
                if ((br->Name.size() > 1 && br->Name[0] == '*') || (flags & 4) || std::getenv("MINICAD_ALLBLOCKS"))
                    std::printf("DWGBLOCKREC %llx [%s] entities=%zu flags=%d xref=[%s] unloaded=%d\n", static_cast<unsigned long long>(h), br->Name.c_str(), br->Entities.size(),
                                flags, blk ? blk->XRefPath.c_str() : "", blk ? int(blk->IsUnloaded) : 0);
            }
        for (MiniDWG::Handle h : db->Layers()->Entries)
            if (const auto* l = db->FindAs<MiniDWG::Layer>(h))
                if (l->Name.find('|') != std::string::npos || std::getenv("MINICAD_ALLBLOCKS"))
                {
                    const auto* lt = db->FindAs<MiniDWG::LineType>(l->LineTypeHandle);
                    std::printf("DWGLAYER %llx [%s] flags=%d color=%d ltype=[%s]\n", static_cast<unsigned long long>(h), l->Name.c_str(), int(l->Flags), int(l->Color.Index()), lt ? lt->Name.c_str() : "?");
                }
        std::printf("DWGHEADER ltscale=%g psltscale=%d celtscale=%g\n", db->Header.LineTypeScale, int(db->Header.PaperSpaceLineTypeScaling), db->Header.CurrentEntityLinetypeScale);
        // MINICAD_ALLBLOCKS=1 时再列出模型空间实体（INSERT 带所引用块的句柄与名称）
        if (std::getenv("MINICAD_ALLBLOCKS") && db->ModelSpace())
            for (MiniDWG::Handle h : db->ModelSpace()->Entities)
            {
                const auto* e = db->FindAs<MiniDWG::Entity>(h);
                if (e == nullptr) { std::printf("DWGMODEL %llx (missing)\n", static_cast<unsigned long long>(h)); continue; }
                std::printf("DWGMODEL %llx %s", static_cast<unsigned long long>(h), std::string(e->GetDxfName()).c_str());
                if (const auto* ins = dynamic_cast<const MiniDWG::Insert*>(e))
                {
                    const auto* br = db->FindAs<MiniDWG::BlockRecord>(ins->BlockHandle);
                    std::printf(" block=%llx [%s] at (%g,%g)", static_cast<unsigned long long>(ins->BlockHandle), br ? br->Name.c_str() : "(missing)", ins->InsertPoint.X, ins->InsertPoint.Y);
                }
                std::printf("\n");
            }
    }
    // MINICAD_VIEWPORTS=1：打印全部 VIEWPORT 的视图参数（图纸空间视口显示不对时用）
    if (std::getenv("MINICAD_VIEWPORTS"))
    {
        for (const auto& [h, obj] : db->Objects())
        {
            const auto* vp = dynamic_cast<const MiniDWG::Viewport*>(obj.get());
            if (vp == nullptr) continue;
            const auto* layer = db->FindAs<MiniDWG::Layer>(vp->LayerHandle);
            std::printf("VIEWPORT %llx owner=%llx layer=[%s] inv=%d id=%d status=0x%x\n", static_cast<unsigned long long>(h),
                        static_cast<unsigned long long>(vp->OwnerHandle), layer ? layer->Name.c_str() : "?", int(vp->IsInvisible),
                        int(vp->ActiveStatus), unsigned(vp->Status));
            std::printf("  center=(%g,%g,%g) w=%g h=%g\n", vp->Center.X, vp->Center.Y, vp->Center.Z, vp->Width, vp->Height);
            std::printf("  viewCenter=(%g,%g) viewHeight=%g twist=%g lens=%g\n", vp->ViewCenter.X, vp->ViewCenter.Y, vp->ViewHeight, vp->TwistAngle, vp->LensLength);
            std::printf("  viewTarget=(%g,%g,%g) viewDir=(%g,%g,%g)\n", vp->ViewTarget.X, vp->ViewTarget.Y, vp->ViewTarget.Z,
                        vp->ViewDirection.X, vp->ViewDirection.Y, vp->ViewDirection.Z);
            std::printf("  ucsOrigin=(%g,%g,%g) ucsX=(%g,%g,%g) ucsY=(%g,%g,%g) elev=%g clip=%g/%g\n",
                        vp->UcsOrigin.X, vp->UcsOrigin.Y, vp->UcsOrigin.Z, vp->UcsXAxis.X, vp->UcsXAxis.Y, vp->UcsXAxis.Z,
                        vp->UcsYAxis.X, vp->UcsYAxis.Y, vp->UcsYAxis.Z, vp->Elevation, vp->FrontClipPlane, vp->BackClipPlane);
            std::printf("  frozenLayers=%zu boundary=%llx\n", vp->FrozenLayers.size(), static_cast<unsigned long long>(vp->BoundaryHandle));
            for (MiniDWG::Handle fl : vp->FrozenLayers)
                if (const auto* l = db->FindAs<MiniDWG::Layer>(fl)) std::printf("    frozen [%s]\n", l->Name.c_str());
        }
    }
    // MINICAD_OLEDIR=目录：把每个 OLE2FRAME 的原始数据流写成 <句柄>.ole
    if (const wchar_t* od = _wgetenv(L"MINICAD_OLEDIR"))
    {
        const std::filesystem::path dir(od);
        std::filesystem::create_directories(dir);
        for (const auto& [handle, object] : db->Objects())
            if (const auto* u = dynamic_cast<const MiniDWG::UnknownEntity*>(object.get()); u && u->Raw.DxfName == "OLE2FRAME")
            {
                char name[32];
                std::snprintf(name, sizeof(name), "%llx.ole", static_cast<unsigned long long>(handle));
                std::ofstream out(dir / name, std::ios::binary);
                out.write(reinterpret_cast<const char*>(u->Raw.Dwg.Main.data()), static_cast<std::streamsize>(u->Raw.Dwg.Main.size()));
                std::printf("OLE %llx owner=%llx layer=%llx bytes=%zu bits=%llu\n", static_cast<unsigned long long>(handle), static_cast<unsigned long long>(u->OwnerHandle),
                            static_cast<unsigned long long>(u->LayerHandle), u->Raw.Dwg.Main.size(), static_cast<unsigned long long>(u->Raw.Dwg.MainBits));
            }
    }
    // MINICAD_H=句柄,句柄...（十六进制）：打印这些对象的类型、所有者、是否在模型空间实体表里、不可见标志
    if (const char* hs = std::getenv("MINICAD_H"))
    {
        std::string list(hs);
        for (std::size_t pos = 0; pos < list.size();)
        {
            std::size_t end = list.find(',', pos);
            if (end == std::string::npos) end = list.size();
            const MiniDWG::Handle h = std::stoull(list.substr(pos, end - pos), nullptr, 16);
            pos = end + 1;
            const auto* o = db->Find(h);
            const auto* e = dynamic_cast<const MiniDWG::Entity*>(o);
            bool inModel = false;
            if (db->ModelSpace())
                for (MiniDWG::Handle m : db->ModelSpace()->Entities) inModel |= m == h;
            std::printf("H %llx type=%s owner=%llx inModel=%d invisible=%d\n", static_cast<unsigned long long>(h),
                        o ? std::string(o->GetDxfName()).c_str() : "(missing)", o ? static_cast<unsigned long long>(o->OwnerHandle) : 0ull, inModel,
                        e ? int(e->IsInvisible) : -1);
            if (e)
            {
                const auto* lt = db->FindAs<MiniDWG::LineType>(e->LineTypeHandle);
                const auto* layer = db->FindAs<MiniDWG::Layer>(e->LayerHandle);
                const auto* llt = layer ? db->FindAs<MiniDWG::LineType>(layer->LineTypeHandle) : nullptr;
                std::printf("  ltype=%llx [%s] ltscale=%g color=%d layer=[%s] layerLtype=[%s]", static_cast<unsigned long long>(e->LineTypeHandle), lt ? lt->Name.c_str() : "?",
                            e->LineTypeScale, int(e->Color.Index()), layer ? layer->Name.c_str() : "?", llt ? llt->Name.c_str() : "?");
                if (llt)
                {
                    std::printf(" pattern:");
                    for (const auto& seg : llt->Segments) std::printf(" %g", seg.Length);
                }
                std::printf("\n");
            }
        }
    }
    // MINICAD_XCLIP：列出带 XCLIP 裁剪（ACAD_FILTER/SPATIAL）的块参照，解码裁剪边界
    if (std::getenv("MINICAD_XCLIP"))
    {
        for (const auto& [handle, object] : db->Objects())
        {
            const auto* ins = dynamic_cast<const MiniDWG::Insert*>(object.get());
            if (!ins || ins->XDictionaryHandle == MiniDWG::kNullHandle) continue;
            auto entry = [&](MiniDWG::Handle dh, const char* name) -> MiniDWG::Handle {
                const auto* d = dynamic_cast<const MiniDWG::CadDictionary*>(db->Find(dh));
                if (!d) return MiniDWG::kNullHandle;
                for (std::size_t i = 0; i < d->EntryNames.size() && i < d->EntryHandles.size(); ++i)
                    if (d->EntryNames[i] == name) return d->EntryHandles[i];
                return MiniDWG::kNullHandle;
            };
            const auto fh = entry(entry(ins->XDictionaryHandle, "ACAD_FILTER"), "SPATIAL");
            const auto* sf = dynamic_cast<const MiniDWG::UnknownObject*>(db->Find(fh));
            if (!sf) continue;
            const auto* br = dynamic_cast<const MiniDWG::TableEntry*>(db->Find(ins->BlockHandle));
            const auto* owner = dynamic_cast<const MiniDWG::TableEntry*>(db->Find(ins->OwnerHandle));
            std::printf("XCLIP insert %llx block=%s owner=%llx(%s) at (%.3f,%.3f) scale (%g,%g) rot %g filter=%s\n", (unsigned long long)handle,
                        br ? br->Name.c_str() : "?", (unsigned long long)ins->OwnerHandle, owner ? owner->Name.c_str() : "-", ins->InsertPoint.X, ins->InsertPoint.Y,
                        ins->XScale, ins->YScale, ins->Rotation, sf->Raw.DxfName.c_str());
            MiniDWG::DwgBitReader r(sf->Raw.Dwg.Main, sf->Raw.Dwg.Version, MiniDWG::Codec::CodePage::Gbk);
            const int n = r.ReadBitShort();
            std::printf("  points %d:", n);
            for (int i = 0; i < n && i < 64; ++i) { const auto p = r.Read2RawDouble(); std::printf(" (%.3f,%.3f)", p.X, p.Y); }
            const auto ex = r.Read3BitDouble(); const auto org = r.Read3BitDouble();
            const int disp = r.ReadBitShort();
            const int front = r.ReadBitShort(); const double fd = front ? r.ReadBitDouble() : 0;
            const int back = r.ReadBitShort(); const double bd = back ? r.ReadBitDouble() : 0;
            std::printf("\n  extrusion (%g,%g,%g) origin (%g,%g,%g) display %d front %d %g back %d %g\n", ex.X, ex.Y, ex.Z, org.X, org.Y, org.Z, disp, front, fd, back, bd);
            for (int m = 0; m < 2; ++m)
            {
                std::printf("  %s", m ? "clipxf" : "invins");
                for (int i = 0; i < 12; ++i) std::printf(" %.4f", r.ReadBitDouble());
                std::printf("\n");
            }
            std::printf("  remaining bits %lld\n", (long long)sf->Raw.Dwg.MainBits - (long long)r.PositionInBits());
            if (const auto* blk = dynamic_cast<const MiniDWG::BlockRecord*>(db->Find(ins->BlockHandle)))
            {
                double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
                for (MiniDWG::Handle eh : blk->Entities)
                    if (const auto* pl = dynamic_cast<const MiniDWG::LwPolyline*>(db->Find(eh)))
                        for (const auto& v : pl->Vertices) { x0 = std::min(x0, v.Location.X); x1 = std::max(x1, v.Location.X); y0 = std::min(y0, v.Location.Y); y1 = std::max(y1, v.Location.Y); }
                std::printf("  block lwpolyline extent (%.3f,%.3f)-(%.3f,%.3f) base (%.3f,%.3f)\n", x0, y0, x1, y1,
                            0.0, 0.0);
            }
        }
        return 0;
    }
    // MINICAD_FINDTEXT=内容：打印内容相同的 TEXT 的句柄、所有者、是否在模型空间实体表里
    // MINICAD_RAWOBJ=DXF 名：导出该类型非图形未知对象（如 TCH_DBCONFIG）的主数据流与文字流（十六进制）
    if (const char* ro = std::getenv("MINICAD_RAWOBJ"))
        for (const auto& [handle, object] : db->Objects())
            if (const auto* u = dynamic_cast<const MiniDWG::UnknownObject*>(object.get()); u && u->Raw.DxfName == ro)
            {
                std::printf("RAWOBJ %llx %s main_bits=%llu text_bits=%llu\n  main ", static_cast<unsigned long long>(handle), ro,
                            static_cast<unsigned long long>(u->Raw.Dwg.MainBits), static_cast<unsigned long long>(u->Raw.Dwg.TextBits));
                for (auto b : u->Raw.Dwg.Main) std::printf("%02x", b);
                std::printf("\n  text ");
                for (auto b : u->Raw.Dwg.Text) std::printf("%02x", b);
                std::printf("\n");
            }
    if (const wchar_t* fw = _wgetenv(L"MINICAD_FINDTEXT"))
        for (const auto& [handle, object] : db->Objects())
            if (const auto* t = dynamic_cast<const MiniDWG::TextEntity*>(object.get()); t && t->Value == [&] { const auto u = std::filesystem::path(fw).u8string(); return std::string(u.begin(), u.end()); }())
            {
                bool inModel = false;
                if (db->ModelSpace())
                    for (MiniDWG::Handle m : db->ModelSpace()->Entities) inModel |= m == handle;
                std::printf("FT %llx owner=%llx inModel=%d ins=%.1f,%.1f,%.1f align=%.1f,%.1f h=%g w=%g rot=%g h72=%d v73=%d normal=%g,%g,%g invisible=%d layer=%llx\n",
                            static_cast<unsigned long long>(handle), static_cast<unsigned long long>(t->OwnerHandle), inModel,
                            t->InsertPoint.X, t->InsertPoint.Y, t->InsertPoint.Z, t->AlignmentPoint.X, t->AlignmentPoint.Y, t->Height, t->WidthFactor,
                            t->Rotation, int(t->HorizontalAlignment), int(t->VerticalAlignment), t->Normal.X, t->Normal.Y, t->Normal.Z,
                            int(t->IsInvisible), static_cast<unsigned long long>(t->LayerHandle));
            }
    // MINICAD_LAYERS=子串：打印名称含该子串的图层的标志（1 冻结、4 锁定）、开关与颜色
    if (const wchar_t* lw = _wgetenv(L"MINICAD_LAYERS"))
    {
        const auto u = std::filesystem::path(lw).u8string();
        const std::string sub(u.begin(), u.end());
        for (const auto& [handle, object] : db->Objects())
            if (const auto* l = dynamic_cast<const MiniDWG::Layer*>(object.get()); l && l->Name.find(sub) != std::string::npos)
                std::printf("LAYER %s flags=%d on=%d color=%d\n", l->Name.c_str(), int(l->Flags), int(l->IsOn), int(l->Color.Index()));
    }
    // MINICAD_WIPEOUT：打印 WIPEOUT 的所有者块、图层与边界
    if (std::getenv("MINICAD_WIPEOUT"))
        for (const auto& [handle, object] : db->Objects())
            if (const auto* w = dynamic_cast<const MiniDWG::Wipeout*>(object.get()))
            {
                const auto* owner = db->FindAs<MiniDWG::BlockRecord>(w->OwnerHandle);
                const auto* layer = db->FindAs<MiniDWG::Layer>(w->LayerHandle);
                std::printf("WIPEOUT %llx owner=%s layer=%s clip=%d verts=%zu\n", static_cast<unsigned long long>(handle),
                            owner ? owner->Name.c_str() : "?", layer ? layer->Name.c_str() : "?", int(w->ClippingState), w->ClipBoundaryVertices.size());
            }
    // MINICAD_WALLRAW：打印天正墙受保护几何前的标记、密钥字节与 84 字节原始数据（十六进制）
    if (std::getenv("MINICAD_WALLRAW"))
        for (const auto& [handle, object] : db->Objects())
            if (const auto* u = dynamic_cast<const MiniDWG::UnknownEntity*>(object.get()); u && u->Raw.DxfName == "TCH_WALL")
            {
                MiniDWG::DwgBitReader r(u->Raw.Dwg.Main, u->Raw.Dwg.Version, MiniDWG::Codec::CodePage::Gbk);
                r.ReadByte(); r.ReadBitDouble(); r.ReadBitShort(); r.ReadByte(); r.Read2Bits(); r.ReadRawShort();
                const int mark = r.ReadByte(), key = r.ReadByte();
                const auto bytes = r.ReadBytes(84);
                std::printf("WALLRAW %llx mark=%d key=%02x ", (unsigned long long)handle, mark, key);
                for (auto b : bytes) std::printf("%02x", b);
                std::printf("\n");
            }
    // MINICAD_MLEADER：打印天正引出标注的解码结果与文字样式（字体、宽度、_TCH 扩展数据）
    if (std::getenv("MINICAD_MLEADER"))
        for (const auto& [handle, object] : db->Objects())
            if (const auto* u = dynamic_cast<const MiniDWG::UnknownEntity*>(object.get()); u && u->Raw.DxfName == "TCH_MULTILEADER")
            {
                MiniCAD::Tch::TchMultiLeader m;
                std::string why;
                const bool ok = MiniCAD::Tch::DecodeMultiLeader(*u, m, &why);
                const auto* st = db->FindAs<MiniDWG::TextStyle>(m.Style);
                std::printf("ML %llx owner=%llx ok=%d %s scale=%g th=%g arrow=%d elbow=(%.3f,%.3f) len=%g side=%d up=[%s] lo=[%s] style=%s font=%s big=%s w=%g",
                            (unsigned long long)handle, (unsigned long long)u->OwnerHandle, ok, why.c_str(), m.Scale, m.TextHeight, m.Arrow, m.ElbowX, m.ElbowY,
                            m.LineLength, m.Side, m.Upper.c_str(), m.Lower.c_str(), st ? st->Name.c_str() : "?", st ? st->Filename.c_str() : "?",
                            st ? st->BigFontFilename.c_str() : "?", st ? st->Width : 0.0);
                if (st)
                    for (const auto& x : st->ExtendedDataList)
                        for (const auto& rec : x.Records)
                            if (const auto* v = std::get_if<double>(&rec.Value)) std::printf(" x%d:%g", rec.Code, *v);
                std::printf(" v3=(%g,%d,)\n", m.Gap, m.Align);
            }
    // MINICAD_EMAP：打印天正桥架 / 桥架配件的属性表（句柄、所有者、键(类型)=值）
    if (std::getenv("MINICAD_EMAP"))
        for (const auto& [handle, object] : db->Objects())
            if (const auto* u = dynamic_cast<const MiniDWG::UnknownEntity*>(object.get()); u && (u->Raw.DxfName == "TCH_EFITTING" || u->Raw.DxfName == "TCH_CABLETRY" || u->Raw.DxfName == "TCH_DBHVACDUCT" || u->Raw.DxfName == "TCH_DBHFITTING"))
            {
                MiniCAD::Tch::TchXDataMap map;
                std::string why;
                MiniCAD::Tch::TchBase base;
                const bool ok = (u->Raw.DxfName == "TCH_EFITTING" || u->Raw.DxfName == "TCH_DBHFITTING") ? MiniCAD::Tch::ReadCableFittingMap(*u, map, &why, &base) : MiniCAD::Tch::ReadCableTrayMap(*u, map, &why, &base);
                const auto* layer = db->FindAs<MiniDWG::Layer>(u->LayerHandle);
                std::printf("EMAP %s %llx owner=%llx ok=%d %s scale=%g layer=[%s]", u->Raw.DxfName.c_str(), (unsigned long long)handle, (unsigned long long)u->OwnerHandle, ok, why.c_str(),
                            base.Scale, layer ? layer->Name.c_str() : "?");
                for (const auto& [k, v] : map.Items)
                {
                    std::printf(" %s=", k.c_str());
                    if (v.Type == 3 || v.Type == 5) std::printf("%.10g", v.Number);
                    else if (v.Type >= 0x4005 && v.Type <= 0x4008) std::printf("(%.10g,%.10g,%.10g)", v.Point.X, v.Point.Y, v.Point.Z);
                    else if (v.HandleIndex >= 0) std::printf("#%llx", (unsigned long long)MiniCAD::Tch::Detail::HandleAt(*u, v.HandleIndex));
                    else std::printf("[%s]", v.Text.c_str());
                }
                std::printf("\n");
            }
    // MINICAD_PROXYEXT=类型：打印该类未建模实体的所有者、代理图形的记录类型与范围、私有数据大小
    if (const char* pt = std::getenv("MINICAD_PROXYEXT"))
        for (const auto& [handle, object] : db->Objects())
            if (const auto* u = dynamic_cast<const MiniDWG::UnknownEntity*>(object.get()); u && u->Raw.DxfName == pt)
            {
                const auto* owner = db->FindAs<MiniDWG::BlockRecord>(u->OwnerHandle);
                std::printf("PX %llx owner=%s proxy=%zu main_bits=%llu refs=%zu records:", (unsigned long long)handle, owner ? owner->Name.c_str() : "?",
                            u->ProxyGraphics.size(), (unsigned long long)u->Raw.Dwg.MainBits, u->Raw.Dwg.References.size());
                const auto& b = u->ProxyGraphics;
                auto rd32 = [&](std::size_t o) { std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= std::uint32_t(b[o + i]) << (8 * i); return v; };
                auto rdd = [&](std::size_t o) { double d; std::memcpy(&d, &b[o], 8); return d; };
                for (std::size_t o = 8; o + 8 <= b.size();)
                {
                    const auto size = rd32(o), type = rd32(o + 4);
                    if (size < 8 || o + size > b.size()) break;
                    std::printf(" %u", type);
                    if (type == 1 && size == 56)
                        std::printf("(%.0f,%.0f)-(%.0f,%.0f)", rdd(o + 8), rdd(o + 16), rdd(o + 32), rdd(o + 40));
                    o += size;
                }
                if (MiniCAD::Tch::TchText t; u->Raw.DxfName == "TCH_MTEXT" || u->Raw.DxfName == "TCH_TEXT")
                {
                    std::string why;
                    const bool ok = MiniCAD::Tch::DecodeText(*u, t, &why);
                    std::printf(" MTEXT ok=%d %s at=(%.1f,%.1f) h=%g scale=%g rot=%g att=%d box=%g spacing=%g value=[%s]", ok, why.c_str(), t.X, t.Y, t.Height,
                                t.Scale, t.Rotation, t.Attachment, t.BoxWidth, t.LineSpacing, t.Value.c_str());
                    std::printf(" hex=");
                    for (unsigned char ch : t.Value) std::printf("%02x", ch);
                }
                if (MiniCAD::Tch::TchWall w; MiniCAD::Tch::DecodeWallPrefix(*u, w))
                    std::printf(" WALL (%.1f,%.1f)-(%.1f,%.1f) angle=%g L=%g R=%g flags=%x", w.Start.X, w.Start.Y, w.End.X, w.End.Y, w.Angle, w.LeftWidth, w.RightWidth, w.Flags);
                std::printf("\n");
            }
    // MINICAD_BULGE：统计多段线弧段与前一直线段相切时符合哪种凸度约定
    if (std::getenv("MINICAD_BULGE"))
    {
        int acad = 0, flipped = 0;
        for (const auto& [handle, object] : db->Objects())
            if (const auto* pl = dynamic_cast<const MiniDWG::LwPolyline*>(object.get()))
                for (size_t i = 1; i + 1 < pl->Vertices.size(); ++i)
                {
                    const auto& p0 = pl->Vertices[i - 1]; const auto& p1 = pl->Vertices[i]; const auto& p2 = pl->Vertices[i + 1];
                    if (std::abs(p0.Bulge) > 1e-9 || std::abs(p1.Bulge) < 1e-6) continue;
                    double lx = p1.Location.X - p0.Location.X, ly = p1.Location.Y - p0.Location.Y, ll = std::hypot(lx, ly);
                    double cx = p2.Location.X - p1.Location.X, cy = p2.Location.Y - p1.Location.Y, cl = std::hypot(cx, cy);
                    if (ll < 1e-9 || cl < 1e-9) continue;
                    lx /= ll; ly /= ll; cx /= cl; cy /= cl;
                    const double h = 2.0 * std::atan(p1.Bulge);
                    auto rot = [&](double a) { return lx * (cx * std::cos(a) - cy * std::sin(a)) + ly * (cx * std::sin(a) + cy * std::cos(a)); };
                    if (rot(-h) > 0.9999) ++acad; else if (rot(h) > 0.9999) ++flipped;
                }
        std::printf("BULGE tangent: autocad=%d minicad=%d\n", acad, flipped);
    }
    // MINICAD_HATCH：按 所有者块 / 图层 / 图案 统计 HATCH
    if (std::getenv("MINICAD_HATCH"))
    {
        std::map<std::string, int> hs;
        for (const auto& [handle, object] : db->Objects())
            if (const auto* h = dynamic_cast<const MiniDWG::Hatch*>(object.get()))
            {
                const auto* owner = db->FindAs<MiniDWG::BlockRecord>(h->OwnerHandle);
                const auto* layer = db->FindAs<MiniDWG::Layer>(h->LayerHandle);
                ++hs[std::string(owner ? owner->Name : "?") + " | " + (layer ? layer->Name : "?") + " | " + (h->IsSolid ? "SOLID" : h->Pattern.Name)];
            }
        for (const auto& [k, n] : hs) std::printf("HATCH %d %s\n", n, k.c_str());
        // 每种图案打印一个样本的线族与边界
        std::map<std::string, int> seen;
        for (const auto& [handle, object] : db->Objects())
            if (const auto* h = dynamic_cast<const MiniDWG::Hatch*>(object.get()))
            {
                const std::string key = h->IsSolid ? "SOLID" : h->Pattern.Name;
                if (seen[key]++ >= 1) continue;
                std::printf("SAMPLE %llx %s type=%d angle=%g scale=%g double=%d style=%d paths=%zu\n", (unsigned long long)handle, key.c_str(),
                            int(h->PatternType), h->PatternAngle, h->PatternScale, int(h->IsDouble), int(h->Style), h->Paths.size());
                {
                    const auto* layer = db->FindAs<MiniDWG::Layer>(h->LayerHandle);
                    std::printf("  COLOR ent idx=%d true=%d byblock=%d | layer %s idx=%d true=%d\n", int(h->Color.Index()), int(h->Color.IsTrueColor()), int(h->Color.IsByBlock()),
                                layer ? layer->Name.c_str() : "?", layer ? int(layer->Color.Index()) : -999, layer ? int(layer->Color.IsTrueColor()) : 0);
                }
                for (const auto& l : h->Pattern.Lines)
                {
                    std::printf("  LINE a=%g base=(%g,%g) off=(%g,%g) dash=", l.Angle, l.BasePoint.X, l.BasePoint.Y, l.Offset.X, l.Offset.Y);
                    for (double d : l.DashLengths) std::printf("%g ", d);
                    std::printf("\n");
                }
                for (const auto& p : h->Paths)
                {
                    std::printf("  PATH flags=%d edges=%zu:", int(p.Flags), p.Edges.size());
                    for (const auto& e : p.Edges) std::printf(" %d", int(e->GetType()));
                    std::printf("\n");
                    for (const auto& e : p.Edges)
                        if (const auto* l = dynamic_cast<const MiniDWG::HatchBoundaryPathLine*>(e.get()))
                            std::printf("    L (%g,%g)-(%g,%g)\n", l->Start.X, l->Start.Y, l->End.X, l->End.Y);
                        else if (const auto* a = dynamic_cast<const MiniDWG::HatchBoundaryPathArc*>(e.get()))
                            std::printf("    A c=(%g,%g) r=%g %g..%g ccw=%d\n", a->Center.X, a->Center.Y, a->Radius, a->StartAngle, a->EndAngle, int(a->CounterClockWise));
                        else if (const auto* el = dynamic_cast<const MiniDWG::HatchBoundaryPathEllipse*>(e.get()))
                            std::printf("    E c=(%g,%g) m=(%g,%g) r=%g %g..%g ccw=%d\n", el->Center.X, el->Center.Y, el->MajorAxisEndPoint.X, el->MajorAxisEndPoint.Y, el->RadiusRatio, el->StartAngle, el->EndAngle, int(el->CounterClockWise));
                }
            }
        for (const auto& [handle, object] : db->Objects())
            if (const auto* s = dynamic_cast<const MiniDWG::SortEntitiesTable*>(object.get()))
            {
                const auto* owner = db->FindAs<MiniDWG::BlockRecord>(s->BlockOwnerHandle);
                std::printf("SORTENTS owner=%s entries=%zu\n", owner ? owner->Name.c_str() : "?", s->Entries.size());
            }
    }
    if (std::getenv("MINICAD_WIPEOUT"))
        for (const auto& [handle, object] : db->Objects())
            if (const auto* w = dynamic_cast<const MiniDWG::Wipeout*>(object.get()))
            {
                std::printf("  WIPE ins=(%g,%g) u=(%g,%g) v=(%g,%g) size=(%g,%g) flags=%d verts:", w->InsertPoint.X, w->InsertPoint.Y, w->UVector.X, w->UVector.Y,
                            w->VVector.X, w->VVector.Y, w->Size.X, w->Size.Y, int(w->Flags));
                for (const auto& v : w->ClipBoundaryVertices) std::printf(" (%g,%g)", v.X, v.Y);
                std::printf("\n");
            }
    for (const auto& cls : db->Classes)
        std::printf("CLASS %d %s cpp=%s app=%s entity=%d flags=%d\n", cls.ClassNumber, cls.DxfName.c_str(), cls.CppClassName.c_str(), cls.ApplicationName.c_str(), cls.IsAnEntity, cls.ProxyFlags);
    std::map<std::string, int> counts;
    for (const auto& [handle, object] : db->Objects())
    {
        ++counts[std::string(object->GetDxfName())];
        const auto* entity = dynamic_cast<const MiniDWG::UnknownEntity*>(object.get());
        if (!entity) continue;
        std::printf("UNKNOWN %llx %s owner=%llx layer=%llx proxy=%zu raw_main_bits=%llu refs=%zu\n",
                    static_cast<unsigned long long>(handle), entity->Raw.DxfName.c_str(),
                    static_cast<unsigned long long>(entity->OwnerHandle), static_cast<unsigned long long>(entity->LayerHandle),
                    entity->ProxyGraphics.size(), static_cast<unsigned long long>(entity->Raw.Dwg.MainBits), entity->Raw.Dwg.References.size());
        for (MiniDWG::Handle ref : entity->Raw.Dwg.References)
        {
            const auto* target = db->Find(ref);
            const auto* record = dynamic_cast<const MiniDWG::BlockRecord*>(target);
            std::printf("  REF %llx %s%s%s\n", static_cast<unsigned long long>(ref),
                        target ? std::string(target->GetDxfName()).c_str() : "(missing)",
                        record ? " " : "", record ? record->Name.c_str() : "");
            if (const auto* ds = dynamic_cast<const MiniDWG::DimensionStyle*>(target))
            {
                const auto* ts = db->FindAs<MiniDWG::TextStyle>(ds->StyleHandle);
                std::printf("    DIMSTYLE %s scale=%g txt=%g asz=%g tsz=%g exo=%g exe=%g gap=%g tvp=%g dec=%d adec=%d lfac=%g txsty=%s\n",
                            ds->Name.c_str(), ds->ScaleFactor, ds->TextHeight, ds->ArrowSize, ds->TickSize,
                            ds->ExtensionLineOffset, ds->ExtensionLineExtension, ds->DimensionLineGap, ds->TextVerticalPosition,
                            ds->DecimalPlaces, ds->AngularDecimalPlaces, ds->LinearScaleFactor, ts ? ts->Name.c_str() : "?");
            }
            if (const auto* ts = dynamic_cast<const MiniDWG::TextStyle*>(target))
            {
                std::printf("    STYLE %s font=%s big=%s h=%g w=%g", ts->Name.c_str(), ts->Filename.c_str(), ts->BigFontFilename.c_str(), ts->Height, ts->Width);
                for (const auto& x : ts->ExtendedDataList)     // 扩展数据（天正样式的附加设置）
                {
                    const auto* app = db->FindAs<MiniDWG::AppId>(x.AppIdHandle);
                    std::printf(" xdata[%s]", app ? app->Name.c_str() : "?");
                    for (const auto& rec : x.Records)
                        std::visit([&](const auto& v) {
                            using T = std::decay_t<decltype(v)>;
                            if constexpr (std::is_same_v<T, std::string>) std::printf(" %d:%s", rec.Code, v.c_str());
                            else if constexpr (std::is_arithmetic_v<T>) std::printf(" %d:%g", rec.Code, static_cast<double>(v));
                            else std::printf(" %d:?", rec.Code);
                        }, rec.Value);
                }
                std::printf("\n");
            }
            if (const auto* layer = dynamic_cast<const MiniDWG::Layer*>(target))
                std::printf("    LAYER %s\n", layer->Name.c_str());
        }
        if (argc > 2)
        {
            const std::filesystem::path dir(argv[2]);
            std::filesystem::create_directories(dir);
            auto dump = [&](const char* suffix, const std::vector<std::uint8_t>& data)
            {
                const auto name = std::to_string(handle) + suffix;
                std::ofstream out(dir / name, std::ios::binary);
                out.write(reinterpret_cast<const char*>(data.data()), data.size());
            };
            dump("-proxy.bin", entity->ProxyGraphics);
            dump("-raw.bin", entity->Raw.Dwg.Main);
            dump("-text.bin", entity->Raw.Dwg.Text);
            dump("-handles.bin", entity->Raw.Dwg.Handles);
            std::ofstream(dir / (std::to_string(handle) + "-bits.txt"))
                << entity->Raw.Dwg.MainBits << ' ' << entity->Raw.Dwg.TextBits << ' ' << entity->Raw.Dwg.HandleBits << '\n';
        }
    }
    for (const auto& [type, count] : counts) std::printf("COUNT %s %d\n", type.c_str(), count);
    // 模型空间的直线 / 多段线线段（与天正分解结果比对用）
    if (db->ModelSpace())
        for (MiniDWG::Handle h : db->ModelSpace()->Entities)
        {
            if (const auto* line = db->FindAs<MiniDWG::Line>(h))
                std::printf("REFSEG %.9f,%.9f %.9f,%.9f\n", line->StartPoint.X, line->StartPoint.Y, line->EndPoint.X, line->EndPoint.Y);
            else if (const auto* pl = db->FindAs<MiniDWG::LwPolyline>(h))
            {
                const auto& v = pl->Vertices;
                const bool closed = (static_cast<int>(pl->Flags) & 1) != 0;
                for (std::size_t i = 0; i + 1 < v.size() + (closed ? 1 : 0); ++i)
                {
                    const auto& a = v[i];
                    const auto& b = v[(i + 1) % v.size()];
                    std::printf("REFSEG %.9f,%.9f %.9f,%.9f%s\n", a.Location.X, a.Location.Y, b.Location.X, b.Location.Y,
                                a.Bulge != 0 ? " bulge" : "");
                }
            }
        }
    if (db->ModelSpace())
        for (MiniDWG::Handle h : db->ModelSpace()->Entities)
            if (const auto* insert = db->FindAs<MiniDWG::Insert>(h))
            {
                const auto* record = db->FindAs<MiniDWG::BlockRecord>(insert->BlockHandle);
                std::printf("INSERT %llx %s at=%.6f,%.6f,%.6f scale=%.6f,%.6f,%.6f rot=%.9f normal=%.3f,%.3f,%.3f\n",
                            static_cast<unsigned long long>(h), record ? record->Name.c_str() : "?",
                            insert->InsertPoint.X, insert->InsertPoint.Y, insert->InsertPoint.Z,
                            insert->XScale, insert->YScale, insert->ZScale, insert->Rotation,
                            insert->Normal.X, insert->Normal.Y, insert->Normal.Z);
            }
    // 天正系统块（门窗图块等）的内容
    for (const auto& [handle, object] : db->Objects())
    {
        const auto* record = dynamic_cast<const MiniDWG::BlockRecord*>(object.get());
        if (!record || (!record->Name.starts_with("$TCHSYS$") && !record->Name.starts_with("$LinePat$"))) continue;
        const auto* block = db->FindAs<MiniDWG::Block>(record->BlockEntityHandle);
        std::printf("TCHBLOCK %s base=%.6g,%.6g entities=%zu\n", record->Name.c_str(), block ? block->BasePoint.X : 0.0,
                    block ? block->BasePoint.Y : 0.0, record->Entities.size());
        for (MiniDWG::Handle h : record->Entities)
        {
            const auto* e = db->Find(h);
            if (const auto* line = dynamic_cast<const MiniDWG::Line*>(e))
                std::printf("  LINE %.6g,%.6g,%.6g %.6g,%.6g,%.6g\n", line->StartPoint.X, line->StartPoint.Y, line->StartPoint.Z,
                            line->EndPoint.X, line->EndPoint.Y, line->EndPoint.Z);
            else if (const auto* arc = dynamic_cast<const MiniDWG::Arc*>(e))
                std::printf("  ARC c=%.6g,%.6g r=%.6g a=%.6g..%.6g\n", arc->Center.X, arc->Center.Y, arc->Radius, arc->StartAngle, arc->EndAngle);
            else if (const auto* pl = dynamic_cast<const MiniDWG::LwPolyline*>(e))
            {
                std::printf("  LWPOLYLINE n=%zu", pl->Vertices.size());
                for (const auto& v : pl->Vertices) std::printf(" %.6g,%.6g b=%.6g w=%.6g/%.6g", v.Location.X, v.Location.Y, v.Bulge, v.StartWidth, v.EndWidth);
                std::printf("\n");
            }
            else
                std::printf("  %s\n", e ? std::string(e->GetDxfName()).c_str() : "(missing)");
        }
    }
    MiniCAD::Document document;
    const auto u8 = path.u8string();
    std::string error;
    auto tLoad0 = std::chrono::steady_clock::now();
    const bool loaded = document.LoadFromFile(std::string(u8.begin(), u8.end()), &error);
    std::printf("TIME LoadFromFile %.1f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tLoad0).count());
    if (std::getenv("MINICAD_TABLES") && loaded)
    {
        for (const auto& r : document.GetScene().GetTextStyleTable().Records())
            std::printf("SCENESTYLE %u %s font=[%s] big=[%s] h=%g w=%g ob=%g\n", r.Id, r.Name.c_str(), r.FontFile.c_str(), r.BigFontFile.c_str(), r.Height, r.WidthFactor, r.ObliqueDeg);
        const auto& lts = document.GetScene().GetLineTypeTable().Records();
        for (std::size_t i = 0; i < lts.size(); ++i)
        {
            std::printf("SCENELTYPE %zu %s desc=[%s] len=%g:", i, lts[i].Name.c_str(), lts[i].Description.c_str(), lts[i].PatternLength);
            for (double d : lts[i].Pattern) std::printf(" %g", d);
            std::printf("\n");
        }
        std::printf("SCENE ltscale=%g layouts=%zu\n", document.GetScene().GetLineTypeScale(), document.GetScene().Layouts().size());
    }
    // MINICAD_WIPECHECK：按 Editor 的方式逐实体细分并用区域覆盖裁剪，统计遮罩内残留的线段（遮罩之前画的应为 0）
    if (std::getenv("MINICAD_WIPECHECK") && loaded)
    {
        auto& scene = document.GetScene();
        std::vector<MiniCAD::Vertex_P3_C4> all, allFill, lines, fills, raw;
        std::vector<MiniCAD::Vertex_P3_C4_UV> texts;
        std::vector<std::vector<MiniCAD::Math::Point3>> wipesAll;
        MiniCAD::Overlay overlay;
        MiniCAD::DrawContext ctx(lines, fills, texts, overlay, scene.GetLayerManager(), scene.GetLineTypeTable());
        std::size_t before = 0;
        scene.ForEachObject([&](const MiniCAD::Object& o) {
            if (!o.IsKindOf<MiniCAD::Entity>()) return;
            const auto& e = static_cast<const MiniCAD::Entity&>(o);
            if (const auto* l = scene.GetLayerManager().GetLayer(e.GetAttr().LayerId); l && !l->IsVisible()) return;
            lines.clear(); fills.clear(); texts.clear();
            ctx.BeginEntity(&e.GetAttr());
            e.Draw(ctx, false, false);
            ctx.BeginEntity(nullptr);
            for (auto& w : ctx.TakeWipes())
            {
                const MiniCAD::WipeClip::Poly poly(w);
                before += all.size();
                MiniCAD::WipeClip::ClipLines(all, poly);
                MiniCAD::WipeClip::ClipFills(allFill, poly);
                wipesAll.push_back(w);
            }
            all.insert(all.end(), lines.begin(), lines.end());
            raw.insert(raw.end(), lines.begin(), lines.end());
            allFill.insert(allFill.end(), fills.begin(), fills.end());
        });
        auto countInside = [&](const std::vector<MiniCAD::Vertex_P3_C4>& v) {
            int inside = 0;
            for (const auto& w : wipesAll)
            {
                const MiniCAD::WipeClip::Poly poly(w);
                for (std::size_t k = 0; k + 1 < v.size(); k += 2)
                {
                    const double mx = (v[k].pos.x + v[k + 1].pos.x) / 2, my = (v[k].pos.y + v[k + 1].pos.y) / 2;
                    if (poly.BoxOverlaps(mx, my, mx, my) && poly.Contains(mx, my)) ++inside;
                }
            }
            return inside;
        };
        std::printf("WIPECHECK wipes=%zu inside_without_clip=%d inside_with_clip=%d (后者只剩遮罩之后画的)\n", wipesAll.size(), countInside(raw), countInside(all));
        return 0;
    }
    // MINICAD_REDAT=x0,y0,x1,y1：列出在该范围内画出红色线段的顶层实体
    if (const char* r = std::getenv("MINICAD_REDAT"); r && loaded)
    {
        double x0, y0, x1, y1;
        if (std::sscanf(r, "%lf,%lf,%lf,%lf", &x0, &y0, &x1, &y1) == 4)
        {
            struct RedSink : MiniCAD::IDrawSink
            {
                double x0, y0, x1, y1; int hits = 0;
                MiniCAD::Math::Color4 GetLayerColor(MiniCAD::LayerID id) const override { return layers->GetLayer(id) ? layers->GetLayer(id)->GetColor() : MiniCAD::Math::Color4::White(); }
                const MiniCAD::LayerManager* layers = nullptr;
                void DrawLine(const MiniCAD::Math::Point3& a, const MiniCAD::Math::Point3& b, const MiniCAD::Math::Color4& c, bool) override
                {
                    const double mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
                    if (c.r > 0.9 && c.g < 0.1 && c.b < 0.1 && mx > x0 && mx < x1 && my > y0 && my < y1) ++hits;
                }
            };
            std::function<void(const MiniCAD::Entity&, const MiniCAD::Math::Mat4&, int)> visit = [&](const MiniCAD::Entity& e, const MiniCAD::Math::Mat4& m, int depth) {
                RedSink s; s.x0 = x0; s.y0 = y0; s.x1 = x1; s.y1 = y1; s.layers = &document.GetScene().GetLayerManager();
                MiniCAD::TransformDrawSink xs(s, m);
                e.Draw(xs, false, false);
                if (s.hits == 0) return;
                const auto* layer = document.GetScene().GetLayerManager().GetLayer(e.GetAttr().LayerId);
                const bool ins = e.IsKindOf<MiniCAD::InsertEntity>();
                std::printf("RED %*s%s %s layer=%s hits=%d\n", depth * 2, "", e.GetTypeInfo()->Name,
                            ins ? static_cast<const MiniCAD::InsertEntity&>(e).GetBlockName().c_str() : "", layer ? layer->GetName().c_str() : "?", s.hits);
                if (ins && depth < 6)
                {
                    const auto& in = static_cast<const MiniCAD::InsertEntity&>(e);
                    if (in.GetBlock())
                        for (const auto& child : in.GetBlock()->GetEntities())
                            visit(*child, in.CellMatrix(0, 0) * m, depth + 1);
                }
            };
            document.GetScene().ForEachObject([&](const MiniCAD::Object& o) {
                if (o.IsKindOf<MiniCAD::Entity>()) visit(static_cast<const MiniCAD::Entity&>(o), MiniCAD::Math::Mat4::Identity(), 0);
            });
        }
        return 0;
    }
    // MINICAD_MEM：块参照展开（解析副本）与细分各占多少内存
    if (std::getenv("MINICAD_MEM") && loaded)
    {
        auto privMb = [] {
            PROCESS_MEMORY_COUNTERS_EX pmc = {}; pmc.cb = sizeof(pmc);
            K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc));
            return static_cast<double>(pmc.PrivateUsage) / 1048576.0;
        };
        std::size_t expanded = 0, inserts = 0;
        std::function<void(const MiniCAD::BlockEntity*, int)> count = [&](const MiniCAD::BlockEntity* b, int depth) {
            if (!b || depth > 16) return;
            for (const auto& e : b->GetEntities())
            {
                ++expanded;
                if (e->IsKindOf<MiniCAD::InsertEntity>()) { ++inserts; count(static_cast<const MiniCAD::InsertEntity&>(*e).GetBlock(), depth + 1); }
            }
        };
        std::size_t top = 0, blockEntities = 0;
        document.GetScene().ForEachObject([&](const MiniCAD::Object& o) {
            ++top;
            if (o.IsKindOf<MiniCAD::InsertEntity>()) { ++inserts; count(static_cast<const MiniCAD::InsertEntity&>(o).GetBlock(), 1); }
        });
        document.GetScene().GetBlockTable().ForEach([&](MiniCAD::BlockID, const MiniCAD::BlockEntity& b) { blockEntities += b.GetEntities().size(); });
        std::printf("MEM 顶层 %zu，块定义内实体 %zu，展开后实体 %zu（块参照 %zu）\n", top, blockEntities, expanded, inserts);
        struct NullSink : MiniCAD::IDrawSink
        {
            std::size_t lines = 0, tris = 0;
            void DrawLine(const MiniCAD::Math::Point3&, const MiniCAD::Math::Point3&, const MiniCAD::Math::Color4&, bool) override { ++lines; }
            void FillTriangle(const MiniCAD::Math::Point3&, const MiniCAD::Math::Point3&, const MiniCAD::Math::Point3&, const MiniCAD::Math::Color4&) override { ++tris; }
        } sink;
        const double m0 = privMb();
        auto t0 = std::chrono::steady_clock::now();
        document.GetScene().ForEachObject([&](const MiniCAD::Object& o) {
            if (o.IsKindOf<MiniCAD::Entity>()) static_cast<const MiniCAD::Entity&>(o).Draw(sink, false, false);
        });
        const double ms1 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const double m1 = privMb();
        t0 = std::chrono::steady_clock::now();
        document.GetScene().ForEachObject([&](const MiniCAD::Object& o) {
            if (o.IsKindOf<MiniCAD::Entity>()) static_cast<const MiniCAD::Entity&>(o).Draw(sink, false, false);
        });
        const double ms2 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("MEM 加载后 %.0f MB，第一次绘制（含解析副本）后 %.0f MB（%.0f ms），第二次绘制 %.0f ms；线段 %zu 三角形 %zu（两遍）\n",
                    m0, m1, ms1, ms2, sink.lines, sink.tris);
        // 按 Editor 的方式细分（全图约 1500 像素宽），统计随视图变化的实体与其顶点
        {
            MiniCAD::AABB ext;
            document.GetScene().GetExtents(ext);
            const double wpp = (ext.Max.x - ext.Min.x) / 1500.0;
            std::vector<MiniCAD::Vertex_P3_C4> lines, fills;
            std::vector<MiniCAD::Vertex_P3_C4_UV> texts;
            MiniCAD::Overlay overlay;
            MiniCAD::DrawContext ctx(lines, fills, texts, overlay, document.GetScene().GetLayerManager(), document.GetScene().GetLineTypeTable(), wpp);
            std::size_t vdN = 0, n = 0, vdL = 0, vdF = 0, allL = 0, allF = 0;
            std::map<std::string, std::size_t> vdTypes;
            t0 = std::chrono::steady_clock::now();
            document.GetScene().ForEachObject([&](const MiniCAD::Object& o) {
                if (!o.IsKindOf<MiniCAD::Entity>()) return;
                const auto& e = static_cast<const MiniCAD::Entity&>(o);
                lines.clear(); fills.clear(); texts.clear();
                ctx.ResetViewDependentFlag();
                ctx.BeginEntity(&e.GetAttr());
                e.Draw(ctx, false, false);
                ctx.BeginEntity(nullptr);
                ctx.TakeWipes();
                ++n; allL += lines.size(); allF += fills.size();
                if (ctx.HasViewDependentGeometry())
                {
                    ++vdN; vdL += lines.size(); vdF += fills.size();
                    vdTypes[e.IsKindOf<MiniCAD::InsertEntity>() ? "INSERT " + static_cast<const MiniCAD::InsertEntity&>(e).GetBlockName() : e.GetTypeInfo()->Name] += lines.size() + fills.size();
                }
            });
            const double ms3 = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("MEM DrawContext 细分 %.0f ms：实体 %zu，随视图变化 %zu；顶点 线 %zu/%zu 填充 %zu/%zu（随视图/全部）\n", ms3, n, vdN, vdL, allL, vdF, allF);
            // 按叶子实体类型统计顶点（逐层展开块参照，按引用次数计）
            std::map<std::string, std::pair<std::size_t, std::size_t>> byType;
            std::function<void(const MiniCAD::Entity&, int)> leaf = [&](const MiniCAD::Entity& e, int depth) {
                if (e.IsKindOf<MiniCAD::InsertEntity>() && depth < 16)
                {
                    const auto& in = static_cast<const MiniCAD::InsertEntity&>(e);
                    if (in.GetBlock())
                        for (int k = 0; k < in.GetRowCount() * in.GetColumnCount(); ++k)
                            for (const auto& c : in.GetBlock()->GetEntities()) leaf(*c, depth + 1);
                    return;
                }
                lines.clear(); fills.clear(); texts.clear();
                ctx.BeginEntity(&e.GetAttr());
                e.Draw(ctx, false, false);
                ctx.BeginEntity(nullptr);
                ctx.TakeWipes();
                auto& t = byType[e.GetTypeInfo()->Name];
                t.first += lines.size(); t.second += fills.size();
            };
            document.GetScene().ForEachObject([&](const MiniCAD::Object& o) { if (o.IsKindOf<MiniCAD::Entity>()) leaf(static_cast<const MiniCAD::Entity&>(o), 0); });
            for (auto& [k, v] : byType) std::printf("MEM 类型 %-24s 线顶点 %9zu 填充顶点 %9zu\n", k.c_str(), v.first, v.second);
            std::vector<std::pair<std::size_t, std::string>> top10;
            for (auto& [k, v] : vdTypes) top10.push_back({ v, k });
            std::sort(top10.rbegin(), top10.rend());
            for (std::size_t i = 0; i < top10.size() && i < 10; ++i) std::printf("MEM   %zu %s\n", top10[i].first, top10[i].second.c_str());
        }
        return 0;
    }
    // MINICAD_GLYPHS：按文字样式解析字体，列出字体里没有字形的字符（需在有 fonts 目录的工作目录下运行）
    if (std::getenv("MINICAD_GLYPHS") && loaded)
    {
        static MiniCAD::FontSystem fontSystem;
        fontSystem.Initialize();
        auto resolver = MiniCAD::MakeTextStyleResolver(&fontSystem, &document.GetScene().GetTextStyleTable());
        if (!resolver) { std::printf("GLYPHS 字体系统未就绪\n"); return 1; }
        std::map<std::string, std::map<std::uint32_t, int>> missing;    // 样式 → 缺字 → 次数
        std::map<std::string, std::string> sample;
        auto check = [&](const std::string& text, std::uint32_t styleId) {
            const auto& rec = document.GetScene().GetTextStyleTable().Resolve(styleId);
            const std::string key = rec.Name + " [" + rec.FontFile + " + " + rec.BigFontFile + "]";
            MiniCAD::ResolvedTextStyle rs = resolver(styleId);
            if (!rs.Font) { ++missing[key][0]; return; }
            const auto u32 = [&] {
                std::u32string out;
                for (std::size_t i = 0; i < text.size();)
                {
                    const unsigned char c = text[i];
                    const int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
                    std::uint32_t cp = n == 1 ? c : c & (0xFF >> (n + 1));
                    for (int k = 1; k < n && i + k < text.size(); ++k) cp = (cp << 6) | (text[i + k] & 0x3F);
                    out.push_back(cp);
                    i += n;
                }
                return out;
            }();
            for (char32_t cp : u32)
                if (cp > 0x20 && !rs.Font->HasGlyph(cp))
                {
                    if (missing[key][cp]++ == 0 && !sample.count(key)) sample[key] = text;
                }
        };
        auto visit = [&](const MiniCAD::Entity& e) {
            if (e.IsKindOf<MiniCAD::TextEntity>()) { const auto& t = static_cast<const MiniCAD::TextEntity&>(e); check(t.GetText(), t.GetStyleId()); }
            else if (e.IsKindOf<MiniCAD::MTextEntity>()) { const auto& t = static_cast<const MiniCAD::MTextEntity&>(e); check(t.GetText(), t.GetStyleId()); }
        };
        document.GetScene().ForEachObject([&](const MiniCAD::Object& o) { if (o.IsKindOf<MiniCAD::Entity>()) visit(static_cast<const MiniCAD::Entity&>(o)); });
        document.GetScene().GetBlockTable().ForEach([&](MiniCAD::BlockID, const MiniCAD::BlockEntity& b) { for (const auto& e : b.GetEntities()) visit(*e); });
        for (const auto& [key, cps] : missing)
        {
            int total = 0;
            for (const auto& [cp, n] : cps) total += n;
            std::printf("GLYPHS %s：缺 %zu 种字符共 %d 处，例：", key.c_str(), cps.size(), total);
            int shown = 0;
            for (const auto& [cp, n] : cps)
                if (shown++ < 30) std::printf(" U+%04X", cp);
            std::printf("\n  原文例：%s\n", sample[key].c_str());
        }
        return 0;
    }
    // MINICAD_BLOCK=块名子串：打印名称含该子串的块里的文字（位置、字高、宽度因子、内容）
    if (const char* bn = std::getenv("MINICAD_BLOCK"); bn && loaded)
    {
        document.GetScene().GetBlockTable().ForEach([&](MiniCAD::BlockID, const MiniCAD::BlockEntity& b) {
            if (b.GetName().find(bn) == std::string::npos) return;
            std::printf("BLOCK %s base (%.3f,%.3f) box (%.1f,%.1f)-(%.1f,%.1f)\n", b.GetName().c_str(), b.GetBasePoint().x, b.GetBasePoint().y,
                        b.GetBoundingBox().Min.x, b.GetBoundingBox().Min.y, b.GetBoundingBox().Max.x, b.GetBoundingBox().Max.y);
            for (const auto& e : b.GetEntities())
                if (e->IsKindOf<MiniCAD::TextEntity>())
                {
                    const auto& t = static_cast<const MiniCAD::TextEntity&>(*e);
                    std::printf("  TEXT (%.1f,%.1f) h=%.2f w=%.4f [%s]\n", t.GetPosition().x, t.GetPosition().y, t.GetHeight(), t.GetWidthFactor(), t.GetText().c_str());
                }
                else if (e->IsKindOf<MiniCAD::MTextEntity>())
                {
                    const auto& t = static_cast<const MiniCAD::MTextEntity&>(*e);
                    std::printf("  MTEXT (%.1f,%.1f) h=%.2f [%s]\n", t.GetPosition().x, t.GetPosition().y, t.GetHeight(), t.GetText().c_str());
                }
                else
                    std::printf("  %s\n", e->GetTypeInfo()->Name);
        });
        return 0;
    }
    // MINICAD_RENDER=x0,y0,x1,y1[,文件]：把范围内的线画成 BMP（白底黑线，填充灰色），默认 render.bmp；MINICAD_RENDER_INS 打印范围内块参照
    if (const char* rr = std::getenv("MINICAD_RENDER"); rr && loaded)
    {
        double x0, y0, x1, y1;
        char outName[260] = "render.bmp";
        if (std::sscanf(rr, "%lf,%lf,%lf,%lf,%259s", &x0, &y0, &x1, &y1, outName) >= 4)
        {
            const int W = 1600, H = static_cast<int>(1600 * (y1 - y0) / (x1 - x0));
            std::vector<std::uint8_t> img(static_cast<std::size_t>(W) * H * 3, 255);
            struct RenderSink : MiniCAD::IDrawSink
            {
                double x0, y0, sc; int W, H; std::vector<std::uint8_t>* img;
                void Put(int x, int y, std::uint8_t v)
                {
                    if (x < 0 || y < 0 || x >= W || y >= H) return;
                    auto* p = &(*img)[(static_cast<std::size_t>(H - 1 - y) * W + x) * 3];
                    p[0] = p[1] = p[2] = std::min(p[0], v);
                }
                void DrawLine(const MiniCAD::Math::Point3& a, const MiniCAD::Math::Point3& b, const MiniCAD::Math::Color4&, bool) override
                {
                    const double ax = (a.x - x0) * sc, ay = (a.y - y0) * sc, bx = (b.x - x0) * sc, by = (b.y - y0) * sc;
                    const int n = static_cast<int>(std::max(std::abs(bx - ax), std::abs(by - ay))) + 1;
                    if (n > 100000) return;
                    for (int i = 0; i <= n; ++i) Put(static_cast<int>(ax + (bx - ax) * i / n), static_cast<int>(ay + (by - ay) * i / n), 0);
                }
                void FillTriangle(const MiniCAD::Math::Point3& a, const MiniCAD::Math::Point3& b, const MiniCAD::Math::Point3& c, const MiniCAD::Math::Color4&) override
                {
                    const double px[3] = {(a.x - x0) * sc, (b.x - x0) * sc, (c.x - x0) * sc}, py[3] = {(a.y - y0) * sc, (b.y - y0) * sc, (c.y - y0) * sc};
                    const int bx0 = std::max(0, static_cast<int>(std::min({px[0], px[1], px[2]}))), bx1 = std::min(W - 1, static_cast<int>(std::max({px[0], px[1], px[2]})));
                    const int by0 = std::max(0, static_cast<int>(std::min({py[0], py[1], py[2]}))), by1 = std::min(H - 1, static_cast<int>(std::max({py[0], py[1], py[2]})));
                    for (int y = by0; y <= by1; ++y)
                        for (int x = bx0; x <= bx1; ++x)
                        {
                            auto e = [&](int i, int j) { return (px[j] - px[i]) * (y + 0.5 - py[i]) - (py[j] - py[i]) * (x + 0.5 - px[i]); };
                            const double e0 = e(0, 1), e1 = e(1, 2), e2 = e(2, 0);
                            if ((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0)) Put(x, y, 190);
                        }
                }
            } sink;
            sink.x0 = x0; sink.y0 = y0; sink.sc = W / (x1 - x0); sink.W = W; sink.H = H; sink.img = &img;
            document.GetScene().ForEachObject([&](const MiniCAD::Object& o) {
                if (!o.IsKindOf<MiniCAD::Entity>()) return;
                const auto& e = static_cast<const MiniCAD::Entity&>(o);
                if (!e.GetAttr().Visible || !document.GetScene().GetLayerManager().GetLayer(e.GetAttr().LayerId) ||
                    !document.GetScene().GetLayerManager().GetLayer(e.GetAttr().LayerId)->IsVisible()) return;
                const auto b = e.GetBoundingBox();
                if (b.Max.x < x0 || b.Min.x > x1 || b.Max.y < y0 || b.Min.y > y1) return;
                if (std::getenv("MINICAD_RENDER_INS") && e.IsKindOf<MiniCAD::InsertEntity>())
                {
                    const auto& in = static_cast<const MiniCAD::InsertEntity&>(e);
                    std::printf("INS %s (%.1f,%.1f) clip=%zu box (%.1f,%.1f)-(%.1f,%.1f)\n", in.GetBlockName().c_str(), in.GetPosition().x, in.GetPosition().y,
                                in.GetClipBoundary().size(), b.Min.x, b.Min.y, b.Max.x, b.Max.y);
                }
                e.Draw(sink, false, false);
            });
            const std::uint32_t rowBytes = (W * 3 + 3) & ~3u, size = 54 + rowBytes * H;
            std::vector<std::uint8_t> bmp(size, 0);
            auto put32 = [&](std::size_t at, std::uint32_t v) { for (int i = 0; i < 4; ++i) bmp[at + i] = static_cast<std::uint8_t>(v >> (8 * i)); };
            bmp[0] = 'B'; bmp[1] = 'M'; put32(2, size); put32(10, 54); put32(14, 40); put32(18, W); put32(22, static_cast<std::uint32_t>(-H));
            bmp[26] = 1; bmp[28] = 24; put32(34, rowBytes * H);
            for (int y = 0; y < H; ++y) std::memcpy(&bmp[54 + y * rowBytes], &img[static_cast<std::size_t>(y) * W * 3], W * 3);
            std::ofstream(outName, std::ios::binary).write(reinterpret_cast<const char*>(bmp.data()), bmp.size());
            std::printf("RENDER %dx%d -> %s\n", W, H, outName);
        }
        return 0;
    }
    // MINICAD_AT=x0,y0,x1,y1：列出在该范围内画出线段的实体（逐层展开块参照）
    if (const char* r = std::getenv("MINICAD_AT"); r && loaded)
    {
        double x0, y0, x1, y1;
        if (std::sscanf(r, "%lf,%lf,%lf,%lf", &x0, &y0, &x1, &y1) == 4)
        {
            struct AtSink : MiniCAD::IDrawSink
            {
                double x0, y0, x1, y1; int hits = 0;
                MiniCAD::Math::Color4 GetLayerColor(MiniCAD::LayerID id) const override { return layers->GetLayer(id) ? layers->GetLayer(id)->GetColor() : MiniCAD::Math::Color4::White(); }
                const MiniCAD::LayerManager* layers = nullptr;
                void DrawLine(const MiniCAD::Math::Point3& a, const MiniCAD::Math::Point3& b, const MiniCAD::Math::Color4&, bool) override
                {
                    const double mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
                    if (mx > x0 && mx < x1 && my > y0 && my < y1) ++hits;
                }
            };
            std::function<void(const MiniCAD::Entity&, const MiniCAD::Math::Mat4&, int)> visit = [&](const MiniCAD::Entity& e, const MiniCAD::Math::Mat4& m, int depth) {
                AtSink s; s.x0 = x0; s.y0 = y0; s.x1 = x1; s.y1 = y1; s.layers = &document.GetScene().GetLayerManager();
                MiniCAD::TransformDrawSink xs(s, m);
                e.Draw(xs, false, false);
                if (s.hits == 0) return;
                const auto* layer = document.GetScene().GetLayerManager().GetLayer(e.GetAttr().LayerId);
                const bool ins = e.IsKindOf<MiniCAD::InsertEntity>();
                std::printf("AT %*s%s %s id=%llu layer=%s visible=%d hits=%d\n", depth * 2, "", e.GetTypeInfo()->Name,
                            ins ? static_cast<const MiniCAD::InsertEntity&>(e).GetBlockName().c_str() : "", (unsigned long long)e.GetID(),
                            layer ? layer->GetName().c_str() : "?", int(e.GetAttr().Visible), s.hits);
                if (ins && depth < 6)
                {
                    const auto& in = static_cast<const MiniCAD::InsertEntity&>(e);
                    if (in.GetBlock())
                        for (const auto& child : in.GetBlock()->GetEntities())
                            visit(*child, in.CellMatrix(0, 0) * m, depth + 1);
                }
            };
            document.GetScene().ForEachObject([&](const MiniCAD::Object& o) {
                if (o.IsKindOf<MiniCAD::Entity>()) visit(static_cast<const MiniCAD::Entity&>(o), MiniCAD::Math::Mat4::Identity(), 0);
            });
        }
        return 0;
    }
    // MINICAD_LOCATE：画一遍模型空间，打印区域覆盖（世界坐标）与含填充的顶层实体包围盒
    if (std::getenv("MINICAD_LOCATE") && loaded)
    {
        struct LocSink : MiniCAD::IDrawSink
        {
            std::vector<std::vector<MiniCAD::Math::Point3>> wipes;
            void DrawLine(const MiniCAD::Math::Point3&, const MiniCAD::Math::Point3&, const MiniCAD::Math::Color4&, bool) override {}
            void Wipe(const std::vector<MiniCAD::Math::Point3>& p) override { wipes.push_back(p); }
        } sink;
        std::function<bool(const MiniCAD::BlockEntity*, int)> hasHatch = [&](const MiniCAD::BlockEntity* b, int depth) -> bool {
            if (!b || depth > 8) return false;
            for (const auto& e : b->GetEntities())
            {
                if (e->IsKindOf<MiniCAD::HatchEntity>()) return true;
                if (e->IsKindOf<MiniCAD::InsertEntity>() && hasHatch(static_cast<const MiniCAD::InsertEntity&>(*e).GetBlock(), depth + 1)) return true;
            }
            return false;
        };
        int shown = 0;
        document.GetScene().ForEachObject([&](const MiniCAD::Object& o) {
            if (!o.IsKindOf<MiniCAD::Entity>()) return;
            const auto& e = static_cast<const MiniCAD::Entity&>(o);
            e.Draw(sink, false, false);
            const bool h = e.IsKindOf<MiniCAD::HatchEntity>() ||
                           (e.IsKindOf<MiniCAD::InsertEntity>() && hasHatch(static_cast<const MiniCAD::InsertEntity&>(e).GetBlock(), 0));
            if (h && shown++ < 40)
            {
                const auto b = e.GetBoundingBox();
                std::printf("HATCHBOX %s (%.0f,%.0f)-(%.0f,%.0f)\n", e.IsKindOf<MiniCAD::InsertEntity>() ? static_cast<const MiniCAD::InsertEntity&>(e).GetBlockName().c_str() : "HATCH",
                            b.Min.x, b.Min.y, b.Max.x, b.Max.y);
            }
        });
        for (const auto& w : sink.wipes)
            std::printf("WIPEPOS (%.0f,%.0f) (%.0f,%.0f) (%.0f,%.0f)\n", w[0].x, w[0].y, w[1].x, w[1].y, w[2].x, w[2].y);
        return 0;
    }
    if (std::getenv("MINICAD_QUIT")) return 0;
    // MINICAD_BBOX=x0,y0,x1,y1：打印包围盒与之相交的顶层块参照及块内实体（递归 2 层）的类型、图层、颜色
    if (const char* bb = std::getenv("MINICAD_BBOX"))
    {
        double x0, y0, x1, y1;
        std::sscanf(bb, "%lf,%lf,%lf,%lf", &x0, &y0, &x1, &y1);
        const auto& scene = document.GetScene();
        auto layerName = [&](MiniCAD::LayerID id) { const auto* l = scene.GetLayerManager().GetLayer(id); return l ? l->GetName() : std::string("?"); };
        auto colorText = [](const MiniCAD::EntityColor& c) {
            switch (c.Method) {
            case MiniCAD::ColorMethod::ByLayer: return std::string("ByLayer");
            case MiniCAD::ColorMethod::ByBlock: return std::string("ByBlock");
            case MiniCAD::ColorMethod::ByAci: return "ACI" + std::to_string(c.Aci);
            default: return std::string("RGB");
            }
        };
        std::function<void(const MiniCAD::Entity&, int)> dump = [&](const MiniCAD::Entity& e, int depth) {
            const auto& a = e.GetAttr();
            const auto* in = dynamic_cast<const MiniCAD::InsertEntity*>(&e);
            std::printf("%*s%s layer=%s color=%s%s%s\n", depth * 2, "", e.GetTypeInfo()->Name, layerName(a.LayerId).c_str(), colorText(a.Color).c_str(),
                        in ? " block=" : "", in && in->GetBlock() ? in->GetBlock()->GetName().c_str() : "");
            if (in && in->GetBlock() && depth < 2)
            {
                std::map<std::string, int> seen;
                for (const auto& child : in->GetBlock()->GetEntities())
                {
                    const auto& ca = child->GetAttr();
                    const std::string key = std::string(child->GetTypeInfo()->Name) + layerName(ca.LayerId) + colorText(ca.Color);
                    if (seen[key]++ == 0) dump(*child, depth + 1);
                }
            }
        };
        // 按世界坐标逐层展开：只打印包围盒与查询框相交的叶子实体（同一路径下同类同层同色只计数一次）
        std::map<std::string, int> leaves;
        std::function<void(const MiniCAD::Entity&, const MiniCAD::Math::Mat4&, const std::string&)> walk =
            [&](const MiniCAD::Entity& e, const MiniCAD::Math::Mat4& m, const std::string& path) {
            const auto local = e.GetBoundingBox();
            MiniCAD::AABB box = MiniCAD::AABB::Empty();
            for (int i = 0; i < 8; ++i)
                box.Expand(m * MiniCAD::Math::Point3{(i & 1) ? local.Max.x : local.Min.x, (i & 2) ? local.Max.y : local.Min.y, (i & 4) ? local.Max.z : local.Min.z});
            const bool emptyBox = local.Min.x > local.Max.x;
            if (!emptyBox && (box.Max.x < x0 || box.Min.x > x1 || box.Max.y < y0 || box.Min.y > y1)) return;
            const auto& a = e.GetAttr();
            const auto* layer = scene.GetLayerManager().GetLayer(a.LayerId);
            const std::string self = std::string(e.GetTypeInfo()->Name) + "[" + layerName(a.LayerId) + (layer && !layer->IsVisible() ? "(off)" : "") + "," + colorText(a.Color) + "]";
            if (const auto* in = dynamic_cast<const MiniCAD::InsertEntity*>(&e); in && in->GetBlock())
            {
                const std::string p = path + " > " + self + (emptyBox ? "(EMPTYBOX)" : "") + "{" + in->GetBlock()->GetName() + "}";
                const auto cm = in->CellMatrix(0, 0) * m;
                for (const auto& child : in->GetBlock()->GetEntities()) walk(*child, cm, p);
                return;
            }
            if (emptyBox) return;
            if (std::getenv("MINICAD_LEAFTYPE") && std::string(e.GetTypeInfo()->Name) != std::getenv("MINICAD_LEAFTYPE")) return;
            ++leaves[path + " > " + self];
        };
        scene.ForEachObject([&](const MiniCAD::Object& object) {
            if (const auto* e = dynamic_cast<const MiniCAD::Entity*>(&object)) walk(*e, MiniCAD::Math::Mat4::Identity(), "");
        });
        for (const auto& [k, n] : leaves) std::printf("LEAF %d %s\n", n, k.c_str());
        // 实际绘制：查询框内的线段按颜色计数（顶层实体 Draw，块参照内部已变换到世界坐标）
        struct Rec : MiniCAD::IDrawSink
        {
            const MiniCAD::Scene* sc = nullptr; double x0 = 0, y0 = 0, x1 = 0, y1 = 0; std::map<std::string, int> colors; std::string top;
            MiniCAD::Math::Color4 GetLayerColor(MiniCAD::LayerID id) const override { const auto* l = sc->GetLayerManager().GetLayer(id); return l ? l->GetColor() : MiniCAD::Math::Color4::White(); }
            void DrawLine(const MiniCAD::Math::Point3& a, const MiniCAD::Math::Point3& b, const MiniCAD::Math::Color4& c, bool) override
            {
                const double mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
                if (mx < x0 || mx > x1 || my < y0 || my > y1) return;
                char buf[96]; std::snprintf(buf, sizeof buf, "%.2f,%.2f,%.2f", c.r, c.g, c.b);
                ++colors[top + " " + buf];
            }
        } rec;
        rec.sc = &scene; rec.x0 = x0; rec.y0 = y0; rec.x1 = x1; rec.y1 = y1;
        scene.ForEachObject([&](const MiniCAD::Object& object) {
            if (const auto* e = dynamic_cast<const MiniCAD::Entity*>(&object))
            {
                const auto* in = dynamic_cast<const MiniCAD::InsertEntity*>(e);
                rec.top = std::string(e->GetTypeInfo()->Name) + (in && in->GetBlock() ? "{" + in->GetBlock()->GetName() + "}" : "");
                e->Draw(rec, false, false);
            }
        });
        for (const auto& [k, n] : rec.colors) std::printf("DRAWN %d %s\n", n, k.c_str());
        return 0;
    }
    MiniCAD::AABB bounds = MiniCAD::AABB::Empty();
    const bool extents = document.GetScene().GetExtents(bounds);
    std::printf("MiniCAD loaded=%d entities=%d extents=%d error=%s\n", loaded, document.GetScene().EntityCount(), extents, error.c_str());
    // 天正对象生成的几何（*U_TCH_<十进制句柄> 块，嵌套块参照按 2D 变换展开）：与天正分解结果比对用
    // MINICAD_GEOMBLOCKS：另外输出块定义里的天正对象（块内局部坐标，对应把块插在原点分解的结果）
    if (std::getenv("MINICAD_GEOM"))
    {
        auto geomOne = [](const MiniCAD::Object& object) {
            const auto* top = dynamic_cast<const MiniCAD::InsertEntity*>(&object);
            if (!top || !top->GetBlock() || !top->GetBlock()->GetName().starts_with("*U_TCH_")) return;
            std::string digits;
            for (char c : top->GetBlock()->GetName().substr(7))
            {
                if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
                else if (!digits.empty()) break;
            }
            const unsigned long long h = digits.empty() ? 0 : std::stoull(digits);
            struct X { double ox, oy, a, sx, sy; };
            std::function<void(const MiniCAD::BlockEntity&, X)> walk = [&](const MiniCAD::BlockEntity& b, X x) {
                auto P = [&](double px, double py) {
                    px = (px - b.GetBasePoint().x) * x.sx; py = (py - b.GetBasePoint().y) * x.sy;
                    return std::pair{x.ox + px * std::cos(x.a) - py * std::sin(x.a), x.oy + px * std::sin(x.a) + py * std::cos(x.a)};
                };
                for (const auto& e : b.GetEntities())
                {
                    if (const auto* l = dynamic_cast<const MiniCAD::LineEntity*>(e.get())) {
                        auto [x1, y1] = P(l->GetLine().Start.x, l->GetLine().Start.y);
                        auto [x2, y2] = P(l->GetLine().End.x, l->GetLine().End.y);
                        std::printf("G %llx LINE %.4f %.4f %.4f %.4f\n", h, x1, y1, x2, y2);
                    } else if (const auto* a = dynamic_cast<const MiniCAD::ArcEntity*>(e.get())) {
                        auto [cx, cy] = P(a->GetArc().Center.x, a->GetArc().Center.y);
                        // 负比例（镜像）时角度按镜像映射，且起止角对调
                        auto map = [&](double t) { return std::atan2(std::sin(t) * (x.sy < 0 ? -1 : 1), std::cos(t) * (x.sx < 0 ? -1 : 1)) + x.a; };
                        double a0 = map(a->GetArc().StartAngle), a1 = map(a->GetArc().EndAngle);
                        if (x.sx * x.sy < 0) std::swap(a0, a1);
                        std::printf("G %llx ARC %.4f %.4f %.4f %.4f %.4f\n", h, cx, cy, a->GetArc().Radius * std::abs(x.sx), a0, a1);
                    } else if (const auto* c = dynamic_cast<const MiniCAD::CircleEntity*>(e.get())) {
                        auto [cx, cy] = P(c->GetCircle().Center.x, c->GetCircle().Center.y);
                        std::printf("G %llx CIRCLE %.4f %.4f %.4f\n", h, cx, cy, c->GetCircle().Radius * std::abs(x.sx));
                    } else if (const auto* pl = dynamic_cast<const MiniCAD::PolylineEntity*>(e.get())) {
                        const auto& g = pl->GetPolyline();
                        std::printf("G %llx PL %.4f %zu", h, pl->GetWidth() * std::abs(x.sx), g.Points.size());
                        for (std::size_t i = 0; i < g.Points.size(); ++i) {
                            auto [px, py] = P(g.Points[i].x, g.Points[i].y);
                            std::printf(" %.4f %.4f %.4f", px, py, i < g.Bulges.size() ? g.Bulges[i] : 0.0);
                        }
                        std::printf("\n");
                    } else if (const auto* t = dynamic_cast<const MiniCAD::MTextEntity*>(e.get())) {
                        auto [px, py] = P(t->GetPosition().x, t->GetPosition().y);
                        std::printf("G %llx MTEXT %.4f %.4f %.4f %.4f %d %s\n", h, px, py, t->GetHeight() * std::abs(x.sy), t->GetRotation() + x.a,
                                    static_cast<int>(t->GetAttachment()), t->GetPlainText().c_str());
                    } else if (dynamic_cast<const MiniCAD::SolidEntity*>(e.get())) {
                        std::printf("G %llx SOLID\n", h);
                    } else if (const auto* in = dynamic_cast<const MiniCAD::InsertEntity*>(e.get()); in && in->GetBlock()) {
                        auto [ox, oy] = P(in->GetPosition().x, in->GetPosition().y);
                        walk(*in->GetBlock(), X{ox, oy, x.a + in->GetRotation(), x.sx * in->GetScale().x, x.sy * in->GetScale().y});
                    }
                }
            };
            walk(*top->GetBlock(), X{top->GetPosition().x, top->GetPosition().y, top->GetRotation(), top->GetScale().x, top->GetScale().y});
        };
        document.GetScene().ForEachObject(geomOne);
        if (std::getenv("MINICAD_GEOMBLOCKS"))
            document.GetScene().GetBlockTable().ForEach([&](MiniCAD::BlockID, const MiniCAD::BlockEntity& block) {
                if (block.GetName().starts_with("*U_TCH_")) return;
                for (const auto& e : block.GetEntities()) geomOne(*e);
            });
    }
    const int faces = CountSceneFaces(document.GetScene());
    std::printf("SCENE 3DFACE %d (root entities plus block definitions)\n", faces);
    if (extents) std::printf("Bounds: %.12g %.12g -> %.12g %.12g\n", bounds.Min.x, bounds.Min.y, bounds.Max.x, bounds.Max.y);
    document.GetScene().ForEachObject([](const MiniCAD::Object& object) {
        if (const auto* insert = dynamic_cast<const MiniCAD::InsertEntity*>(&object); insert && insert->GetBlock())
            for (const auto& entity : insert->GetBlock()->GetEntities())
                if (const auto* line = dynamic_cast<const MiniCAD::LineEntity*>(entity.get())) {
                    const auto& g = line->GetLine();
                    std::printf("EDGE %.12g,%.12g %.12g,%.12g\n", g.Start.x, g.Start.y, g.End.x, g.End.y);
                }
        if (const auto* line = dynamic_cast<const MiniCAD::PolylineEntity*>(&object)) {
            std::printf("POLYLINE");
            for (const auto& point : line->GetPolyline().Points)
                std::printf(" %.12g,%.12g", point.x, point.y);
            std::printf("\n");
        }
    });
    // 第 3 个参数：把 MiniCAD 显示的普通几何（不保留天正对象）写成 DWG，供与天正显示对比
    if (argc > 3)
    {
        const auto plain = MiniCAD::ExportCad(document.GetScene(), MiniCAD::CadFileKind::Dwg, MiniCAD::CadSaveVersion::R2013);
        std::ofstream(std::filesystem::path(argv[3]), std::ios::binary).write(reinterpret_cast<const char*>(plain.data()), plain.size());
        std::printf("PLAIN export bytes=%zu\n", plain.size());
    }
    for (const auto kind : {MiniCAD::CadFileKind::Dwg, MiniCAD::CadFileKind::Dxf}) {
        const auto exported = MiniCAD::ExportCad(document.GetScene(), kind);
        MiniCAD::Scene restored;
        const bool imported = MiniCAD::ImportCad(exported, restored);
        std::printf("ROUNDTRIP %s loaded=%d entities=%d\n", kind == MiniCAD::CadFileKind::Dwg ? "DWG" : "DXF", imported, restored.EntityCount());
        const int restoredFaces = CountSceneFaces(restored);
        std::printf("ROUNDTRIP %s 3DFACE=%d\n", kind == MiniCAD::CadFileKind::Dwg ? "DWG" : "DXF", restoredFaces);
        if (restoredFaces != faces) return 1;
        if (!imported || restored.EntityCount() != document.GetScene().EntityCount()) return 1;
    }

    // 按原版本另存：未修改的天正对象应原样写回（原始位流与句柄不变）
    const auto savedPath = std::filesystem::temp_directory_path() / "MiniCADDwgInspect-preserve.dwg";
    const auto savedU8 = savedPath.u8string();
    if (!document.SaveToFile(std::string(savedU8.begin(), savedU8.end()))) { std::printf("PRESERVE save failed\n"); return 1; }
    std::ifstream savedFile(savedPath, std::ios::binary);
    const std::vector<std::uint8_t> savedBytes{std::istreambuf_iterator<char>(savedFile), {}};
    savedFile.close();
    if (argc > 4)
        std::filesystem::copy_file(savedPath, std::filesystem::path(argv[4]), std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(savedPath);
    const auto saved = MiniDWG::ReadDwg(savedBytes, {});
    if (!saved) { std::printf("PRESERVE reread failed\n"); return 1; }
    int tch = 0, same = 0;
    for (const auto& [handle, object] : db->Objects())
    {
        const auto* raw = MiniDWG::RawDataOf(*object);
        if (raw == nullptr || !raw->DxfName.starts_with("TCH_")) continue;
        ++tch;
        const auto* copy = saved->Find(handle);
        const auto* copyRaw = copy ? MiniDWG::RawDataOf(*copy) : nullptr;
        if (copyRaw && copyRaw->DxfName == raw->DxfName && copyRaw->Dwg.Main == raw->Dwg.Main && copyRaw->Dwg.MainBits == raw->Dwg.MainBits
            && copyRaw->Dwg.Handles == raw->Dwg.Handles && copyRaw->Dwg.Text == raw->Dwg.Text)
            ++same;
        else
            std::printf("PRESERVE MISMATCH %llx %s\n", static_cast<unsigned long long>(handle), raw->DxfName.c_str());
    }
    MiniCAD::Scene reopened;
    const bool reimported = MiniCAD::ImportCad(savedBytes, reopened);
    std::printf("PRESERVE bytes=%zu TCH=%d identical=%d classes=%zu reopened=%d entities=%d\n", savedBytes.size(), tch, same,
                saved->Classes.size(), reimported, reopened.EntityCount());
    if (same != tch || !reimported || reopened.EntityCount() != document.GetScene().EntityCount()) return 1;
    return loaded ? 0 : 1;
}

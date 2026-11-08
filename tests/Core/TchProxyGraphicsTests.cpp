#include "Import/TchProxyGraphics.h"
#include "Dwg/Write/DwgBitWriter.h"
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
    using Bytes = std::vector<std::uint8_t>;
    void U32(Bytes& data, std::uint32_t value)
    { for (int i = 0; i < 4; ++i) data.push_back(static_cast<std::uint8_t>(value >> (8 * i))); }
    void F64(Bytes& data, double value)
    {
        std::uint64_t bits; std::memcpy(&bits, &value, 8);
        for (int i = 0; i < 8; ++i) data.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    }
    void WritePoint(Bytes& data, double x, double y, double z = 0) { F64(data,x); F64(data,y); F64(data,z); }
    void Record(Bytes& data, std::uint32_t type, const Bytes& payload)
    { U32(data, static_cast<std::uint32_t>(payload.size() + 8)); U32(data,type); data.insert(data.end(),payload.begin(),payload.end()); }
    Bytes CircleBytes(double radius = 5)
    { Bytes data; WritePoint(data,10,20,3); F64(data,radius); WritePoint(data,0,0,1); return data; }
    Bytes Vertices(bool normal = false, bool nonplanar = false)
    {
        Bytes data; U32(data,3); WritePoint(data,0,0,3); WritePoint(data,10,0,3); WritePoint(data,10,10,nonplanar ? 4 : 3);
        if (normal) WritePoint(data,0,0,1);
        return data;
    }
}

int RunTchProxyGraphicsTests()
{
    using namespace MiniDWG;
    using namespace MiniCAD::Tch;
    int failures = 0;
    auto check = [&](bool okay, const char* message)
    { std::printf("[%s] ProxyGraphics: %s\n", okay ? "PASS" : "FAIL",message); failures += !okay; };
    UnknownEntity source; source.Raw.DxfName = "TCH_COLUMN";
    source.ProxyGraphics.assign(8,0);
    source.LayerHandle = 123; source.IsInvisible = true; source.MaterialHandle = 456;
    source.LineTypeHandle = 789; source.LineWeight = LineWeightType::W50;
    source.Color = Color(std::int16_t(3)); source.LineTypeScale = 2;
    TchProxyGraphics result; std::string error;
    check(!DecodeProxyGraphics(source,result,&error) && !error.empty(),"header-only proxy reports missing drawable geometry");
    Record(source.ProxyGraphics,2,CircleBytes());
    check(DecodeProxyGraphics(source,result,&error) && result.Entities.size() == 1,"circle record decodes");
    auto* circle = result.Entities.empty() ? nullptr : dynamic_cast<MiniDWG::Circle*>(result.Entities[0].get());
    check(circle && circle->Center == XYZ{10,20,3} && circle->Radius == 5,"circle center/radius/elevation are preserved");
    check(circle && circle->LayerHandle == 123 && circle->IsInvisible && circle->MaterialHandle == 456 &&
        circle->LineTypeHandle == 789 && circle->LineWeight == LineWeightType::W50 && circle->Color.Index() == 3 &&
        circle->LineTypeScale == 2,"all inherited appearance attributes remain intact");
    Bytes value; U32(value,0xc2123456); Record(source.ProxyGraphics,22,value);
    value.clear(); F64(value,3.5); Record(source.ProxyGraphics,24,value);
    value.clear(); U32(value,211); Record(source.ProxyGraphics,23,value);
    Record(source.ProxyGraphics,7,Vertices());
    check(DecodeProxyGraphics(source,result,&error) && result.Entities.size() == 2,"polygon and color/scale/weight state decode");
    auto* poly = result.Entities.size() == 2 ? dynamic_cast<LwPolyline*>(result.Entities[1].get()) : nullptr;
    check(poly && poly->Flags == LwPolylineFlags::Closed && poly->Elevation == 3 && poly->Vertices.size() == 3 &&
        poly->Color.TrueColor() == 0x123456 && poly->LineTypeScale == 3.5 && poly->LineWeight == LineWeightType::W211,
        "polygon closure, elevation and following-record attributes are correct");
    check(result.Entities.size() == 2 && result.Entities[0]->Color.Index() == 3,
        "later attributes do not mutate preceding primitives");
    auto good = source.ProxyGraphics;
    for (std::uint32_t transform : {27u,29u,31u})
    {
        source.ProxyGraphics = good; Record(source.ProxyGraphics,transform,{});
        check(!DecodeProxyGraphics(source,result,&error) && result.Entities.empty() && error.find(std::to_string(transform)) != std::string::npos,
            "clip / transform commands reject the whole preview with record type diagnostics");
    }
    for (std::uint32_t unsupported : {10u,16u,18u,999u})
    {
        source.ProxyGraphics = good; Record(source.ProxyGraphics,unsupported,{});
        Record(source.ProxyGraphics,2,CircleBytes());
        check(DecodeProxyGraphics(source,result,&error) && result.Entities.size() == 3 && result.Skipped == 1 &&
            error.find(std::to_string(unsupported)) != std::string::npos,
            "unsupported or malformed record is skipped, records around it still decode");
    }
    source.ProxyGraphics = good; source.ProxyGraphics.pop_back();
    check(!DecodeProxyGraphics(source,result,&error) && result.Entities.empty(),"truncated final record discards preceding shapes");
    source.ProxyGraphics.assign(8,0); U32(source.ProxyGraphics,0); U32(source.ProxyGraphics,2);
    check(!DecodeProxyGraphics(source,result,&error),"zero record size rejected");
    source.ProxyGraphics.assign(8,0); Record(source.ProxyGraphics,2,CircleBytes(std::numeric_limits<double>::quiet_NaN()));
    check(!DecodeProxyGraphics(source,result,&error),"NaN radius rejected");
    source.ProxyGraphics.assign(8,0); Record(source.ProxyGraphics,6,Vertices(false,true));
    check(!DecodeProxyGraphics(source,result,&error),"non-planar polyline rejected");
    source.ProxyGraphics.assign(8,0); Record(source.ProxyGraphics,32,Vertices(true));
    check(DecodeProxyGraphics(source,result,&error) && result.Entities.size() == 1,"polyline with explicit +Z normal accepted");
    source.ProxyGraphics.assign(8,0); Bytes count; U32(count,0xffffffff); Record(source.ProxyGraphics,6,count);
    check(!DecodeProxyGraphics(source,result,&error),"huge vertex count rejected before allocation");
    {
        Bytes wild; WritePoint(wild,1e70,1e70); F64(wild,5); WritePoint(wild,0,0,1);
        source.ProxyGraphics.assign(8,0); Record(source.ProxyGraphics,2,wild);
        check(!DecodeProxyGraphics(source,result,&error) && result.Entities.empty() && result.Skipped == 1,
            "finite but absurd circle center is skipped instead of entering the scene");
    }
    source.ProxyGraphics.assign(8,0); value.clear(); U32(value,1); Record(source.ProxyGraphics,20,value); Record(source.ProxyGraphics,7,Vertices());
    check(DecodeProxyGraphics(source,result,&error) && result.Entities.size() == 1 && dynamic_cast<Hatch*>(result.Entities[0].get()) &&
        static_cast<Hatch*>(result.Entities[0].get())->IsSolid,"filled polygon becomes a solid hatch");
    {
        Bytes shell; U32(shell,5);
        WritePoint(shell,0,0); WritePoint(shell,10,0); WritePoint(shell,10,10); WritePoint(shell,0,10); WritePoint(shell,5,5);
        U32(shell,(1 + 4) + (1 + 3)); U32(shell,4); U32(shell,0); U32(shell,1); U32(shell,2); U32(shell,3);
        U32(shell,static_cast<std::uint32_t>(-3)); U32(shell,0); U32(shell,1); U32(shell,4);
        U32(shell,0);   // 之后的边 / 面属性
        source.ProxyGraphics.assign(8,0); value.clear(); U32(value,1); Record(source.ProxyGraphics,20,value); Record(source.ProxyGraphics,9,shell);
        auto* h = DecodeProxyGraphics(source,result,&error) && result.Entities.size() == 1 ? dynamic_cast<Hatch*>(result.Entities[0].get()) : nullptr;
        check(h && h->Paths.size() == 2,"filled shell face keeps its hole loop");
        source.ProxyGraphics.assign(8,0); Record(source.ProxyGraphics,9,shell);
        check(DecodeProxyGraphics(source,result,&error) && result.Entities.size() == 2 && dynamic_cast<LwPolyline*>(result.Entities[0].get()),
            "unfilled shell draws face outlines");
    }
    {
        Bytes arc3; WritePoint(arc3,10,0); WritePoint(arc3,0,10); WritePoint(arc3,-10,0); U32(arc3,0);
        source.ProxyGraphics.assign(8,0); Record(source.ProxyGraphics,5,arc3);
        auto* a3 = DecodeProxyGraphics(source,result,&error) && result.Entities.size() == 1 ? dynamic_cast<Arc*>(result.Entities[0].get()) : nullptr;
        check(a3 && std::abs(a3->Radius - 10) < 1e-9 && std::abs(a3->StartAngle) < 1e-9 && std::abs(a3->EndAngle - kPi) < 1e-9,
            "three-point arc through the top half is counter-clockwise from 0 to pi");
        arc3.clear(); WritePoint(arc3,-10,0); WritePoint(arc3,0,10); WritePoint(arc3,10,0);
        source.ProxyGraphics.assign(8,0); Record(source.ProxyGraphics,5,arc3);
        a3 = DecodeProxyGraphics(source,result,&error) && result.Entities.size() == 1 ? dynamic_cast<Arc*>(result.Entities[0].get()) : nullptr;
        check(a3 && std::abs(a3->StartAngle) < 1e-9 && std::abs(a3->EndAngle - kPi) < 1e-9,"clockwise three-point arc covers the same span");
    }
    {
        auto padded = [](Bytes& data, std::string_view s) { data.insert(data.end(),s.begin(),s.end()); data.push_back(0); while (data.size() % 4) data.push_back(0); };
        Bytes text; WritePoint(text,1,2); WritePoint(text,0,0,1); WritePoint(text,0,1);
        padded(text,"\xd6\xd0"); U32(text,2); U32(text,0);
        F64(text,3.5); F64(text,0.7); F64(text,0); F64(text,0);
        for (int i = 0; i < 5; ++i) U32(text,0);
        padded(text,"simplex.shx"); padded(text,"gbcbig.shx");
        ProxyGraphicsContext context;
        context.Layers = {11,12,13}; context.LineTypes = {21,22}; context.ByLayerLineType = 21;
        context.StyleForFont = [](std::string_view font, std::string_view big) { return font == "simplex.shx" && big == "gbcbig.shx" ? Handle(77) : kNullHandle; };
        source.ProxyGraphics.assign(8,0);
        value.clear(); U32(value,2); Record(source.ProxyGraphics,16,value);
        value.clear(); U32(value,1); Record(source.ProxyGraphics,18,value);
        Record(source.ProxyGraphics,11,text);
        value.clear(); U32(value,32767); Record(source.ProxyGraphics,18,value);
        Record(source.ProxyGraphics,2,CircleBytes());
        const bool ok = DecodeProxyGraphics(source,result,&error,&context) && result.Entities.size() == 2;
        auto* t = ok ? dynamic_cast<TextEntity*>(result.Entities[0].get()) : nullptr;
        check(t && t->Value == "\xe4\xb8\xad" && t->Height == 3.5 && t->WidthFactor == 0.7 && std::abs(t->Rotation - kPi / 2) < 1e-12 &&
            t->StyleHandle == 77 && t->LayerHandle == 13 && t->LineTypeHandle == 22,"text2 decodes code page, size, direction, font style, layer and linetype");
        check(ok && result.Entities[1]->LayerHandle == 13 && result.Entities[1]->LineTypeHandle == 21,"linetype ByLayer index and layer persist to later records");
        source.ProxyGraphics.assign(8,0); value.clear(); U32(value,3); Record(source.ProxyGraphics,16,value); Record(source.ProxyGraphics,2,CircleBytes());
        check(DecodeProxyGraphics(source,result,&error,&context) && result.Skipped == 1 && result.Entities.size() == 1 &&
            result.Entities[0]->LayerHandle == source.LayerHandle,"layer index outside the table is skipped, layer stays unchanged");
        source.ProxyGraphics.assign(8,0);
        value.clear(); U32(value,2); Record(source.ProxyGraphics,16,value);
        value.clear(); U32(value,1); Record(source.ProxyGraphics,18,value);
        value.clear(); U32(value,0xFFFFFFFFu); Record(source.ProxyGraphics,16,value); Record(source.ProxyGraphics,18,value);
        Record(source.ProxyGraphics,2,CircleBytes());
        const bool reset = DecodeProxyGraphics(source,result,&error,&context) && result.Entities.size() == 1;
        check(reset && result.Entities[0]->LayerHandle == source.LayerHandle && result.Entities[0]->LineTypeHandle == source.LineTypeHandle,
            "layer and linetype index -1 fall back to the object's own");
    }
    source.ProxyGraphics.assign(8,0); value.clear(); U32(value,212); Record(source.ProxyGraphics,23,value); Record(source.ProxyGraphics,2,CircleBytes());
    check(DecodeProxyGraphics(source,result,&error) && result.Skipped == 1 && result.Entities.size() == 1 &&
        result.Entities[0]->LineWeight == LineWeightType::W50,"invalid lineweight is skipped, lineweight stays unchanged");
    {
        struct Case { std::uint32_t raw; bool trueColor; std::uint32_t expected; const char* message; };
        const Case cases[] = {
            {0x070000c3u, false, 7, "raw color [c3 00 00 07] is ACI 7"},
            {0xc3000007u, false, 7, "raw color [07 00 00 c3] (little-endian, method in the high byte) is ACI 7"},
            {0xc30000c1u, false, 193, "little-endian ACI 193 is not mistaken for ByBlock"},
            {0x000000c2u, true, 0x000000, "raw color [c2 00 00 00] is true-color black"},
            {0x563412c2u, true, 0x123456, "raw color [c2 12 34 56] is true color 0x123456"},
        };
        for (const auto& c : cases)
        {
            source.ProxyGraphics.assign(8,0); value.clear(); U32(value,c.raw); Record(source.ProxyGraphics,22,value);
            Record(source.ProxyGraphics,2,CircleBytes());
            const bool ok = DecodeProxyGraphics(source,result,&error) && result.Entities.size() == 1;
            const auto& color = ok ? result.Entities[0]->Color : Color();
            check(ok && (c.trueColor ? color.IsTrueColor() && color.TrueColor() == c.expected
                                     : !color.IsTrueColor() && color.Index() == static_cast<std::int16_t>(c.expected)), c.message);
        }
    }
    source.ProxyGraphics.assign(8,0); value.clear(); F64(value,250); Record(source.ProxyGraphics,25,value); Record(source.ProxyGraphics,2,CircleBytes());
    check(DecodeProxyGraphics(source,result,&error) && result.Skipped == 0 && result.Entities.size() == 1,"nonzero thickness is ignored for the plan view");
    {
        auto lwpolyline = [](std::int16_t flags, const std::vector<XY>& points, const std::vector<double>& bulges)
        {
            DwgBitWriter w(CadVersion::AC1024, Codec::CodePage::Windows1252);
            w.WriteBitShort(flags);
            if (flags & 0x8) w.WriteBitDouble(7.5);
            w.WriteBitLong(static_cast<std::int32_t>(points.size()));
            if (flags & 0x10) w.WriteBitLong(static_cast<std::int32_t>(bulges.size()));
            if (flags & 0x20) w.WriteBitLong(1);
            w.Write2RawDouble(points[0]);
            for (std::size_t i = 1; i < points.size(); ++i) w.Write2BitDoubleWithDefault(points[i - 1], points[i]);
            for (double b : bulges) w.WriteBitDouble(b);
            if (flags & 0x20) { w.WriteBitDouble(1.5); w.WriteBitDouble(2.5); }
            const auto& bits = w.Data();
            Bytes payload; U32(payload,static_cast<std::uint32_t>(bits.size()));
            payload.insert(payload.end(),bits.begin(),bits.end());
            while (payload.size() % 4) payload.push_back(0);
            return payload;
        };
        const std::vector<XY> pts{{-148976.25,43026.5},{-149096.25,42906.5},{-149216.25,43026.5}};
        source.ProxyGraphics.assign(8,0); Record(source.ProxyGraphics,33,lwpolyline(0x8 | 0x10 | 0x20,pts,{0,0.5,0}));
        const bool ok = DecodeProxyGraphics(source,result,&error) && result.Entities.size() == 1;
        auto* pl = ok ? dynamic_cast<LwPolyline*>(result.Entities[0].get()) : nullptr;
        check(pl && pl->Vertices.size() == 3 && pl->Vertices[2].Location.X == -149216.25 && pl->Vertices[1].Location.Y == 42906.5 &&
            pl->Vertices[1].Bulge == 0.5 && pl->Vertices[0].StartWidth == 1.5 && pl->Vertices[0].EndWidth == 2.5 && pl->Elevation == 7.5,
            "lwpolyline record decodes the DWG bit stream (points, bulges, widths, elevation)");
        source.ProxyGraphics.assign(8,0); value.clear(); U32(value,1); Record(source.ProxyGraphics,20,value);
        Record(source.ProxyGraphics,33,lwpolyline(0x200,pts,{}));
        check(DecodeProxyGraphics(source,result,&error) && result.Entities.size() == 1 && dynamic_cast<Hatch*>(result.Entities[0].get()),
            "filled closed lwpolyline becomes a solid hatch");
        source.ProxyGraphics.assign(8,0); Record(source.ProxyGraphics,33,lwpolyline(0,{{1e300,0},{0,0}},{}));
        check(!DecodeProxyGraphics(source,result,&error) && result.Skipped == 1,"lwpolyline with out-of-range coordinates is skipped");
    }
    source.ProxyGraphics.assign(8,0); Bytes arc = CircleBytes(); WritePoint(arc,0,1,0); F64(arc,kPi/2); Record(source.ProxyGraphics,4,arc);
    check(DecodeProxyGraphics(source,result,&error),"planar arc accepted");
    auto* a = result.Entities.empty() ? nullptr : dynamic_cast<Arc*>(result.Entities[0].get());
    check(a && std::abs(a->StartAngle-kPi/2) < 1e-12 && std::abs(a->EndAngle-kPi) < 1e-12,
        "arc start direction and sweep map to radians");
    check(source.Raw.DxfName == "TCH_COLUMN" && source.LayerHandle == 123 && source.IsInvisible,
        "original custom object remains unchanged");
    return failures;
}

#ifdef TCH_PROXY_TEST_MAIN
int main() { return RunTchProxyGraphicsTests(); }
#endif


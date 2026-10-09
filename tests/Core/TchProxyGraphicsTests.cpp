#include "Import/TchProxyGraphics.h"
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
    for (std::uint32_t unsupported : {29u,27u,10u,16u,18u,999u})
    {
        source.ProxyGraphics = good; Record(source.ProxyGraphics,unsupported,{});
        check(!DecodeProxyGraphics(source,result,&error) && result.Entities.empty() && error.find(std::to_string(unsupported)) != std::string::npos,
            "unsupported commands reject the whole preview with record type diagnostics");
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
    source.ProxyGraphics.assign(8,0); value.clear(); U32(value,1); Record(source.ProxyGraphics,20,value); Record(source.ProxyGraphics,7,Vertices());
    check(!DecodeProxyGraphics(source,result,&error),"filled polygons rejected instead of inventing outline-only display");
    source.ProxyGraphics.assign(8,0); value.clear(); U32(value,212); Record(source.ProxyGraphics,23,value); Record(source.ProxyGraphics,2,CircleBytes());
    check(!DecodeProxyGraphics(source,result,&error),"invalid lineweight rejected");
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


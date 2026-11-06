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
    {
        // 真实样本：句柄 368F5（SWR_TEXT 文字）的黑底白字。颜色记录 22 的 4 字节
        // 是 [method, r, g, b]，不是小端 u32：
        //   `c2 00 00 00` = 真彩 RGB(0,0,0) 黑  -> 背景框
        //   `c3 00 00 07` = ACI 7 白            -> 文字
        // 早先按 `value >> 24` 取 method 会得到 0，两个都落进 ByLayer 兜底，
        // 于是背景框被画成背景色（浅色主题下就是白块），把文字盖住了。
        source.ProxyGraphics.assign(8,0);
        value.clear(); U32(value,0x000000c2u); Record(source.ProxyGraphics,22,value);   // 黑
        Record(source.ProxyGraphics,2,CircleBytes());
        value.clear(); U32(value,0x070000c3u); Record(source.ProxyGraphics,22,value);   // ACI 7
        Record(source.ProxyGraphics,2,CircleBytes());
        check(DecodeProxyGraphics(source,result,&error) && result.Entities.size() == 2,
            "two colour-encoded records decode");
        check(result.Entities.size() == 2 && result.Entities[0]->Color.IsTrueColor() &&
            result.Entities[0]->Color.TrueColor() == 0x000000,
            "colour record [c2 00 00 00] is true colour black (the text backdrop frame)");
        check(result.Entities.size() == 2 && result.Entities[1]->Color.Index() == 7,
            "colour record [c3 00 00 07] is ACI 7 (the white text)");
    }
    auto good = source.ProxyGraphics;
    // 未知记录不再让整份放弃：跳过该条，前面的几何照常输出。
    // 以前是「整份失败」，第三方对象只要撞上一条不认识的记录就完全不显示。
    for (std::uint32_t unsupported : {29u, 27u, 10u, 16u, 18u, 999u})
    {
        source.ProxyGraphics = good; Record(source.ProxyGraphics,unsupported,{});
        const bool ok = DecodeProxyGraphics(source,result,&error);
        check(ok && !result.Entities.empty() && result.Skipped >= 1,
            "unsupported record is skipped and earlier geometry survives");
    }
    // 尾部截断：只丢掉坏掉的那条，已解出的几何保留
    source.ProxyGraphics = good; source.ProxyGraphics.pop_back();
    check(DecodeProxyGraphics(source,result,&error) && !result.Entities.empty(),
        "truncated final record keeps the shapes decoded before it");
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
    // 坐标超出合理范围：丢弃该图元，但不影响整份（避免脏数据把包围盒撑爆）
    source.ProxyGraphics.assign(8,0);
    {
        // type 2 圆 = 圆心(3×f64) + 半径(f64) + 法向(3×f64)，共 56 字节
        Bytes wild; const double huge = 1e70;   // 有限但荒谬：以前会被收下，视图「全部显示」时整张图缩成一点
        F64(wild, huge); F64(wild, huge); F64(wild, 0.0);   // 圆心
        F64(wild, 5.0);                                     // 半径
        F64(wild, 0.0); F64(wild, 0.0); F64(wild, 1.0);     // 法向 +Z
        Record(source.ProxyGraphics, 2, wild);
    }
    check(!DecodeProxyGraphics(source,result,&error) && result.Entities.empty() && result.Discarded == 1,
        "coordinates beyond the sane limit are discarded instead of entering the scene");
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
        // fill 时既出实心填充、也出轮廓线（只出填充会得到一个
        // 没有边框的纯色块，把文字类的背景框渲染成"白色块状"）
        const bool filled = DecodeProxyGraphics(source,result,&error);
        auto* h = filled ? dynamic_cast<Hatch*>(result.Entities[0].get()) : nullptr;
        check(h && h->Paths.size() == 2,"filled shell face keeps its hole loop");
        check(filled && result.Entities.size() >= 3 && dynamic_cast<LwPolyline*>(result.Entities.back().get()),
            "filled shell also draws its outline (so a text backdrop is not a bare colour block)");
        source.ProxyGraphics.assign(8,0); Record(source.ProxyGraphics,9,shell);
        check(DecodeProxyGraphics(source,result,&error) && result.Entities.size() == 2 && dynamic_cast<LwPolyline*>(result.Entities[0].get()),
            "unfilled shell draws face outlines");
    }
    {
        // type 33 = LWPOLYLINE：位字段编码，**MSB-first**（先取字节最高位）。
        // 采样自句柄 1863E6：标高标注对象的实心倒三角箭头。
        // 该对象共 10 条记录，以前这一条不支持导致整个对象不显示；修好容错后只解出文字和横线、缺三角箭头，
        // 根因是位序用了 LSB-first（flags 解成 0x1FE4 这类垃圾值）。
        static const std::uint8_t kLwPolyline[] = {
            0x43,0x00,0x00,0x00,0x90,0x7f,0x84,0x50,0xc9,0x78,0x22,0xf0,0x2c,0x10,0x09,0x1d,
            0xd9,0xb4,0x70,0x2e,0x54,0x08,0x08,0xa3,0xe1,0x14,0x32,0x5c,0xa0,0x8b,0xfe,0x11,
            0x43,0x25,0xf0,0x09,0x1d,0xd9,0xb4,0x7e,0x4e,0x44,0x08,0x08,0xdf,0xe1,0x14,0x32,
            0x5f,0x00,0x91,0xdd,0x9b,0x47,0x02,0xe5,0x40,0xa0,0x8b,0xfe,0x11,0x43,0x25,0xc0,
            0x84,0x02,0x1d,0x02,0xd9,0x04,0xc0,0x00,0x00,0x00
        };
        Bytes payload33(std::begin(kLwPolyline), std::end(kLwPolyline));
        source.ProxyGraphics.assign(8,0);
        Record(source.ProxyGraphics,33,payload33);
        const bool decoded33 = DecodeProxyGraphics(source,result,&error);
        auto* lwp = decoded33 && result.Entities.size() == 1 ? dynamic_cast<LwPolyline*>(result.Entities[0].get()) : nullptr;
        check(lwp != nullptr,"type 33 LWPOLYLINE record decodes into a polyline");
        check(lwp && lwp->Vertices.size() == 7,"type 33 polyline keeps its 7 vertices");
        // 坐标必须落在该标注所在区域。只用 isfinite 挡不住「有限但荒谬」的错位解码，
        // 那正是之前三角箭头画不出来、甚至一度把全图包围盒撑爆的原因。
        check(lwp && std::abs(lwp->Vertices[0].Location.X - (-148976.323754)) < 0.01 &&
            std::abs(lwp->Vertices[0].Location.Y - 43026.237777) < 0.01,
            "type 33 vertex coordinates match the drawing (MSB-first bit order)");
        // 倒三角：右上角 (-148736,-43026) 与下方尖点 y=42786，说明不是一条退化直线
        check(lwp && lwp->Vertices.size() == 7 &&
            std::abs(lwp->Vertices[1].Location.X - (-148736.323754)) < 0.01 &&
            std::abs(lwp->Vertices[2].Location.Y - 42786.237777) < 0.01,
            "type 33 decodes the triangle outline, not a degenerate line");
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
        // 图层序号越界：这条状态记录跳过，后面的圆仍按对象自身图层画出来
        check(DecodeProxyGraphics(source,result,&error,&context) && result.Entities.size() == 1 &&
            result.Entities[0]->LayerHandle == source.LayerHandle && result.Skipped >= 1,
            "out-of-range layer index is skipped, geometry still decoded");
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
    // 线宽值不认识：跳过该条状态记录，圆照常输出（线宽沿用对象自身的）
    check(DecodeProxyGraphics(source,result,&error) && result.Entities.size() == 1 && result.Skipped >= 1,
        "invalid lineweight record is skipped, geometry still decoded");
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


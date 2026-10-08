// 序列化便捷函数:数学类型 / 实体公共属性 / 几何内核结构 / 标注样式。
// 全部为读写合一(按 ISerializer::IsLoading 分流),供 EntityIO 与各表使用。
#pragma once
#include "ISerializer.h"
#include "Core/Math/Point3.hpp"
#include "Core/Math/Vec3.hpp"
#include "Core/Math/Color4.hpp"
#include "Core/Entity/EntityAttr.hpp"
#include "Core/Entity/DimensionEntity.hpp"   // DimStyle
#include "Core/Entity/MLeaderEntity.hpp"     // MLeaderStyle
#include "Core/GeomKernel/Polyline.hpp"
#include "Core/GeomKernel/Spline.hpp"
#include "Core/GeomKernel/Ellipse.hpp"
#include <type_traits>
#include <vector>

namespace MiniCAD::Ser
{
    // ── 数学类型(写为 [x,y,z] / [r,g,b,a] 数组)──────────────────────────
    inline void Value(ISerializer& s, const char* key, Math::Point3& p)
    {
        std::vector<double> v{ p.x, p.y, p.z };
        s.Value(key, v);
        if (s.IsLoading() && v.size() >= 3) { p.x = v[0]; p.y = v[1]; p.z = v[2]; }
    }

    inline void Value(ISerializer& s, const char* key, Math::Vec3& p)
    {
        std::vector<double> v{ p.x, p.y, p.z };
        s.Value(key, v);
        if (s.IsLoading() && v.size() >= 3) { p.x = v[0]; p.y = v[1]; p.z = v[2]; }
    }

    inline void Value(ISerializer& s, const char* key, Math::Color4& c)
    {
        std::vector<double> v{ c.r, c.g, c.b, c.a };
        s.Value(key, v);
        if (s.IsLoading() && v.size() >= 4) { c.r = v[0]; c.g = v[1]; c.b = v[2]; c.a = v[3]; }
    }

    // 点列写为扁平 [x0,y0,z0, x1,y1,z1, ...]。
    inline void Value(ISerializer& s, const char* key, std::vector<Math::Point3>& pts)
    {
        std::vector<double> flat;
        if (!s.IsLoading())
        {
            flat.reserve(pts.size() * 3);
            for (const auto& p : pts) { flat.push_back(p.x); flat.push_back(p.y); flat.push_back(p.z); }
        }
        s.Value(key, flat);
        if (s.IsLoading())
        {
            pts.clear();
            pts.reserve(flat.size() / 3);
            for (size_t i = 0; i + 2 < flat.size(); i += 3)
                pts.push_back({ flat[i], flat[i + 1], flat[i + 2] });
        }
    }

    // 枚举按底层整型读写。
    template<typename E>
    inline void Enum(ISerializer& s, const char* key, E& e)
    {
        static_assert(std::is_enum_v<E>);
        int32_t v = static_cast<int32_t>(static_cast<std::underlying_type_t<E>>(e));
        s.Value(key, v);
        if (s.IsLoading()) e = static_cast<E>(v);
    }

    // ── 实体公共属性 EntityAttr(在当前作用域的 "attr" 子对象内)────────────
    // 写出端默认值不写(读取端字段缺失即保持默认构造值,见 TreeSerializer::Value)。
    // 几万个全默认(ByLayer)实体的文档里,attr 不再逐实体重复占空间;
    // 全部默认且无 XDATA 时连 "attr" 对象本身都省去。
    inline void Attr(ISerializer& s, EntityAttr& a)
    {
        static const EntityAttr kDef{};
        const bool ld = s.IsLoading();

        auto sameColor = [](const Math::Color4& x, const Math::Color4& y)
        { return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a; };

        const bool defColorMethod = a.Color.Method == kDef.Color.Method;
        const bool defAci         = a.Color.Aci == kDef.Color.Aci;
        const bool defRgba        = sameColor(a.Color.Rgba, kDef.Color.Rgba);
        const bool defLayer       = a.LayerId == kDef.LayerId;
        const bool defLineType    = a.LineType == kDef.LineType;
        const bool defLtScale     = a.LinetypeScale == kDef.LinetypeScale;
        const bool defLw          = a.Lineweight == kDef.Lineweight;
        const bool defTrans       = a.Transparency.ToRaw() == kDef.Transparency.ToRaw();
        const bool defExtrusion   = a.Extrusion.x == kDef.Extrusion.x &&
                                    a.Extrusion.y == kDef.Extrusion.y &&
                                    a.Extrusion.z == kDef.Extrusion.z;
        const bool defVisible     = a.Visible == kDef.Visible;

        if (!ld)
        {
            const bool allDefault = defColorMethod && defAci && defRgba && defLayer &&
                                    defLineType && defLtScale && defLw && defTrans &&
                                    defExtrusion && defVisible && a.XData.Empty();
            if (allDefault) return;
        }

        if (!s.BeginObject("attr")) return;

        if (ld || !defColorMethod) Enum(s, "colorMethod", a.Color.Method);
        if (ld || !defAci)
        {
            uint32_t aci = a.Color.Aci; s.Value("aci", aci);
            if (ld) a.Color.Aci = static_cast<uint16_t>(aci);
        }
        if (ld || !defRgba)     Value(s, "rgba", a.Color.Rgba);

        if (ld || !defLayer)    s.Value("layer", a.LayerId);
        if (ld || !defLineType) s.Value("lineType", a.LineType);
        if (ld || !defLtScale)  s.Value("ltScale", a.LinetypeScale);

        if (ld || !defLw)
        {
            int32_t lw = static_cast<int32_t>(LineweightToRaw(a.Lineweight));
            s.Value("lineweight", lw);
            if (ld) a.Lineweight = LineweightFromRaw(static_cast<int16_t>(lw));
        }

        if (ld || !defTrans)
        {
            int32_t trans = a.Transparency.ToRaw();
            s.Value("transparency", trans);
            if (ld) a.Transparency = Transparency::FromRaw(trans);
        }

        if (ld || !defExtrusion) Value(s, "extrusion", a.Extrusion);
        if (ld || !defVisible)   s.Value("visible", a.Visible);

        // XDATA:仅在非空时写出,保证常见文档紧凑。
        if (!s.IsLoading() && a.XData.Empty()) { s.EndObject(); return; }
        if (s.IsLoading() && !s.Has("xdata")) { s.EndObject(); return; }
        if (s.BeginObject("xdata"))
        {
            size_t nApp = a.XData.XData.size();
            if (s.BeginArray("apps", nApp))
            {
                if (s.IsLoading()) a.XData.XData.resize(nApp);
                for (size_t i = 0; i < nApp; ++i)
                {
                    if (!s.BeginElement(i)) continue;
                    XDataApp& app = a.XData.XData[i];
                    s.Value("name", app.AppName);
                    size_t nItem = app.Items.size();
                    if (s.BeginArray("items", nItem))
                    {
                        if (s.IsLoading()) app.Items.resize(nItem);
                        for (size_t k = 0; k < nItem; ++k)
                        {
                            if (!s.BeginElement(k)) continue;
                            XDataItem& it = app.Items[k];
                            int32_t code = it.Code; s.Value("code", code);
                            if (s.IsLoading()) it.Code = static_cast<int16_t>(code);
                            s.Value("isNum", it.IsNum);
                            s.Value("num", it.Num);
                            s.Value("str", it.Str);
                            s.EndElement();
                        }
                        s.EndArray();
                    }
                    s.EndElement();
                }
                s.EndArray();
            }

            size_t nDict = a.XData.ExtDict.size();
            if (s.BeginArray("extDict", nDict))
            {
                if (s.IsLoading()) a.XData.ExtDict.resize(nDict);
                for (size_t i = 0; i < nDict; ++i)
                {
                    if (!s.BeginElement(i)) continue;
                    int32_t code = a.XData.ExtDict[i].first; s.Value("code", code);
                    if (s.IsLoading()) a.XData.ExtDict[i].first = static_cast<int16_t>(code);
                    s.Value("value", a.XData.ExtDict[i].second);
                    s.EndElement();
                }
                s.EndArray();
            }

            size_t nReact = a.XData.Reactors.size();
            if (s.BeginArray("reactors", nReact))
            {
                if (s.IsLoading()) a.XData.Reactors.resize(nReact);
                for (size_t i = 0; i < nReact; ++i)
                {
                    if (!s.BeginElement(i)) continue;
                    s.Value("handle", a.XData.Reactors[i]);
                    s.EndElement();
                }
                s.EndArray();
            }
            s.EndObject();
        }
        s.EndObject();
    }

    // ── 几何内核结构 ─────────────────────────────────────────────────────
    inline void PolylineGeom(ISerializer& s, const char* key, Polyline& pl)
    {
        if (!s.BeginObject(key)) return;
        Value(s, "points", pl.Points);
        s.Value("bulges", pl.Bulges);
        s.EndObject();
        if (s.IsLoading())
        {
            // Bulges 与段数对齐(缺失补 0,多余截断),防御手工编辑过的文件。
            const size_t segs = pl.Points.size() > 1 ? pl.Points.size() - 1 : 0;
            pl.Bulges.resize(segs, 0.0);
        }
    }

    inline void SplineGeom(ISerializer& s, const char* key, Spline& sp)
    {
        if (!s.BeginObject(key)) return;
        Value(s, "fitPoints", sp.FitPoints);
        Enum(s, "boundary", sp.Boundary);
        Value(s, "startTangent", sp.StartTangent);
        Value(s, "endTangent", sp.EndTangent);
        s.Value("degree", sp.Degree);
        s.Value("flags", sp.Flags);
        Value(s, "controlPoints", sp.ControlPoints);
        s.Value("weights", sp.Weights);
        s.Value("knots", sp.Knots);
        s.Value("fitTolerance", sp.FitTolerance);
        s.Value("knotTolerance", sp.KnotTolerance);
        s.Value("ctrlTolerance", sp.CtrlTolerance);
        s.EndObject();
        if (s.IsLoading() && sp.FitPoints.size() >= 2)
            sp.Build();   // 重建插值段(Segments/Params 不入档)
    }

    inline void EllipseGeom(ISerializer& s, const char* key, Ellipse& el)
    {
        if (!s.BeginObject(key)) return;
        Value(s, "center", el.Center);
        s.Value("rx", el.RadiusX);
        s.Value("ry", el.RadiusY);
        s.Value("rotation", el.Rotation);
        s.EndObject();
    }

    // ── 标注 / 多重引线样式 ──────────────────────────────────────────────
    inline void DimStyleObj(ISerializer& s, const char* key, DimStyle& st)
    {
        if (!s.BeginObject(key)) return;
        s.Value("textHeight", st.TextHeight);
        s.Value("arrowSize", st.ArrowSize);
        s.Value("extLineExtend", st.ExtLineExtend);
        s.Value("extLineOffset", st.ExtLineOffset);
        s.Value("textGap", st.TextGap);
        s.Value("baselineSpacing", st.BaselineSpacing);
        s.Value("precision", st.Precision);
        s.Value("anglePrecision", st.AnglePrecision);
        Enum(s, "terminator", st.Terminator);
        s.Value("textStyle", st.TextStyle);
        s.EndObject();
    }

    inline void MLeaderStyleObj(ISerializer& s, const char* key, MLeaderStyle& st)
    {
        if (!s.BeginObject(key)) return;
        s.Value("arrowSize", st.ArrowSize);
        s.Value("landingGap", st.LandingGap);
        s.Value("doglegLength", st.DoglegLength);
        s.Value("textHeight", st.TextHeight);
        s.Value("textStyle", st.TextStyle);
        s.Value("enableLanding", st.EnableLanding);
        s.Value("enableDogleg", st.EnableDogleg);
        s.EndObject();
    }
}

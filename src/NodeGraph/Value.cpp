#include "NodeGraph/Value.h"
#include "NodeGraph/GeometryOps.h"
#include "Core/Entity/Entity.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstdlib>

namespace MiniCAD::Nodes
{
    DataType Value::Type() const
    {
        switch (Data.index())
        {
        case 1: return DataType::Number;
        case 2: return DataType::Integer;
        case 3: return DataType::Boolean;
        case 4: return DataType::Text;
        case 5: return DataType::Vector;
        case 6: return DataType::Curve;
        case 7: return DataType::Body;
        case 8: return DataType::Curve3D;
        case 9: return DataType::Domain;
        case 10: return DataType::Plane;
        case 11: return DataType::Mesh;
        default: return DataType::Any;
        }
    }

    namespace
    {
        Math::Vec3 CrossV(const Math::Vec3& a, const Math::Vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
        double     DotV(const Math::Vec3& a, const Math::Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    }

    Math::Vec3 Plane::Normal() const { return CrossV(X, Y); }

    Math::Vec3 Plane::Coordinates(const Math::Vec3& p) const
    {
        const Math::Vec3 d = p - Origin;
        return { DotV(d, X), DotV(d, Y), DotV(d, Normal()) };
    }

    Math::Mat4 Plane::ToWorld() const
    {
        const Math::Vec3 z = Normal();
        Math::Mat4 m = Math::Mat4::Identity();
        const Math::Vec3 rows[4] = { X, Y, z, Origin };
        for (int r = 0; r < 4; ++r)
        {
            m.m[r * 4 + 0] = rows[r].x;
            m.m[r * 4 + 1] = rows[r].y;
            m.m[r * 4 + 2] = rows[r].z;
        }
        return m;
    }

    std::optional<Plane> Plane::FromNormal(const Math::Vec3& origin, const Math::Vec3& normal, const Math::Vec3& xHint)
    {
        const Math::Vec3 z = normal.Normalized();
        if (z.LengthSq() < 0.5)
            return std::nullopt;
        Math::Vec3 x = xHint - z * DotV(xHint, z);   // 投影到平面内
        if (x.Length() < 1e-9)
        {
            // 法向接近 Z 时取世界 X，否则取 Z × 法向（水平方向），与 AutoCAD 的任意轴算法思路一致
            x = std::abs(z.z) > 0.9 ? Math::Vec3{ 1, 0, 0 } - z * z.x : CrossV({ 0, 0, 1 }, z);
        }
        x = x.Normalized();
        return Plane{ origin, x, CrossV(z, x) };
    }

    std::optional<Plane> Plane::FromAxes(const Math::Vec3& origin, const Math::Vec3& x, const Math::Vec3& yHint)
    {
        const Math::Vec3 xa = x.Normalized();
        if (xa.LengthSq() < 0.5)
            return std::nullopt;
        const Math::Vec3 z = CrossV(xa, yHint);
        if (z.Length() < 1e-9 * std::max(1.0, yHint.Length()))
            return std::nullopt;
        const Math::Vec3 za = z.Normalized();
        return Plane{ origin, xa, CrossV(za, xa) };
    }

    namespace
    {
        struct TypeRow { DataType Type; const char* Key; const char* Name; };
        constexpr TypeRow kTypes[] = {
            { DataType::Any,      "any",      NN_("任意") },
            { DataType::Number,   "number",   NN_("数值") },
            { DataType::Integer,  "integer",  NN_("整数") },
            { DataType::Boolean,  "boolean",  NN_("布尔") },
            { DataType::Text,     "text",     NN_("文字") },
            { DataType::Vector,   "vector",   NN_("点 / 向量") },
            { DataType::Curve,    "curve",    NN_("曲线") },
            { DataType::Body,     "body",     NN_("实体 / 曲面") },
            { DataType::Curve3D,  "curve3d",  NN_("空间曲线") },
            { DataType::Geometry, "geometry", NN_("几何") },
            { DataType::Domain,   "domain",   NN_("区间") },
            { DataType::Plane,    "plane",    NN_("平面") },
            { DataType::Mesh,     "mesh",     NN_("网格") },
        };

        bool IsScalar(DataType t) { return t == DataType::Number || t == DataType::Integer || t == DataType::Boolean; }

        bool ParseNumber(const std::string& s, double& out)
        {
            if (s.empty())
                return false;
            char* end = nullptr;
            out = std::strtod(s.c_str(), &end);
            while (end && (*end == ' ' || *end == '\t'))
                ++end;
            return end && *end == '\0' && std::isfinite(out);
        }

        std::string FormatNumber(double v)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.6g", v);
            return buf;
        }
    }

    std::string TrNode(std::string_view text)
    {
        if (text.find('\n') == std::string_view::npos)
            return TrC(kNodeTextContext, text);
        std::string out;
        for (size_t pos = 0;;)
        {
            const size_t end = text.find('\n', pos);
            out += TrC(kNodeTextContext, text.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos));
            if (end == std::string_view::npos)
                return out;
            out += '\n';
            pos = end + 1;
        }
    }

    std::string TypeName(DataType t)
    {
        for (const auto& r : kTypes)
            if (r.Type == t) return TrNode(r.Name);
        return "?";
    }

    const char* TypeKey(DataType t)
    {
        for (const auto& r : kTypes)
            if (r.Type == t) return r.Key;
        return "any";
    }

    bool TypeFromKey(const std::string& key, DataType& out)
    {
        for (const auto& r : kTypes)
            if (key == r.Key) { out = r.Type; return true; }
        return false;
    }

    bool CanConvert(DataType from, DataType to)
    {
        if (from == to || to == DataType::Any || from == DataType::Any || to == DataType::Text)
            return true;
        if (IsScalar(from) && IsScalar(to))
            return true;
        if (from == DataType::Text && IsScalar(to))
            return true;                                // 解析，失败时求值报错
        if (from == DataType::Curve && to == DataType::Curve3D)
            return true;                                // 曲线实体 → NURBS（精确表示圆弧），不能转换的求值时报错
        if (from == DataType::Body && to == DataType::Mesh)
            return true;                                // 按默认精度网格化：需要内核，由 Graph 求值时处理
        if (IsScalar(from) && to == DataType::Domain)
            return true;                                // x → [0, x]
        if ((from == DataType::Plane && to == DataType::Vector) || (from == DataType::Vector && to == DataType::Plane))
            return true;                                // 平面的原点 / 过该点的 XY 平面
        if (to == DataType::Geometry)
            return IsGeometryType(from);
        if (from == DataType::Geometry)
            return IsGeometryType(to);                  // 求值时逐项检查
        return false;
    }

    bool Convert(const Value& v, DataType to, Value& out)
    {
        const DataType from = v.Type();
        if (from == DataType::Any)
            return false;
        if (to == DataType::Any || from == to)
        {
            out = v;
            return true;
        }
        if (to == DataType::Geometry)
        {
            if (!IsGeometryType(from))
                return false;
            out = v;
            return true;
        }
        if (to == DataType::Text)
        {
            out = Value(ToDisplayString(v));
            return true;
        }
        if (from == DataType::Curve && to == DataType::Curve3D)
        {
            const auto nurbs = v.AsCurve() ? Geo::NurbsOf(*v.AsCurve()) : std::nullopt;
            if (!nurbs)
                return false;
            out = Value(*nurbs);
            return true;
        }

        if (from == DataType::Plane && to == DataType::Vector)
        {
            out = Value(v.AsPlane().Origin);
            return true;
        }
        if (from == DataType::Vector && to == DataType::Plane)
        {
            out = Value(Plane{ v.AsVector() });
            return true;
        }

        double num = 0.0;
        bool   haveNum = true;
        switch (from)
        {
        case DataType::Number:  num = v.AsNumber(); break;
        case DataType::Integer: num = static_cast<double>(v.AsInteger()); break;
        case DataType::Boolean: num = v.AsBool() ? 1.0 : 0.0; break;
        case DataType::Text:
        {
            const std::string& s = v.AsText();
            if (to == DataType::Boolean && (s == "true" || s == "True" || s == "TRUE")) { out = Value(true); return true; }
            if (to == DataType::Boolean && (s == "false" || s == "False" || s == "FALSE")) { out = Value(false); return true; }
            haveNum = ParseNumber(s, num);
            break;
        }
        default: haveNum = false; break;
        }
        if (!haveNum)
            return false;
        switch (to)
        {
        case DataType::Number:  out = Value(num); return true;
        case DataType::Integer: out = Value(static_cast<int64_t>(std::llround(num))); return true;
        case DataType::Boolean: out = Value(num != 0.0); return true;
        case DataType::Domain:  out = Value(Domain{ 0.0, num }); return true;
        default: return false;
        }
    }

    std::string PlaneName(const Plane& pl)
    {
        const Math::Vec3 n = pl.Normal();
        const char* name = std::abs(n.z) > 1 - 1e-9 && std::abs(pl.X.x) > 1 - 1e-9 ? NN_("XY 平面")
                         : std::abs(n.x) > 1 - 1e-9 && std::abs(pl.X.y) > 1 - 1e-9 ? NN_("YZ 平面")
                         : std::abs(n.y) > 1 - 1e-9 && std::abs(pl.X.x) > 1 - 1e-9 ? NN_("XZ 平面") : NN_("平面");
        return TrNode(name);
    }

    std::string ToDisplayString(const Value& v)
    {
        switch (v.Type())
        {
        case DataType::Number:  return FormatNumber(v.AsNumber());
        case DataType::Integer: return std::to_string(v.AsInteger());
        case DataType::Boolean: return v.AsBool() ? "True" : "False";
        case DataType::Text:    return v.AsText();
        case DataType::Vector:
        {
            const auto& p = v.AsVector();
            return "{" + FormatNumber(p.x) + ", " + FormatNumber(p.y) + ", " + FormatNumber(p.z) + "}";
        }
        case DataType::Curve:
        {
            const auto& c = v.AsCurve();
            if (!c)
                return Tr("曲线（空）");
            static const std::pair<const char*, const char*> kNames[] = {
                { "LineEntity", NN_("直线") }, { "ArcEntity", NN_("圆弧") }, { "CircleEntity", NN_("圆") },
                { "PolylineEntity", NN_("多段线") }, { "RectangleEntity", NN_("矩形") }, { "SplineEntity", NN_("样条") },
                { "EllipseEntity", NN_("椭圆") },
            };
            const std::string type = c->GetTypeInfo()->Name;
            for (const auto& [key, name] : kNames)
                if (type == key) return TrF("曲线（{0}）", TrNode(name));
            return TrF("曲线（{0}）", type);
        }
        case DataType::Body:    return !v.AsBody() ? Tr("实体 / 曲面（空）") : v.Void ? Tr("空心形体") : TrNode("实体 / 曲面");
        case DataType::Domain:  return TrF("{0} 至 {1}", FormatNumber(v.AsDomain().Min), FormatNumber(v.AsDomain().Max));
        case DataType::Plane:
        {
            const Plane& pl = v.AsPlane();
            auto vec = [](const Math::Vec3& p) { return "{" + FormatNumber(p.x) + ", " + FormatNumber(p.y) + ", " + FormatNumber(p.z) + "}"; };
            return TrF("{0}（原点 {1}，法向 {2}）", PlaneName(pl), vec(pl.Origin), vec(pl.Normal()));
        }
        case DataType::Mesh:
        {
            const auto& m = v.AsMesh();
            if (!m)
                return Tr("网格（空）");
            return TrF("网格（{0} 个顶点，{1} 个面）", m->Positions.size(), m->TriangleCount());
        }
        case DataType::Curve3D:
        {
            const auto& c = v.AsCurve3D();
            if (!c || !c->IsValid())
                return Tr("空间曲线（无效）");
            return c->IsClosed() ? TrF("空间曲线（{0} 次，{1} 个控制点，闭合）", c->Degree, c->ControlPoints.size())
                                 : TrF("空间曲线（{0} 次，{1} 个控制点）", c->Degree, c->ControlPoints.size());
        }
        default:                return Tr("<空>");
        }
    }
}

#pragma once
#include "Database/UnknownObjects.h"
#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace MiniCAD::Tch
{
    struct TchProxyGraphics
    {
        std::vector<std::unique_ptr<MiniDWG::Entity>> Entities;
    };

    namespace ProxyDetail
    {
        struct Reader
        {
            std::span<const std::uint8_t> Bytes;
            std::size_t Position = 0;
            bool Good = true;
            std::uint32_t Long()
            {
                if (Bytes.size() - Position < 4) { Good = false; return 0; }
                std::uint32_t value = 0;
                for (int i = 0; i < 4; ++i) value |= std::uint32_t(Bytes[Position++]) << (8 * i);
                return value;
            }
            double Double()
            {
                if (Bytes.size() - Position < 8) { Good = false; return 0; }
                std::uint64_t bits = 0;
                for (int i = 0; i < 8; ++i) bits |= std::uint64_t(Bytes[Position++]) << (8 * i);
                double value;
                std::memcpy(&value, &bits, sizeof(value));
                if (!std::isfinite(value)) Good = false;
                return value;
            }
            MiniDWG::XYZ Point() { const double x = Double(), y = Double(), z = Double(); return {x, y, z}; }
            bool Done() const { return Good && Position == Bytes.size(); }
        };

        inline bool AxisZ(const MiniDWG::XYZ& normal)
        {
            return std::abs(normal.X) < 1e-9 && std::abs(normal.Y) < 1e-9 && std::abs(normal.Z - 1) < 1e-9;
        }
        inline void CopyAppearance(const MiniDWG::Entity& source, MiniDWG::Entity& destination)
        {
            destination.BookColorHandle = source.BookColorHandle;
            destination.Color = source.Color;
            destination.IsInvisible = source.IsInvisible;
            destination.LayerHandle = source.LayerHandle;
            destination.LineTypeHandle = source.LineTypeHandle;
            destination.LineTypeScale = source.LineTypeScale;
            destination.LineWeight = source.LineWeight;
            destination.MaterialHandle = source.MaterialHandle;
            destination.Transparency = source.Transparency;
        }
    }

    // Independent bounded implementation of the documented proxy record layouts.
    // Format references and supported subset: docs/Tch-proxy-graphics.md.
    // No custom TCH field schema is assumed; original UnknownEntity remains intact.
    inline bool DecodeProxyGraphics(const MiniDWG::UnknownEntity& source, TchProxyGraphics& output,
                                    std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        output.Entities.clear();
        if (diagnostic) diagnostic->clear();
        auto fail = [&](const std::string& reason)
        {
            if (diagnostic) *diagnostic = reason;
            return false;
        };
        const auto& bytes = source.ProxyGraphics;
        if (bytes.size() < 8) return fail("proxy header is shorter than 8 bytes");
        if (bytes.size() > 32 * 1024 * 1024) return fail("proxy exceeds 32 MiB compatibility limit");
        TchProxyGraphics parsed;
        Color color = source.Color;
        double lineTypeScale = source.LineTypeScale;
        LineWeightType lineWeight = source.LineWeight;
        if (!std::isfinite(lineTypeScale) || lineTypeScale <= 0) return fail("invalid inherited linetype scale");
        std::size_t offset = 8, records = 0, totalVertices = 0;
        while (offset < bytes.size())
        {
            if (++records > 100000) return fail("proxy record limit exceeded");
            if (bytes.size() - offset < 8) return fail("truncated proxy record header at " + std::to_string(offset));
            ProxyDetail::Reader header{std::span(bytes).subspan(offset, 8)};
            const auto size = header.Long(), type = header.Long();
            const std::string where = "proxy type " + std::to_string(type) + " at " + std::to_string(offset) + ": ";
            if (size < 8 || size > bytes.size() - offset) return fail(where + "invalid record size");
            ProxyDetail::Reader r{std::span(bytes).subspan(offset + 8, size - 8)};
            std::unique_ptr<Entity> primitive;
            switch (type)
            {
            case 1: // Extents are metadata, not a drawing command.
                if (r.Bytes.size() != 48) return fail(where + "invalid extents size");
                r.Point(); r.Point();
                break;
            case 2:
            case 4:
            {
                if (r.Bytes.size() != (type == 2 ? 56u : 88u) && !(type == 4 && r.Bytes.size() == 92))
                    return fail(where + "invalid circle/arc size");
                const auto center = r.Point();
                const double radius = r.Double();
                const auto normal = r.Point();
                if (!r.Good || radius <= 0 || !ProxyDetail::AxisZ(normal))
                    return fail(where + "requires finite positive radius and +Z plane");
                if (type == 2)
                {
                    auto circle = std::make_unique<Circle>();
                    circle->Center = center; circle->Radius = radius; circle->Normal = normal;
                    primitive = std::move(circle);
                }
                else
                {
                    const auto start = r.Point();
                    const double sweep = r.Double();
                    if (!r.Good || std::abs(start.Z) > 1e-9 || std::hypot(start.X, start.Y) < 1e-12 ||
                        sweep <= 0 || sweep >= 2 * kPi)
                        return fail(where + "unsupported or invalid arc direction/sweep");
                    // The trailing arc-type field has no verified nonzero semantics.
                    if (r.Bytes.size() == 92 && r.Long() != 0) return fail(where + "unsupported arc type");
                    auto arc = std::make_unique<Arc>();
                    arc->Center = center; arc->Radius = radius; arc->Normal = normal;
                    arc->StartAngle = std::atan2(start.Y, start.X);
                    arc->EndAngle = arc->StartAngle + sweep;
                    primitive = std::move(arc);
                }
                break;
            }
            case 6:
            case 7:
            case 32:
            {
                if (r.Bytes.size() < 4) return fail(where + "missing vertex count");
                const auto count = r.Long();
                const std::size_t normalBytes = type == 32 ? 24 : 0;
                if (count < (type == 7 ? 3u : 2u) || count > 1000000 ||
                    count > (r.Bytes.size() - 4) / 24 ||
                    r.Bytes.size() != 4 + std::size_t(count) * 24 + normalBytes ||
                    totalVertices + count > 1000000)
                    return fail(where + "invalid/excessive vertex count or payload size");
                totalVertices += count;
                auto polyline = std::make_unique<LwPolyline>();
                polyline->Vertices.reserve(count);
                double elevation = 0;
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    const auto point = r.Point();
                    if (!r.Good) return fail(where + "non-finite vertex");
                    if (i == 0) elevation = point.Z;
                    if (std::abs(point.Z - elevation) > 1e-9) return fail(where + "non-planar vertices");
                    LwPolylineVertex vertex; vertex.Location = {point.X, point.Y};
                    polyline->Vertices.push_back(vertex);
                }
                if (type == 32 && !ProxyDetail::AxisZ(r.Point())) return fail(where + "unsupported normal");
                polyline->Elevation = elevation;
                if (type == 7) polyline->Flags = LwPolylineFlags::Closed;
                primitive = std::move(polyline);
                break;
            }
            case 14:
            {
                if (r.Bytes.size() != 4) return fail(where + "invalid color size");
                const auto aci = r.Long();
                if (aci > 256) return fail(where + "invalid ACI color");
                color = Color(static_cast<std::int16_t>(aci));
                break;
            }
            case 22:
            {
                if (r.Bytes.size() != 4) return fail(where + "invalid true-color size");
                const auto value = r.Long(), method = value >> 24;
                if (method == 0xc0) color = Color::ByLayer();
                else if (method == 0xc1) color = Color::ByBlock();
                else if (method == 0xc2) color = Color::FromTrueColor(value);
                else if (method == 0xc3 && (value & 0xffffff) <= 255) color = Color(static_cast<std::int16_t>(value & 255));
                else return fail(where + "unsupported raw color method");
                break;
            }
            case 19: // Selection marker does not alter display geometry.
                if (r.Bytes.size() != 4) return fail(where + "invalid marker size");
                r.Long();
                break;
            case 20:
                if (r.Bytes.size() != 4 || r.Long() != 0) return fail(where + "filled geometry is unsupported");
                break;
            case 23:
            {
                if (r.Bytes.size() != 4) return fail(where + "invalid lineweight size");
                const auto weight = static_cast<std::int32_t>(r.Long());
                switch (weight)
                {
                case -3: case -2: case -1: case 0: case 5: case 9: case 13: case 15: case 18:
                case 20: case 25: case 30: case 35: case 40: case 50: case 53: case 60:
                case 70: case 80: case 90: case 100: case 106: case 120: case 140: case 158: case 200: case 211:
                    lineWeight = static_cast<LineWeightType>(weight);
                    break;
                default: return fail(where + "invalid or unsupported lineweight");
                }
                break;
            }
            case 24:
                if (r.Bytes.size() != 8) return fail(where + "invalid linetype-scale size");
                lineTypeScale = r.Double();
                if (!r.Good || lineTypeScale <= 0) return fail(where + "invalid linetype scale");
                break;
            case 25:
                if (r.Bytes.size() != 8 || r.Double() != 0) return fail(where + "nonzero thickness is unsupported");
                break;
            default:
                return fail(where + "unsupported drawing/state command (no partial preview)");
            }
            if (!r.Done()) return fail(where + "truncated, non-finite or surplus payload");
            if (primitive)
            {
                ProxyDetail::CopyAppearance(source, *primitive);
                primitive->Color = color;
                primitive->LineTypeScale = lineTypeScale;
                primitive->LineWeight = lineWeight;
                parsed.Entities.push_back(std::move(primitive));
            }
            offset += size;
        }
        if (parsed.Entities.empty()) return fail("proxy contains no supported drawable geometry");
        output = std::move(parsed);
        return true;
    }
}

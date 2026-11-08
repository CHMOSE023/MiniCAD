#pragma once
#include "Database/UnknownObjects.h"
#include "Codec/CodePage.h"
#include "Dwg/Read/DwgBitReader.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MiniCAD::Proxy
{
    struct ProxyGraphics
    {
        std::vector<std::unique_ptr<MiniDWG::Entity>> Entities;
        std::size_t Skipped = 0;   // 跳过的记录（不支持的类型、数据不合理的记录）
    };

    // 记录里的图层 / 线型是表内序号，文字只给字体文件名：由调用方按图纸提供映射
    struct ProxyGraphicsContext
    {
        std::vector<MiniDWG::Handle> Layers;      // 图层表的表项顺序
        std::vector<MiniDWG::Handle> LineTypes;   // 线型表的表项顺序
        MiniDWG::Handle ByLayerLineType = MiniDWG::kNullHandle;
        MiniDWG::Handle ByBlockLineType = MiniDWG::kNullHandle;
        std::function<MiniDWG::Handle(std::string_view font, std::string_view bigFont)> StyleForFont;
        MiniDWG::Codec::CodePage CodePage = MiniDWG::Codec::CodePage::Gbk;
    };

    namespace Detail
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
            void Align() { Position = std::min(Bytes.size(), (Position + 3) & ~std::size_t(3)); }
            std::string PaddedString()
            {
                const std::size_t start = Position;
                while (Position < Bytes.size() && Bytes[Position] != 0) ++Position;
                if (Position >= Bytes.size()) { Good = false; return {}; }
                std::string value(reinterpret_cast<const char*>(Bytes.data() + start), Position - start);
                ++Position;
                Align();
                return value;
            }
            std::string PaddedUnicodeString()
            {
                std::string value;
                for (;;)
                {
                    if (Bytes.size() - Position < 2) { Good = false; return {}; }
                    char32_t unit = Bytes[Position] | (char32_t(Bytes[Position + 1]) << 8);
                    Position += 2;
                    if (unit == 0) break;
                    if (unit >= 0xd800 && unit < 0xdc00 && Bytes.size() - Position >= 2)
                    {
                        const char32_t low = Bytes[Position] | (char32_t(Bytes[Position + 1]) << 8);
                        if (low >= 0xdc00 && low < 0xe000)
                        {
                            Position += 2;
                            unit = 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00);
                        }
                    }
                    MiniDWG::Codec::AppendUtf8(value, unit);
                }
                Align();
                return value;
            }
            bool Done() const { return Good && Position == Bytes.size(); }
        };

        inline bool AxisZ(const MiniDWG::XYZ& normal)
        {
            return std::abs(normal.X) < 1e-9 && std::abs(normal.Y) < 1e-9 && std::abs(normal.Z - 1) < 1e-9;
        }
        // 坐标量级上限：超出的记录当作坏数据跳过，免得一条坏记录把整张图的范围撑到无穷大
        inline bool Sane(double v) { return std::isfinite(v) && std::abs(v) < 1e12; }
        inline bool Sane(const MiniDWG::XYZ& p) { return Sane(p.X) && Sane(p.Y) && Sane(p.Z); }

        // 记录 22 的 4 字节：通常是小端 uint32、颜色方法在最高字节（[b, g, r, method]）；
        // 也有写成 [method, r, g, b] 的。先按前者认，最高字节不是颜色方法时再按后者
        inline bool RawColor(std::uint32_t value, MiniDWG::Color& color)
        {
            auto isMethod = [](std::uint32_t m) { return m >= 0xc0 && m <= 0xc3; };
            std::uint32_t method = value >> 24, rgb = value & 0xffffff;
            if (!isMethod(method) && isMethod(value & 0xff))
            {
                method = value & 0xff;
                rgb = ((value >> 8) & 0xff) << 16 | ((value >> 16) & 0xff) << 8 | (value >> 24);
            }
            if (method == 0xc0) color = MiniDWG::Color::ByLayer();
            else if (method == 0xc1) color = MiniDWG::Color::ByBlock();
            else if (method == 0xc2) color = MiniDWG::Color::FromTrueColor(rgb);
            else if (method == 0xc3 && rgb <= 255) color = MiniDWG::Color(static_cast<std::int16_t>(rgb));
            else return false;
            return true;
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
        inline bool ArcThrough(const MiniDWG::XYZ& a, const MiniDWG::XYZ& b, const MiniDWG::XYZ& c, MiniDWG::XYZ& center, double& radius)
        {
            const double d = 2 * (a.X * (b.Y - c.Y) + b.X * (c.Y - a.Y) + c.X * (a.Y - b.Y));
            const double scale = std::max({std::hypot(b.X - a.X, b.Y - a.Y), std::hypot(c.X - a.X, c.Y - a.Y), 1e-300});
            if (std::abs(d) < 1e-12 * scale * scale) return false;
            const double a2 = a.X * a.X + a.Y * a.Y, b2 = b.X * b.X + b.Y * b.Y, c2 = c.X * c.X + c.Y * c.Y;
            center = {(a2 * (b.Y - c.Y) + b2 * (c.Y - a.Y) + c2 * (a.Y - b.Y)) / d,
                      (a2 * (c.X - b.X) + b2 * (a.X - c.X) + c2 * (b.X - a.X)) / d, a.Z};
            radius = std::hypot(a.X - center.X, a.Y - center.Y);
            return std::isfinite(radius) && radius > 0;
        }
        // bulges 只用于单环（记录 33 的闭合多段线），与顶点一一对应；边界顶点的 Z 是凸度
        inline std::unique_ptr<MiniDWG::Hatch> SolidHatch(const std::vector<std::vector<MiniDWG::XYZ>>& loops,
                                                          const std::vector<double>* bulges = nullptr)
        {
            auto hatch = std::make_unique<MiniDWG::Hatch>();
            hatch->IsSolid = true;
            hatch->Pattern.Name = "SOLID";
            hatch->PatternType = MiniDWG::HatchPatternType::SolidFill;
            hatch->Elevation = loops.front().front().Z;
            for (const auto& loop : loops)
            {
                MiniDWG::HatchBoundaryPath path;
                path.Flags = MiniDWG::BoundaryPathFlags::Polyline;
                auto edge = std::make_unique<MiniDWG::HatchBoundaryPathPolyline>();
                edge->IsClosed = true;
                for (std::size_t i = 0; i < loop.size(); ++i)
                    edge->Vertices.push_back({loop[i].X, loop[i].Y, bulges && i < bulges->size() ? (*bulges)[i] : 0.0});
                path.Edges.push_back(std::move(edge));
                hatch->Paths.push_back(std::move(path));
            }
            return hatch;
        }
    }

    // 按代理图形记录格式（docs/evidence/Tch-proxy-graphics.md）生成可显示的图元。
    // 不支持的记录与数据不合理的记录只跳过那一条（计入 Skipped）；记录头损坏、变换矩阵与裁剪（27～31，
    // 跳过后后面的图元位置会错）时整体失败；一个图元都没有时也返回失败
    inline bool Decode(const MiniDWG::UnknownEntity& source, ProxyGraphics& output,
                                    std::string* diagnostic = nullptr, const ProxyGraphicsContext* context = nullptr)
    {
        using namespace MiniDWG;
        output.Entities.clear();
        output.Skipped = 0;
        if (diagnostic) diagnostic->clear();
        auto fail = [&](const std::string& reason)
        {
            if (diagnostic) *diagnostic = reason;
            return false;
        };
        const auto& bytes = source.ProxyGraphics;
        if (bytes.size() < 8) return fail("proxy header is shorter than 8 bytes");
        if (bytes.size() > 32 * 1024 * 1024) return fail("proxy exceeds 32 MiB compatibility limit");
        ProxyGraphics parsed;
        Color color = source.Color;
        Handle layer = source.LayerHandle, lineType = source.LineTypeHandle;
        double lineTypeScale = source.LineTypeScale;
        LineWeightType lineWeight = source.LineWeight;
        bool fill = false;
        if (!std::isfinite(lineTypeScale) || lineTypeScale <= 0) return fail("invalid inherited linetype scale");
        std::size_t offset = 8, records = 0, totalVertices = 0;
        std::string firstSkip;
        auto emit = [&](std::unique_ptr<Entity> primitive)
        {
            Detail::CopyAppearance(source, *primitive);
            primitive->Color = color;
            primitive->LayerHandle = layer;
            primitive->LineTypeHandle = lineType;
            primitive->LineTypeScale = lineTypeScale;
            primitive->LineWeight = lineWeight;
            parsed.Entities.push_back(std::move(primitive));
        };
        auto closedPolyline = [&](const std::vector<XYZ>& loop)
        {
            auto polyline = std::make_unique<LwPolyline>();
            polyline->Vertices.reserve(loop.size());
            for (const auto& p : loop)
            {
                LwPolylineVertex vertex; vertex.Location = {p.X, p.Y};
                polyline->Vertices.push_back(vertex);
            }
            polyline->Elevation = loop.front().Z;
            polyline->Flags = LwPolylineFlags::Closed;
            return polyline;
        };
        while (offset < bytes.size())
        {
            if (++records > 100000) return fail("proxy record limit exceeded");
            if (bytes.size() - offset < 8) return fail("truncated proxy record header at " + std::to_string(offset));
            Detail::Reader header{std::span(bytes).subspan(offset, 8)};
            const auto size = header.Long(), type = header.Long();
            const std::string where = "proxy type " + std::to_string(type) + " at " + std::to_string(offset) + ": ";
            if (size < 8 || size > bytes.size() - offset) return fail(where + "invalid record size");
            if (type >= 27 && type <= 31) return fail(where + "clip / transform records are unsupported (no partial preview)");
            Detail::Reader r{std::span(bytes).subspan(offset + 8, size - 8)};
            offset += size;
            bool trailing = false;
            auto skip = [&](const std::string& reason)
            {
                ++parsed.Skipped;
                if (firstSkip.empty()) firstSkip = where + reason;
            };
            switch (type)
            {
            case 1:
                if (r.Bytes.size() != 48) { skip("invalid extents size"); continue; }
                r.Point(); r.Point();
                break;
            case 2:
            case 4:
            {
                if (r.Bytes.size() != (type == 2 ? 56u : 88u) && !(type == 4 && r.Bytes.size() == 92))
                    { skip("invalid circle/arc size"); continue; }
                const auto center = r.Point();
                const double radius = r.Double();
                const auto normal = r.Point();
                if (!r.Good || radius <= 0 || !Detail::AxisZ(normal) || !Detail::Sane(center) || !Detail::Sane(radius))
                    { skip("requires sane positive radius and +Z plane"); continue; }
                if (type == 2)
                {
                    auto circle = std::make_unique<Circle>();
                    circle->Center = center; circle->Radius = radius; circle->Normal = normal;
                    emit(std::move(circle));
                }
                else
                {
                    const auto start = r.Point();
                    const double sweep = r.Double();
                    if (!r.Good || std::abs(start.Z) > 1e-9 || std::hypot(start.X, start.Y) < 1e-12 ||
                        sweep <= 0 || sweep >= 2 * kPi)
                        { skip("unsupported or invalid arc direction/sweep"); continue; }
                    if (r.Bytes.size() == 92 && r.Long() != 0) { skip("unsupported arc type"); continue; }
                    auto arc = std::make_unique<Arc>();
                    arc->Center = center; arc->Radius = radius; arc->Normal = normal;
                    arc->StartAngle = std::atan2(start.Y, start.X);
                    arc->EndAngle = arc->StartAngle + sweep;
                    emit(std::move(arc));
                }
                break;
            }
            case 3:
            case 5:
            {
                if (r.Bytes.size() != 72 && r.Bytes.size() != 76) { skip("invalid 3-point circle/arc size"); continue; }
                const auto p1 = r.Point(), p2 = r.Point(), p3 = r.Point();
                if (r.Bytes.size() == 76 && r.Long() != 0) { skip("unsupported arc type"); continue; }
                XYZ center; double radius = 0;
                if (!r.Good || std::abs(p1.Z - p2.Z) > 1e-9 || std::abs(p1.Z - p3.Z) > 1e-9 ||
                    !Detail::ArcThrough(p1, p2, p3, center, radius) || !Detail::Sane(center) || !Detail::Sane(radius))
                    { skip("requires three distinct, non-collinear coplanar points"); continue; }
                if (type == 3)
                {
                    auto circle = std::make_unique<Circle>();
                    circle->Center = center; circle->Radius = radius;
                    emit(std::move(circle));
                    break;
                }
                const double a1 = std::atan2(p1.Y - center.Y, p1.X - center.X);
                const double a3 = std::atan2(p3.Y - center.Y, p3.X - center.X);
                const double cross = (p2.X - p1.X) * (p3.Y - p1.Y) - (p2.Y - p1.Y) * (p3.X - p1.X);
                auto arc = std::make_unique<Arc>();
                arc->Center = center; arc->Radius = radius;
                arc->StartAngle = cross > 0 ? a1 : a3;
                arc->EndAngle = cross > 0 ? a3 : a1;
                if (arc->EndAngle <= arc->StartAngle) arc->EndAngle += 2 * kPi;
                emit(std::move(arc));
                break;
            }
            case 6:
            case 7:
            case 32:
            {
                if (r.Bytes.size() < 4) { skip("missing vertex count"); continue; }
                const auto count = r.Long();
                const std::size_t normalBytes = type == 32 ? 24 : 0;
                if (count < (type == 7 ? 3u : 2u) || count > 1000000 ||
                    count > (r.Bytes.size() - 4) / 24 ||
                    r.Bytes.size() != 4 + std::size_t(count) * 24 + normalBytes ||
                    totalVertices + count > 1000000)
                    { skip("invalid/excessive vertex count or payload size"); continue; }
                totalVertices += count;
                std::vector<XYZ> points;
                points.reserve(count);
                bool ok = true;
                for (std::uint32_t i = 0; i < count && ok; ++i)
                {
                    points.push_back(r.Point());
                    ok = r.Good && Detail::Sane(points.back()) && std::abs(points.back().Z - points.front().Z) <= 1e-9;
                }
                if (ok && type == 32) ok = Detail::AxisZ(r.Point());
                if (!ok) { skip("non-finite, non-planar or non +Z vertices"); continue; }
                if (type == 7 && fill)
                {
                    emit(Detail::SolidHatch({points}));
                    break;
                }
                if (type == 7)
                {
                    emit(closedPolyline(points));
                    break;
                }
                auto polyline = std::make_unique<LwPolyline>();
                polyline->Vertices.reserve(count);
                for (const auto& p : points)
                {
                    LwPolylineVertex vertex; vertex.Location = {p.X, p.Y};
                    polyline->Vertices.push_back(vertex);
                }
                polyline->Elevation = points.front().Z;
                emit(std::move(polyline));
                break;
            }
            case 9:
            {
                const auto count = r.Long();
                if (!r.Good || count < 3 || count > (r.Bytes.size() - 4) / 24 || totalVertices + count > 1000000)
                    { skip("invalid/excessive shell vertex count"); continue; }
                totalVertices += count;
                std::vector<XYZ> vertices(count);
                for (auto& v : vertices) v = r.Point();
                const auto entries = r.Long();
                bool ok = r.Good && entries <= (r.Bytes.size() - r.Position) / 4;
                for (const auto& v : vertices) ok = ok && Detail::Sane(v);
                std::vector<std::vector<std::vector<XYZ>>> faces;   // 面（外环 + 洞）
                for (std::uint32_t read = 0; ok && read < entries;)
                {
                    const auto edges = static_cast<std::int32_t>(r.Long());
                    const std::uint32_t n = static_cast<std::uint32_t>(edges < 0 ? -std::int64_t(edges) : edges);
                    if (!r.Good || n < 3 || n > entries - read - 1) { ok = false; break; }
                    read += 1 + n;
                    std::vector<XYZ> loop;
                    for (std::uint32_t i = 0; i < n && ok; ++i)
                    {
                        const auto index = r.Long();
                        ok = r.Good && index < count;
                        if (ok) loop.push_back(vertices[index]);
                        ok = ok && std::abs(loop.back().Z - loop.front().Z) <= 1e-9;
                    }
                    if (edges < 0 && !faces.empty()) faces.back().push_back(std::move(loop));
                    else faces.push_back({std::move(loop)});
                }
                if (!ok) { skip("invalid shell vertices or faces"); continue; }
                for (auto& face : faces)
                {
                    if (fill) emit(Detail::SolidHatch(face));
                    else for (const auto& loop : face) emit(closedPolyline(loop));
                }
                trailing = true;   // 之后的边 / 面属性不影响二维显示
                break;
            }
            case 10:
            case 11:
            case 36:
            case 38:
            {
                const bool unicode = type >= 36;
                const auto position = r.Point(), normal = r.Point(), direction = r.Point();
                double height = 0, width = 1;
                std::string value, font, bigFont;
                if (type == 10 || type == 36)
                {
                    height = r.Double(); width = r.Double(); r.Double();
                    value = unicode ? r.PaddedUnicodeString() : r.PaddedString();
                }
                else
                {
                    value = unicode ? r.PaddedUnicodeString() : r.PaddedString();
                    r.Long(); r.Long();
                    height = r.Double(); width = r.Double(); r.Double(); r.Double();
                    for (int i = 0; i < 5; ++i) r.Long();
                    font = unicode ? r.PaddedUnicodeString() : r.PaddedString();
                    bigFont = unicode ? r.PaddedUnicodeString() : r.PaddedString();
                }
                if (!r.Good || !Detail::AxisZ(normal) || !(height > 0) || std::hypot(direction.X, direction.Y) < 1e-12 ||
                    !Detail::Sane(position) || !Detail::Sane(height))
                    { skip("invalid text record"); continue; }
                auto text = std::make_unique<TextEntity>();
                text->InsertPoint = position;
                text->Height = height;
                text->WidthFactor = width > 0 ? width : 1;
                text->Rotation = std::atan2(direction.Y, direction.X);
                text->Value = Codec::DecodeUnicodeEscapes(unicode ? value
                    : Codec::ToUtf8(value, context ? context->CodePage : Codec::CodePage::Gbk));
                if (context && context->StyleForFont)
                    text->StyleHandle = context->StyleForFont(font, bigFont);
                trailing = true;   // 有的写法在末尾多补对齐字节
                if (!text->Value.empty()) emit(std::move(text));
                break;
            }
            case 14:
            {
                const auto aci = r.Long();
                if (r.Bytes.size() != 4 || aci > 256) { skip("invalid ACI color"); continue; }
                color = Color(static_cast<std::int16_t>(aci));
                break;
            }
            case 16:
            {
                const auto index = r.Long();
                if (r.Bytes.size() != 4) { skip("invalid layer size"); continue; }
                if (index == 0xFFFFFFFFu) layer = source.LayerHandle;
                else if (!context || index >= context->Layers.size()) { skip("layer index out of range"); continue; }
                else layer = context->Layers[index];
                break;
            }
            case 18:
            {
                const auto index = r.Long();
                if (r.Bytes.size() != 4) { skip("invalid linetype size"); continue; }
                if (index == 0xFFFFFFFFu) lineType = source.LineTypeHandle;
                else if (!context) { skip("linetype index without linetype table"); continue; }
                else if (index == 32767) lineType = context->ByLayerLineType;
                else if (index == 32766) lineType = context->ByBlockLineType;
                else if (index < context->LineTypes.size()) lineType = context->LineTypes[index];
                else { skip("linetype index out of range"); continue; }
                break;
            }
            case 22:
            {
                const auto value = r.Long();
                if (r.Bytes.size() != 4 || !Detail::RawColor(value, color)) { skip("unsupported raw color"); continue; }
                break;
            }
            case 19:
                r.Long();
                break;
            case 20:
                if (r.Bytes.size() != 4) { skip("invalid fill size"); continue; }
                fill = r.Long() != 0;
                break;
            case 23:
            {
                if (r.Bytes.size() != 4) { skip("invalid lineweight size"); continue; }
                const auto weight = static_cast<std::int32_t>(r.Long());
                switch (weight)
                {
                case -3: case -2: case -1: case 0: case 5: case 9: case 13: case 15: case 18:
                case 20: case 25: case 30: case 35: case 40: case 50: case 53: case 60:
                case 70: case 80: case 90: case 100: case 106: case 120: case 140: case 158: case 200: case 211:
                    lineWeight = static_cast<LineWeightType>(weight);
                    break;
                default: skip("invalid or unsupported lineweight"); continue;
                }
                break;
            }
            case 24:
            {
                const double scale = r.Double();
                if (r.Bytes.size() != 8 || !r.Good || scale <= 0) { skip("invalid linetype scale"); continue; }
                lineTypeScale = scale;
                break;
            }
            case 25:   // 厚度：二维显示与厚度无关
                r.Double();
                break;
            case 33:
            {
                // 4 字节长度 + DWG 位流的 LWPOLYLINE（与对象数据里的同一格式）
                const auto dataBytes = r.Long();
                if (!r.Good || dataBytes < 1 || dataBytes > r.Bytes.size() - 4) { skip("invalid lwpolyline size"); continue; }
                DwgBitReader b(r.Bytes.subspan(4, dataBytes), CadVersion::AC1024, Codec::CodePage::Windows1252);
                const std::int16_t flags = b.ReadBitShort();
                const double constantWidth = (flags & 0x4) ? b.ReadBitDouble() : 0.0;
                const double elevation = (flags & 0x8) ? b.ReadBitDouble() : 0.0;
                if (flags & 0x2) b.ReadBitDouble();
                const XYZ normal = (flags & 0x1) ? b.Read3BitDouble() : XYZ{0, 0, 1};
                const std::int32_t count = b.ReadBitLong();
                const std::int32_t bulges = (flags & 0x10) ? b.ReadBitLong() : 0;
                const std::int32_t ids = (flags & 0x400) ? b.ReadBitLong() : 0;
                const std::int32_t widths = (flags & 0x20) ? b.ReadBitLong() : 0;
                if (b.Failed() || !b.CheckCount(count, 2) || count < 2 || bulges < 0 || bulges > count || ids < 0 || ids > count ||
                    widths < 0 || widths > count || totalVertices + std::size_t(count) > 1000000)
                    { skip("invalid lwpolyline counts"); continue; }
                totalVertices += std::size_t(count);
                auto polyline = std::make_unique<LwPolyline>();
                polyline->Vertices.resize(std::size_t(count));
                XY p = b.Read2RawDouble();
                polyline->Vertices[0].Location = p;
                for (std::int32_t i = 1; i < count; ++i)
                    polyline->Vertices[i].Location = p = b.Read2BitDoubleWithDefault(p);
                for (std::int32_t i = 0; i < bulges; ++i) polyline->Vertices[i].Bulge = b.ReadBitDouble();
                for (std::int32_t i = 0; i < ids; ++i) b.ReadBitLong();
                for (std::int32_t i = 0; i < widths; ++i)
                {
                    polyline->Vertices[i].StartWidth = b.ReadBitDouble();
                    polyline->Vertices[i].EndWidth = b.ReadBitDouble();
                }
                bool ok = !b.Failed() && Detail::AxisZ(normal) && Detail::Sane(elevation);
                for (const auto& v : polyline->Vertices)
                    ok = ok && Detail::Sane(v.Location.X) && Detail::Sane(v.Location.Y) && std::isfinite(v.Bulge);
                if (!ok) { skip("invalid lwpolyline data"); continue; }
                const bool closed = (flags & 0x200) != 0;
                if (fill && closed)
                {
                    std::vector<XYZ> loop;
                    std::vector<double> loopBulges;
                    for (const auto& v : polyline->Vertices)
                    {
                        loop.push_back({v.Location.X, v.Location.Y, elevation});
                        loopBulges.push_back(v.Bulge);
                    }
                    emit(Detail::SolidHatch({loop}, &loopBulges));
                    trailing = true;
                    break;
                }
                polyline->Elevation = elevation;
                polyline->ConstantWidth = constantWidth;
                if (closed) polyline->Flags = LwPolylineFlags::Closed;
                emit(std::move(polyline));
                trailing = true;
                break;
            }
            default:
                skip("unsupported drawing/state command");
                continue;
            }
            if (trailing ? !r.Good : !r.Done()) skip("truncated, non-finite or surplus payload");
        }
        output.Skipped = parsed.Skipped;
        if (parsed.Entities.empty())
            return fail(firstSkip.empty() ? "proxy contains no supported drawable geometry"
                                          : "proxy contains no supported drawable geometry; first skipped " + firstSkip);
        if (diagnostic) *diagnostic = firstSkip;
        output = std::move(parsed);
        return true;
    }
}

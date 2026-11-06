#pragma once
// 代理图形（proxy graphics）通用解码器。
//
// 自定义实体在 DWG 里会附带一段预先算好的二维矢量快照（代理图形），供没有安装
// 原插件的程序也能把对象画出来。这段数据与厂商无关——天正（TCH_*）、斯维尔
// （SWR_*）、浩辰、建筑结构设计工具（ASDT_*）等写进来的格式是同一套记录流，
// 区别只在各自用到了哪些记录类型、以及少数记录的取值约定。
//
// 本文件只做「把记录流还原成基础图元」，不含任何厂商分支；天正私有的主数据流
// （墙体、门窗、楼梯等语义）另见 `Tch*.h`。
//
// 记录格式（小端）：
//   u32 total_size   （== 整段长度）
//   u32 record_count
//   record_count × {
//       u32 record_size   （字节数，含这 8 字节头）
//       u32 record_type
//       u8[record_size-8] data
//   }
// 字符串为 NUL 结尾（8 位或 UTF-16LE），按 4 字节对齐。
//
// 容错原则（参照 OpenCADStudio 的 proxy_graphics.rs）：
//   · 未知记录类型 → 跳过该条，继续解后面的记录（不再整份放弃）
//   · 单条记录数据不合理 → 跳过该条，继续
//   · 坐标超出合理范围 → 丢弃该图元（防止少量脏数据把整张图的包围盒撑爆）
//   · 一条都解不出 → 返回 false，由调用方决定怎么提示
// 返回值只表示「有没有解出可显示的图元」，具体跳过情况记在 Skipped 里。

#include "Database/UnknownObjects.h"
#include "Codec/CodePage.h"
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
        // 因为不认识 / 数据不合理而跳过的记录条数。几何仍照常输出这些除外。
        std::size_t Skipped = 0;
        // 因为坐标超出合理范围而丢弃的图元个数（正常图纸应为 0）。
        std::size_t Discarded = 0;
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
        // 坐标合理上限。代理图形偶尔有解错位产生的天文数字，一旦进入场景会把包围盒
        // 撑到 1e70 量级，视图「全部显示」时整张图缩成一个点（真实事故）。1e12 远大于
        // 任何实际工程图纸（即使以毫米为单位也够用），却足以挡住脏数据。
        inline constexpr double kMaxCoordinate = 1e12;
        inline bool Sane(double v) { return std::isfinite(v) && std::abs(v) < kMaxCoordinate; }
        inline bool Sane(const MiniDWG::XYZ& p) { return Sane(p.X) && Sane(p.Y) && Sane(p.Z); }

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

        // DWG 位流读取（type 33 内嵌的 LWPOLYLINE 用位字段编码，与其它记录不同）。
        //
        // ⚠ 位序是 **MSB-first**：一个字节里先取最高位。这与直觉相反，但 ODA 的位流
        // 规范如此，acadrust（OpenCADStudio 用的 DWG 库）的 `read_2bits` 就是
        // `last_byte >> 6`。用 LSB-first 解出来的 flags 会是 0x1FE4 这类垃圾值。
        //
        // 另外 raw 类型（RS/RL/RD）读取时若当前不在字节边界，需要把跨字节的位拼起来，
        // 见 Byte() 的实现。
        struct BitReader
        {
            std::span<const std::uint8_t> Bytes;
            std::size_t Bit = 0;     // 已消费的位
            bool Good = true;

            bool Has(std::size_t bits) const
            {
                const std::size_t total = Bytes.size() * 8;
                return Bit <= total && total - Bit >= bits;
            }
            // n 位（n<=8），先取高位
            std::uint8_t Bits(int count)
            {
                std::uint32_t value = 0;
                for (int i = 0; i < count; ++i)
                {
                    if (!Has(1)) { Good = false; return static_cast<std::uint8_t>(value); }
                    const std::uint8_t bit = (Bytes[Bit >> 3] >> (7 - (Bit & 7))) & 1;
                    value = (value << 1) | bit;
                    ++Bit;
                }
                return static_cast<std::uint8_t>(value);
            }
            std::uint8_t Byte()
            {
                if ((Bit & 7) == 0)
                {
                    if (!Has(8)) { Good = false; return 0; }
                    const std::uint8_t value = Bytes[Bit >> 3];
                    Bit += 8;
                    return value;
                }
                return Bits(8);   // 跨字节：逐位拼
            }
            std::uint16_t RawShort()
            {
                const std::uint16_t lo = Byte(), hi = Byte();
                return static_cast<std::uint16_t>(lo | (hi << 8));
            }
            std::int32_t RawLong()
            {
                std::uint32_t value = 0;
                for (int i = 0; i < 4; ++i) value |= std::uint32_t(Byte()) << (8 * i);
                return static_cast<std::int32_t>(value);
            }
            double RawDouble()
            {
                std::uint64_t bits = 0;
                for (int i = 0; i < 8; ++i) bits |= std::uint64_t(Byte()) << (8 * i);
                double value = 0;
                if (Good) std::memcpy(&value, &bits, sizeof(value));
                return value;
            }
            // BS：2 位标记，0 = 16 位原码，1 = 8 位无符号，2 = 0，3 = 256
            std::int16_t BitShort()
            {
                switch (Bits(2))
                {
                case 0: return static_cast<std::int16_t>(RawShort());
                case 1: return Byte();
                case 2: return 0;
                default: return 256;
                }
            }
            // BL：2 位标记，0 = 32 位原码，1 = 8 位无符号，2 = 0，3 未用（按 0 处理）
            std::int32_t BitLong()
            {
                switch (Bits(2))
                {
                case 0: return RawLong();
                case 1: return Byte();
                case 2: return 0;
                default: return 0;
                }
            }
            // BD：2 位标记，0 = 原码 double，1 = 1.0，2/3 = 0.0
            double BitDouble()
            {
                switch (Bits(2))
                {
                case 0: return RawDouble();
                case 1: return 1.0;
                default: return 0.0;
                }
            }
            // DD：相对上一个值的**字节差分**编码（不是"沿用"）
            //   00 = 用默认值；01 = 4 字节覆盖低 4 字节；
            //   10 = 6 字节（2 覆盖偏移 4-5、4 覆盖低 4）；11 = 完整 RD
            double BitDoubleWithDefault(double fallback)
            {
                std::uint8_t arr[8];
                std::memcpy(arr, &fallback, sizeof(arr));
                switch (Bits(2))
                {
                case 0: return fallback;
                case 1:
                    for (int i = 0; i < 4; ++i) arr[i] = Byte();
                    break;
                case 2:
                    arr[4] = Byte(); arr[5] = Byte();
                    for (int i = 0; i < 4; ++i) arr[i] = Byte();
                    break;
                default:
                    return RawDouble();
                }
                double value = 0;
                std::memcpy(&value, arr, sizeof(value));
                return value;
            }
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
        inline std::unique_ptr<MiniDWG::Hatch> SolidHatch(const std::vector<std::vector<MiniDWG::XYZ>>& loops)
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
                for (const auto& p : loop) edge->Vertices.push_back({p.X, p.Y, 0});
                path.Edges.push_back(std::move(edge));
                hatch->Paths.push_back(std::move(path));
            }
            return hatch;
        }
    }

    // 把一段代理图形记录流解成可显示的图元。返回是否解出了至少一个图元；
    // 跳过的记录与丢弃的图元数量记在 output 的 Skipped / Discarded 里。
    inline bool Decode(const MiniDWG::UnknownEntity& source, ProxyGraphics& output,
                       std::string* diagnostic = nullptr, const ProxyGraphicsContext* context = nullptr);

    inline bool Decode(const MiniDWG::UnknownEntity& source, ProxyGraphics& output,
                       std::string* diagnostic, const ProxyGraphicsContext* context)
    {
        // 本文件不 using MiniDWG：调用方（如 CadExchange.cpp）也会引入
        // MiniCAD 自己的 Circle/Arc/TextEntity 等实体，只用 using 会被这些同名类型遮蔽，
        // 导致 emit() 收到错误的 unique_ptr。所有类型一律写全限定名。
        output.Entities.clear();
        output.Skipped = 0;
        output.Discarded = 0;
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
        MiniDWG::Color color = source.Color;
        MiniDWG::Handle layer = source.LayerHandle, lineType = source.LineTypeHandle;
        double lineTypeScale = source.LineTypeScale;
        MiniDWG::LineWeightType lineWeight = source.LineWeight;
        bool fill = false;
        if (!std::isfinite(lineTypeScale) || lineTypeScale <= 0) return fail("invalid inherited linetype scale");

        std::size_t offset = 8, records = 0, totalVertices = 0;
        // 解出一条图元：外观属性随当前状态；坐标由各分支在生成时校验（Detail::Sane）
        auto emit = [&](std::unique_ptr<MiniDWG::Entity> primitive) -> bool
        {
            Detail::CopyAppearance(source, *primitive);
            primitive->Color = color;
            primitive->LayerHandle = layer;
            primitive->LineTypeHandle = lineType;
            primitive->LineTypeScale = lineTypeScale;
            primitive->LineWeight = lineWeight;
            parsed.Entities.push_back(std::move(primitive));
            return true;
        };
        // 单条记录解析失败时调用：记账后跳过，不放弃整份
        auto skip = [&]() { ++parsed.Skipped; };

        while (offset < bytes.size())
        {
            if (++records > 100000) { skip(); break; }
            if (bytes.size() - offset < 8) { skip(); break; }
            Detail::Reader header{std::span(bytes).subspan(offset, 8)};
            const auto size = header.Long(), type = header.Long();
            if (size < 8 || size > bytes.size() - offset) { skip(); break; }
            const std::span<const std::uint8_t> payload = std::span(bytes).subspan(offset + 8, size - 8);
            const bool advanced = [&]() -> bool
            {
                Detail::Reader r{payload};
                bool trailing = false;
                switch (type)
                {
                case 1:   // 包围盒：不产生图元
                    if (r.Bytes.size() != 48) return false;
                    r.Point(); r.Point();
                    break;
                case 2:
                case 4:
                {
                    if (r.Bytes.size() != (type == 2 ? 56u : 88u) && !(type == 4 && r.Bytes.size() == 92))
                        return false;
                    const auto center = r.Point();
                    const double radius = r.Double();
                    const auto normal = r.Point();
                    if (!r.Good || radius <= 0 || !Detail::AxisZ(normal)) return false;
                    if (!Detail::Sane(center) || !Detail::Sane(radius)) { ++parsed.Discarded; break; }
                    if (type == 2)
                    {
                        auto circle = std::make_unique<MiniDWG::Circle>();
                        circle->Center = center; circle->Radius = radius; circle->Normal = normal;
                        emit(std::move(circle));
                    }
                    else
                    {
                        const auto start = r.Point();
                        const double sweep = r.Double();
                        if (!r.Good || std::abs(start.Z) > 1e-9 || std::hypot(start.X, start.Y) < 1e-12 ||
                            sweep <= 0 || sweep >= 2 * MiniDWG::kPi)
                            return false;
                        if (r.Bytes.size() == 92 && r.Long() != 0) return false;
                        auto arc = std::make_unique<MiniDWG::Arc>();
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
                    if (r.Bytes.size() != 72 && r.Bytes.size() != 76) return false;
                    const auto p1 = r.Point(), p2 = r.Point(), p3 = r.Point();
                    if (r.Bytes.size() == 76 && r.Long() != 0) return false;
                    MiniDWG::XYZ center; double radius = 0;
                    if (!r.Good || std::abs(p1.Z - p2.Z) > 1e-9 || std::abs(p1.Z - p3.Z) > 1e-9 ||
                        !Detail::ArcThrough(p1, p2, p3, center, radius))
                        return false;
                    if (!Detail::Sane(center) || !Detail::Sane(radius)) { ++parsed.Discarded; break; }
                    if (type == 3)
                    {
                        auto circle = std::make_unique<MiniDWG::Circle>();
                        circle->Center = center; circle->Radius = radius;
                        emit(std::move(circle));
                        break;
                    }
                    const double a1 = std::atan2(p1.Y - center.Y, p1.X - center.X);
                    const double a3 = std::atan2(p3.Y - center.Y, p3.X - center.X);
                    const double cross = (p2.X - p1.X) * (p3.Y - p1.Y) - (p2.Y - p1.Y) * (p3.X - p1.X);
                    auto arc = std::make_unique<MiniDWG::Arc>();
                    arc->Center = center; arc->Radius = radius;
                    arc->StartAngle = cross > 0 ? a1 : a3;
                    arc->EndAngle = cross > 0 ? a3 : a1;
                    if (arc->EndAngle <= arc->StartAngle) arc->EndAngle += 2 * MiniDWG::kPi;
                    emit(std::move(arc));
                    break;
                }
                case 6:
                case 7:
                case 32:
                {
                    if (r.Bytes.size() < 4) return false;
                    const auto count = r.Long();
                    const std::size_t normalBytes = type == 32 ? 24 : 0;
                    if (count < (type == 7 ? 3u : 2u) || count > 1000000 ||
                        count > (r.Bytes.size() - 4) / 24 ||
                        r.Bytes.size() != 4 + std::size_t(count) * 24 + normalBytes ||
                        totalVertices + count > 1000000)
                        return false;
                    totalVertices += count;
                    std::vector<MiniDWG::XYZ> points;
                    points.reserve(count);
                    for (std::uint32_t i = 0; i < count; ++i)
                    {
                        points.push_back(r.Point());
                        if (!r.Good) return false;
                        if (std::abs(points.back().Z - points.front().Z) > 1e-9) return false;
                    }
                    if (type == 32 && !Detail::AxisZ(r.Point())) return false;
                    for (const auto& q : points)
                        if (!Detail::Sane(q)) { ++parsed.Discarded; return true; }
                    if (type == 7 && fill)
                    {
                        emit(Detail::SolidHatch({points}));
                        break;
                    }
                    auto polyline = std::make_unique<MiniDWG::LwPolyline>();
                    polyline->Vertices.reserve(count);
                    for (const auto& p : points)
                    {
                        MiniDWG::LwPolylineVertex vertex; vertex.Location = {p.X, p.Y};
                        polyline->Vertices.push_back(vertex);
                    }
                    polyline->Elevation = points.front().Z;
                    if (type == 7) polyline->Flags = MiniDWG::LwPolylineFlags::Closed;
                    emit(std::move(polyline));
                    break;
                }
                case 9:
                {
                    const auto count = r.Long();
                    if (!r.Good || count < 3 || count > (r.Bytes.size() - 4) / 24 || totalVertices + count > 1000000)
                        return false;
                    totalVertices += count;
                    std::vector<MiniDWG::XYZ> vertices(count);
                    for (auto& v : vertices) v = r.Point();
                    for (const auto& q : vertices)
                        if (!Detail::Sane(q)) { ++parsed.Discarded; return true; }
                    const auto entries = r.Long();
                    if (!r.Good) return false;
                    if (entries > (r.Bytes.size() - r.Position) / 4) return false;
                    std::vector<std::vector<std::vector<MiniDWG::XYZ>>> faces;   // 面（外环 + 洞）
                    for (std::uint32_t read = 0; read < entries;)
                    {
                        const auto edges = static_cast<std::int32_t>(r.Long());
                        const std::uint32_t n = static_cast<std::uint32_t>(edges < 0 ? -std::int64_t(edges) : edges);
                        if (!r.Good || n < 3 || n > entries - read - 1) return false;
                        read += 1 + n;
                        std::vector<MiniDWG::XYZ> loop;
                        for (std::uint32_t i = 0; i < n; ++i)
                        {
                            const auto index = r.Long();
                            if (!r.Good || index >= count) return false;
                            loop.push_back(vertices[index]);
                            if (std::abs(loop.back().Z - loop.front().Z) > 1e-9) return false;
                        }
                        if (edges < 0 && !faces.empty()) faces.back().push_back(std::move(loop));
                        else faces.push_back({std::move(loop)});
                    }
                    for (auto& face : faces)
                    {
                        // 与 OpenCADStudio 一致：fill 为真时**既出实心填充、也出边框轮廓**。
                        // 以前这里 fill 时 `continue` 只出填充、不加轮廓线，于是文字类的
                        // 背景框（`SWR_TEXT`，实测 2200×650）渲染成一个没有边框的纯白色块，
                        // 把画在上面的文字整个盖住——看起来就是"显示成了白色块状"。
                        if (fill) emit(Detail::SolidHatch(face));
                        for (const auto& loop : face)
                        {
                            auto polyline = std::make_unique<MiniDWG::LwPolyline>();
                            for (const auto& p : loop)
                            {
                                MiniDWG::LwPolylineVertex vertex; vertex.Location = {p.X, p.Y};
                                polyline->Vertices.push_back(vertex);
                            }
                            polyline->Elevation = loop.front().Z;
                            polyline->Flags = MiniDWG::LwPolylineFlags::Closed;
                            emit(std::move(polyline));
                        }
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
                        // type 11 / 38 的布局（与 OpenCADStudio 的 decode_text2 一致）：
                        //   [72] 字符串（NUL 结尾、补到 4 字节对齐）
                        //   然后 8 字节「字符串长度 + raw 标志」，接着 height / widthFactor / oblique。
                        value = unicode ? r.PaddedUnicodeString() : r.PaddedString();
                        r.Long(); r.Long();
                        height = r.Double(); width = r.Double(); r.Double(); r.Double();
                        for (int i = 0; i < 5; ++i) r.Long();
                        font = unicode ? r.PaddedUnicodeString() : r.PaddedString();
                        bigFont = unicode ? r.PaddedUnicodeString() : r.PaddedString();
                    }
                    if (!r.Good || !Detail::AxisZ(normal) || !(height > 0) ||
                        std::hypot(direction.X, direction.Y) < 1e-12)
                        return false;
                    if (!Detail::Sane(position) || !Detail::Sane(height)) { ++parsed.Discarded; break; }
                    auto text = std::make_unique<MiniDWG::TextEntity>();
                    text->InsertPoint = position;
                    text->Height = height;
                    text->WidthFactor = width > 0 ? width : 1;
                    text->Rotation = std::atan2(direction.Y, direction.X);
                    text->Value = MiniDWG::Codec::DecodeUnicodeEscapes(unicode ? value
                        : MiniDWG::Codec::ToUtf8(value, context ? context->CodePage : MiniDWG::Codec::CodePage::Gbk));
                    if (context && context->StyleForFont)
                        text->StyleHandle = context->StyleForFont(font, bigFont);
                    trailing = true;   // 有的写法在末尾多补对齐字节
                    if (!text->Value.empty()) emit(std::move(text));
                    break;
                }
                case 14:
                {
                    if (r.Bytes.size() != 4) return false;
                    const auto aci = r.Long();
                    if (aci > 256) return false;
                    color = MiniDWG::Color(static_cast<std::int16_t>(aci));
                    break;
                }
                case 16:
                {
                    if (r.Bytes.size() != 4) return false;
                    const auto index = r.Long();
                    if (index == 0xFFFFFFFFu) layer = source.LayerHandle;
                    else if (!context || index >= context->Layers.size()) return false;
                    else layer = context->Layers[index];
                    break;
                }
                case 18:
                {
                    if (r.Bytes.size() != 4) return false;
                    const auto index = r.Long();
                    if (index == 0xFFFFFFFFu) lineType = source.LineTypeHandle;
                    else if (!context) return false;
                    else if (index == 32767) lineType = context->ByLayerLineType;
                    else if (index == 32766) lineType = context->ByBlockLineType;
                    else if (index < context->LineTypes.size()) lineType = context->LineTypes[index];
                    else return false;
                    break;
                }
                case 22:
                {
                    if (r.Bytes.size() != 4) return false;
                    // 颜色记录的 4 字节有两种写法，都要认：
                    //   a) 字节序 [method, r, g, b]：实测 `练习-施工图.dwg` 句柄 368F5 的
                    //      `c2 00 00 00` = 真彩 RGB(0,0,0)、`c3 00 00 07` = ACI 7；
                    //   b) 小端 u32 且 method 在高字节（OpenCADStudio 的样例 0xC2FF8000）。
                    // 先看低字节是不是合法 method，不是再看高字节。以前只认 b)，a) 形态下
                    // `>> 24` 恒为 0，颜色全部落到 ByLayer 兜底。
                    const std::uint32_t raw = r.Long();
                    const auto lo = static_cast<std::uint8_t>(raw & 0xFFu);
                    if (lo == 0xc0 || lo == 0xc1 || lo == 0xc2 || lo == 0xc3)
                    {
                        const auto c1 = static_cast<std::uint8_t>((raw >> 8) & 0xFFu);
                        const auto c2 = static_cast<std::uint8_t>((raw >> 16) & 0xFFu);
                        const auto c3 = static_cast<std::uint8_t>((raw >> 24) & 0xFFu);
                        if (lo == 0xc0) color = MiniDWG::Color::ByLayer();
                        else if (lo == 0xc1) color = MiniDWG::Color::ByBlock();
                        else if (lo == 0xc2)
                            color = MiniDWG::Color::FromTrueColor((std::uint32_t(c3) << 16) | (std::uint32_t(c2) << 8) | c1);
                        else color = MiniDWG::Color(static_cast<std::int16_t>(c3));
                        break;
                    }
                    const auto method = raw >> 24;
                    if (method == 0xc0) color = MiniDWG::Color::ByLayer();
                    else if (method == 0xc1) color = MiniDWG::Color::ByBlock();
                    else if (method == 0xc2) color = MiniDWG::Color::FromTrueColor(raw);
                    else if (method == 0xc3 && (raw & 0xffffff) <= 255) color = MiniDWG::Color(static_cast<std::int16_t>(raw & 255));
                    else return false;
                    break;
                }
                case 19:
                    if (r.Bytes.size() != 4) return false;
                    r.Long();
                    break;
                case 20:
                    if (r.Bytes.size() != 4) return false;
                    fill = r.Long() != 0;
                    break;
                case 23:
                {
                    if (r.Bytes.size() != 4) return false;
                    const auto weight = static_cast<std::int32_t>(r.Long());
                    switch (weight)
                    {
                    case -3: case -2: case -1: case 0: case 5: case 9: case 13: case 15: case 18:
                    case 20: case 25: case 30: case 35: case 40: case 50: case 53: case 60:
                    case 70: case 80: case 90: case 100: case 106: case 120: case 140: case 158: case 200: case 211:
                        lineWeight = static_cast<MiniDWG::LineWeightType>(weight);
                        break;
                    default: return false;
                    }
                    break;
                }
                case 24:
                    if (r.Bytes.size() != 8) return false;
                    lineTypeScale = r.Double();
                    if (!r.Good || lineTypeScale <= 0) return false;
                    break;
                case 25:
                    // 厚度：只接受 0（非零厚度本预览不建模，跳过该条状态记录即可）
                    if (r.Bytes.size() != 8) return false;
                    r.Double();
                    break;
                case 33:
                {
                    // LWPOLYLINE：顶点与凸度按**位字段**编码，payload 前 4 字节是长度前缀
                    // （与 OpenCADStudio / acadrust 的 `d.get(4..)` 一致），其后才是位流。
                    // 实测 `1#楼-结构-202104.dwg` 句柄 1863E6（标高标注的实心倒三角箭头）：
                    // flags=0x0000、7 个顶点，解出 (-148976.3,43026.2)…(-149216.3,43026.2)，
                    // 与同对象 type 6 折线的坐标同一区域。
                    if (r.Bytes.size() < 4) return false;
                    const auto dataBytes = static_cast<std::uint32_t>(r.Long());
                    if (!r.Good || dataBytes < 4 || dataBytes > r.Bytes.size() - 4) return false;
                    Detail::BitReader b{r.Bytes.subspan(4, dataBytes)};
                    const auto flags = static_cast<std::uint16_t>(b.BitShort());
                    double constantWidth = 0.0, elevation = 0.0, thickness = 0.0;
                    MiniDWG::XYZ extrusion{0, 0, 1};
                    if (flags & 0x4) constantWidth = b.BitDouble();
                    if (flags & 0x8) elevation = b.BitDouble();
                    if (flags & 0x2) thickness = b.BitDouble();
                    if (flags & 0x1)
                    {
                        extrusion.X = b.BitDouble();
                        extrusion.Y = b.BitDouble();
                        extrusion.Z = b.BitDouble();
                    }
                    // 顶点数上限按实际字节数推导，不用魔数
                    const std::int32_t maxPoints = static_cast<std::int32_t>(b.Bytes.size() / 8);
                    const auto pointCount = b.BitLong();
                    const auto bulgeCount = (flags & 0x10) ? b.BitLong() : 0;
                    if (!b.Good || pointCount < 2 || pointCount > maxPoints ||
                        bulgeCount < 0 || bulgeCount > pointCount ||
                        totalVertices + std::size_t(pointCount) > 1000000)
                        return false;
                    totalVertices += std::size_t(pointCount);
                    std::vector<MiniDWG::XYZ> points;
                    points.reserve(std::size_t(pointCount));
                    MiniDWG::XYZ p{b.RawDouble(), b.RawDouble(), elevation};
                    points.push_back(p);
                    for (std::int32_t i = 1; i < pointCount; ++i)
                    {
                        const double x = b.BitDoubleWithDefault(p.X);
                        const double y = b.BitDoubleWithDefault(p.Y);
                        p = {x, y, elevation};
                        points.push_back(p);
                    }
                    std::vector<double> bulges(points.size(), 0.0);
                    for (std::int32_t i = 0; i < bulgeCount; ++i) bulges[std::size_t(i)] = b.BitDouble();
                    if (!b.Good || !Detail::AxisZ(extrusion)) return false;
                    for (const auto& q : points)
                        if (!Detail::Sane(q)) return false;
                    const bool closed = (flags & 0x200) != 0;
                    if (fill && closed)
                    {
                        emit(Detail::SolidHatch({points}));
                        break;
                    }
                    auto polyline = std::make_unique<MiniDWG::LwPolyline>();
                    polyline->Vertices.reserve(points.size());
                    for (std::size_t i = 0; i < points.size(); ++i)
                    {
                        MiniDWG::LwPolylineVertex vertex;
                        vertex.Location = {points[i].X, points[i].Y};
                        vertex.Bulge = bulges[i];
                        polyline->Vertices.push_back(vertex);
                    }
                    polyline->Elevation = elevation;
                    polyline->ConstantWidth = constantWidth;
                    polyline->Thickness = thickness;
                    if (closed) polyline->Flags = MiniDWG::LwPolylineFlags::Closed;
                    trailing = true;
                    emit(std::move(polyline));
                    break;
                }
                default:
                    return false;   // 不认识的记录：交给外层跳过
                }
                if (trailing ? !r.Good : !r.Done()) return false;
                return true;
            }();
            if (!advanced) ++parsed.Skipped;
            offset += size;
        }
        // 统计信息即使在没有可显示图元时也要带回去，调用方才能判断「是没解出几何」
        // 还是「几何都被丢弃了」
        const std::size_t skippedCount = parsed.Skipped;
        const std::size_t discardedCount = parsed.Discarded;
        if (parsed.Entities.empty())
        {
            output.Skipped = skippedCount;
            output.Discarded = discardedCount;
            return fail("proxy contains no supported drawable geometry (skipped " +
                        std::to_string(skippedCount) + " records, discarded " +
                        std::to_string(discardedCount) + " primitives)");
        }
        output = std::move(parsed);
        return true;
    }
}

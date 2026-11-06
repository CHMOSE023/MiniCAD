#pragma once
#include "Import/TchText.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <numbers>
#include <string>
#include <vector>
 
namespace MiniCAD::Tch
{
    struct TchPipe
    {
        MiniDWG::XYZ Start, End;
        double Sweep = 0;           // 弧管的圆心角（弧度，正为逆时针；字段名像凸度，实际存的是角度），0 为直管
        double RiserRadius = 0;     // 立管圆的图面半径（× 比例为世界半径）
        double Diameter = 0;        // 管径（如 100）
        double Scale = 100;
        bool   Label = false;       // 显示管径标注
        double LabelX = 0, LabelY = 0, LabelHeight = 3;
        std::string LabelPrefix, LabelSuffix;     // 标注 = 前缀（如“DN”）+ 管径 + 后缀
        MiniDWG::Handle LabelStyle = MiniDWG::kNullHandle;
        MiniDWG::Handle LabelLayer = MiniDWG::kNullHandle;
        // 可见段：沿管线距起点 VisibleMiddle 处为中点、长 VisibleLength（两端让出管件的位置）；双线管按它显示
        double VisibleMiddle = 0, VisibleLength = 0;
        // TCH_DOUBLELINEPIPE 双线管：管宽、弯头中心半径与管宽之比、中心线图层
        double Width = 0, BendRatio = 1;
        MiniDWG::Handle CenterLayer = MiniDWG::kNullHandle;

        bool IsRiser() const { return std::abs(Start.X - End.X) < 1e-6 && std::abs(Start.Y - End.Y) < 1e-6; }
    };

    struct TchPipeBlock
    {
        double X = 0, Y = 0, Rotation = 0, ScaleX = 1, ScaleY = 1;
        std::vector<MiniDWG::Handle> Blocks;    // 候选图块（句柄流中的硬指针，取第一个能解析为块定义的）
        double WellInvert = 0, WellGround = 0;  // 只用于 TCH_WELL
    };

    namespace Detail
    {
        inline bool CheckPayload(const MiniDWG::UnknownEntity& entity, std::string_view name, const char*& why)
        {
            const auto& data = entity.Raw.Dwg;
            if (entity.Raw.DxfName != name) return false;
            if (data.Version < MiniDWG::CadVersion::AC1021 || data.Main.size() < (data.MainBits + 7) / 8)
            {
                why = "unsupported DWG version for native pipe object";
                return false;
            }
            return true;
        }

        inline MiniDWG::XYZ Read3(MiniDWG::DwgBitReader& r) { return {r.ReadBitDouble(), r.ReadBitDouble(), r.ReadBitDouble()}; }

        inline bool ReadPipeNode(MiniDWG::DwgBitReader& r, int& handles, int& texts)
        {
            if (r.ReadByte() >= 2) return false;
            r.ReadByte();
            const int count = r.ReadByte();
            for (int i = 0; i < count && !r.Failed(); ++i)
            {
                if (r.ReadByte() >= 2) return false;
                for (int k = 0; k < 4; ++k) r.ReadByte();
                ++texts; ++handles;
                Read3(r); Read3(r);
            }
            ++texts;
            return !r.Failed();
        }
         
        inline bool PlaceEquipment(TchPipeBlock& b, const MiniDWG::XYZ& at, const MiniDWG::XYZ& dir, double s, double sy, int mirror)
        {
            if (!(std::abs(sy) >= 1e-3) || !Finite(sy)) sy = s;
            b.X = at.X; b.Y = at.Y;
            b.Rotation = std::atan2(dir.Y, dir.X);
            b.ScaleX = mirror & 2 ? -s : s;
            b.ScaleY = mirror & 1 ? -sy : sy;
            return Finite(b.X) && Finite(b.Y) && Finite(b.Rotation) && std::abs(s) > 1e-9 && Finite(s);
        }

        inline bool ReadEquipment(MiniDWG::DwgBitReader& r, const TchBase& base, TchPipeBlock& b, int& handles, int& texts, bool well)
        {
            if (!ReadPipeNode(r, handles, texts)) return false;
            const int version = r.ReadByte();
            if (version >= 4) return false;
            ++texts;
            const auto at = Read3(r);
            Read3(r);
            const auto dir = Read3(r);
            const double scale = r.ReadBitDouble();
            r.ReadBit();
            const int mirror = r.ReadByte();
            r.ReadBitDouble(); r.ReadBitDouble();
            double scaleY = 0;
            if (version > 1)
                for (int i = 0; i < 4; ++i) { const double v = r.ReadBitDouble(); if (i == 1) scaleY = v; }
            if (version > 2) r.ReadBit();
            const double k = base.Scale / (well ? 1 : 100);
            return !r.Failed() && PlaceEquipment(b, at, dir, scale * k, scaleY * k, mirror);
        }

        inline std::vector<MiniDWG::Handle> ReadBlockHandles(const MiniDWG::UnknownEntity& entity, int skip, int count)
        {
            const auto& data = entity.Raw.Dwg;
            MiniDWG::DwgBitReader handles(data.Handles, data.Version, MiniDWG::Codec::CodePage::Gbk);
            for (int i = 0; i < skip; ++i) handles.ReadHandle(entity.ObjectHandle);
            std::vector<MiniDWG::Handle> result;
            for (int i = 0; i < count; ++i) result.push_back(handles.ReadHandle(entity.ObjectHandle));
            if (handles.Failed()) result.clear();
            return result;
        }

        struct CipherPipeNode
        {
            std::vector<MiniDWG::Handle> Pipes;
            std::vector<TchGraphics::Point> Directions;
            std::string Last;
        };

        // 新版管线节点基类 版本 1 无加密层；管线接口版本 3 起自带 RC 密钥（第二个密钥为 0）
        inline bool ReadCipherPipeNode(CipherReader& f, CipherPipeNode& node)
        {
            if (!f.ReadBase() || f.Main.ReadByte() >= 2) return false;
            f.Main.ReadByte();
            const int count = f.Main.ReadByte();
            for (int i = 0; i < count && f.Ok(); ++i)
            {
                CipherLayer layer(f);
                const int version = layer.Version(4, 3);
                if (version < 0) return false;
                for (int k = 0; k < 4; ++k) f.Byte();
                f.Text();
                node.Pipes.push_back(f.Handle());
                f.Point3();
                const auto dir = f.Point3();
                node.Directions.push_back({dir.X, dir.Y});
                if (version > 1) f.Text();
            }
            node.Last = f.Text();
            return f.Ok();
        }

        // 新版 TCH_EQUIPMENT 版本 4～5
        inline bool ReadCipherEquipment(CipherReader& f, TchPipeBlock& b, bool well)
        {
            CipherPipeNode node;
            if (!ReadCipherPipeNode(f, node)) return false;
            const int version = f.Main.ReadByte();
            if (version < 4 || version > 5) return false;
            f.ReadKey();
            b.Blocks.push_back(f.Handle());
            b.Blocks.push_back(f.Handle());
            f.Text();
            const auto at = f.Point3();
            f.Point3();
            const auto dir = f.Point3();
            const double scale = f.Double();
            f.Bool();
            const int mirror = f.Byte();
            double scaleY = 0;
            for (int i = 0; i < 6; ++i) { const double v = f.Double(); if (i == 3) scaleY = v; }
            f.Bool();
            if (version > 4) { f.Byte(); f.Byte(); f.Handle(); f.Byte(); }
            const double k = f.Scale / (well ? 1 : 100);
            return f.Ok() && PlaceEquipment(b, at, dir, scale * k, scaleY * k, mirror);
        }

        // 新版管线附件 版本 2
        inline bool ReadCipherPipeAttachment(CipherReader& f, MiniDWG::XYZ& at, MiniDWG::XYZ& dir)
        {
            if (!f.ReadBase() || f.Main.ReadByte() != 2) return false;
            f.ReadKey();
            f.Byte();
            at = f.Point3();
            dir = f.Point3();
            f.Handle();
            return f.Ok();
        }

        // 新版标注样式 版本 2～3：自带 RC 密钥
        inline bool ReadCipherLabelStyle(CipherReader& f, MiniDWG::Handle& style, double& height)
        {
            CipherLayer layer(f);
            if (layer.Version(3, 2) < 0) return false;
            style = f.Handle();
            height = f.Double();
            f.Double();
            return f.Ok();
        }

        // 新版管线原型 版本 2、TCH_PIPE 版本 3～4、TCH_DOUBLELINEPIPE 版本 3：字段与明文版本相同
        inline const char* ReadCipherPipe(CipherReader& f, TchPipe& p, bool doubleLine)
        {
            if (!f.ReadBase()) return "unsupported Tianzheng base layout";
            if (f.Main.ReadByte() != 2) return "unsupported native pipe prototype version";
            f.ReadKey();
            p.Scale = f.Scale;
            f.Byte(); f.Byte(); f.Byte();
            p.Start = f.Point3();
            p.End = f.Point3();
            p.Sweep = f.Double();
            f.Text();
            f.Handle(); f.Byte(); f.Handle(); f.Byte();
            auto handles = [&] { for (int i = 0, n = f.Byte(); i < n && f.Ok(); ++i) f.Handle(); };
            handles();
            f.Byte();
            handles();
            p.LabelStyle = f.Handle();
            p.LabelLayer = f.Handle();
            p.LabelHeight = f.Double();
            const auto label = f.Point3();
            p.LabelX = label.X; p.LabelY = label.Y;
            p.Label = f.Bool();
            const int version = f.Main.ReadByte();
            if (version < 3 || version > 4) return "unsupported native pipe version";
            f.ReadKey();
            p.RiserRadius = f.Double();
            f.Double();
            p.Diameter = f.Double();
            f.Double();
            f.Short();
            p.LabelPrefix = f.Text();
            p.VisibleMiddle = f.Double();
            p.VisibleLength = f.Double();
            f.Double();
            p.LabelSuffix = f.Text();
            if (version > 3)
            {
                f.Text();
                for (int i = 0, n = f.Byte(); i < n && f.Ok(); ++i) { f.Text(); f.Byte(); }
            }
            if (doubleLine)
            {
                if (f.Main.ReadByte() != 3) return "unsupported native double-line pipe version";
                f.ReadKey();
                p.Width = f.Double();
                p.BendRatio = f.Double();
                f.Short();
                f.Double(); f.Double(); f.Double();
                p.CenterLayer = f.Handle();
            }
            return f.Exact() ? nullptr : "native pipe fields do not consume the payload exactly";
        }
    }
     
    inline bool DecodePipe(const MiniDWG::UnknownEntity& entity, TchPipe& pipe, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const char* why = nullptr;
        auto fail = [&](const char* w) { if (diagnostic) *diagnostic = w; return false; };
        const bool doubleLine = entity.Raw.DxfName == "TCH_DOUBLELINEPIPE";
        if (!Detail::CheckPayload(entity, doubleLine ? "TCH_DOUBLELINEPIPE" : "TCH_PIPE", why)) return why ? fail(why) : false;
        const auto& data = entity.Raw.Dwg;
        TchPipe p;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (const char* w = Detail::ReadCipherPipe(f, p, doubleLine)) return fail(w);
            if (doubleLine && (!(p.Width > 0) || !Detail::Finite(p.Width))) return fail("invalid native double-line pipe width");
        }
        else
        {
            DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
            TchBase base;
            if (!ReadTchBase(r, base, &data)) return fail("unsupported Tianzheng base layout");
            if (r.ReadByte() >= 2) return fail("unsupported native pipe prototype version");
            p.Scale = base.Scale;
            r.ReadByte(); r.ReadByte(); r.ReadByte();
            p.Start = Detail::Read3(r);
            p.End = Detail::Read3(r);
            p.Sweep = r.ReadBitDouble();
            int handles = 2;                                    // 两端连接的管件
            r.ReadByte(); r.ReadByte();
            handles += r.ReadByte();
            r.ReadByte();
            handles += r.ReadByte();
            p.LabelHeight = r.ReadBitDouble();
            const auto label = Detail::Read3(r);
            p.LabelX = label.X; p.LabelY = label.Y;
            p.Label = r.ReadBit() != 0;
            const int version = r.ReadByte();
            if (version >= 3) return fail("unsupported native pipe version");
            p.RiserRadius = r.ReadBitDouble();
            r.ReadBitDouble();
            p.Diameter = r.ReadBitDouble();
            r.ReadBitDouble();
            r.ReadBitShort();
            p.VisibleMiddle = r.ReadBitDouble();
            p.VisibleLength = r.ReadBitDouble();
            if (version > 1) r.ReadBitDouble();
            int doubleVersion = 0;
            if (doubleLine)
            {
                // 双线管（版本 < 3）：BD 管宽、BD 弯头半径与管宽之比、BS、3 × BD，版本 ≥ 2 再硬指针（中心线图层）
                doubleVersion = r.ReadByte();
                if (doubleVersion >= 3) return fail("unsupported native double-line pipe version");
                p.Width = r.ReadBitDouble();
                p.BendRatio = r.ReadBitDouble();
                r.ReadBitShort();
                r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble();
                if (!(p.Width > 0) || !Detail::Finite(p.Width)) return fail("invalid native double-line pipe width");
            }
            if (r.Failed() || r.PositionInBits() != data.MainBits)
                return fail("native pipe fields do not consume the payload exactly");

            // 文字流：基类之后为原型的字符串、管线的标注前缀，版本 ≥ 2 时还有后缀
            Detail::TextStream stream(data, r);
            for (int i = 0; i < base.TextCount + 1; ++i) stream.Next();
            p.LabelPrefix = stream.Next();
            if (version > 1) p.LabelSuffix = stream.Next();
            if (stream.Reader.Failed()) return fail("native pipe text stream is incomplete");
            const auto hs = Detail::ReadBlockHandles(entity, base.HandleCount + handles, doubleVersion >= 2 ? 3 : 2);
            if (hs.size() < 2) return fail("native pipe handle stream is incomplete");
            p.LabelStyle = hs[0];
            p.LabelLayer = hs[1];
            if (hs.size() > 2) p.CenterLayer = hs[2];
        }
        if (!Detail::Finite(p.Start.X) || !Detail::Finite(p.Start.Y) || !Detail::Finite(p.End.X) || !Detail::Finite(p.End.Y) ||
            !Detail::Finite(p.Sweep) || std::abs(p.Sweep) >= 2 * std::numbers::pi || !Detail::Finite(p.RiserRadius) || !(p.Scale > 0))
            return fail("invalid native pipe geometry");
        if (p.Label && !(p.LabelHeight > 0 && Detail::Finite(p.LabelX) && Detail::Finite(p.LabelY))) p.Label = false;
        pipe = std::move(p);
        if (diagnostic) diagnostic->clear();
        return true;
    }
     
    inline TchGraphics BuildPipe(const TchPipe& p, const TchTextStyleRule& rule = {})
    {
        TchGraphics g;
        if (p.IsRiser())
        {
            if (p.RiserRadius > 0) g.Circles.push_back({{p.Start.X, p.Start.Y}, p.RiserRadius * p.Scale});
        }
        else if (std::abs(p.Sweep) > 1e-9)
        {
            const double dx = p.End.X - p.Start.X, dy = p.End.Y - p.Start.Y, chord = std::hypot(dx, dy);
            const double half = std::abs(p.Sweep) / 2, radius = chord / 2 / std::sin(half);
            const double toCenter = chord / 2 / std::tan(half) * (p.Sweep > 0 ? 1 : -1);
            const double cx = (p.Start.X + p.End.X) / 2 - dy / chord * toCenter, cy = (p.Start.Y + p.End.Y) / 2 + dx / chord * toCenter;
            double a0 = std::atan2(p.Start.Y - cy, p.Start.X - cx), a1 = std::atan2(p.End.Y - cy, p.End.X - cx);
            if (p.Sweep < 0) std::swap(a0, a1);
            g.Arcs.push_back({{cx, cy}, radius, a0, a1});
        }
        else
            g.Lines.push_back({{p.Start.X, p.Start.Y}, {p.End.X, p.End.Y}});
        if (p.Label)
        {
            char number[32];
            std::snprintf(number, sizeof number, "%g", p.Diameter);
            double angle = p.IsRiser() ? 0 : std::atan2(p.End.Y - p.Start.Y, p.End.X - p.Start.X);
            if (angle > std::numbers::pi / 2 + 1e-9) angle -= std::numbers::pi;
            else if (angle <= -std::numbers::pi / 2 + 1e-9) angle += std::numbers::pi;
            TchText t;
            t.X = p.LabelX; t.Y = p.LabelY;
            t.Height = p.LabelHeight;
            t.Rotation = angle;
            t.Scale = p.Scale;
            t.Attachment = 5;
            t.Value = p.LabelPrefix + number + p.LabelSuffix;
            for (auto text : BuildText(t, rule).Texts)
            {
                text.Layer = 1;
                text.Color = 256;
                g.Texts.push_back(std::move(text));
            }
        }
        return g;
    }

    inline bool DecodePipeFitting(const MiniDWG::UnknownEntity& entity, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const char* why = nullptr;
        auto fail = [&](const char* w) { if (diagnostic) *diagnostic = w; return false; };
        if (!Detail::CheckPayload(entity, "TCH_PIPEFITTING", why)) return why ? fail(why) : false;
        const auto& data = entity.Raw.Dwg;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            Detail::CipherPipeNode node;
            if (!Detail::ReadCipherPipeNode(f, node)) return fail("unsupported native pipe node layout");
            if (f.Main.ReadByte() != 2) return fail("unsupported native pipe fitting version");
            f.ReadKey();
            f.Point3();
            if (!f.Exact()) return fail("native pipe fitting fields do not consume the payload exactly");
            if (diagnostic) diagnostic->clear();
            return true;
        }
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base, &data)) return fail("unsupported Tianzheng base layout");
        int handles = 0, texts = 0;
        if (!Detail::ReadPipeNode(r, handles, texts)) return fail("unsupported native pipe node layout");
        if (r.ReadByte() >= 2) return fail("unsupported native pipe fitting version");
        Detail::Read3(r);
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native pipe fitting fields do not consume the payload exactly");
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline bool DecodeEquipment(const MiniDWG::UnknownEntity& entity, TchPipeBlock& block, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const char* why = nullptr;
        auto fail = [&](const char* w) { if (diagnostic) *diagnostic = w; return false; };
        const bool well = entity.Raw.DxfName == "TCH_WELL", pump = entity.Raw.DxfName == "TCH_PUMP";
        if (!Detail::CheckPayload(entity, well ? "TCH_WELL" : pump ? "TCH_PUMP" : "TCH_EQUIPMENT", why)) return why ? fail(why) : false;
        const auto& data = entity.Raw.Dwg;
        TchPipeBlock b;
        double size[6] = {};
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!Detail::ReadCipherEquipment(f, b, well)) return fail("unsupported native equipment layout");
            if (well)
            {
                MiniDWG::Handle style;
                double height = 0;
                if (!Detail::ReadCipherLabelStyle(f, style, height)) return fail("unsupported native well label style");
                if (f.Main.ReadByte() != 3) return fail("unsupported native well version");
                f.ReadKey();
                f.Text();
                f.Point3();
                b.WellInvert = f.Double();
                b.WellGround = f.Double();
                f.Double();
                f.Bool();
                f.Handle();
            }
            if (pump)
            {
                const int version = f.Main.ReadByte();
                if (version < 3 || version > 4) return fail("unsupported native pump version");
                f.ReadKey();
                for (int i = 0; i < 4; ++i) f.Text();
                for (double& v : size) v = f.Double();
                if (version > 3) f.Double();
            }
            if (!f.Exact()) return fail("native equipment fields do not consume the payload exactly");
        }
        else
        {
            DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
            TchBase base;
            if (!ReadTchBase(r, base, &data)) return fail("unsupported Tianzheng base layout");
            int handles = 0, texts = 0;
            if (!Detail::ReadEquipment(r, base, b, handles, texts, well)) return fail("unsupported native equipment layout");
            if (well)
            {
                if (r.ReadByte() >= 2) return fail("unsupported native well label style");
                r.ReadBitDouble(); r.ReadBitDouble();
                r.ReadByte();
                Detail::Read3(r);
                b.WellInvert = r.ReadBitDouble();               // 井底标高、井面标高（井标注 TCH_WELLDIM 显示这两个值）
                b.WellGround = r.ReadBitDouble();
                r.ReadBitDouble();
                r.ReadBit();
            }
            if (pump)
            {
                r.ReadByte();
                for (double& v : size) v = r.ReadBitDouble();
            }
            if (r.Failed() || r.PositionInBits() != data.MainBits)
                return fail("native equipment fields do not consume the payload exactly");
            b.Blocks = Detail::ReadBlockHandles(entity, base.HandleCount + handles, 2);
        }
        if (pump && size[3] > 0 && size[4] > 0 && Detail::Finite(size[3]) && Detail::Finite(size[4]))
        {
            b.ScaleX = b.ScaleX < 0 ? -size[3] : size[3];
            b.ScaleY = b.ScaleY < 0 ? -size[4] : size[4];
        }
        if (b.Blocks.empty()) return fail("native equipment handle stream is incomplete");
        block = std::move(b);
        if (diagnostic) diagnostic->clear();
        return true;
    } 

    inline bool DecodePipeValve(const MiniDWG::UnknownEntity& entity, TchPipeBlock& block, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const char* why = nullptr;
        auto fail = [&](const char* w) { if (diagnostic) *diagnostic = w; return false; };
        if (!Detail::CheckPayload(entity, "TCH_PIPEVALVE", why)) return why ? fail(why) : false;
        const auto& data = entity.Raw.Dwg;
        XYZ at, dir;
        double scale = 0, scaleY = 0, baseScale = 100;
        int quadrant = 0;
        std::vector<Handle> blocks;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!Detail::ReadCipherPipeAttachment(f, at, dir)) return fail("unsupported native pipe attachment version");
            if (f.Main.ReadByte() != 3) return fail("unsupported native valve version");
            f.ReadKey();
            blocks.push_back(f.Handle());
            blocks.push_back(f.Handle());
            f.Text();
            scale = f.Double();
            f.Double();
            f.Bool();
            quadrant = f.Byte();
            scaleY = f.Double();
            for (int i = 0; i < 3; ++i) f.Double();
            f.Bool();
            if (!f.Exact()) return fail("native valve fields do not consume the payload exactly");
            baseScale = f.Scale;
        }
        else
        {
            DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
            TchBase base;
            if (!ReadTchBase(r, base, &data)) return fail("unsupported Tianzheng base layout");
            if (r.ReadByte() >= 2) return fail("unsupported native pipe attachment version");
            r.ReadByte();
            at = Detail::Read3(r);
            dir = Detail::Read3(r);
            const int version = r.ReadByte();
            if (version >= 3) return fail("unsupported native valve version");
            scale = r.ReadBitDouble();
            r.ReadBitDouble();
            r.ReadBit();
            quadrant = r.ReadByte();
            if (version >= 2)
            {
                scaleY = r.ReadBitDouble();
                for (int i = 0; i < 3; ++i) r.ReadBitDouble();
                r.ReadBit();
            }
            if (r.Failed() || r.PositionInBits() != data.MainBits)
                return fail("native valve fields do not consume the payload exactly");
            baseScale = base.Scale;
            blocks = Detail::ReadBlockHandles(entity, base.HandleCount + 1, 2);
        }
        TchPipeBlock b;
        const double s = scale * baseScale / 100;
        if (!(std::abs(scaleY) >= 1e-3) || !Detail::Finite(scaleY)) scaleY = scale;
        b.X = at.X; b.Y = at.Y;
        b.Rotation = std::atan2(dir.Y, dir.X);
        b.ScaleX = quadrant == 1 || quadrant == 2 ? -s : s;
        b.ScaleY = (quadrant == 2 || quadrant == 3 ? -scaleY : scaleY) * baseScale / 100;
        if (!Detail::Finite(b.X) || !Detail::Finite(b.Y) || !Detail::Finite(b.Rotation) || !Detail::Finite(s) || std::abs(s) < 1e-9)
            return fail("invalid native valve placement");
        b.Blocks = std::move(blocks);
        if (b.Blocks.empty()) return fail("native valve handle stream is incomplete");
        block = std::move(b);
        if (diagnostic) diagnostic->clear();
        return true;
    }


    namespace Detail
    {
        struct PropertyValues
        {
            std::map<std::string, std::string> Strings;
            std::map<std::string, double>      Numbers;
        };

        inline bool ReadBagValues(MiniDWG::DwgBitReader& r, TextStream& text, PropertyValues& values, int& handles, int depth = 0)
        {
            if (depth > 8) return false;
            const int count = r.ReadBitLong();
            if (count < 0 || count > 4096) return false;
            for (int i = 0; i < count && !r.Failed(); ++i)
            {
                const std::string name = text.Next();
                switch (r.ReadBitLong())
                {
                case 0: r.ReadBit(); break;
                case 1: case 2: case 3: r.ReadByte(); break;
                case 4: case 5: r.ReadBitShort(); break;
                case 6: case 7: r.ReadBitLong(); break;
                case 100: if (!ReadBagValues(r, text, values, handles, depth + 1)) return false; break;
                case 0x65: values.Strings[name] = text.Next(); break;
                case 200: values.Numbers[name] = r.ReadBitDouble(); break;
                case 0xc9: case 0xcb: r.ReadRawDouble(); r.ReadRawDouble(); break;
                case 0xca: case 0xcc: case 0xcd: r.ReadBitDouble(); r.ReadBitDouble(); r.ReadBitDouble(); break;
                case 0xd0: case 0xd1: ++handles; break;
                default: return false;
                }
            }
            return !r.Failed();
        }

        inline bool ReadTchBaseValues(MiniDWG::DwgBitReader& r, TchBase& base, TextStream& text, PropertyValues& values, double& angle)
        {
            base = {};
            angle = 0;
            base.Version = r.ReadByte();
            if (base.Version < 1 || base.Version > 5) return false;
            if (base.Version > 1) base.Scale = r.ReadBitDouble();
            if (base.Version > 2) angle = r.ReadBitDouble();
            if (base.Version > 3)
            {
                const int flags = r.ReadByte();
                if (flags & 1) base.HandleCount += r.ReadByte();
                if (flags & 2) base.HandleCount += r.ReadByte();
            }
            if (base.Version > 4 && !ReadBagValues(r, text, values, base.HandleCount)) return false;
            return !r.Failed();
        }

        struct PipeSymbol
        {
            TchBase Base;
            PropertyValues Values;
            double Angle = 0, TextHeight = 3.5;
            int    Color = 7, HandleCount = 0;
            MiniDWG::Handle Style = MiniDWG::kNullHandle;
        };

        inline bool ReadPipeSymbol(const MiniDWG::UnknownEntity& entity, MiniDWG::DwgBitReader& r, TextStream& text, PipeSymbol& s)
        {
            if (!ReadTchBaseValues(r, s.Base, text, s.Values, s.Angle)) return false;
            const int version = r.ReadByte();
            if (version < 1 || version > 3) return false;
            s.TextHeight = r.ReadBitDouble();
            s.HandleCount = s.Base.HandleCount + 1;
            if (version >= 2) { r.ReadBitDouble(); ++s.HandleCount; }
            if (version > 2) s.Color = r.ReadBitShort();
            if (s.Color < 0 || s.Color > 256) s.Color = 7;
            const auto hs = ReadBlockHandles(entity, s.Base.HandleCount, 1);
            if (hs.empty()) return false;
            s.Style = hs[0];
            return !r.Failed() && s.TextHeight > 0 && Finite(s.TextHeight) && s.Base.Scale > 0;
        }

        inline bool ReadCipherPipeSymbol(CipherReader& f, PipeSymbol& s)
        {
            TchSymbolBase symbol;
            if (!ReadCipherSymbolBase(f, symbol, s.Style)) return false;
            s.Base = symbol.Base;
            s.TextHeight = symbol.TextHeight;
            s.Color = symbol.Color < 0 || symbol.Color > 256 ? 7 : symbol.Color;
            s.Angle = f.Angle;
            s.Values.Strings = f.FieldStrings;
            s.Values.Numbers = f.FieldNumbers;
            return s.TextHeight > 0 && Finite(s.TextHeight) && s.Base.Scale > 0;
        }

        inline TchGraphics::Point P2(MiniDWG::DwgBitReader& r) { const double x = r.ReadRawDouble(); return {x, r.ReadRawDouble()}; }

        inline double Upright(double a)
        {
            a = std::remainder(a, 2 * std::numbers::pi);
            if (a > std::numbers::pi / 2 + 1e-9) a -= std::numbers::pi;
            else if (a <= -std::numbers::pi / 2 + 1e-9) a += std::numbers::pi;
            return a;
        }
    }

    struct TchPipeText
    {
        double X = 0, Y = 0, Rotation = 0, Height = 3, Scale = 100;
        std::string Value;
        MiniDWG::Handle Style = MiniDWG::kNullHandle;
    };

    inline bool DecodePipeText(const MiniDWG::UnknownEntity& entity, TchPipeText& text, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const char* why = nullptr;
        auto fail = [&](const char* w) { if (diagnostic) *diagnostic = w; return false; };
        if (!Detail::CheckPayload(entity, "TCH_PIPETEXT", why)) return why ? fail(why) : false;
        const auto& data = entity.Raw.Dwg;
        TchPipeText t;
        XYZ at, dir;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!Detail::ReadCipherPipeAttachment(f, at, dir)) return fail("unsupported native pipe attachment version");
            if (!Detail::ReadCipherLabelStyle(f, t.Style, t.Height)) return fail("unsupported native label style version");
            const int version = f.Main.ReadByte();
            if (version > 2) return fail("unsupported native pipe text version");
            if (version > 1) f.Main.ReadByte();
            t.Value = f.Text();
            if (!f.Exact()) return fail("native pipe text fields do not consume the payload exactly");
            t.Scale = f.Scale;
        }
        else
        {
            DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
            TchBase base;
            if (!ReadTchBase(r, base, &data)) return fail("unsupported Tianzheng base layout");
            if (r.ReadByte() >= 2) return fail("unsupported native pipe attachment version");
            r.ReadByte();
            at = Detail::Read3(r);
            dir = Detail::Read3(r);
            if (r.ReadByte() >= 2) return fail("unsupported native label style version");
            t.Height = r.ReadBitDouble();
            r.ReadBitDouble();
            if (r.ReadByte() >= 2) return fail("unsupported native pipe text version");
            if (r.Failed() || r.PositionInBits() != data.MainBits)
                return fail("native pipe text fields do not consume the payload exactly");
            t.Scale = base.Scale;
            Detail::TextStream stream(data, r);
            for (int i = 0; i < base.TextCount; ++i) stream.Next();
            t.Value = stream.Next();
            if (stream.Reader.Failed()) return fail("native pipe text stream is incomplete");
            const auto hs = Detail::ReadBlockHandles(entity, base.HandleCount + 1, 1);
            if (hs.empty()) return fail("native pipe text handle stream is incomplete");
            t.Style = hs[0];
        }
        t.X = at.X; t.Y = at.Y;
        t.Rotation = Detail::Upright(std::atan2(dir.Y, dir.X));
        if (!Detail::Finite(t.X) || !Detail::Finite(t.Y) || !(t.Height > 0) || !(t.Scale > 0)) return fail("invalid native pipe text settings");
        text = std::move(t);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildPipeText(const TchPipeText& t, const TchTextStyleRule& rule)
    {
        TchGraphics g;
        const double h = t.Height * t.Scale * rule.HeightFactor();
        g.Texts.push_back({{t.X, t.Y}, h, t.Rotation, t.Value, 5, 0, 256, 0, rule.WidthFactor()});
        return g;
    }

    struct TchVPipeDim
    {
        double X1 = 0, Y1 = 0, X2 = 0, Y2 = 0;  // 立管点、转折点
        double Run = 0;                         // 水平段长（世界单位，带方向：负为向左）
        double TextHeight = 3, Scale = 100;
        int    Color = 7;
        std::string Value;
        MiniDWG::Handle Style = MiniDWG::kNullHandle;
    };

    inline bool DecodeVPipeDim(const MiniDWG::UnknownEntity& entity, TchVPipeDim& dim, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const char* why = nullptr;
        auto fail = [&](const char* w) { if (diagnostic) *diagnostic = w; return false; };
        if (!Detail::CheckPayload(entity, "TCH_VPIPEDIM", why)) return why ? fail(why) : false;
        const auto& data = entity.Raw.Dwg;
        Detail::PipeSymbol s;
        XYZ a, b, dir;
        double run = 0;
        std::string own;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!Detail::ReadCipherPipeSymbol(f, s)) return fail("unsupported native symbol base layout");
            if (f.Main.ReadByte() != 2) return fail("unsupported native riser dimension version");
            f.ReadKey();
            f.Short(); f.Short();
            a = f.Point3(); b = f.Point3(); dir = f.Point3();
            run = f.Double();
            f.Double();
            f.Handle();
            own = f.Text();
            if (!f.Exact()) return fail("native riser dimension fields do not consume the payload exactly");
        }
        else
        {
            DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
            Detail::TextStream stream(data, r);
            if (!Detail::ReadPipeSymbol(entity, r, stream, s)) return fail("unsupported native symbol base layout");
            if (r.ReadByte() >= 2) return fail("unsupported native riser dimension version");
            r.ReadBitShort(); r.ReadBitShort();
            a = Detail::Read3(r); b = Detail::Read3(r); dir = Detail::Read3(r);
            run = r.ReadBitDouble();
            r.ReadBitDouble();
            if (r.Failed() || r.PositionInBits() != data.MainBits)
                return fail("native riser dimension fields do not consume the payload exactly");
            own = stream.Next();
            if (stream.Reader.Failed()) return fail("native riser dimension text stream is incomplete");
        }
        TchVPipeDim d;
        d.X1 = a.X; d.Y1 = a.Y; d.X2 = b.X; d.Y2 = b.Y;
        d.Scale = s.Base.Scale; d.TextHeight = s.TextHeight; d.Color = s.Color; d.Style = s.Style;
        d.Run = run * d.Scale * (dir.X < 0 ? -1 : 1);
        const auto text = s.Values.Strings.find("VPIPE_DIM_TEXT");
        const auto floor = s.Values.Strings.find("VPIPE_DIM_FLOOR");
        d.Value = (floor != s.Values.Strings.end() ? floor->second : std::string()) + (text != s.Values.Strings.end() ? text->second : own);
        if (!Detail::Finite(d.X1) || !Detail::Finite(d.Y1) || !Detail::Finite(d.X2) || !Detail::Finite(d.Y2) || !Detail::Finite(d.Run))
            return fail("invalid native riser dimension geometry");
        dim = std::move(d);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildVPipeDim(const TchVPipeDim& d, const TchTextStyleRule& rule)
    {
        TchGraphics g;
        const double n = d.TextHeight * d.Scale, endX = d.X2 + d.Run;
        g.Lines.push_back({{d.X1, d.Y1}, {d.X2, d.Y2}});
        g.Lines.push_back({{d.X2, d.Y2}, {endX, d.Y2}});
        const bool left = d.Run < 0;
        g.Texts.push_back({{endX + (left ? 0.3 : -0.3) * n, d.Y2 + 0.05 * n}, n * rule.HeightFactor(), 0, d.Value,
                           left ? 7 : 9, 0, d.Color, 0, rule.WidthFactor()});
        return g;
    }

    struct TchWellDim
    {
        double X1 = 0, Y1 = 0, X2 = 0, Y2 = 0, Run = 0;
        double TextHeight = 2, Scale = 1;
        int    Color = 7;
        MiniDWG::Handle Style = MiniDWG::kNullHandle, Well = MiniDWG::kNullHandle;
    };

    inline bool DecodeWellDim(const MiniDWG::UnknownEntity& entity, TchWellDim& dim, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const char* why = nullptr;
        auto fail = [&](const char* w) { if (diagnostic) *diagnostic = w; return false; };
        if (!Detail::CheckPayload(entity, "TCH_WELLDIM", why)) return why ? fail(why) : false;
        const auto& data = entity.Raw.Dwg;
        Detail::PipeSymbol s;
        TchGraphics::Point a, b;
        double run = 0;
        Handle well = kNullHandle;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!Detail::ReadCipherPipeSymbol(f, s)) return fail("unsupported native symbol base layout");
            if (f.Main.ReadByte() != 2) return fail("unsupported native well dimension version");
            f.ReadKey();
            f.Short();
            run = f.Double();
            const auto p = f.Point2(), q = f.Point2();
            a = {p.X, p.Y}; b = {q.X, q.Y};
            well = f.Handle();
            if (!f.Exact()) return fail("native well dimension fields do not consume the payload exactly");
        }
        else
        {
            DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
            Detail::TextStream stream(data, r);
            if (!Detail::ReadPipeSymbol(entity, r, stream, s)) return fail("unsupported native symbol base layout");
            if (r.ReadByte() >= 2) return fail("unsupported native well dimension version");
            r.ReadBitShort();
            run = r.ReadBitDouble();
            a = Detail::P2(r); b = Detail::P2(r);
            if (r.Failed() || r.PositionInBits() != data.MainBits)
                return fail("native well dimension fields do not consume the payload exactly");
            const auto hs = Detail::ReadBlockHandles(entity, s.HandleCount, 1);
            if (hs.empty()) return fail("native well dimension handle stream is incomplete");
            well = hs[0];
        }
        TchWellDim d;
        d.X1 = a.X; d.Y1 = a.Y; d.X2 = b.X; d.Y2 = b.Y;
        d.Scale = s.Base.Scale; d.TextHeight = s.TextHeight; d.Color = s.Color; d.Style = s.Style;
        d.Run = run * d.Scale;
        d.Well = well;
        if (!Detail::Finite(d.X1) || !Detail::Finite(d.Y1) || !Detail::Finite(d.X2) || !Detail::Finite(d.Y2) || !Detail::Finite(d.Run))
            return fail("invalid native well dimension geometry");
        dim = std::move(d);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildWellDim(const TchWellDim& d, double ground, double invert, const TchTextStyleRule& rule)
    {
        TchGraphics g;
        g.LineColor = 6;
        const double n = d.TextHeight * d.Scale, endX = d.X2 + d.Run, mid = (d.X2 + endX) / 2;
        g.Lines.push_back({{d.X1, d.Y1}, {d.X2, d.Y2}});
        g.Lines.push_back({{d.X2, d.Y2}, {endX, d.Y2}});
        char top[32], bottom[32];
        std::snprintf(top, sizeof top, "%.3f", ground);
        std::snprintf(bottom, sizeof bottom, "%.3f", invert);
        const double h = n * rule.HeightFactor();
        g.Texts.push_back({{mid, d.Y2 + 0.5 * n}, h, 0, top, 8, 0, d.Color, 0, rule.WidthFactor()});
        g.Texts.push_back({{mid, d.Y2 - 0.5 * n}, h, 0, bottom, 2, 0, d.Color, 0, rule.WidthFactor()});
        return g;
    }

    struct TchPipeMultDim
    {
        TchGraphics::Point P1, P2;
        double Run = 0, Angle = 0, TextHeight = 3, Spacing = 3.5, Scale = 100;
        int    Color = 7;
        std::vector<MiniDWG::Handle> Pipes;
        MiniDWG::Handle Style = MiniDWG::kNullHandle;
    };

    inline bool DecodePipeMultDim(const MiniDWG::UnknownEntity& entity, TchPipeMultDim& dim, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const char* why = nullptr;
        auto fail = [&](const char* w) { if (diagnostic) *diagnostic = w; return false; };
        if (!Detail::CheckPayload(entity, "TCH_PIPEMULTDIM", why)) return why ? fail(why) : false;
        const auto& data = entity.Raw.Dwg;
        Detail::PipeSymbol s;
        TchPipeMultDim d;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader f(entity);
            if (!Detail::ReadCipherPipeSymbol(f, s)) return fail("unsupported native symbol base layout");
            if (f.Main.ReadByte() != 3) return fail("unsupported native multi-pipe dimension version");
            f.ReadKey();
            for (int i = 0, n = f.Byte(); i < n && f.Ok(); ++i) d.Pipes.push_back(f.Handle());
            d.Run = f.Double();
            const auto p = f.Point2(), q = f.Point2();
            d.P1 = {p.X, p.Y}; d.P2 = {q.X, q.Y};
            f.Short();
            f.Text();
            if (!f.Exact()) return fail("native multi-pipe dimension fields do not consume the payload exactly");
        }
        else
        {
            DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
            Detail::TextStream stream(data, r);
            if (!Detail::ReadPipeSymbol(entity, r, stream, s)) return fail("unsupported native symbol base layout");
            const int version = r.ReadByte();
            if (version >= 3) return fail("unsupported native multi-pipe dimension version");
            const int count = r.ReadByte();
            d.Run = r.ReadBitDouble();
            d.P1 = Detail::P2(r);
            d.P2 = Detail::P2(r);
            if (version > 1) r.ReadBitShort();
            if (r.Failed() || r.PositionInBits() != data.MainBits)
                return fail("native multi-pipe dimension fields do not consume the payload exactly");
            d.Pipes = Detail::ReadBlockHandles(entity, s.HandleCount, count);
            if (static_cast<int>(d.Pipes.size()) != count) return fail("native multi-pipe dimension handle stream is incomplete");
        }
        d.Scale = s.Base.Scale; d.TextHeight = s.TextHeight; d.Color = s.Color; d.Style = s.Style; d.Angle = s.Angle;
        d.Run *= d.Scale;
        double gap = 0.5;
        if (const auto it = s.Values.Numbers.find("\xe5\xa4\x9a\xe7\xae\xa1\xe6\xa0\x87\xe6\xb3\xa8\xe5\x9f\xba\xe7\xba\xbf\xe8\xb7\x9d\xe7\xa6\xbb");
            it != s.Values.Numbers.end() && Detail::Finite(it->second))
            gap = it->second;                                   // “多管标注基线距离”
        d.Spacing = (d.TextHeight + gap) * d.Scale;
        if (!Detail::Finite(d.P1.X) || !Detail::Finite(d.P1.Y) || !Detail::Finite(d.P2.X) || !Detail::Finite(d.P2.Y) ||
            !Detail::Finite(d.Run) || !Detail::Finite(d.Angle) || std::hypot(d.P2.X - d.P1.X, d.P2.Y - d.P1.Y) < 1e-9)
            return fail("invalid native multi-pipe dimension geometry");
        dim = std::move(d);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    inline TchGraphics BuildPipeMultDim(const TchPipeMultDim& d, const std::vector<std::string>& labels,
                                        const std::vector<std::optional<TchGraphics::Point>>& crossings, const TchTextStyleRule& rule)
    {
        TchGraphics g;
        const double n = d.TextHeight * d.Scale;
        const double len = std::hypot(d.P2.X - d.P1.X, d.P2.Y - d.P1.Y);
        const double ux = (d.P2.X - d.P1.X) / len, uy = (d.P2.Y - d.P1.Y) / len;
        g.Lines.push_back({d.P1, d.P2});
        const double tx = (ux - uy) / std::sqrt(2.0) * n / 2, ty = (ux + uy) / std::sqrt(2.0) * n / 2;
        for (const auto& c : crossings)
            if (c) g.Lines.push_back({{c->X - tx, c->Y - ty}, {c->X + tx, c->Y + ty}});
        const double wx = std::cos(d.Angle), wy = std::sin(d.Angle);
        const double h = n * rule.HeightFactor();
        for (std::size_t k = 0; k < labels.size(); ++k)
        {
            const TchGraphics::Point b{d.P2.X + ux * d.Spacing * k, d.P2.Y + uy * d.Spacing * k};
            g.Lines.push_back({b, {b.X + wx * d.Run, b.Y + wy * d.Run}});
            const double ox = n / 24, oy = n / 12;
            g.Texts.push_back({{b.X + wx * ox - wy * oy, b.Y + wy * ox + wx * oy}, h, d.Angle, labels[k], 7, 0, d.Color, 0,
                               rule.WidthFactor()});
        }
        if (labels.size() > 1)
            g.Lines.push_back({d.P2, {d.P2.X + ux * d.Spacing * (labels.size() - 1), d.P2.Y + uy * d.Spacing * (labels.size() - 1)}});
        return g;
    }

    inline std::optional<TchGraphics::Point> PipeCrossing(const TchPipe& p, TchGraphics::Point a, double ux, double uy)
    {
        if (std::abs(p.Sweep) > 1e-9 || p.IsRiser()) return std::nullopt;
        const double dx = p.End.X - p.Start.X, dy = p.End.Y - p.Start.Y;
        const double den = dx * uy - dy * ux;
        if (std::abs(den) < 1e-12) return std::nullopt;
        const double s = ((a.X - p.Start.X) * uy - (a.Y - p.Start.Y) * ux) / den;
        if (s < -1e-6 || s > 1 + 1e-6) return std::nullopt;
        return TchGraphics::Point{p.Start.X + dx * s, p.Start.Y + dy * s};
    }

    inline TchGraphics BuildDoubleLinePipe(const TchPipe& p)
    {
        TchGraphics g;
        const double half = p.Width / 2;
        const double dx = p.End.X - p.Start.X, dy = p.End.Y - p.Start.Y, length = std::hypot(dx, dy);
        if (length < 1e-9) return g;
        if (std::abs(p.Sweep) > 1e-9)
        {
            const auto arc = BuildPipe(p).Arcs;
            if (!arc.empty())
                for (const double off : {-half, half})
                    if (arc[0].Radius + off > 0) g.Arcs.push_back({arc[0].Center, arc[0].Radius + off, arc[0].Start, arc[0].End});
            if (!arc.empty())
            {
                const auto& a = arc[0];
                double sweep = a.End - a.Start;
                while (sweep <= 0) sweep += 2 * std::numbers::pi;
                TchGraphics::Path c;
                c.Layer = 1;
                c.Points.push_back({{a.Center.X + a.Radius * std::cos(a.Start), a.Center.Y + a.Radius * std::sin(a.Start)}, std::tan(sweep / 4)});
                c.Points.push_back({{a.Center.X + a.Radius * std::cos(a.End), a.Center.Y + a.Radius * std::sin(a.End)}, 0});
                g.Paths.push_back(std::move(c));
            }
            return g;
        }
        const double ux = dx / length, uy = dy / length;
        double t0 = 0, t1 = length;
        if (p.VisibleLength > 1e-9 && Detail::Finite(p.VisibleMiddle))
        {
            t0 = std::max(0.0, p.VisibleMiddle - p.VisibleLength / 2);
            t1 = std::min(length, p.VisibleMiddle + p.VisibleLength / 2);
        }
        if (t1 - t0 < 1e-9) return g;
        auto at = [&](double t, double off) { return TchGraphics::Point{p.Start.X + ux * t - uy * off, p.Start.Y + uy * t + ux * off}; };
        TchGraphics::Path center;
        center.Layer = 1;
        center.Points = {{at(t0, 0), 0}, {at(t1, 0), 0}};
        g.Paths.push_back(std::move(center));
        g.Lines.push_back({at(t0, half), at(t1, half)});
        g.Lines.push_back({at(t0, -half), at(t1, -half)});
        return g;
    }

    struct TchFlange
    {
        double X = 0, Y = 0, BendRatio = 1;
        std::vector<TchGraphics::Point> Directions;     // 各接口指向所连管线的方向
        std::vector<MiniDWG::Handle> Pipes;             // 各接口连接的管线
    };

    inline bool DecodeFlange(const MiniDWG::UnknownEntity& entity, TchFlange& flange, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const char* why = nullptr;
        auto fail = [&](const char* w) { if (diagnostic) *diagnostic = w; return false; };
        if (!Detail::CheckPayload(entity, "TCH_FLANGE", why)) return why ? fail(why) : false;
        const auto& data = entity.Raw.Dwg;
        TchFlange f;
        std::string ratio;
        if (Detail::IsCipherObject(data))
        {
            Detail::CipherReader c(entity);
            Detail::CipherPipeNode node;
            if (!Detail::ReadCipherPipeNode(c, node)) return fail("unsupported native pipe node layout");
            if (c.Main.ReadByte() != 2) return fail("unsupported native pipe fitting version");
            c.ReadKey();
            const auto at = c.Point3();
            if (c.Main.ReadByte() != 3) return fail("unsupported native flange version");
            c.ReadKey();
            c.Handle();
            if (!c.Exact()) return fail("native flange fields do not consume the payload exactly");
            f.X = at.X; f.Y = at.Y;
            f.Directions = std::move(node.Directions);
            f.Pipes = std::move(node.Pipes);
            ratio = std::move(node.Last);
        }
        else
        {
            DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
            TchBase base;
            if (!ReadTchBase(r, base, &data)) return fail("unsupported Tianzheng base layout");
            if (r.ReadByte() >= 2) return fail("unsupported native pipe node layout");
            r.ReadByte();
            const int count = r.ReadByte();
            for (int i = 0; i < count && !r.Failed(); ++i)
            {
                if (r.ReadByte() >= 2) return fail("unsupported native pipe interface version");
                for (int k = 0; k < 4; ++k) r.ReadByte();
                Detail::Read3(r);
                const auto dir = Detail::Read3(r);
                f.Directions.push_back({dir.X, dir.Y});
            }
            if (r.ReadByte() >= 2) return fail("unsupported native pipe fitting version");
            const auto at = Detail::Read3(r);
            if (r.ReadByte() >= 3) return fail("unsupported native flange version");
            if (r.Failed() || r.PositionInBits() != data.MainBits)
                return fail("native flange fields do not consume the payload exactly");
            f.X = at.X; f.Y = at.Y;
            Detail::TextStream stream(data, r);
            for (int i = 0; i < base.TextCount + count; ++i) stream.Next();
            ratio = stream.Next();
            if (stream.Reader.Failed()) return fail("native flange text stream is incomplete");
            f.Pipes = Detail::ReadBlockHandles(entity, base.HandleCount, count);
            if (static_cast<int>(f.Pipes.size()) != count) return fail("native flange handle stream is incomplete");
        }
        char* end = nullptr;
        const double v = std::strtod(ratio.c_str(), &end);
        if (end != ratio.c_str() && v > 0 && Detail::Finite(v)) f.BendRatio = v;
        flange = std::move(f);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 两个不平行接口的法兰画分节弯头（分解结果：中心圆弧，两侧各 3 节的折线、两端为半节，节间与两端画横线）：
    // 中心半径 R = 比例 × 管宽，切线长 R / tan(θ/2)。其他情况（单接口、三通）不显示
    inline TchGraphics BuildFlange(const TchFlange& f, double width)
    {
        TchGraphics g;
        if (f.Directions.size() != 2 || !(width > 0)) return g;
        auto unit = [](TchGraphics::Point d) { const double l = std::hypot(d.X, d.Y); return l > 1e-12 ? TchGraphics::Point{d.X / l, d.Y / l} : d; };
        const auto d1 = unit(f.Directions[0]), d2 = unit(f.Directions[1]);
        const double cross = d1.X * d2.Y - d1.Y * d2.X, dot = d1.X * d2.X + d1.Y * d2.Y;
        if (std::abs(cross) < 1e-6) return g;
        const double theta = std::acos(std::clamp(dot, -1.0, 1.0));
        const double R = f.BendRatio * width, t = R / std::tan(theta / 2);
        const double bx = d1.X + d2.X, by = d1.Y + d2.Y, bl = std::hypot(bx, by);
        const TchGraphics::Point o{f.X + bx / bl * R / std::sin(theta / 2), f.Y + by / bl * R / std::sin(theta / 2)};
        const TchGraphics::Point t1{f.X + d1.X * t, f.Y + d1.Y * t}, t2{f.X + d2.X * t, f.Y + d2.Y * t};
        double a1 = std::atan2(t1.Y - o.Y, t1.X - o.X), a2 = std::atan2(t2.Y - o.Y, t2.X - o.X);
        double sweep = std::remainder(a2 - a1, 2 * std::numbers::pi);     // 小于半圈的那段，带方向
        TchGraphics::Path center;
        center.Layer = 1;
        center.Points = {{t1, std::tan(sweep / 4)}, {t2, 0}};
        g.Paths.push_back(std::move(center));
        constexpr int kSegments = 3;
        const double step = sweep / kSegments;
        std::vector<double> angles{a1};
        for (int k = 0; k < kSegments; ++k) angles.push_back(a1 + step / 2 + step * k);
        angles.push_back(a1 + sweep);
        auto at = [&](double a, double r) { return TchGraphics::Point{o.X + r * std::cos(a), o.Y + r * std::sin(a)}; };
        const double ri = R - width / 2, ro = R + width / 2;
        for (const double r : {ri, ro})
            for (std::size_t k = 0; k + 1 < angles.size(); ++k)
                g.Lines.push_back({at(angles[k], r), at(angles[k + 1], r)});
        for (const double a : angles)
            g.Lines.push_back({at(a, ri), at(a, ro)});
        return g;
    }
}

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

// 天正给排水（TWT）管线类对象。布局来自天正给排水 tch_pipewire.arx 的 dwgInFields（带符号），
// 显示规则与天正给排水 + AutoCAD 2014 的分解结果核对（139 户型水施图）。这些对象都不写代理图形。
// 继承关系：TDbPipe → TDbPipePrototype → TDbCurveEntity；TDbPipeFitting / TDbEquipment → TDbPipeNode → TDbEntity；
// TDbPipeValve → TDbPipeAttachment → TDbEntity；TDbWell → TDbEquipment。TDbCurveEntity / TDbEntity 即天正基类（ReadTchBase）。
namespace MiniCAD::Tch
{
    // TCH_PIPE 管线：直管为 LINE，弧管为 ARC，立管（起终点平面位置相同）为圆；带标注时另有管径文字
    struct TchPipe
    {
        MiniDWG::XYZ Start, End;
        double Sweep = 0;           // 弧管的圆心角（弧度，正为逆时针；字段在内核里叫 Bulge，实际存的是角度），0 为直管
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

    // 设备（TCH_EQUIPMENT、TCH_WELL）与阀门（TCH_PIPEVALVE）：显示为所引用图块的一个块参照
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

        // TDbPipeNode：RC 版本（< 2）、RC、RC 接口数，每个接口（TPipeInterface::DwgInFields）为 RC 版本、4 × RC、
        // 字符串（文字流）、软指针（句柄流）、3BD 位置、3BD 方向；最后一个字符串（文字流）
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

        // TDbEquipment 的私有段（版本 < 4）：两个硬指针（图块），字符串，3BD 插入点、3BD、3BD 方向，BD 比例，B，
        // RC 镜像（位 0：Y 向镜像，位 1：X 向镜像），2 × BD；版本 ≥ 2 再 4 × BD，≥ 3 再一个 B
        // well：井的图块是单位尺寸（如半径 0.5 的圆），比例 = 设备比例 × 出图比例，不除以 100（3 个检查井与分解结果核对）
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
            if (version > 1)
                for (int i = 0; i < 4; ++i) r.ReadBitDouble();
            if (version > 2) r.ReadBit();
            const double s = scale * base.Scale / (well ? 1 : 100);
            b.X = at.X; b.Y = at.Y;
            b.Rotation = std::atan2(dir.Y, dir.X);
            b.ScaleX = mirror & 2 ? -s : s;
            b.ScaleY = mirror & 1 ? -s : s;
            return !r.Failed() && Finite(b.X) && Finite(b.Y) && Finite(b.Rotation) && std::abs(s) > 1e-9 && Finite(s);
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
    }

    // TDbPipePrototype（版本 < 2）：RC × 3，3BD 起点、3BD 终点、BD 圆心角，字符串，软指针、RC、软指针、RC，
    // RC 个数 + 软指针若干，RC，RC 个数 + 软指针若干，硬指针（标注文字样式）、硬指针（标注图层），BD 标注字高、3BD 标注位置、B 显示标注。
    // TDbPipe（版本 < 3）：BD 立管半径、BD、BD 管径、BD、BS、字符串（标注前缀）、BD、BD，版本 ≥ 2 再 BD 与字符串（标注后缀）
    inline bool DecodePipe(const MiniDWG::UnknownEntity& entity, TchPipe& pipe, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const char* why = nullptr;
        auto fail = [&](const char* w) { if (diagnostic) *diagnostic = w; return false; };
        const bool doubleLine = entity.Raw.DxfName == "TCH_DOUBLELINEPIPE";
        if (!Detail::CheckPayload(entity, doubleLine ? "TCH_DOUBLELINEPIPE" : "TCH_PIPE", why)) return why ? fail(why) : false;
        const auto& data = entity.Raw.Dwg;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        if (r.ReadByte() >= 2) return fail("unsupported native pipe prototype version");
        TchPipe p;
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
            // TDbDoubleLinePipe（版本 < 3）：BD 管宽、BD 弯头半径与管宽之比、BS、3 × BD，版本 ≥ 2 再硬指针（中心线图层）
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
        if (!Detail::Finite(p.Start.X) || !Detail::Finite(p.Start.Y) || !Detail::Finite(p.End.X) || !Detail::Finite(p.End.Y) ||
            !Detail::Finite(p.Sweep) || std::abs(p.Sweep) >= 2 * std::numbers::pi || !Detail::Finite(p.RiserRadius) || !(p.Scale > 0))
            return fail("invalid native pipe geometry");

        // 文字流：基类之后为原型的字符串、管线的标注前缀，版本 ≥ 2 时还有后缀
        Detail::TextStream stream(data);
        for (int i = 0; i < base.TextCount + 1; ++i) stream.Next();
        p.LabelPrefix = stream.Next();
        if (version > 1) p.LabelSuffix = stream.Next();
        if (stream.Reader.Failed()) return fail("native pipe text stream is incomplete");
        const auto hs = Detail::ReadBlockHandles(entity, base.HandleCount + handles, doubleVersion >= 2 ? 3 : 2);
        if (hs.size() < 2) return fail("native pipe handle stream is incomplete");
        p.LabelStyle = hs[0];
        p.LabelLayer = hs[1];
        if (hs.size() > 2) p.CenterLayer = hs[2];
        if (p.Label && !(p.LabelHeight > 0 && Detail::Finite(p.LabelX) && Detail::Finite(p.LabelY))) p.Label = false;
        pipe = std::move(p);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 管线显示：线、弧随对象图层；标注文字放到标注图层（TchGraphics 的 Layer 1），颜色随层，正中对齐，
    // 方向沿管线并保持字头朝上，字体规则同 TCH_TEXT
    inline TchGraphics BuildPipe(const TchPipe& p, const TchTextStyleRule& rule = {})
    {
        TchGraphics g;
        if (p.IsRiser())
        {
            if (p.RiserRadius > 0) g.Circles.push_back({{p.Start.X, p.Start.Y}, p.RiserRadius * p.Scale});
        }
        else if (std::abs(p.Sweep) > 1e-9)
        {
            // 由弦与圆心角求圆心：圆心在弦中点的左侧（逆时针）或右侧（顺时针）
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

    // TCH_PIPEFITTING 管件（弯头、三通等）：只校验布局，天正分解结果为空、不显示。TDbPipeNode 之后为 RC 版本（< 2）、3BD 位置
    inline bool DecodePipeFitting(const MiniDWG::UnknownEntity& entity, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const char* why = nullptr;
        auto fail = [&](const char* w) { if (diagnostic) *diagnostic = w; return false; };
        if (!Detail::CheckPayload(entity, "TCH_PIPEFITTING", why)) return why ? fail(why) : false;
        const auto& data = entity.Raw.Dwg;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        int handles = 0, texts = 0;
        if (!Detail::ReadPipeNode(r, handles, texts)) return fail("unsupported native pipe node layout");
        if (r.ReadByte() >= 2) return fail("unsupported native pipe fitting version");
        Detail::Read3(r);
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native pipe fitting fields do not consume the payload exactly");
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // TCH_EQUIPMENT 设备（洁具、地漏、雨水斗等）与 TCH_WELL 井：引用图块的块参照。
    // 插入点 = 设备插入点，转角 = 方向向量的角度，比例 = 设备比例 × 出图比例 / 100（井不除以 100），镜像位取负对应方向的比例。
    // TDbWell 在设备之后另有标注样式（TLabelStyle：RC 版本、硬指针、2 × BD）与 RC、字符串、3BD、3 × BD、B、硬指针，标注不显示
    inline bool DecodeEquipment(const MiniDWG::UnknownEntity& entity, TchPipeBlock& block, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const char* why = nullptr;
        auto fail = [&](const char* w) { if (diagnostic) *diagnostic = w; return false; };
        const bool well = entity.Raw.DxfName == "TCH_WELL", pump = entity.Raw.DxfName == "TCH_PUMP";
        if (!Detail::CheckPayload(entity, well ? "TCH_WELL" : pump ? "TCH_PUMP" : "TCH_EQUIPMENT", why)) return why ? fail(why) : false;
        const auto& data = entity.Raw.Dwg;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        TchPipeBlock b;
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
            // TDbPump：RC、4 个字符串（文字流）、6 × BD（0x1e0、0x1e8、0x1f0、0x1f8 长、0x200 宽、0x230；样例为 0、0、165、700、700、165）。
            // 水泵图块是 1 × 1 的单位块，按长、宽缩放（不乘设备比例），镜像与转角同设备（4 台水泵与分解结果核对）
            r.ReadByte();
            double size[6];
            for (double& v : size) v = r.ReadBitDouble();
            if (size[3] > 0 && size[4] > 0 && Detail::Finite(size[3]) && Detail::Finite(size[4]))
            {
                b.ScaleX = b.ScaleX < 0 ? -size[3] : size[3];
                b.ScaleY = b.ScaleY < 0 ? -size[4] : size[4];
            }
        }
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native equipment fields do not consume the payload exactly");
        b.Blocks = Detail::ReadBlockHandles(entity, base.HandleCount + handles, 2);
        if (b.Blocks.empty()) return fail("native equipment handle stream is incomplete");
        block = std::move(b);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // TCH_PIPEVALVE 阀门：TDbPipeAttachment（RC 版本 < 2、RC、3BD 位置、3BD 方向、软指针）之后为 RC 版本（< 3）、
    // 两个硬指针（图块）、字符串、BD 比例、BD、B、RC，版本 ≥ 2 再 4 × BD 与 B。比例 = 阀门比例 × 出图比例 / 100
    inline bool DecodePipeValve(const MiniDWG::UnknownEntity& entity, TchPipeBlock& block, std::string* diagnostic = nullptr)
    {
        using namespace MiniDWG;
        const char* why = nullptr;
        auto fail = [&](const char* w) { if (diagnostic) *diagnostic = w; return false; };
        if (!Detail::CheckPayload(entity, "TCH_PIPEVALVE", why)) return why ? fail(why) : false;
        const auto& data = entity.Raw.Dwg;
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        if (r.ReadByte() >= 2) return fail("unsupported native pipe attachment version");
        r.ReadByte();
        const auto at = Detail::Read3(r);
        const auto dir = Detail::Read3(r);
        const int version = r.ReadByte();
        if (version >= 3) return fail("unsupported native valve version");
        const double scale = r.ReadBitDouble();
        r.ReadBitDouble();
        r.ReadBit();
        r.ReadByte();
        if (version >= 2)
        {
            for (int i = 0; i < 4; ++i) r.ReadBitDouble();
            r.ReadBit();
        }
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native valve fields do not consume the payload exactly");
        TchPipeBlock b;
        const double s = scale * base.Scale / 100;
        b.X = at.X; b.Y = at.Y;
        b.Rotation = std::atan2(dir.Y, dir.X);
        b.ScaleX = b.ScaleY = s;
        if (!Detail::Finite(b.X) || !Detail::Finite(b.Y) || !Detail::Finite(b.Rotation) || !Detail::Finite(s) || std::abs(s) < 1e-9)
            return fail("invalid native valve placement");
        b.Blocks = Detail::ReadBlockHandles(entity, base.HandleCount + 1, 2);
        if (b.Blocks.empty()) return fail("native valve handle stream is incomplete");
        block = std::move(b);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // ---- 天正给排水的标注类：管线文字、立管标注、井标注、多管标注；以及双线管与法兰（弯头） ----
    // 显示规则与天正给排水的分解结果核对（总图、14#~16#楼调规两张图）。文字的字高、宽度因子按分解出的 TEXT 取：
    // 字高与宽度因子由文字样式的 _TCH 扩展数据决定（见 TchTextStyleRule）

    namespace Detail
    {
        // 天正基类的属性包，记下字符串与实数值（名字与字符串值依次在文字流中）
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

        // 同 ReadTchBase，另外取出第二个 BD（多管标注的文字方向）与属性包的值
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

        // 天正符号基类 TDbSymbWithText（版本 1～3，同 ReadTchSymbolBase）：句柄流依次为文字样式、（版本 ≥ 2）图层；版本 > 2 时 BS 文字颜色
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

        inline TchGraphics::Point P2(MiniDWG::DwgBitReader& r) { const double x = r.ReadRawDouble(); return {x, r.ReadRawDouble()}; }

        // 文字方向保持字头朝上：角度落在 (-90°, 90°]
        inline double Upright(double a)
        {
            a = std::remainder(a, 2 * std::numbers::pi);
            if (a > std::numbers::pi / 2 + 1e-9) a -= std::numbers::pi;
            else if (a <= -std::numbers::pi / 2 + 1e-9) a += std::numbers::pi;
            return a;
        }
    }

    // TCH_PIPETEXT 管线文字：TDbPipeAttachment（位置、方向）、TLabelStyle（RC 版本、硬指针文字样式、BD 字高、BD）、RC 版本（< 2）、字符串
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
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        if (r.ReadByte() >= 2) return fail("unsupported native pipe attachment version");
        r.ReadByte();
        const auto at = Detail::Read3(r);
        const auto dir = Detail::Read3(r);
        if (r.ReadByte() >= 2) return fail("unsupported native label style version");
        TchPipeText t;
        t.Height = r.ReadBitDouble();
        r.ReadBitDouble();
        if (r.ReadByte() >= 2) return fail("unsupported native pipe text version");
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native pipe text fields do not consume the payload exactly");
        t.X = at.X; t.Y = at.Y; t.Scale = base.Scale;
        t.Rotation = Detail::Upright(std::atan2(dir.Y, dir.X));
        if (!Detail::Finite(t.X) || !Detail::Finite(t.Y) || !(t.Height > 0) || !(t.Scale > 0)) return fail("invalid native pipe text settings");
        Detail::TextStream stream(data);
        for (int i = 0; i < base.TextCount; ++i) stream.Next();
        t.Value = stream.Next();
        if (stream.Reader.Failed()) return fail("native pipe text stream is incomplete");
        const auto hs = Detail::ReadBlockHandles(entity, base.HandleCount + 1, 1);
        if (hs.empty()) return fail("native pipe text handle stream is incomplete");
        t.Style = hs[0];
        text = std::move(t);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 文字以插入点为正中，沿管线方向，字高 = 字高 × 比例（颜色随层）
    inline TchGraphics BuildPipeText(const TchPipeText& t, const TchTextStyleRule& rule)
    {
        TchGraphics g;
        const double h = t.Height * t.Scale * rule.HeightFactor();
        g.Texts.push_back({{t.X, t.Y}, h, t.Rotation, t.Value, 5, 0, 256, 0, rule.WidthFactor()});
        return g;
    }

    // TCH_VPIPEDIM 立管标注：TDbSymbWithText 之后为 RC 版本（< 2）、2 × BS、3BD 立管点、3BD 转折点、3BD 方向、BD 水平段长、BD，
    // 软指针（立管），字符串（立管编号前缀）。显示文字 = 属性包 VPIPE_DIM_FLOOR + VPIPE_DIM_TEXT（如“8”+“JL-1”）
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
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        Detail::TextStream stream(data);
        Detail::PipeSymbol s;
        if (!Detail::ReadPipeSymbol(entity, r, stream, s)) return fail("unsupported native symbol base layout");
        if (r.ReadByte() >= 2) return fail("unsupported native riser dimension version");
        r.ReadBitShort(); r.ReadBitShort();
        const auto a = Detail::Read3(r), b = Detail::Read3(r), dir = Detail::Read3(r);
        const double run = r.ReadBitDouble();
        r.ReadBitDouble();
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native riser dimension fields do not consume the payload exactly");
        TchVPipeDim d;
        d.X1 = a.X; d.Y1 = a.Y; d.X2 = b.X; d.Y2 = b.Y;
        d.Scale = s.Base.Scale; d.TextHeight = s.TextHeight; d.Color = s.Color; d.Style = s.Style;
        d.Run = run * d.Scale * (dir.X < 0 ? -1 : 1);
        const std::string own = stream.Next();
        if (stream.Reader.Failed()) return fail("native riser dimension text stream is incomplete");
        const auto text = s.Values.Strings.find("VPIPE_DIM_TEXT");
        const auto floor = s.Values.Strings.find("VPIPE_DIM_FLOOR");
        d.Value = (floor != s.Values.Strings.end() ? floor->second : std::string()) + (text != s.Values.Strings.end() ? text->second : own);
        if (!Detail::Finite(d.X1) || !Detail::Finite(d.Y1) || !Detail::Finite(d.X2) || !Detail::Finite(d.Y2) || !Detail::Finite(d.Run))
            return fail("invalid native riser dimension geometry");
        dim = std::move(d);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 引线：立管点 → 转折点 → 水平段末端（自由端）；文字在水平段上方 0.05 × 字高，贴自由端、内缩 0.3 × 字高
    //（75 个立管标注的分解结果全部如此）
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

    // TCH_WELLDIM 井标注：TDbSymbWithText 之后为 RC 版本（< 2）、BS、BD 水平段长（带方向）、2RD 井点、2RD 转折点、软指针（井）。
    // 上行显示井面标高、下行显示井底标高（取自所引用的 TCH_WELL，三位小数）
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
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        Detail::TextStream stream(data);
        Detail::PipeSymbol s;
        if (!Detail::ReadPipeSymbol(entity, r, stream, s)) return fail("unsupported native symbol base layout");
        if (r.ReadByte() >= 2) return fail("unsupported native well dimension version");
        r.ReadBitShort();
        const double run = r.ReadBitDouble();
        const auto a = Detail::P2(r), b = Detail::P2(r);
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native well dimension fields do not consume the payload exactly");
        TchWellDim d;
        d.X1 = a.X; d.Y1 = a.Y; d.X2 = b.X; d.Y2 = b.Y;
        d.Scale = s.Base.Scale; d.TextHeight = s.TextHeight; d.Color = s.Color; d.Style = s.Style;
        d.Run = run * d.Scale;
        const auto hs = Detail::ReadBlockHandles(entity, s.HandleCount, 1);
        if (hs.empty()) return fail("native well dimension handle stream is incomplete");
        d.Well = hs[0];
        if (!Detail::Finite(d.X1) || !Detail::Finite(d.Y1) || !Detail::Finite(d.X2) || !Detail::Finite(d.Y2) || !Detail::Finite(d.Run))
            return fail("invalid native well dimension geometry");
        dim = std::move(d);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 两段引线为 6 号色；两行标高在水平段上下各留 0.5 × 字高，按水平段居中（分解结果中文字左端距自由端约 0.26 × 字高，差别在字宽内）
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

    // TCH_PIPEMULTDIM 多管标注：TDbSymbWithText 之后为 RC 版本（< 3）、RC 个数 + 软指针（被标注的管线）、BD 水平段长、
    // 2RD 基线起点、2RD 基线终点，版本 ≥ 2 再 BS 与字符串。文字方向为天正基类的第二个 BD；行距 = (字高 + 属性包“多管标注基线距离”) × 比例
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
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        Detail::TextStream stream(data);
        Detail::PipeSymbol s;
        if (!Detail::ReadPipeSymbol(entity, r, stream, s)) return fail("unsupported native symbol base layout");
        const int version = r.ReadByte();
        if (version >= 3) return fail("unsupported native multi-pipe dimension version");
        const int count = r.ReadByte();
        TchPipeMultDim d;
        d.Run = r.ReadBitDouble();
        d.P1 = Detail::P2(r);
        d.P2 = Detail::P2(r);
        if (version > 1) r.ReadBitShort();
        if (r.Failed() || r.PositionInBits() != data.MainBits)
            return fail("native multi-pipe dimension fields do not consume the payload exactly");
        d.Scale = s.Base.Scale; d.TextHeight = s.TextHeight; d.Color = s.Color; d.Style = s.Style; d.Angle = s.Angle;
        d.Run *= d.Scale;
        double gap = 0.5;
        if (const auto it = s.Values.Numbers.find("\xe5\xa4\x9a\xe7\xae\xa1\xe6\xa0\x87\xe6\xb3\xa8\xe5\x9f\xba\xe7\xba\xbf\xe8\xb7\x9d\xe7\xa6\xbb");
            it != s.Values.Numbers.end() && Detail::Finite(it->second))
            gap = it->second;                                   // “多管标注基线距离”
        d.Spacing = (d.TextHeight + gap) * d.Scale;
        d.Pipes = Detail::ReadBlockHandles(entity, s.HandleCount, count);
        if (static_cast<int>(d.Pipes.size()) != count) return fail("native multi-pipe dimension handle stream is incomplete");
        if (!Detail::Finite(d.P1.X) || !Detail::Finite(d.P1.Y) || !Detail::Finite(d.P2.X) || !Detail::Finite(d.P2.Y) ||
            !Detail::Finite(d.Run) || !Detail::Finite(d.Angle) || std::hypot(d.P2.X - d.P1.X, d.P2.Y - d.P1.Y) < 1e-9)
            return fail("invalid native multi-pipe dimension geometry");
        dim = std::move(d);
        if (diagnostic) diagnostic->clear();
        return true;
    }

    // 基线 P1 → P2；每根管线与基线的交点处画一条斜线（基线方向转 45°，长 = 字高 × 比例）；
    // 从 P2 起沿基线方向每隔一个行距一条下划线（沿文字方向，长 Run），其上为该管线的管径文字（按管线的存储顺序，
    // 字高按样式规则，左下角距下划线起点沿文字方向 1/24、向上 1/12 × 字高 × 比例）；基线延长到最后一条下划线。
    // labels[i] 为第 i 根管线的文字，crossings[i] 为交点（没有交点时不画斜线）
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

    // 管线与无限长直线 a + t·(ux, uy) 的交点（只算直管）
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

    // 双线管：可见段内画中心线（中心线图层，TchGraphics 的 Layer 1）与两侧边线（± 管宽 / 2，对象图层）；弧形双线管画同心圆弧
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

    // TCH_FLANGE 法兰（双线管的管件）：TDbPipeNode（各接口的位置、方向、软指针）+ RC 版本 + 3BD，再 RC 版本（< 3），版本 ≥ 2 再硬指针。
    // 节点的最后一个字符串为弯头中心半径与管宽之比（如“1.0”）
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
        DwgBitReader r(data.Main, data.Version, Codec::CodePage::Gbk);
        TchBase base;
        if (!ReadTchBase(r, base)) return fail("unsupported Tianzheng base layout");
        TchFlange f;
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
        Detail::TextStream stream(data);
        for (int i = 0; i < base.TextCount + count; ++i) stream.Next();
        const std::string ratio = stream.Next();
        if (stream.Reader.Failed()) return fail("native flange text stream is incomplete");
        char* end = nullptr;
        const double v = std::strtod(ratio.c_str(), &end);
        if (end != ratio.c_str() && v > 0 && Detail::Finite(v)) f.BendRatio = v;
        f.Pipes = Detail::ReadBlockHandles(entity, base.HandleCount, count);
        if (static_cast<int>(f.Pipes.size()) != count) return fail("native flange handle stream is incomplete");
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

// 天正线图案（TCH_PATH_ARRAY）：私有布局解码与沿路径排布；天正矩形（TCH_RECT）的对角线
// 合成对象的数值取自真实样例与用 MiniCADTchPatch 构造的变体，期望值取自天正 2014 的分解结果（docs/Tch-compatibility.md）：
// 图案块里的圆弧经不等比缩放分解为椭圆弧，按中心与两轴长比对；直线按端点比对
#include "Import/TchPathArray.h"
#include "Import/TchShape.h"
#include "Import/CadExchange.h"
#include "Dwg/Write/DwgBitWriter.h"
#include "Dwg/Write/DwgWriter.h"
#include "Scene/Scene.h"
#include "Core/Entity/InsertEntity.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace MiniDWG;
using MiniCAD::Tch::TchPathArray;
using MiniCAD::Tch::TchPathArrayUnit;

namespace
{
    int g_failed = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        g_failed += ok ? 0 : 1;
    }

    bool Near(double a, double b, double tol = 0.02) { return std::abs(a - b) <= tol; }

    struct Vtx { double X, Y, Angle; };
    struct PathSpec
    {
        const char* Name = "";
        bool Wave = true;                    // 波浪线（3a3）或 45 度线（39b）
        std::vector<Vtx> Vertices;
        bool Closed = false;
        double Width = 120;
        int Flags = 8;
        double PatternScale = 1;
        int Version = 2;
    };

    // TDbPolyPathArray：样例 3a3（611 位）、39b（747 位）
    std::unique_ptr<UnknownEntity> PathObject(const PathSpec& s, Handle block = 0x39c)
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_PATH_ARRAY";
        auto& raw = e->Raw.Dwg;
        raw.Version = CadVersion::AC1027;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        r.WriteByte(5); r.WriteBitDouble(100); r.WriteBitDouble(0); r.WriteByte(1); r.WriteByte(0); r.WriteBitLong(0);
        r.WriteByte(0); r.WriteBitShort(static_cast<std::int16_t>(s.Vertices.size()));
        for (const auto& v : s.Vertices) { r.WriteBitDouble(v.X); r.WriteBitDouble(v.Y); r.WriteBitDouble(0); r.WriteBitDouble(v.Angle); }
        r.WriteBit(s.Closed); r.WriteBitDouble(0); r.WriteBitDouble(0); r.WriteBitDouble(1);
        r.WriteByte(static_cast<std::uint8_t>(s.Version)); r.WriteBitDouble(s.Width); r.WriteByte(static_cast<std::uint8_t>(s.Flags));
        if (s.Version >= 2) r.WriteBitDouble(s.PatternScale);
        raw.MainBits = r.PositionInBits(); raw.Main = r.Take();
        raw.TextBits = 0;
        r.WriteHandle(DwgRef::SoftPointer, 0);
        r.WriteHandle(DwgRef::HardPointer, block);
        raw.HandleBits = r.PositionInBits(); raw.Handles = r.Take();
        return e;
    }

    // 天正矩形（版本 2）：中心 (1000, 2000)、长 400、宽 200、转角 rotation，flag 为矩形标志
    std::unique_ptr<UnknownEntity> RectObject(int flag, double rotation = 0)
    {
        auto e = std::make_unique<UnknownEntity>();
        e->Raw.DxfName = "TCH_RECT";
        auto& raw = e->Raw.Dwg;
        raw.Version = CadVersion::AC1027;
        DwgBitWriter r(raw.Version, Codec::CodePage::Gbk);
        r.WriteByte(5); r.WriteBitDouble(100); r.WriteBitDouble(0); r.WriteByte(1); r.WriteByte(0); r.WriteBitLong(0);
        r.WriteByte(2); r.WriteBitShort(static_cast<std::int16_t>(flag));
        r.WriteBitDouble(1000); r.WriteBitDouble(2000); r.WriteBitDouble(0);
        // 天正自定义编码的双精度：两位 00 后接 RD，10 为 0
        auto tchDouble = [&](double v) {
            if (v == 0) { r.WriteBit(true); r.WriteBit(false); return; }
            r.WriteBit(false); r.WriteBit(false); r.WriteRawDouble(v);
        };
        tchDouble(rotation); tchDouble(400); tchDouble(200); tchDouble(0);
        r.WriteBitDouble(0); r.WriteBitDouble(0); r.WriteBitDouble(1);
        r.WriteByte(0);
        raw.MainBits = r.PositionInBits(); raw.Main = r.Take();
        return e;
    }

    // 样例的两个图案块
    struct BlockArc { double Cx, Cy, R; };
    struct BlockLine { double X1, Y1, X2, Y2; };
    const std::vector<BlockArc> kWaveArcs{{150, -42.1918, 161.41}, {449.999, -161.41, 161.41}};
    const std::vector<BlockLine> kDiagLines{{100, 0, 0, -100}, {100, -100, 0, -200}, {100, -200, 0, -300}};

    struct Ell { double X, Y, A, B; };       // 中心、短轴半长、长轴半长
    struct Seg { double X1, Y1, X2, Y2; };
    struct Case
    {
        PathSpec Spec;
        std::vector<Ell> Ellipses;
        std::vector<Seg> Lines;              // 分解结果的抽样
        int LineCount = 0;
    };

    struct Mapped { std::vector<Ell> Ellipses; std::vector<Seg> Lines; };
    Mapped Map(const std::vector<TchPathArrayUnit>& units, bool wave)
    {
        Mapped m;
        for (const auto& u : units)
        {
            const double c = std::cos(u.Rotation), s = std::sin(u.Rotation);
            auto T = [&](double x, double y) {
                const double lx = x * u.ScaleX, ly = y * u.ScaleY;
                return std::pair{u.X + lx * c - ly * s, u.Y + lx * s + ly * c};
            };
            if (wave)
                for (const auto& a : kWaveArcs)
                {
                    const auto [x, y] = T(a.Cx, a.Cy);
                    const double p = std::abs(a.R * u.ScaleX), q = std::abs(a.R * u.ScaleY);
                    m.Ellipses.push_back({x, y, std::min(p, q), std::max(p, q)});
                }
            else
                for (const auto& l : kDiagLines)
                {
                    const auto [x1, y1] = T(l.X1, l.Y1);
                    const auto [x2, y2] = T(l.X2, l.Y2);
                    m.Lines.push_back({x1, y1, x2, y2});
                }
        }
        return m;
    }

    bool SameEll(const Ell& a, const Ell& b) { return Near(a.X, b.X, 0.05) && Near(a.Y, b.Y, 0.05) && Near(a.A, b.A) && Near(a.B, b.B); }
    bool SameSeg(const Seg& a, const Seg& b)
    {
        return (Near(a.X1, b.X1) && Near(a.Y1, b.Y1) && Near(a.X2, b.X2) && Near(a.Y2, b.Y2)) ||
               (Near(a.X1, b.X2) && Near(a.Y1, b.Y2) && Near(a.X2, b.X1) && Near(a.Y2, b.Y1));
    }

    // 天正 2014 分解结果（生成脚本见 docs/Tch-compatibility.md 的说明）
    const std::vector<Case>& Cases()
    {
        static const std::vector<Case> cases{
        {{"样例：波浪线", true, {{52748.55241501352, 28460.271471896252, 0.0}, {54772.640919170444, 28785.667299979083, 0.0}, {55128.63639917792, 28358.585262155306, 0.0}}, false, 120.0, 8, 1.0, 2}, {{52844.1805, 28511.2286, 95.1347, 110.3011}, {53057.7420, 28474.3935, 95.1347, 110.3011}, {53248.9982, 28576.3078, 95.1347, 110.3011}, {53462.5597, 28539.4726, 95.1347, 110.3011}, {53653.8159, 28641.3870, 95.1347, 110.3011}, {53867.3774, 28604.5518, 95.1347, 110.3011}, {54058.6336, 28706.4661, 95.1347, 110.3011}, {54272.1951, 28669.6310, 95.1347, 110.3011}, {54463.4513, 28771.5453, 95.1347, 110.3011}, {54677.0128, 28734.7101, 95.1347, 110.3011}, {54888.6267, 28701.3916, 95.1280, 149.5723}, {55012.6507, 28442.8609, 95.1280, 149.5723}}, {}, 0},
        {{"样例：45 度线", false, {{52534.95515506754, 29253.423807336505, 0.0}, {55606.68739617201, 29690.674444590273, 0.0}, {55606.68739617201, 30443.15231049688, 0.0}, {53297.80259218464, 31134.618436196186, 0.0}}, false, 120.0, 8, 1.0, 2}, {}, {{52566.3922, 29318.5036, 52532.1366, 29273.2242}, {52856.9147, 29279.0520, 52822.6591, 29233.7727}, {53170.4187, 29364.0815, 53136.1631, 29318.8021}, {53483.9226, 29449.1109, 53449.6670, 29403.8315}, {53774.4451, 29409.6594, 53740.1895, 29364.3800}, {54087.9491, 29494.6888, 54053.6935, 29449.4094}, {54401.4530, 29579.7183, 54367.1975, 29534.4389}, {54691.9755, 29540.2667, 54657.7199, 29494.9873}, {55005.4795, 29625.2962, 54971.2239, 29580.0168}, {55318.9835, 29710.3256, 55284.7279, 29665.0462}, {55609.5059, 29670.8740, 55575.2503, 29625.5947}, {55586.6874, 30025.1091, 55626.6874, 29983.3047}, {55546.6874, 30359.5437, 55586.6874, 30317.7393}, {55420.0182, 30519.9338, 55469.9752, 30546.7278}, {55100.6912, 30573.8107, 55150.6483, 30600.6048}, {54781.3642, 30627.6877, 54831.3213, 30654.4818}, {54534.9456, 30784.9958, 54584.9027, 30811.7899}, {54215.6187, 30838.8727, 54265.5758, 30865.6668}, {53896.2917, 30892.7497, 53946.2488, 30919.5438}, {53649.8731, 31050.0578, 53699.8302, 31076.8519}, {53330.5462, 31103.9347, 53380.5032, 31130.7288}}, 465},
        {{"宽度 200", true, {{52748.55241501352, 28460.271471896252, 0.0}, {54772.640919170444, 28785.667299979083, 0.0}, {55128.63639917792, 28358.585262155306, 0.0}}, false, 200.0, 8, 1.0, 2}, {{52907.9326, 28545.2001, 158.5577, 183.8351}, {53263.8684, 28483.8081, 158.5577, 183.8351}, {53582.6288, 28653.6654, 158.5577, 183.8351}, {53938.5646, 28592.2734, 158.5577, 183.8351}, {54257.3249, 28762.1306, 158.5577, 183.8351}, {54613.2607, 28700.7387, 158.5577, 183.8351}}, {}, 0},
        {{"顶边对齐", true, {{52748.55241501352, 28460.271471896252, 0.0}, {54772.640919170444, 28785.667299979083, 0.0}, {55128.63639917792, 28358.585262155306, 0.0}}, false, 120.0, 0, 1.0, 2}, {{52853.7039, 28451.9893, 95.1347, 110.3011}, {53067.2654, 28415.1541, 95.1347, 110.3011}, {53258.5216, 28517.0684, 95.1347, 110.3011}, {53472.0831, 28480.2333, 95.1347, 110.3011}, {53663.3393, 28582.1476, 95.1347, 110.3011}, {53876.9008, 28545.3124, 95.1347, 110.3011}, {54068.1571, 28647.2268, 95.1347, 110.3011}, {54281.7185, 28610.3916, 95.1347, 110.3011}, {54472.9748, 28712.3059, 95.1347, 110.3011}, {54686.5362, 28675.4707, 95.1347, 110.3011}, {54842.5384, 28662.9746, 95.1280, 149.5723}, {54966.5624, 28404.4439, 95.1280, 149.5723}}, {}, 0},
        {{"不拉伸、顶边对齐", true, {{52748.55241501352, 28460.271471896252, 0.0}, {54772.640919170444, 28785.667299979083, 0.0}, {55128.63639917792, 28358.585262155306, 0.0}}, false, 120.0, 1, 1.0, 2}, {{52978.9601, 28472.1256, 95.1327, 95.1327}, {53164.6869, 28430.8157, 95.1327, 95.1327}, {53328.1084, 28528.2553, 95.1327, 95.1327}, {53513.8352, 28486.9454, 95.1327, 95.1327}, {53677.2567, 28584.3850, 95.1327, 95.1327}, {53862.9835, 28543.0750, 95.1327, 95.1327}, {54026.4050, 28640.5146, 95.1327, 95.1327}, {54212.1318, 28599.2047, 95.1327, 95.1327}, {54375.5533, 28696.6443, 95.1327, 95.1327}, {54561.2801, 28655.3344, 95.1327, 95.1327}, {54874.9311, 28624.1135, 95.1327, 95.1327}, {54934.1696, 28443.3049, 95.1327, 95.1327}}, {}, 0},
        {{"底边对齐", true, {{52748.55241501352, 28460.271471896252, 0.0}, {54772.640919170444, 28785.667299979083, 0.0}, {55128.63639917792, 28358.585262155306, 0.0}}, false, 120.0, 4, 1.0, 2}, {{52834.6571, 28570.4680, 95.1347, 110.3011}, {53048.2186, 28533.6329, 95.1347, 110.3011}, {53239.4748, 28635.5472, 95.1347, 110.3011}, {53453.0363, 28598.7120, 95.1347, 110.3011}, {53644.2925, 28700.6264, 95.1347, 110.3011}, {53857.8540, 28663.7912, 95.1347, 110.3011}, {54049.1102, 28765.7055, 95.1347, 110.3011}, {54262.6717, 28728.8703, 95.1347, 110.3011}, {54453.9279, 28830.7847, 95.1347, 110.3011}, {54667.4894, 28793.9495, 95.1347, 110.3011}, {54934.7150, 28739.8087, 95.1280, 149.5723}, {55058.7390, 28481.2780, 95.1280, 149.5723}}, {}, 0},
        {{"不拉伸、居中", true, {{52748.55241501352, 28460.271471896252, 0.0}, {54772.640919170444, 28785.667299979083, 0.0}, {55128.63639917792, 28358.585262155306, 0.0}}, false, 120.0, 9, 1.0, 2}, {{52969.4367, 28531.3650, 95.1327, 95.1327}, {53155.1635, 28490.0551, 95.1327, 95.1327}, {53318.5850, 28587.4947, 95.1327, 95.1327}, {53504.3118, 28546.1848, 95.1327, 95.1327}, {53667.7333, 28643.6243, 95.1327, 95.1327}, {53853.4601, 28602.3144, 95.1327, 95.1327}, {54016.8816, 28699.7540, 95.1327, 95.1327}, {54202.6084, 28658.4441, 95.1327, 95.1327}, {54366.0298, 28755.8837, 95.1327, 95.1327}, {54551.7566, 28714.5737, 95.1327, 95.1327}, {54921.0194, 28662.5306, 95.1327, 95.1327}, {54980.2579, 28481.7220, 95.1327, 95.1327}}, {}, 0},
        {{"翻转、顶边对齐", true, {{52748.55241501352, 28460.271471896252, 0.0}, {54772.640919170444, 28785.667299979083, 0.0}, {55128.63639917792, 28358.585262155306, 0.0}}, false, 120.0, 16, 1.0, 2}, {{53056.1126, 28484.5288, 95.1347, 110.3011}, {52864.8567, 28382.6145, 95.1347, 110.3011}, {53460.9303, 28549.6080, 95.1347, 110.3011}, {53269.6744, 28447.6937, 95.1347, 110.3011}, {53865.7480, 28614.6871, 95.1347, 110.3011}, {53674.4921, 28512.7729, 95.1347, 110.3011}, {54270.5657, 28679.7663, 95.1347, 110.3011}, {54079.3098, 28577.8520, 95.1347, 110.3011}, {54675.3834, 28744.8455, 95.1347, 110.3011}, {54484.1275, 28642.9312, 95.1347, 110.3011}, {55020.5360, 28449.4338, 95.1280, 149.5723}, {54788.5648, 28617.9847, 95.1280, 149.5723}}, {}, 0},
        {{"图案缩放 2", true, {{52748.55241501352, 28460.271471896252, 0.0}, {54772.640919170444, 28785.667299979083, 0.0}, {55128.63639917792, 28358.585262155306, 0.0}}, false, 120.0, 8, 2.0, 2}, {{52990.4109, 28570.3207, 190.2693, 275.7526}, {53518.7382, 28512.9201, 190.2693, 275.7526}, {54002.4551, 28733.0186, 190.2693, 275.7526}, {54530.7824, 28675.6181, 190.2693, 275.7526}}, {}, 0},
        {{"图案缩放 0.5", true, {{52748.55241501352, 28460.271471896252, 0.0}, {54772.640919170444, 28785.667299979083, 0.0}, {55128.63639917792, 28358.585262155306, 0.0}}, false, 120.0, 8, 0.5, 2}, {{52791.7663, 28485.0105, 47.5649, 50.1369}, {52889.3466, 28465.1139, 47.5649, 50.1369}, {52975.7743, 28514.5920, 47.5649, 50.1369}, {53073.3547, 28494.6953, 47.5649, 50.1369}, {53159.7824, 28544.1734, 47.5649, 50.1369}, {53257.3627, 28524.2767, 47.5649, 50.1369}, {53343.7904, 28573.7548, 47.5649, 50.1369}, {53441.3707, 28553.8582, 47.5649, 50.1369}, {53527.7984, 28603.3363, 47.5649, 50.1369}, {53625.3788, 28583.4396, 47.5649, 50.1369}, {53711.8065, 28632.9177, 47.5649, 50.1369}, {53809.3868, 28613.0211, 47.5649, 50.1369}, {53895.8145, 28662.4992, 47.5649, 50.1369}, {53993.3949, 28642.6025, 47.5649, 50.1369}, {54079.8226, 28692.0806, 47.5649, 50.1369}, {54177.4029, 28672.1839, 47.5649, 50.1369}, {54263.8306, 28721.6620, 47.5649, 50.1369}, {54361.4110, 28701.7654, 47.5649, 50.1369}, {54447.8387, 28751.2435, 47.5649, 50.1369}, {54545.4190, 28731.3468, 47.5649, 50.1369}, {54631.8467, 28780.8249, 47.5649, 50.1369}, {54729.4271, 28760.9283, 47.5649, 50.1369}, {54815.8006, 28761.3246, 47.5640, 49.8574}, {54848.1464, 28667.6494, 47.5640, 49.8574}, {54934.4658, 28618.9639, 47.5640, 49.8574}, {54966.8115, 28525.2887, 47.5640, 49.8574}, {55053.1310, 28476.6032, 47.5640, 49.8574}, {55085.4767, 28382.9280, 47.5640, 49.8574}}, {}, 0},
        {{"闭合", true, {{52748.55241501352, 28460.271471896252, 0.0}, {54772.640919170444, 28785.667299979083, 0.0}, {55128.63639917792, 28358.585262155306, 0.0}}, true, 120.0, 8, 1.0, 2}, {{52844.1805, 28511.2286, 95.1347, 110.3011}, {53057.7420, 28474.3935, 95.1347, 110.3011}, {53248.9982, 28576.3078, 95.1347, 110.3011}, {53462.5597, 28539.4726, 95.1347, 110.3011}, {53653.8159, 28641.3870, 95.1347, 110.3011}, {53867.3774, 28604.5518, 95.1347, 110.3011}, {54058.6336, 28706.4661, 95.1347, 110.3011}, {54272.1951, 28669.6310, 95.1347, 110.3011}, {54463.4513, 28771.5453, 95.1347, 110.3011}, {54677.0128, 28734.7101, 95.1347, 110.3011}, {54888.6267, 28701.3916, 95.1280, 149.5723}, {55012.6507, 28442.8609, 95.1280, 149.5723}, {55027.9665, 28327.7215, 95.1367, 106.8111}, {54832.6256, 28406.3968, 95.1367, 106.8111}, {54631.2859, 28344.6692, 95.1367, 106.8111}, {54435.9450, 28423.3445, 95.1367, 106.8111}, {54234.6052, 28361.6169, 95.1367, 106.8111}, {54039.2643, 28440.2922, 95.1367, 106.8111}, {53837.9245, 28378.5646, 95.1367, 106.8111}, {53642.5836, 28457.2399, 95.1367, 106.8111}, {53441.2439, 28395.5123, 95.1367, 106.8111}, {53245.9030, 28474.1876, 95.1367, 106.8111}, {53044.5632, 28412.4600, 95.1367, 106.8111}, {52849.2223, 28491.1353, 95.1367, 106.8111}}, {}, 0},
        {{"逆时针圆弧段", true, {{52748.55241501352, 28460.271471896252, 0.4}, {54772.640919170444, 28785.667299979083, 0.0}, {55128.63639917792, 28358.585262155306, 0.0}}, false, 120.0, 8, 1.0, 2}, {{52852.9337, 28491.2143, 95.1376, 109.7193}, {53056.8177, 28420.8260, 95.1376, 109.7193}, {53262.7787, 28499.2084, 95.1376, 109.7193}, {53471.6356, 28445.3386, 95.1376, 109.7193}, {53670.6739, 28539.9297, 95.1376, 109.7193}, {53883.1679, 28502.9229, 95.1376, 109.7193}, {54074.0104, 28613.1175, 95.1376, 109.7193}, {54288.7821, 28593.2104, 95.1376, 109.7193}, {54470.2082, 28718.3037, 95.1376, 109.7193}, {54685.8838, 28715.6237, 95.1376, 109.7193}, {54888.6267, 28701.3916, 95.1280, 149.5723}, {55012.6507, 28442.8609, 95.1280, 149.5723}}, {}, 0},
        {{"阵列版本 1", true, {{52748.55241501352, 28460.271471896252, 0.0}, {54772.640919170444, 28785.667299979083, 0.0}, {55128.63639917792, 28358.585262155306, 0.0}}, false, 120.0, 8, 1.0, 1}, {{52844.1805, 28511.2286, 95.1347, 110.3011}, {53057.7420, 28474.3935, 95.1347, 110.3011}, {53248.9982, 28576.3078, 95.1347, 110.3011}, {53462.5597, 28539.4726, 95.1347, 110.3011}, {53653.8159, 28641.3870, 95.1347, 110.3011}, {53867.3774, 28604.5518, 95.1347, 110.3011}, {54058.6336, 28706.4661, 95.1347, 110.3011}, {54272.1951, 28669.6310, 95.1347, 110.3011}, {54463.4513, 28771.5453, 95.1347, 110.3011}, {54677.0128, 28734.7101, 95.1347, 110.3011}, {54888.6267, 28701.3916, 95.1280, 149.5723}, {55012.6507, 28442.8609, 95.1280, 149.5723}}, {}, 0},
        {{"短于一个单元的段", true, {{0, 0, 0}, {100, 0, 0}, {400, 300, 0}}, false, 120.0, 8, 1.0, 2}, {{150.1575, 99.8427, 95.1308, 114.1341}, {349.8425, 200.1573, 95.1308, 114.1341}}, {}, 0},
        {{"底边与居中同时置位", true, {{52748.55241501352, 28460.271471896252, 0.0}, {54772.640919170444, 28785.667299979083, 0.0}, {55128.63639917792, 28358.585262155306, 0.0}}, false, 120.0, 12, 1.0, 2}, {{52834.6571, 28570.4680, 95.1347, 110.3011}, {53048.2186, 28533.6329, 95.1347, 110.3011}, {53239.4748, 28635.5472, 95.1347, 110.3011}, {53453.0363, 28598.7120, 95.1347, 110.3011}, {53644.2925, 28700.6264, 95.1347, 110.3011}, {53857.8540, 28663.7912, 95.1347, 110.3011}, {54049.1102, 28765.7055, 95.1347, 110.3011}, {54262.6717, 28728.8703, 95.1347, 110.3011}, {54453.9279, 28830.7847, 95.1347, 110.3011}, {54667.4894, 28793.9495, 95.1347, 110.3011}, {54934.7150, 28739.8087, 95.1280, 149.5723}, {55058.7390, 28481.2780, 95.1280, 149.5723}}, {}, 0},
        {{"顺时针圆弧段", true, {{52748.55241501352, 28460.271471896252, 0.0}, {54772.640919170444, 28785.667299979083, -0.8}, {55128.63639917792, 28358.585262155306, 0.0}}, false, 120.0, 8, 1.0, 2}, {{52844.1805, 28511.2286, 95.1347, 110.3011}, {53057.7420, 28474.3935, 95.1347, 110.3011}, {53248.9982, 28576.3078, 95.1347, 110.3011}, {53462.5597, 28539.4726, 95.1347, 110.3011}, {53653.8159, 28641.3870, 95.1347, 110.3011}, {53867.3774, 28604.5518, 95.1347, 110.3011}, {54058.6336, 28706.4661, 95.1347, 110.3011}, {54272.1951, 28669.6310, 95.1347, 110.3011}, {54463.4513, 28771.5453, 95.1347, 110.3011}, {54677.0128, 28734.7101, 95.1347, 110.3011}, {54924.4334, 28746.4474, 95.1296, 162.1436}, {55063.4177, 28469.9691, 95.1296, 162.1436}}, {}, 0},
        {{"闭合、末段为圆弧", true, {{52748.55241501352, 28460.271471896252, 0.0}, {54772.640919170444, 28785.667299979083, 0.0}, {55128.63639917792, 28358.585262155306, 1.0}}, true, 120.0, 8, 1.0, 2}, {{52844.1805, 28511.2286, 95.1347, 110.3011}, {53057.7420, 28474.3935, 95.1347, 110.3011}, {53248.9982, 28576.3078, 95.1347, 110.3011}, {53462.5597, 28539.4726, 95.1347, 110.3011}, {53653.8159, 28641.3870, 95.1347, 110.3011}, {53867.3774, 28604.5518, 95.1347, 110.3011}, {54058.6336, 28706.4661, 95.1347, 110.3011}, {54272.1951, 28669.6310, 95.1347, 110.3011}, {54463.4513, 28771.5453, 95.1347, 110.3011}, {54677.0128, 28734.7101, 95.1347, 110.3011}, {54888.6267, 28701.3916, 95.1280, 149.5723}, {55012.6507, 28442.8609, 95.1280, 149.5723}, {55021.9621, 28381.7753, 95.1368, 108.5789}, {54872.2299, 28534.2335, 95.1368, 108.5789}, {54648.8670, 28547.1396, 95.1369, 108.5790}, {54475.9174, 28672.6452, 95.1369, 108.5790}, {54253.5085, 28648.3174, 95.1369, 108.5790}, {54062.1345, 28743.3923, 95.1369, 108.5790}, {53846.8434, 28682.5048, 95.1369, 108.5789}, {53642.3487, 28744.5140, 95.1369, 108.5789}, {53440.1418, 28648.7543, 95.1368, 108.5789}, {53228.1936, 28675.9794, 95.1368, 108.5789}, {53044.6748, 28548.0012, 95.1369, 108.5790}, {52831.1470, 28539.6876, 95.1369, 108.5790}}, {}, 0},
        };
        return cases;
    }

    DxfClass Class(const char* dxf, const char* cpp, std::int16_t number)
    {
        DxfClass c;
        c.DxfName = dxf; c.CppClassName = cpp; c.ApplicationName = "TCH_KERNAL";
        c.ClassNumber = number; c.IsAnEntity = true; c.ItemClassId = 0x1f2;
        return c;
    }
}

int main()
{
    using namespace MiniCAD::Tch;
    std::string why;

    // ── 样例：逐位重现与字段 ──
    {
        const auto wave = PathObject(Cases()[0].Spec);
        const auto diag = PathObject(Cases()[1].Spec, 0x393);
        Check(wave->Raw.Dwg.MainBits == 611 && diag->Raw.Dwg.MainBits == 747, "synthetic path arrays reproduce the 611 / 747-bit real layouts");
        TchPathArray a;
        Check(DecodePathArray(*wave, a, &why) && a.Vertices.size() == 3 && !a.Closed && Near(a.Width, 120) && a.Flags == 8 &&
                  Near(a.PatternScale, 1) && a.Block == 0x39c && Near(a.Vertices[1].X, 54772.6409, 1e-3),
              ("sample path array decodes its path, width, flags and pattern block " + why).c_str());
        int accepted = 0;
        for (std::uint64_t bits = 0; bits < wave->Raw.Dwg.MainBits; bits += 5)
        {
            UnknownEntity cut;
            cut.Raw = wave->Raw;
            cut.Raw.Dwg.MainBits = bits;
            accepted += DecodePathArray(cut, a);
        }
        Check(accepted == 0, "every truncated path array payload is rejected");
    }

    // ── 变体：与天正 2014 分解结果逐个比对 ──
    for (const auto& c : Cases())
    {
        TchPathArray a;
        const std::string label = std::string("path array ") + c.Spec.Name;
        if (!DecodePathArray(*PathObject(c.Spec), a, &why)) { Check(false, (label + ": " + why).c_str()); continue; }
        const auto units = c.Spec.Wave ? BuildPathArray(a, 0, -203.6018, 600, 0) : BuildPathArray(a, 0, -300, 100, 0);
        const Mapped m = Map(units, c.Spec.Wave);
        bool ok;
        if (c.Spec.Wave)
        {
            ok = m.Ellipses.size() == c.Ellipses.size();
            for (const auto& e : c.Ellipses)
                ok = ok && std::any_of(m.Ellipses.begin(), m.Ellipses.end(), [&](const Ell& x) { return SameEll(x, e); });
        }
        else
        {
            ok = static_cast<int>(m.Lines.size()) == c.LineCount;
            for (const auto& l : c.Lines)
                ok = ok && std::any_of(m.Lines.begin(), m.Lines.end(), [&](const Seg& x) { return SameSeg(x, l); });
        }
        Check(ok, label.c_str());
    }

    // ── 不支持的布局 ──
    {
        TchPathArray a;
        PathSpec v3 = Cases()[0].Spec; v3.Version = 3;
        Check(!DecodePathArray(*PathObject(v3), a), "path array version 3 (encoded branch) is not guessed");
        PathSpec one = Cases()[0].Spec; one.Vertices.resize(1);
        Check(!DecodePathArray(*PathObject(one), a), "a path needs at least two vertices");
        PathSpec zero = Cases()[0].Spec; zero.Width = 0;
        Check(!DecodePathArray(*PathObject(zero), a), "zero pattern width is rejected");
    }

    // ── 天正矩形的对角线（内核 TDbRect::ExplodeOrDraw2d）：标志 1 左下—右上，2 左上—右下，3 两条都画（8#楼 套管图例），0 只有矩形 ──
    {
        // 矩形之外的线段（两点的路径），端点按 x 排序
        auto diagonals = [](const TchGraphics& g) {
            std::vector<Seg> out;
            for (std::size_t i = 1; i < g.Paths.size(); ++i)
            {
                if (g.Paths[i].Points.size() != 2) return std::vector<Seg>{};
                auto a = g.Paths[i].Points[0].P, b = g.Paths[i].Points[1].P;
                if (b.X < a.X) std::swap(a, b);
                out.push_back({a.X, a.Y, b.X, b.Y});
            }
            return out;
        };
        auto same = [](const std::vector<Seg>& got, const std::vector<Seg>& want) {
            if (got.size() != want.size()) return false;
            for (const auto& w : want)
                if (!std::any_of(got.begin(), got.end(), [&](const Seg& s) { return SameSeg(s, w); })) return false;
            return true;
        };
        const Seg rising{800, 1900, 1200, 2100}, falling{800, 2100, 1200, 1900};
        TchRect t;
        bool ok = true;
        for (int flag : {0, 1, 2, 3})
        {
            ok = ok && DecodeRect(*RectObject(flag), t, &why) && t.Flag == flag && Near(t.Length, 400) && Near(t.Width, 200);
            const auto d = diagonals(BuildRect(t));
            const std::vector<Seg> want = flag == 0 ? std::vector<Seg>{} : flag == 1 ? std::vector<Seg>{rising}
                                        : flag == 2 ? std::vector<Seg>{falling} : std::vector<Seg>{rising, falling};
            ok = ok && BuildRect(t).Paths.front().Points.size() == 5 && same(d, want);
        }
        Check(ok, ("rectangle flag adds the diagonals drawn by Tianzheng (1 rising, 2 falling, 3 both) " + why).c_str());
        // 转 90°：局部的左下—右上变成 (1100, 1800)—(900, 2200)
        Check(DecodeRect(*RectObject(1, std::numbers::pi / 2), t) && same(diagonals(BuildRect(t)), {{900, 2200, 1100, 1800}}),
              "rectangle diagonals turn with the rectangle");
    }

    // ── 端到端：图案块 + 线图案 → DWG → Scene，每个单元一个块参照 ──
    CadDatabase db;
    db.SetVersion(CadVersion::AC1027);
    db.CreateDefaults();
    db.Classes.push_back(Class("TCH_PATH_ARRAY", "TDbPolyPathArray", 500));
    BlockRecord* pattern = db.CreateBlockRecord("$LinePat$45度线");
    for (const auto& l : kDiagLines)
    {
        auto line = std::make_unique<Line>(); line->StartPoint = {l.X1, l.Y1, 0}; line->EndPoint = {l.X2, l.Y2, 0};
        db.AddEntity(pattern, std::move(line));
    }
    PathSpec spec;
    spec.Vertices = {{0, 0, 0}, {1000, 0, 0}};
    db.AddEntity(db.ModelSpace(), PathObject(spec, pattern->ObjectHandle));
    DwgWriteOptions options; options.Version = CadVersion::AC1027;
    const auto bytes = WriteDwg(db, options);
    MiniCAD::Scene scene;
    Check(!bytes.empty() && MiniCAD::ImportCad(bytes, scene) && scene.EntityCount() == 1, "path array appears in the scene");
    int units = 0, placed = 0;
    scene.ForEachObject([&](const MiniCAD::Object& obj)
    {
        const auto* group = dynamic_cast<const MiniCAD::InsertEntity*>(&obj);
        if (!group || !group->GetBlock()) return;
        for (const auto& e : group->GetBlock()->GetEntities())
            if (const auto* unit = dynamic_cast<const MiniCAD::InsertEntity*>(e.get()); unit && unit->GetBlock())
            {
                ++units;
                // 宽 120、块高 300：sy = 0.4，名义单元长 40，1000 / 40 = 25 个；块居中，第一个单元的块原点在 (0, 60)
                placed += units == 1 && Near(unit->GetPosition().x, 0) && Near(unit->GetPosition().y, 60) && Near(unit->GetScale().x, 0.4) &&
                          Near(unit->GetScale().y, 0.4) && unit->GetBlock()->GetName() == "$LinePat$45度线";
            }
    });
    Check(units == 25 && placed == 1, "each pattern unit is an insert of the line pattern block");
    for (auto kind : {MiniCAD::CadFileKind::Dwg, MiniCAD::CadFileKind::Dxf})
    {
        MiniCAD::Scene restored;
        const auto saved = MiniCAD::ExportCad(scene, kind);
        Check(!saved.empty() && MiniCAD::ImportCad(saved, restored) && restored.EntityCount() == 1, "displayed path array survives plain export and reimport");
    }
    return g_failed;
}

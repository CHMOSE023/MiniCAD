// EXB 几何层回归测试。
//
// 分两段，样例不是必需的：
//   ① 纯函数段：字符串去格式码、UTF-16 解码、图层 ID 归属。不依赖任何文件，
//      所以在没有样例图纸的机器上也会真正跑起来（不是空跳）。
//   ② 样例段：把 .exb 依次走容器 → 样式流 → 块流 → Scene，检查结构不变量。
//      样例不入库（与 tests/Data 的其他私有图纸一致），路径从 argv[1] 或
//      环境变量 MINICAD_EXB_SAMPLE 取；没有就打印 SKIP。
//
// 输出沿用 ExbContainerTests 的行式格式，便于与 Python/ezexb 的独立实现比对。
#include "Import/Exb/ExbContainer.h"
#include "Import/Exb/ExbGeometry.h"
#include "Import/Exb/ExbReader.h"
#include "Scene/Scene.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    int g_failures = 0;

    void Check(bool condition, const std::string& what)
    {
        if (!condition)
        {
            ++g_failures;
            std::cout << "FAIL|" << what << "\n";
        }
    }

    std::filesystem::path MakePath(const std::string& utf8)
    {
        return std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
    }

    bool ReadFile(const std::string& utf8, std::vector<std::uint8_t>& out)
    {
        std::ifstream file(MakePath(utf8), std::ios::binary);
        if (!file.is_open()) return false;
        file.seekg(0, std::ios::end);
        const std::streamoff length = file.tellg();
        if (length < 0) return false;
        file.seekg(0, std::ios::beg);
        out.resize(static_cast<std::size_t>(length));
        if (!out.empty())
            file.read(reinterpret_cast<char*>(out.data()), length);
        return static_cast<bool>(file);
    }

    // 现做一段块流：块头 + 直线 + 带扩展属性的点 + 一段没有长度前缀的数据 + 直线
    struct StreamBuilder
    {
        std::vector<std::uint8_t> Bytes;

        void U8(std::uint8_t v) { Bytes.push_back(v); }
        void U32(std::uint32_t v) { for (int i = 0; i < 4; ++i) Bytes.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }
        void F64(double v)
        {
            std::uint64_t u = 0;
            std::memcpy(&u, &v, sizeof(u));
            for (int i = 0; i < 8; ++i) Bytes.push_back(static_cast<std::uint8_t>(u >> (8 * i)));
        }
        void Fill(std::size_t count, std::uint8_t v) { Bytes.insert(Bytes.end(), count, v); }
        void Name(const char* ascii)
        {
            const std::size_t n = std::strlen(ascii) + 1;
            U32(static_cast<std::uint32_t>(n * 2));
            for (std::size_t i = 0; i < n; ++i) { U8(static_cast<std::uint8_t>(ascii[i])); U8(0); }
        }
        void Header(const std::uint8_t* classId, std::int32_t id, std::int32_t owner, bool ext)
        {
            Bytes.insert(Bytes.end(), classId, classId + 16);
            U32(static_cast<std::uint32_t>(id));
            U32(static_cast<std::uint32_t>(owner));
            U8(1);
            U8(ext ? 1 : 0);
        }
        void Raw(const std::uint8_t* bytes, std::size_t count) { Bytes.insert(Bytes.end(), bytes, bytes + count); }
        // 之后用 EndBody 回填正文长度
        std::size_t BeginBody()
        {
            U32(0);
            return Bytes.size();
        }
        void EndBody(std::size_t start)
        {
            const std::uint32_t len = static_cast<std::uint32_t>(Bytes.size() - start);
            for (int i = 0; i < 4; ++i) Bytes[start - 4 + i] = static_cast<std::uint8_t>(len >> (8 * i));
        }
        void EntityPrefix(std::int32_t layer, std::int32_t lineType)
        {
            U32(0x00800000u); U32(static_cast<std::uint32_t>(layer));
            U32(0x00800000u); U32(static_cast<std::uint32_t>(lineType));
            U32(0x80FFFFFFu);
            Fill(9, 0);
            U32(0x3F800000u);
        }
        void Line(const std::uint8_t* classId, std::int32_t id, double x0, double y0, double x1, double y1)
        {
            Header(classId, id, -5, false);
            U32(81);
            EntityPrefix(-26, -21);
            F64(x0); F64(y0); F64(0.0);
            F64(x1); F64(y1); F64(0.0);
        }
    };

    void RunRecordWalkChecks()
    {
        using namespace MiniCAD::Exb;
        const std::uint8_t blockClass[16] = { 0x81, 0x30, 0x5f, 0xb6, 0x0e, 0x5c, 0x00, 0x4d, 0x97, 0xb4, 0x0c, 0x52, 0x31, 0x05, 0x1a, 0xf1 };
        const std::uint8_t lineClass[16]  = { 0x2d, 0xf7, 0xec, 0x17, 0x46, 0xc0, 0x9a, 0x4e, 0x84, 0x16, 0xfd, 0x82, 0x20, 0xfd, 0x33, 0xb5 };
        const std::uint8_t pointClass[16] = { 0x2e, 0x37, 0xda, 0xd6, 0x4f, 0x0f, 0xfb, 0x4b, 0x9e, 0x92, 0x3b, 0x33, 0x63, 0xac, 0x6c, 0x22 };
        const std::uint8_t groupId[16]    = { 0x8f, 0x31, 0x59, 0xc2, 0x72, 0x11, 0x14, 0x46, 0xb5, 0x54, 0x8e, 0x8a, 0xcc, 0xc4, 0x1d, 0x9e };

        StreamBuilder b;
        b.Name("0.0");
        b.Fill(64, 0);
        b.Header(blockClass, -5, -4, false);
        b.U32(8);
        b.Fill(8, 0);

        b.Line(lineClass, 112, 10.5, 20.25, 110.5, 70.75);

        b.Header(pointClass, 113, -5, true);
        b.U32(1);
        b.Bytes.insert(b.Bytes.end(), groupId, groupId + 16);
        b.U32(2);
        b.U8(4); b.Name("SolidFlag"); b.U32(1);
        b.U8(7); b.Name("Text"); b.U32(2); b.U8(0); b.U8(0);
        b.U32(57);
        b.EntityPrefix(45, -21);
        b.F64(33.5); b.F64(44.25); b.F64(0.0);

        b.Fill(40, 0xEE);

        b.Line(lineClass, 114, 200.5, 300.25, 250.5, 320.75);

        ExbGeometryResult geo;
        Check(ParseBlockStream(b.Bytes.data(), b.Bytes.size(), geo), "synthetic block stream parses");
        Check(geo.RecordCount == 3, "walk finds every record");
        Check(geo.UnparsedBytes == 40, "bytes without a length prefix are skipped and counted");
        Check(geo.LineCount == 2 && geo.PointCount == 1 && geo.UnclassifiedCount == 0, "record kinds");
        if (geo.Items.size() == 3)
        {
            Check(geo.Items[0].Id == 112 && geo.Items[1].Id == 113 && geo.Items[2].Id == 114, "object ids in order");
            Check(geo.Items[0].Points.size() == 2 && geo.Items[0].Points[1].X == 110.5 && geo.Items[0].Points[1].Y == 70.75,
                  "line end point");
            Check(geo.Items[0].Style.Valid && geo.Items[0].Style.LayerId == -26 && geo.Items[0].Style.LineTypeId == -21,
                  "line layer and linetype");
            Check(geo.Items[1].Kind == ExbGeometryKind::Point && geo.Items[1].Points.size() == 1 &&
                      geo.Items[1].Points[0].X == 33.5 && geo.Items[1].Style.LayerId == 45,
                  "point after extended properties");
            Check(geo.Items[2].Points.size() == 2 && geo.Items[2].Points[0].X == 200.5, "record after resync");
            Check(geo.Items[1].ClassId == "2e37dad64f0ffb4b9e923b3363ac6c22", "class id is exposed");
        }
        else
        {
            Check(false, "three items expected");
        }

        // 一个流里两个块，第二个块里有一个转 90°、放大 2 倍的块参照指向第一个块
        const std::uint8_t insertClass[16] = { 0xba, 0x4e, 0x8b, 0xd3, 0x1f, 0xa8, 0x28, 0x4b, 0xaa, 0x19, 0x71, 0xa6, 0x0d, 0x88, 0x40, 0x04 };
        StreamBuilder g;
        g.Name("0.0");
        g.Fill(64, 0);
        const auto blockHeader = [&](std::int32_t id, const char* name)
        {
            g.Header(blockClass, id, -4, false);
            const std::size_t lenAt = g.Bytes.size();
            g.U32(0);
            g.Name(name);
            g.Fill(28, 0);
            const std::uint32_t len = static_cast<std::uint32_t>(g.Bytes.size() - lenAt - 4);
            for (int i = 0; i < 4; ++i) g.Bytes[lenAt + i] = static_cast<std::uint8_t>(len >> (8 * i));
        };
        blockHeader(142, "Door");
        g.Header(lineClass, 143, 142, false);
        g.U32(81);
        g.EntityPrefix(-26, -21);
        g.F64(1.5); g.F64(2.5); g.F64(0.0);
        g.F64(11.5); g.F64(2.5); g.F64(0.0);

        blockHeader(158, "*Anony2");
        g.Header(insertClass, 159, 158, false);
        g.U32(247);
        g.EntityPrefix(-26, -21);
        g.Fill(60, 0);
        const double matrix[16] = { 0, 2, 0, 0,  -2, 0, 0, 0,  0, 0, 1, 0,  30.5, 40.25, 0, 1 };
        for (double v : matrix) g.F64(v);
        g.U32(0x00800000u); g.U32(142);
        g.Fill(18, 0);

        std::vector<ExbBlockDef> blocks;
        Check(ParseBlockDefinitions(g.Bytes.data(), g.Bytes.size(), blocks), "block definition stream parses");
        Check(blocks.size() == 2, "two blocks in one stream");
        if (blocks.size() == 2)
        {
            Check(blocks[0].Id == 142 && blocks[0].Name == "Door" && blocks[0].Items.size() == 1, "first block");
            Check(blocks[1].Id == 158 && blocks[1].Name == "*Anony2" && blocks[1].Items.size() == 1, "second block");
            if (blocks[1].Items.size() == 1)
            {
                const ExbGeometryItem& ins = blocks[1].Items[0];
                Check(ins.Kind == ExbGeometryKind::Insert && ins.BlockRef == 142, "insert refers to the first block");
                Check(ins.Insert.X == 30.5 && ins.Insert.Y == 40.25, "insert position");
                Check(std::fabs(ins.ScaleX - 2.0) < 1e-12 && std::fabs(ins.ScaleY - 2.0) < 1e-12, "insert scale");
                Check(std::fabs(ins.Rotation - 1.5707963267948966) < 1e-12, "insert rotation");
            }
        }

        // 属性类型 12 / 10 / 1、记录版本 3 的显示缓存、椭圆、不认识的类
        const std::uint8_t relationList[16] = { 0xe7, 0x54, 0xbb, 0x0f, 0x10, 0xfc, 0x09, 0x4b, 0x94, 0x99, 0x92, 0x40, 0x97, 0xc3, 0xbf, 0x94 };
        const std::uint8_t displayCache[16] = { 0x36, 0xf3, 0x45, 0x00, 0xf0, 0x69, 0x1a, 0x4e, 0xb1, 0x17, 0x6f, 0x06, 0x28, 0x40, 0xb2, 0xab };
        const std::uint8_t ellipseClass[16] = { 0x5d, 0x7b, 0xdc, 0x85, 0xac, 0xda, 0xc8, 0x47, 0xa1, 0x26, 0xff, 0xad, 0x50, 0xd6, 0xcd, 0x68 };
        std::uint8_t unknownClass[16];
        std::memset(unknownClass, 0x77, sizeof(unknownClass));

        StreamBuilder h;
        h.Name("0.0");
        h.Fill(64, 0);
        h.Header(blockClass, -5, -4, false);
        h.U32(8);
        h.Fill(8, 0);

        h.Header(lineClass, 112, -5, true);
        h.U32(1);
        h.Raw(groupId, 16);
        h.U32(3);
        h.U8(12); h.Raw(groupId, 16); h.U32(4); h.Fill(4, 0xAB);
        h.U8(10); h.Name("List"); h.U32(1); h.U8(3); h.Name("Flag"); h.U32(7);
        h.U8(1); h.Name("Bool"); h.U8(1);
        h.U32(81);
        h.EntityPrefix(-26, -21);
        h.F64(1.5); h.F64(2.5); h.F64(0.0);
        h.F64(3.5); h.F64(4.5); h.F64(0.0);

        h.Raw(unknownClass, 16);
        h.U32(113); h.U32(static_cast<std::uint32_t>(-5)); h.U8(3); h.U8(0);
        h.U32(37);
        const std::size_t attrStart = h.Bytes.size();
        h.EntityPrefix(-26, -21);
        h.U32(0);
        h.Raw(relationList, 16);
        h.U32(0);
        h.U8(0); h.U8(0); h.U32(1); h.U32(0xf000fe00u);
        for (std::size_t i = 0; i < 33; ++i) h.U8(h.Bytes[attrStart + i]);
        h.U8(1);
        h.Raw(displayCache, 16);
        h.U32(1);
        h.Header(lineClass, 300, 113, false);
        h.U32(81);
        h.EntityPrefix(45, -21);
        h.F64(5.5); h.F64(6.5); h.F64(0.0);
        h.F64(7.5); h.F64(8.5); h.F64(0.0);
        h.U8(0);

        h.Header(ellipseClass, 114, -5, false);
        const std::size_t ellipseBody = h.BeginBody();
        h.EntityPrefix(-26, -21);
        h.F64(10.0); h.F64(20.0); h.F64(0.0);
        h.F64(0.0); h.F64(1.0); h.F64(0.0);
        h.F64(-1.0); h.F64(0.0); h.F64(0.0);
        h.F64(8.0); h.F64(3.0); h.F64(0.0); h.F64(6.283185307179586); h.F64(0.0);
        h.EndBody(ellipseBody);

        h.Header(unknownClass, 115, -5, false);
        const std::size_t unknownBody = h.BeginBody();
        h.EntityPrefix(-26, -21);
        for (double v : { 10.5, 20.5, 0.0, 30.5, 40.5, 0.0 }) h.F64(v);
        h.EndBody(unknownBody);

        ExbGeometryResult mixed;
        Check(ParseBlockStream(h.Bytes.data(), h.Bytes.size(), mixed), "stream with extension and display cache parses");
        Check(mixed.RecordCount == 4 && mixed.UnparsedBytes == 0, "extension is consumed without resync");
        Check(mixed.UnclassifiedCount == 1 && mixed.Items.size() == 3, "unknown class is not guessed");
        if (mixed.Items.size() == 3)
        {
            Check(mixed.Items[0].Kind == ExbGeometryKind::Line && mixed.Items[0].Points[1].X == 3.5,
                  "line after property tags 12 / 10 / 1");
            const ExbGeometryItem& group = mixed.Items[1];
            Check(group.Kind == ExbGeometryKind::Group && group.Children.size() == 1, "display cache becomes children");
            if (group.Children.size() == 1)
                Check(group.Children[0].Kind == ExbGeometryKind::Line && group.Children[0].Points[0].X == 5.5 &&
                          group.Children[0].Style.LayerId == 45,
                      "display cache line keeps its own attributes");
            const ExbGeometryItem& ellipse = mixed.Items[2];
            Check(ellipse.Kind == ExbGeometryKind::Ellipse && ellipse.Center.X == 10.0 && ellipse.Radius == 8.0 &&
                      ellipse.RadiusMinor == 3.0 && std::fabs(ellipse.Rotation - 1.5707963267948966) < 1e-12,
                  "ellipse axes and rotation");
        }

        // 实心填充：一个环由两条直线和一段反向存放的圆弧组成；后面跟线性渐变
        const std::uint8_t solidClass[16] = { 0x4e, 0x6d, 0x67, 0xd7, 0xe0, 0xb9, 0xc3, 0x46, 0x89, 0x4d, 0xe9, 0xef, 0x7f, 0xb6, 0x6f, 0xb2 };
        StreamBuilder f;
        f.Name("0.0");
        f.Fill(64, 0);
        f.Header(blockClass, -5, -4, false);
        f.U32(8);
        f.Fill(8, 0);
        f.Line(lineClass, 112, 0.5, 0.5, 1.5, 1.5);
        f.Header(solidClass, 113, -5, false);
        const std::size_t solidBody = f.BeginBody();
        f.EntityPrefix(-26, -21);
        f.Fill(24, 0);
        f.U32(1);
        f.U32(3);
        f.U32(1011); f.U32(1011); f.U32(1012);
        for (double v : { 0.0, 0.0, 0.0, 10.0, 0.0, 0.0 }) f.F64(v);
        for (double v : { 10.0, 0.0, 0.0, 10.0, 10.0, 0.0 }) f.F64(v);
        // 圆心 (5, 5)、半径 5√2，从 (0, 0) 逆时针 180° 到 (10, 10)：与环的走向相反
        f.F64(5.0); f.F64(5.0); f.F64(0.0);
        f.F64(7.0710678118654755);
        f.F64(-0.7071067811865476); f.F64(-0.7071067811865476); f.F64(0.0);
        f.F64(0.7071067811865476); f.F64(-0.7071067811865476); f.F64(0.0);
        f.F64(3.141592653589793);
        f.U32(0); f.U32(0);
        f.U32(0);
        f.U32(0);
        f.Fill(4 + 24 + 24 + 20 + 24 + 4 + 8, 0);
        f.U32(0);
        f.U32(1);
        f.Name("LINEAR");
        f.F64(0.5); f.F64(0.0);
        f.U32(0);
        f.F64(0.0);
        f.U32(0x00FF0000u); f.U32(0x0000FFFFu);
        f.Fill(16, 0);
        f.EndBody(solidBody);

        // 文字样式表：样式流里的一条记录
        const std::uint8_t textStyleClass[16] = { 0xd2, 0x09, 0xbd, 0xb9, 0xaa, 0x0e, 0xe7, 0x4f, 0x9f, 0xf6, 0xa2, 0x3f, 0x19, 0xfe, 0x89, 0x54 };
        StreamBuilder t;
        t.Fill(172, 0);
        t.Header(textStyleClass, 65, -40, false);
        const std::size_t styleBody = t.BeginBody();
        t.Name("Notes");
        t.U32(0);
        t.Name("Arial");
        t.Name("simsun");
        t.F64(0.8); t.F64(1.0); t.F64(0.0); t.F64(1.0);
        t.F64(3.5); t.F64(0.25); t.F64(0.0);
        t.U32(0); t.U32(0);
        t.EndBody(styleBody);
        std::vector<ExbTextStyleDef> styles;
        ParseTextStyles(t.Bytes.data(), t.Bytes.size(), styles);
        Check(styles.size() == 1, "one text style");
        if (styles.size() == 1)
            Check(styles[0].Id == 65 && styles[0].Name == "Notes" && styles[0].WesternFont == "Arial" &&
                      styles[0].CjkFont == "simsun" && styles[0].WidthFactor == 0.8 && styles[0].Height == 3.5 &&
                      styles[0].Oblique == 0.25,
                  "text style fields");

        // 三维多段线、图片
        const std::uint8_t polyline3dClass[16] = { 0x02, 0xe0, 0xce, 0xd5, 0x08, 0x64, 0x9b, 0x4d, 0xbe, 0x79, 0x6c, 0x4c, 0xf4, 0xef, 0xe4, 0xd6 };
        const std::uint8_t imageClass[16] = { 0x0a, 0x0e, 0x50, 0xb4, 0x15, 0xc7, 0x4e, 0x42, 0xbc, 0x25, 0x08, 0xa1, 0x52, 0xdb, 0xb9, 0x8b };
        StreamBuilder m;
        m.Name("0.0");
        m.Fill(64, 0);
        m.Header(blockClass, -5, -4, false);
        m.U32(8);
        m.Fill(8, 0);
        m.Line(lineClass, 112, 0.5, 0.5, 1.5, 1.5);
        m.Header(polyline3dClass, 113, -5, false);
        const std::size_t polyBody = m.BeginBody();
        m.EntityPrefix(-26, -21);
        m.U32(3);
        for (double v : { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0 }) m.F64(v);
        m.U32(1);
        m.EndBody(polyBody);
        m.Header(imageClass, 114, -5, false);
        const std::size_t imageBody = m.BeginBody();
        m.EntityPrefix(-26, -21);
        for (double v : { 0.0, 15.0, 10.5, 25.5, 0.0, 25.5, 10.5 }) m.F64(v);
        m.U32(0x00800000u); m.U32(249);
        m.Fill(2, 0);
        m.EndBody(imageBody);
        ExbGeometryResult misc;
        Check(ParseBlockStream(m.Bytes.data(), m.Bytes.size(), misc) && misc.Items.size() == 3, "3d polyline and image stream parses");
        if (misc.Items.size() == 3)
        {
            Check(misc.Items[1].Kind == ExbGeometryKind::Polyline && misc.Items[1].Points.size() == 4 &&
                      misc.Items[1].Points[1].X == 4.0 && misc.Items[1].Points[3].X == 1.0,
                  "closed 3d polyline is flattened");
            Check(misc.Items[2].Kind == ExbGeometryKind::Image && misc.Items[2].ImageRef == 249 &&
                      misc.Items[2].Points.size() == 2 && misc.Items[2].Points[1].Y == 25.5,
                  "image corners and definition id");
        }

        ExbGeometryResult fill;
        Check(ParseBlockStream(f.Bytes.data(), f.Bytes.size(), fill) && fill.Items.size() == 2, "solid fill stream parses");
        if (fill.Items.size() == 2)
        {
            const ExbGeometryItem& solid = fill.Items[1];
            Check(solid.Kind == ExbGeometryKind::Group && solid.Filled && solid.Loops.size() == 1, "solid fill has one loop");
            if (solid.Loops.size() == 1)
            {
                const ExbFillLoop& loop = solid.Loops[0];
                Check(loop.Points.size() == 4 && loop.Bulges.size() == 3, "loop is stitched end to end");
                if (loop.Points.size() == 4 && loop.Bulges.size() == 3)
                {
                    Check(std::fabs(loop.Points[2].X - 10.0) < 1e-9 && std::fabs(loop.Points[2].Y - 10.0) < 1e-9 &&
                              std::fabs(loop.Points[3].X) < 1e-9 && std::fabs(loop.Points[3].Y) < 1e-9,
                          "arc is reversed to follow the loop");
                    Check(loop.Bulges[0] == 0.0 && loop.Bulges[1] == 0.0 &&
                              std::fabs(loop.Bulges[2] + 1.0) < 1e-9,
                          "reversed arc gets a negated bulge");
                }
            }
            Check(solid.GradientName == "LINEAR" && solid.GradientAngle == 0.5 &&
                      solid.GradientColors[0] == 0x00FF0000u && solid.GradientColors[1] == 0x0000FFFFu,
                  "gradient name, angle and colors");
        }
    }

    // 纯函数段：不依赖样例，任何机器上都应全绿
    void RunPureChecks()
    {
        using namespace MiniCAD::Exb;
        RunRecordWalkChecks();

        // MText / CAXA 格式码剥离。两种写法对应同一个可见文字。
        Check(StripTextCodes("\\A3;LB1 *18") == "LB1 *18", "strip \\A3; prefix");
        Check(StripTextCodes("\\A3;{\\fsimsun|b0|i0;\\f^simsun|b0|i0;LB1 *18}") == "LB1 *18",
              "strip font block wrapper");
        Check(StripTextCodes("保险杠-TS-B1") == "保险杠-TS-B1", "plain text unchanged");
        Check(StripTextCodes("") == "", "empty text stays empty");

        // UTF-16LE：ASCII / 汉字 / 代理对（𰀀 之外用常见 emoji 区）
        {
            const std::uint8_t ascii[] = { 'A', 0, 'B', 0 };
            Check(Utf16LeToUtf8(ascii, sizeof(ascii)) == "AB", "utf16 ascii");

            // 「层」U+5C42 → 42 5C，「图」U+56FE → FE 56（小端）
            const std::uint8_t han[] = { 0x42, 0x5C, 0xFE, 0x56 };
            Check(Utf16LeToUtf8(han, sizeof(han)) == "层图", "utf16 cjk");
        }

        // 图层 ID → 下标。两条分支都要覆盖：
        //   ① 句柄精确匹配（大图的正数持久 ID 与旧代的负句柄都走这条）
        //   ② 旧代连续句柄（句柄 = 首句柄 + 定义序号）
        {
            std::vector<ExbLayerDef> layers(3);
            layers[0].Handle = 45;
            layers[0].Name   = "0 Layer";
            layers[1].Handle = 46;
            layers[1].Name   = "折线";
            layers[2].Handle = 47;
            layers[2].Name   = "轮廓线";
            Check(ResolveLayerIndex(45, layers) == 0, "layer id exact match 45");
            Check(ResolveLayerIndex(47, layers) == 2, "layer id exact match 47");
            Check(ResolveLayerIndex(999, layers) == -1, "unknown positive id is unmapped");
            Check(ResolveLayerIndex(0, layers) == -1, "zero handle never matches");

            std::vector<ExbLayerDef> legacy(3);
            for (int i = 0; i < 3; ++i) legacy[i].Handle = -26 + i;
            Check(ResolveLayerIndex(-26, legacy) == 0, "legacy first handle");
            Check(ResolveLayerIndex(-24, legacy) == 2, "legacy handle offset");
            Check(ResolveLayerIndex(-24, legacy) == 2, "legacy handle is stable");
            Check(ResolveLayerIndex(-30, legacy) == -1, "legacy out-of-range handle");

            const std::vector<ExbLayerDef> none;
            Check(ResolveLayerIndex(45, none) == -1, "empty layer table is unmapped");
        }
    }

    // 记录种类 → 名字，打印用
    const char* KindName(MiniCAD::Exb::ExbGeometryKind kind)
    {
        using K = MiniCAD::Exb::ExbGeometryKind;
        switch (kind)
        {
        case K::Polyline: return "polyline";
        case K::Line:     return "line";
        case K::Circle:   return "circle";
        case K::Arc:      return "arc";
        case K::Point:    return "point";
        case K::Text:     return "text";
        case K::Insert:   return "insert";
        case K::Ellipse:  return "ellipse";
        case K::Ray:      return "ray";
        case K::XLine:    return "xline";
        case K::Group:    return "group";
        default:          return "unknown";
        }
    }
}

int main(int argc, char** argv)
{
    // 行式输出 + 立即刷新：万一样例把解析器打崩，最后一行就是崩溃点
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::cout.setf(std::ios::unitbuf);
    RunPureChecks();

    std::string sample;
    if (argc > 1) sample = argv[1];
    if (sample.empty())
    {
        const char* env = std::getenv("MINICAD_EXB_SAMPLE");
        if (env) sample = env;
    }

#ifdef MINICAD_TEST_DATA_DIR
    bool pinned = false;
    if (sample.empty())
    {
        const std::string local = std::string(MINICAD_TEST_DATA_DIR) + "/Exb/sample_AC2013.exb";
        std::error_code ec;
        if (std::filesystem::exists(MakePath(local), ec))
        {
            sample = local;
            pinned = true;
        }
    }
#else
    const bool pinned = false;
#endif

    if (sample.empty())
    {
        std::cout << "SKIP|no EXB sample (pure checks only)\n";
        if (g_failures != 0) { std::printf("RESULT|FAIL|%d\n", g_failures); return 1; }
        std::printf("RESULT|PASS|0\n");
        return 0;
    }

    std::vector<std::uint8_t> bytes;
    if (!ReadFile(sample, bytes))
    {
        std::cout << "SKIP|cannot read sample: " << sample << "\n";
        std::printf("RESULT|PASS|0\n");
        return 0;
    }

    // ── 容器 ────────────────────────────────────────────────────────────
    MiniCAD::Exb::ExbReport report;
    std::string error;
    // 默认只留哈希不留字节（大图纸省一份内存）；这个测试要自己解样式流与块流，
    // 所以必须显式打开 KeepLogicalBytes，否则 StreamInfo::Logical 恒为空。
    MiniCAD::Exb::ReadOptions options;
    options.KeepLogicalBytes = true;
    if (!MiniCAD::Exb::ProbeExb(bytes.data(), bytes.size(), report, &error, options))
    {
        std::cout << "FAIL|ProbeExb: " << error << "\n";
        std::printf("RESULT|FAIL|%d\n", g_failures + 1);
        return 1;
    }

    // ── 样式流：图层表 + 线型表 ────────────────────────────────────────
    const MiniCAD::Exb::StreamInfo* styleStream = nullptr;
    const MiniCAD::Exb::StreamInfo* blockStream = nullptr;
    for (const MiniCAD::Exb::StreamInfo& stream : report.Streams)
    {
        if (stream.Logical.empty()) continue;
        if (stream.Path.find("StyleStream") != std::string::npos) styleStream = &stream;
        // 主几何块：Blk_fffffffb（块内序号从 107 起、每条 8 字节头的那一个）
        if (stream.Path.find("Blk_fffffffb") != std::string::npos) blockStream = &stream;
    }

    if (const char* dir = std::getenv("MINICAD_EXB_STREAMDIR"))
    {
        std::size_t index = 0;
        for (const MiniCAD::Exb::StreamInfo& stream : report.Streams)
        {
            const std::string file = std::string(dir) + "/" + std::to_string(index++) + ".bin";
            if (std::FILE* f = std::fopen(file.c_str(), "wb"))
            {
                if (!stream.Logical.empty())
                    std::fwrite(stream.Logical.data(), 1, stream.Logical.size(), f);
                std::fclose(f);
            }
            std::printf("STREAMFILE|%s|%s|%zu\n", file.c_str(), stream.Path.c_str(), stream.Logical.size());
        }
    }

    Check(styleStream != nullptr, "style stream exists and decodes");
    Check(blockStream != nullptr, "main geometry block stream exists and decodes");
    if (!styleStream || !blockStream)
    {
        std::printf("RESULT|FAIL|%d\n", g_failures);
        return 1;
    }

    std::vector<MiniCAD::Exb::ExbLayerDef> layers;
    MiniCAD::Exb::ParseStyleStream(styleStream->Logical.data(), styleStream->Logical.size(), layers);
    Check(!layers.empty(), "layer table is not empty");

    std::vector<MiniCAD::Exb::ExbLineTypeDef> lineTypes;
    MiniCAD::Exb::ParseLineTypeTable(styleStream->Logical.data(), styleStream->Logical.size(), lineTypes);
    Check(!lineTypes.empty(), "linetype table is not empty");

    std::printf("LAYERS|%zu\n", layers.size());
    for (const MiniCAD::Exb::ExbLayerDef& def : layers)
    {
        std::printf("LAYER|%d|%s|%06X|%d|%.3f|%s%s\n", def.Handle, def.Name.c_str(),
                    def.ColorWord & 0xFFFFFFu, def.LineTypeId, def.LineWidth, def.Visible ? "on" : "off",
                    def.Locked ? ",locked" : "");
        Check(!def.Name.empty(), "layer has a name");
        Check(def.LineWidth >= 0.0 && def.LineWidth <= 10.0, "layer line width in range");
    }

    std::vector<MiniCAD::Exb::ExbTextStyleDef> textStyles;
    MiniCAD::Exb::ParseTextStyles(styleStream->Logical.data(), styleStream->Logical.size(), textStyles);
    for (const MiniCAD::Exb::ExbTextStyleDef& style : textStyles)
        std::printf("TEXTSTYLE|%d|%s|%s|%s|%.3f|%.3f|%.4f\n", style.Id, style.Name.c_str(), style.WesternFont.c_str(),
                    style.CjkFont.c_str(), style.WidthFactor, style.Height, style.Oblique);

    std::printf("LINETYPES|%zu\n", lineTypes.size());
    for (const MiniCAD::Exb::ExbLineTypeDef& lt : lineTypes)
    {
        std::printf("LINETYPE|%d|%s|%s|%.4g|%zu\n", lt.Id, lt.Name.c_str(), lt.Alt.c_str(),
                    lt.Scale, lt.Dashes.size());
        Check(lt.Scale > 0.0, "linetype scale is positive");
        Check((lt.Dashes.size() % 2) == 0, "linetype dashes alternate on/off");
        for (double dash : lt.Dashes)
            Check(dash >= 0.0, "linetype dash length is not negative");
    }

    // ── 块流：记录 → 几何 ──────────────────────────────────────────────
    MiniCAD::Exb::ExbGeometryResult geometry;
    const bool parsed = MiniCAD::Exb::ParseBlockStream(blockStream->Logical.data(),
                                                       blockStream->Logical.size(), geometry);
    Check(parsed, "block stream parses");
    // 空图纸（blank 这类新建文件）只有 1 条记录、0 个实体，也是合法结果，
    // 所以这里只校验「计数自洽」，不要求一定有几何。
    Check(geometry.UnclassifiedCount <= geometry.RecordCount,
          "unclassified records never exceed record count");

    // 分类计数必须与实际吐出的条目一致，否则说明计数与 Items 不同步
    std::size_t counted = geometry.TextCount + geometry.PolylineCount + geometry.LineCount +
                          geometry.CircleCount + geometry.ArcCount + geometry.PointCount + geometry.InsertCount +
                          geometry.OtherCount;
    Check(counted == geometry.Items.size(), "per-kind counters match item count");

    std::printf("RECORDS|%zu|%zu|%zu\n", geometry.RecordCount, geometry.Items.size(),
                geometry.UnclassifiedCount);
    std::printf("KINDS|text=%zu|poly=%zu|line=%zu|circle=%zu|arc=%zu|point=%zu\n",
                geometry.TextCount, geometry.PolylineCount, geometry.LineCount,
                geometry.CircleCount, geometry.ArcCount, geometry.PointCount);
    if (pinned)
    {
        Check(geometry.RecordCount == 226 && geometry.UnparsedBytes == 0, "sample: all 226 records walked");
        Check(geometry.CircleCount == 8 && geometry.ArcCount == 1 && geometry.PointCount == 6, "sample: circles, arc, points");
        Check(geometry.TextCount >= 49, "sample: every text record is read");
        Check(geometry.InsertCount == 29, "sample: every block reference is read");
        Check(geometry.UnclassifiedCount == 0, "sample: every record is decoded");
        Check(textStyles.size() == 5, "sample: text style table");
        std::size_t images = 0, rightAligned = 0, styled = 0;
        for (const MiniCAD::Exb::ExbGeometryItem& item : geometry.Items)
        {
            using K = MiniCAD::Exb::ExbGeometryKind;
            if (item.Kind == K::Image && item.ImageRef != 0) ++images;
            if (item.Kind == K::Text && item.Align % 3 == 2) ++rightAligned;
            if (item.Kind == K::Text && item.StyleId != 0) ++styled;
        }
        Check(images == 1 && rightAligned >= 1 && styled >= 49, "sample: image, text alignment and styles");
        std::size_t ellipses = 0, groups = 0, emptyGroups = 0, wrapped = 0;
        for (const MiniCAD::Exb::ExbGeometryItem& item : geometry.Items)
        {
            using K = MiniCAD::Exb::ExbGeometryKind;
            if (item.Kind == K::Ellipse) ++ellipses;
            if (item.Kind == K::Group) { ++groups; if (item.Children.empty()) ++emptyGroups; }
            if (item.Kind == K::Text && item.BoxWidth > 0.0) ++wrapped;
        }
        Check(ellipses == 1 && groups >= 30 && emptyGroups == 0, "sample: ellipse, dimensions and fills");
        Check(wrapped >= 5, "sample: multiline text keeps its wrap width");
        std::size_t fills = 0, gradients = 0;
        for (const MiniCAD::Exb::ExbGeometryItem& item : geometry.Items)
        {
            if (item.Filled && !item.Loops.empty()) ++fills;
            if (!item.GradientName.empty()) ++gradients;
        }
        Check(fills >= 18 && gradients == 2, "sample: solid fills have loops, two of them are gradients");
        std::size_t unitTexts = 0;
        for (const MiniCAD::Exb::ExbGeometryItem& item : geometry.Items)
            if (item.Plain == "Hello this is a single line text" && item.Height == 1.0) ++unitTexts;
        Check(unitTexts == 8, "sample: single line text height");
        std::size_t closed = 0, bulged = 0;
        for (const MiniCAD::Exb::ExbGeometryItem& item : geometry.Items)
        {
            if (item.Closed) ++closed;
            for (double bulge : item.Bulges)
                if (bulge != 0.0) { ++bulged; break; }
        }
        Check(closed == 25 && bulged == 2, "sample: closed polylines and arc segments");
        for (const MiniCAD::Exb::StreamInfo& stream : report.Streams)
        {
            if (stream.Logical.empty() || stream.Path.find("GBlock") == std::string::npos) continue;
            std::vector<MiniCAD::Exb::ExbBlockDef> blocks;
            Check(MiniCAD::Exb::ParseBlockDefinitions(stream.Logical.data(), stream.Logical.size(), blocks) &&
                      blocks.size() == 31, "sample: 31 block definitions");
        }
    }
    if (std::getenv("MINICAD_EXB_DUMP"))
    {
        std::printf("BLOCKSIZE|%zu|unparsed=%zu\n", blockStream->Logical.size(), geometry.UnparsedBytes);
        for (const auto& [classId, count] : geometry.UnclassifiedByClass)
            std::printf("UNCLASSIFIED|%s|%zu\n", classId.c_str(), count);
        const std::function<void(const MiniCAD::Exb::ExbGeometryItem&, int)> printItem =
            [&printItem](const MiniCAD::Exb::ExbGeometryItem& item, int depth)
        {
            using K = MiniCAD::Exb::ExbGeometryKind;
            const MiniCAD::Exb::ExbPoint2 p = !item.Points.empty() ? item.Points.front()
                : (item.Kind == K::Text || item.Kind == K::Insert) ? item.Insert : item.Center;
            std::printf("%s|%zu|%zu|%.8s|%d|%s|layer=%d|lt=%d|color=%08X|n=%zu|%.3f,%.3f|r=%.3f|%s",
                        depth == 0 ? "ITEM" : "SUB", item.Offset, item.Size, item.ClassId.c_str(), item.Id,
                        KindName(item.Kind), item.Style.LayerId, item.Style.LineTypeId, item.Style.ColorWord,
                        item.Points.size(), p.X, p.Y, item.Radius, item.Plain.c_str());
            if (item.Kind == K::Insert)
                std::printf("block=%d|scale=%.4f,%.4f|rot=%.4f", item.BlockRef, item.ScaleX, item.ScaleY, item.Rotation);
            if (item.Kind == K::Text)
                std::printf("|style=%d|align=%u|h=%.3f|box=%.3f", item.StyleId, item.Align, item.Height, item.BoxWidth);
            if (item.Filled)
                std::printf("|fill loops=%zu gradient=%s angle=%.4f shift=%.3f single=%d tint=%.3f colors=%06X,%06X",
                            item.Loops.size(), item.GradientName.c_str(), item.GradientAngle, item.GradientShift,
                            item.GradientSingleColor ? 1 : 0, item.GradientTint, item.GradientColors[0] & 0xFFFFFFu,
                            item.GradientColors[1] & 0xFFFFFFu);
            std::printf("|modes=%u%u%u|children=%zu\n", item.Style.ColorMode, item.Style.LineTypeMode,
                        item.Style.WidthMode, item.Children.size());
            if (depth < 1)
                for (const MiniCAD::Exb::ExbGeometryItem& child : item.Children) printItem(child, depth + 1);
        };
        for (const MiniCAD::Exb::ExbGeometryItem& item : geometry.Items) printItem(item, 0);
        for (const MiniCAD::Exb::StreamInfo& stream : report.Streams)
        {
            if (stream.Logical.empty() || stream.Path.find("GBlock") == std::string::npos) continue;
            std::vector<MiniCAD::Exb::ExbBlockDef> blocks;
            MiniCAD::Exb::ParseBlockDefinitions(stream.Logical.data(), stream.Logical.size(), blocks);
            for (const MiniCAD::Exb::ExbBlockDef& def : blocks)
            {
                std::printf("BLOCK|%d|%s|records=%zu|unclassified=%zu\n", def.Id, def.Name.c_str(),
                            def.RecordCount, def.UnclassifiedCount);
                for (const MiniCAD::Exb::ExbGeometryItem& item : def.Items) printItem(item, 0);
            }
        }
    }
    if (geometry.Items.empty())
        std::printf("INFO|empty drawing (no classified geometry)\n");

    // 几何数值的卫生检查：坐标不能是 NaN/Inf，半径必须为正，折线至少 2 点。
    std::size_t badCoord = 0, badRadius = 0, badPoly = 0;
    for (const MiniCAD::Exb::ExbGeometryItem& item : geometry.Items)
    {
        using K = MiniCAD::Exb::ExbGeometryKind;
        if (item.Kind == K::Polyline)
        {
            if (item.Points.size() < 2) ++badPoly;
            for (const MiniCAD::Exb::ExbPoint2& p : item.Points)
                if (!std::isfinite(p.X) || !std::isfinite(p.Y)) ++badCoord;
        }
        else if (item.Kind == K::Line)
        {
            if (item.Points.size() != 2) ++badPoly;
            for (const MiniCAD::Exb::ExbPoint2& p : item.Points)
                if (!std::isfinite(p.X) || !std::isfinite(p.Y)) ++badCoord;
        }
        else if (item.Kind == K::Circle || item.Kind == K::Arc)
        {
            if (!std::isfinite(item.Center.X) || !std::isfinite(item.Center.Y)) ++badCoord;
            if (!(item.Radius > 0.0) || !std::isfinite(item.Radius)) ++badRadius;
        }
        else if (item.Kind == K::Text)
        {
            if (!std::isfinite(item.Insert.X) || !std::isfinite(item.Insert.Y)) ++badCoord;
            if (item.Plain.empty()) ++badPoly;   // 文字必须有可见内容
            // 旋转角只在 [0, 2π) 内才算读到；越界视为未解析
            Check(item.Rotation >= 0.0 && item.Rotation < 6.283185307179587,
                  "text rotation within one turn");
        }
    }
    Check(badCoord == 0, "all coordinates are finite");
    Check(badRadius == 0, "all radii are positive");
    Check(badPoly == 0, "points/vertex counts and text bodies are sane");
    if (badCoord || badRadius || badPoly)
        std::printf("BAD|coord=%zu|radius=%zu|shape=%zu\n", badCoord, badRadius, badPoly);

    // ── 端到端：ImportExb → Scene ─────────────────────────────────────
    MiniCAD::Scene scene;
    MiniCAD::Exb::ExbImportResult result;
    std::string importError;
    const bool imported = MiniCAD::Exb::ImportExb(bytes, scene, &importError, &result, nullptr, sample);
    Check(imported, "ImportExb succeeds");
    Check(result.ContainerOk, "import reports container ok");
    if (!imported)
        std::cout << "FAIL|import error: " << importError << "\n";

    // 空图纸应当导入成空场景；有几何的图纸必须真的建出实体。
    if (geometry.Items.empty())
        Check(result.EntityCount == 0, "empty drawing imports as an empty scene");
    else
        Check(result.EntityCount > 0, "entities were created in the scene");

    const auto layerIds = scene.GetLayerManager().GetAllLayerIDs();
    Check(!layerIds.empty(), "scene has layers");
    for (const MiniCAD::LayerID id : layerIds)
    {
        const MiniCAD::Layer* layer = scene.GetLayerManager().GetLayer(id);
        Check(layer != nullptr, "layer lookup by id");
        if (layer) Check(!layer->GetName().empty(), "scene layer has a name");
    }

    std::size_t entityTotal = 0;
    for (const MiniCAD::Scene::ObjectID id : scene.GetAllIDs())
    {
        const MiniCAD::Object* object = scene.GetEntity(id);
        const auto* entity = object ? dynamic_cast<const MiniCAD::Entity*>(object) : nullptr;
        if (!entity) continue;
        ++entityTotal;
        const MiniCAD::Layer* layer = scene.GetLayerManager().GetLayer(entity->GetAttr().LayerId);
        Check(layer != nullptr, "every entity belongs to a real layer");
    }
    Check(entityTotal == result.EntityCount, "entity count matches scene contents");

    MiniCAD::AABB extents = MiniCAD::AABB::Empty();
    const bool haveExtents = scene.GetExtents(extents);
    if (entityTotal > 0)
    {
        Check(haveExtents, "scene has extents");
    }
    if (haveExtents && entityTotal > 0)
    {
        std::printf("EXTENTS|%.3f|%.3f|%.3f|%.3f\n", extents.Min.x, extents.Max.x,
                    extents.Min.y, extents.Max.y);
        Check(std::isfinite(extents.Min.x) && std::isfinite(extents.Max.x) &&
                  std::isfinite(extents.Min.y) && std::isfinite(extents.Max.y),
              "extents are finite");
        Check(extents.Max.x > extents.Min.x && extents.Max.y > extents.Min.y,
              "extents describe a non-empty box");
    }

    std::printf("SCENE|entities=%zu|layers=%zu|skipped=%zu\n", result.EntityCount,
                layerIds.size(), result.SkippedRecords);

    if (g_failures != 0)
    {
        std::printf("RESULT|FAIL|%d\n", g_failures);
        return 1;
    }
    std::printf("RESULT|PASS|0\n");
    return 0;
}

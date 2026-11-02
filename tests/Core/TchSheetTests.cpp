#include "TchCipherWriter.h"
#include "Import/TchSheet.h"
#include "Dwg/Read/DwgReader.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace MiniDWG;
namespace Tch = MiniCAD::Tch;

namespace
{
    int g_failed = 0;
    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        g_failed += ok ? 0 : 1;
    }

    bool Near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

    // 写表格的各层：cipher 时子对象用最新版本、密钥在 0x13 / 0 / 0x12 / 0x14 / 0x11 间轮换（第二个密钥按 0 加密），
    // 否则用最早带全部字段的明文版本
    struct SheetWriter
    {
        TchTest::CipherWriter& W;
        bool Cipher;
        int Next = 0;

        int NextKey() { static const int keys[] = {0x13, 0, 0x12, 0x14, 0x11}; return Cipher ? keys[Next++ % 5] : 0; }
        std::pair<int, int> Open(int version, int keyFrom)
        {
            const std::pair<int, int> outer{W.Key, W.Key2};
            W.Main.WriteByte(static_cast<std::uint8_t>(version));
            W.Key2 = 0;
            W.Key = version >= keyFrom ? NextKey() : 0;
            if (version >= keyFrom) W.Main.WriteByte(static_cast<std::uint8_t>(W.Key));
            return outer;
        }
        void Close(std::pair<int, int> outer) { W.Key = outer.first; W.Key2 = outer.second; }

        void Style(int flags, int hor, int ver)
        {
            const auto o = Open(Cipher ? 4 : 2, 3);
            W.Byte(flags);
            if (flags & 1) W.Byte(hor);
            if (flags & 2) W.Byte(ver);
            if (flags & 4) W.Handle(0x20);
            if (flags & 8) W.Short(0);
            if (flags & 0x10) W.Byte(0);
            if (flags & 0x20) W.Double(0.4);
            if (flags & 0x40) W.Double(3.5);
            W.Byte(0);
            Close(o);
        }
        void Divider(int kind)
        {
            const auto o = Open(Cipher ? 3 : 1, 2);
            W.Byte(kind);
            if (kind == 1) { W.Short(0); W.Handle(0); W.Double(0); }
            Close(o);
        }
        // range ≥ 0：属于该合并区；否则写样式与内容
        void Cell(int range, const char* text, int flags = 0, int hor = 1, int ver = 2)
        {
            const auto o = Open(Cipher ? 4 : 2, 3);
            W.Short(range + 1);
            if (range < 0) { Style(flags, hor, ver); W.Text.WriteVariableText(text); }
            W.Handle(0);
            Close(o);
        }
        void Row(int divider, int mode, double height)
        {
            const std::pair<int, int> outer{W.Key, W.Key2};
            const int version = Cipher ? 3 : 1;
            W.Main.WriteByte(static_cast<std::uint8_t>(version));
            W.Key2 = 0;
            Divider(divider);
            W.Key = version >= 2 ? NextKey() : 0;
            if (version >= 2) W.Main.WriteByte(static_cast<std::uint8_t>(W.Key));
            W.Byte(mode); W.Double(height); W.Byte(0);
            Close(outer);
        }
        void Column(double width)
        {
            const auto o = Open(Cipher ? 3 : 1, 2);
            Style(0, 0, 0);
            Divider(2);
            W.Byte(1);
            W.Double(width);
            Close(o);
        }
    };

    // 2 列（宽 30、50）× 3 行（默认行高 7、10、默认），第 0 列第 0～1 行合并为“合并”；标题“表头”显示并框在外框内；
    // 第 2 行第 0 列左下对齐。插入点 (1000, 2000) 为左上角，比例 100。
    // cipher：新版（基类密钥 18、第二个密钥 94，表格层密钥 0x14），否则旧版明文（基类版本 5、表格版本 4）
    std::unique_ptr<UnknownEntity> SheetObject(bool cipher)
    {
        TchTest::CipherWriter w;
        if (cipher) w.Base(0x12, 94, 100, 94);
        else w.PlainBase(100);
        SheetWriter s{w, cipher};
        w.Main.WriteByte(cipher ? 5 : 4);
        s.Style(0x7f, 1, 2);
        s.Divider(1); s.Divider(2); s.Divider(2);
        s.Row(1, 4, 7);                                     // 标题行
        s.Cell(-1, "表头", 0x7f, 1, 2);                     // i18n: 数据
        const auto o = s.Open(cipher ? 3 : 1, 2);
        w.Byte(1);                                          // 显示标题
        s.Close(o);
        if (cipher) { w.Key = 0x14; w.Main.WriteByte(0x14); }
        else w.Key = 0;
        w.Short(2);
        s.Column(30); s.Column(50);
        w.Short(3);
        s.Row(2, 4, 99); s.Row(2, 0, 10); s.Row(2, 4, 99);
        w.Short(1);
        s.Cell(-1, "合并", 3, 1, 2);                        // i18n: 数据
        const auto r = s.Open(cipher ? 4 : 2, 3);
        w.Short(0); w.Short(0); w.Short(1); w.Short(0);
        s.Close(r);
        s.Cell(0, ""); s.Cell(-1, "A");
        s.Cell(0, ""); s.Cell(-1, "B");
        s.Cell(-1, "C", 3, 0, 1); s.Cell(-1, "D");
        w.Double(7);
        w.Byte(1);
        w.Point3(1000, 2000, 0);
        w.Double(0);
        w.Byte(0);
        return w.Take("TCH_SHEET");
    }

    const Tch::TchGraphics::Text* FindText(const Tch::TchGraphics& g, const std::string& value)
    {
        for (const auto& t : g.Texts)
            if (t.Value == value) return &t;
        return nullptr;
    }
    bool HasLine(const Tch::TchGraphics& g, double x1, double y1, double x2, double y2)
    {
        for (const auto& l : g.Lines)
            if ((Near(l.A.X, x1) && Near(l.A.Y, y1) && Near(l.B.X, x2) && Near(l.B.Y, y2)) ||
                (Near(l.A.X, x2) && Near(l.A.Y, y2) && Near(l.B.X, x1) && Near(l.B.Y, y1)))
                return true;
        return false;
    }

    std::vector<std::uint8_t> ReadFile(const std::string& path)
    {
        const std::u8string u8(path.begin(), path.end());      // 路径含中文，按 UTF-8 解释
        std::ifstream f(std::filesystem::path(u8), std::ios::binary);
        return {std::istreambuf_iterator<char>(f), {}};
    }
}

int main()
{
    // 合成表格：新版与旧版同一布局，解出相同的内容
    for (const bool cipher : {true, false})
    {
        const std::string kind = cipher ? "encrypted" : "plain";
        Tch::TchSheet t;
        std::string why;
        const bool ok = Tch::DecodeSheet(*SheetObject(cipher), t, &why);
        Check(ok, "synthetic " + kind + " sheet decodes" + (ok ? "" : ": " + why));
        if (!ok) continue;
        Check(t.Columns.size() == 2 && Near(t.Columns[0].Width, 30) && Near(t.Columns[1].Width, 50) && t.Rows.size() == 3 &&
              t.Rows[1].HeightMode == 0 && Near(t.Rows[1].Height, 10) && Near(t.RowHeight, 7) && Near(t.Scale, 100),
              "synthetic " + kind + " sheet: columns, rows and row height");
        Check(t.TitleVisible && t.Title.Text == "表头" && t.TitleRow.Line.Kind == 1 && t.Border.Kind == 1 &&    // i18n: 数据
              t.Style.Flags == 0x7f && Near(t.Style.TextHeight, 3.5) && Near(t.Style.Margin, 0.4) && t.Style.TextStyle == 0x20,
              "synthetic " + kind + " sheet: title, border and table style (sub-objects use second key 0)");
        Check(t.Ranges.size() == 1 && t.Ranges[0].Row1 == 1 && t.Ranges[0].Column1 == 0 && t.Ranges[0].Content.Text == "合并" &&   // i18n: 数据
              t.Cells.size() == 6 && t.Cells[0].Range == 0 && t.Cells[2].Range == 0 && t.Cells[1].Text == "A" && t.Cells[5].Text == "D" &&
              Near(t.X, 1000) && Near(t.Y, 2000) && t.Anchor == 1,
              "synthetic " + kind + " sheet: merged range, cells and insertion point");
    }

    // 天正的画法：表宽 8000、行高 700 / 1000 / 700、标题行高 700，插入点为含标题的左上角
    {
        Tch::TchSheet t;
        Tch::DecodeSheet(*SheetObject(true), t);
        const auto g = Tch::BuildSheet(t);
        Check(g.Lines.size() == 8 && HasLine(g, 1000, 2000, 9000, 2000) && HasLine(g, 9000, 2000, 9000, -1100) &&
              HasLine(g, 9000, -1100, 1000, -1100) && HasLine(g, 1000, -1100, 1000, 2000),
              "border frames the title and the body");
        Check(HasLine(g, 1000, 1300, 9000, 1300), "title separator one title row below the top");
        Check(HasLine(g, 4000, 600, 9000, 600) && HasLine(g, 1000, -400, 9000, -400), "row lines skip the merged cells they cross");
        Check(HasLine(g, 4000, 1300, 4000, -1100), "column line runs the full body height");
        const auto* title = FindText(g, "表头");     // i18n: 数据
        const auto* a = FindText(g, "A");
        const auto* merged = FindText(g, "合并");    // i18n: 数据
        const auto* c = FindText(g, "C");
        Check(title && Near(title->Center.X, 5000) && Near(title->Center.Y, 1650) && title->Attachment == 5,
              "title text sits centred in the title row");
        Check(a && Near(a->Center.X, 6500) && Near(a->Center.Y, 950) && a->Attachment == 5 && a->Color == 0,
              "cell text centred in its cell, coloured by block");
        Check(merged && Near(merged->Center.X, 2500) && Near(merged->Center.Y, 450), "merged range text centred in the merged box");
        Check(c && Near(c->Center.X, 1140) && Near(c->Center.Y, -960) && c->Attachment == 7,
              "bottom-left text keeps one margin (0.4 x text height x scale) from the lines");

        auto rotated = t;
        rotated.Rotation = std::numbers::pi / 2;
        Check(HasLine(Tch::BuildSheet(rotated), 1000, 2000, 1000, 10000), "rotation turns the table about the insertion point");
        auto bottomRight = t;
        bottomRight.Anchor = 9;
        Check(HasLine(Tch::BuildSheet(bottomRight), -7000, 5100, 1000, 5100) && HasLine(Tch::BuildSheet(bottomRight), 1000, 5100, 1000, 2000),
              "anchor 9 puts the insertion point at the bottom right of table and title");
    }

    // 换行判断的宽度：上标 ^U…^U 的标记不占宽、上标字按 0.35 个步进（电气样图的“km^U2^U”以前被当成放不下而折行）
    {
        const double plain = Tch::Detail::SheetTextWidth("km", 100, {}), sup = Tch::Detail::SheetTextWidth("km^U2^U", 100, {});
        const double digit = Tch::Detail::SheetTextWidth("2", 100, {});
        Check(std::abs(sup - plain - 0.35 * digit) < 1e-9, "superscript markers take no width, superscript digits 0.35 of a step");
    }

    // 本机样图（不在版本库里，没有时跳过）：新版（tianzheng2.dwg，T30 写的 2007 格式）、旧版 R2013 与 R2004（天正电气）
    struct Sample { const char* File; int Sheets; };
    const Sample samples[] = {
        {"tianzheng2.dwg", 3},
        {"TchElec/CK-02 施工说明及系统图.dwg", 1},           // i18n: 数据
        {"TchElec/1#楼-03照明、弱电、消防平面图.dwg", 1},     // i18n: 数据
    };
    for (const auto& sample : samples)
    {
        const auto data = ReadFile(std::string(MINICAD_TEST_DATA_DIR) + "/" + sample.File);
        if (data.empty()) { std::printf("[SKIP] %s（没有样图）\n", sample.File); continue; }
        const auto db = ReadDwg(data);
        if (!db) { Check(false, std::string(sample.File) + " reads"); continue; }
        int total = 0, decoded = 0;
        std::vector<Tch::TchSheet> sheets;
        for (const auto& [handle, object] : db->Objects())
        {
            const auto* e = dynamic_cast<const UnknownEntity*>(object.get());
            if (!e || e->Raw.DxfName != "TCH_SHEET") continue;
            ++total;
            Tch::TchSheet t;
            std::string why;
            if (Tch::DecodeSheet(*e, t, &why)) { ++decoded; sheets.push_back(std::move(t)); }
            else std::printf("  %llx: %s\n", static_cast<unsigned long long>(handle), why.c_str());
        }
        Check(total == sample.Sheets && decoded == total, std::string(sample.File) + ": " + std::to_string(decoded) + " of " +
              std::to_string(total) + " sheets decode");
        auto has = [&](auto pred) { for (const auto& t : sheets) if (pred(t)) return true; return false; };
        if (std::string(sample.File) == "tianzheng2.dwg")
            Check(has([](const Tch::TchSheet& t) {
                      return t.Title.Text == "门窗表" && t.Columns.size() == 8 && t.Rows.size() == 3 && t.Cells[18].Text == "1800X1500";  // i18n: 数据
                  }), "tianzheng2.dwg: door and window schedule content");
        else if (std::string(sample.File).find("CK-02") != std::string::npos)
            Check(has([](const Tch::TchSheet& t) { return t.Columns.size() == 9 && t.Rows.size() == 27 && t.Ranges.size() == 22 &&
                                                          t.Ranges[0].Content.Text == "项目名称"; }),       // i18n: 数据
                  "R2013 plain sheet sample with merged ranges");
        else
            Check(has([](const Tch::TchSheet& t) { return t.Columns.size() == 3 && t.Rows.size() == 9 && t.Ranges.size() == 3 &&
                                                          t.Ranges[0].Content.Text == "建筑物数据" && t.Ranges[0].Row1 == 4; }),   // i18n: 数据
                  "1#-03: R2004 sheet with inline strings");    }

    std::printf("%s\n", g_failed ? "FAILED" : "ALL PASSED");
    return g_failed ? 1 : 0;
}

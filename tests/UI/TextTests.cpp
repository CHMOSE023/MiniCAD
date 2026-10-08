#include "TestFramework.h"
#include "TestUtils.h"
#include "Paint/DrawList.h"
#include "Text/Font.h"
#include "Text/Utf8.hpp"
#include "Widgets/Label.h"
#include <cstdlib>
#include <string>
#include <vector>

using namespace MiniGUI;
using namespace MiniGUI::Test;

namespace
{
    std::vector<uint32_t> DecodeAll(std::string_view s)
    {
        std::vector<uint32_t> out;
        size_t pos = 0;
        while (pos < s.size())
            out.push_back(DecodeUtf8(s, pos));
        return out;
    }

    // 系统字体（微软雅黑 UI）；找不到时依赖字体的用例跳过
    std::shared_ptr<Font> TestFont()
    {
        static std::shared_ptr<Font> font = []() -> std::shared_ptr<Font>
        {
            char* windir = nullptr;
            size_t len = 0;
            if (_dupenv_s(&windir, &len, "WINDIR") != 0 || !windir)
                return nullptr;
            std::string path = std::string(windir) + "\\Fonts\\msyh.ttc";
            std::free(windir);
            return Font::LoadFromFile(path, 1);
        }();
        return font;
    }

#define REQUIRE_FONT()                                              \
    if (!TestFont()) { std::printf("    （没有系统字体，跳过）\n"); return; }

    std::string CJKRun(uint32_t first, int count)
    {
        std::string s;
        for (uint32_t cp = first; cp < first + static_cast<uint32_t>(count); ++cp)
        {
            s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        return s;
    }

    struct TextFixture
    {
        NullBackend backend;
        TextSystem  text{ &backend };
        TextParams  params;

        TextFixture() { text.AddFont(TestFont()); }
    };
}

// ── UTF-8 ────────────────────────────────────────────────────────

TEST(Utf8_DecodesMixedText)
{
    const auto cps = DecodeAll("A\xE4\xB8\xAD\xF0\x9F\x98\x80");   // "A中😀"
    CHECK(cps.size() == 3);
    CHECK(cps[0] == 0x41);
    CHECK(cps[1] == 0x4E2D);
    CHECK(cps[2] == 0x1F600);
}

TEST(Utf8_InvalidSequencesBecomeReplacement)
{
    CHECK(DecodeAll("\xC0\x80") == std::vector<uint32_t>({ kReplacementChar, kReplacementChar }));   // 过长编码
    CHECK(DecodeAll("\xED\xA0\x80").front() == kReplacementChar);                                    // 代理区
    CHECK(DecodeAll("\xE4\xB8") == std::vector<uint32_t>({ kReplacementChar, kReplacementChar }));   // 截断
    CHECK(DecodeAll("a\xFFz") == std::vector<uint32_t>({ 'a', kReplacementChar, 'z' }));
}

TEST(Utf8_CJKRanges)
{
    CHECK(IsCJK(0x4E2D));     // 中
    CHECK(IsCJK(0x3002));     // 。
    CHECK(IsCJK(0xFF0C));     // ，
    CHECK(!IsCJK('A'));
    CHECK(!IsCJK(0x00E9));    // é
}

// ── 测量与换行 ───────────────────────────────────────────────────

TEST(Text_MeasureSingleLine)
{
    REQUIRE_FONT();
    TextFixture f;
    const float lh = f.text.GetLineHeight(14.0f);
    CHECK(lh > 14.0f && lh < 25.0f);

    const Vec2 empty = f.text.Measure("", f.params);
    CHECK_NEAR(empty.x, 0);
    CHECK_NEAR(empty.y, lh);

    const Vec2 a = f.text.Measure("Hello", f.params);
    const Vec2 b = f.text.Measure("Hello, 世界", f.params);
    CHECK(a.x > 20.0f);
    CHECK(b.x > a.x);
    CHECK_NEAR(b.y, lh);
}

TEST(Text_WrapCJKBetweenAnyCharacters)
{
    REQUIRE_FONT();
    TextFixture f;
    f.params.wrap = true;

    const std::string text   = CJKRun(0x4E00, 20);
    const float       five   = f.text.Measure(CJKRun(0x4E00, 5), f.params).x;
    const Vec2        size   = f.text.Measure(text, f.params, five + 1.0f);
    const float       lh     = f.text.GetLineHeight(14.0f);

    CHECK_NEAR(size.y, lh * 4);         // 每行 5 个字，共 4 行
    CHECK(size.x <= five + 1.0f);
}

TEST(Text_WrapEnglishAtSpaces)
{
    REQUIRE_FONT();
    TextFixture f;
    f.params.wrap = true;

    const float first = f.text.Measure("hello world", f.params).x;
    const Vec2  size  = f.text.Measure("hello world wraps", f.params, first + 2.0f);

    CHECK_NEAR(size.y, f.text.GetLineHeight(14.0f) * 2);
    CHECK_NEAR(size.x, first);          // 第一行正好是 "hello world"，行尾空格不计宽度
}

TEST(Text_KinsokuPunctuationNeverStartsLine)
{
    REQUIRE_FONT();
    TextFixture f;
    f.params.wrap = true;

    // 宽度正好放下前 4 个字：句号不能单独换到行首，要带着前一个字一起换行
    const float four = f.text.Measure("一二三四", f.params).x;
    TextLayout layout;
    f.text.BuildLayout("一二三四。五", f.params, four + 0.5f, layout);
    CHECK(layout.lines.size() == 2);
    CHECK(layout.lines[1].byteBegin == 9);         // 第二行从"四"开始（每个汉字 3 字节）

    // 左括号不留在行尾
    f.text.BuildLayout("一二三（四）", f.params, four + 0.5f, layout);
    CHECK(layout.lines.size() == 2);
    CHECK(layout.lines[1].byteBegin == 9);         // "（" 换到下一行
}

TEST(Text_LongWordBreaksWhenNoOpportunity)
{
    REQUIRE_FONT();
    TextFixture f;
    f.params.wrap = true;

    // 没有空格的长单词：只能在字母之间强制断开，每行宽度仍不超过限制
    const Vec2 size = f.text.Measure("abcdefghijklmnopqrstuvwxyz", f.params, 40.0f);
    CHECK(size.y > f.text.GetLineHeight(14.0f) * 2);
    CHECK(size.x <= 40.0f);
}

TEST(Text_EllipsisLimitsWidth)
{
    REQUIRE_FONT();
    TextFixture f;
    f.params.ellipsis = true;

    const Vec2 full = f.text.Measure("一段很长很长的文字 with English", f.params);
    const Vec2 cut  = f.text.Measure("一段很长很长的文字 with English", f.params, 60.0f);
    CHECK(full.x > 60.0f);
    CHECK(cut.x <= 60.0f);
    CHECK_NEAR(cut.y, full.y);
}

TEST(Text_MeasureStableAcrossScale)
{
    REQUIRE_FONT();
    TextFixture f;
    const Vec2 at100 = f.text.Measure("Retained UI 保留模式", f.params);
    f.text.SetPixelScale(1.5f);
    const Vec2 at150 = f.text.Measure("Retained UI 保留模式", f.params);

    // 物理像素下取整会有细微差别，逻辑尺寸应基本一致
    CHECK(std::abs(at100.x - at150.x) < 2.0f);
    CHECK(std::abs(at100.y - at150.y) < 2.0f);
}

// ── Label 与布局 ─────────────────────────────────────────────────

TEST(Label_SizeFollowsText)
{
    REQUIRE_FONT();
    TestUI t({ 400, 200 });
    t.ui.GetTextSystem().AddFont(TestFont());

    Node* c = t.Add();
    c->SetLayoutStyle([] { LayoutStyle s; s.direction = FlexDirection::Row; s.alignItems = Align::Start; return s; }());
    Label* a = c->AddChild<Label>("短");
    Label* b = c->AddChild<Label>("右边的标签");
    t.Layout();

    const Vec2 expected = t.ui.GetTextSystem().Measure("短", a->GetParams());
    CHECK_NEAR(a->GetBounds().Width(), expected.x);
    CHECK_NEAR(a->GetBounds().Height(), expected.y);
    CHECK_NEAR(b->GetBounds().min.x, a->GetBounds().max.x);

    // 修改文字 → 重新布局，右边的标签跟着移动
    a->SetText("长一些的文字");
    t.Layout();
    CHECK(b->GetBounds().min.x > expected.x + 10.0f);
}

TEST(Label_WrapHeightInColumn)
{
    REQUIRE_FONT();
    TestUI t({ 120, 400 });
    t.ui.GetTextSystem().AddFont(TestFont());

    Node* c = t.Add();                            // Column，子项拉伸到 120 宽
    Label* p = c->AddChild<Label>(CJKRun(0x4E00, 30));
    p->SetWrap(true);
    Node* below = c->AddChild<Node>();
    below->SetLayoutStyle(Fixed(10, 10));
    t.Layout();

    const float lh = t.ui.GetTextSystem().GetLineHeight(14.0f);
    CHECK(p->GetBounds().Height() > lh * 2);      // 换成多行
    CHECK_NEAR(below->GetBounds().min.y, p->GetBounds().max.y);
}

// ── 字形图集 ─────────────────────────────────────────────────────

TEST(Atlas_GrowsOnDemandAndKeepsWhitePixel)
{
    REQUIRE_FONT();
    TextFixture f;
    GlyphAtlas& atlas = f.text.GetAtlas();
    CHECK(atlas.GetWidth() == 512);

    DrawListSharedData shared;
    DrawList dl(&shared);
    dl.Reset(Rect{ 0, 0, 2000, 2000 });

    atlas.BeginFrame();
    f.params.size = 20.0f;
    f.params.wrap = true;
    f.text.Draw(dl, Rect{ 0, 0, 2000, 2000 }, CJKRun(0x4E00, 1200), f.params);

    CHECK(atlas.Changed());                        // 本帧发生了扩容，调用方需要重绘
    CHECK(atlas.GetWidth() == 1024);
    CHECK(atlas.GetGlyphCount() == 1200);

    // 白色像素仍在左上角，UV 随尺寸变化
    const Vec2 uv = atlas.GetWhitePixelUV();
    CHECK_NEAR(uv.x * atlas.GetWidth(), 2.0);
    CHECK_NEAR(uv.y * atlas.GetHeight(), 2.0);

    // 再画一遍：字形都已缓存，图集不再变化
    atlas.BeginFrame();
    f.text.Draw(dl, Rect{ 0, 0, 2000, 2000 }, CJKRun(0x4E00, 1200), f.params);
    CHECK(!atlas.Changed());
}

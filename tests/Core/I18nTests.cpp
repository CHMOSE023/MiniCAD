// 界面文字的翻译（MiniI18n，见 docs/I18n/PLAN.md）：查表、上下文、占位符、翻译文件的解析、伪语言。
// 结束时卸下翻译表：其他测试按中文原文断言
#include "MiniI18n/I18n.h"
#include <cstdio>
#include <memory>
#include <string>

namespace
{
    int g_failures = 0;

    void Check(bool ok, const char* what)
    {
        if (!ok)
            ++g_failures;
        std::printf("  [%s] %s\n", ok ? "通过" : "失败", what);
    }

    std::unique_ptr<MiniI18n::Catalog> ParseCatalog(const char* json, std::string* error = nullptr)
    {
        auto c = std::make_unique<MiniI18n::Catalog>();
        return c->Parse(json, error) ? std::move(c) : nullptr;
    }
}

int RunI18nTests()
{
    g_failures = 0;
    std::printf("── 界面文字的翻译 ──\n");

    // ── 没有翻译表：原样返回 ────────────────────────────────────
    MiniI18n::SetCatalog(nullptr);
    Check(Tr("保存(&S)") == "保存(&S)", "没有翻译表时 Tr 返回原文");
    Check(TrF("{0} 个外部参照没有找到", 3) == "3 个外部参照没有找到", "没有翻译表时 TrF 照样替换占位符");
    Check(std::string(N_("新建(&N)")) == "新建(&N)", "N_ 原样返回");

    // ── 占位符 ─────────────────────────────────────────────────
    Check(MiniI18n::Format("{1} / {0}", { "a", "b" }) == "b / a", "占位符按位置，可以换顺序");
    Check(MiniI18n::Format("{0}{0}", { "x" }) == "xx", "同一个占位符出现两次");
    Check(MiniI18n::Format("{2} {x} {", { "a" }) == "{2} {x} {", "下标越界、不是数字、没闭合的花括号原样保留");
    Check(MiniI18n::Format("{{0}} {0}", { "a" }) == "{0} a", "{{ 与 }} 是字面的花括号");
    Check(TrF("{0}|{1}|{2}|{3}", 42, 2.5, std::string("s"), "c") == "42|2.5|s|c", "TrF 的参数：整数、浮点、std::string、字符串字面量");
    Check(TrF("{0}", 0.1 + 0.2) == "0.3", "浮点按最多 12 位有效数字");

    // ── 翻译文件的解析 ──────────────────────────────────────────
    const char* kJson = "\xEF\xBB\xBF{\n"
                        "  // 注释\n"
                        "  \"language\": \"en\", \"name\": \"English\",\n"
                        "  \"version\": 1, \"flags\": [ true, null, { \"a\": 1 } ],   /* 其他字段跳过 */\n"
                        "  \"messages\": {\n"
                        "    \"保存(&S)\": \"&Save\",\n"
                        "    \"{0} 个外部参照没有找到\": \"References not found: {0}\",\n"
                        "    \"关闭(&C)\": \"&Close\",\n"
                        "    \"还没翻译\": \"\",\n"
                        "    \"转义\": \"a\\\"b\\\\c\\n\\u00e9\\ud83d\\ude00\",\n"
                        "  },\n"
                        "  \"contexts\": { \"文档\": { \"关闭(&C)\": \"&Close Drawing\" } },\n"
                        "  \"obsolete\": { \"旧的\": \"old\" }\n"
                        "}\n";
    std::string error;
    auto en = ParseCatalog(kJson, &error);
    Check(en != nullptr, ("翻译文件解析成功（BOM、注释、结尾逗号、其他类型的字段）" + error).c_str());
    if (en)
    {
        Check(en->GetLanguage() == "en" && en->GetName() == "English", "language / name");
        Check(en->Find("还没翻译") == nullptr, "译文为空的条目当作没有翻译");
        Check(en->Find("旧的") == nullptr, "obsolete 不读");
        const std::string* esc = en->Find("转义");
        Check(esc && *esc == "a\"b\\c\n\xC3\xA9\xF0\x9F\x98\x80", "字符串转义（含 \\u 与代理对）");
    }

    MiniI18n::SetCatalog(std::move(en));
    Check(Tr("保存(&S)") == "&Save", "装上翻译表后 Tr 返回译文");
    Check(Tr("还没翻译") == "还没翻译", "没有翻译的条目返回原文");
    Check(Tr("表里没有") == "表里没有", "表里没有的条目返回原文");
    Check(Tr("") == "", "空串");
    Check(TrF("{0} 个外部参照没有找到", 2) == "References not found: 2", "TrF 先翻译再替换");
    Check(TrC("文档", "关闭(&C)") == "&Close Drawing", "带上下文的译文");
    Check(TrC("窗口", "关闭(&C)") == "&Close", "上下文里没有时用不带上下文的译文");
    Check(MiniI18n::GetCatalog() && MiniI18n::GetCatalog()->Size() == 5, "条目数（带上下文的单独算一条，没翻译的不算）");

    // ── 格式错误 ───────────────────────────────────────────────
    error.clear();
    Check(ParseCatalog("{\n  \"messages\": {\n    \"a\": \"b\"\n    \"c\": \"d\"\n  }\n}", &error) == nullptr, "缺逗号时解析失败");
    Check(error.find("第 4 行") == 0, ("出错信息带行号：" + error).c_str());
    error.clear();
    Check(ParseCatalog("{ \"messages\": { \"a\": \"没结束 } }", &error) == nullptr && !error.empty(), "字符串没有结束");
    Check(ParseCatalog("{ } x") == nullptr, "对象结束后还有多余的内容");
    MiniI18n::Catalog partial;
    partial.Add("x", "y");
    Check(!partial.Parse("{ \"messages\": { \"a\": 1 ") && partial.Size() == 0, "失败时清空已读入的内容");

    // ── 伪语言 ─────────────────────────────────────────────────
    auto pseudo = std::make_unique<MiniI18n::Catalog>();
    pseudo->SetPseudo(true);
    pseudo->Add("保存(&S)", "&Save");
    MiniI18n::SetCatalog(std::move(pseudo));
    Check(Tr("指定第一点") == "[指定第一点~~]", "伪语言：没有译文的也包上方括号并拉长约 40%");
    Check(Tr("保存(&S)") == "[&Save~~]", "伪语言：有译文的包译文");
    Check(TrF("共 {0} 个", 5) == "[共 5 个~~~]", "伪语言下占位符照样替换");

    MiniI18n::SetCatalog(nullptr);
    Check(Tr("保存(&S)") == "保存(&S)", "卸下翻译表后回到原文");
    return g_failures;
}

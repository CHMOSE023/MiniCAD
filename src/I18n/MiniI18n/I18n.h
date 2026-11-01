#pragma once
// MiniI18n：界面文字的翻译（见 docs/I18n/PLAN.md）。只用标准库，MiniCAD、MiniGUI、MiniDWG 都链接它。
//
// 源语言是简体中文，翻译表的键就是代码里的中文原文（gettext 式）：
//   Tr("指定第一个点:")                        → 当前语言的译文；没有翻译表、没有这一条、译文为空时返回原文
//   TrC("窗口", "关闭(&C)")                    → 同字不同义时加上下文
//   TrF("{0} 个外部参照没有找到", n)           → 先翻译再替换 {0}、{1}（译文里可以换顺序）
//   N_("保存(&S)")                             → 原样返回，只给提取工具（tools/i18n_extract.py）做标记：
//                                                存进数据、显示时才翻译的字符串用它（命令名称、节点名称……）
// 翻译表由 SetCatalog 装上，运行中可以再换（切换界面语言）；表本身只读，各线程都可以直接调用 Tr。
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace MiniI18n
{
    // 一种语言的翻译表
    class Catalog
    {
    public:
        // 读翻译文件（UTF-8 JSON，允许 // 注释）：
        //   { "language": "en", "name": "English", "messages": { 原文: 译文 }, "contexts": { 上下文: { 原文: 译文 } }, "obsolete": { … } }
        // 译文为空的条目当作没有翻译；"obsolete" 不读。失败时返回 false，error 为"第 n 行：原因"，已读入的内容清空
        bool Parse(std::string_view json, std::string* error = nullptr);

        void Add(std::string_view msgid, std::string translation);
        void Add(std::string_view context, std::string_view msgid, std::string translation);

        // 没有这一条时返回 nullptr
        const std::string* Find(std::string_view msgid) const;
        const std::string* Find(std::string_view context, std::string_view msgid) const;

        const std::string& GetLanguage() const { return m_language; }    // "en"
        const std::string& GetName()     const { return m_name; }        // "English"
        void SetLanguage(std::string code, std::string name) { m_language = std::move(code); m_name = std::move(name); }
        size_t Size() const { return m_messages.size(); }

        // 伪语言（开发用）：查不到的条目也"翻译"成 [原文~~~]，拉长约 40%，用来找出没走 Tr 的文字和放不下的控件
        void SetPseudo(bool pseudo) { m_pseudo = pseudo; }
        bool IsPseudo() const { return m_pseudo; }

    private:
        struct Hash
        {
            using is_transparent = void;
            size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
        };
        std::unordered_map<std::string, std::string, Hash, std::equal_to<>> m_messages;   // 有上下文时键为 上下文 + '\x04' + 原文
        std::string m_language;
        std::string m_name;
        bool        m_pseudo = false;
    };

    // 装上当前语言的翻译表（nullptr = 源语言，不翻译）。在界面线程调用；换下来的表不释放（后台线程可能正在查它），
    // 到程序退出才释放——切换语言很少，每张表几十 KB
    void           SetCatalog(std::unique_ptr<Catalog> catalog);
    const Catalog* GetCatalog();

    std::string Tr(std::string_view msgid);
    std::string TrC(std::string_view context, std::string_view msgid);

    // 伪语言的变换：[原文~~~]（Catalog::SetPseudo）
    std::string Pseudo(std::string_view text);

    // 把 pattern 里的 {0}、{1}… 换成 args[n]；{{ 与 }} 表示字面的花括号；下标越界的占位符原样保留
    std::string Format(std::string_view pattern, const std::vector<std::string>& args);

    namespace Detail
    {
        inline std::string ToArg(std::string_view s)        { return std::string(s); }
        inline std::string ToArg(const std::string& s)      { return s; }
        inline std::string ToArg(const char* s)             { return s ? std::string(s) : std::string(); }
        inline std::string ToArg(char c)                    { return std::string(1, c); }
        std::string        ToArg(double v);                 // 最短的写法（%g，最多 12 位有效数字）
        inline std::string ToArg(float v)                   { return ToArg(static_cast<double>(v)); }
        template <class T>
            requires std::is_integral_v<T>
        std::string ToArg(T v)                              { return std::to_string(v); }
    }

    template <class... Args>
    std::string TrF(std::string_view msgid, const Args&... args)
    {
        return Format(Tr(msgid), { Detail::ToArg(args)... });
    }

    template <class... Args>
    std::string TrCF(std::string_view context, std::string_view msgid, const Args&... args)
    {
        return Format(TrC(context, msgid), { Detail::ToArg(args)... });
    }

    constexpr const char* N_(const char* msgid) { return msgid; }
}

// 各库的代码直接写 Tr("…")，不必带命名空间
using MiniI18n::Tr;
using MiniI18n::TrC;
using MiniI18n::TrF;
using MiniI18n::TrCF;
using MiniI18n::N_;

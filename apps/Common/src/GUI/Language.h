#pragma once
// 界面语言：选定语言、读翻译文件（资源目录下 i18n/<语言>.json）、装进 MiniI18n（见 docs/I18n/PLAN.md 3.6）。
// 源语言是简体中文（没有翻译文件）。切换语言重启后生效
#include <string>
#include <vector>

namespace MiniCAD
{
    class AppPlatform;

    inline constexpr const char* kSourceLanguage = "zh-CN";     // 源语言：代码里的原文
    inline constexpr const char* kPseudoLanguage = "qps";       // 伪语言（开发用，不在菜单里）：[原文~~~]
    inline constexpr const char* kLanguageCommandPrefix = "ui.language.";   // 每种语言一条命令："ui.language.en"

    struct LanguageInfo
    {
        std::string code;       // "en"：翻译文件名（不含 .json）
        std::string name;       // "English"：该语言自己的写法，"设置 > 语言"里显示
    };

    // 可选的语言：简体中文排第一，其余是 i18n/ 下的翻译文件（按文件名）
    std::vector<LanguageInfo> ListLanguages(const std::string& resourceDir);

    // 启动时调用，宿主在创建图形设备之前（图形设备的出错信息也要翻译）：选定语言并装好翻译表，返回实际使用的语言代码。
    // 依次看 requested（命令行 --lang、环境变量 MINICAD_LANG、网址的 ?lang=，由宿主读取）→ 用户设置 → 系统语言 → 简体中文；
    // 系统语言没有对应的翻译文件时用 English（"zh*" 用简体中文）。
    // useUserSetting = false（自测、基准）：只认 requested——自测按中文文字检查，不受本机设置与系统语言影响
    std::string InitLanguage(AppPlatform& platform, const std::string& requested, bool useUserSetting = true);

    const std::string& CurrentLanguage();       // 正在使用的语言
    const std::string& PendingLanguage();       // "设置 > 语言"选中的（重启后生效；没改过时同 CurrentLanguage）

    // "设置 > 语言"：写进用户设置，重启后生效
    bool SaveLanguageSetting(AppPlatform& platform, const std::string& code);
}

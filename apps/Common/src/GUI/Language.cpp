#include "GUI/Language.h"
#include "GUI/AppPlatform.h"
#include "Core/Log.h"
#include "Data/Json.h"
#include "MiniI18n/I18n.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>

namespace MiniCAD
{
    namespace
    {
        std::string g_current = kSourceLanguage;

        constexpr const char* kSettingKey = "language";

        std::filesystem::path ToPath(const std::string& utf8)
        {
            return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        }

        std::string ToUtf8(const std::filesystem::path& p)
        {
            const std::u8string u8 = p.u8string();
            return std::string(u8.begin(), u8.end());
        }

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        // "en_US.UTF-8" / "zh_CN@xx" → "en-US" / "zh-CN"；"C"、"POSIX" → 空串
        std::string NormalizeTag(std::string tag)
        {
            if (const size_t cut = tag.find_first_of(".@"); cut != std::string::npos)
                tag.resize(cut);
            std::replace(tag.begin(), tag.end(), '_', '-');
            if (tag == "C" || tag == "POSIX")
                return {};
            return tag;
        }

        std::string TranslationFile(const std::string& resourceDir, const std::string& code)
        {
            return ToUtf8(ToPath(resourceDir) / "i18n" / ToPath(code + ".json"));
        }

        // 语言标签对应的语言代码：先找完全相同的翻译文件，再找主标签（"en-US" → "en"）；
        // "zh*" 没有专门的翻译文件时是源语言；都没有返回空串
        std::string Match(const std::vector<LanguageInfo>& available, const std::string& rawTag)
        {
            const std::string tag = Lower(NormalizeTag(rawTag));
            if (tag.empty())
                return {};
            if (tag == kPseudoLanguage)
                return kPseudoLanguage;
            const std::string primary = tag.substr(0, tag.find('-'));
            for (const std::string& want : { tag, primary })
                for (const LanguageInfo& l : available)
                    if (Lower(l.code) == want)
                        return l.code;
            return primary == "zh" ? std::string(kSourceLanguage) : std::string();
        }

        // 装上翻译表；读不了或格式不对时退回源语言（界面不会出现空白）
        std::string Apply(const std::string& resourceDir, const std::string& code)
        {
            if (code == kSourceLanguage || code.empty())
            {
                MiniI18n::SetCatalog(nullptr);
                return kSourceLanguage;
            }
            auto catalog = std::make_unique<MiniI18n::Catalog>();
            if (code == kPseudoLanguage)
            {
                catalog->SetLanguage(kPseudoLanguage, "Pseudo");
                catalog->SetPseudo(true);
            }
            else
            {
                const std::string path = TranslationFile(resourceDir, code);
                std::string text, error;
                if (!MiniGUI::ReadTextFile(path, text))
                {
                    LOG_WARN_TR("无法读取翻译文件 {0}，使用简体中文", path);
                    MiniI18n::SetCatalog(nullptr);
                    return kSourceLanguage;
                }
                if (!catalog->Parse(text, &error))
                {
                    LOG_WARN_TR("翻译文件 {0} 格式错误，{1}；使用简体中文", path, error);
                    MiniI18n::SetCatalog(nullptr);
                    return kSourceLanguage;
                }
                if (catalog->GetLanguage().empty())
                    catalog->SetLanguage(code, code);
            }
            MiniI18n::SetCatalog(std::move(catalog));
            return code;
        }
    }

    std::vector<LanguageInfo> ListLanguages(const std::string& resourceDir)
    {
        std::vector<LanguageInfo> list{ { kSourceLanguage, "简体中文" } };     // i18n: 数据（语言名称用它自己的写法）
        std::error_code ec;
        const std::filesystem::path dir = ToPath(resourceDir) / "i18n";
        std::vector<LanguageInfo> found;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || it->path().extension() != ".json")
                continue;
            const std::string code = ToUtf8(it->path().stem());
            std::string text, name = code;
            MiniI18n::Catalog c;
            if (MiniGUI::ReadTextFile(ToUtf8(it->path()), text) && c.Parse(text) && !c.GetName().empty())
                name = c.GetName();
            found.push_back({ code, name });
        }
        std::sort(found.begin(), found.end(), [](const LanguageInfo& a, const LanguageInfo& b) { return a.code < b.code; });
        list.insert(list.end(), found.begin(), found.end());
        return list;
    }

    std::string InitLanguage(AppPlatform& platform, const std::string& requested, bool useUserSetting)
    {
        const std::string resourceDir = platform.GetResourceDir();
        const std::vector<LanguageInfo> available = ListLanguages(resourceDir);

        std::string code;
        if (!requested.empty())
        {
            code = Match(available, requested);
            if (code.empty())
            {
                LOG_WARN_TR("没有语言 {0} 的翻译文件，使用简体中文", requested);
                code = kSourceLanguage;
            }
        }
        if (code.empty() && useUserSetting)
            code = Match(available, platform.ReadUserSetting(kSettingKey));
        if (code.empty() && useUserSetting)
        {
            const std::string system = platform.GetSystemLanguage();
            code = Match(available, system);
            if (code.empty() && !system.empty())
                code = Match(available, "en");      // 没有这种语言的翻译时用英文（中文系统已在 Match 里归到简体中文）
        }
        g_current = Apply(resourceDir, code);
        if (g_current != kSourceLanguage)
            LOG_INFO_TR("界面语言：{0}", g_current);
        return g_current;
    }

    const std::string& CurrentLanguage() { return g_current; }

    std::string SwitchLanguage(AppPlatform& platform, const std::string& code, bool save)
    {
        g_current = Apply(platform.GetResourceDir(), code);
        LOG_INFO_TR("界面语言：{0}", g_current);
        // 保存不了（没有用户数据目录）时照样切换，只是下次启动不记得
        if (save && !platform.WriteUserSetting(kSettingKey, code))
            LOG_WARN_TR("无法保存界面语言的设置");
        return g_current;
    }
}

// 用户设置的默认存储：用户数据目录（AppPlatform::GetUserDataDir）下的 settings.json，{ "language": "en", … }。
// 网页版没有用户数据目录，WebApp 改存浏览器的 localStorage
#include "GUI/AppPlatform.h"
#include "Data/Json.h"
#include <filesystem>
#include <fstream>

namespace MiniCAD
{
    namespace
    {
        std::filesystem::path ToPath(const std::string& utf8)
        {
            return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        }

        std::filesystem::path SettingsPath(const AppPlatform& platform)
        {
            const std::string dir = platform.GetUserDataDir();
            return dir.empty() ? std::filesystem::path() : ToPath(dir) / "settings.json";
        }

        // 读不到或格式不对时返回空对象（写入时整个文件重写）
        MiniGUI::JsonValue LoadSettings(const std::filesystem::path& path)
        {
            std::string text;
            MiniGUI::JsonValue doc;
            MiniGUI::JsonError error;
            const std::u8string u8 = path.u8string();
            if (path.empty() || !MiniGUI::ReadTextFile(std::string(u8.begin(), u8.end()), text) || !MiniGUI::ParseJson(text, doc, error) || !doc.IsObject())
                return MiniGUI::JsonValue::MakeObject();
            return doc;
        }
    }

    std::string AppPlatform::ReadUserSetting(const std::string& key) const
    {
        return LoadSettings(SettingsPath(*this))[key].AsString();
    }

    bool AppPlatform::WriteUserSetting(const std::string& key, const std::string& value)
    {
        const std::filesystem::path path = SettingsPath(*this);
        if (path.empty())
            return false;
        MiniGUI::JsonValue doc = LoadSettings(path);
        doc.Set(key, value);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << doc.Dump(2) << '\n';
        return static_cast<bool>(out);
    }
}

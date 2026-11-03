#include "Platform/Win32/Win32Fonts.h"
#include "Text/Font.h"
#include "Text/TextSystem.h"
#include <windows.h>

namespace MiniGUI
{
    std::string GetSystemFontPath(const char* fileName)
    {
        wchar_t dir[MAX_PATH] = {};
        const UINT len = GetWindowsDirectoryW(dir, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
            return {};

        const int size = WideCharToMultiByte(CP_UTF8, 0, dir, -1, nullptr, 0, nullptr, nullptr);
        std::string path(static_cast<size_t>(size > 0 ? size - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, dir, -1, path.data(), size, nullptr, nullptr);
        return path + "\\Fonts\\" + fileName;
    }

    std::shared_ptr<Font> LoadSystemUIFont()
    {
        // 微软雅黑 UI 行距比微软雅黑紧凑，更适合界面；缺失时依次退回
        if (auto f = Font::LoadShared(GetSystemFontPath("msyh.ttc"), 1))   return f;
        if (auto f = Font::LoadShared(GetSystemFontPath("msyh.ttf"), 0))   return f;
        if (auto f = Font::LoadShared(GetSystemFontPath("simsun.ttc"), 0)) return f;
        return Font::LoadShared(GetSystemFontPath("segoeui.ttf"), 0);
    }

    std::shared_ptr<Font> LoadSystemSymbolFont()
    {
        return Font::LoadShared(GetSystemFontPath("seguisym.ttf"), 0);
    }

    std::vector<std::shared_ptr<Font>> LoadSystemLanguageFonts(const std::string& language)
    {
        struct Candidate { const char* file; int face; };
        std::vector<Candidate> candidates;
        switch (NotoCjkFaceForLanguage(language))
        {
        case 0: candidates = { { "YuGothM.ttc", 1 }, { "meiryo.ttc", 2 }, { "msgothic.ttc", 1 } }; break;
        case 1: candidates = { { "malgun.ttf", 0 }, { "gulim.ttc", 0 } }; break;
        case 3: candidates = { { "msjh.ttc", 1 }, { "mingliu.ttc", 1 } }; break;
        default: break;
        }
        for (const Candidate& c : candidates)
            if (auto f = Font::LoadShared(GetSystemFontPath(c.file), c.face))
                return { f };
        return {};
    }

    bool LoadSystemUIFonts(TextSystem& text)
    {
        auto main = LoadSystemUIFont();
        if (!main)
            return false;
        const bool cjk = main->FindGlyph(0x4E00) != 0;
        text.AddFont(std::move(main));
        if (auto symbol = LoadSystemSymbolFont())
            text.AddFallbackFont(std::move(symbol));
        // 主字体退到 Segoe UI 时没有汉字
        if (!cjk)
            for (const char* file : { "simsun.ttc", "msjh.ttc" })
                if (auto f = Font::LoadShared(GetSystemFontPath(file), 0))
                {
                    text.AddFallbackFont(std::move(f));
                    break;
                }
        // 微软雅黑没有韩文
        if (auto hangul = Font::LoadShared(GetSystemFontPath("malgun.ttf"), 0))
            text.AddFallbackFont(std::move(hangul));
        return true;
    }
}

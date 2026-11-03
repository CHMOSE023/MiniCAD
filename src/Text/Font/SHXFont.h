#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

#include "IFont.h"
#include "../Parser/SHXParser.h"

namespace MiniCAD
{
    class SHXFont : public IFont
    {
    public:
        SHXFont(const std::string& name, const std::string& filePath, uint64_t fontId);

        const Glyph& GetGlyph(uint32_t codepoint) override;

        double GetAdvance(uint32_t codepoint) override;

        double GetHeight() const override;

        uint64_t GetFontId() const override;

        const char* GetName() const override;

        bool HasGlyph(uint32_t codepoint) override;

        // 形文件是否真的读进来了。加载失败时 SHXFont 仍然是一份合法的空字体
        // （不抛异常，只是所有字形都 MISS），所以 FontEngine 的缓存必须能分辨
        // 「读成功但字少」和「压根没读到」——后者要允许后续重试，
        // 否则字体目录晚一步就绪时这个失败结果会被永久缓存。
        bool IsLoaded() const { return m_loaded; }
    private:
        void LoadFont(const std::string& filePath);

        uint32_t ResolveShxKey(uint32_t codepoint) const;
    public:
        // 大字体按哪种代码页排字（936 / 950 / 932 / 949）：先看文件名，再看形编码落在哪些区
        static uint16_t DetectBigFontCodePage(const std::string& fileName, const std::vector<uint32_t>& codes);
        uint16_t GetCodePage() const { return m_codePage; }
    private:
        std::string m_name;
        std::string m_filePath;
        uint64_t    m_fontId;

        double      m_height = 1.0;
        double      m_defaultAdvance = 0.6;
        bool        m_loaded = false;
        uint16_t    m_codePage = 936;

        std::unique_ptr<SHXParser> m_parser;

        std::unordered_map<uint32_t, Glyph>  m_glyphCache;
        std::unordered_map<uint32_t, double> m_advanceCache;

        mutable std::unordered_map<uint32_t, uint32_t> m_codeMap;
    };
}
#pragma once
#include <string>
#include <unordered_map>
#include <memory>

#include "IFont.h"
#include "../Parser/SHXParser.h"

namespace MiniCAD
{
    class SHXFont : public IFont
    {
    public:
        SHXFont(const std::string& name, const std::string& filePath, uint64_t fontId);

        Glyph GetGlyph(uint32_t codepoint) override;

        double GetAdvance(uint32_t codepoint) override;

        double GetHeight() const override;

        uint64_t GetFontId() const override;

        const char* GetName() const override;

    private:
        void LoadFont(const std::string& filePath);

    private:
        std::string m_name;
        std::string m_filePath;
        uint64_t    m_fontId;

        double      m_height = 1.0;
        double      m_defaultAdvance = 0.6;

        std::unique_ptr<SHXParser> m_parser;

        std::unordered_map<uint32_t, Glyph> m_glyphCache;
        std::unordered_map<uint32_t, double> m_advanceCache;
    };
}
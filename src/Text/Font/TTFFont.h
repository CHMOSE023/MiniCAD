#pragma once
#include <string>
#include <unordered_map>
#include <memory>

#include "../Parser/TTFParser.h"
#include "IFont.h"

namespace MiniCAD
{
    class TTFFont : public IFont
    {
    public:
        TTFFont(const std::string& name, const std::string& filePath, uint64_t fontId);

        Glyph GetGlyph(uint32_t codepoint) override;

        double GetAdvance(uint32_t codepoint) override;

        double GetHeight() const override;

        uint64_t GetFontId() const override;

        const char* GetName() const override;

    private:
        void LoadFont(const std::string& filePath);

        Glyph BuildGlyph(uint32_t codepoint);

        void InitFreeType();

        void LoadFace(const std::string& filePath);

        void OutlineToLines(void* ftOutline, Glyph& glyph);

        void FlattenBezier(const std::vector<Line>& input, std::vector<Line>& output, double tolerance);

    private:
        std::string m_name;
        std::string m_filePath;
        uint64_t    m_fontId;

        double m_height = 1.0;
        double m_defaultAdvance = 0.6;

        void* m_ftLibrary = nullptr;
        void* m_ftFace = nullptr;

        std::unordered_map<uint32_t, Glyph>  m_glyphCache;
        std::unordered_map<uint32_t, double> m_advanceCache;

        std::unique_ptr<TTFParser>           m_parser;
    };
}
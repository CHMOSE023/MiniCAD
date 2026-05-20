#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "IFont.h"

namespace MiniCAD
{
   
    // TrueType 矢量字体，使用 stb_truetype 解析 outline → 折线段。
    // stbtt_fontinfo 通过 void* 隐藏，避免将大型头文件暴露给使用方。
    // Web 端（MINICAD_WEB）不使用本类，全部走 WebFontAtlas 纹理路径。
    class TTFFont : public IFont
    {
    public:
        TTFFont(const std::string& name, const std::string& filePath, uint64_t fontId);
        ~TTFFont();

        Glyph       GetGlyph  (uint32_t codepoint) override;
        double      GetAdvance(uint32_t codepoint) override;
        double      GetHeight ()   const override { return m_height; }
        uint64_t    GetFontId ()   const override { return m_fontId; }
        const char* GetName   ()   const override { return m_name.c_str(); }
        bool        IsLoaded  ()   const          { return m_stbFont != nullptr; }

        bool        HasGlyph(uint32_t codepoint) override { return  m_glyphCache[codepoint].empty() ? false : true; }
    private:
        void  Load(const std::string& filePath);
        Glyph BuildGlyph(uint32_t codepoint);

        void  BuildFill(Glyph& g);
        void  ScanlineFill(const std::vector<Line>& lines, std::vector<Triangle>& out);

        // 递归自适应细分二次贝塞尔曲线 → 折线段
        void FlattenQuad(double x0, double y0,
                         double cx, double cy,
                         double x1, double y1,
                         double tolerance,
                         std::vector<Line>& out) const;

        // 递归自适应细分三次贝塞尔曲线 → 折线段
        void FlattenCubic(double x0, double y0,
                          double c1x, double c1y,
                          double c2x, double c2y,
                          double x1,  double y1,
                          double tolerance,
                          std::vector<Line>& out) const;

    private:
        std::string  m_name;
        uint64_t     m_fontId;
        double       m_height  = 1.0;
        double       m_scale   = 1.0; // stbtt 坐标 → 归一化

        std::vector<uint8_t> m_fileData; // TTF 文件字节，必须保持生命周期
        void*                m_stbFont = nullptr; // stbtt_fontinfo*，heap 分配

        std::unordered_map<uint32_t, Glyph>  m_glyphCache;
        std::unordered_map<uint32_t, double> m_advanceCache;
    };
}

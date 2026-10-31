#pragma once
#include <memory>
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

        const Glyph& GetGlyph (uint32_t codepoint) override;
        double      GetAdvance(uint32_t codepoint) override;
        double      GetHeight ()   const override { return m_height; }
        uint64_t    GetFontId ()   const override { return m_fontId; }
        const char* GetName   ()   const override { return m_name.c_str(); }
        bool        IsLoaded  ()   const          { return m_stbFont != nullptr; }

        bool        HasGlyph(uint32_t codepoint) override { return Covers(codepoint) || (m_fallback && m_fallback->Covers(codepoint)); }

        // 字体文件里有这个字符（缺字时 stb_truetype 给的是 .notdef 方块，不算）
        bool        Covers(uint32_t codepoint) const;
        // 本字体缺的字改用这个字体的字形，与 Windows 的字体链接一样（AutoCAD 里 Arial 等西文字体的汉字照样显示）。
        // 两者都按大写字母高归一化，字形可以直接拿来用。FontEngine 设为自带的 GB2312.ttf
        void        SetFallback(std::shared_ptr<TTFFont> fallback) { m_fallback = std::move(fallback); }
    private:
        void  Load(const std::string& filePath);
        Glyph BuildGlyph(uint32_t codepoint);

        void  BuildFill(Glyph& g);

        // 梯形 Y-扫描三角化：在每个顶点 Y 处切片，slab 内各边均为直线，
        // 梯形斜边贴合真实轮廓（无台阶），偶奇配对天然处理内孔。
        void  TrapezoidFill(const std::vector<Line>& lines, std::vector<Triangle>& out);

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
        std::shared_ptr<TTFFont>             m_fallback;
    };
}

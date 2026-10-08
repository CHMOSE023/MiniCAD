#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace MiniGUI
{
    // 单个字形的光栅化结果：8 位覆盖率，偏移相对笔位置（x）和基线（y，向下为正）
    struct GlyphBitmap
    {
        int                  width   = 0;
        int                  height  = 0;
        int                  offsetX = 0;
        int                  offsetY = 0;
        std::vector<uint8_t> coverage;
    };

    // TrueType / OpenType(TrueType 轮廓) 字体，基于 stb_truetype。
    // 所有度量值都是未缩放的字体单位，乘以 ScaleForSize(像素字号) 得到像素值
    class Font
    {
    public:
        // faceIndex 用于 .ttc 字体集合（例如 msyh.ttc：0 = 微软雅黑，1 = 微软雅黑 UI）
        static std::shared_ptr<Font> LoadFromFile  (const std::string& utf8Path, int faceIndex = 0);
        static std::shared_ptr<Font> LoadFromMemory(std::vector<uint8_t> data, int faceIndex = 0);

        ~Font();

        Font(const Font&) = delete;
        Font& operator=(const Font&) = delete;

        int   FindGlyph(uint32_t codepoint) const;              // 0 表示字体里没有这个字
        float ScaleForSize(float pixelSize) const;              // 字号按 em 计算（与 CSS font-size 一致）

        int   GetAscent()  const { return m_ascent; }
        int   GetDescent() const { return m_descent; }          // 负数
        int   GetLineGap() const { return m_lineGap; }
        int   GetAdvance(int glyph) const;
        int   GetKerning(int glyph1, int glyph2) const;

        bool  RasterizeGlyph(int glyph, float scale, GlyphBitmap& out) const;

        const std::vector<uint8_t>& GetData() const { return m_data; }

    private:
        Font();

        struct Impl;
        std::unique_ptr<Impl> m_impl;
        std::vector<uint8_t>  m_data;
        int m_ascent  = 0;
        int m_descent = 0;
        int m_lineGap = 0;
        bool m_hasKerning = false;
    };
}

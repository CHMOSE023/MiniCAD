#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

#include "IFont.h"
#include "SHXFont.h"
#include "TTFFont.h"

namespace MiniCAD
{
    struct FontStyle
    {
        using FontStyleId = uint32_t;

        FontStyleId id = 0;

        std::string name;
        std::string fontFile;  // 文件名或绝对路径；相对路径由 FontEngine 拼接 searchDir
        std::string bigFontFile;   // SHX 大字体（中文等双字节字符）；非空时与主字体组成 SHXCompositeFont

        bool  isShx       = false;
        float widthFactor = 1.0f;
        float oblique     = 0.0f;
    };

    class FontEngine
    {
    public:
        // 设置字体文件搜索目录（相对于可执行文件）。默认 "fonts/"。
        void SetFontDir(std::string dir) { m_fontDir = std::move(dir); }
        const std::string& GetFontDir() const { return m_fontDir; }
        // 额外的搜索目录（如 Windows 的系统字体目录 C:\Windows\Fonts）：字体目录里没有的文件再到这些目录找，
        // 图纸引用的 simhei.ttf、arial.ttf 等不必复制进程序目录
        void AddSearchDir(std::string dir);

        // 解析 FontStyle → IFont（按 key 缓存，懒加载）
        IFont& Resolve(const FontStyle& style);

        // 清空缓存（换字体目录后调用）
        void Clear();

    private:
        std::string BuildKey(const FontStyle& style) const;
        std::string ResolvePath(const std::string& fontFile) const;
        std::string FindPath(const std::string& fontFile, const char* substitute, bool* found = nullptr) const;

    private:
        std::unordered_map<std::string, std::shared_ptr<IFont>> m_cache;
        uint64_t    m_nextRuntimeFontId = 1;
        std::string m_fontDir           = "fonts/";
        std::vector<std::string> m_extraDirs;   // 额外的搜索目录，都以分隔符结尾

        mutable std::mutex m_mutex;
    };
}

#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

#include "IFont.h"
#include "SHXFont.h"
#include "TTFFont.h"
#include "SHXCompositeFont.h"

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
        bool  shapesOnly  = false;   // 线型用的形文件：缺形不到单线回退字体里找（那里同号的形是钢筋符号等）
        float widthFactor = 1.0f;
        float oblique     = 0.0f;
    };

    // 字体所属的中日韩文字：决定缺字时先找哪种字体（日文汉字用日文字形）
    enum class CjkScript { Simplified, Traditional, Japanese, Korean };

    class FontEngine
    {
    public:
        // 设置字体文件搜索目录（以 / 结尾）。默认 "fonts/"，相对当前目录；主窗口设为资源目录下的 fonts/（MainFrame::Initialize）
        void SetFontDir(std::string dir) { m_fontDir = std::move(dir); }
        const std::string& GetFontDir() const { return m_fontDir; }
        // 额外的搜索目录（如 Windows 的系统字体目录 C:\Windows\Fonts）：字体目录里没有的文件再到这些目录找，
        // 图纸引用的 simhei.ttf、arial.ttf 等不必复制进程序目录
        void AddSearchDir(std::string dir);

        // 解析 FontStyle → IFont（按 key 缓存，懒加载）
        IFont& Resolve(const FontStyle& style);

        // 清空缓存（换字体目录后调用）
        void Clear();

        // 字体目录里新加了字体（网页版后下载的字体）：缺字回退重新找之前没找到的字体
        void RescanFallbacks();

        // 按字体文件名或族名猜它是哪种文字（新細明體、MS Gothic、Gulim ……）；看不出来时是简体
        static CjkScript ScriptOfFont(const std::string& fontFile);

    private:
        std::string BuildKey(const FontStyle& style) const;
        std::string ResolvePath(const std::string& fontFile) const;
        std::string FindPath(const std::string& fontFile, const char* substitute, bool* found = nullptr) const;
        std::string FindExisting(const std::string& fontFile) const;
        std::shared_ptr<TTFFallbackChain> FallbackChain(CjkScript script);
        std::shared_ptr<SHXFallbackChain> ShxFallbackChain(CjkScript script);
        std::string SubstituteTrueType(CjkScript script, int& face) const;

    private:
        std::unordered_map<std::string, std::shared_ptr<IFont>> m_cache;
        std::shared_ptr<TTFFallbackChain> m_chains[4];   // 按 CjkScript，TrueType 缺字时依次找的字体
        std::shared_ptr<SHXFallbackChain> m_shxChains[4];   // 按 CjkScript，SHX 缺字时依次找的单线字体
        uint64_t    m_nextRuntimeFontId = 1;
        std::string m_fontDir           = "fonts/";
        std::vector<std::string> m_extraDirs;   // 额外的搜索目录，都以分隔符结尾

        mutable std::mutex m_mutex;
    };
}

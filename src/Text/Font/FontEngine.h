#pragma once
#include <memory>
#include <string>
#include <unordered_map>
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
        std::string fontFile;

        bool  isShx       = false;
        float widthFactor = 1.0f;
        float oblique     = 0.0f;
    };

    class FontEngine
    {
    public:
        IFont& Resolve(const FontStyle& style);

        void Clear();

    private:
        std::string BuildKey(const FontStyle& style) const;

    private:
        std::unordered_map<std::string, std::shared_ptr<IFont>> m_cache;

        uint64_t m_nextRuntimeFontId = 1;

        mutable std::mutex m_mutex;
    };
}
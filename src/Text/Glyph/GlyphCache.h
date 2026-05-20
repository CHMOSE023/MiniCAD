#pragma once
#include <unordered_map>
#include <memory>
#include <mutex>
#include "Glyph.h"
#include "GlyphKey.h"

namespace MiniCAD 
{
    class GlyphCache
    {
    public:
        const Glyph& Get(const GlyphKey& key, std::function<Glyph()> loader)
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            auto it = m_cache.find(key);
            if (it != m_cache.end())
                return it->second;

            Glyph g = loader();
            auto [insertIt, ok] = m_cache.emplace(key, std::move(g));
            return insertIt->second;
        }

        void Clear()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_cache.clear();
        }

        size_t Size() const
        {
            return m_cache.size();
        }

    private:
        std::unordered_map<GlyphKey, Glyph> m_cache;
        mutable std::mutex                  m_mutex;
    };
}
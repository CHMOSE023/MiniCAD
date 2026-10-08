#include "ImageLibrary.h"
#include "Core/Log.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>

// 用 STB_IMAGE_STATIC 编译一个本文件私有副本，避免与宿主里的 stb_image 符号冲突。
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb/stb_image.h"

namespace MiniCAD
{
    namespace
    {
        namespace fs = std::filesystem;

        fs::path FromUtf8(const std::string& s)
        {
            return fs::path(std::u8string(s.begin(), s.end()));
        }

        std::string ToUtf8(const fs::path& p)
        {
            const auto u8 = p.u8string();
            return std::string(u8.begin(), u8.end());
        }

        std::atomic<uint64_t> g_nextKey{ 1 };
    }

    std::string ImageLibrary::Resolve(const std::string& path) const
    {
        if (path.empty()) return {};
        std::error_code ec;

        const fs::path p = FromUtf8(path);
        if (fs::is_regular_file(p, ec)) return path;

        if (!m_baseDir.empty())
        {
            const fs::path base = FromUtf8(m_baseDir);
            if (p.is_relative())
            {
                fs::path cand = base / p;
                if (fs::is_regular_file(cand, ec)) return ToUtf8(cand);
            }
            fs::path cand = base / p.filename();       // 文档挪了位置：到文档目录里找同名文件
            if (fs::is_regular_file(cand, ec)) return ToUtf8(cand);
        }
        return {};
    }

    std::shared_ptr<const ImageData> ImageLibrary::Get(const std::string& path)
    {
        if (auto it = m_cache.find(path); it != m_cache.end())
            return it->second;

        std::shared_ptr<const ImageData> result;
        const std::string real = Resolve(path);
        if (real.empty())
        {
            LOG_WARN("[Image] 找不到图像文件: %s", path.c_str());
        }
        else
        {
            std::ifstream f(FromUtf8(real), std::ios::binary);
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

            int w = 0, h = 0, comp = 0;
            unsigned char* px = bytes.empty() ? nullptr
                : stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp, 4);
            if (!px)
            {
                LOG_WARN("[Image] 无法解码图像: %s", real.c_str());
            }
            else
            {
                auto img    = std::make_shared<ImageData>();
                img->Key    = g_nextKey++;
                img->Width  = static_cast<uint32_t>(w);
                img->Height = static_cast<uint32_t>(h);
                img->Rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
                stbi_image_free(px);
                result = std::move(img);
            }
        }

        m_cache[path] = result;
        return result;
    }
}

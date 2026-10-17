#include "ImageLibrary.h"
#include "Core/Log.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
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

        // 字符串 → 路径。**这个函数绝不能抛出**。
        //
        // 调用链是 渲染 → ImageEntity::Draw → EmitImage → ImageLibrary::Get → 这里。
        // 字符串可能来自图纸文件（IMAGE 的相对路径），也可能是上一级传下来的非 UTF-8 字节
        // （例如用 path.string() 拿到的 GBK 路径）。只要字节不是合法 UTF-8，
        // fs::path(std::u8string(...)) 就会抛 filesystem_error；渲染路径上没有 catch，
        // Windows 上直接 __fastfail —— 实测打开含 IMAGE 的图纸必崩（0xC0000409）。
        // 所以：① 纯 ASCII 直接构造；② 试 UTF-8；③ 退回本地代码页(ANSI/GBK)；
        // ④ 都不行给空路径，由调用方按「文件不存在」处理（画占位框），绝不让渲染崩掉。
        fs::path FromUtf8(const std::string& s)
        {
            if (s.empty()) return {};

            bool ascii = true;
            for (unsigned char ch : s) { if (ch >= 0x80) { ascii = false; break; } }
            if (ascii) return fs::path(s);

            try
            {
                return fs::path(std::u8string(s.begin(), s.end()));
            }
            catch (const std::exception&)
            {
#ifdef _WIN32
                try
                {
                    const int need = ::MultiByteToWideChar(CP_ACP, 0, s.c_str(),
                                                           static_cast<int>(s.size()), nullptr, 0);
                    if (need > 0)
                    {
                        std::wstring w(static_cast<std::size_t>(need), static_cast<wchar_t>(0));
                        ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()),
                                              w.data(), need);
                        return fs::path(w);
                    }
                }
                catch (const std::exception&)
                {
                }
#endif
                return {};
            }
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
        std::string real;
        try
        {
            real = Resolve(path);
        }
        catch (const std::exception& e)
        {
            // 兜底：路径异常不得逃出渲染路径（理由见 FromUtf8 的注释）
            LOG_WARN("[Image] 路径无效，跳过: %s (%s)", path.c_str(), e.what());
            m_cache[path] = nullptr;
            return nullptr;
        }
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

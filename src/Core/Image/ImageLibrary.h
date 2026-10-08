#pragma once
#include "ImageData.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace MiniCAD
{
    // 光栅图像缓存：路径 → 解码后的位图。
    // 相对路径（或原路径已失效）时，到文档所在目录里找同名文件。
    class ImageLibrary
    {
    public:
        // 文档所在目录（未保存的文档传空串）
        void SetBaseDir(std::string dir)
        {
            if (dir == m_baseDir) return;
            m_baseDir = std::move(dir);
            m_cache.clear();                // 目录变了：之前找不到的文件可能现在找得到
        }

        // 取得图像；找不到 / 解码失败返回 nullptr（失败结果也会缓存，避免每帧重试）
        std::shared_ptr<const ImageData> Get(const std::string& path);

        // 丢弃某路径的缓存，下次 Get 重新读盘
        void Reload(const std::string& path) { m_cache.erase(path); }

        void Clear() { m_cache.clear(); }

        // 解析出实际存在的文件路径，找不到返回空串
        std::string Resolve(const std::string& path) const;

    private:
        std::string m_baseDir;
        std::unordered_map<std::string, std::shared_ptr<const ImageData>> m_cache;
    };
}

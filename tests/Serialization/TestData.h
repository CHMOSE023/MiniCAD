#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// 测试用的样例图纸：tests/data 下 ACadSharp 提供的同一张图的各版本文件
namespace MiniDWG::Test
{
    inline std::string SamplePath(const std::string& fileName)
    {
        return std::string(MINIDWG_SAMPLES_DIR) + "/" + fileName;
    }

    inline std::vector<std::uint8_t> ReadFileAll(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    // 读取文件开头最多 maxBytes 字节；文件不存在时返回空
    inline std::vector<std::uint8_t> ReadFileHead(const std::string& path, std::size_t maxBytes)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return {};

        std::vector<std::uint8_t> bytes(maxBytes);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(maxBytes));
        bytes.resize(static_cast<std::size_t>(file.gcount()));
        return bytes;
    }
}

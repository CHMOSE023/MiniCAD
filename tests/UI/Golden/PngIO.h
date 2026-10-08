#pragma once
#include "Software/SoftwareBackend.h"
#include <string>

namespace MiniGUI::Test
{
    // 写 RGBA8 PNG。自带一个简单的 deflate 压缩器（LZ77 + 固定 Huffman），
    // 界面截图大面积纯色，压缩率足够，基准图片提交到 git 不会太大
    bool WritePng(const std::string& path, const SoftwareImage& image);

    // 读 PNG（用 stb_image 解码，任何 PNG 都能读），统一转为 RGBA8
    bool ReadPng(const std::string& path, SoftwareImage& image);
}

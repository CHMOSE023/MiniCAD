#pragma once
#include "TreeSerializer.h"
#include <string>

namespace MiniCAD
{
    // JSON 文本档案(无第三方依赖)。
    //   写:默认构造(空根对象) → Serialize(...) → Dump() 得到 JSON 文本。
    //   读:Parse(text) 成功后 → Deserialize(...)。
    class JsonSerializer : public TreeSerializer
    {
    public:
        // 解析 JSON 文本并切换为读模式。失败返回 false(根保持原状)。
        bool Parse(const std::string& jsonText);

        // 序列化整棵树为 JSON 文本(带缩进的可读格式)。
        std::string Dump() const;
    };
}

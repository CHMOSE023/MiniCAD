#pragma once
#include "TreeSerializer.h"
#include <string>

namespace MiniCAD
{
    // .mcad 二进制档案。与 JsonSerializer 共用同一棵值树,两种格式可无损互转。
    //
    // 文件布局(小端):
    //   "MCAD"            4 字节魔数
    //   u8  version       容器版本(当前 1)
    //   value             根值(见下)
    //
    // value := u8 tag + 负载:
    //   0 Null
    //   1 Bool(false)     2 Bool(true)
    //   3 Int64(8B)       4 UInt64(8B)       5 Double(8B)
    //   6 String          u32 长度 + UTF-8 字节
    //   7 Array           u32 元素数 + 元素 value*
    //   8 Object          u32 成员数 + (String 键 + value)*
    //   9 DoubleArray     u32 元素数 + double*(点列等纯数字数组的紧凑路径)
    class BinarySerializer : public TreeSerializer
    {
    public:
        // 头 4 字节是否为 .mcad 魔数(用于打开文件时嗅探格式)。
        static bool IsBinary(const std::string& bytes);

        // 解码二进制内容并切换为读模式。失败返回 false(根保持原状)。
        bool Parse(const std::string& bytes);

        // 编码整棵树为二进制内容(含文件头)。
        std::string Dump() const;
    };
}

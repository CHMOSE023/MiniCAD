#pragma once
#include "ISerializer.h"
#include <iostream>
#include <fstream>

namespace MiniCAD
{
    class BinarySerializer : public ISerializer
    {
    public:
        // 构造器：用于保存
        explicit BinarySerializer(std::ostream& os)  : m_out(&os), m_in(nullptr) {}

        // 构造器：用于加载
        explicit BinarySerializer(std::istream& is)  : m_out(nullptr), m_in(&is) {}

        // 写入
        void WriteUInt64(uint64_t value) override;
        void WriteString(const std::string& str) override;
        void WriteBool(bool b) override;
        void WriteFloat(float f) override;

        // 读取
        uint64_t ReadUInt64() override;
        std::string ReadString() override;
        bool ReadBool() override;
        float ReadFloat() override;

    private:
        std::ostream* m_out;
        std::istream* m_in;
    };
}

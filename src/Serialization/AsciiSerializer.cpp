#include "AsciiSerializer.h"
#include <sstream>
#include <iomanip>

namespace MiniCAD
{
    // ===== 写入 =====
    void AsciiSerializer::WriteUInt64(uint64_t value)
    {
        (*m_out) << "UInt64 " << value << "\n";
    }

    void AsciiSerializer::WriteString(const std::string& str)
    {
        (*m_out) << "String " << str.size() << " " << str << "\n";
    }

    void AsciiSerializer::WriteBool(bool b)
    {
        (*m_out) << "Bool " << (b ? 1 : 0) << "\n";
    }

    void AsciiSerializer::WriteFloat(float f)
    {
        (*m_out) << "Float " << std::fixed << std::setprecision(6) << f << "\n";
    }

    // ===== 读取 =====
    uint64_t AsciiSerializer::ReadUInt64()
    {
        std::string label;
        uint64_t value;
        (*m_in) >> label >> value;
        m_in->ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return value;
    }

    std::string AsciiSerializer::ReadString()
    {
        std::string label;
        size_t len;
        (*m_in) >> label >> len;
        m_in->get(); // 跳过空格
        std::string str(len, '\0');
        m_in->read(&str[0], len);
        m_in->ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return str;
    }

    bool AsciiSerializer::ReadBool()
    {
        std::string label;
        int b;
        (*m_in) >> label >> b;
        m_in->ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return b != 0;
    }

    float AsciiSerializer::ReadFloat()
    {
        std::string label;
        float f;
        (*m_in) >> label >> f;
        m_in->ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return f;
    }
}
#include "BinarySerializer.h"
#include <cstring> // memcpy
#include <stdexcept>

namespace MiniCAD
{
    void BinarySerializer::WriteUInt64(uint64_t value)
    {
        if (!m_out) throw std::runtime_error("Serializer not in write mode");
        m_out->write(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    uint64_t BinarySerializer::ReadUInt64()
    {
        if (!m_in) throw std::runtime_error("Serializer not in read mode");
        uint64_t value = 0;
        m_in->read(reinterpret_cast<char*>(&value), sizeof(value));
        if (!*m_in) throw std::runtime_error("Failed to read uint64");
        return value;
    }

    void BinarySerializer::WriteBool(bool b)
    {
        uint8_t v = b ? 1 : 0;
        if (!m_out) throw std::runtime_error("Serializer not in write mode");
        m_out->write(reinterpret_cast<const char*>(&v), sizeof(v));
    }

    bool BinarySerializer::ReadBool()
    {
        if (!m_in) throw std::runtime_error("Serializer not in read mode");
        uint8_t v = 0;
        m_in->read(reinterpret_cast<char*>(&v), sizeof(v));
        if (!*m_in) throw std::runtime_error("Failed to read bool");
        return v != 0;
    }

    void BinarySerializer::WriteFloat(float f)
    {
        if (!m_out) throw std::runtime_error("Serializer not in write mode");
        m_out->write(reinterpret_cast<const char*>(&f), sizeof(f));
    }

    float BinarySerializer::ReadFloat()
    {
        if (!m_in) throw std::runtime_error("Serializer not in read mode");
        float f = 0.f;
        m_in->read(reinterpret_cast<char*>(&f), sizeof(f));
        if (!*m_in) throw std::runtime_error("Failed to read float");
        return f;
    }

    void BinarySerializer::WriteString(const std::string& str)
    {
        if (!m_out) throw std::runtime_error("Serializer not in write mode");
        uint64_t len = str.size();
        WriteUInt64(len);
        if (len > 0)
            m_out->write(str.data(), len);
    }

    std::string BinarySerializer::ReadString()
    {
        if (!m_in) throw std::runtime_error("Serializer not in read mode");
        uint64_t len = ReadUInt64();
        std::string str(len, '\0');
        if (len > 0)
            m_in->read(str.data(), len);
        if (!*m_in) throw std::runtime_error("Failed to read string");
        return str;
    }
}
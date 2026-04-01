// ISerializer.h
#pragma once
#include <string>
#include <cstdint>

namespace MiniCAD
{
    class ISerializer
    {
    public:
        virtual ~ISerializer() = default;

        virtual void WriteUInt64(uint64_t value) = 0;
        virtual void WriteString(const std::string& str) = 0;
        virtual void WriteBool(bool b) = 0;
        virtual void WriteFloat(float f) = 0;

        virtual uint64_t ReadUInt64() = 0;
        virtual std::string ReadString() = 0;
        virtual bool ReadBool() = 0;
        virtual float ReadFloat() = 0;
    };
}

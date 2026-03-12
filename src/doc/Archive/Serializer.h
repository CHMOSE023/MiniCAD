// ============================================================
// MiniCAD — doc/Archive/Serializer.h
// 职责：序列化策略接口（Strategy 模式）
// 依赖：math/Vector.h
// 约束：纯虚接口，不依赖具体格式
// ============================================================
#pragma once
#include "math/MathDefs.hpp"
#include "math/Vector.hpp"
#include <string>
#include <string_view>

namespace MiniCAD {

    class ISerializer {
    public:
        virtual ~ISerializer() = default;

        // ── 写入 ─────────────────────────────
        virtual void Write(std::string_view key, int value) = 0;
        virtual void Write(std::string_view key, Real value) = 0;
        virtual void Write(std::string_view key, std::string_view value) = 0;
        virtual void Write(std::string_view key, const Vec3& value) = 0;
        virtual void WriteVec3(const char* key, Real x, Real y, Real z) = 0;

        // ── 读取 ─────────────────────────────
        virtual int         ReadInt(std::string_view key) const = 0;
        virtual float       ReadReal(std::string_view key) const = 0;
        virtual std::string ReadString(std::string_view key) const = 0;
        virtual Vec3        ReadVec3(std::string_view key) const = 0;

        // ── Flush / Load ─────────────────────  

        virtual std::string Flush() const = 0;
        virtual bool Load(const std::string& json) = 0;
    };

} // namespace MiniCAD
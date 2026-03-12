// ============================================================
// MiniCAD — doc/Archive/JsonSerializer.h
// 职责：JSON 格式序列化实现（基于 std::map 的简单自实现）
// 依赖：core/Object/Object.h (ISerializer)
// 约束：不依赖第三方 JSON 库；不依赖 render/ ui/
// ============================================================
#pragma once
#include "doc/Archive/Serializer.h"
#include "math/Vector.hpp"
#include "math/MathDefs.hpp"
#include <map>
#include <string> 
#include <string_view>

namespace MiniCAD {

    class JsonSerializer : public ISerializer {
    public:
        // ── 写入 ─────────────────────────────
        void Write(std::string_view key, int value) override;
        void Write(std::string_view key, Real value) override;
        void Write(std::string_view key, std::string_view value) override;
        void Write(std::string_view key, const Vec3& value) override;
        void WriteVec3(const char* key, Real x, Real y, Real z) override;

        // ── 读取 ─────────────────────────────
        int         ReadInt(std::string_view key) const override;
        float       ReadReal(std::string_view key) const override;
        std::string ReadString(std::string_view key) const override;
        Vec3        ReadVec3(std::string_view key) const override;

        // ── 序列化 / 反序列化 ────────────────
        std::string Flush() const;
        bool        Load(const std::string& json);

    private:
        std::map<std::string, std::string> m_data;

        static std::string RealToStr(Real v);
        static float StrToReal(const std::string& s);

        // 旧实现复用
        void ReadVec3Impl(const char* key, Real& x, Real& y, Real& z) const;
    };

} // namespace MiniCAD
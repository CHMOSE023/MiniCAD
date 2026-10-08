#pragma once
#include "Database/Color.hpp"
#include "Database/DxfValue.h"
#include "Database/Transparency.hpp"
#include "Database/Types.hpp"
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

// 按成员类型把一个 DXF 组码的值写入成员（对应 ACadSharp DxfProperty.SetValue）。
// 生成的元数据（DxfMeta.g.cpp）为每个属性生成一个调用 Assign 的函数。
namespace MiniDWG
{
    // 向量：组码的十位决定分量（10/20/30 → X/Y/Z，11/21/31 ...）
    inline int VectorComponent(int code)
    {
        return (code / 10) % 10 - 1;
    }

    inline void Assign(XYZ& m, int code, const DxfValue& v)
    {
        if (v.Is<XYZ>())
        {
            m = v.AsXYZ();
            return;
        }
        switch (VectorComponent(code))
        {
        case 0: m.X = v.AsDouble(); break;
        case 1: m.Y = v.AsDouble(); break;
        case 2: m.Z = v.AsDouble(); break;
        default: break;
        }
    }

    inline void Assign(XY& m, int code, const DxfValue& v)
    {
        switch (VectorComponent(code))
        {
        case 0: m.X = v.AsDouble(); break;
        case 1: m.Y = v.AsDouble(); break;
        default: break;
        }
    }

    // 颜色：16 位整数组码是 ACI 索引（62、63 ...），32 位整数组码是真彩色（420、421、90）；
    // 字符串组码（430 颜色簿名称）不改变颜色
    inline void Assign(Color& m, int code, const DxfValue& v)
    {
        switch (GroupCodeTypeOf(code))
        {
        case GroupCodeType::Int16:
        case GroupCodeType::Byte:
            m = Color(static_cast<std::int16_t>(v.AsInt()));
            break;
        case GroupCodeType::Int32:
            m = Color::FromTrueColor(static_cast<std::uint32_t>(v.AsInt()));
            break;
        default:
            break;
        }
    }

    inline void Assign(Transparency& m, int, const DxfValue& v)
    {
        m = Transparency::FromAlphaValue(static_cast<std::int32_t>(v.AsInt()));
    }

    inline void Assign(std::string& m, int, const DxfValue& v) { m = v.AsString(); }
    inline void Assign(bool& m, int, const DxfValue& v) { m = v.AsBool(); }
    inline void Assign(double& m, int, const DxfValue& v) { m = v.AsDouble(); }

    inline void Assign(char& m, int, const DxfValue& v)
    {
        if (v.Is<std::string>())
        {
            const std::string s = v.AsString();
            m = s.empty() ? '\0' : s[0];
        }
        else
        {
            m = static_cast<char>(v.AsInt());
        }
    }

    inline void Assign(JulianDate& m, int, const DxfValue& v) { m.Value = v.AsDouble(); }
    inline void Assign(TimeSpanDays& m, int, const DxfValue& v) { m.Value = v.AsDouble(); }

    // 二进制数据：多行 310 依次追加
    inline void Assign(std::vector<std::uint8_t>& m, int, const DxfValue& v)
    {
        if (const auto* bytes = v.AsBytes())
            m.insert(m.end(), bytes->begin(), bytes->end());
    }

    // 整数与枚举（句柄 Handle 就是 uint64）
    template <class T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>)
    void Assign(T& m, int code, const DxfValue& v)
    {
        const GroupCodeType type = GroupCodeTypeOf(code);
        if constexpr (std::is_same_v<T, std::uint64_t>)
        {
            if (type == GroupCodeType::Handle)
            {
                m = v.AsHandle();
                return;
            }
        }
        m = static_cast<T>(v.AsInt());
    }

    template <class E>
        requires std::is_enum_v<E>
    void Assign(E& m, int, const DxfValue& v)
    {
        m = static_cast<E>(v.AsInt());
    }

    // 简单类型的集合：每个组码追加一个元素
    template <class T>
        requires(std::is_arithmetic_v<T> || std::is_same_v<T, std::string>)
    void Assign(std::vector<T>& m, int code, const DxfValue& v)
    {
        T item{};
        Assign(item, code, v);
        m.push_back(item);
    }

    // ── 取值（Assign 的反向，写文件用）：返回该组码应写出的值，空值表示不写 ──

    inline DxfValue Extract(const XYZ& m, int code)
    {
        switch (VectorComponent(code))
        {
        case 1: return DxfValue(m.Y);
        case 2: return DxfValue(m.Z);
        default: return DxfValue(m.X);
        }
    }

    inline DxfValue Extract(const XY& m, int code)
    {
        return DxfValue(VectorComponent(code) == 1 ? m.Y : m.X);
    }

    // 颜色：16 位整数组码写索引（真彩色时写不出索引，返回空），32 位整数组码只在真彩色时写
    inline DxfValue Extract(const Color& m, int code)
    {
        switch (GroupCodeTypeOf(code))
        {
        case GroupCodeType::Int16:
        case GroupCodeType::Byte:
            return m.IsTrueColor() ? DxfValue() : DxfValue(static_cast<std::int64_t>(m.Index()));
        case GroupCodeType::Int32:
            return m.IsTrueColor() ? DxfValue(static_cast<std::int64_t>(m.TrueColor())) : DxfValue();
        default:
            return DxfValue();
        }
    }

    inline DxfValue Extract(const Transparency& m, int)
    {
        return m.IsByLayer() ? DxfValue() : DxfValue(static_cast<std::int64_t>(Transparency::ToAlphaValue(m)));
    }

    inline DxfValue Extract(const std::string& m, int) { return DxfValue(m); }
    inline DxfValue Extract(bool m, int) { return DxfValue(m); }
    inline DxfValue Extract(double m, int) { return DxfValue(m); }
    inline DxfValue Extract(char m, int) { return DxfValue(static_cast<std::int64_t>(m)); }
    inline DxfValue Extract(const JulianDate& m, int) { return DxfValue(m.Value); }
    inline DxfValue Extract(const TimeSpanDays& m, int) { return DxfValue(m.Value); }
    inline DxfValue Extract(const std::vector<std::uint8_t>& m, int) { return DxfValue(m); }

    template <class T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>)
    DxfValue Extract(T m, int code)
    {
        if constexpr (std::is_same_v<T, std::uint64_t>)
        {
            if (GroupCodeTypeOf(code) == GroupCodeType::Handle)
                return DxfValue(HandleValue{ m });
        }
        return DxfValue(static_cast<std::int64_t>(m));
    }

    template <class E>
        requires std::is_enum_v<E>
    DxfValue Extract(E m, int)
    {
        return DxfValue(static_cast<std::int64_t>(m));
    }
}

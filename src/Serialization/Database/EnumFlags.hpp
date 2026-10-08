#pragma once
#include <type_traits>

// 为标志位枚举（ACadSharp 中带 [Flags] 的枚举）生成位运算符
#define MINIDWG_ENUM_FLAGS(E)                                                                      \
    constexpr E operator|(E a, E b)                                                                \
    {                                                                                              \
        using U = std::underlying_type_t<E>;                                                       \
        return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));                              \
    }                                                                                              \
    constexpr E operator&(E a, E b)                                                                \
    {                                                                                              \
        using U = std::underlying_type_t<E>;                                                       \
        return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));                              \
    }                                                                                              \
    constexpr E operator^(E a, E b)                                                                \
    {                                                                                              \
        using U = std::underlying_type_t<E>;                                                       \
        return static_cast<E>(static_cast<U>(a) ^ static_cast<U>(b));                              \
    }                                                                                              \
    constexpr E operator~(E a)                                                                     \
    {                                                                                              \
        using U = std::underlying_type_t<E>;                                                       \
        return static_cast<E>(~static_cast<U>(a));                                                 \
    }                                                                                              \
    constexpr E& operator|=(E& a, E b) { return a = a | b; }                                       \
    constexpr E& operator&=(E& a, E b) { return a = a & b; }                                       \
    constexpr bool HasFlag(E value, E flag) { return (value & flag) == flag; }

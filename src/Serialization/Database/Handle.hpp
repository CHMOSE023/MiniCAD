#pragma once
#include <cstdint>

namespace MiniDWG
{
    // DWG/DXF 对象句柄。对象之间的引用一律存句柄、经 CadDatabase 查表，不持有指针，
    // 避免 owner / reactor 互相引用形成循环所有权。
    using Handle = std::uint64_t;

    inline constexpr Handle kNullHandle = 0;
}

// ============================================================
// MiniCAD — core/Object/IDAllocator.h
// 职责：线程安全的自增 ObjectID 分配器
// 依赖：无
// 约束：线程安全，使用原子操作
// ============================================================
#pragma once
#include <cstdint>
#include <atomic>

namespace MiniCAD {

    class IDAllocator {
    public:
        using ObjectID = uint64_t;
        static constexpr ObjectID INVALID_ID = 0;

        static IDAllocator& Instance() {
            static IDAllocator s_instance;
            return s_instance;
        }

        ObjectID Allocate() {
            return s_counter.fetch_add(1, std::memory_order_relaxed);
        }

        void Reset() { s_counter.store(1, std::memory_order_relaxed); }

    private:
        IDAllocator() : s_counter(1) {}
        std::atomic<ObjectID> s_counter;
    };

} // namespace MiniCAD

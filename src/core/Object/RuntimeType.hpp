// ============================================================
// MiniCAD — core/Object/RuntimeType.hpp
// 职责：轻量级运行时类型系统，替代 dynamic_cast（项目禁用 RTTI）
// 约束：不使用 dynamic_cast / typeid，C++17 可用 inline static
// ============================================================

#pragma once

namespace MiniCAD {

    struct RuntimeTypeInfo {
        const char* name;                  // 类型名
        const RuntimeTypeInfo* parent;     // 父类型信息

        // 判断当前类型是否为 other 类型或其子类
        bool IsKindOf(const RuntimeTypeInfo* other) const {
            const RuntimeTypeInfo* cur = this;
            while (cur) {
                if (cur == other) return true;
                cur = cur->parent;
            }
            return false;
        }
    };

} // namespace MiniCAD

// ============================================================
// 宏：声明运行时类型
// ============================================================
#define DECLARE_RUNTIME_TYPE(ThisClass, ParentClass)                        \
public:                                                                      \
    inline static const ::MiniCAD::RuntimeTypeInfo s_typeInfo;              \
    virtual const ::MiniCAD::RuntimeTypeInfo* GetTypeInfo() const {         \
        return &s_typeInfo;                                                  \
    }                                                                        \
    template<typename T>                                                     \
    bool IsKindOf() const {                                                  \
        return GetTypeInfo()->IsKindOf(&T::s_typeInfo);                      \
    }

// ============================================================
// 宏：实现运行时类型
// ============================================================
#define IMPLEMENT_RUNTIME_TYPE(ThisClass, ParentClass)                      \
    const ::MiniCAD::RuntimeTypeInfo ThisClass::s_typeInfo {                \
        #ThisClass, &ParentClass::s_typeInfo                                 \
    };

#pragma once

namespace MiniCAD 
{

    struct RuntimeTypeInfo
    {
        const char* name;                  // 类型名
        const RuntimeTypeInfo* parent;     // 父类型信息
         
        bool IsKindOf(const RuntimeTypeInfo* other) const 
        {
            const RuntimeTypeInfo* cur = this;
            while (cur) 
            {
                if (cur == other)
                    return true;

                cur = cur->parent;
            }

            return false;
        }
    };

}  

// ============================================================
// 宏：声明运行时类型
// ============================================================
#define DECLARE_RUNTIME_TYPE(ThisClass, ParentClass)                         \
public:                                                                      \
    inline static const ::MiniCAD::RuntimeTypeInfo s_typeInfo {              \
        #ThisClass, &ParentClass::s_typeInfo                                 \
    };                                                                       \
    virtual const ::MiniCAD::RuntimeTypeInfo* GetTypeInfo() const override { \
        return &s_typeInfo;                                                  \
    }
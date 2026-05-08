namespace MiniCAD
{
    struct RuntimeTypeInfo
    {
        const char* Name;       // 类型名
        const RuntimeTypeInfo* Parent;     // 父类型信息

        bool IsKindOf(const RuntimeTypeInfo* other) const
        {
            const RuntimeTypeInfo* cur = this;
            while (cur)
            {
                if (cur == other)
                    return true;

                cur = cur->Parent;
            }

            return false;
        }
    };

// ============================================================
// 宏：声明运行时类型 , 宏定义 1.注册类型 2.查询类型
// ============================================================
#define DECLARE_RUNTIME_TYPE(ThisClass, ParentClass)                         \
public:                                                                      \
    using Super = ParentClass;                                               \
    inline static const ::MiniCAD::RuntimeTypeInfo TypeInfo {                \
        #ThisClass, &ParentClass::TypeInfo                                   \
    };                                                                       \
    virtual const ::MiniCAD::RuntimeTypeInfo* GetTypeInfo() const override { \
        return &TypeInfo;                                                    \
    }

}
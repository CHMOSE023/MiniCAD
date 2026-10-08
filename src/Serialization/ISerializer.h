// ISerializer.h
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace MiniCAD
{
    // 序列化档案接口(读写合一)。
    // 以「当前作用域」为中心的树状游标模型:
    //   · BeginObject / EndObject  进入/离开命名子对象(写时创建,读时定位)
    //   · BeginArray  / EndArray   进入/离开命名数组(读时返回元素个数)
    //   · BeginElement/ EndElement 进入/离开数组第 i 个元素对象(写 = 追加)
    //   · Value(key, v)            标量读写:写入 v;读取时缺失则保留 v 原值(即默认值)
    // 同一份 Serialize 代码按 IsLoading() 分流读写,保证存读结构一致。
    class ISerializer
    {
    public:
        virtual ~ISerializer() = default;

        virtual bool IsLoading() const = 0;

        // ── 作用域 ───────────────────────────────────────────────────────
        virtual bool BeginObject(const char* key) = 0;
        virtual void EndObject() = 0;

        // 读模式:返回是否存在,count 填元素个数;写模式:创建空数组,恒返回 true。
        virtual bool BeginArray(const char* key, size_t& count) = 0;
        virtual void EndArray() = 0;

        // 进入数组第 index 个元素(对象)。写模式忽略 index,在尾部追加。
        virtual bool BeginElement(size_t index) = 0;
        virtual void EndElement() = 0;

        // ── 标量 ─────────────────────────────────────────────────────────
        virtual void Value(const char* key, bool& v) = 0;
        virtual void Value(const char* key, int32_t& v) = 0;
        virtual void Value(const char* key, uint32_t& v) = 0;
        virtual void Value(const char* key, int64_t& v) = 0;
        virtual void Value(const char* key, uint64_t& v) = 0;
        virtual void Value(const char* key, double& v) = 0;
        virtual void Value(const char* key, float& v) = 0;
        virtual void Value(const char* key, std::string& v) = 0;

        // 数值数组(点列/节点矢量等高频路径,避免逐元素虚调用)。
        virtual void Value(const char* key, std::vector<double>& v) = 0;

        // 读模式:当前对象作用域内是否存在该键。写模式恒返回 false。
        virtual bool Has(const char* key) const = 0;
    };
}

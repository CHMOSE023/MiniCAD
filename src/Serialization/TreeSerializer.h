#pragma once
#include "ISerializer.h"
#include <memory>
#include <vector>

namespace MiniCAD
{
    namespace JsonDetail { struct JsonValue; }

    // 基于内存值树的档案基类:实现 ISerializer 的全部游标与标量逻辑。
    // 派生类只负责树与具体格式之间的编解码:
    //   · JsonSerializer   —— JSON 文本(可读、可手工编辑)
    //   · BinarySerializer —— .mcad 二进制(紧凑、读写快)
    class TreeSerializer : public ISerializer
    {
    public:
        TreeSerializer();                       // 写模式(空根对象)
        ~TreeSerializer() override;

        TreeSerializer(const TreeSerializer&) = delete;
        TreeSerializer& operator=(const TreeSerializer&) = delete;

        // ── ISerializer ──────────────────────────────────────────────────
        bool IsLoading() const override { return m_loading; }

        bool BeginObject(const char* key) override;
        void EndObject() override;

        bool BeginArray(const char* key, size_t& count) override;
        void EndArray() override;

        bool BeginElement(size_t index) override;
        void EndElement() override;

        void Value(const char* key, bool& v) override;
        void Value(const char* key, int32_t& v) override;
        void Value(const char* key, uint32_t& v) override;
        void Value(const char* key, int64_t& v) override;
        void Value(const char* key, uint64_t& v) override;
        void Value(const char* key, double& v) override;
        void Value(const char* key, float& v) override;
        void Value(const char* key, std::string& v) override;
        void Value(const char* key, std::vector<double>& v) override;

        bool Has(const char* key) const override;

    protected:
        JsonDetail::JsonValue* Cur() const;                          // 当前作用域节点
        JsonDetail::JsonValue* WriteSlot(const char* key);           // 写:在当前作用域取/建键槽
        const JsonDetail::JsonValue* ReadSlot(const char* key) const;// 读:查键(不存在返回空)

        // 派生类解析成功后调用:替换根节点并切换为读模式。
        void ResetForLoad(std::unique_ptr<JsonDetail::JsonValue> root);

        std::unique_ptr<JsonDetail::JsonValue> m_root;
        std::vector<JsonDetail::JsonValue*>    m_stack;   // 作用域栈,栈底为根
        bool                                   m_loading = false;
    };
}

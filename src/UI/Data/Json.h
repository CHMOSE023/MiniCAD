#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MiniGUI
{
    // 最小的 JSON 值：null / bool / number / string / array / object。
    // 对象保留键的书写顺序（菜单、工具栏的顺序就是文件里的顺序）；重复的键以最后一个为准。
    // 解析支持 // 和 /* */ 注释、末尾多余的逗号（手写界面描述文件更方便），字符串按 UTF-8 处理
    class JsonValue
    {
    public:
        enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };

        using Array  = std::vector<JsonValue>;
        using Member = std::pair<std::string, JsonValue>;
        using Object = std::vector<Member>;

        JsonValue() = default;
        JsonValue(std::nullptr_t) {}
        JsonValue(bool b)               : m_type(Type::Bool), m_bool(b) {}
        JsonValue(double n)             : m_type(Type::Number), m_number(n) {}
        JsonValue(int n)                : m_type(Type::Number), m_number(n) {}
        JsonValue(std::string s)        : m_type(Type::String), m_string(std::move(s)) {}
        JsonValue(const char* s)        : m_type(Type::String), m_string(s) {}
        static JsonValue MakeArray()    { JsonValue v; v.m_type = Type::Array;  return v; }
        static JsonValue MakeObject()   { JsonValue v; v.m_type = Type::Object; return v; }

        Type GetType()   const { return m_type; }
        bool IsNull()    const { return m_type == Type::Null; }
        bool IsBool()    const { return m_type == Type::Bool; }
        bool IsNumber()  const { return m_type == Type::Number; }
        bool IsString()  const { return m_type == Type::String; }
        bool IsArray()   const { return m_type == Type::Array; }
        bool IsObject()  const { return m_type == Type::Object; }

        // 类型不符时返回默认值
        bool               AsBool  (bool def = false) const        { return IsBool() ? m_bool : def; }
        double             AsNumber(double def = 0.0) const        { return IsNumber() ? m_number : def; }
        const std::string& AsString() const;                        // 不是字符串时返回空串
        std::string        AsString(std::string_view def) const     { return IsString() ? m_string : std::string(def); }

        const Array&  GetArray()  const;                            // 类型不符时返回空数组
        const Object& GetObject() const;
        Array&        EditArray()  { return m_array; }
        Object&       EditObject() { return m_object; }

        // 对象成员；不存在或不是对象时返回 nullptr
        const JsonValue* Find(std::string_view key) const;
        // 对象成员；不存在时返回 null 值（便于链式访问）
        const JsonValue& operator[](std::string_view key) const;
        size_t           Size() const { return IsArray() ? m_array.size() : (IsObject() ? m_object.size() : 0); }

        void Push(JsonValue v)                     { m_array.push_back(std::move(v)); }
        void Set(std::string key, JsonValue v);

        // 序列化：indent < 0 时紧凑输出
        std::string Dump(int indent = -1) const;

    private:
        void DumpTo(std::string& out, int indent, int depth) const;

        Type        m_type   = Type::Null;
        bool        m_bool   = false;
        double      m_number = 0.0;
        std::string m_string;
        Array       m_array;
        Object      m_object;
    };

    struct JsonError
    {
        std::string message;
        int         line   = 0;     // 从 1 开始
        int         column = 0;     // 从 1 开始，按字节计

        std::string ToString() const;   // "第 3 行第 15 列：缺少 ':'"
    };

    // 解析失败返回 false，error 给出位置和原因
    bool ParseJson(std::string_view text, JsonValue& out, JsonError& error);

    // 读取 UTF-8 文件（可带 BOM）；失败返回 false
    bool ReadTextFile(const std::string& utf8Path, std::string& out);
}

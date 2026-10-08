// 序列化内部值树(模块内部头,不对外暴露)。
// JsonSerializer(JSON 文本)与 BinarySerializer(.mcad 二进制)共用同一棵树,
// 仅编解码不同 —— 两种格式可无损互转。
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace MiniCAD::JsonDetail
{
    // 对象成员用保序 vector<pair>(文档结构固定,无需哈希查找;保序便于 diff)。
    // 数字同时保留 double 与 int64/uint64 两轨,整型(ObjectID 等)往返不丢精度。
    struct JsonValue
    {
        enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };

        Type        type  = Type::Null;
        bool        b     = false;
        double      num   = 0.0;
        int64_t     inum  = 0;      // type==Number 且 isInt 时有效
        uint64_t    unum  = 0;      //   同上(无符号轨,解析超大正整数用)
        bool        isInt = false;
        std::string str;

        std::vector<JsonValue>                         arr;
        std::vector<std::pair<std::string, JsonValue>> obj;

        JsonValue* Find(const char* key)
        {
            for (auto& kv : obj)
                if (kv.first == key) return &kv.second;
            return nullptr;
        }
        const JsonValue* Find(const char* key) const
        {
            return const_cast<JsonValue*>(this)->Find(key);
        }

        // 写:取既有键或追加新键。
        JsonValue& GetOrAdd(const char* key)
        {
            if (JsonValue* v = Find(key)) return *v;
            obj.emplace_back(key, JsonValue{});
            return obj.back().second;
        }

        void SetInt(int64_t v)  { type = Type::Number; isInt = true;  inum = v; unum = (v < 0 ? 0 : static_cast<uint64_t>(v)); num = static_cast<double>(v); }
        void SetUInt(uint64_t v){ type = Type::Number; isInt = true;  unum = v; inum = (v > static_cast<uint64_t>(INT64_MAX) ? INT64_MAX : static_cast<int64_t>(v)); num = static_cast<double>(v); }
        void SetNum(double v)   { type = Type::Number; isInt = false; num = v; }
    };
}

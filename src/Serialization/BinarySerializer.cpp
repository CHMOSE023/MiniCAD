#include "BinarySerializer.h"
#include "JsonValue.hpp"
#include <cstring>
#include <unordered_map>
#include <vector>

namespace MiniCAD
{
using JsonDetail::JsonValue;

namespace
{
    constexpr char    kMagic[4]  = { 'M', 'C', 'A', 'D' };
    // v1:逐节点写键名字符串,整数固定 8 字节。
    // v2:文件头字符串表(对象键与字符串值只存一次,节点内存表索引),
    //     整数/长度/索引一律 varint。几万实体的文档体积约为 v1 的 1/5。
    // 写出固定 v2;读取按版本字节分流,v1 旧文件仍可打开。
    constexpr uint8_t kVersion   = 2;

    enum Tag : uint8_t
    {
        TagNull        = 0,
        TagFalse       = 1,
        TagTrue        = 2,
        TagInt64       = 3,
        TagUInt64      = 4,
        TagDouble      = 5,
        TagString      = 6,
        TagArray       = 7,
        TagObject      = 8,
        TagDoubleArray = 9,
    };

    // ── 写出 ─────────────────────────────────────────────────────────────
    // 小端平台直拷(x86/x64/wasm 均为小端)。
    template<typename T>
    void Put(std::string& out, T v)
    {
        out.append(reinterpret_cast<const char*>(&v), sizeof(T));
    }

    // ── varint(LEB128)/ zigzag ──────────────────────────────────────────
    void PutVarint(std::string& out, uint64_t v)
    {
        while (v >= 0x80)
        {
            out.push_back(static_cast<char>(static_cast<uint8_t>(v) | 0x80));
            v >>= 7;
        }
        out.push_back(static_cast<char>(static_cast<uint8_t>(v)));
    }

    uint64_t ZigZag(int64_t v)   { return (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63); }
    int64_t  UnZigZag(uint64_t u){ return static_cast<int64_t>(u >> 1) ^ -static_cast<int64_t>(u & 1); }

    // ── 字符串表:对象键与字符串值统一驻留,按首次出现顺序编号 ─────────────
    struct StringTable
    {
        std::unordered_map<std::string, uint32_t> map;
        std::vector<const std::string*>           order;

        uint32_t Intern(const std::string& s)
        {
            auto [it, inserted] = map.emplace(s, static_cast<uint32_t>(map.size()));
            if (inserted) order.push_back(&it->first);
            return it->second;
        }
    };


    // ── v2 编码:字符串以表索引引用,整数/长度走 varint,double 仍 8 字节 ───
    // (v1 编码已移除:写出固定 v2;v1 仅保留解码以打开旧文件。)
    void EncodeValueV2(std::string& out, const JsonValue& v, StringTable& tbl)
    {
        switch (v.type)
        {
        case JsonValue::Type::Null:
            out.push_back(static_cast<char>(TagNull));
            break;
        case JsonValue::Type::Bool:
            out.push_back(static_cast<char>(v.b ? TagTrue : TagFalse));
            break;
        case JsonValue::Type::Number:
            if (v.isInt)
            {
                if (v.inum < 0) { out.push_back(static_cast<char>(TagInt64));  PutVarint(out, ZigZag(v.inum)); }
                else            { out.push_back(static_cast<char>(TagUInt64)); PutVarint(out, v.unum); }
            }
            else
            {
                out.push_back(static_cast<char>(TagDouble));
                Put<double>(out, v.num);
            }
            break;
        case JsonValue::Type::String:
            out.push_back(static_cast<char>(TagString));
            PutVarint(out, tbl.Intern(v.str));
            break;
        case JsonValue::Type::Array:
        {
            bool allDouble = !v.arr.empty();
            for (const auto& e : v.arr)
                if (e.type != JsonValue::Type::Number || e.isInt) { allDouble = false; break; }

            if (allDouble)
            {
                out.push_back(static_cast<char>(TagDoubleArray));
                PutVarint(out, v.arr.size());
                for (const auto& e : v.arr)
                    Put<double>(out, e.num);
            }
            else
            {
                out.push_back(static_cast<char>(TagArray));
                PutVarint(out, v.arr.size());
                for (const auto& e : v.arr)
                    EncodeValueV2(out, e, tbl);
            }
            break;
        }
        case JsonValue::Type::Object:
            out.push_back(static_cast<char>(TagObject));
            PutVarint(out, v.obj.size());
            for (const auto& kv : v.obj)
            {
                PutVarint(out, tbl.Intern(kv.first));
                EncodeValueV2(out, kv.second, tbl);
            }
            break;
        }
    }

    // ── 读入(全程带边界检查,损坏文件安全失败)─────────────────────────────
    struct Reader
    {
        const char* p;
        const char* end;

        bool Bytes(void* dst, size_t n)
        {
            if (static_cast<size_t>(end - p) < n) return false;
            std::memcpy(dst, p, n);
            p += n;
            return true;
        }

        template<typename T>
        bool Get(T& v) { return Bytes(&v, sizeof(T)); }

        bool GetString(std::string& s)
        {
            uint32_t len = 0;
            if (!Get(len)) return false;
            if (static_cast<size_t>(end - p) < len) return false;
            s.assign(p, len);
            p += len;
            return true;
        }

        bool GetVarint(uint64_t& v)
        {
            v = 0;
            for (int shift = 0; shift < 64; shift += 7)
            {
                if (p >= end) return false;
                const uint8_t b = static_cast<uint8_t>(*p++);
                v |= static_cast<uint64_t>(b & 0x7F) << shift;
                if (!(b & 0x80)) return true;
            }
            return false;   // 超过 10 字节:数据损坏
        }

        bool DecodeValue(JsonValue& out)
        {
            uint8_t tag = 0;
            if (!Get(tag)) return false;
            switch (tag)
            {
            case TagNull:
                out.type = JsonValue::Type::Null;
                return true;
            case TagFalse:
            case TagTrue:
                out.type = JsonValue::Type::Bool;
                out.b = (tag == TagTrue);
                return true;
            case TagInt64:
            {
                int64_t v = 0;
                if (!Get(v)) return false;
                out.SetInt(v);
                return true;
            }
            case TagUInt64:
            {
                uint64_t v = 0;
                if (!Get(v)) return false;
                out.SetUInt(v);
                return true;
            }
            case TagDouble:
            {
                double v = 0.0;
                if (!Get(v)) return false;
                out.SetNum(v);
                return true;
            }
            case TagString:
                out.type = JsonValue::Type::String;
                return GetString(out.str);
            case TagDoubleArray:
            {
                uint32_t n = 0;
                if (!Get(n)) return false;
                if (static_cast<size_t>(end - p) < static_cast<size_t>(n) * sizeof(double)) return false;
                out.type = JsonValue::Type::Array;
                out.arr.resize(n);
                for (uint32_t i = 0; i < n; ++i)
                {
                    double v = 0.0;
                    Get(v);
                    out.arr[i].SetNum(v);
                }
                return true;
            }
            case TagArray:
            {
                uint32_t n = 0;
                if (!Get(n)) return false;
                out.type = JsonValue::Type::Array;
                out.arr.reserve(n);
                for (uint32_t i = 0; i < n; ++i)
                {
                    JsonValue child;
                    if (!DecodeValue(child)) return false;
                    out.arr.push_back(std::move(child));
                }
                return true;
            }
            case TagObject:
            {
                uint32_t n = 0;
                if (!Get(n)) return false;
                out.type = JsonValue::Type::Object;
                out.obj.reserve(n);
                for (uint32_t i = 0; i < n; ++i)
                {
                    std::string key;
                    if (!GetString(key)) return false;
                    JsonValue child;
                    if (!DecodeValue(child)) return false;
                    out.obj.emplace_back(std::move(key), std::move(child));
                }
                return true;
            }
            default:
                return false;   // 未知 tag:版本不兼容或文件损坏
            }
        }

        // ── v2 解码:strings 为文件头读出的字符串表 ──────────────────────
        bool DecodeValueV2(JsonValue& out, const std::vector<std::string>& strings)
        {
            uint8_t tag = 0;
            if (!Get(tag)) return false;
            switch (tag)
            {
            case TagNull:
                out.type = JsonValue::Type::Null;
                return true;
            case TagFalse:
            case TagTrue:
                out.type = JsonValue::Type::Bool;
                out.b = (tag == TagTrue);
                return true;
            case TagInt64:
            {
                uint64_t u = 0;
                if (!GetVarint(u)) return false;
                out.SetInt(UnZigZag(u));
                return true;
            }
            case TagUInt64:
            {
                uint64_t u = 0;
                if (!GetVarint(u)) return false;
                out.SetUInt(u);
                return true;
            }
            case TagDouble:
            {
                double v = 0.0;
                if (!Get(v)) return false;
                out.SetNum(v);
                return true;
            }
            case TagString:
            {
                uint64_t idx = 0;
                if (!GetVarint(idx) || idx >= strings.size()) return false;
                out.type = JsonValue::Type::String;
                out.str = strings[idx];
                return true;
            }
            case TagDoubleArray:
            {
                uint64_t n = 0;
                if (!GetVarint(n)) return false;
                // 除法比较,避免 n * 8 在损坏文件下回绕
                if (n > static_cast<uint64_t>(end - p) / sizeof(double)) return false;
                out.type = JsonValue::Type::Array;
                out.arr.resize(static_cast<size_t>(n));
                for (uint64_t i = 0; i < n; ++i)
                {
                    double v = 0.0;
                    Get(v);
                    out.arr[static_cast<size_t>(i)].SetNum(v);
                }
                return true;
            }
            case TagArray:
            {
                uint64_t n = 0;
                if (!GetVarint(n)) return false;
                out.type = JsonValue::Type::Array;
                for (uint64_t i = 0; i < n; ++i)
                {
                    JsonValue child;
                    if (!DecodeValueV2(child, strings)) return false;
                    out.arr.push_back(std::move(child));
                }
                return true;
            }
            case TagObject:
            {
                uint64_t n = 0;
                if (!GetVarint(n)) return false;
                out.type = JsonValue::Type::Object;
                for (uint64_t i = 0; i < n; ++i)
                {
                    uint64_t idx = 0;
                    if (!GetVarint(idx) || idx >= strings.size()) return false;
                    JsonValue child;
                    if (!DecodeValueV2(child, strings)) return false;
                    out.obj.emplace_back(strings[idx], std::move(child));
                }
                return true;
            }
            default:
                return false;
            }
        }
    };
} // namespace

bool BinarySerializer::IsBinary(const std::string& bytes)
{
    return bytes.size() >= sizeof(kMagic)
        && std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) == 0;
}

bool BinarySerializer::Parse(const std::string& bytes)
{
    if (!IsBinary(bytes) || bytes.size() < sizeof(kMagic) + 1)
        return false;

    const uint8_t version = static_cast<uint8_t>(bytes[sizeof(kMagic)]);
    Reader r{ bytes.data() + sizeof(kMagic) + 1, bytes.data() + bytes.size() };
    auto root = std::make_unique<JsonValue>();

    if (version == 1)
    {
        if (!r.DecodeValue(*root) || root->type != JsonValue::Type::Object)
            return false;
    }
    else if (version == 2)
    {
        // 字符串表:varint 数量 + 逐条(varint 长度 + 字节)
        uint64_t count = 0;
        if (!r.GetVarint(count)) return false;
        std::vector<std::string> strings;
        // 损坏文件的天文数字 count 不能直接 reserve;循环本身受剩余字节数约束
        strings.reserve(static_cast<size_t>(count < 65536 ? count : 65536));
        for (uint64_t i = 0; i < count; ++i)
        {
            uint64_t len = 0;
            if (!r.GetVarint(len)) return false;
            if (static_cast<size_t>(r.end - r.p) < len) return false;
            strings.emplace_back(r.p, static_cast<size_t>(len));
            r.p += len;
        }

        if (!r.DecodeValueV2(*root, strings) || root->type != JsonValue::Type::Object)
            return false;
    }
    else
    {
        return false;   // 未知版本
    }

    if (r.p != r.end)
        return false;   // 尾部有多余内容

    ResetForLoad(std::move(root));
    return true;
}

std::string BinarySerializer::Dump() const
{
    // 先编码正文(过程中驻留字符串),再组装 文件头 + 字符串表 + 正文。
    StringTable tbl;
    std::string body;
    body.reserve(4096);
    EncodeValueV2(body, *m_root, tbl);

    std::string out;
    size_t tblBytes = 0;
    for (const auto* s : tbl.order) tblBytes += s->size() + 5;
    out.reserve(sizeof(kMagic) + 1 + 10 + tblBytes + body.size());

    out.append(kMagic, sizeof(kMagic));
    out.push_back(static_cast<char>(kVersion));
    PutVarint(out, tbl.order.size());
    for (const auto* s : tbl.order)
    {
        PutVarint(out, s->size());
        out.append(*s);
    }
    out.append(body);
    return out;
}
}

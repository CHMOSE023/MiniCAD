#include "Database/DxfValue.h"
#include <charconv>
#include <cstdlib>

namespace MiniDWG
{
    double DxfValue::AsDouble() const
    {
        struct Visitor
        {
            double operator()(std::monostate) const { return 0.0; }
            double operator()(const std::string& s) const { return std::strtod(s.c_str(), nullptr); }
            double operator()(double v) const { return v; }
            double operator()(std::int64_t v) const { return static_cast<double>(v); }
            double operator()(bool v) const { return v ? 1.0 : 0.0; }
            double operator()(const std::vector<std::uint8_t>&) const { return 0.0; }
            double operator()(const XYZ& v) const { return v.X; }
            double operator()(HandleValue h) const { return static_cast<double>(h.Value); }
        };
        return std::visit(Visitor{}, m_value);
    }

    std::int64_t DxfValue::AsInt() const
    {
        struct Visitor
        {
            std::int64_t operator()(std::monostate) const { return 0; }
            std::int64_t operator()(const std::string& s) const { return std::strtoll(s.c_str(), nullptr, 10); }
            std::int64_t operator()(double v) const { return static_cast<std::int64_t>(v); }
            std::int64_t operator()(std::int64_t v) const { return v; }
            std::int64_t operator()(bool v) const { return v ? 1 : 0; }
            std::int64_t operator()(const std::vector<std::uint8_t>&) const { return 0; }
            std::int64_t operator()(const XYZ& v) const { return static_cast<std::int64_t>(v.X); }
            std::int64_t operator()(HandleValue h) const { return static_cast<std::int64_t>(h.Value); }
        };
        return std::visit(Visitor{}, m_value);
    }

    Handle DxfValue::AsHandle() const
    {
        if (auto* h = std::get_if<HandleValue>(&m_value))
            return h->Value;
        if (auto* s = std::get_if<std::string>(&m_value))
        {
            Handle v = 0;
            std::from_chars(s->data(), s->data() + s->size(), v, 16);
            return v;
        }
        return static_cast<Handle>(AsInt());
    }

    std::string DxfValue::AsString() const
    {
        struct Visitor
        {
            std::string operator()(std::monostate) const { return {}; }
            std::string operator()(const std::string& s) const { return s; }
            std::string operator()(double v) const { return std::to_string(v); }
            std::string operator()(std::int64_t v) const { return std::to_string(v); }
            std::string operator()(bool v) const { return v ? "1" : "0"; }
            std::string operator()(const std::vector<std::uint8_t>&) const { return {}; }
            std::string operator()(const XYZ&) const { return {}; }
            std::string operator()(HandleValue h) const
            {
                char buf[17];
                auto r = std::to_chars(buf, buf + sizeof(buf), h.Value, 16);
                std::string s(buf, r.ptr);
                for (char& c : s)
                    c = static_cast<char>(c >= 'a' && c <= 'f' ? c - 'a' + 'A' : c);
                return s;
            }
        };
        return std::visit(Visitor{}, m_value);
    }

    XYZ DxfValue::AsXYZ() const
    {
        if (auto* p = std::get_if<XYZ>(&m_value))
            return *p;
        return XYZ{ AsDouble(), 0, 0 };
    }

    // 与 ACadSharp GroupCodeValue.TransformValue 一致
    GroupCodeType GroupCodeTypeOf(int code)
    {
        if (code >= 0 && code <= 4) return GroupCodeType::String;
        if (code == 5) return GroupCodeType::Handle;
        if (code >= 6 && code <= 9) return GroupCodeType::String;
        if (code >= 10 && code <= 39) return GroupCodeType::Point3D;
        if (code >= 40 && code <= 59) return GroupCodeType::Double;
        if (code >= 60 && code <= 79) return GroupCodeType::Int16;
        if (code >= 90 && code <= 99) return GroupCodeType::Int32;
        if (code >= 100 && code <= 102) return GroupCodeType::String;
        if (code == 105) return GroupCodeType::Handle;
        if (code >= 110 && code <= 149) return GroupCodeType::Double;
        if (code >= 160 && code <= 169) return GroupCodeType::Int64;
        if (code >= 170 && code <= 179) return GroupCodeType::Int16;
        if (code >= 210 && code <= 239) return GroupCodeType::Double;
        if (code >= 270 && code <= 279) return GroupCodeType::Int16;
        if (code >= 280 && code <= 289) return GroupCodeType::Byte;
        if (code >= 290 && code <= 299) return GroupCodeType::Bool;
        if (code >= 300 && code <= 309) return GroupCodeType::String;
        if (code >= 310 && code <= 319) return GroupCodeType::Chunk;
        if (code >= 320 && code <= 369) return GroupCodeType::Handle;
        if (code >= 370 && code <= 389) return GroupCodeType::Int16;
        if (code >= 390 && code <= 399) return GroupCodeType::Handle;
        if (code >= 400 && code <= 409) return GroupCodeType::Int16;
        if (code >= 410 && code <= 419) return GroupCodeType::String;
        if (code >= 420 && code <= 429) return GroupCodeType::Int32;
        if (code >= 430 && code <= 439) return GroupCodeType::String;
        if (code >= 440 && code <= 459) return GroupCodeType::Int32;
        if (code >= 460 && code <= 469) return GroupCodeType::Double;
        if (code >= 470 && code <= 479) return GroupCodeType::String;
        if (code >= 480 && code <= 481) return GroupCodeType::Handle;
        if (code == 999) return GroupCodeType::Comment;
        if (code >= 1000 && code <= 1003) return GroupCodeType::String;
        if (code == 1004) return GroupCodeType::Chunk;
        if (code >= 1005 && code <= 1009) return GroupCodeType::Handle;
        if (code >= 1010 && code <= 1059) return GroupCodeType::Double;
        if (code >= 1060 && code <= 1070) return GroupCodeType::Int16;
        if (code == 1071) return GroupCodeType::Int32;
        return GroupCodeType::None;
    }
}

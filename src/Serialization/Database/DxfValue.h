#pragma once
#include "Database/Handle.hpp"
#include "Database/Types.hpp"
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace MiniDWG
{
    // 句柄值（与整数区分开，DXF 中以十六进制字符串出现）
    struct HandleValue
    {
        Handle Value = kNullHandle;

        friend bool operator==(const HandleValue&, const HandleValue&) = default;
    };

    // 一个组码的值：DXF 读写、XRecord 条目共用。
    // 整数统一存为 int64，按需取较窄的类型。
    class DxfValue
    {
    public:
        using Storage = std::variant<std::monostate, std::string, double, std::int64_t, bool,
                                     std::vector<std::uint8_t>, XYZ, HandleValue>;

        DxfValue() = default;
        DxfValue(std::string v) : m_value(std::move(v)) {}
        DxfValue(double v) : m_value(v) {}
        DxfValue(std::int64_t v) : m_value(v) {}
        DxfValue(bool v) : m_value(v) {}
        DxfValue(std::vector<std::uint8_t> v) : m_value(std::move(v)) {}
        DxfValue(XYZ v) : m_value(v) {}
        DxfValue(HandleValue v) : m_value(v) {}

        const Storage& Get() const { return m_value; }
        bool IsEmpty() const { return std::holds_alternative<std::monostate>(m_value); }

        template <class T>
        bool Is() const { return std::holds_alternative<T>(m_value); }

        // 数值视图：整数、布尔、浮点、句柄之间互相转换；字符串尝试解析
        double AsDouble() const;
        std::int64_t AsInt() const;
        bool AsBool() const { return AsInt() != 0; }
        Handle AsHandle() const;

        // 字符串视图：数值转为文本
        std::string AsString() const;

        const std::vector<std::uint8_t>* AsBytes() const { return std::get_if<std::vector<std::uint8_t>>(&m_value); }
        XYZ AsXYZ() const;

        friend bool operator==(const DxfValue&, const DxfValue&) = default;

    private:
        Storage m_value;
    };

    // DXF 组码的值类型（对应 ACadSharp GroupCodeValueType）
    enum class GroupCodeType : std::uint8_t
    {
        None, String, Point3D, Double, Int16, Int32, Int64, Byte, Bool, Handle, Chunk, Comment,
    };

    GroupCodeType GroupCodeTypeOf(int code);
}

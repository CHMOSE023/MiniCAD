#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace MiniCAD
{
    // =========================================================================
    // 图层层级（同 Rhino 的子图层）：层级写在图层名里，各级之间用 '$' 分隔，如 "模型$G2$钢箱梁"。
    // DWG 的图层表是平的，AutoCAD 的图层名又不能含 ':'（Rhino 自己的 "A::B" 写不进去），
    // Rhino 导出 DWG 时子图层就写成 "父$子$孙"；这样文件格式不用改，与 AutoCAD、Rhino 往返都不丢层级。
    // 有空的一级时不分级，整个名字算一级（"$0$xx" 这类绑定外部参照留下的名字、"a$$b"）。
    // 各级名称比较时 ASCII 字母不分大小写（同 AutoCAD 的图层名）。
    // =========================================================================
    namespace LayerPath
    {
        inline constexpr char Separator = '$';

        // 拆成各级名称（指向 name 内部）；不分级时只有一项
        inline std::vector<std::string_view> Split(std::string_view name)
        {
            std::vector<std::string_view> parts;
            size_t start = 0;
            while (true)
            {
                const size_t at = name.find(Separator, start);
                const std::string_view part = name.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start);
                if (part.empty())
                    return { name };
                parts.push_back(part);
                if (at == std::string_view::npos)
                    return parts;
                start = at + 1;
            }
        }

        // 上一级的路径；顶层图层为空
        inline std::string_view Parent(std::string_view name)
        {
            const auto parts = Split(name);
            return parts.size() < 2 ? std::string_view{} : name.substr(0, name.size() - parts.back().size() - 1);
        }

        // 最后一级的名称（面板里显示的名字）
        inline std::string_view Leaf(std::string_view name) { return Split(name).back(); }

        inline std::string Join(std::string_view parent, std::string_view leaf)
        {
            std::string s(parent);
            if (!s.empty())
                s += Separator;
            s += leaf;
            return s;
        }

        // 能作为一级（或几级）名称写在上级路径后面：不空、不以 '$' 开头或结尾、没有 "$$"
        inline bool IsValidRelative(std::string_view text)
        {
            return !text.empty() && text.front() != Separator && text.back() != Separator &&
                   text.find("$$") == std::string_view::npos;
        }

        inline bool SameSegment(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
            {
                char x = a[i], y = b[i];
                if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + 32);
                if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + 32);
                if (x != y)
                    return false;
            }
            return true;
        }

        // name 就是 path，或在 path 下面（path 不必有对应的图层）
        inline bool InSubtree(std::string_view name, std::string_view path)
        {
            // 先比前缀（多数图层到这里就排除，面板每帧给分组行汇总下级状态时要逐个图层判断）
            if (name.size() < path.size() || !SameSegment(name.substr(0, path.size()), path) ||
                (name.size() > path.size() && name[path.size()] != Separator))
                return false;
            const auto n = Split(name), p = Split(path);
            if (p.size() > n.size())
                return false;
            for (size_t i = 0; i < p.size(); ++i)
                if (!SameSegment(n[i], p[i]))
                    return false;
            return true;
        }

        // 把 InSubtree(name, from) 的 name 搬到 to 下面："A$B$C" 从 "A$B" 搬到 "X" → "X$C"
        inline std::string Rebase(std::string_view name, std::string_view from, std::string_view to)
        {
            return std::string(to) + std::string(name.substr(from.size()));   // 各级只差大小写，长度相同
        }
    }
}

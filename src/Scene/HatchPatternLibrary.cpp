#include "Scene/HatchPatternLibrary.h"
#include "Import/CadTextFile.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace MiniCAD
{
    namespace
    {
        // 图案数据：内置的公制图案、天正自带的图案（同 CADReader 的做法编进程序，不用另放天正的 acad.pat）
#include "Scene/HatchPatternLibrary.inc"

        std::string Upper(std::string_view s)
        {
            std::string r(s);
            for (char& c : r)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return r;
        }

        std::string_view Trim(std::string_view s)
        {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
            return s;
        }

        // 逗号分隔的数字；任一字段不是数字返回 false
        bool ParseNumbers(std::string_view line, std::vector<double>& out)
        {
            out.clear();
            while (true)
            {
                const size_t comma = line.find(',');
                const std::string field(Trim(line.substr(0, comma)));
                if (field.empty())
                    return false;
                char* end = nullptr;
                const double v = std::strtod(field.c_str(), &end);    // strtod 接受 ".5"、"1e-3"
                if (end != field.c_str() + field.size())
                    return false;
                out.push_back(v);
                if (comma == std::string_view::npos)
                    return true;
                line.remove_prefix(comma + 1);
            }
        }
    }

    HatchPatternLibrary& HatchPatternLibrary::Instance()
    {
        static HatchPatternLibrary lib;
        return lib;
    }

    HatchPatternLibrary::HatchPatternLibrary()
    {
        LoadText(kBuiltinPatterns);
        for (std::string_view text : kTianzhengOwnPatterns)
        {
            LoadText(text);
            Parse(text, m_tianzheng);
        }
        for (std::string_view text : kTianzhengStandardPatterns)
            Parse(text, m_tianzheng);
    }

    bool HatchPatternLibrary::Parse(std::string_view text, std::vector<HatchPattern>& out, std::string* error)
    {
        bool ok = true;
        HatchPattern* cur = nullptr;
        std::vector<double> nums;
        size_t lineNo = 0;

        auto fail = [&](std::string_view line, const char* why)
        {
            if (error && ok)        // 只报告第一处错误
                *error = "第 " + std::to_string(lineNo) + " 行" + why + "：" + std::string(line);
            ok = false;
        };

        if (text.substr(0, 3) == "\xEF\xBB\xBF")      // UTF-8 BOM
            text.remove_prefix(3);

        while (!text.empty())
        {
            const size_t nl = text.find('\n');
            std::string_view line = text.substr(0, nl);
            text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
            ++lineNo;

            if (const size_t semi = line.find(';'); semi != std::string_view::npos)
                line = line.substr(0, semi);
            line = Trim(line);
            if (line.empty())
                continue;

            if (line.front() == '*')
            {
                const size_t comma = line.find(',');
                HatchPattern p;
                p.Name  = Upper(Trim(line.substr(1, comma == std::string_view::npos ? std::string_view::npos : comma - 1)));
                p.Solid = false;
                if (comma != std::string_view::npos)
                    p.Description = std::string(Trim(line.substr(comma + 1)));
                if (p.Name.empty()) { fail(line, "图案名为空"); cur = nullptr; continue; }
                out.push_back(std::move(p));
                cur = &out.back();
                continue;
            }

            if (!cur) { fail(line, "不属于任何图案"); continue; }
            if (!ParseNumbers(line, nums) || nums.size() < 5) { fail(line, "不是有效的线族定义"); continue; }

            HatchLineFamily f;
            f.AngleDeg = nums[0];
            f.OffsetX  = nums[1];
            f.OffsetY  = nums[2];
            f.DeltaX   = nums[3];
            f.Spacing  = nums[4];
            f.Dashes.assign(nums.begin() + 5, nums.end());
            cur->Families.push_back(std::move(f));
        }

        // SOLID 为实心；其余没有任何线族的图案无效，丢弃
        std::erase_if(out, [](HatchPattern& p)
        {
            if (p.Name == "SOLID") { p.Solid = true; p.Families.clear(); return false; }
            return p.Families.empty();
        });
        return ok;
    }

    size_t HatchPatternLibrary::LoadText(std::string_view text, std::string* error)
    {
        std::vector<HatchPattern> parsed;
        Parse(text, parsed, error);
        for (auto& p : parsed)
        {
            auto it = std::find_if(m_patterns.begin(), m_patterns.end(),
                                   [&](const HatchPattern& q) { return q.Name == p.Name; });
            if (it != m_patterns.end()) *it = std::move(p);
            else                        m_patterns.push_back(std::move(p));
        }
        return parsed.size();
    }

    size_t HatchPatternLibrary::LoadFile(const std::string& utf8Path, std::string* error)
    {
        const std::u8string u8(utf8Path.begin(), utf8Path.end());
        std::ifstream in(std::filesystem::path(u8), std::ios::binary);
        if (!in)
        {
            if (error) *error = "无法打开 " + utf8Path;
            return 0;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        return LoadText(CadTextToUtf8(ss.str()), error);     // 中文版 AutoCAD、天正的 .pat 是 GBK
    }

    const HatchPattern* HatchPatternLibrary::Find(std::string_view name) const
    {
        const std::string key = Upper(name);
        for (const auto& p : m_patterns)
            if (p.Name == key)
                return &p;
        return nullptr;
    }

    const HatchPattern* HatchPatternLibrary::FindTianzheng(std::string_view name) const
    {
        const std::string key = Upper(name);
        for (const auto& p : m_tianzheng)
            if (p.Name == key)
                return &p;
        return Find(name);
    }

    HatchPattern HatchPatternLibrary::Get(std::string_view name) const
    {
        if (const HatchPattern* p = Find(name))
            return *p;
        return HatchPattern::MakeSolid();
    }
}

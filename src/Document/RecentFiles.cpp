#include "Document/RecentFiles.h"
#include "Core/Log.h"
#include "Core/PathUtils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace MiniCAD
{
    std::string RecentFiles::Normalize(const std::string& path)
    {
        std::string out = path;
        for (char& c : out)
        {
            if (c == '/')
                c = '\\';
            else
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return out;
    }

    std::string RecentFiles::FileName(const std::string& path)
    {
        const size_t pos = path.find_last_of("/\\");
        return pos == std::string::npos ? path : path.substr(pos + 1);
    }

    void RecentFiles::Add(const std::string& path)
    {
        if (path.empty())
            return;
        Remove(path);
        m_items.insert(m_items.begin(), path);
        if (m_items.size() > kMaxItems)
            m_items.resize(kMaxItems);
    }

    void RecentFiles::Remove(const std::string& path)
    {
        const std::string key = Normalize(path);
        m_items.erase(std::remove_if(m_items.begin(), m_items.end(),
                                     [&key](const std::string& item) { return Normalize(item) == key; }),
                      m_items.end());
    }

    void RecentFiles::Clear()
    {
        m_items.clear();
    }

    std::string RecentFiles::DisplayName(size_t index) const
    {
        if (index >= m_items.size())
            return {};
        const std::string name = FileName(m_items[index]);

        // 同名的其他项：补上父目录名区分（"建筑平面图.mcad (一层)"）
        bool duplicated = false;
        for (size_t i = 0; i < m_items.size() && !duplicated; ++i)
            duplicated = i != index && FileName(m_items[i]) == name;
        if (!duplicated)
            return name;

        // 与 FileName 一样两种分隔符都认（Linux 的 std::filesystem 不把反斜杠当分隔符）
        const std::string& path = m_items[index];
        const size_t       end  = path.find_last_of("/\\");
        if (end == std::string::npos || end == 0)
            return name;
        const size_t      sep    = path.find_last_of("/\\", end - 1);
        const size_t      begin  = sep == std::string::npos ? 0 : sep + 1;
        const std::string parent = path.substr(begin, end - begin);
        if (parent.empty() || parent.back() == ':')     // "C:\a.mcad" 的上一级是盘符，不算目录名
            return name;
        return name + "  (" + parent + ")";
    }

    bool RecentFiles::Load(const std::string& utf8Path)
    {
        m_items.clear();
        if (utf8Path.empty())
            return false;

        std::ifstream in(Utf8ToPath(utf8Path), std::ios::binary);
        if (!in)
            return false;

        std::string line;
        std::error_code ec;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            if (!std::filesystem::exists(Utf8ToPath(line), ec))
                continue;                   // 文件已经被删掉 / 移走：不进菜单，也不报错
            if (std::find_if(m_items.begin(), m_items.end(), [&](const std::string& item)
                             { return Normalize(item) == Normalize(line); }) != m_items.end())
                continue;                   // 手改过的文件可能有重复项
            m_items.push_back(line);
            if (m_items.size() >= kMaxItems)
                break;
        }
        return true;
    }

    bool RecentFiles::Save(const std::string& utf8Path) const
    {
        if (utf8Path.empty())
            return false;

        const std::filesystem::path path = Utf8ToPath(utf8Path);
        std::error_code ec;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), ec);

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            const std::u8string u8 = path.u8string();
            LOG_WARN("无法保存最近打开列表：%s", std::string(u8.begin(), u8.end()).c_str());
            return false;
        }
        for (const std::string& item : m_items)
            out << item << '\n';
        return static_cast<bool>(out);
    }
}

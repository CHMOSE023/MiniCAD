#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace MiniCAD
{
    // 最近打开的文件（MRU）：只记路径，按最近使用排序，最多 kMaxItems 条。
    // 不依赖任何平台代码——存储位置由宿主给出（桌面版是 exe 所在目录下的
    // recent.txt，网页版不保存）；存储格式是 UTF-8 文本，每行一个路径。
    // Windows 的文件名不允许 0x00～0x1F，所以换行分隔是安全的。
    class RecentFiles
    {
    public:
        static constexpr size_t kMaxItems = 10;

        // 记录一次打开：已在列表里的项移到最前（比较时忽略大小写和 / \ 的差别），
        // 超过上限时丢掉最旧的。空路径忽略。
        void Add(const std::string& path);
        void Remove(const std::string& path);
        void Clear();

        const std::vector<std::string>& Items() const { return m_items; }
        bool   Empty() const { return m_items.empty(); }
        size_t Size()  const { return m_items.size(); }

        // 菜单显示名：文件名；与别的项重名时补上父目录名以便区分（同名同目录不会出现两条）
        std::string DisplayName(size_t index) const;

        // UTF-8 路径。Load 会丢掉空行与已经不存在的文件（文件不存在时返回 false，不算错误）；
        // Save 会自动创建父目录
        bool Load(const std::string& utf8Path);
        bool Save(const std::string& utf8Path) const;

        // 大小写与斜杠归一化，用于比较两条路径是否指向同一个文件
        static std::string Normalize(const std::string& path);
        static std::string FileName(const std::string& path);

    private:
        std::vector<std::string> m_items;
    };
}

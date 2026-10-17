// ── 最近打开列表测试：排序 / 去重 / 上限 / 显示名 / 存取与失效项过滤 ──────────────────
#include "Document/RecentFiles.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace MiniCAD;

namespace
{
    int g_failures = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok)
            ++g_failures;
    }

    // 可写的临时目录：先用系统的 %TEMP%，某些环境里它不可写（沙箱 / 杀软 / 权限策略），
    // 这时退回当前工作目录。探测方式是实际写一个文件，不能只看目录是否存在
    std::filesystem::path TempDir()
    {
        std::error_code ec;
        std::vector<std::filesystem::path> candidates;
        candidates.push_back(std::filesystem::temp_directory_path(ec) / L"minicad_recent_test");
        candidates.push_back(std::filesystem::current_path(ec) / L"minicad_recent_test");

        for (const std::filesystem::path& dir : candidates)
        {
            if (dir.empty())
                continue;
            std::filesystem::remove_all(dir, ec);
            std::filesystem::create_directories(dir, ec);
            if (ec)
                continue;
            const std::filesystem::path probe = dir / L".write_probe";
            std::ofstream out(probe, std::ios::binary | std::ios::trunc);
            if (!out)
                continue;
            out << "x";
            out.close();
            std::filesystem::remove(probe, ec);
            if (!ec)
                return dir;
        }
        return {};
    }

    std::string Utf8(const std::filesystem::path& p)
    {
        const std::u8string u8 = p.u8string();
        return std::string(u8.begin(), u8.end());
    }

    // 触及一个真实文件：Load 会滤掉不存在的路径，测试里得先造出来
    void Touch(const std::filesystem::path& p)
    {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        std::ofstream(p, std::ios::binary) << "x";
    }
}

int RunRecentFilesTests()
{
    g_failures = 0;

    // ── 排序与去重 ─────────────────────────────────────────────────────
    {
        RecentFiles r;
        Check(r.Empty() && r.Size() == 0, "初始为空");

        r.Add("C:\\a\\one.mcad");
        r.Add("C:\\a\\two.mcad");
        r.Add("C:\\a\\three.mcad");
        Check(r.Items()[0] == "C:\\a\\three.mcad", "最后加入的排在最前");
        Check(r.Size() == 3, "三条记录");

        // 重复项移到最前，不新增（大小写与斜杠不敏感）
        r.Add("c:/A/ONE.mcad");
        Check(r.Size() == 3, "重复路径不新增条目");
        Check(r.Items()[0] == "c:/A/ONE.mcad", "重复路径移到最前（保留这次写入的大小写）");
        Check(r.Items()[1] == "C:\\a\\three.mcad" && r.Items()[2] == "C:\\a\\two.mcad", "其余顺序不变");

        // 空路径忽略
        r.Add("");
        Check(r.Size() == 3, "空路径被忽略");
    }

    // ── 上限 ───────────────────────────────────────────────────────────
    {
        RecentFiles r;
        for (int i = 0; i < 15; ++i)
            r.Add("C:\\d\\f" + std::to_string(i) + ".mcad");
        Check(r.Size() == RecentFiles::kMaxItems, "超过上限时截断到 10 条");
        Check(r.Items()[0] == "C:\\d\\f14.mcad", "最新的仍在最前");
        Check(r.Items().back() == "C:\\d\\f5.mcad", "最旧的被丢掉");
    }

    // ── 显示名：重名时补父目录 ────────────────────────────────────────
    {
        RecentFiles r;
        r.Add("D:\\proj\\一层\\平面图.mcad");
        r.Add("E:\\proj\\二层\\平面图.mcad");
        r.Add("D:\\proj\\详图.dwg");

        const auto& items = r.Items();
        // 加入顺序：一层、二层、详图 → 列表倒序：详图、二层、一层
        Check(items.size() == 3, "三条记录");
        Check(r.DisplayName(0) == "详图.dwg", "不重名时只显示文件名");
        Check(r.DisplayName(1) == "平面图.mcad  (二层)", "重名项补上父目录名");
        Check(r.DisplayName(2) == "平面图.mcad  (一层)", "另一条重名项显示另一个父目录");
        Check(r.DisplayName(99).empty(), "越界返回空串");
    }

    // ── 文件名与归一化 ─────────────────────────────────────────────────
    {
        Check(RecentFiles::FileName("C:\\a\\b\\c.mcad") == "c.mcad", "反斜杠路径取文件名");
        Check(RecentFiles::FileName("C:/a/b/c.mcad") == "c.mcad", "正斜杠路径取文件名");
        Check(RecentFiles::FileName("plain.dwg") == "plain.dwg", "没有目录时原样返回");
        Check(RecentFiles::Normalize("C:/A\\B.MCAD") == "c:\\a\\b.mcad", "归一化：小写 + 统一反斜杠");
    }

    // ── Remove / Clear ─────────────────────────────────────────────────
    {
        RecentFiles r;
        r.Add("C:\\a\\one.mcad");
        r.Add("C:\\a\\two.mcad");
        r.Remove("c:/A/ONE.mcad");
        Check(r.Size() == 1 && r.Items()[0] == "C:\\a\\two.mcad", "Remove 忽略大小写与斜杠差异");
        r.Clear();
        Check(r.Empty(), "Clear 清空");
    }

    // ── 存盘 / 读取：过滤已被删掉的文件 ────────────────────────────────
    {
        const std::filesystem::path dir = TempDir();
        if (dir.empty())
        {
            // 磁盘上没有任何可写目录：跳过这一组（内存逻辑上面已经全部覆盖）
            std::printf("[跳过] 没有可写的临时目录，跳过存盘 / 读取测试\n");
            return g_failures;
        }
        const std::filesystem::path kept = dir / L"kept.mcad";
        const std::filesystem::path gone = dir / L"gone.mcad";
        const std::filesystem::path store = dir / L"recent.txt";
        Touch(kept);

        RecentFiles save;
        save.Add(Utf8(kept));
        save.Add(Utf8(gone));               // 只记录，不建文件
        Check(save.Save(Utf8(store)), "Save 写入成功");
        Check(std::filesystem::exists(store), "文件确实生成");
        Check(save.Size() == 2, "内存里仍是两条");

        RecentFiles load;
        const bool loaded = load.Load(Utf8(store));
        Check(loaded, "Load 读回成功");
        // 断言失败时不能直接索引（Load 失败会让列表为空）
        Check(loaded && load.Size() == 1, "不存在的路径在读取时被丢掉");
        Check(loaded && !load.Empty() && load.Items()[0] == Utf8(kept), "留下的是存在的那个文件");
        if (!loaded)
            return g_failures;

        // 文件里的怪行：空行、\r、重复项
        {
            std::ofstream out(store, std::ios::binary | std::ios::trunc);
            out << "\n" << Utf8(kept) << "\r\n" << Utf8(kept) << "\n\n";
        }
        RecentFiles again;
        Check(again.Load(Utf8(store)), "含空行与重复项的列表也能读");
        Check(again.Size() == 1, "空行与重复项被丢掉");

        // 路径为空 / 文件不存在：不算错误，列表保持清空
        RecentFiles none;
        none.Add("C:\\a\\x.mcad");
        Check(!none.Load(""), "空路径 Load 返回 false");
        Check(none.Empty(), "Load 失败时列表被清空（不会留着上一次的内容）");
        Check(!none.Load(Utf8(dir / L"no_such_file.txt")), "文件不存在时 Load 返回 false");

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    return g_failures;
}

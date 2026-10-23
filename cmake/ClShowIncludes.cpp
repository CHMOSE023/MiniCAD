// cl 的编译启动器（Ninja + MSVC，见根 CMakeLists.txt）：把 /showIncludes 的本地化前缀统一改写成 ASCII 的
// "Note: including file:"，其余输出原样转发，退出码照传。
//
// 用法：ClShowIncludes.exe --prefix=<UTF-8 前缀的十六进制> <编译器> <参数...>
//
// 原因：cl 按构建时控制台的代码页（GBK / UTF-8）输出"注意: 包含文件:"，而 ninja 只认 rules.ninja 里配置时写下的那一种字节，
// 对不上时 ninja 记下的头文件依赖为空，改了头文件也不重编。这里同时认控制台代码页、UTF-8、ANSI、OEM 四种编码的前缀。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

namespace
{
    const char kAsciiPrefix[] = "Note: including file:";

    std::string FromHex(const wchar_t* s)
    {
        auto nibble = [](wchar_t c) -> int
        {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            return -1;
        };
        std::string out;
        for (; s[0] && s[1]; s += 2)
        {
            const int hi = nibble(s[0]), lo = nibble(s[1]);
            if (hi < 0 || lo < 0)
                break;
            out.push_back(static_cast<char>(hi * 16 + lo));
        }
        return out;
    }

    // UTF-8 → 指定代码页
    std::string Encode(const std::string& utf8, UINT cp)
    {
        const int wn = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring w(wn, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), wn);
        const int n = WideCharToMultiByte(cp, 0, w.data(), wn, nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        WideCharToMultiByte(cp, 0, w.data(), wn, out.data(), n, nullptr, nullptr);
        return out;
    }

    // 跳过命令行里的一个参数（可带引号）及其后的空白
    const wchar_t* SkipArg(const wchar_t* p)
    {
        bool quoted = false;
        for (; *p && (quoted || (*p != L' ' && *p != L'\t')); ++p)
            if (*p == L'"')
                quoted = !quoted;
        while (*p == L' ' || *p == L'\t')
            ++p;
        return p;
    }

    void WriteAll(HANDLE h, const std::string& s)
    {
        DWORD written = 0;
        for (size_t off = 0; off < s.size(); off += written)
            if (!WriteFile(h, s.data() + off, static_cast<DWORD>(s.size() - off), &written, nullptr) || written == 0)
                return;
    }
}

int wmain()
{
    const wchar_t* p = SkipArg(GetCommandLineW());   // 本程序
    std::vector<std::string> prefixes;
    if (wcsncmp(p, L"--prefix=", 9) == 0)
    {
        const std::string utf8 = FromHex(p + 9);
        for (UINT cp : { GetConsoleOutputCP(), static_cast<UINT>(CP_UTF8), GetACP(), GetOEMCP() })
        {
            if (cp == 0)
                continue;
            std::string enc = Encode(utf8, cp);
            if (!enc.empty() && std::find(prefixes.begin(), prefixes.end(), enc) == prefixes.end())
                prefixes.push_back(std::move(enc));
        }
        p = SkipArg(p);
    }
    std::wstring cmdLine = p;
    if (cmdLine.empty())
        return 1;

    // cl 的临时文件放到本程序旁的 cltemp（构建目录里）。有的机器上从项目目录启动的进程写不了系统 TEMP，
    // cl 带 /Z7 / /Zi 时就会报 D8050“未能将命令行放入调试记录中”
    {
        wchar_t self[MAX_PATH] = L"";
        const DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring dir(self, n);
        if (const size_t slash = dir.find_last_of(L"\\/"); n > 0 && n < MAX_PATH && slash != std::wstring::npos)
        {
            dir.resize(slash + 1);
            dir += L"cltemp";
            CreateDirectoryW(dir.c_str(), nullptr);
            SetEnvironmentVariableW(L"TMP", dir.c_str());
            SetEnvironmentVariableW(L"TEMP", dir.c_str());
        }
    }

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0))
        return 1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = wr;
    si.hStdError  = wr;   // ninja 本来就把两者合在一起
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi))
    {
        WriteAll(GetStdHandle(STD_ERROR_HANDLE), "ClShowIncludes: cannot start the compiler\n");
        return 1;
    }
    CloseHandle(wr);

    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    auto emitLine = [&](const std::string& line)
    {
        for (const auto& prefix : prefixes)
            if (line.compare(0, prefix.size(), prefix) == 0)
            {
                WriteAll(out, kAsciiPrefix + line.substr(prefix.size()));
                return;
            }
        WriteAll(out, line);
    };

    std::string pending;
    char buf[8192];
    DWORD n = 0;
    while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n > 0)
    {
        pending.append(buf, n);
        size_t start = 0;
        for (size_t nl; (nl = pending.find('\n', start)) != std::string::npos; start = nl + 1)
            emitLine(pending.substr(start, nl + 1 - start));
        pending.erase(0, start);
    }
    if (!pending.empty())
        emitLine(pending);
    CloseHandle(rd);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(code);
}

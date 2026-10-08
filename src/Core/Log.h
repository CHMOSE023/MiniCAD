#pragma once
#include <cstdio>
#include <cstdarg>
#include <cstring>

// Forward-declare only what we need — avoids pulling in <windows.h>.
#ifdef _WIN32
extern "C" {
    __declspec(dllimport) void* __stdcall GetStdHandle(unsigned long);
    __declspec(dllimport) int   __stdcall GetConsoleMode(void*, unsigned long*);
    __declspec(dllimport) int   __stdcall SetConsoleMode(void*, unsigned long);
    __declspec(dllimport) void  __stdcall OutputDebugStringA(const char*);
}
#endif

namespace MiniCAD {

// ── Log levels ────────────────────────────────────────────────────────────
enum class LogLevel : int { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Off = 5 };

// Global minimum level — change at runtime to increase/reduce verbosity.
inline LogLevel g_logLevel =
#ifdef NDEBUG
    LogLevel::Info;
#else
    LogLevel::Debug;
#endif

// ── Internal implementation ───────────────────────────────────────────────
namespace Detail {

inline const char* BaseName(const char* path)
{
    const char* p = path;
    for (const char* c = path; *c; ++c)
        if (*c == '/' || *c == '\\') p = c + 1;
    return p;
}

#ifdef _WIN32
inline void EnsureAnsiColors()
{
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    // STD_ERROR_HANDLE = (DWORD)-12 = 0xFFFFFFF4
    void*         h    = GetStdHandle(0xFFFFFFF4u);
    unsigned long mode = 0;
    if (GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | 0x0004u); // ENABLE_VIRTUAL_TERMINAL_PROCESSING
}
#endif

#ifdef __GNUC__
__attribute__((format(printf, 4, 5)))
#endif
inline void LogWrite(LogLevel level, const char* file, int line, const char* fmt, ...)
{
    if (level < g_logLevel) return;

    // Label text
    static constexpr const char* kLabels[] = { "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR" };

    // ANSI colors per level
    //                                             trace    debug    info     warn     error
    static constexpr const char* kLabelColor[] = { "\033[90m", "\033[96m", "\033[92m", "\033[93m", "\033[91m" };
    static constexpr const char* kMsgColor[]   = { "\033[90m", nullptr,    nullptr,    "\033[93m", "\033[91m" };
    static constexpr const char* kDim          = "\033[2m";
    static constexpr const char* kReset        = "\033[0m";

    const int idx = static_cast<int>(level);

    // Format user message
    char msg[1800];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    const bool hasLoc = file[0] != '\0';

    char loc[40] = "";
    if (hasLoc)
        std::snprintf(loc, sizeof(loc), "%s:%d", BaseName(file), line);

#ifdef _WIN32
    EnsureAnsiColors();

    char plain[2048];
    if (hasLoc)
        std::snprintf(plain, sizeof(plain), "[%s] %-22s | %s\n", kLabels[idx], loc, msg);
    else
        std::snprintf(plain, sizeof(plain), "[%s] %s\n", kLabels[idx], msg);
    OutputDebugStringA(plain);
#endif

    // Replace embedded newlines with newline + indent so wrapped lines stay aligned.
    const int indentWidth = hasLoc ? 33 : 8; // "[LEVEL] " (8) + loc (22) + " | " (3)
    char indentStr[34] = {};
    std::memset(indentStr, ' ', indentWidth);
    char fmtMsg[1900];
    {
        const char* src = msg;
        char* dst = fmtMsg;
        char* end = fmtMsg + sizeof(fmtMsg) - 1;
        while (*src && dst < end) {
            if (*src == '\n' && *(src + 1) != '\0') {
                const int rem = static_cast<int>(end - dst);
                int n = std::snprintf(dst, rem, "\n%s", indentStr);
                if (n > 0) dst += n;
                ++src;
            } else {
                *dst++ = *src++;
            }
        }
        *dst = '\0';
    }

    char buf[2200];
    const char* mc = kMsgColor[idx];
    if (hasLoc) {
        if (mc) {
            std::snprintf(buf, sizeof(buf),
                "%s[%s]%s %s%-22s%s | %s%s%s\n",
                kLabelColor[idx], kLabels[idx], kReset,
                kDim, loc, kReset,
                mc, fmtMsg, kReset);
        } else {
            std::snprintf(buf, sizeof(buf),
                "%s[%s]%s %s%-22s%s | %s\n",
                kLabelColor[idx], kLabels[idx], kReset,
                kDim, loc, kReset,
                fmtMsg);
        }
    } else {
        if (mc) {
            std::snprintf(buf, sizeof(buf),
                "%s[%s]%s %s%s%s\n",
                kLabelColor[idx], kLabels[idx], kReset,
                mc, fmtMsg, kReset);
        } else {
            std::snprintf(buf, sizeof(buf),
                "%s[%s]%s %s\n",
                kLabelColor[idx], kLabels[idx], kReset,
                fmtMsg);
        }
    }
    fputs(buf, stderr);
}

} // namespace Detail
} // namespace MiniCAD

// ── Public macros ─────────────────────────────────────────────────────────
#ifdef NDEBUG
#define LOG_TRACE(fmt, ...) ::MiniCAD::Detail::LogWrite(::MiniCAD::LogLevel::Trace, "", 0, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) ::MiniCAD::Detail::LogWrite(::MiniCAD::LogLevel::Debug, "", 0, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  ::MiniCAD::Detail::LogWrite(::MiniCAD::LogLevel::Info,  "", 0, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  ::MiniCAD::Detail::LogWrite(::MiniCAD::LogLevel::Warn,  "", 0, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) ::MiniCAD::Detail::LogWrite(::MiniCAD::LogLevel::Error, "", 0, fmt, ##__VA_ARGS__)
#else
#define LOG_TRACE(fmt, ...) ::MiniCAD::Detail::LogWrite(::MiniCAD::LogLevel::Trace, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) ::MiniCAD::Detail::LogWrite(::MiniCAD::LogLevel::Debug, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  ::MiniCAD::Detail::LogWrite(::MiniCAD::LogLevel::Info,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  ::MiniCAD::Detail::LogWrite(::MiniCAD::LogLevel::Warn,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) ::MiniCAD::Detail::LogWrite(::MiniCAD::LogLevel::Error, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#endif

// Runtime level control
#define LOG_SET_LEVEL(lvl)  (::MiniCAD::g_logLevel = (lvl))

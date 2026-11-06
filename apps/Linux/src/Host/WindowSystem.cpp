#include "Host/WindowSystem.h"
#include "Core/Log.h"
#include <cstdlib>

namespace MiniCAD
{
    namespace
    {
        int HexValue(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        std::unique_ptr<WindowSystem> CreateByName(const std::string& name)
        {
#ifdef MINICAD_LINUX_WAYLAND
            if (name == "wayland")
                return CreateWaylandWindowSystem();
#endif
#ifdef MINICAD_LINUX_X11
            if (name == "x11")
                return CreateX11WindowSystem();
#endif
            return nullptr;
        }
    }

    std::unique_ptr<WindowSystem> CreateWindowSystem(const std::string& preferred)
    {
        const char* wayland = std::getenv("WAYLAND_DISPLAY");
        std::vector<std::string> order;
        if (preferred == "x11" || preferred == "wayland")
            order.push_back(preferred);
        else if (!preferred.empty())
            LOG_WARN_TR("没有名为 {0} 的窗口系统，按默认顺序连接", preferred.c_str());
        for (const char* name : { (wayland && *wayland) ? "wayland" : "x11", (wayland && *wayland) ? "x11" : "wayland" })
            if (order.empty() || order.front() != name)
                order.push_back(name);

        for (const std::string& name : order)
            if (auto ws = CreateByName(name))
                return ws;
        return nullptr;
    }

    std::string FileUriToPath(const std::string& uri)
    {
        constexpr std::string_view kScheme = "file://";
        if (uri.compare(0, kScheme.size(), kScheme) != 0)
            return uri.empty() || uri[0] != '/' ? std::string() : uri;
        const size_t slash = uri.find('/', kScheme.size());
        if (slash == std::string::npos)
            return {};
        std::string path;
        for (size_t i = slash; i < uri.size(); ++i)
        {
            const int hi = uri[i] == '%' && i + 2 < uri.size() ? HexValue(uri[i + 1]) : -1;
            const int lo = hi >= 0 ? HexValue(uri[i + 2]) : -1;
            if (lo >= 0)
            {
                path.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
            }
            else
                path.push_back(uri[i]);
        }
        return path;
    }

    std::vector<std::string> ParseUriList(const std::string& text)
    {
        std::vector<std::string> paths;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(pos, end - pos);
            pos = end + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == '\0'))
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            if (std::string path = FileUriToPath(line); !path.empty())
                paths.push_back(std::move(path));
        }
        return paths;
    }

    const char* const* CursorNames(MiniGUI::CursorShape shape)
    {
        using MiniGUI::CursorShape;
        static const char* const kArrow[]  = { "default", "left_ptr", nullptr };
        static const char* const kText[]   = { "text", "xterm", nullptr };
        static const char* const kHand[]   = { "pointer", "hand2", "hand1", nullptr };
        static const char* const kWE[]     = { "ew-resize", "sb_h_double_arrow", "h_double_arrow", nullptr };
        static const char* const kNS[]     = { "ns-resize", "sb_v_double_arrow", "v_double_arrow", nullptr };
        static const char* const kNWSE[]   = { "nwse-resize", "bottom_right_corner", "size_fdiag", nullptr };
        static const char* const kNESW[]   = { "nesw-resize", "bottom_left_corner", "size_bdiag", nullptr };
        static const char* const kMove[]   = { "move", "fleur", "all-scroll", nullptr };
        static const char* const kGrab[]   = { "grab", "openhand", "fleur", nullptr };
        static const char* const kNo[]     = { "not-allowed", "crossed_circle", "X_cursor", nullptr };
        switch (shape)
        {
        case CursorShape::IBeam:      return kText;
        case CursorShape::Hand:       return kHand;
        case CursorShape::SizeWE:     return kWE;
        case CursorShape::SizeNS:     return kNS;
        case CursorShape::SizeNWSE:   return kNWSE;
        case CursorShape::SizeNESW:   return kNESW;
        case CursorShape::SizeAll:    return kMove;
        case CursorShape::Grab:       return kGrab;
        case CursorShape::NotAllowed: return kNo;
        default:                      return kArrow;
        }
    }
}

#include "ErrorReporter.h"
#include <iostream>
namespace MiniCAD
{
    static std::function<void(const std::string&)> s_handler;

    void SetErrorHandler(std::function<void(const std::string&)> handler)
    {
        s_handler = std::move(handler);
    }

    void ReportError(const std::string& msg)
    {
        std::clog << "[ERROR] " << msg << "\n";
        if (s_handler)
            s_handler(msg);
    }
}

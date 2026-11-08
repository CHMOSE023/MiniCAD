#pragma once
#include "Import/ProxyGraphics.h"

namespace MiniCAD::Tch
{
    using TchProxyGraphics = Proxy::ProxyGraphics;
    using ProxyGraphicsContext = Proxy::ProxyGraphicsContext;

    inline bool DecodeProxyGraphics(const MiniDWG::UnknownEntity& source, TchProxyGraphics& output,
                                    std::string* diagnostic = nullptr, const ProxyGraphicsContext* context = nullptr)
    {
        return Proxy::Decode(source, output, diagnostic, context);
    }
}

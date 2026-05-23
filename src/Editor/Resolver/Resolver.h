#pragma once

#include "Editor/Input/InputContext.h"
#include "Editor/Resolver/ResolvedInput.h"
#include "Core/Math/Point3.hpp"

namespace MiniCAD
{
    class Resolver
    {
    public:
        ResolvedInput GetResolve(const InputContext& ctx);

    private:
        Math::Point3 ScreenToWorld(const InputContext& ctx) const;
        bool         ShouldSnap   (const InputContext& ctx) const;
    };
}

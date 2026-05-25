#pragma once  
#include "Core/Math/Point3.hpp"  

namespace MiniCAD
{
    class EditorContext;
    /// <summary>
    /// 输入解析器
    /// </summary>
    class InputResolver
    {
    public:
        void Resolve(EditorContext& ctx);

    private:
        Math::Point3 ScreenToWorld(const EditorContext& ctx) const;
        bool         ShouldSnap   (const EditorContext& ctx) const;
    };
}

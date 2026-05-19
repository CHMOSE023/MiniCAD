#pragma once
#include "../Math/Point3.hpp"
#include "../Math/Color4.hpp"
#include <string>
namespace MiniCAD
{
    class IDrawSink
    {
    public:
        static constexpr Math::Color4 kHoverColor     = { 0, 0.5, 0.8, 0.9 };
        static constexpr Math::Color4 kSelectionColor = { 0, 0.3, 0.8, 0.9 };

        virtual ~IDrawSink() = default;
        virtual void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4& color, bool isOverlay) = 0;

        // pos: baseline-left insertion point（世界坐标）
        // height: 字高（世界单位）
        // rotation: 旋转角（弧度）
        virtual void EmitText(const Math::Point3& pos, const std::string& utf8Text,
                              float height, float rotation,
                              const Math::Color4& color) {}
    };
}

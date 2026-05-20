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
         
        // 纹理字形（Web / ImGui atlas 路径）
        virtual void EmitText(const Math::Point3& pos, const std::string& utf8Text,
                              float height, float rotation,
                              const Math::Color4& color) {}

        // 矢量多行文字（SHX / TTF 路径）：原始数据透传，渲染逻辑由 DrawContext 实现
        // styleId: FontStyle ID（由 DocumentManager 管理）
        // boxWidth: 自动换行宽度，0 = 不限宽
        virtual void EmitMText(const Math::Point3& pos,
                               const std::string&  utf8Text,
                               uint32_t            styleId,
                               double              height,
                               double              rotation,
                               double              boxWidth,
                               const Math::Color4& color) {}
    };
}

#pragma once
#include "../Math/Point3.hpp"
#include "../Math/Color4.hpp"
#include <array>
#include <string>
#include <vector>
#include <cstdint>
namespace MiniCAD
{
    using LayerID = uint32_t;
    struct EntityAttr;

    class IDrawSink
    {
    public:
        static constexpr Math::Color4 kHoverColor = { 0, 0.5, 0.8, 0.9 };
        static constexpr Math::Color4 kSelectionColor = { 0, 0.3, 0.8, 0.9 };

        virtual ~IDrawSink() = default;

        // 实体级绘制上下文:Editor 在调用 entity.Draw 前传入该实体的属性,
        // 供 sink 解析 ByLayer 线型/线宽;传 nullptr 清除。默认实现忽略。
        virtual void BeginEntity(const EntityAttr* attr) {}

        virtual Math::Color4 GetLayerColor(LayerID layerId) const { return Math::Color4::White(); }
        virtual void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4& color, bool isOverlay) = 0;

        // 填充三角形（线宽 > 1 的实体由三角形拼接而成，走 TRIANGLELIST 场景缓冲）
        virtual void FillTriangle(const Math::Point3& a, const Math::Point3& b, const Math::Point3& c, const Math::Color4& color) {}

        // 区域覆盖（WIPEOUT）：声明一个闭合多边形，之前绘制的内容在其内部不可见。
        // 只有生成场景顶点流的 sink 需要处理，默认忽略。
        virtual void Wipe(const std::vector<Math::Point3>& polygon) {}

        // 光栅图像：corners 依次为左下、右下、右上、左上（世界坐标）。
        // 返回 false 表示 sink 不显示图像或图像不可用，调用方改画占位框。
        virtual bool EmitImage(const std::string& path, const std::array<Math::Point3, 4>& corners) { return false; }

        // 纹理字形（Web 字体图集路径）
        virtual void EmitText(const Math::Point3& pos, const std::string& utf8Text, float height, float rotation, const Math::Color4& color) {}

        // 矢量多行文字（SHX / TTF 路径）：原始数据透传，渲染逻辑由 DrawContext 实现
        // styleId: FontStyle ID（由 DocumentManager 管理）
        // boxWidth: 自动换行宽度，0 = 不限宽
        virtual void EmitMText(const Math::Point3& pos, const std::string& utf8Text, uint32_t styleId, double height, double rotation, double boxWidth, const Math::Color4& color) {}
    };
} 
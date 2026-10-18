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
        // 图层是否显示（关闭或冻结为 false）：块参照展开时据此跳过块内隐藏图层上的子实体。默认都显示
        virtual bool IsLayerVisible(LayerID layerId) const { return true; }
        virtual void DrawLine(const Math::Point3& a, const Math::Point3& b, const Math::Color4& color, bool isOverlay) = 0;

        // 按线型铺设有宽度的线段（width 为模型单位）。粗多段线无法整体填充成一条带宽度、
        // 又带线型的路径：填充几何不经过线型解析，虚线会被画成实线。所以虚线粗线逐段走这里，
        // 由 sink 解析线型、挤出加宽几何。默认实现忽略线宽，等价于 DrawLine（保持旧行为）。
        virtual void DrawStyledLine(const Math::Point3& a, const Math::Point3& b,
                                    const Math::Color4& color, double width)
        {
            (void)width;
            DrawLine(a, b, color, false);
        }

        // 当前实体的生效线型是否为虚线。有宽度的实体据此选择：
        // 连续线走整体填充（保留斜接尖角），虚线则逐段调用 DrawStyledLine。
        // 默认 false —— 未知线型按连续实线处理。
        virtual bool IsCurrentLinetypeDashed() const { return false; }

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

        // 单行文字（TEXT）按对齐点输出：hAlign 0 左 / 1 中 / 2 右（按实际字宽），vAlign 0 基线 / 1 底 / 2 中 / 3 上。
        // widthFactor > 0 时代替样式的宽度因子（TEXT 自身的组码 41），0 = 用样式的。
        // 默认当作左对齐基线交给 EmitMText（只关心样式等的 sink 不必实现）
        virtual void EmitAlignedText(const Math::Point3& pos, const std::string& utf8Text, uint32_t styleId, double height, double rotation,
                                     int hAlign, int vAlign, double widthFactor, const Math::Color4& color)
        {
            (void)hAlign; (void)vAlign; (void)widthFactor;
            EmitMText(pos, utf8Text, styleId, height, rotation, 0.0, color);
        }
    };
} 
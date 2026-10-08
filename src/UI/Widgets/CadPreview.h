#pragma once
#include "Style/ColorRef.hpp"
#include "Core/Types/Rect.hpp"
#include <string>
#include <vector>

namespace MiniGUI
{
    class DrawList;

    // 线型定义：pattern 与 AutoCAD .lin 文件相同的记法——正数为划线长度，负数为空白，0 为点。
    // 单位是预览时的逻辑像素（不是图纸单位）
    struct LinetypeDef
    {
        std::string        name;
        std::string        description;
        std::vector<float> pattern;      // 空表示实线
    };

    // 常用线型：Continuous、Dashed、Hidden、Center、Phantom、Dot、DashDot、Border、Divide
    const std::vector<LinetypeDef>& StandardLinetypes();

    // AutoCAD 标准线宽（毫米）：0.00、0.05 … 2.11
    const std::vector<float>& StandardLineweights();

    // 在 r 的竖直中线上画线型样例（按图案重复，末尾截断）
    void DrawLinetypeSample(DrawList& dl, const Rect& r, const std::vector<float>& pattern, ColorRef color, float thickness = 1.2f);

    // 画线宽样例：按屏幕 96 DPI 把毫米换算成像素，至少 1 像素
    void DrawLineweightSample(DrawList& dl, const Rect& r, float millimeters, ColorRef color);

    // 颜色色块（图层颜色等）：填充 + 随主题变化的淡描边，白色色块在浅色底上也看得清
    void DrawColorSwatch(DrawList& dl, const Rect& r, Color32 color, float rounding = 2.0f);

    // 图层开关小图标：开 = 亮黄色灯泡，关 = 暗色
    void DrawLayerOnIcon(DrawList& dl, const Rect& r, bool on);
    // 冻结小图标：冻结 = 蓝色雪花，解冻 = 太阳
    void DrawLayerFreezeIcon(DrawList& dl, const Rect& r, bool frozen);
    // 锁定小图标
    void DrawLayerLockIcon(DrawList& dl, const Rect& r, bool locked);
}

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

    // 复杂线型预览里的一个符号段（AutoCAD 组码 9 = 文字、75 = 形）。
    // 预览不加载真实 SHX 形字体（MiniGUI 层不依赖文字引擎），只画占位标记：
    // 文字段画小方块、形段画小菱形，位置落在对应 pattern 下标之后。
    struct LinetypeSampleSymbol
    {
        size_t patternIndex = 0;   // 符号铺在 Pattern 的这个下标之后
        bool   isShape      = false;  // true = 形段（组码 75），false = 文字段（组码 9）
    };

    // 常用线型：Continuous、Dashed、Hidden、Center、Phantom、Dot、DashDot、Border、Divide
    const std::vector<LinetypeDef>& StandardLinetypes();

    // AutoCAD 标准线宽（毫米）：0.00、0.05 … 2.11
    const std::vector<float>& StandardLineweights();

    // 在 r 的竖直中线上画线型样例（按图案重复，末尾截断）
    void DrawLinetypeSample(DrawList& dl, const Rect& r, const std::vector<float>& pattern, ColorRef color, float thickness = 1.2f);

    // 同上，另外把复杂线型的文字/形段画成占位标记。
    // symbols 为空时行为与上一个重载完全一致；patternIndex 越界的项被忽略。
    void DrawLinetypeSample(DrawList& dl, const Rect& r, const std::vector<float>& pattern,
                            const std::vector<LinetypeSampleSymbol>& symbols,
                            ColorRef color, float thickness = 1.2f);

    // 在 at 处画一个符号段占位标记：形段（isShape）画小菱形，文字段画小方框。
    // 界面层拿不到真实 SHX 形字体，预览里用它示意「这里有一段文字/一个形」。
    void DrawLinetypeSampleSymbol(DrawList& dl, const LinetypeSampleSymbol& sy, Vec2 at,
                                  ColorRef color, float thickness = 1.2f);

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

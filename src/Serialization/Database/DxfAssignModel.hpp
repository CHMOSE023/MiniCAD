#pragma once
#include "Database/DxfAssign.hpp"
#include "Database/Generated/Model.g.h"

// 生成模型中嵌套结构的 Assign（对应 ACadSharp DxfProperty.SetValue 中的特例）
namespace MiniDWG
{
    // 打印边距：40/41/42/43 → 左/下/右/上
    inline void Assign(PaperMargin& m, int code, const DxfValue& v)
    {
        switch (code)
        {
        case 40: m.Left = v.AsDouble(); break;
        case 41: m.Bottom = v.AsDouble(); break;
        case 42: m.Right = v.AsDouble(); break;
        case 43: m.Top = v.AsDouble(); break;
        default: break;
        }
    }

    inline DxfValue Extract(const PaperMargin& m, int code)
    {
        switch (code)
        {
        case 41: return DxfValue(m.Bottom);
        case 42: return DxfValue(m.Right);
        case 43: return DxfValue(m.Top);
        default: return DxfValue(m.Left);
        }
    }
}

#pragma once
#include "Core/Math/Point3.hpp"
#include "Core/Math/Color4.hpp"
#include "Scene/LineTypeTable.h"

namespace MiniCAD
{
    // 延迟铺设的长虚线段（一段几百上千个周期的虚线，见 DrawContext::EmitStyled）：
    // 细分实体时不铺出来，只记下端点、颜色、线型与比例；Editor 按当前缩放把它铺进动态流
    //（一个周期不足 4px 时画成一段实线），滚轮缩放时从这个小列表直接重铺，所在实体不重新细分
    struct DeferredDash
    {
        Math::Point3 a, b;
        Math::Color4 color;
        LineTypeID   lineType;   // 已解析（ByLayer / ByBlock 已换成实际线型）
        double       scale;      // 实体线型比例 × 全局 $LTSCALE
        double       phase = 0.0; // 起点落在线型图案里的位置（世界单位，已含比例）：被视口矩形裁掉前半段后，后半段仍按原相位铺
    };
}

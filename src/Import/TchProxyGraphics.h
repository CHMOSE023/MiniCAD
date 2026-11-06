#pragma once
// 天正（TCH_*）自定义实体的解码入口。
//
// 代理图形本身与厂商无关，通用解码器在 `Import/ProxyGraphics.h`（命名空间
// MiniCAD::Proxy）。本文件只保留天正这一侧的入口，做两件事：
//   1. 把通用的 ProxyGraphics 结果转成 TchProxyGraphics（历史类型名，供调用方沿用）；
//   2. 记录天正特有的取值约定。
//
// 天正私有主数据流（墙体、门窗、楼梯、标注等语义）不在这里，见同目录的
// `TchWall.h`、`TchOpening.h`、`TchShape.h` 等。
//
// 历史说明：这个文件以前同时承担「通用代理图形解码器」的职责，2026-11 把它拆成
// 了 ProxyGraphics.h；现在 Tch::DecodeProxyGraphics 只是通用解码器的薄封装。

#include "Import/ProxyGraphics.h"
#include <string>

namespace MiniCAD::Tch
{
    // 天正对象解出的代理图形图元（历史类型名，等价于 Proxy::ProxyGraphics 的实体表）
    using TchProxyGraphics = Proxy::ProxyGraphics;

    // 记录里的图层 / 线型是表内序号，文字只给字体文件名：由调用方按图纸提供映射。
    // 天正另存为代理实体的图纸里，图层/线型序号多为 -1，此时按对象自身的处理——
    // 这一约定在通用解码器里已实现（见 ProxyGraphics.h 的 case 16 / case 18）。
    using ProxyGraphicsContext = Proxy::ProxyGraphicsContext;

    // 解一段天正对象的代理图形。返回是否解出了至少一个图元。
    // 与通用解码器语义一致：未知记录跳过而不是整份放弃。
    inline bool DecodeProxyGraphics(const MiniDWG::UnknownEntity& source, TchProxyGraphics& output,
                                    std::string* diagnostic = nullptr,
                                    const ProxyGraphicsContext* context = nullptr)
    {
        return Proxy::Decode(source, output, diagnostic, context);
    }
}

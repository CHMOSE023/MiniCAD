#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <utility>

namespace MiniCAD
{
    // 单条 XDATA 项(组码 1000..1071)。为无损往返,既保留组码也保留原始值。
    // 字符串类(1000/1003/1004…)用 Str;数值类(1040/1070/1071…)用 Num。
    struct XDataItem
    {
        int16_t     Code   = 1000;
        std::string Str;
        double      Num    = 0.0;
        bool        IsNum  = false;

        XDataItem() = default;
        XDataItem(int16_t code, std::string s) : Code(code), Str(std::move(s)), IsNum(false) {}
        XDataItem(int16_t code, double n)      : Code(code), Num(n), IsNum(true) {}
    };

    // 单个注册应用(APPID,组码 1001)下的 XDATA 序列。
    struct XDataApp
    {
        std::string            AppName;   // 1001 应用名,如 "ACAD"
        std::vector<XDataItem> Items;      // 该应用的后续 1000..1071 数据
    };

    // 实体级扩展数据透传容器:导入时原样保存未知/扩展数据,导出时原样写回,
    // 保证 round-trip 不丢失(T0.4 验收)。
    struct ExtendedData
    {
        std::vector<XDataApp>                    XData;       // XDATA(1001 起)
        std::vector<std::pair<int16_t, std::string>> ExtDict; // 扩展字典原始组码对
        std::vector<std::string>                 Reactors;    // 持久 reactor 句柄(330)

        bool Empty() const { return XData.empty() && ExtDict.empty() && Reactors.empty(); }
    };
}

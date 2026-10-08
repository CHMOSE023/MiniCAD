// ── 属性描述表测试：取值、修改、校验、多选公共特性、撤销 ──────────────────────
#include "Core/Entity/LineEntity.hpp"
#include "Core/Entity/CircleEntity.hpp"
#include "Core/Entity/ArcEntity.hpp"
#include "Core/Entity/TextEntity.hpp"
#include "Core/Entity/PointEntity.hpp"
#include "Core/Entity/RectangleEntity.hpp"
#include "Core/Entity/EllipseEntity.hpp"
#include "Core/Entity/PolylineEntity.hpp"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Entity/InsertEntity.hpp"
#include "Core/Entity/RayEntity.hpp"
#include "Core/Entity/XLineEntity.hpp"
#include "Core/Entity/HatchEntity.hpp"
#include "Core/Entity/RegionEntity.hpp"
#include "Core/Entity/TableEntity.hpp"
#include "Core/Entity/SolidEntity.hpp"
#include "Core/Entity/ImageEntity.hpp"
#include "Core/Entity/WipeoutEntity.hpp"
#include "Document/Command/GeometryEditCommand.h"
#include "Document/CommandStack/CommandStack.h"
#include "Editor/Properties/PropertyTable.h"
#include "Scene/Scene.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace MiniCAD;

namespace
{
    int g_failures = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok)
            ++g_failures;
    }

    bool Near(double a, double b) { return std::abs(a - b) < 1e-6; }

    // 取一个数值特性（找不到返回 NaN）
    double Num(const Entity& e, const char* name)
    {
        for (const auto& d : PropertiesOf(e))
            if (std::string(d.name) == name)
                return std::get<double>(d.get(e));
        return std::nan("");
    }

    const CommonProperty* Find(const std::vector<CommonProperty>& v, const char* name)
    {
        for (const auto& p : v)
            if (std::string(p.name) == name)
                return &p;
        return nullptr;
    }
}

int RunPropertyTableTests()
{
    g_failures = 0;
    constexpr double kPi = 3.14159265358979323846;

    // ── 直线 ─────────────────────────────────────────────────
    LineEntity line(1, { 0, 0, 0 }, { 3, 4, 0 });
    Check(Near(Num(line, "长度"), 5.0) && Near(Num(line, "端点 X"), 3.0), "直线：长度 5，端点 X 3");
    Check(Near(Num(line, "角度"), std::atan2(4.0, 3.0) * 180.0 / kPi), "直线：角度以度为单位");

    auto longer = ApplyProperty(line, "长度", PropValue(10.0));
    Check(longer && Near(Num(*longer, "长度"), 10.0) && Near(Num(*longer, "端点 X"), 6.0) && Near(Num(*longer, "起点 X"), 0.0),
          "直线：改长度，起点不动、方向不变");
    Check(Near(Num(line, "长度"), 5.0), "修改在副本上进行，原对象不变");

    auto turned = ApplyProperty(line, "角度", PropValue(90.0));
    Check(turned && Near(Num(*turned, "长度"), 5.0) && Near(Num(*turned, "端点 X"), 0.0) && Near(Num(*turned, "端点 Y"), 5.0),
          "直线：改角度，绕起点转动、长度不变");
    Check(!ApplyProperty(line, "长度", PropValue(-1.0)) && !ApplyProperty(line, "长度", PropValue(0.0)), "直线：长度必须大于 0");
    Check(!ApplyProperty(line, "长度", PropValue(5.0)), "值没有变化：不产生副本");
    Check(!ApplyProperty(line, "长度", PropValue(std::string("abc"))), "类型不符的值被拒绝");
    Check(!ApplyProperty(line, "不存在的特性", PropValue(1.0)), "不存在的特性：忽略");

    LineEntity dot(2, { 1, 1, 0 }, { 1, 1, 0 });
    Check(!ApplyProperty(dot, "长度", PropValue(5.0)) && !ApplyProperty(dot, "角度", PropValue(30.0)), "零长度的线没有方向，不能改长度 / 角度");

    // ── 圆 ───────────────────────────────────────────────────
    CircleEntity circle(3, { 1, 2, 0 }, 5.0);
    Check(Near(Num(circle, "直径"), 10.0) && Near(Num(circle, "周长"), 2 * kPi * 5.0) && Near(Num(circle, "面积"), kPi * 25.0), "圆：直径、周长、面积是派生值");
    auto byDia = ApplyProperty(circle, "直径", PropValue(4.0));
    Check(byDia && Near(Num(*byDia, "半径"), 2.0), "圆：改直径同步半径");
    Check(!ApplyProperty(circle, "半径", PropValue(0.0)), "圆：半径必须大于 0");
    Check(!ApplyProperty(circle, "面积", PropValue(1.0)) && !ApplyProperty(circle, "周长", PropValue(1.0)), "圆：面积、周长只读");
    auto moved = ApplyProperty(circle, "圆心 X", PropValue(-7.5));
    Check(moved && Near(Num(*moved, "圆心 X"), -7.5) && Near(Num(*moved, "圆心 Y"), 2.0), "圆：改圆心 X 不影响 Y");

    // ── 圆弧 ─────────────────────────────────────────────────
    ArcEntity arc(4, { 0, 0, 0 }, 2.0, 0.0, kPi / 2);
    Check(Near(Num(arc, "起始角"), 0.0) && Near(Num(arc, "终止角"), 90.0) && Near(Num(arc, "总角度"), 90.0) && Near(Num(arc, "弧长"), kPi),
          "圆弧：起止角、总角度、弧长");
    auto widen = ApplyProperty(arc, "总角度", PropValue(180.0));
    Check(widen && Near(Num(*widen, "终止角"), 180.0) && Near(Num(*widen, "起始角"), 0.0), "圆弧：改总角度，起始角不动");
    Check(!ApplyProperty(arc, "总角度", PropValue(0.0)) && !ApplyProperty(arc, "总角度", PropValue(400.0)), "圆弧：总角度范围 (0, 360]");

    // ── 文字 ─────────────────────────────────────────────────
    TextEntity text(5, { 10, 20, 0 }, "你好", 2.5f, 0.0f);
    Check(std::get<std::string>(PropertiesOf(text)[0].get(text)) == "你好", "文字：内容");
    auto renamed = ApplyProperty(text, "内容", PropValue(std::string("MiniCAD")));
    Check(renamed && static_cast<TextEntity&>(*renamed).GetText() == "MiniCAD", "文字：改内容");
    Check(!ApplyProperty(text, "内容", PropValue(std::string())), "文字：内容不能为空");
    auto rotated = ApplyProperty(text, "旋转", PropValue(90.0));
    Check(rotated && std::abs(Num(*rotated, "旋转") - 90.0) < 1e-4, "文字：旋转以度为单位");
    Check(!ApplyProperty(text, "高度", PropValue(-2.0)), "文字：高度必须大于 0");

    // ── 点 ───────────────────────────────────────────────────
    {
        PointEntity p(20, { 3, 4, 0 });
        auto m = ApplyProperty(p, "位置 X", PropValue(9.0));
        Check(m && Near(Num(*m, "位置 X"), 9.0) && Near(Num(*m, "位置 Y"), 4.0), "点：改位置 X 不影响 Y");
    }

    // ── 矩形 ─────────────────────────────────────────────────
    {
        RectangleEntity r(21, { 0, 0, 0 }, { 4, 3, 0 });
        Check(Near(Num(r, "宽度"), 4.0) && Near(Num(r, "高度"), 3.0) && Near(Num(r, "面积"), 12.0) && Near(Num(r, "周长"), 14.0), "矩形：宽高面积周长");
        Check(Near(Num(r, "中心 X"), 2.0) && Near(Num(r, "中心 Y"), 1.5), "矩形：中心");
        auto w = ApplyProperty(r, "宽度", PropValue(10.0));
        Check(w && Near(Num(*w, "宽度"), 10.0) && Near(Num(*w, "高度"), 3.0) && Near(Num(*w, "面积"), 30.0), "矩形：改宽度，高度不变");
        auto h = ApplyProperty(r, "高度", PropValue(6.0));
        Check(h && Near(Num(*h, "高度"), 6.0) && Near(Num(*h, "宽度"), 4.0), "矩形：改高度，宽度不变");
        auto c = ApplyProperty(r, "中心 X", PropValue(10.0));
        Check(c && Near(Num(*c, "中心 X"), 10.0) && Near(Num(*c, "宽度"), 4.0), "矩形：改中心 X 是整体平移");
        Check(!ApplyProperty(r, "面积", PropValue(1.0)) && !ApplyProperty(r, "宽度", PropValue(0.0)), "矩形：面积只读，宽度必须大于 0");

        // 旋转过的矩形（45°）：宽高沿自己的边，不沿坐标轴
        const double s2 = std::sqrt(0.5);
        RectangleEntity rot(22, { 0, 0, 0 }, { 2 * s2, 2 * s2, 0 }, { 2 * s2 - s2, 2 * s2 + s2, 0 }, { -s2, s2, 0 });
        Check(Near(Num(rot, "宽度"), 2.0) && Near(Num(rot, "高度"), 1.0), "旋转矩形：宽 2 高 1");
        auto rw = ApplyProperty(rot, "宽度", PropValue(4.0));
        Check(rw && Near(Num(*rw, "宽度"), 4.0) && Near(Num(*rw, "高度"), 1.0), "旋转矩形：改宽度后高度不变，仍是矩形");
    }

    // ── 椭圆 ─────────────────────────────────────────────────
    {
        EllipseEntity el(23, { 0, 0, 0 }, 5.0, 3.0, 0.0);
        Check(Near(Num(el, "半轴 X"), 5.0) && Near(Num(el, "半轴 Y"), 3.0), "椭圆：两个半轴");
        auto a = ApplyProperty(el, "半轴 Y", PropValue(4.0));
        Check(a && Near(Num(*a, "半轴 Y"), 4.0) && Near(Num(*a, "半轴 X"), 5.0), "椭圆：改半轴 Y");
        auto rot = ApplyProperty(el, "旋转", PropValue(30.0));
        Check(rot && Near(Num(*rot, "旋转"), 30.0), "椭圆：旋转以度为单位");
        Check(!ApplyProperty(el, "半轴 X", PropValue(0.0)), "椭圆：半轴必须大于 0");
    }

    // ── 多段线 ───────────────────────────────────────────────
    {
        PolylineEntity pl(24, { { 0, 0, 0 }, { 3, 0, 0 }, { 3, 4, 0 } }, { 0.0, 0.0 });
        Check(Near(Num(pl, "顶点数"), 3.0) && Near(Num(pl, "长度"), 7.0), "多段线：顶点数 3，长度 7");
        Check(Near(Num(pl, "终点 X"), 3.0) && Near(Num(pl, "终点 Y"), 4.0), "多段线：终点是最后一个顶点");
        auto e = ApplyProperty(pl, "终点 Y", PropValue(8.0));
        Check(e && Near(Num(*e, "长度"), 11.0) && Near(Num(*e, "顶点数"), 3.0), "多段线：改终点 Y，长度跟着变");
        Check(!ApplyProperty(pl, "长度", PropValue(1.0)) && !ApplyProperty(pl, "顶点数", PropValue(1.0)), "多段线：长度、顶点数只读");
        Check(!ApplyProperty(pl, "线宽", PropValue(-1.0)), "多段线：线宽不能为负");
    }

    // ── 多行文字 ─────────────────────────────────────────────
    {
        MTextEntity mt(25, 0, "第一行\P第二行", { 1, 2, 0 }, 2.5, 0.0, 30.0);
        auto h = ApplyProperty(mt, "高度", PropValue(5.0));
        Check(h && Near(Num(*h, "高度"), 5.0), "多行文字：改高度");
        auto t = ApplyProperty(mt, "内容", PropValue(std::string("abc")));
        Check(t && static_cast<MTextEntity&>(*t).GetText() == "abc", "多行文字：改内容");
        auto bw = ApplyProperty(mt, "框宽度", PropValue(0.0));
        Check(bw && Near(Num(*bw, "框宽度"), 0.0), "多行文字：框宽度可以是 0（不限宽）");
        Check(!ApplyProperty(mt, "框宽度", PropValue(-1.0)), "多行文字：框宽度不能为负");
    }

    // ── 块插入 ───────────────────────────────────────────────
    {
        InsertEntity ins(26, 1, "BLK", { 5, 6, 0 });
        Check(std::get<std::string>(PropertiesOf(ins)[0].get(ins)) == "BLK" && !PropertiesOf(ins)[0].set, "块插入：块名只读");
        auto m = ApplyProperty(ins, "缩放 X", PropValue(-2.0));
        Check(m && Near(Num(*m, "缩放 X"), -2.0) && Near(Num(*m, "缩放 Y"), 1.0), "块插入：缩放可以为负（镜像），只改 X");
        Check(!ApplyProperty(ins, "缩放 Y", PropValue(0.0)), "块插入：缩放不能为 0");
        auto r = ApplyProperty(ins, "旋转", PropValue(45.0));
        Check(r && std::abs(Num(*r, "旋转") - 45.0) < 1e-9 && Near(Num(*r, "位置 X"), 5.0), "块插入：改旋转，位置不变");
    }

    // ── 射线 / 构造线 ────────────────────────────────────────
    {
        RayEntity ray(27, { 1, 1, 0 }, { 2, 0, 0 });
        XLineEntity xl(28, { 0, 0, 0 }, { 0, 3, 0 });
        Check(Near(Num(ray, "角度"), 0.0) && Near(Num(xl, "角度"), 90.0), "射线 / 构造线：方向角");
        auto r = ApplyProperty(ray, "角度", PropValue(90.0));
        const auto& d = static_cast<RayEntity&>(*r).GetDirection();
        Check(r && Near(d.x, 0.0) && Near(d.y, 2.0), "射线：改方向角，方向向量长度不变");
        auto x = ApplyProperty(xl, "基点 X", PropValue(7.0));
        Check(x && Near(Num(*x, "基点 X"), 7.0) && Near(Num(*x, "角度"), 90.0), "构造线：改基点不影响方向");
    }

    // ── 填充 / 面域（面域是填充的子类，表不能串）────────────────
    {
        HatchLoop loop = HatchLoop::FromPolyline(Polyline({ { 0, 0, 0 }, { 10, 0, 0 }, { 10, 5, 0 }, { 0, 5, 0 }, { 0, 0, 0 } }));
        HatchEntity hatch(29, std::vector<HatchLoop>{ loop });
        RegionEntity region(30, std::vector<HatchLoop>{ loop });
        Check(Near(Num(hatch, "比例"), 1.0) && !std::isnan(Num(hatch, "角度")), "填充：比例、角度");
        auto sc = ApplyProperty(hatch, "比例", PropValue(2.5));
        Check(sc && Near(Num(*sc, "比例"), 2.5), "填充：改比例");
        Check(!ApplyProperty(hatch, "比例", PropValue(0.0)), "填充：比例必须大于 0");
        Check(std::isnan(Num(region, "比例")) && Near(Num(region, "面积"), 50.0) && Near(Num(region, "周长"), 30.0), "面域：只有面积、周长，没有填充的比例");
        Check(!ApplyProperty(region, "面积", PropValue(1.0)), "面域：全部只读");
    }

    // ── 派生类不套用基类的表 ─────────────────────────────────
    {
        TableEntity table(31, { 0, 0, 0 }, std::vector<double>{ 10, 10 }, std::vector<double>{ 5, 5 }, 2.5);
        SolidEntity solid(32, { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 });
        Check(PropertiesOf(table).empty(), "表格：不套用多行文字的表（否则会改写表格内容）");
        Check(PropertiesOf(solid).empty(), "实心填充：不套用矩形的表");
    }

    // ── 没有描述表的实体 ─────────────────────────────────────
    SolidEntity point(6, { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 });      // 暂无描述表的实体
    Check(PropertiesOf(point).empty(), "没有描述表的实体：空表");

    // ── 多选：公共特性 ───────────────────────────────────────
    {
        LineEntity a(10, { 0, 0, 0 }, { 3, 4, 0 }), b(11, { 0, 0, 0 }, { 6, 8, 0 });
        auto common = CommonProperties({ &a, &b });
        const auto* sx  = Find(common, "起点 X");
        const auto* len = Find(common, "长度");
        Check(sx && sx->value && Near(std::get<double>(*sx->value), 0.0), "多选：值一致显示具体值");
        Check(len && !len->value, "多选：值不同为「多种」");
    }
    {
        auto common = CommonProperties({ &line, &circle });
        Check(Find(common, "长度") == nullptr && Find(common, "半径") == nullptr, "多选不同类型：各自独有的特性不出现");
        Check(common.empty(), "直线 + 圆：没有名称相同的特性");
    }
    {
        CircleEntity c2(12, { 9, 9, 0 }, 1.0);
        auto common = CommonProperties({ &circle, &c2 });
        Check(Find(common, "半径") && !Find(common, "半径")->value && Find(common, "半径")->editable, "两个圆：半径可编辑，取值不同");
        Check(Find(common, "面积") && !Find(common, "面积")->editable, "只读特性在公共表里仍然只读");
    }
    {
        auto common = CommonProperties({ &line, &point });
        Check(common.empty(), "含无描述表实体：没有公共特性");
        Check(CommonProperties({}).empty(), "没有选择：空");
    }

    // ── 通过 GeometryEditCommand 修改并撤销（面板走的路径）────────
    {
        Scene scene;
        CommandStack stack;
        const auto id = scene.NextObjectID();
        scene.AddEntity(std::make_unique<CircleEntity>(id, Math::Point3{ 0, 0, 0 }, 5.0));
        const auto* orig = static_cast<const Entity*>(scene.GetEntity(id));
        auto after = ApplyProperty(*orig, "半径", PropValue(8.0));

        std::vector<GeometryEditCommand::Item> items;
        GeometryEditCommand::Item it;
        it.id = id; it.before = orig->Clone(id); it.after = std::move(after);
        items.push_back(std::move(it));
        Check(stack.Execute(std::make_unique<GeometryEditCommand>("修改特性", std::move(items)), scene), "命令：执行成功");
        Check(Near(Num(*static_cast<Entity*>(scene.GetEntity(id)), "半径"), 8.0), "命令：半径变为 8");
        stack.Undo(scene);
        Check(Near(Num(*static_cast<Entity*>(scene.GetEntity(id)), "半径"), 5.0), "撤销：半径还原为 5");
    }

    return g_failures;
}

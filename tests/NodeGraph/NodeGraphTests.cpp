// NodeGraphTests 的入口：按顺序运行各组测试；--dump-nodes 按菜单顺序输出节点清单。
// 测试分在 NodeGraph*Tests.cpp，公共部分与分组见 NodeGraphTestCommon.h
#include "NodeGraphTestCommon.h"

using namespace NodeGraphTest;

// 按菜单顺序输出节点清单（Markdown），用来核对 docs/NodeGraph/PLAN.md 第 4.2 节
void DumpNodes()
{
    std::string cat;
    for (const NodeTypeInfo* t : NodeRegistry::Builtin().Ordered())
    {
        if (t->Category != cat)
        {
            cat = t->Category;
            std::printf("\n### %s\n\n| 节点 | ID | 输入 | 输出 |\n|---|---|---|---|\n", cat.c_str());
        }
        auto ports = [](const std::vector<PortSpec>& ps)
        {
            std::string s;
            for (const auto& p : ps)
                s += (s.empty() ? "" : "、") + p.Name + "（" + KindName(p.Type, p.Kind) + "）";
            return s;
        };
        std::printf("| %s | `%s` | %s | %s |\n", t->Name.c_str(), t->Id.c_str(), ports(t->Inputs).c_str(), ports(t->Outputs).c_str());
    }
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "--dump-nodes")
    {
        DumpNodes();
        return 0;
    }
    TestRegistry();
    TestCategoryOrder();
    TestSearchForPort();
    TestBasicsAndDirty();
    TestLinksRules();
    TestLongestList();
    TestSerialization();
    TestHistoryAndDuplicate();
    TestCopyPaste();
    TestSearch();
    TestExpression();
    TestDataNodes();
    TestCurvesWithoutKernel();
    TestReferenceAndDocument();
    TestPreviewAndBake();
    TestParameters();
    TestBackground();
    TestGroups();
    std::printf("\n%s：%d 项失败\n", g_failed == 0 ? "全部通过" : "有失败", g_failed);
    return g_failed;
}

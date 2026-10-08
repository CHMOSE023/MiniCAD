#include "TestFramework.h"
#include "TestUtils.h"
#include "Widgets/AutoComplete.h"
#include "Widgets/CommandConsole.h"
#include "Widgets/TextBox.h"
#include <cctype>
#include <string>
#include <vector>

using namespace MiniGUI;
using namespace MiniGUI::Test;

namespace
{
    // 命令行 + 记录提交与 Esc；补全：按前缀（不区分大小写）匹配几个 CAD 命令，别名 L 把 LINE 排在最前
    struct ConsoleScene
    {
        TestUI          t{ { 600, 300 } };
        CommandConsole* console = nullptr;
        std::vector<std::string> submitted;
        int             escapes = 0;

        ConsoleScene()
        {
            console = t.Add<CommandConsole>();
            console->SetCompletion([](const std::string& text)
            {
                static const char* names[] = { "LINE", "LAYER", "LENGTHEN", "CIRCLE", "COPY" };
                std::vector<ListItem> items;
                for (const char* n : names)
                {
                    const std::string name = n;
                    if (name.size() >= text.size() && std::equal(text.begin(), text.end(), name.begin(),
                            [](char a, char b) { return std::toupper(static_cast<unsigned char>(a)) == b; }))
                        items.push_back({ name });
                }
                return items;
            });
            console->SetOnSubmit([this](const std::string& s) { submitted.push_back(s); });
            console->SetOnEscape([this] { ++escapes; });
            t.Layout();
            console->FocusInput();
        }

        void Key(MiniGUI::Key k) { t.ui.KeyDown(k, 0); t.ui.KeyUp(k, 0); }
        void Type(const char* text) { t.ui.TextInput(text); t.Layout(); }
        AutoComplete* Ac() const { return console->GetInput()->GetAutoComplete(); }
    };
}

TEST(Console_CompletionSelectsFirstAndEnterRuns)
{
    ConsoleScene s;
    s.Type("l");
    CHECK(s.Ac()->IsOpen());
    CHECK(s.Ac()->GetSuggestions().size() == 3);
    CHECK(s.Ac()->GetCurrent() == 0);           // 默认选中第一项

    s.Key(Key::Down);                           // 移到 LAYER
    s.Key(Key::Enter);
    CHECK(s.submitted.size() == 1 && s.submitted[0] == "LAYER");
    CHECK(s.console->GetInputText().empty());
    CHECK(!s.Ac()->IsOpen());
    CHECK(s.console->GetHistory().size() == 1);
}

TEST(Console_SpaceSubmitsAndEmptyRepeats)
{
    ConsoleScene s;
    s.Type("ci");
    s.Type(" ");                                // 空格 = 回车，执行选中的候选
    CHECK(s.submitted.size() == 1 && s.submitted[0] == "CIRCLE");

    s.Type(" ");                                // 没有输入：提交空串（宿主重复上一条命令）
    CHECK(s.submitted.size() == 2 && s.submitted[1].empty());

    // 没有候选的输入原样提交；输入法确认的整段文字里的空格照常输入
    s.Type("100,50");
    CHECK(!s.Ac()->IsOpen());
    s.Key(Key::Enter);
    CHECK(s.submitted.back() == "100,50");
    s.Type("a b");
    CHECK(s.console->GetInputText() == "a b");
}

TEST(Console_HistoryNavigation)
{
    ConsoleScene s;
    for (const char* cmd : { "100,50", "200,80", "100,50" })    // 重复的只保留最近一次
    {
        s.Type(cmd);
        s.Key(Key::Enter);
    }
    CHECK(s.console->GetHistory().size() == 2);

    s.Key(Key::Up);
    CHECK(s.console->GetInputText() == "100,50");
    s.Key(Key::Up);
    CHECK(s.console->GetInputText() == "200,80");
    s.Key(Key::Up);                             // 到头了不动
    CHECK(s.console->GetInputText() == "200,80");
    s.Key(Key::Down);
    CHECK(s.console->GetInputText() == "100,50");
    s.Key(Key::Down);                           // 回到空白输入
    CHECK(s.console->GetInputText().empty());
}

TEST(Console_EscapeClosesThenClears)
{
    ConsoleScene s;
    s.Type("co");
    CHECK(s.Ac()->IsOpen());
    s.Key(Key::Escape);                         // 先关闭候选
    CHECK(!s.Ac()->IsOpen());
    CHECK(s.console->GetInputText() == "co");
    CHECK(s.escapes == 0);
    s.Key(Key::Escape);                         // 再清空并通知宿主
    CHECK(s.console->GetInputText().empty());
    CHECK(s.escapes == 1);
    CHECK(s.submitted.empty());
}

TEST(Console_LogScrollsToEnd)
{
    ConsoleScene s;
    std::vector<std::string> lines;
    for (int i = 0; i < 40; ++i)
        lines.push_back("命令: Line " + std::to_string(i));
    s.console->SetLog(lines);
    s.t.Layout();
    CHECK(s.console->GetLogCount() == 40);
    CHECK(s.console->IsLogScrolledToEnd());

    s.console->SetPrompt("指定第一个点:");
    CHECK(s.console->GetPrompt() == "指定第一个点:");
}

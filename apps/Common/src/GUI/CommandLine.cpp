// ── 命令行：回显、提示、补全、历史；绘图区直接打字进入命令行 ─────────
#include "GUI/MainFrame.h"
#include "Core/UIContext.h"
#include "Editor/Input/InputEvent.h"
#include "Widgets/CommandConsole.h"
#include "Widgets/DockSpace.h"
#include "Widgets/TextBox.h"
#include "Widgets/UiLayout.h"
#include "Widgets/ViewportHost.h"
#include <algorithm>
#include <cctype>

namespace MiniCAD
{
    namespace
    {
        bool StartsWithNoCase(const std::string& name, const std::string& prefix)
        {
            return name.size() >= prefix.size()
                && std::equal(prefix.begin(), prefix.end(), name.begin(), [](char a, char b)
                   {
                       return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b));
                   });
        }
    }

    std::unique_ptr<MiniGUI::Node> MainFrame::CreateCommandLine()
    {
        using namespace MiniGUI;
        auto console = std::make_unique<CommandConsole>();
        m_console = console.get();

        // 补全：工具全名按前缀匹配；输入恰好是别名（L → Line）时把别名指向的命令排在最前，回车 / 空格直接执行它
        m_console->SetCompletion([this](const std::string& text)
        {
            std::vector<ListItem> items;
            if (!m_docManager.GetActive())
                return items;
            const Editor& editor = m_docManager.GetEditor();
            if (editor.IsZoomPending())
                return items;   // 正在输入 ZOOM 的选项：E / A 不是命令前缀，不补全
            for (const std::string& name : editor.GetCommandNames())
            {
                if (StartsWithNoCase(name, text))
                    items.push_back({ name });
            }
            const std::string resolved = editor.ResolveCommandAlias(text);
            if (!resolved.empty())
            {
                items.erase(std::remove_if(items.begin(), items.end(), [&resolved](const ListItem& i) { return i.text == resolved; }),
                            items.end());
                ListItem first{ resolved };
                if (!StartsWithNoCase(resolved, text))
                    first.detail = "别名 " + text;
                items.insert(items.begin(), first);
            }
            // 候选里显示对应的中文名称（直线、圆…）
            for (ListItem& item : items)
            {
                const auto it = m_toolCommands.find(item.text);
                if (it != m_toolCommands.end() && item.detail.empty())
                    item.detail = MiniGUI::CommandRegistry::StripMnemonic(m_commands.GetLabel(it->second));
            }
            return items;
        });
        // ZOOM 等待选项时，敲下 E / A 立即执行，不必再按空格 / 回车（Z 空格 E 两步完成）
        m_console->GetInput()->SetOnChanged([this](const std::string& text)
        {
            if (text.size() == 1 && m_docManager.GetActive() && m_docManager.GetEditor().IsZoomPending()
                && (text[0] == 'e' || text[0] == 'E' || text[0] == 'a' || text[0] == 'A'))
                m_console->Submit();
        });
        m_console->SetOnSubmit([this](const std::string& text) { RunCommandLine(text); });
        m_console->SetOnEscape([this]
        {
            // Esc：与在绘图区按 Esc 相同（取消当前工具、清除选择），焦点回到绘图区
            if (m_viewport->IsVisible())
            {
                m_viewport->Focus();
                SendKeyToEditor(MiniGUI::Key::Escape);
            }
        });
        return console;
    }

    void MainFrame::RunCommandLine(const std::string& text)
    {
        if (!m_docManager.GetActive())
            return;
        Editor& editor = m_docManager.GetEditor();
        if (text.empty())
        {
            // 空回车：工具进行中交给工具（例如结束多段线），否则重复上一条命令
            if (editor.IsActiveTool())
                SendKeyToEditor(MiniGUI::Key::Enter);
            else
                editor.RunCommand(text);
        }
        else if (!editor.IsActiveTool())
        {
            editor.RunCommand(text);
        }
        else if (!editor.SubmitCoordinateText(text))
        {
            // 工具进行中（同 AutoCAD）：坐标 / 距离已交给工具；单个字母是工具的选项键（如多段线的 A、闭合的 C）；
            // 像坐标但格式不对的给出提示；其他文字按新命令处理
            const unsigned char first = static_cast<unsigned char>(text[0]);
            if (text.size() == 1 && std::isalpha(first))
                SendKeyToEditor(MiniGUI::KeyFromLetter(static_cast<char>(std::toupper(first))));
            else if (text.find_first_of("0123456789,<@") != std::string::npos)
                editor.GetCmdLine().Echo("无效的点或距离：" + text);
            else
                editor.RunCommand(text);
        }

        // 执行后焦点回到绘图区：继续用鼠标取点、键盘输入选项、Esc 取消
        if (m_viewport->IsVisible())
            m_viewport->Focus();
        NoteInput();
        StateChanged();
        m_viewport->RequestRender();   // ZOOM 等命令只改相机，不经过工具的重绘

    }

    bool MainFrame::RouteKeyToCommandLine(const MiniGUI::KeyEvent& e)
    {
        // AutoCAD 习惯：没有进行中的工具时，在绘图区敲字母就是在输入命令。
        // 只把焦点交给命令行，随后到达的字符消息（WM_CHAR）自然落入输入框，大小写、输入法都按输入框处理
        using MiniGUI::Key;
        const bool letter = e.key >= Key::A && e.key <= Key::Z;
        if (e.type != MiniGUI::KeyEventType::Down || !letter || e.Ctrl() || e.Alt() || !m_console
            || m_docManager.GetEditor().IsActiveTool())
            return false;

        MiniGUI::DockSpace* dock = m_layout->GetDock("main");
        if (dock && dock->HasPanel("commandline"))
        {
            if (!dock->IsPanelVisible("commandline"))
                return false;               // 命令行被关掉了：交给 Editor（它自己也能缓存字母命令）
            dock->ActivatePanel("commandline");
        }
        else if (!m_console->GetParent())
        {
            return false;                   // 界面描述里没有命令行
        }
        m_console->FocusInput();
        return true;
    }

    void MainFrame::SyncCommandLine()
    {
        if (!m_console)
            return;
        if (!m_docManager.GetActive())
        {
            m_console->SetPrompt("命令:");
            return;
        }
        const CommandLine& cl = m_docManager.GetEditor().GetCmdLine();
        m_console->SetPrompt(cl.Prompt().empty() ? std::string("命令:") : cl.Prompt());
        m_console->SetLog(cl.Lines());
    }
}

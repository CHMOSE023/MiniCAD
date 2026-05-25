#pragma once
#include "Editor/Editor.h"
#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cctype>

namespace MiniCAD
{
    // 移植自 imgui_demo 的 ExampleAppConsole，接入 MiniCAD 命令系统：
    //   回显 ← Editor::CommandLine::Lines()   提示 ← 活跃工具 GetPrompt()
    //   Tab 补全候选 ← Editor::GetCommandNames()   回车 → Editor::RunCommand()
    //   上下键历史由本类维护（纯 UI 导航状态）
    class CommandConsole
    {
    public:
        void Draw(Editor& editor, float height)
        {
            CommandLine& cl = editor.GetCmdLine();

            ImGui::BeginChild("##Console", ImVec2(0.f, height), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar);

            // ── 回显滚动区（保留底部一行给输入框） ──────────────
            ImGuiStyle& style = ImGui::GetStyle();
            const float footer = style.ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));   // 透出外层 Console 底色
            if (ImGui::BeginChild("##Scroll", ImVec2(0.f, -footer), ImGuiChildFlags_NavFlattened,
                                  ImGuiWindowFlags_HorizontalScrollbar))
            {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));
                for (const std::string& line : cl.Lines())
                {
                    ImVec4 color;
                    bool   hasColor = false;
                    if (line.rfind("未知命令", 0) == 0 || line.rfind("候选", 0) == 0)
                    { color = ImVec4(1.0f, 0.5f, 0.4f, 1.f); hasColor = true; }
                    else if (line.rfind("命令:", 0) == 0)
                    { color = ImVec4(0.5f, 0.9f, 0.5f, 1.f); hasColor = true; }

                    if (hasColor) ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::TextUnformatted(line.c_str());
                    if (hasColor) ImGui::PopStyleColor();
                }

                const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
                if (cl.ConsumeScrollToBottom() || (m_autoScroll && atBottom))
                    ImGui::SetScrollHereY(1.f);

                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();   // ChildBg

            // ── 提示 + 输入框 ──────────────────────────────────
            const std::string& prompt = cl.Prompt();
            ImGui::AlignTextToFramePadding();   // 提示文本与输入框垂直居中对齐
            ImGui::TextUnformatted(prompt.empty() ? "命令:" : prompt.c_str());
            ImGui::SameLine();

            CbCtx ctx{ this, &editor };
            const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue
                                            | ImGuiInputTextFlags_CallbackCompletion
                                            | ImGuiInputTextFlags_CallbackHistory;
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));   // 输入框背景透出 Console 底色
            ImGui::SetNextItemWidth(-1.f);
            const bool submitted = ImGui::InputText("##cmdinput", m_input, sizeof(m_input), flags, &TextEditStub, &ctx);
            ImGui::PopStyleColor();
            if (submitted)
            {
                char*  s   = m_input;
                size_t len = std::strlen(s);
                while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) s[--len] = '\0';
                if (s[0]) ExecCommand(editor, s);
                m_input[0]     = '\0';
                m_reclaimFocus = true;
            }

            ImGui::SetItemDefaultFocus();
            if (m_reclaimFocus)
            {
                ImGui::SetKeyboardFocusHere(-1);   // 回车后把焦点收回输入框
                m_reclaimFocus = false;
            }

            ImGui::EndChild();
        }

    private:
        struct CbCtx { CommandConsole* self; Editor* editor; };

        void ExecCommand(Editor& editor, const char* cmd)
        {
            // 命令历史去重后追加到末尾
            for (size_t i = 0; i < m_history.size(); ++i)
                if (IEquals(m_history[i], cmd)) { m_history.erase(m_history.begin() + i); break; }
            m_history.emplace_back(cmd);
            m_historyPos = -1;

            editor.RunCommand(cmd);   // 回显由 Editor 写入 CommandLine
        }

        static int TextEditStub(ImGuiInputTextCallbackData* data)
        {
            auto* c = static_cast<CbCtx*>(data->UserData);
            return c->self->OnTextEdit(data, *c->editor);
        }

        int OnTextEdit(ImGuiInputTextCallbackData* data, Editor& editor)
        {
            switch (data->EventFlag)
            {
            case ImGuiInputTextFlags_CallbackCompletion:
            {
                // 定位当前单词
                const char* word_end   = data->Buf + data->CursorPos;
                const char* word_start = word_end;
                while (word_start > data->Buf)
                {
                    const char c = word_start[-1];
                    if (c == ' ' || c == '\t' || c == ',' || c == ';') break;
                    word_start--;
                }
                const int wlen = (int)(word_end - word_start);

                // 候选 = 命令名中前缀匹配者
                std::vector<std::string> all = editor.GetCommandNames();
                std::vector<const char*> cand;
                for (const std::string& name : all)
                    if (Strnicmp(name.c_str(), word_start, wlen) == 0)
                        cand.push_back(name.c_str());

                if (cand.empty())
                {
                    // 无匹配
                }
                else if (cand.size() == 1)
                {
                    data->DeleteChars((int)(word_start - data->Buf), wlen);
                    data->InsertChars(data->CursorPos, cand[0]);
                    data->InsertChars(data->CursorPos, " ");
                }
                else
                {
                    // 多候选：补全到公共前缀，并把候选列入回显
                    int match_len = wlen;
                    for (;;)
                    {
                        int  c            = 0;
                        bool allMatch     = true;
                        for (size_t i = 0; i < cand.size() && allMatch; ++i)
                        {
                            if (i == 0)
                                c = std::toupper((unsigned char)cand[i][match_len]);
                            else if (c == 0 || c != std::toupper((unsigned char)cand[i][match_len]))
                                allMatch = false;
                        }
                        if (!allMatch) break;
                        match_len++;
                    }
                    if (match_len > wlen)
                    {
                        data->DeleteChars((int)(word_start - data->Buf), wlen);
                        data->InsertChars(data->CursorPos, cand[0], cand[0] + match_len);
                    }

                    std::string msg = "候选: ";
                    for (const char* c : cand) { msg += c; msg += "  "; }
                    editor.GetCmdLine().Echo(msg);
                }
                break;
            }
            case ImGuiInputTextFlags_CallbackHistory:
            {
                const int prev = m_historyPos;
                if (data->EventKey == ImGuiKey_UpArrow)
                {
                    if (m_historyPos == -1)        m_historyPos = (int)m_history.size() - 1;
                    else if (m_historyPos > 0)     m_historyPos--;
                }
                else if (data->EventKey == ImGuiKey_DownArrow)
                {
                    if (m_historyPos != -1 && ++m_historyPos >= (int)m_history.size())
                        m_historyPos = -1;
                }
                if (prev != m_historyPos)
                {
                    const char* h = (m_historyPos >= 0) ? m_history[m_historyPos].c_str() : "";
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, h);
                }
                break;
            }
            }
            return 0;
        }

        // 大小写不敏感比较：name 前 n 字符是否匹配输入 word（ASCII 命令名）
        static int Strnicmp(const char* s1, const char* s2, int n)
        {
            int d = 0;
            while (n > 0 && (d = std::toupper((unsigned char)*s2) - std::toupper((unsigned char)*s1)) == 0 && *s1)
            { s1++; s2++; n--; }
            return d;
        }

        static bool IEquals(const std::string& a, const char* b)
        {
            if (a.size() != std::strlen(b)) return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (std::toupper((unsigned char)a[i]) != std::toupper((unsigned char)b[i])) return false;
            return true;
        }

    private:
        char                     m_input[256] = {};
        std::vector<std::string> m_history;          // 输入过的命令
        int                      m_historyPos   = -1; // -1 = 新行；0..n-1 = 浏览历史
        bool                     m_reclaimFocus = false;
        bool                     m_autoScroll   = true;
    };
}

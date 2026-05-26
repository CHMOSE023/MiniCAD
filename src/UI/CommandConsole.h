#pragma once
#include "Editor/Editor.h"
#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cctype>
#include <algorithm>

namespace MiniCAD
{
    // 命令行控件：输入字符实时在上方弹出候选列表（类 AutoCAD / Claude Code 风格）
    //   回显 ← Editor::CommandLine::Lines()   提示 ← 活跃工具 GetPrompt()
    //   候选 ← Editor::GetCommandNames() 前缀匹配   回车/鼠标 → Editor::RunCommand()
    //   Up/Down：候选列表导航（无候选时导航历史）
    class CommandConsole
    { 
    public:
        void Draw(Editor& editor, float height)
        { 
            CommandLine& cl = editor.GetCmdLine(); 

            // 配色
            const ImVec4 kHistoryBg     = ImVec4(0.85f, 0.85f, 0.85f, 1.f);
            const ImVec4 kHistoryText   = ImVec4(0.f, 0.f, 0.f, 1.f); 
            const ImVec4 kInputBg       = ImVec4(1.f, 1.f, 1.f, 1.f);
            const ImVec4 kInputText     = ImVec4(0.f, 0.f, 0.f, 1.f); 
            const ImVec4 kCandidateBg   = ImVec4(0.15f, 0.15f, 0.18f, 1.f);
            const ImVec4 kCandidateText = ImVec4(1.f, 1.f, 1.f, 1.f);
            const float kInputBarH      = 32.f;
            const float kSeparatorH     = 1.f;

            
            ImGui::BeginChild("##Console", ImVec2(0.f, height), ImGuiChildFlags_Borders
                                                              , ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
             
            ImGuiStyle& style = ImGui::GetStyle();
            const float footer = kInputBarH + kSeparatorH + style.ItemSpacing.y * 2.f;

            // ============================================================
            // 1. 命令历史区
            // ============================================================
            { 
                ImGui::PushStyleColor(ImGuiCol_ChildBg,          kHistoryBg);
                ImGui::PushStyleColor(ImGuiCol_Text,             kHistoryText);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 4.f));

                if (ImGui::BeginChild("##Scroll", ImVec2(0.f, -footer), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_HorizontalScrollbar))
                {
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f, 1.f));

                    for (const std::string& line : cl.Lines())
                    {
                        ImGui::SetCursorPosX(5.0);
                        ImGui::TextUnformatted(line.c_str());
                    }

                    const bool atBottom =
                        ImGui::GetScrollY() >= ImGui::GetScrollMaxY();

                    if (cl.ConsumeScrollToBottom() || (m_autoScroll && atBottom))
                        ImGui::SetScrollHereY(1.f); 

                    ImGui::PopStyleVar();
                }

                ImGui::EndChild(); 
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
            }

            // ============================================================
            // 2. 分割线
            // ============================================================
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.6f, 0.6f, 0.6f, 1.f));

            ImGui::Separator();

            ImGui::PopStyleColor();

            // ============================================================
            // 3. 输入区
            // ============================================================
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, kInputBg);

                if (ImGui::BeginChild("##InputBar", ImVec2(0.f, 32.f), 0, ImGuiWindowFlags_NoScrollbar))
                {
                    const float lineH   = ImGui::GetFrameHeight();
                    const float offsetY = (32.f - lineH) * 0.5f;  
                    ImGui::SetCursorPosY(offsetY);
                    ImGui::SetCursorPosX(5.0);


                    const std::string& prompt = cl.Prompt(); 
                    ImGui::AlignTextToFramePadding(); 
                    ImGui::PushStyleColor(ImGuiCol_Text, kInputText); 
                    ImGui::TextUnformatted(prompt.empty() ? "命令:" : prompt.c_str());
                    ImGui::PopStyleColor(); 
                    ImGui::SameLine();

                    // 输入框位置（候选框定位）
                    const ImVec2 inputPos  = ImGui::GetCursorScreenPos();
                    const float inputWidth = ImGui::GetContentRegionAvail().x;

                    CbCtx ctx{ this, &editor };

                    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue
                                                    | ImGuiInputTextFlags_CallbackAlways 
                                                    | ImGuiInputTextFlags_CallbackHistory;

                    ImGui::PushStyleColor(ImGuiCol_FrameBg,           kInputBg);
                    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,    kInputBg);
                    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,     kInputBg);
                    ImGui::PushStyleColor(ImGuiCol_Text,              ImVec4(0.f, 0.f, 0.f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_InputTextCursor,   ImVec4(0.f, 0.f, 0.f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg,    ImVec4(0.25f, 0.48f, 0.78f, 0.35f));
                    ImGui::PushStyleColor(ImGuiCol_Border,            ImVec4(0.6f, 0.6f, 0.6f, 1.f));

                    ImGui::SetNextItemWidth(-1.f);

                    if (m_reclaimFocus)
                    {
                        ImGui::SetKeyboardFocusHere(0);
                        m_reclaimFocus = false;
                    }

                    const bool submitted = ImGui::InputText("##cmdinput", m_input, sizeof(m_input), flags, &TextEditStub, &ctx);

                    ImGui::PopStyleColor(7);

                    // ====================================================
                    // 回车执行
                    // ====================================================
                    if (submitted)
                    {
                        char tmp[256];

                        if (m_selectedCandidate >= 0 &&  m_selectedCandidate < (int)m_candidates.size())
                        {
                            std::strncpy(tmp, m_candidates[m_selectedCandidate].c_str(), sizeof(tmp) - 1);
                        }
                        else
                        {
                            std::strncpy(tmp, m_input, sizeof(tmp) - 1);
                        }

                        tmp[sizeof(tmp) - 1] = '\0'; 
                        size_t len = std::strlen(tmp);

                        while (len > 0 && (tmp[len - 1] == ' ' || tmp[len - 1] == '\t'))
                        {
                            tmp[--len] = '\0';
                        }

                        if (tmp[0])
                        {
                            ExecCommand(editor, tmp);
                        }

                        m_input[0] = '\0';

                        m_candidates.clear();
                        m_selectedCandidate = -1;

                        m_reclaimFocus = true;
                    }

                    // ====================================================
                    // 鼠标点击候选项执行
                    // ====================================================
                    if (m_pendingExec >= 0)
                    {
                        if (m_pendingExec < (int)m_candidates.size())
                        {
                            ExecCommand(editor, m_candidates[m_pendingExec].c_str());
                        }

                        m_pendingExec = -1; 

                        m_candidates.clear();
                        m_selectedCandidate = -1;

                        m_clearBuffer = true;
                        m_reclaimFocus = true;
                    }

                    // ====================================================
                    // 候选列表
                    // ====================================================
                    DrawCandidateList({ inputPos.x,inputPos.y - 12 }, 320);
                } 

                ImGui::EndChild(); 
                ImGui::PopStyleColor();
            }

            ImGui::EndChild();
        }

    private:
        struct CbCtx { CommandConsole* self; Editor* editor; }; 

        // 候选列表浮窗：贴紧输入框上方，固定宽度与输入框一致
        void DrawCandidateList(ImVec2 inputPos, float inputWidth)
        {
            if (m_candidates.empty()) return;

            const float lineH   = ImGui::GetTextLineHeightWithSpacing();
            const float padV    = 4.f;
            const int   maxShow = 8;
            const int   count   = std::min((int)m_candidates.size(), maxShow);
            const float popupH  = count * lineH + padV * 2.f;

            ImGui::SetNextWindowPos(ImVec2(inputPos.x, inputPos.y - popupH), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(inputWidth, popupH), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.95f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.f, padV));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.13f, 0.17f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.48f, 0.78f, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.48f, 0.78f, 0.75f));

            const ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration
                                      | ImGuiWindowFlags_NoMove
                                      | ImGuiWindowFlags_NoSavedSettings
                                      | ImGuiWindowFlags_NoFocusOnAppearing
                                      | ImGuiWindowFlags_NoNav;
            if (ImGui::Begin("##cmd_candidates", nullptr, wf))
            {
                for (int i = 0; i < (int)m_candidates.size(); ++i)
                {
                    const bool sel = (i == m_selectedCandidate);
                    ImGui::PushID(i);
                    if (ImGui::Selectable(m_candidates[i].c_str(), sel))
                        m_pendingExec = i;
                    if (sel) ImGui::SetScrollHereY(0.5f);
                    ImGui::PopID();
                }
            }
            ImGui::End();

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
        }

        void ExecCommand(Editor& editor, const char* cmd)
        {
            for (size_t i = 0; i < m_history.size(); ++i)
                if (IEquals(m_history[i], cmd)) { m_history.erase(m_history.begin() + i); break; }
            m_history.emplace_back(cmd);
            m_historyPos = -1;
            editor.RunCommand(cmd);
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
            case ImGuiInputTextFlags_CallbackAlways:
            {
                // 鼠标点击执行后清空 InputText 内部缓冲
                if (m_clearBuffer)
                {
                    data->DeleteChars(0, data->BufTextLen);
                    m_clearBuffer = false;
                    m_prevBuf[0] = '\0';
                    m_candidates.clear();
                    m_selectedCandidate = -1;
                    break;
                }

                // 仅在内容变化时重算候选（避免每帧无意义查询）
                if (std::strcmp(data->Buf, m_prevBuf) != 0)
                {
                    std::strncpy(m_prevBuf, data->Buf, sizeof(m_prevBuf) - 1);
                    m_prevBuf[sizeof(m_prevBuf) - 1] = '\0';

                    // 提取光标前的当前单词
                    const char* end = data->Buf + data->CursorPos;
                    const char* start = end;
                    while (start > data->Buf)
                    {
                        const char ch = start[-1];
                        if (ch == ' ' || ch == '\t' || ch == ',' || ch == ';') break;
                        start--;
                    }
                    const int wlen = (int)(end - start); 

                    m_candidates.clear();
                    m_selectedCandidate = -1;
                    if (wlen > 0)
                    {
                        for (const std::string& name : editor.GetCommandNames())
                        {
                            if (Strnicmp(name.c_str(), start, wlen) == 0)
                            {
                                m_candidates.push_back(name);
                            }
                        }
                    }
                }
                break;
            }
            case ImGuiInputTextFlags_CallbackHistory:
            {
                if (!m_candidates.empty())
                {
                    // 有候选时 Up/Down 在候选列表中循环导航
                    if (data->EventKey == ImGuiKey_UpArrow)
                        m_selectedCandidate = (m_selectedCandidate <= 0)
                        ? (int)m_candidates.size() - 1 : m_selectedCandidate - 1;
                    else if (data->EventKey == ImGuiKey_DownArrow)
                        m_selectedCandidate = (m_selectedCandidate >= (int)m_candidates.size() - 1)
                        ? -1 : m_selectedCandidate + 1;
                }
                else
                {
                    // 无候选时 Up/Down 导航历史（保留原有行为）
                    const int prev = m_historyPos;
                    if (data->EventKey == ImGuiKey_UpArrow)
                    {
                        if (m_historyPos == -1)      m_historyPos = (int)m_history.size() - 1;
                        else if (m_historyPos > 0)   m_historyPos--;
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
                }
                break;
            }
            }
            return 0;
        }

        // 检查 name[0..n-1] 是否与 word[0..n-1] 大小写不敏感匹配（ASCII 命令名）
        static int Strnicmp(const char* name, const char* word, int n)
        {
            int d = 0;
            while (n > 0 && (d = std::toupper((unsigned char)*word) - std::toupper((unsigned char)*name)) == 0 && *name)
            {
                name++; word++; n--;
            }
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
        char                     m_prevBuf[256] = {};        // 上一帧 buf，用于变化检测
        std::vector<std::string> m_history;
        int                      m_historyPos = -1;
        bool                     m_reclaimFocus = true;
        bool                     m_autoScroll = true;
        bool                     m_clearBuffer = false;      // 通知回调清空 InputText 内部缓冲

        std::vector<std::string> m_candidates;               // 当前前缀匹配的候选命令
        int                      m_selectedCandidate = -1;   // -1 = 无选中
        int                      m_pendingExec = -1;         // 鼠标点击候选项的索引
    };
}

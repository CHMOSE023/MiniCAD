// 检查更新：GitHub Release 的最新版本比当前新时，标题栏版本号旁显示提示，点开可直接下载本平台的发布包；
// AppPlatform::PromptsForUpdates 的平台（安卓）启动时直接弹出提示，可以忽略这个版本。
// 请求经 AppPlatform::HttpGet 在后台线程完成；网页版不支持（部署的总是最新版）
#include "GUI/MainFrame.h"
#include "MiniI18n/I18n.h"
#include "MiniCADVersion.h"
#include "Core/Log.h"
#include "Core/UIContext.h"
#include "Data/Json.h"
#include "Style/Theme.hpp"
#include "Widgets/Dialog.h"
#include "Widgets/Label.h"
#include "Widgets/ScrollView.h"
#include <algorithm>
#include <memory>
#include <sstream>
#include <vector>

namespace MiniCAD
{
    namespace
    {
        constexpr const char* kLatestReleaseApi = "https://api.github.com/repos/CHMOSE023/MiniCAD/releases/latest";
        constexpr const char* kIgnoredUpdateKey = "ignoredUpdate";     // 用户设置：启动提示里点了"忽略"的版本，之后不再提示它

        std::vector<int> ParseVersion(std::string_view text)
        {
            if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
                text.remove_prefix(1);
            std::vector<int> parts;
            int  value = 0;
            bool digit = false;
            for (char ch : text)
            {
                if (ch >= '0' && ch <= '9')
                {
                    value = value * 10 + (ch - '0');
                    digit = true;
                }
                else if (ch == '.' && digit)
                {
                    parts.push_back(value);
                    value = 0;
                    digit = false;
                }
                else
                    break;
            }
            if (digit)
                parts.push_back(value);
            return parts;
        }

        bool IsNewerVersion(std::string_view latest, std::string_view current)
        {
            std::vector<int> a = ParseVersion(latest), b = ParseVersion(current);
            if (a.empty())
                return false;
            const size_t n = std::max(a.size(), b.size());
            a.resize(n, 0);
            b.resize(n, 0);
            return a > b;
        }

        struct CheckResult
        {
            bool ok = false;
            std::string tag, pageUrl, downloadUrl, notes;
        };

        CheckResult ParseRelease(const std::string& body, const std::string& assetSuffix)
        {
            CheckResult r;
            MiniGUI::JsonValue doc;
            MiniGUI::JsonError error;
            if (!MiniGUI::ParseJson(body, doc, error) || !doc.IsObject())
                return r;
            r.tag     = doc["tag_name"].AsString();
            r.pageUrl = doc["html_url"].AsString();
            r.notes   = doc["body"].AsString();
            for (const MiniGUI::JsonValue& asset : doc["assets"].GetArray())
            {
                const std::string& name = asset["name"].AsString();
                if (name.size() >= assetSuffix.size() && name.compare(name.size() - assetSuffix.size(), assetSuffix.size(), assetSuffix) == 0)
                {
                    r.downloadUrl = asset["browser_download_url"].AsString();
                    break;
                }
            }
            r.ok = !r.tag.empty();
            return r;
        }

        // 发布说明的 Markdown 粗略转成纯文字行：去掉标题的 #、列表的 - 换成圆点、去掉 ` 与 **
        std::vector<std::string> NotesToLines(const std::string& notes, size_t maxLines)
        {
            std::vector<std::string> lines;
            std::istringstream in(notes);
            std::string line;
            while (lines.size() < maxLines && std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                size_t start = line.find_first_not_of(' ');
                if (start == std::string::npos)
                    continue;
                std::string text = line.substr(start);
                if (text.starts_with('#'))
                    text = text.substr(std::min(text.find_first_not_of('#'), text.size()));
                else if (text.starts_with("- ") || text.starts_with("* "))
                    text = std::string(start > 0 ? "    " : "") + "\xE2\x80\xA2 " + text.substr(2);
                std::string clean;
                for (size_t i = 0; i < text.size(); ++i)
                {
                    if (text[i] == '`' || (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '*'))
                    {
                        if (text[i] == '*')
                            ++i;
                        continue;
                    }
                    clean += text[i];
                }
                const size_t first = clean.find_first_not_of(' ');
                if (first != std::string::npos)
                    lines.push_back(start > 0 ? clean : clean.substr(first));
            }
            return lines;
        }
    }

    void MainFrame::CheckForUpdates(bool manual)
    {
        const std::string suffix = m_platform ? m_platform->GetReleaseAssetSuffix() : std::string();
        if (suffix.empty() || m_updateChecking)
            return;
        m_updateChecking = true;
        auto result = std::make_shared<CheckResult>();
        AppPlatform* platform = m_platform;
        m_platform->RunInBackground(
            [platform, result, suffix]
            {
                std::string body;
                if (platform->HttpGet(kLatestReleaseApi, body))
                    *result = ParseRelease(body, suffix);
            },
            [this, result, manual]
            {
                using namespace MiniGUI;
                m_updateChecking = false;
                if (!result->ok)
                {
                    LOG_WARN("Update check failed");
                    if (manual)
                    {
                        auto dialog = std::make_unique<Dialog>(Tr("检查更新"));
                        dialog->GetBody()->EditLayoutStyle().padding = Edges::Symmetric(18.0f, 12.0f);
                        dialog->GetBody()->AddChild<Label>(Tr("无法获取最新版本信息，请检查网络连接后重试。"), 14.0f);
                        Dialog* raw = dialog.get();
                        raw->AddButton(Tr("确定"), [raw] { raw->Close(); }, DialogButtonRole::Default);
                        m_ui->OpenPopup(std::move(dialog));
                    }
                    return;
                }
                const std::string latest = result->tag.starts_with('v') || result->tag.starts_with('V') ? result->tag.substr(1) : result->tag;
                if (!IsNewerVersion(latest, MINICAD_VERSION_STRING))
                {
                    if (manual)
                    {
                        auto dialog = std::make_unique<Dialog>(Tr("检查更新"));
                        dialog->GetBody()->EditLayoutStyle().padding = Edges::Symmetric(18.0f, 12.0f);
                        dialog->GetBody()->AddChild<Label>(TrF("已是最新版本（v{0}）。", MINICAD_VERSION_STRING), 14.0f);
                        Dialog* raw = dialog.get();
                        raw->AddButton(Tr("确定"), [raw] { raw->Close(); }, DialogButtonRole::Default);
                        m_ui->OpenPopup(std::move(dialog));
                    }
                    return;
                }
                LOG_INFO("Update available: %s", latest.c_str());
                m_update.version     = latest;
                m_update.pageUrl     = result->pageUrl;
                m_update.downloadUrl = result->downloadUrl.empty() ? result->pageUrl : result->downloadUrl;
                m_update.notes       = result->notes;
                if (m_appBar)
                    BuildAppBar();
                if (manual)
                    ShowUpdateDialog();
                else if (m_platform->PromptsForUpdates() && m_platform->ReadUserSetting(kIgnoredUpdateKey) != latest)
                    ShowUpdateDialog(true);
                m_platform->RequestRedraw();
            });
    }

    void MainFrame::ShowUpdateDialog(bool prompt)
    {
        using namespace MiniGUI;
        if (m_update.version.empty())
            return;
        // 手机竖屏放不下 480 宽的说明
        const Vec2  display = m_ui->GetDisplaySize();
        const float notesW  = std::clamp(display.x - 96.0f, 200.0f, 480.0f);
        const float notesH  = std::clamp(display.y * 0.4f, 100.0f, 260.0f);
        auto dialog = std::make_unique<Dialog>(Tr("发现新版本"));
        Node* body = dialog->GetBody();
        {
            LayoutStyle& s = body->EditLayoutStyle();
            s.direction = FlexDirection::Column;
            s.padding   = Edges::Symmetric(18.0f, 12.0f);
            s.gap       = 8.0f;
        }
        body->AddChild<Label>(TrF("MiniCAD v{0} 已发布", m_update.version), 16.0f);
        body->AddChild<Label>(TrF("当前版本 v{0}", MINICAD_VERSION_STRING), 13.0f, Theme::TextDim);

        const std::vector<std::string> lines = NotesToLines(m_update.notes, 60);
        if (!lines.empty())
        {
            auto* scroll = body->AddChild<ScrollView>();
            scroll->SetScrollAxes(false, true);
            scroll->EditLayoutStyle().width  = notesW;
            scroll->EditLayoutStyle().height = notesH;
            auto* col = scroll->SetContent<Node>();
            {
                LayoutStyle& cs = col->EditLayoutStyle();
                cs.direction = FlexDirection::Column;
                cs.width     = notesW - 16.0f;
                cs.gap       = 3.0f;
            }
            for (const std::string& line : lines)
            {
                auto* label = col->AddChild<Label>(line, 13.0f, Theme::TextDim);
                label->SetWrap(true);
            }
        }

        Dialog* raw = dialog.get();
        AppPlatform* platform = m_platform;
        const std::string download = m_update.downloadUrl, page = m_update.pageUrl;
        raw->AddButton(Tr("下载"), [raw, platform, download] { platform->OpenUrl(download); raw->Close(); }, DialogButtonRole::Default);
        if (prompt)
        {
            raw->AddButton(Tr("忽略此版本"), [raw, platform, version = m_update.version]
            {
                platform->WriteUserSetting(kIgnoredUpdateKey, version);
                raw->Close();
            }, DialogButtonRole::Cancel);
            m_ui->OpenPopup(std::move(dialog));
            return;
        }
        if (!page.empty() && page != download)
            raw->AddButton(Tr("发布页"), [platform, page] { platform->OpenUrl(page); });
        raw->AddButton(Tr("稍后"), [raw] { raw->Close(); }, DialogButtonRole::Cancel);
        m_ui->OpenPopup(std::move(dialog));
    }
}

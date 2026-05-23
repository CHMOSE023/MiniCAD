#include "DocumentManager.h"
#include "Document.h"
#include <algorithm>
#include <utility>
#include <memory>
#include <string>
#include "Text/FontSystem.h"
#include "Core/Entity/MTextEntity.hpp"
#include "Core/Math/Point3.hpp"
namespace MiniCAD
{
    Document& DocumentManager::Create()
    {
        auto doc = std::make_unique<Document>();
        doc->SetName(GenerateUniqueName());
        doc->SetFontSystem(m_fontSystem);

        auto styleId  = m_fontSystem->FindStyle("GB2312")->id;
        auto styleId1 = 0;

        doc->GetScene().AddEntity(std::make_unique<MTextEntity>(doc->GetScene().NextObjectID(), styleId,  "TTF >>> 仿宋字体  GB2312.ttf  ",  Math::Point3(1, 2, 0), 1, 0, 100));
        doc->GetScene().AddEntity(std::make_unique<MTextEntity>(doc->GetScene().NextObjectID(), styleId1, "SHX >>> 探索者中文字体 %%% %%p %%c20 %%p0.5 %%13225 %%1318@200  4%%132 %%132   %%132%%131%%130 TSSDCHN.SHX L1 梁 %%132 25 @ 200mm  ",    Math::Point3(1,0.5, 0), 1, 0, 100));

        m_active = doc.get();
        m_docs.push_back(std::move(doc));

        if (m_viewport)
            m_editor.Bind(*m_active, *m_viewport);

        return *m_active;
    }

    void DocumentManager::InitViewport(IRenderer& renderer, float w, float h)
    {
        m_viewport = std::make_unique<Viewport>(renderer, w, h);

        if (m_active)
            m_editor.Bind(*m_active, *m_viewport);
    }

    void DocumentManager::Close(Document* doc)
    {
        auto it = std::find_if(m_docs.begin(), m_docs.end(),
            [&](const auto& d) { return d.get() == doc; });

        if (it == m_docs.end())
            return;

        if (m_active == doc)
        {
            m_editor.Unbind();
            m_active = nullptr;
        }

        m_docs.erase(it);

        if (!m_docs.empty() && m_active == nullptr)
        {
            m_active = m_docs.back().get();
            if (m_viewport)
                m_editor.Bind(*m_active, *m_viewport);
        }
    }

    Document* DocumentManager::GetActive() const
    {
        return m_active;
    }

    void DocumentManager::SetActive(Document* doc)
    {
        if (m_active == doc) return;

        m_editor.Unbind();
        m_active = doc;

        if (m_active && m_viewport)
            m_editor.Bind(*m_active, *m_viewport);
    }

    void DocumentManager::SetFontSystem(FontSystem* fontSystem)
    {
        m_fontSystem = fontSystem;
    }

    FontStyle::FontStyleId DocumentManager::RegisterFontStyle(FontStyle style)
    {
        if (!m_fontSystem) return 0;
        return m_fontSystem->RegisterStyle(std::move(style));
    }

    const FontStyle* DocumentManager::FindFontStyle(const std::string& name) const
    {
        if (!m_fontSystem) return nullptr;
        return m_fontSystem->FindStyle(name);
    }

    const FontStyle* DocumentManager::FindFontStyle(FontStyle::FontStyleId id) const
    {
        if (!m_fontSystem) return nullptr;
        return m_fontSystem->FindStyle(id);
    }

    std::vector<std::unique_ptr<Document>>& DocumentManager::GetAll()
    {
        return m_docs;
    }

    void DocumentManager::SetRenderer(IRenderer* renderer)
    {
        m_renderer = renderer;
    }

    void DocumentManager::New()
    {
        if (!m_renderer)
            return;

        Create();
    }

    void DocumentManager::Open()
    {
        printf("Open\n");
    }

    void DocumentManager::Save()
    {
        if (m_active)
        {
            m_active->Save();
        }
    }

    void DocumentManager::SaveAs()
    {
        if (m_active)
        {
            m_active->SaveAs("");
        }
    }

    void DocumentManager::SaveAll()
    {
        for (auto& doc : m_docs)
        {
            doc->Save();
        }
    }

    void DocumentManager::Undo() const
    {
        GetActive()->Undo();
    }

    void DocumentManager::Redo() const
    {
        GetActive()->Redo();
    }

    void DocumentManager::Paste()
    {
        printf("Paste\n");
    }

    void DocumentManager::CopySelected()
    {
        printf("Copy Selected\n");
    }

    std::string DocumentManager::GenerateUniqueName()
    {
        int index = 0;

        while (true)
        {
            std::string name = "Untitled";
            if (index > 0)
                name += " " + std::to_string(index);

            bool exists = false;
            for (auto& d : m_docs)
            {
                if (d->GetName() == name)
                {
                    exists = true;
                    break;
                }
            }

            if (!exists)
                return name;

            index++;
        }
    }

}

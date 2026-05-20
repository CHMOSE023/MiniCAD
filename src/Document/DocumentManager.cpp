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
    Document& DocumentManager::Create(IRenderer& r, float w, float h)
    {
        auto doc = std::make_unique<Document>(r, w, h);
        doc->SetName(GenerateUniqueName());
        doc->SetFontSystem(m_fontSystem); // 注入字体系统
         
        auto styleId  = m_fontSystem->FindStyle("GB2312")->id; 
        auto styleId1 = 0; 
  
        doc->GetScene().AddEntity(std::make_unique<MTextEntity>(doc->GetScene().NextObjectID(), styleId,  "MyMiniCAD GB2312  仿宋字体",  Math::Point3(1, 2, 0), 1, 0, 100));
        doc->GetScene().AddEntity(std::make_unique<MTextEntity>(doc->GetScene().NextObjectID(), styleId1, "MyMiniCAD 探索者字体中文",    Math::Point3(1, 1, 0), 1, 0, 100));

        m_active = doc.get();
        m_docs.push_back(std::move(doc));
        return *m_active;
    }

    void DocumentManager::Close(Document* doc)
    {
        auto it = std::find_if(m_docs.begin(), m_docs.end(),
            [&](const auto& d) { return d.get() == doc; });

        if (it == m_docs.end())
            return;

        if (m_active == doc)
            m_active = nullptr;

        m_docs.erase(it);

        if (!m_docs.empty() && m_active == nullptr)
            m_active = m_docs.back().get();
    }

    Document* DocumentManager::GetActive() const
    {
        return m_active;
    }

    void DocumentManager::SetActive(Document* doc)
    {
        m_active = doc;
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

        Create(*m_renderer, m_defaultWidth, m_defaultHeight);
    }

    void DocumentManager::Open()
    {
		// 这里直接创建一个新文档，实际应用中应该弹出文件对话框让用户选择文件
        // New();
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
			// 这里直接调用 SaveAs，实际应用中应该弹出文件对话框让用户选择路径
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

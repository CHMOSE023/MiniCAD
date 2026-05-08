#include "DocumentManager.h"
#include "Document.h"
#include "Render/D3D11/Renderer.h"
#include <utility>
#include <memory>
#include <string>
namespace MiniCAD
{
    Document& DocumentManager::Create(Renderer& r, float w, float h)
    {
        auto doc = std::make_unique<Document>(r, w, h);

        doc->SetName(GenerateUniqueName());

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
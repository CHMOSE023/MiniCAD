#include "Document.h"

namespace MiniCAD
{

    Document::Document()
    {
        New();
    }

    void Document::New() 
    {
        m_scene    = std::make_unique<Scene>();
        m_cmdStack = std::make_unique<CommandStack>();
        m_editor   = std::make_unique<Editor>(m_scene.get(), m_cmdStack.get());
    }

    void Document::OnInput(const InputEvent& e) 
    {
        m_editor->OnInput(e);
    }

    void Document::Save(const std::string& path)
    {
        // TODO: 序列化 Scene
    }

    void Document::Load(const std::string& path)
    {
        // TODO: 反序列化 Scene
    }

}

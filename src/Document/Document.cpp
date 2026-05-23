#include "Document.h"
#include "Text/FontSystem.h"
#include <cstdio>

namespace MiniCAD
{
    Document::Document()
        : m_scene()
        , m_cmdStack()
    {
    }

    void Document::SetPath(const std::string& path)
    {
        m_path = path;

        auto pos = path.find_last_of("/\\");
        if (pos != std::string::npos)
            m_name = path.substr(pos + 1);
        else
            m_name = path;
    }

    void Document::SetName(const std::string& name)
    {
        m_name = name;
    }

    bool Document::Save()
    {
        if (!HasPath())
            return false;

        return SaveToFile(m_path);
    }

    bool Document::SaveAs(const std::string& path)
    {
        if (SaveToFile(path))
        {
            SetPath(path);
            m_dirty = false;
            return true;
        }
        return false;
    }

    bool Document::SaveToFile(const std::string& path)
    {
        // TODO: Scene 序列化
        printf("Saving to %s ... (not implemented)\n", path.c_str());
        m_dirty = false;
        return true;
    }
}

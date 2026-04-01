#include "Document.h"
#include "Serialization/AsciiSerializer.h"
#include <fstream>
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

    bool Document::OnInput(const InputEvent& e) 
    { 
        // 只处理键盘按下事件
        if (e.type == InputEventType::KeyDown)
        { 
            if (e.keyCode == 'S' && e.HasModifier(ModifierKey::Ctrl))  // 判断 Ctrl+S
            { 
                // MessageBox(0, L"Ctrl+S", L"保存文件", 0);
                Save("autosave.cad"); // 保存文档（可改成实际路径或弹窗选择）
                return true; // 拦截事件
            }
           
            if (e.keyCode == 'O' && e.HasModifier(ModifierKey::Ctrl))  // 判断 Ctrl+O
            {
                // MessageBox(0, L"Ctrl+O", L"打开文件", 0);
                Load("autosave.cad"); // 保存文档（可改成实际路径或弹窗选择）                 
                return true; // 拦截事件
            }
            
        }

        // 其他输入交给 Editor 处理
        if (m_editor)
            return m_editor->OnInput(e);

        return false;
    }

    void Document::Save(const std::string& path)
    {
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open())
        {
            throw std::runtime_error("无法打开文件保存");
        }

        // 创建序列化器
        AsciiSerializer serializer(ofs);

        // 保存场景
        m_scene->Serialize(serializer); 

        ofs.close();
         
    }

    void Document::Load(const std::string& path)
    {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open())
        {
            throw std::runtime_error("无法打开文件加载");
        }

        AsciiSerializer serializer(ifs);

        m_scene->Deserialize(serializer);    // 清空旧数据并加载   
        m_cmdStack->Clear();                 // 加载后清空命令栈和选择状态

        //m_editor->ClearSelection();

        ifs.close();
    }

}

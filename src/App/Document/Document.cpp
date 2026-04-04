#include "Document.h"
#include "Serialization/AsciiSerializer.h"
#include <fstream>
#include <format> 
#include "App/ErrorReporter.h"
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
                Save("autosave.mcad"); // 保存文档（可改成实际路径或弹窗选择）
                return true; // 拦截事件
            }
           
            if (e.keyCode == 'O' && e.HasModifier(ModifierKey::Ctrl))  // 判断 Ctrl+O
            {
                // MessageBox(0, L"Ctrl+O", L"打开文件", 0);
                Load("autosave.mcad"); // 保存文档（可改成实际路径或弹窗选择）                 
                return true; // 拦截事件
            }
            
        }

        // 其他输入交给 Editor 处理
        if (m_editor)
            return m_editor->OnInput(e);

        return false;
    }

    bool Document::Save(const std::filesystem::path& path)
    {
        auto tmpPath = path;
        tmpPath.replace_extension(".tmp");

        {
            std::ofstream ofs(tmpPath);
            if (!ofs.is_open())
            {
                ReportError(std::format("无法创建临时文件: {}", tmpPath.string()));
                return false;
            }

            AsciiSerializer serializer(ofs);
            m_scene->Serialize(serializer);
        }

        std::error_code ec;
        std::filesystem::rename(tmpPath, path, ec);
        if (ec)
        {
            std::filesystem::remove(tmpPath);
            ReportError(std::format("无法保存文件: {}", ec.message()));
            return false;
        }

        return true;
         
    }

    bool Document::Load(const std::filesystem::path& path)
    {
        std::ifstream ifs(path);
        if (!ifs.is_open())
        {
            auto msg = std::format("无法打开文件: {}", path.string());
            ReportError(msg);
			return false;
        }

        AsciiSerializer serializer(ifs);

        m_scene->Deserialize(serializer);    // 清空旧数据并加载   
        m_cmdStack->Clear();                 // 加载后清空命令栈和选择状态

        //m_editor->ClearSelection();

        ifs.close();
		return true;
    }

}

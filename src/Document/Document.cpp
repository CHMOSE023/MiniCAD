#include "Document.h"
#include "Core/Log.h"
#include "Document/DimAssoc.h"
#include "Import/CadExchange.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/BinarySerializer.h"
#include "Serialization/SerializeHelpers.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace MiniCAD
{
    namespace
    {
        // std::string(UTF-8) → std::filesystem::path,中文路径在 Windows 下正确解码。
        std::filesystem::path Utf8Path(const std::string& utf8)
        {
            return std::filesystem::path(
                std::u8string(reinterpret_cast<const char8_t*>(utf8.c_str()), utf8.size()));
        }

        // 扩展名是否为 .mcad(不区分大小写)→ 二进制格式;其余按 JSON 文本保存。
        bool IsMcadPath(const std::string& path)
        {
            constexpr const char* kExt = ".mcad";
            constexpr size_t      kLen = 5;
            if (path.size() < kLen) return false;
            const char* tail = path.c_str() + path.size() - kLen;
            for (size_t i = 0; i < kLen; ++i)
                if (std::tolower(static_cast<unsigned char>(tail[i])) != kExt[i])
                    return false;
            return true;
        }
    }

    Document::Document()
        : m_scene()
        , m_cmdStack()
    {
        // 所有编辑都经由 CommandStack,在此统一标记未保存
        m_cmdStack.SetOnMutate([this] { MarkDirty(); });
        // 每条命令生效后让关联标注跟上几何,跟随的改动与该命令一起撤销
        m_cmdStack.SetPostCommandHook([this] { return DimAssoc::Sync(m_scene); });
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

        if (!SaveToFile(m_path))
            return false;

        m_dirty = false;
        return true;
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

    std::string Document::SaveToString(bool binary) const
    {
        if (binary)
        {
            BinarySerializer s;
            Serialize(s);
            return s.Dump();
        }
        JsonSerializer s;
        Serialize(s);
        return s.Dump();
    }

    bool Document::SaveToFile(const std::string& path)
    {
        if (path.empty())
            return false;

        // .dwg / .dxf → 经 MiniDWG 导出(只保留 MiniCAD 支持的实体类型)
        if (const CadFileKind kind = CadKindFromPath(path); kind != CadFileKind::None)
        {
            std::string err;
            const std::vector<std::uint8_t> bytes = ExportCad(m_scene, kind, &err);
            if (bytes.empty())
            {
                LOG_ERROR("Save failed, export error: %s", err.c_str());
                return false;
            }
            std::ofstream file(Utf8Path(path), std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                LOG_ERROR("Save failed, cannot open file: %s", path.c_str());
                return false;
            }
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            file.close();
            if (!file)
            {
                LOG_ERROR("Save failed, write error: %s", path.c_str());
                return false;
            }
            LOG_INFO("Document exported: %s", path.c_str());
            return true;
        }

        // .mcad → 二进制;其余(.json 等)→ JSON 文本。两种格式内容等价。
        const std::string text = SaveToString(IsMcadPath(path));

        std::ofstream file(Utf8Path(path), std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            LOG_ERROR("Save failed, cannot open file: %s", path.c_str());
            return false;
        }

        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();

        if (!file)
        {
            LOG_ERROR("Save failed, write error: %s", path.c_str());
            return false;
        }

        LOG_INFO("Document saved: %s", path.c_str());
        return true;
    }

    bool Document::LoadFromString(const std::string& text)
    {
        // 按文件头嗅探格式(与扩展名无关):"MCAD" 魔数 → 二进制,否则按 JSON 解析。
        BinarySerializer bin;
        JsonSerializer   json;
        TreeSerializer*  s = nullptr;
        if (BinarySerializer::IsBinary(text))
        {
            if (!bin.Parse(text))
            {
                LOG_ERROR("Open failed, corrupt binary document data");
                return false;
            }
            s = &bin;
        }
        else
        {
            if (!json.Parse(text))
            {
                LOG_ERROR("Open failed, invalid JSON document data");
                return false;
            }
            s = &json;
        }

        // 校验文档标识,拒绝结构无关的文件。
        std::string magic;
        s->Value("format", magic);
        if (magic != "MiniCAD-Document")
        {
            LOG_ERROR("Open failed, not a MiniCAD document");
            return false;
        }

        Deserialize(*s);
        m_dirty = false;
        return true;
    }

    bool Document::LoadFromFile(const std::string& path)
    {
        std::ifstream file(Utf8Path(path), std::ios::binary);
        if (!file.is_open())
        {
            LOG_ERROR("Open failed, cannot open file: %s", path.c_str());
            return false;
        }

        std::ostringstream buf;
        buf << file.rdbuf();

        const std::string content = buf.str();
        if (CadKindFromPath(path) != CadFileKind::None)
        {
            const std::vector<std::uint8_t> bytes(content.begin(), content.end());
            std::string err;
            if (!ImportCad(bytes, m_scene, &err))
            {
                LOG_ERROR("Open failed: %s (%s)", path.c_str(), err.c_str());
                return false;
            }
            m_dirty = false;
            SetPath(path);
            LOG_INFO("Document imported: %s (%d entities)", path.c_str(), m_scene.EntityCount());
            return true;
        }

        if (!LoadFromString(content))
        {
            LOG_ERROR("Open failed: %s", path.c_str());
            return false;
        }

        SetPath(path);

        LOG_INFO("Document loaded: %s (%d entities)", path.c_str(), m_scene.EntityCount());
        return true;
    }

    void Document::Serialize(ISerializer& s) const
    {
        std::string magic = "MiniCAD-Document";
        s.Value("format", magic);
        int32_t version = 1;
        s.Value("version", version);

        std::string name = m_name;
        s.Value("name", name);

        if (s.BeginObject("camera"))
        {
            CameraState cam = m_cameraState;
            Ser::Value(s, "target", cam.Target);
            s.Value("zoom", cam.Zoom);
            s.EndObject();
        }

        if (s.BeginObject("scene"))
        {
            m_scene.Serialize(s);
            s.EndObject();
        }
    }

    void Document::Deserialize(ISerializer& s)
    {
        s.Value("name", m_name);

        if (s.BeginObject("camera"))
        {
            Ser::Value(s, "target", m_cameraState.Target);
            s.Value("zoom", m_cameraState.Zoom);
            s.EndObject();
        }

        if (s.BeginObject("scene"))
        {
            m_scene.Deserialize(s);
            s.EndObject();
        }
    }
}

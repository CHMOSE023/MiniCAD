#include "Document.h"
#include "Core/Log.h"
#include "Document/DimAssoc.h"
#include "Import/CadExchange.h"
#include "NodeGraph/Graph.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/BinarySerializer.h"
#include "Serialization/SerializeHelpers.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
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

    Document::~Document() = default;

    Nodes::Graph& Document::GetNodeGraph()
    {
        if (!m_nodeGraph)
            m_nodeGraph = std::make_unique<Nodes::Graph>();
        return *m_nodeGraph;
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
            const std::vector<std::uint8_t> bytes = ExportCad(m_scene, kind, m_cadVersion, &err, m_cadSource.get());
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

    bool Document::LoadFromFile(const std::string& path, std::string* error, const ImportProgress& progress)
    {
        if (error) error->clear();
        auto fail = [&](const std::string& reason)
        {
            if (error) *error = reason;
            LOG_ERROR("Open failed: %s (%s)", path.c_str(), reason.c_str());
            return false;
        };
        std::ifstream file(Utf8Path(path), std::ios::binary);
        if (!file.is_open())
        {
            return fail("无法读取文件，请检查文件是否存在及访问权限");
        }

        if (CadKindFromPath(path) != CadFileKind::None)
        {
            // CAD 直接读入字节缓冲，避免同时持有流缓冲、字符串和字节数组三份文件内容。
            file.seekg(0, std::ios::end);
            const auto length = file.tellg();
            if (length < 0 || static_cast<std::uintmax_t>(length) > std::numeric_limits<std::streamsize>::max())
            {
                return fail("无法确定图纸文件大小");
            }
            std::vector<std::uint8_t> bytes;
            if (static_cast<std::uintmax_t>(length) > bytes.max_size())
                return fail("图纸文件过大，无法分配读取缓冲区");
            bytes.resize(static_cast<std::size_t>(length));
            file.seekg(0, std::ios::beg);
            if (!file || (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length))))
            {
                return fail("无法完整读取图纸文件");
            }
            std::string err;
            if (!ImportCad(bytes, m_scene, &err, &m_cadVersion, &m_cadSource, progress, path))
            {
                return fail(err.empty() ? "图纸已损坏或版本不受支持" : err);
            }
            m_dirty = false;
            SetPath(path);
            LOG_INFO("Document imported: %s (%d entities)", path.c_str(), m_scene.EntityCount());
            return true;
        }

        std::ostringstream buf;
        buf << file.rdbuf();
        if (file.bad())
            return fail("无法完整读取文档文件");
        const std::string content = buf.str();
        if (!LoadFromString(content))
        {
            return fail("不是有效的 MiniCAD 文档，或文件已损坏");
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
            s.Value("azimuth", cam.Azimuth);       // 视图方向（M6）；旧文件没有时为俯视
            s.Value("elevation", cam.Elevation);
            s.EndObject();
        }

        if (s.BeginObject("scene"))
        {
            m_scene.Serialize(s);
            s.EndObject();
        }

        if (m_nodeGraph && (!m_nodeGraph->Empty() || !m_nodeGraph->Groups().Empty()) && s.BeginObject("nodeGraph"))
        {
            m_nodeGraph->Serialize(s);
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
            if (s.Has("azimuth") && s.Has("elevation"))
            {
                s.Value("azimuth", m_cameraState.Azimuth);
                s.Value("elevation", m_cameraState.Elevation);
            }
            s.EndObject();
        }

        if (s.BeginObject("scene"))
        {
            m_scene.Deserialize(s);
            s.EndObject();
        }

        m_nodeGraph.reset();
        if (s.Has("nodeGraph") && s.BeginObject("nodeGraph"))
        {
            GetNodeGraph().Deserialize(s);
            s.EndObject();
        }
    }

    // ── 图纸布局（LAYOUT / 图纸空间）─────────────────────────────────────

    // 当前布局按归属键认（m_activeKey，0 = 模型页）：布局增删、撤销后下标会变，键不变。
    // 当前布局被删掉（含撤销新建）时自动回到模型页
    int Document::ActiveLayout() const
    {
        const int i = m_scene.FindLayout(m_activeKey);
        return i >= 0 ? i : m_scene.ModelLayoutIndex();
    }

    bool Document::ActiveLayoutIsPaper() const
    {
        return m_scene.FindLayout(m_activeKey) >= 0;
    }

    uint64_t Document::ActiveLayoutOwnerKey() const
    {
        return ActiveLayoutIsPaper() ? m_activeKey : 0;     // 模型页：不过滤图纸空间归属
    }

    bool Document::SetActiveLayout(int index, CameraState& camera)
    {
        const auto& layouts = m_scene.Layouts();
        if (index < 0 || index >= static_cast<int>(layouts.size()))
            return false;
        const uint64_t key  = layouts[static_cast<size_t>(index)].Model ? 0 : layouts[static_cast<size_t>(index)].OwnerKey;
        const uint64_t from = ActiveLayoutOwnerKey();
        if (key == from && m_activeKey == from)
            return false;

        // 保存当前布局的视图（当前布局刚被删掉时它的视图也没用了，照样记下无妨）
        m_layoutViews[from] = camera;
        m_activeKey = key;
        m_scene.SetCurrentSpace(key != 0, key);

        // 恢复新布局的视图；首次进入图纸页时把纸张范围交给调用方去框选
        if (auto it = m_layoutViews.find(key); it != m_layoutViews.end())
        {
            camera = it->second;
        }
        else if (key != 0)
        {
            const DrawingLayout& sheet = layouts[static_cast<size_t>(index)];
            // 布局是二维图纸：模型页可能在三维视图里，进布局时回到俯视
            camera.Azimuth   = CameraState{}.Azimuth;
            camera.Elevation = CameraState{}.Elevation;
            if (sheet.ViewHeight > 0.0)
                camera.Target = { sheet.ViewCenterX, sheet.ViewCenterY, 0.0 };   // 保存的图纸空间视图
            else if (sheet.MaxX > sheet.MinX && sheet.MaxY > sheet.MinY)
            {
                // 只把目标点移到纸张中心；**缩放系数不在这里算** ——
                // Camera::Zoom 的单位是「每像素多少世界单位」的倒数关系
                // （GetWorldPerPixel = zoom / screenHeight），
                // 而 Document 不持有 Viewport、拿不到屏幕高度。
                // 调用方拿到 PaperBox 后调用 ZoomToBounds 统一处理。
                camera.Target = { (sheet.MinX + sheet.MaxX) * 0.5,
                                  (sheet.MinY + sheet.MaxY) * 0.5, 0.0 };
            }
            // 布局视图一旦进入就记为有效，后续切回来保留用户的平移缩放
            m_layoutViews[key] = camera;
        }

        // 切换布局必须让整个场景重建：布局归属是「按当前布局过滤图纸空间实体」，
        // 过滤条件变了，增量路径并不知道哪些块该消失（它只看实体自身的脏标记）。
        // 标记全量脏，下次渲染重建全部顶点。
        MarkDirty();
        m_scene.MarkDirty();
        return true;
    }

    bool Document::ValidateActiveLayout(CameraState& camera)
    {
        if (m_activeKey == 0 || ActiveLayoutIsPaper())
            return false;
        // 当前布局已不在布局表里：回到模型页，恢复模型页的视图
        const uint64_t gone = m_activeKey;
        m_activeKey = 0;
        m_scene.SetCurrentSpace(false, 0);
        m_layoutViews.erase(gone);
        if (auto it = m_layoutViews.find(0); it != m_layoutViews.end())
            camera = it->second;
        m_scene.MarkDirty();
        return true;
    }

    bool Document::IsVisibleInActiveLayout(const EntityAttr& attr) const
    {
        const bool paper = ActiveLayoutIsPaper();
        // 模型空间实体只在模型页显示；图纸空间实体只在布局页显示
        if (attr.InPaperSpace != paper)
            return false;

        // 布局页里进一步按归属键过滤：只显示属于**当前布局**的图纸空间实体。
        // OwnerKey 为 0（未提供）时不过滤，退化为「显示全部图纸空间实体」，
        // 这样老数据仍能显示，而不是整页空白。
        if (paper)
        {
            const uint64_t key = ActiveLayoutOwnerKey();
            if (key != 0 && attr.LayoutKey != 0 && attr.LayoutKey != key)
                return false;
        }
        return true;
    }
}

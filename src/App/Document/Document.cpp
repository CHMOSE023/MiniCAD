#include "pch.h"
#include "Document.h"
#include "App/Command/LayerCommands.h"
#include "Serialization/AsciiSerializer.h"
#include "App/ErrorReporter.h"
#include <commdlg.h>
#include <fstream>
#include <format>
#include <array>
#include <algorithm>

namespace MiniCAD
{
    namespace
    {
        constexpr wchar_t kFileFilter[] =
            L"MiniCAD Files (*.mcad)\0*.mcad\0All Files (*.*)\0*.*\0";

        std::vector<LayerID> GetSortedLayerIDs(const LayerManager& layerManager)
        {
            auto ids = layerManager.GetAllLayerIDs();
            std::sort(ids.begin(), ids.end());
            return ids;
        }
    }

    Document::Document()
    {
        New();
    }

    void Document::New()
    {
        IViewContext* view = m_editor ? m_editor->GetViewContext() : nullptr;

        m_scene = std::make_unique<Scene>();
        m_cmdStack = std::make_unique<CommandStack>();
        m_editor = std::make_unique<Editor>(m_scene.get(), m_cmdStack.get());
        m_editor->SetViewContext(view);
        m_scene->GetLayerManager().SetActiveLayerID(Layer::DefaultLayerID);

        m_currentPath.clear();
        m_scene->ClearDirty();
    }

    bool Document::OnInput(const InputEvent& e)
    {
        if (e.type == InputEventType::KeyDown)
        {
            if (e.keyCode == 'S' && e.HasModifier(ModifierKey::Ctrl))
            {
                return e.HasModifier(ModifierKey::Shift) ? SaveAs() : Save();
            }

            if (e.keyCode == 'O' && e.HasModifier(ModifierKey::Ctrl))
            {
                return Open();
            }

            if (e.HasModifier(ModifierKey::Ctrl) && e.HasModifier(ModifierKey::Shift))
            {
                if (e.keyCode == 'N')
                    return CreateLayerAndActivate();

                if (e.keyCode == 'H')
                    return ToggleActiveLayerVisibility();

                if (e.keyCode == 'L')
                    return ToggleActiveLayerLock();

                if (e.keyCode == VK_DELETE)
                    return DeleteActiveLayer();
            }

            if (e.HasModifier(ModifierKey::Ctrl) && e.keyCode == VK_PRIOR)
                return CycleActiveLayer(-1);

            if (e.HasModifier(ModifierKey::Ctrl) && e.keyCode == VK_NEXT)
                return CycleActiveLayer(1);
        }

        if (m_editor)
            return m_editor->OnInput(e);

        return false;
    }

    bool Document::Save()
    {
        if (!HasPath())
            return SaveAs();

        return Save(m_currentPath);
    }

    bool Document::SaveAs()
    {
        std::filesystem::path path;
        if (!PromptSavePath(path))
            return false;

        return Save(path);
    }

    bool Document::Open()
    {
        std::filesystem::path path;
        if (!PromptOpenPath(path))
            return false;

        return Load(path);
    }

    bool Document::Save(const std::filesystem::path& path)
    {
        if (path.empty())
        {
            ReportError("Save failed: empty path.");
            return false;
        }

        auto tmpPath = path;
        tmpPath.replace_extension(".tmp");

        try
        {
            {
                std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
                if (!ofs.is_open())
                {
                    ReportError(std::format("Failed to create temp file: {}", tmpPath.string()));
                    return false;
                }

                AsciiSerializer serializer(ofs);
                m_scene->Serialize(serializer);
            }

            std::error_code ec;
            if (std::filesystem::exists(path, ec))
            {
                ec.clear();
                std::filesystem::remove(path, ec);
                if (ec)
                {
                    std::filesystem::remove(tmpPath);
                    ReportError(std::format("Failed to overwrite file: {}", path.string()));
                    return false;
                }
            }

            ec.clear();
            std::filesystem::rename(tmpPath, path, ec);
            if (ec)
            {
                std::filesystem::remove(tmpPath);
                ReportError(std::format("Failed to save file: {}", ec.message()));
                return false;
            }
        }
        catch (const std::exception& ex)
        {
            std::filesystem::remove(tmpPath);
            ReportError(std::format("Save failed: {}", ex.what()));
            return false;
        }

        m_currentPath = path;
        m_scene->ClearDirty();
        return true;
    }

    bool Document::Load(const std::filesystem::path& path)
    {
        auto nextScene = std::make_unique<Scene>();
        auto nextCommandStack = std::make_unique<CommandStack>();

        try
        {
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs.is_open())
            {
                ReportError(std::format("Failed to open file: {}", path.string()));
                return false;
            }

            AsciiSerializer serializer(ifs);
            nextScene->Deserialize(serializer);
        }
        catch (const std::exception& ex)
        {
            ReportError(std::format("Load failed: {}", ex.what()));
            return false;
        }

        IViewContext* view = m_editor ? m_editor->GetViewContext() : nullptr;
        auto nextEditor = std::make_unique<Editor>(nextScene.get(), nextCommandStack.get());
        nextEditor->SetViewContext(view);

        m_scene = std::move(nextScene);
        m_cmdStack = std::move(nextCommandStack);
        m_editor = std::move(nextEditor);

        m_currentPath = path;
        m_scene->ClearDirty();
        ResetEditorState();
        return true;
    }

    bool Document::PromptSavePath(std::filesystem::path& path) const
    {
        std::array<wchar_t, MAX_PATH> buffer{};
        std::wstring initial = HasPath() ? m_currentPath.wstring() : L"untitled.mcad";
        initial.copy(buffer.data(), buffer.size() - 1);

        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetActiveWindow();
        ofn.lpstrFilter = kFileFilter;
        ofn.lpstrFile = buffer.data();
        ofn.nMaxFile = static_cast<DWORD>(buffer.size());
        ofn.lpstrDefExt = L"mcad";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

        if (!GetSaveFileNameW(&ofn))
            return false;

        path = buffer.data();
        return true;
    }

    bool Document::PromptOpenPath(std::filesystem::path& path) const
    {
        std::array<wchar_t, MAX_PATH> buffer{};

        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetActiveWindow();
        ofn.lpstrFilter = kFileFilter;
        ofn.lpstrFile = buffer.data();
        ofn.nMaxFile = static_cast<DWORD>(buffer.size());
        ofn.lpstrDefExt = L"mcad";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

        if (!GetOpenFileNameW(&ofn))
            return false;

        path = buffer.data();
        return true;
    }

    void Document::ResetEditorState()
    {
        if (m_editor)
            m_editor->ResetTransientState();
    }

    bool Document::CreateLayerAndActivate()
    {
        auto& layerManager = m_scene->GetLayerManager();
        auto ids = GetSortedLayerIDs(layerManager);
        std::string name = std::format("Layer {}", ids.size());

        auto cmd = std::make_unique<AddLayerCommand>(name);
        m_cmdStack->Execute(std::move(cmd), *m_scene);
        RefreshEditorAfterLayerChange();
        return true;
    }

    bool Document::CycleActiveLayer(int direction)
    {
        auto& layerManager = m_scene->GetLayerManager();
        auto ids = GetSortedLayerIDs(layerManager);
        if (ids.empty())
            return false;

        auto it = std::find(ids.begin(), ids.end(), layerManager.GetActiveLayerID());
        size_t index = (it != ids.end()) ? static_cast<size_t>(std::distance(ids.begin(), it)) : 0;

        if (direction > 0)
            index = (index + 1) % ids.size();
        else
            index = (index + ids.size() - 1) % ids.size();

        auto cmd = std::make_unique<SetActiveLayerCommand>(ids[index]);
        m_cmdStack->Execute(std::move(cmd), *m_scene);
        RefreshEditorAfterLayerChange();
        return true;
    }

    bool Document::ToggleActiveLayerVisibility()
    {
        auto& layerManager = m_scene->GetLayerManager();
        auto* layer = layerManager.GetLayer(layerManager.GetActiveLayerID());
        if (!layer)
            return false;

        auto cmd = std::make_unique<SetLayerVisibilityCommand>(layer->GetID(), !layer->IsVisible());
        m_cmdStack->Execute(std::move(cmd), *m_scene);
        RefreshEditorAfterLayerChange(true);
        return true;
    }

    bool Document::ToggleActiveLayerLock()
    {
        auto& layerManager = m_scene->GetLayerManager();
        auto* layer = layerManager.GetLayer(layerManager.GetActiveLayerID());
        if (!layer)
            return false;

        auto cmd = std::make_unique<SetLayerLockCommand>(layer->GetID(), !layer->IsLocked());
        m_cmdStack->Execute(std::move(cmd), *m_scene);
        RefreshEditorAfterLayerChange(true);
        return true;
    }

    bool Document::DeleteActiveLayer()
    {
        auto& layerManager = m_scene->GetLayerManager();
        const LayerID activeId = layerManager.GetActiveLayerID();
        if (activeId == Layer::DefaultLayerID)
        {
            ReportError("Cannot delete the default layer.");
            return false;
        }

        auto cmd = std::make_unique<DeleteLayerCommand>(activeId);
        m_cmdStack->Execute(std::move(cmd), *m_scene);
        RefreshEditorAfterLayerChange(true);
        return true;
    }

    void Document::RefreshEditorAfterLayerChange(bool cancelActiveTool)
    {
        if (m_editor)
            m_editor->RefreshSceneState(cancelActiveTool);
    }
}

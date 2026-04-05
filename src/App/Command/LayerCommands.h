#pragma once
#include "App/Abstractions/ICommand.h"
#include "App/Scene/Layer.h"
#include "App/Scene/Scene.h"
#include "Core/Entity/LineEntity.hpp"
#include <memory>
#include <string>
#include <vector>

namespace MiniCAD
{
    namespace LayerCommandDetail
    {
        inline std::unique_ptr<Layer> CloneLayer(const Layer& layer)
        {
            auto copy = std::make_unique<Layer>(layer.GetID(), layer.GetName());
            copy->SetColor(layer.GetColor());
            copy->SetVisible(layer.IsVisible());
            copy->SetLocked(layer.IsLocked());
            return copy;
        }
    }

    class AddLayerCommand : public ICommand
    {
    public:
        explicit AddLayerCommand(std::string name)
            : m_name(std::move(name))
        {}

        void Execute(Scene& scene) override
        {
            auto& layerManager = scene.GetLayerManager();
            m_previousActiveLayerID = layerManager.GetActiveLayerID();

            if (m_layerSnapshot)
                m_layerID = layerManager.AddLayer(LayerCommandDetail::CloneLayer(*m_layerSnapshot));
            else
                m_layerID = layerManager.AddLayer(m_name);

            auto* layer = layerManager.GetLayer(m_layerID);
            if (layer)
                m_layerSnapshot = LayerCommandDetail::CloneLayer(*layer);

            if (m_layerID != Layer::DefaultLayerID)
                layerManager.SetActiveLayerID(m_layerID);
        }

        void Undo(Scene& scene) override
        {
            if (m_layerID == Layer::DefaultLayerID)
                return;

            auto& layerManager = scene.GetLayerManager();
            auto* layer = layerManager.GetLayer(m_layerID);
            if (layer)
                m_layerSnapshot = LayerCommandDetail::CloneLayer(*layer);

            scene.RemoveLayer(m_layerID);
            layerManager.SetActiveLayerID(m_previousActiveLayerID);
        }

        std::string GetName() const override { return "AddLayer"; }

    private:
        std::string m_name;
        LayerID m_layerID = Layer::DefaultLayerID;
        LayerID m_previousActiveLayerID = Layer::DefaultLayerID;
        std::unique_ptr<Layer> m_layerSnapshot;
    };

    class SetActiveLayerCommand : public ICommand
    {
    public:
        explicit SetActiveLayerCommand(LayerID nextLayerID)
            : m_nextLayerID(nextLayerID)
        {}

        void Execute(Scene& scene) override
        {
            auto& layerManager = scene.GetLayerManager();
            m_previousLayerID = layerManager.GetActiveLayerID();
            layerManager.SetActiveLayerID(m_nextLayerID);
        }

        void Undo(Scene& scene) override
        {
            scene.GetLayerManager().SetActiveLayerID(m_previousLayerID);
        }

        std::string GetName() const override { return "SetActiveLayer"; }

    private:
        LayerID m_nextLayerID = Layer::DefaultLayerID;
        LayerID m_previousLayerID = Layer::DefaultLayerID;
    };

    class SetLayerVisibilityCommand : public ICommand
    {
    public:
        SetLayerVisibilityCommand(LayerID layerID, bool visible)
            : m_layerID(layerID)
            , m_newValue(visible)
        {}

        void Execute(Scene& scene) override
        {
            auto* layer = scene.GetLayerManager().GetLayer(m_layerID);
            if (!layer)
                return;

            m_oldValue = layer->IsVisible();
            layer->SetVisible(m_newValue);
        }

        void Undo(Scene& scene) override
        {
            auto* layer = scene.GetLayerManager().GetLayer(m_layerID);
            if (layer)
                layer->SetVisible(m_oldValue);
        }

        std::string GetName() const override { return "SetLayerVisibility"; }

    private:
        LayerID m_layerID = Layer::DefaultLayerID;
        bool m_newValue = true;
        bool m_oldValue = true;
    };

    class SetLayerLockCommand : public ICommand
    {
    public:
        SetLayerLockCommand(LayerID layerID, bool locked)
            : m_layerID(layerID)
            , m_newValue(locked)
        {}

        void Execute(Scene& scene) override
        {
            auto* layer = scene.GetLayerManager().GetLayer(m_layerID);
            if (!layer)
                return;

            m_oldValue = layer->IsLocked();
            layer->SetLocked(m_newValue);
        }

        void Undo(Scene& scene) override
        {
            auto* layer = scene.GetLayerManager().GetLayer(m_layerID);
            if (layer)
                layer->SetLocked(m_oldValue);
        }

        std::string GetName() const override { return "SetLayerLock"; }

    private:
        LayerID m_layerID = Layer::DefaultLayerID;
        bool m_newValue = false;
        bool m_oldValue = false;
    };

    class DeleteLayerCommand : public ICommand
    {
    public:
        explicit DeleteLayerCommand(LayerID layerID, LayerID fallbackLayerID = Layer::DefaultLayerID)
            : m_layerID(layerID)
            , m_fallbackLayerID(fallbackLayerID)
        {}

        void Execute(Scene& scene) override
        {
            auto& layerManager = scene.GetLayerManager();
            auto* layer = layerManager.GetLayer(m_layerID);
            if (!layer || m_layerID == Layer::DefaultLayerID)
                return;

            m_previousActiveLayerID = layerManager.GetActiveLayerID();
            m_layerSnapshot = LayerCommandDetail::CloneLayer(*layer);
            m_reassignedEntityIDs.clear();

            for (const auto& [id, obj] : scene.GetEntities())
            {
                if (!obj->IsKindOf<LineEntity>())
                    continue;

                const auto* line = static_cast<const LineEntity*>(obj.get());
                if (line->GetLayerID() == m_layerID)
                    m_reassignedEntityIDs.push_back(id);
            }

            scene.RemoveLayer(m_layerID, m_fallbackLayerID);
        }

        void Undo(Scene& scene) override
        {
            if (!m_layerSnapshot)
                return;

            auto& layerManager = scene.GetLayerManager();
            layerManager.AddLayer(LayerCommandDetail::CloneLayer(*m_layerSnapshot));

            bool restoredAnyEntity = false;
            for (auto id : m_reassignedEntityIDs)
            {
                auto* obj = scene.GetEntity(id);
                if (!obj || !obj->IsKindOf<LineEntity>())
                    continue;

                auto* line = static_cast<LineEntity*>(obj);
                if (line->GetLayerID() == m_fallbackLayerID)
                {
                    line->SetLayerId(m_layerID);
                    restoredAnyEntity = true;
                }
            }

            if (restoredAnyEntity)
                scene.MarkDirty();

            layerManager.SetActiveLayerID(m_previousActiveLayerID);
        }

        std::string GetName() const override { return "DeleteLayer"; }

    private:
        LayerID m_layerID = Layer::DefaultLayerID;
        LayerID m_fallbackLayerID = Layer::DefaultLayerID;
        LayerID m_previousActiveLayerID = Layer::DefaultLayerID;
        std::unique_ptr<Layer> m_layerSnapshot;
        std::vector<Object::ObjectID> m_reassignedEntityIDs;
    };
}

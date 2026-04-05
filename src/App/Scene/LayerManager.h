#pragma once
#include "App/Scene/Layer.h"
#include <atomic>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <functional>
#include "Serialization/ISerializer.h"

namespace MiniCAD
{
    class ISerializer;

    class LayerManager
    {
    public:
        LayerManager();

        LayerID AddLayer(const std::string& name);
        LayerID AddLayer(std::unique_ptr<Layer> layer);

        bool RemoveLayer(LayerID id);
        bool HasLayer(LayerID id) const;

        Layer* GetLayer(LayerID id);
        const Layer* GetLayer(LayerID id) const;

        std::vector<LayerID> GetAllLayerIDs() const;

        LayerID GetActiveLayerID() const { return m_activeLayerID; }
        void SetActiveLayerID(LayerID id);

        using ChangeCallback = std::function<void()>;
        void SetChangeCallback(ChangeCallback cb);

        void Serialize(ISerializer& s) const;
        void Deserialize(ISerializer& s);

    private:
        void NotifyChanged();
        void WireLayerCallbacks(Layer& layer);

        std::unordered_map<LayerID, std::unique_ptr<Layer>> m_layers;
        LayerID m_activeLayerID = Layer::DefaultLayerID;
        std::atomic<LayerID> m_nextID{ 1 };
        ChangeCallback m_onChange;
    };
}

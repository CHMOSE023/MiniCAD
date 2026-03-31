#pragma once 
#include "App/Picking/Picking.h"
#include "Core/Object/Object.hpp"
#include <unordered_set> 
#include <DirectXMath.h>
#include <App/Input/IInputHandler.h>

namespace MiniCAD
{
    class Scene;
    class CommandStack;
    class Camera;

    class Editor : public IInputHandler
    {
    public:
        Editor(Scene* scene, CommandStack* cmdStack);

        // ── IEventHandler ─────────────────────────────────────
        bool OnInput(const InputEvent& e) override; 

        // ── 实体操作 ──────────────────────────────────────────
        void AddLine(
            const DirectX::XMFLOAT3& start,
            const DirectX::XMFLOAT3& end,
            const DirectX::XMFLOAT4& color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));

        void DeleteSelected();

        // ── 选择集 ────────────────────────────────────────────
        const std::unordered_set<Object::ObjectID>& GetSelection() const
        {
            return m_selection;
        }
        void  ClearSelection() { m_selection.clear(); }
        bool  IsSelected(Object::ObjectID id) const
        {
            return m_selection.count(id) > 0;
        }

        // ── 相机（供 Picking 使用）────────────────────────────
        void SetCamera(Camera* camera) { m_camera = camera; }

    private:
        void OnMouseButtonDown(const InputEvent& e);
        void OnKeyDown(const InputEvent& e);

        // 借用
        Scene*        m_scene    = nullptr;
        CommandStack* m_cmdStack = nullptr;
        Camera*       m_camera   = nullptr;

        // 持有
        Picking                              m_picking;
        std::unordered_set<Object::ObjectID> m_selection;

        // ID 生成
        Object::ObjectID m_nextID = 1;

    };
}
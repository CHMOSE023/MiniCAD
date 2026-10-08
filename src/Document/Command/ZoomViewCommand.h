#pragma once
#include "Document/CommandStack/ICommand.h"
#include "Viewport/Camera.h"

namespace MiniCAD
{
    // 视图缩放 / 平移的撤销重做：只改相机，不改场景，也不让文档变成"已修改"。
    // 相机属于视口（随文档切换保存 / 还原），命令只保存前后两个状态。
    class ZoomViewCommand : public ICommand
    {
    public:
        ZoomViewCommand(Camera& camera, const CameraState& before, const CameraState& after, std::string name = "缩放")
            : m_camera(camera), m_before(before), m_after(after), m_name(std::move(name))
        {
        }

        bool Execute(Scene&) override { m_camera.SetState(m_after); return true; }
        void Undo(Scene&) override    { m_camera.SetState(m_before); }
        std::string GetName() const override { return m_name; }
        bool AffectsDocument() const override { return false; }

    private:
        Camera&     m_camera;
        CameraState m_before;
        CameraState m_after;
        std::string m_name;
    };
}

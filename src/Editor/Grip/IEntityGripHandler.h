#pragma once
#include "Core/Entity/Entity.hpp"
#include "Core/Object/Object.hpp"
#include "Core/Math/Point3.hpp"
#include "Document/CommandStack/CommandStack.h"
#include <memory>
#include <vector>

namespace MiniCAD
{
    // 前置声明
    struct Grip;
    class  Overlay;
    // ─────────────────────────────────────────────
    // IGripDragState
    //
    // 每种 Entity 的拖拽状态基类
    //
    // 例如：
    //   LineDragState
    //   CircleDragState
    //   RectangleDragState
    // ─────────────────────────────────────────────
    class IGripDragState
    {
    public:
        virtual ~IGripDragState() = default;
    };

    // ─────────────────────────────────────────────
    // IEntityGripHandler
    //
    // 每种 Entity 一个 Handler（Line / Circle / Point …）
    //
    // 职责划分：
    //   BuildGrips    — 构建夹点列表
    //   BeginDrag     — 冻结初始快照
    //   UpdateDrag    — 更新几何 + 同步夹点坐标（预览跟手）
    //   DrawPreview   — 向 Overlay 写入拖拽期间的临时几何（可选覆盖）
    //   EndDrag       — 提交到 CommandStack
    //   CancelDrag    — 还原 Entity 到快照
    //
    // GripEditor 只负责：
    //   Input Routing / HitTest / Hover / 生命周期 / 绘制夹点方块
    // ─────────────────────────────────────────────
    class IEntityGripHandler
    {
    public:
        virtual ~IEntityGripHandler() = default;

    public:
        // ─────────────────────────────────────────
        // 构建实体夹点
        // ─────────────────────────────────────────
        virtual void BuildGrips(Entity* entity, std::vector<Grip>& outGrips) = 0;

        // ─────────────────────────────────────────
        // 开始拖拽 — 返回每次 drag 独立状态（快照）
        // ─────────────────────────────────────────
        virtual std::unique_ptr<IGripDragState> BeginDrag(Entity* entity, const Grip& activeGrip) = 0;

        // ─────────────────────────────────────────
        // 拖拽更新
        //
        // 职责：
        //   1. 按 activeGrip.GripType 修改 Entity 几何
        //   2. 同步 grips 中属于该 Entity 的夹点坐标
        //      （使夹点在拖拽过程中实时跟手）
        // ─────────────────────────────────────────
        virtual void UpdateDrag(Entity* entity, IGripDragState* dragState, const Grip& activeGrip, const Math::Point3& worldPos, std::vector<Grip>& grips) = 0;

        // ─────────────────────────────────────────
        // 绘制拖拽预览（可选覆盖）
        //
        // 每帧由 GripEditor::Draw() 在拖拽期间调用。
        // 向 Overlay 写入临时几何（如辅助线、尺寸标注等）。
        // 默认不绘制任何内容。
        // ─────────────────────────────────────────
        virtual void DrawPreview(Entity* entity, IGripDragState* dragState, const Grip& activeGrip, Overlay& overlay) {}

        // ─────────────────────────────────────────
        // 拖拽结束 — 将操作推入 CommandStack
        //
        // 默认空实现（不可 undo 的场景可不覆盖）
        // ─────────────────────────────────────────
        virtual void EndDrag(Entity* entity, IGripDragState* dragState, CommandStack& cmdStack) {}

        // ─────────────────────────────────────────
        // 取消拖拽 — ESC / 右键，恢复快照
        // ─────────────────────────────────────────
        virtual void CancelDrag(Entity* entity, IGripDragState* dragState) = 0;
    };
}

#pragma once
#include <memory>

// 平台无关的 CAD 引擎入口
// 应用层职责：窗口管理、输入采集、渲染
// 库的职责：文档管理、工具系统、几何计算、顶点输出

namespace MiniCADLib
{
    class MiniCADEngine
    {
    public:
        MiniCADEngine();
        ~MiniCADEngine();

        // 初始化（传入视口初始尺寸）
        void Init(float viewportW, float viewportH);

        // 视口尺寸变化时调用
        void Resize(float w, float h);

        // --- 以下接口随模块迁入后逐步启用 ---

        // 应用层每帧推入输入事件（从 Win32 / JS / 其他平台采集后调用）
        // bool InjectInput(const InputEvent& event);

        // 应用层每帧拉取顶点数据，传给自己的渲染器
        // const ViewState& BuildViewState();

        // 文档管理（新建 / 打开 / 保存 / 撤销等）
        // DocumentManager& GetDocumentManager();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}

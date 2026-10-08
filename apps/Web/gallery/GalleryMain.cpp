// MiniGUI 网页原型：在浏览器里运行控件展示（Gallery），验证 WebGL2 后端、浏览器输入与字体
#include "Host/WebHost.h"
#include "WebGL2/WebGL2Backend.h"
#include "Core/UIContext.h"
#include "Platform/Web/WebFonts.h"
#include "Platform/Web/WebInput.h"
#include "Gallery.h"
#include <GLES3/gl3.h>
#include <cstdio>
#include <exception>
#include <memory>

using namespace MiniGUI;

namespace
{
    struct App
    {
        MiniCAD::WebHost               host;
        std::unique_ptr<WebGL2Backend> backend;
        std::unique_ptr<UIContext>     ui;
        std::unique_ptr<WebInput>      input;
        unsigned long long             frames = 0;
    };

    App* g_app = nullptr;

    void RenderFrame(App& app)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, app.host.GetFramebufferWidth(), app.host.GetFramebufferHeight());
        glClearColor(0x1E / 255.0f, 0x1F / 255.0f, 0x22 / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        app.ui->Render();
        app.input->Sync();      // 文字焦点变化时切换 textarea，候选窗跟随光标
        ++app.frames;
    }
}

int main()
{
    try
    {
        g_app = new App();      // 页面关闭前一直存在
        App& app = *g_app;
        if (!app.host.Initialize())
            return 1;

        app.backend = std::make_unique<WebGL2Backend>();
        app.ui      = std::make_unique<UIContext>(app.backend.get());
        app.input   = std::make_unique<WebInput>(app.ui.get(), app.host.GetCanvasSelector().c_str());
        if (!LoadWebUIFonts(app.ui->GetTextSystem()))
            std::printf("警告：没有找到界面字体 %s，文字将无法显示\n", kWebUIFontPath);

        app.ui->SetRedrawCallback([&app] { app.host.RequestFrame(); });
        app.host.SetOnResize([&app](float w, float h, float scale) { app.ui->SetDisplaySize(Vec2{ w, h }, scale); });
        app.host.SetOnFrame ([&app] { RenderFrame(app); });
        app.ui->SetDisplaySize(Vec2{ app.host.GetLogicalWidth(), app.host.GetLogicalHeight() }, app.host.GetPixelScale());

        BuildGallery(*app.ui, "/assets");
        app.host.RequestFrame();
    }
    catch (const std::exception& e)
    {
        std::printf("%s\n", e.what());
        return 1;
    }
    // 返回后运行时继续存在（-sEXIT_RUNTIME=0），由浏览器事件驱动
    return 0;
}

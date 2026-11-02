// MiniCAD 网页版入口：界面与桌面版相同（MiniGUI 绘制），经 WebGPU 渲染，浏览器不支持时用 WebGL2
#include "Host/WebApp.h"
#include <cstdio>
#include "MiniI18n/I18n.h"

int main()
{
    // 页面关闭前一直存在：main 返回后运行时继续由浏览器事件驱动（-sEXIT_RUNTIME=0）
    auto* app = new MiniCAD::WebApp();
    if (!app->Initialize())
    {
        std::printf("%s\n", Tr("MiniCAD：初始化失败（浏览器需要支持 WebGPU 或 WebGL2）").c_str());
        return 1;
    }
    return 0;
}

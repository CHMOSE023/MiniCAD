// MiniCADAndroid：安卓版入口（NativeActivity 经 native_app_glue 调用 android_main，在它自己的线程上）
#include "Host/AndroidApp.h"
#include "GUI/Language.h"
#include "Core/Log.h"
#include <android_native_app_glue.h>

using namespace MiniCAD;

void android_main(android_app* app)
{
    LOG_INFO("MiniCAD android_main");
    AndroidApp host(app);
    InitLanguage(host, {});     // 按"设置 > 语言"，没设过时按系统语言
    host.Run();
}

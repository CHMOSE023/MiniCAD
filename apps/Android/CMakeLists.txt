# ── MiniCAD 安卓版：NativeActivity 宿主（窗口、事件、EGL / GLES3）+ 共用的主窗口界面（MiniCADApp）──
# 由 apps/Android 的 Gradle 工程经 externalNativeBuild 调用仓库根的 CMakeLists.txt，只编这个目标（见 docs/Android/PLAN.md）

if(NOT MINICAD_LINUX_GLES3)
    message(FATAL_ERROR "MiniCADAndroid：NDK 里没有找到 OpenGL ES 3")
endif()

# NDK 自带的 native_app_glue（C 源码）：在自己的线程上调用 android_main，命令与输入经 ALooper 交给它
enable_language(C)
set(MINICAD_APP_GLUE "${ANDROID_NDK}/sources/android/native_app_glue")
add_library(MiniCADAppGlue STATIC "${MINICAD_APP_GLUE}/android_native_app_glue.c")
target_include_directories(MiniCADAppGlue PUBLIC "${MINICAD_APP_GLUE}")
target_link_libraries(MiniCADAppGlue PUBLIC android log)

add_library(MiniCADAndroid SHARED
    src/Host/AndroidMain.cpp
    src/Host/AndroidApp.cpp
    src/Host/AndroidInput.cpp
)

target_include_directories(MiniCADAndroid PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(MiniCADAndroid PRIVATE
    MiniCADApp
    MiniCAD
    MiniGUI
    MiniCADRender
    MiniCADAppGlue
    android
    EGL
    log
)

# NativeActivity 按名字找 ANativeActivity_onCreate（在静态库里，不显式引用的话链接器会把它丢掉）
target_link_options(MiniCADAndroid PRIVATE "-Wl,--undefined=ANativeActivity_onCreate")

# 只导出 NativeActivity 与 JNI 要找的入口，其余符号（静态库里两万多个）隐藏，库小 2～3 MB
set(MINICAD_ANDROID_EXPORTS "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
target_link_options(MiniCADAndroid PRIVATE "-Wl,--version-script=${MINICAD_ANDROID_EXPORTS}" "-Wl,--gc-sections")
set_target_properties(MiniCADAndroid PROPERTIES LINK_DEPENDS "${MINICAD_ANDROID_EXPORTS}")

# 三维实体内核（SMLib，见 docs/Solid/PLAN.md）
if(MINICAD_WITH_SOLID)
    target_link_libraries(MiniCADAndroid PRIVATE MiniCADSolid)
    target_compile_definitions(MiniCADAndroid PRIVATE MINICAD_WITH_SOLID)
endif()

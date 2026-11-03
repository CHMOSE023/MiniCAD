# 安卓版

> 状态：开发中（2026-11-03 起）。本机环境已装在 `D:\dev`（§1）；阶段 1～3 的代码已写，APK 在模拟器（API 36 平板，GLES3）上验证过：启动、新建、点按 / 拖动 / 长按、键入命令、返回键、切后台再回来；双指手势、中文输入法、真机还没试。
> 定位：新宿主 `apps/Android`（目标 `MiniCADAndroid`，打包成 APK），界面与 Windows / 网页 / Linux 版共用 `apps/Common`。
> 已定：调用安卓原生接口（NativeActivity + NDK 的 `native_app_glue`，不依赖 AndroidX）；渲染复用 `VulkanGraphicsDevice` / `GLES3GraphicsDevice`。
> 先做平板（≥ 8 英寸）和接了鼠标键盘的手机，小屏手机简化浏览界面。

---

## 1. Windows开发环境

| 程序 | 版本 | 用途 | 说明 |
|---|---|---|---|
| **Android Studio** | Rabbit 2（2026.2.2） | 打包 APK、装到设备、看日志（Logcat）、调试原生代码 | 自带 JDK（JBR 21）、SDK 管理器、模拟器、adb。只想用命令行也可以只装 command-line tools，再装下面几项 |
| Android SDK Platform | API 36 | 编译用的 SDK | Android Studio → SDK Manager → SDK Platforms |
| Android SDK Build-Tools | 36.1.0（AGP 另会自动装 36.0.0） | 打包、签名 | SDK Tools 页 |
| Android SDK Platform-Tools | 最新 | `adb`：装 APK、看日志 | SDK Tools 页 |
| **NDK (Side by side)** | r29（29.0.14206865） | 编译 C++20 代码（clang、libc++、Vulkan / EGL / GLES 头文件与库） | SDK Tools 页，勾 "Show Package Details" 选版本 |
| CMake（SDK 里的） | 3.31.6 | Gradle 调用它编原生代码 | SDK Tools 页；项目要求 CMake 3.20+，自带 Ninja |
| Android Emulator + 系统映像 | API 36 x86_64（Google APIs） | 没有真机时试运行 | 模拟器的 GLES3 走主机显卡；Vulkan 1.3 不一定有，主要测 GLES3 回退 |
| 着色器编译器 | NDK 自带的 `glslc`，或 Vulkan SDK 的 `glslangValidator` | 构建时把 GLSL 编成 SPIR-V | 现在 `cmake/Render.cmake` 只找 glslangValidator，要加上 glslc（§5） |
| JDK | 21（`D:\dev\jdk-21.0.12.1+1`，本机原有） | sdkmanager、Gradle | Android Studio 自带的 JBR 也可以 |
| Gradle | 9.8.2（wrapper 进仓库） | 构建 | Android Gradle Plugin 9.5.0；`D:\dev\gradle-9.8.2` 只用来生成 wrapper |

本机布局：SDK 在 `D:\dev\Android\Sdk`（用户环境变量 `ANDROID_HOME`，`platform-tools`、`emulator` 已加进用户 PATH），Android Studio 在 `D:\dev\android-studio`，
下载的安装包在 `D:\dev\downloads`。模拟器 `MiniCAD_API36`（Pixel Tablet，2560×1600，WHPX 加速）在 `D:\dev\Android\avd`（用户环境变量 `ANDROID_AVD_HOME`），启动：`emulator -avd MiniCAD_API36`。`apps/Android/local.properties`（不进仓库）写 `sdk.dir`。


## 2. 结构

```
apps/Android/
  settings.gradle.kts、build.gradle.kts、gradle/wrapper/   Gradle 工程（wrapper 进仓库）
  app/build.gradle.kts         externalNativeBuild → 仓库根的 CMakeLists.txt；minSdk 29、targetSdk 36；abiFilters arm64-v8a（调试版加 x86_64 给模拟器）
  app/src/main/AndroidManifest.xml
  app/src/main/java/io/github/minicad/MiniCADActivity.java   继承 NativeActivity；软键盘、返回手势、文件选择、保存、打开网址
  app/src/main/java/io/github/minicad/MiniCADInputView.java  看不见的 1×1 视图，输入法经它的 InputConnection 把组合串与确认的文字交给原生代码（§4.3）
  （资源）                    仓库的 assets/ 直接作为 APK 的 assets 目录；Gradle 任务另生成清单 minicad-assets.lst（路径与字节数），启动时按它解包
  src/Host/AndroidApp.h/.cpp   AppPlatform 实现
  src/Host/AndroidInput.h/.cpp 输入队列的 AInputEvent（触摸 / 鼠标 / 键盘）与输入法事件 → UIContext（也可以放进 MiniGUI 的 Platform/Android）
  src/Host/AndroidMain.cpp     android_main 入口
  CMakeLists.txt               NDK 的 native_app_glue 编成静态库 MiniCADAppGlue；add_library(MiniCADAndroid SHARED …)，CMake 只在 ANDROID 时进这个目录
```

- 原生代码编成一个 `libMiniCADAndroid.so`，核心库、MiniGUI、MiniDWG、MiniI18n、`apps/Common`、渲染后端照常静态链入
- 根 `CMakeLists.txt` 加 `if(ANDROID) add_subdirectory(apps/Android)`；`cmake/Render.cmake` 的平台段加安卓：Vulkan + GLES3（NDK 自带头文件与 `libvulkan.so` / `libEGL.so` / `libGLESv3.so`），不编 D3D / WebGPU
- 测试程序、`tools/` 在安卓构建里不编

### 2.1 为什么用 NativeActivity，不用 GameActivity

- 两者都能纯 C++ 写（`android_main` + 事件循环，见 `android_native_app_glue`）
- NativeActivity 在系统框架里（`android.app.NativeActivity`），glue 是 NDK 自带的 C 源码：不依赖 AndroidX、Prefab 和要下载的库，构建只要 SDK + NDK
- 代价是没有软键盘与输入法的接口，自己补一小段 Java（§4.3）：`MiniCADInputView` 提供 `InputConnection`，三个 native 方法把组合串、确认的文字、输入法的删除交给原生代码；弹出 / 收起软键盘是 `MiniCADActivity.showKeyboard()` / `hideKeyboard()`，原生代码经 JNI 调用
- 按键事件里没有字符（`KeyCharacterMap` 只在 Java 端）：不在文本框时实体键盘按美式布局换出命令行用的字符；文本框里的文字一律经输入法

## 3. 平台服务（`AppPlatform`）

| 接口 | 安卓的做法 |
|---|---|
| 图形 | 默认 Vulkan，不行时 EGL + GLES3（同 Linux）；窗口是 `ANativeWindow`（§5） |
| `HasWindowControls()` | false（全屏应用，没有标题栏按钮） |
| `CreateFloatingWindow` | 返回空，面板在主窗口里浮动（同网页版） |
| `GetResourceDir` | APK 的 assets 不是普通文件，而字体、图标、`ui/minicad_ui.json` 都按路径读：第一次启动（或版本号变了）时把 assets 解到应用私有目录 `files/res`，返回它 |
| `GetUserDataDir` | `files/user` |
| `GetAppDir` | 也是 `files/user`（放 `recent.txt`）：资源目录 `files/res` 在资源换版本时整个重解，放在那里会丢 |
| `GetSystemFontDirs` | `/system/fonts`（有 NotoSansCJK，中文字体不用自带） |
| `GetSystemLanguage` | `AConfiguration_getLanguage` / `getCountry` |
| `PickFile` / `PickDrawingFiles` | Java 里 `ACTION_OPEN_DOCUMENT`（异步，正好符合接口）；拿到的是 `content://` 地址，复制到 `cache/open/` 再把路径交回。三星 Android 13 的"下载"（`raw:` 文档）只读打开会被媒体库以 `SecurityException` 拒绝，退回读写模式打开（`MiniCADActivity.openForRead`）；都读不了时弹提示（原生代码传入翻译好的"无法打开 {0}"） |
| `PickFolder`（外部参照） | `ACTION_OPEN_DOCUMENT_TREE`；目录树也只能经 `content://` 访问，按需要的文件名复制到缓存目录（接口本来就传了 `fileNames`，网页版也这样用） |
| 保存 / 另存为 | `HasSaveDialog()` 为 false（同网页版，用界面里的另存为对话框选名字和格式），先存到 `files/docs/`，`OnDocumentSaved` 里再用 `ACTION_CREATE_DOCUMENT` 让用户选存到哪里（相当于网页版的"下载"）；从 `content://` 打开的文件记住地址，下次保存直接写回 |
| `OpenUrl` | `Intent.ACTION_VIEW` |
| `HttpGet` / 检查更新 | `GetReleaseAssetSuffix()` 返回空，不检查更新（以后上架应用商店由商店更新） |
| `RunInBackground` / `Post` | `std::thread`；完成回调经 `ALooper` 的管道唤醒界面线程（同 Linux 的做法） |
| `ReleaseFreeMemory` | 图形设备的释放；bionic 没有 `malloc_trim`，用 `mallopt(M_PURGE, 0)` |
| `RasterizeOle` | 不做（同 Linux） |

## 4. 安卓特有的问题

### 4.1 生命周期

- 切到后台时系统销毁窗口（`APP_CMD_TERM_WINDOW`），回来再给一个新的（`APP_CMD_INIT_WINDOW`）：设备、渲染器、纹理留着，只重建窗口表面。`IGraphicsDevice` 要加一对接口（例如 `ReleaseWindow()` / `AttachWindow(const NativeWindow&, int, int)`）：Vulkan 销毁并重建表面与交换链，GLES3 换 EGL 窗口表面（上下文不动）
- 后台时进程随时会被杀：`APP_CMD_PAUSE` / `SAVE_STATE` 时把未保存的文档存一份到 `files/recover/`，下次启动询问是否恢复（桌面版没有这个功能，需要在 `apps/Common` 里加一个与平台无关的"恢复"入口）
- 屏幕旋转：清单里 `configChanges` 声明自己处理，只当成改尺寸，不重建 Activity
- 原生库由 NativeActivity 自己 `dlopen`，不登记到 Java 的类加载器，Java 的 `native` 方法找不到实现（`UnsatisfiedLinkError`）：`MiniCADActivity` 的静态块里再 `System.loadLibrary("MiniCADAndroid")` 一次

### 4.2 触摸

现在的输入全按鼠标和键盘设计。手势在宿主里识别，再换成 `UIContext` 已有的指针事件，`apps/Common` 和 `Editor` 不用改：

| 手势 | 换成 |
|---|---|
| 单指点按 | 左键按下 + 抬起 |
| 单指拖动 | 左键拖动（框选、拖夹点、画线） |
| 长按（500 ms 不动） | 右键（右键菜单） |
| 双指捏合 | 在两指中点处滚轮缩放（按比例换成滚轮格数） |
| 双指平移 | 中键拖动（平移视图） |
| 鼠标、触控笔（`AMOTION_EVENT_TOOL_TYPE_MOUSE` / `STYLUS`） | 按真实的按键和悬停处理，与桌面一样 |

- 触摸没有悬停：CAD 视口的十字光标、捕捉标记要在手指按下时才显示；手指抬起（或触摸被取消）时发 `UIContext::PointerLeave`，不留悬停高亮和按钮提示；手指挡住落点，画图时需要"偏移光标"（以后的改进，不在第一版）
- 界面元素对手指偏小：`DisplayScale` 取屏幕密度（`AConfiguration_getDensity / 160`），触摸设备上再乘一个系数（例如 1.25，设置里可调）

### 4.3 键盘与软键盘

- 实体键盘：`AKeyEvent` 的键码（`AKEYCODE_*`）换成 `MiniGUI::Key`；音量键等不认识的键交还系统
- 软键盘：焦点进文本框（`UIContext::WantsTextInput()`）时原生代码调 `MiniCADActivity.showKeyboard()`，它把焦点给 `MiniCADInputView` 并弹出软键盘，离开时 `hideKeyboard()`
- 输入法：`MiniCADInputView` 的 `InputConnection` 不保存文字（文本框的内容由 MiniGUI 自己管），只转发三种事件——`setComposingText` → `nativeOnComposition`（空串 = 组合取消）、`commitText` / `finishComposingText` → `nativeOnCommit`、`deleteSurroundingText` → `nativeOnDelete`（换成退格 / Delete 键）；软键盘上的回车经 `sendKeyEvent` 走输入队列。这些方法在 Java 的 UI 线程上调用，宿主 `AndroidApp::PostInput` 转到 `android_main` 的线程再交给 `UIContext`
- 返回键 / 返回手势：清单开了 `enableOnBackInvokedCallback`，`OnBackInvokedCallback`（API 33 以下是 `onBackPressed`）→ `nativeOnBack` → Esc；原生代码的输入队列不处理 `AKEYCODE_BACK`，免得发两次
- 命令行：视口里直接键入命令在没有实体键盘时做不到，状态栏上加一个"键盘"按钮，点了把焦点放到命令行并弹出软键盘
- 软键盘、状态栏、手势条、挖孔：targetSdk 35 起窗口强制铺满整个屏幕（Android 11 起我们也这样设），`MiniCADActivity` 量出这些占用的边（`WindowInsets`）经 `nativeOnInsets` 交给宿主，`UIContext::SetSafeArea` 让主界面只排在其余范围里（弹层、悬浮提示仍铺满）；软键盘弹出时命令行随之上移
- 输入法视图只在文本框有焦点时可获得焦点（`MiniCADInputView.setActive`），否则窗口重新获得焦点（切回前台）时系统会把焦点给它，输入法自己弹出来

### 4.4 Vulkan 版本

`VulkanContext` 现在要求 Vulkan 1.3（动态渲染）。安卓上：Android 14 起新出厂的设备才要求 1.3，较老的设备是 1.1。做法：1.1 / 1.2 的设备有 `VK_KHR_dynamic_rendering` 扩展时也用 Vulkan，否则用 GLES3（GLES 3.2 所有设备都有）。着色器的 `--target-env vulkan1.3` 相应降到 `vulkan1.1`

### 4.5 小屏手机：只浏览的紧凑布局

手机竖屏只有约 340 dp 宽（横屏约 750 × 340 dp），桌面版的功能区、停靠面板放不下。定下的做法：**手机上只浏览，不提供绘图与修改**；以后再加批注（云线、文字批注、测量标注等），批注单独存放，不改图纸本身。

- 布局：`assets/ui/minicad_ui.json` 的 `layouts.compact`：标题栏（首页、打开、文档标签）、铺满的绘图区（视图方块、导航条，不要命令行）、状态栏（布局标签）；没有功能区和特性 / 图层停靠面板
- 全屏：右侧导航条最下面的 `view.fullScreen`（`AppPlatform::SetFullScreen`）隐藏状态栏和导航栏（`WindowInsetsController`，Android 10 用 `SYSTEM_UI_FLAG_IMMERSIVE_STICKY`），从屏幕边缘滑动临时显示；返回手势先退出全屏（`MiniCADActivity.onBackGesture` → `nativeOnFullScreenExited`），不全屏时才当 Esc
- 绘图区界面（`MainFrame::ApplyCompactChrome`）：视图方块缩到 88（完整布局 140）；导航条在方块以下的高度里垂直居中，只留放大、缩小、全部显示与全屏；文档标签缩窄的下限从 120 降到 96（`MiniGUI::TabView::SetMinTabWidth`），竖屏标题栏里能并排两个文档，再多才滚动；两个以上文档时总是有"⌄"列出全部（`MiniGUI::TabView::SetAlwaysShowListButton`）（平移、窗口缩放、三维视图要靠左键，只浏览里用不了）；状态栏只留"细线"，读取进度只显示进度条（不显示"正在打开 文件名"）；文档、布局标签条不显示"+"；不画十字光标与拾取框（`Editor::SetCursorHidden`）。起始页的返回行不画 Esc 键帽，最近列表第二行是修改时间（文件是缓存里的副本，目录没有意义）
- 切换：平台允许（`AppPlatform::SupportsCompactLayout`，目前只有安卓版）且逻辑尺寸宽 < 600 或高 < 480 时用紧凑布局，宽 ≥ 680 且高 ≥ 540 时回到完整布局（中间留一段，旋转、软键盘弹出时不来回跳）；平板始终是完整布局
- 只浏览模式（紧凑布局下）：单指拖动 = 平移、双击 = 缩放全图、双指 = 缩放 / 平移；点按、长按不选对象，不出夹点；键盘不进命令行、不执行快捷键；切进紧凑布局时结束进行中的命令、清空选择
- 之后按顺序加：图层开关（底部拉出的面板）、查找文字、测量距离 / 面积、批注；批注是只浏览模式里**允许的少数工具**，所以只浏览不做成"所有编辑一律禁止"，而是由 `MainFrame` 决定紧凑布局下哪些输入、命令能用

## 5. 构建

- Gradle 调 CMake：`-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static`，工具链文件由 Gradle 传入
- `cmake/Render.cmake`：安卓下找 NDK 的 `glslc`（`find_program(… NAMES glslangValidator glslang glslc)`），glslc 的参数与 glslangValidator 不同（输出 C 数组要 `-mfmt=c`），生成头文件的命令分两种写
- SMLib（`3rd/SMLib`）与 MiniDWG 第一次用 NDK 的 clang 编译，可能要修一些 GCC / MSVC 才接受的写法；其他库已经在 GCC 13 和 Emscripten 的 clang 上编过，问题应该不多
- 资源：`app/build.gradle.kts` 直接把仓库的 `assets/` 作为 APK 的 assets 目录；`preBuild` 前的任务 `minicadAssetList` 生成清单 `minicad-assets.lst`
- native_app_glue：`apps/Android/CMakeLists.txt` 从 `${ANDROID_NDK}/sources/android/native_app_glue` 编成静态库，链接时 `-Wl,--undefined=ANativeActivity_onCreate` 保住入口（NativeActivity 按名字找它）
- 签名：调试版用本机的调试密钥；发布版见 §6
- 输出目录：都在仓库的 `out/android`（与 `out/debug`、`out/release` 并列）——APK 在 `out/android/app/outputs/apk/<debug|release>/`，CMake 的中间文件在并列的 `out/android/cxx`（不能放进 app 的构建目录，`clean` 会删掉），根工程的在 `out/android/root`；源码目录里只剩 Gradle 自己的缓存 `.gradle/`
- 命令行构建：`apps/Android` 里 `gradlew assembleDebug`（或 `gradlew installDebug` 编完直接装到连着的设备），手动装 `adb install -r out/android/app/outputs/apk/debug/app-debug.apk`（在仓库根运行），看日志 `adb logcat -s MiniCAD`（`Core/Log.h` 在安卓下输出到 `__android_log_write`）

## 6. CI 与发布

- 任务 `Android (arm64)`（`.github/workflows/build.yml`，ubuntu-24.04 + Temurin JDK 21，用机器上已有 SDK 的 `sdkmanager` 装 Platform / Build-Tools / NDK / CMake，版本在 workflow 的 `ANDROID_*` 变量），`gradlew assembleRelease` 编 arm64 的 release APK，作为 `MiniCAD-<标签>-android-arm64.apk` 随 Release 上传
- 签名：`app/build.gradle.kts` 读环境变量 `MINICAD_KEYSTORE`（密钥库路径）、`MINICAD_KEYSTORE_PASSWORD`、`MINICAD_KEY_ALIAS`、`MINICAD_KEY_PASSWORD`，没设时用调试密钥。CI 从仓库 secrets `ANDROID_KEYSTORE_BASE64`（密钥库的 base64）解出密钥库，密码与别名取 `ANDROID_KEYSTORE_PASSWORD`、`ANDROID_KEY_ALIAS`、`ANDROID_KEY_PASSWORD`；没有这些 secrets 时 CI 用当次机器的调试密钥，每次发布签名都不同，已装的设备要先卸载才能装新版——正式发布前要配好
- 不设为合并必需项（同 Linux 任务），稳定后再议
- 检查更新：启动时 `MainFrame::CheckForUpdates` 经 `AndroidApp::HttpGet`（`MiniCADActivity.httpGet`，Java 的 HttpURLConnection，要 INTERNET 权限）取 GitHub 最新 Release，找 `-android-arm64.apk` 附件；比当前新时弹出提示（`AppPlatform::PromptsForUpdates`）："下载"交给浏览器下载 APK，下完点通知安装；"忽略此版本"记进用户设置 `ignoredUpdate`，这个版本不再提示
- 版本号：`versionName` 取根 `CMakeLists.txt` 的 `MINICAD_VERSION`，`versionCode` 由它换算（例如 0.7.0 → 700）

## 7. 阶段

| 阶段 | 内容 | 验收 |
|---|---|---|
| 1 | Gradle 工程 + 原生库能编过（含 SMLib、MiniDWG）；`android_main` 建窗口、GLES3 清屏 | 模拟器和真机上显示纯色窗口，Logcat 有日志 |
| 2 | `AndroidApp` 实现 `AppPlatform`；资源解包、字体、`MainFrame` 跑起来；Vulkan 优先、GLES3 回退；切后台 / 回来重建表面 | 起始页、新建图纸、画线；切后台再回来不黑屏、不崩溃 |
| 3 | 触摸手势、实体键盘、软键盘与输入法 | 平板上用手指画图、缩放、平移；中文输入图层名 |
| 4 | 打开 / 保存（`content://`）、拖入外部参照目录、崩溃恢复 | 从"文件"应用打开 DWG，改完保存回原处 |
| 5 | CI、发布包、文档（README、AGENTS.md、release-notes） | Release 页有 APK，干净设备上能装能用 |


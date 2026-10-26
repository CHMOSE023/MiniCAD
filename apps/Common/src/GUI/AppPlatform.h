#pragma once
#include "Render/IRenderBackend.h"
#include "Import/CadExchange.h"
#include "Core/Ole/OleObject.h"
#include <cctype>
#include <iterator>
#include <functional>
#include <string>
#include <vector>
#include <utility>

namespace MiniGUI
{
    class TextSystem;
}

namespace MiniCAD
{
    class IRenderer;
    class IRenderTarget;
    namespace Solid { class ISolidKernel; }

    // 选择文件的用途（决定筛选的扩展名）
    enum class FileKind
    {
        Drawing,    // .mcad / .dwg / .dxf / .json
        Image,      // .png / .jpg / .bmp / .tga / .gif
        NodeGraph,  // .mcnode（节点图，JSON 文本）/ .json
    };

    // 另存为可选的格式（桌面版系统对话框的筛选器与网页版另存为对话框共用，顺序即列表顺序）
    struct SaveFormat
    {
        const char*    label;
        const char*    ext;         // 小写，带点
        CadSaveVersion version;     // 只对 .dwg / .dxf 有意义
    };
    // DWG 没有 2007：MiniDWG 不能写 2007 格式的 DWG（见 CadSaveVersionSupported）
    inline constexpr SaveFormat kSaveFormats[] =
    {
        { "MiniCAD 文档 (*.mcad)",          ".mcad", kDefaultCadSaveVersion },
        { "AutoCAD 2018 图形 (*.dwg)",      ".dwg",  CadSaveVersion::R2018 },
        { "AutoCAD 2013/2014 图形 (*.dwg)", ".dwg",  CadSaveVersion::R2013 },
        { "AutoCAD 2010 图形 (*.dwg)",      ".dwg",  CadSaveVersion::R2010 },
        { "AutoCAD 2004 图形 (*.dwg)",      ".dwg",  CadSaveVersion::R2004 },
        { "AutoCAD 2000 图形 (*.dwg)",      ".dwg",  CadSaveVersion::R2000 },
        { "AutoCAD 2018 DXF (*.dxf)",       ".dxf",  CadSaveVersion::R2018 },
        { "AutoCAD 2013/2014 DXF (*.dxf)",  ".dxf",  CadSaveVersion::R2013 },
        { "AutoCAD 2010 DXF (*.dxf)",       ".dxf",  CadSaveVersion::R2010 },
        { "AutoCAD 2007 DXF (*.dxf)",       ".dxf",  CadSaveVersion::R2007 },
        { "AutoCAD 2004 DXF (*.dxf)",       ".dxf",  CadSaveVersion::R2004 },
        { "AutoCAD 2000 DXF (*.dxf)",       ".dxf",  CadSaveVersion::R2000 },
        { "JSON 文件 (*.json)",             ".json", kDefaultCadSaveVersion },
    };

    // 文件名（不区分大小写）与版本对应的另存格式下标：扩展名相同时优先版本一致的，
    // 版本在该格式下不可选（如 2007 DWG）时取该格式的默认版本（2004）；扩展名不认识返回 -1
    inline int FindSaveFormat(const std::string& fileName, CadSaveVersion version)
    {
        auto hasExt = [&fileName](const char* ext)
        {
            const size_t n = std::char_traits<char>::length(ext);
            if (fileName.size() <= n)
                return false;
            for (size_t i = 0; i < n; ++i)
                if (std::tolower(static_cast<unsigned char>(fileName[fileName.size() - n + i])) != ext[i])
                    return false;
            return true;
        };
        int byExt = -1, byDefault = -1;
        for (int i = 0; i < static_cast<int>(std::size(kSaveFormats)); ++i)
        {
            if (!hasExt(kSaveFormats[i].ext))
                continue;
            if (kSaveFormats[i].version == version)
                return i;
            if (byExt < 0)
                byExt = i;
            if (byDefault < 0 && kSaveFormats[i].version == kDefaultCadSaveVersion)
                byDefault = i;
        }
        return byDefault >= 0 ? byDefault : byExt;
    }

    // 主窗口（MainFrame）依赖的平台服务。Win32 版（窗口 + D3D11，apps/Win32/src/Host/Win32Window）
    // 与网页版（画布 + WebGL2，apps/Web/src/Host/WebApp）各实现一份；MainFrame 本身不含平台代码。
    class AppPlatform
    {
    public:
        virtual ~AppPlatform() = default;

        // ── 图形 ─────────────────────────────────────────────────
        virtual MiniGUI::IRenderBackend& GetUiBackend()      = 0;   // 界面渲染后端（D3D11Backend / WebGL2Backend）
        virtual IRenderer&               GetRenderer()       = 0;   // CAD 视口渲染器
        virtual IRenderTarget&           GetViewportTarget() = 0;   // CAD 视口的离屏渲染目标
        // 把视口渲染目标的着色器资源（IRenderTarget::GetNativeShaderResource）登记为界面纹理
        virtual MiniGUI::TextureId RegisterViewportTexture(void* nativeShaderResource) = 0;
        virtual bool LoadUiFonts(MiniGUI::TextSystem& text) = 0;
        virtual void RequestRedraw() = 0;       // 安排一次重绘（Win32：InvalidateRect；网页：requestAnimationFrame）
        virtual void BeginUiPass()   = 0;       // 绑定窗口的帧缓冲并清屏
        virtual void EndUiPass()     = 0;       // 呈现，并同步输入法位置等平台输入状态
        virtual void WaitForGpu() {}            // 自测计时用：等 GPU 执行完已提交的命令
        virtual void ReleaseFreeMemory() {}     // 关闭文档后：把堆里空闲的内存归还系统（Win32：整理 CRT 堆）
        virtual const char* GetGraphicsName() const = 0;    // 关于对话框显示，例如 "D3D11"

        // ── 窗口 ─────────────────────────────────────────────────
        virtual void SetTitle(const std::string& utf8) = 0;
        virtual bool HasWindowControls() const = 0;         // 标题栏是否显示最小化 / 最大化 / 关闭
        virtual bool IsWindowActive()    const { return true; }
        virtual bool IsWindowMaximized() const { return false; }
        virtual void MinimizeWindow()       {}
        virtual void ToggleMaximizeWindow() {}
        virtual void CloseWindow() = 0;         // 相当于点了关闭按钮：由 MainFrame::RequestExit 询问未保存的文档
        virtual void Quit()        = 0;         // 确认后退出

        // ── 文件 ─────────────────────────────────────────────────
        // 选择要打开的文件；done 收到 UTF-8 路径（取消时不调用）。网页版的选择是异步的
        virtual void PickFile(FileKind kind, std::function<void(const std::string& path)> done) = 0;
        // 图纸可批量打开；没有多选能力的平台沿用单文件选择。
        virtual void PickDrawingFiles(std::function<void(const std::vector<std::string>&)> done)
        {
            PickFile(FileKind::Drawing, [done = std::move(done)](const std::string& path)
            {
                if (done && !path.empty()) done({ path });
            });
        }
        // 选一个本地文件夹，在里面找文件（外部参照）。done 收到可以用 std::filesystem 遍历的目录（UTF-8，取消时不调用）。
        // fileNames 不为空时只需要这些文件名的文件（不分大小写，没有扩展名的也匹配加 .dwg 的）：网页版拿不到本机路径，
        // 经浏览器授权读取文件夹，只把这些文件按原来的子目录读进内存文件系统，done 收到那个目录。
        // 平台不支持时返回 false
        virtual bool PickFolder(const std::vector<std::string>& fileNames, std::function<void(const std::string& dir)> done)
        {
            (void)fileNames; (void)done;
            return false;
        }
        // 另存为：返回保存路径（UTF-8，空串 = 取消）。
        // 有系统对话框时（HasSaveDialog）suggestedName 为文档名，由用户在对话框里选位置、格式和 DWG / DXF 版本
        // （version 传入当前版本作为默认值，返回所选版本）；
        // 没有时 MainFrame 先用自己的对话框问文件名、格式和版本，suggestedName 是带扩展名的文件名，version 不变
        virtual std::string ChooseSavePath(const std::string& suggestedName, CadSaveVersion& version) = 0;
        virtual bool HasSaveDialog() const { return true; }
        // 导出文件（如节点图）的保存路径（UTF-8，空串 = 取消）：桌面版弹系统对话框；
        // 网页版直接返回内存文件系统里的路径（suggestedName 补上扩展名），写完后调用 OnDocumentSaved 下载
        virtual std::string ChooseExportPath(FileKind kind, const std::string& suggestedName) = 0;
        virtual void OnDocumentSaved(const std::string& path) { (void)path; }   // 网页版：保存（或导出）后下载到本机
        virtual std::string GetResourceDir() const = 0;     // icons/ ui/ patterns/ fonts/ 所在目录（UTF-8）
        virtual std::string GetUserDataDir() const = 0;     // 用户布局等文件的目录（UTF-8）；空串 = 不保存
        // 可执行文件所在目录（UTF-8）：随程序一起携带的小数据文件放在这里（如最近打开列表）。
        // 与 GetResourceDir 分开命名——那个是只读资源目录，两者目前指向同一处，但语义不同。
        // 没有本地文件系统的平台返回空串（不保存）
        virtual std::string GetAppDir() const { return ""; }
        // 系统字体目录（UTF-8，可为空）：图纸引用而程序没有自带的 TrueType 字体（simhei.ttf、arial.ttf …）到这些目录找。
        // 桌面版：%WINDIR%\Fonts 与当前用户安装字体的目录；网页版没有，仍用自带的 GB2312.ttf 代替
        virtual std::vector<std::string> GetSystemFontDirs() const { return {}; }

        // ── 系统 ─────────────────────────────────────────────────
        // 用系统默认程序打开链接（网页版在新标签页打开）；平台不支持或不认识时返回 false
        virtual bool OpenUrl(const std::string& url) { (void)url; return false; }

        // ── 后台任务 ─────────────────────────────────────────────
        // 在后台线程执行 work，完成后在界面线程调用 done（读取大图纸时界面不冻结）。
        // work 只能访问自己的数据；可以调用 RequestRedraw 让界面刷新进度（各平台的实现都是线程安全的）。
        // 没有线程的平台先让界面画出一帧，再在界面线程上依次执行 work 和 done
        virtual void RunInBackground(std::function<void()> work, std::function<void()> done)
        {
            work();
            done();
        }
        // 在界面线程上稍后执行 fn：当前消息（包括正在进行的绘制）处理完之后。
        // 绘制过程中要改界面并重绘时用它——绘制结束会清掉绘制期间提出的重绘请求
        virtual void Post(std::function<void()> fn) { fn(); }
        // RunInBackground 真的在另一个线程上执行（桌面版）。为 false 时节点图在界面线程上同步求值
        virtual bool HasBackgroundThread() const { return false; }

        // ── 三维实体内核 
        // 内核不能并发使用：实现内部加锁，界面线程与节点图的后台求值线程轮流调用
        virtual Solid::ISolidKernel* GetSolidKernel() { return nullptr; }

        // OLE 对象（嵌入的 Word / Excel 等）的展示图是 WMF / EMF 图元文件时把它画成位图（RGBA，白底），长边 maxSide 像素。
        // 桌面版用 GDI 回放；网页版没有图元文件回放，返回 nullptr，OLE 对象只画边框
        virtual std::shared_ptr<const ImageData> RasterizeOle(const Ole::Presentation& presentation, uint32_t maxSide)
        {
            (void)presentation; (void)maxSide;
            return nullptr;
        }
    };
}

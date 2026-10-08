// ── 光栅图像测试：解码（BMP）、路径解析、实体绘制 / 占位框、序列化往返 ───────────────
#include "Core/Draw/IDrawSink.hpp"
#include "Core/Entity/ImageEntity.hpp"
#include "Core/Image/ImageLibrary.h"
#include "Serialization/EntityIO.h"
#include "Serialization/JsonSerializer.h"
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace MiniCAD;
namespace fs = std::filesystem;

namespace
{
    int g_failures = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("[%s] %s\n", ok ? "通过" : "失败", what);
        if (!ok)
            ++g_failures;
    }

    // 写一个 2×1 的 24 位 BMP：左像素红、右像素蓝
    void WriteBmp(const fs::path& path)
    {
        const uint8_t bmp[] = {
            'B', 'M', 0x3E, 0, 0, 0, 0, 0, 0, 0, 0x36, 0, 0, 0,                         // 文件头（54 + 8 字节像素）
            0x28, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 1, 0, 24, 0, 0, 0, 0, 0, 8, 0, 0, 0, // 信息头：2×1，24 位
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0x00, 0x00, 0xFF,  0xFF, 0x00, 0x00,  0, 0                                  // BGR：红、蓝，行补齐到 4 字节
        };
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bmp), sizeof(bmp));
    }

    class ImgSink : public IDrawSink
    {
    public:
        bool available = false;
        int  images    = 0;
        int  lines     = 0;
        void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override { ++lines; }
        bool EmitImage(const std::string&, const std::array<Math::Point3, 4>&) override
        {
            if (available) ++images;
            return available;
        }
    };
}

int RunImageTests()
{
    g_failures = 0;

    const fs::path dir = fs::temp_directory_path() / "minicad_image_test";
    fs::create_directories(dir);
    const fs::path file = dir / "px.bmp";
    WriteBmp(file);
    const std::string path = file.string();

    // ── 解码 ──────────────────────────────────────────────────────────
    ImageLibrary lib;
    auto img = lib.Get(path);
    Check(img && img->Width == 2 && img->Height == 1 && img->Rgba.size() == 8, "解码 BMP：尺寸 2×1，RGBA8");
    if (img && img->Rgba.size() == 8)
        Check(img->Rgba[0] == 255 && img->Rgba[2] == 0 && img->Rgba[4] == 0 && img->Rgba[6] == 255, "像素颜色：左红右蓝");
    Check(lib.Get(path) == img, "同一路径命中缓存");

    lib.Reload(path);
    auto again = lib.Get(path);
    Check(again && again->Key != img->Key, "重新加载后得到新的 Key（后端据此重建纹理）");

    Check(lib.Get((dir / "none.png").string()) == nullptr, "文件不存在返回空");

    // ── 路径解析：原路径失效时到文档目录找同名文件 ──────────────────────
    ImageLibrary lib2;
    lib2.SetBaseDir(dir.string());
    Check(lib2.Get("px.bmp") != nullptr, "相对路径按文档目录解析");
    Check(lib2.Get("Z:/not_exist_dir/px.bmp") != nullptr, "原路径失效时按文件名在文档目录里找");

    // ── 实体 ──────────────────────────────────────────────────────────
    ImageEntity e(1, path, { 0, 0, 0 }, { 20, 0, 0 }, { 20, 10, 0 }, { 0, 10, 0 });

    ImgSink ok;  ok.available = true;
    e.Draw(ok, false, false);
    Check(ok.images == 1 && ok.lines == 4, "图像可用：提交一张图像并画边框");

    e.SetShowFrame(false);
    ImgSink noFrame;  noFrame.available = true;
    e.Draw(noFrame, false, false);
    Check(noFrame.images == 1 && noFrame.lines == 0, "关闭边框后只剩图像");

    ImgSink missing;
    e.Draw(missing, false, false);
    Check(missing.images == 0 && missing.lines == 6, "图像不可用：画边框加两条对角线占位");

    ImgSink selected;  selected.available = true;
    e.Draw(selected, true, false);
    Check(selected.images == 0 && selected.lines == 4, "选中时不重复提交图像，只画高亮边框");

    auto clone = e.Clone(2);
    Check(clone && clone->IsKindOf<ImageEntity>() && static_cast<ImageEntity*>(clone.get())->GetPath() == path, "克隆保持类型与路径");

    JsonSerializer w;
    EntityIO::Write(w, e);
    JsonSerializer r;
    auto back = r.Parse(w.Dump()) ? EntityIO::Read(r) : nullptr;
    Check(back && back->IsKindOf<ImageEntity>(), "序列化往返保持 ImageEntity 类型");
    if (back && back->IsKindOf<ImageEntity>())
    {
        auto* b = static_cast<ImageEntity*>(back.get());
        Check(b->GetPath() == path && b->GetRectangle().P3.x == 20 && b->GetRectangle().P4.y == 10 && !b->GetShowFrame(),
              "序列化往返：路径、四角、边框设置一致");
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
    return g_failures;
}

// 生成天正对象的核对变体：把 DWG 副本中某个未建模对象的主数据流（及可选的文字流）替换为给定位流后另存。
// 用法：MiniCADTchPatch <输入.dwg> <输出.dwg> <句柄十六进制> <主数据流.bin> <主数据流位数> [<文字流.bin> <文字流位数>]
// 只用于在副本上构造变体，交给天正打开、分解或截图，与 MiniCAD 的显示对照
#include "Dwg/Read/DwgReader.h"
#include "Dwg/Write/DwgWriter.h"
#include "Database/UnknownObjects.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
    std::vector<std::uint8_t> Load(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), {}};
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 6 && argc != 8)
    {
        std::fprintf(stderr, "Usage: MiniCADTchPatch <in.dwg> <out.dwg> <handle> <main.bin> <mainbits> [<text.bin> <textbits>]\n");
        return 2;
    }
    const auto bytes = Load(argv[1]);
    const auto db = MiniDWG::ReadDwg(bytes);
    if (!db) { std::fprintf(stderr, "DWG parse failed\n"); return 1; }
    const auto handle = static_cast<MiniDWG::Handle>(std::wcstoull(argv[3], nullptr, 16));
    // 图形对象（墙、门窗等）或非图形对象（如图纸设置 TCH_DBCONFIG）
    MiniDWG::RawObjectData* object = nullptr;
    if (auto* entity = db->FindAs<MiniDWG::UnknownEntity>(handle)) object = &entity->Raw;
    else if (auto* nonGraphic = db->FindAs<MiniDWG::UnknownObject>(handle)) object = &nonGraphic->Raw;
    if (object == nullptr) { std::fprintf(stderr, "handle %llx is not an unknown object\n", static_cast<unsigned long long>(handle)); return 1; }
    auto& raw = object->Dwg;
    raw.Main = Load(argv[4]);
    raw.MainBits = std::wcstoull(argv[5], nullptr, 10);
    if (argc == 8)
    {
        raw.Text = Load(argv[6]);
        raw.TextBits = std::wcstoull(argv[7], nullptr, 10);
    }
    if (raw.Main.size() * 8 < raw.MainBits) { std::fprintf(stderr, "main stream shorter than its bit count\n"); return 1; }
    MiniDWG::DwgWriteOptions options;
    options.Version = raw.Version;
    const auto out = MiniDWG::WriteDwg(*db, options);
    if (out.empty()) { std::fprintf(stderr, "DWG write failed\n"); return 1; }
    std::ofstream(std::filesystem::path(argv[2]), std::ios::binary).write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    std::printf("patched %s %llx main=%llu bits, wrote %zu bytes\n", object->DxfName.c_str(),
                static_cast<unsigned long long>(handle), static_cast<unsigned long long>(raw.MainBits), out.size());
    return 0;
}

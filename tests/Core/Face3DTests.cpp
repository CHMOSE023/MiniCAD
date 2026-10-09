#include "Core/Entity/Face3DEntity.hpp"
#include "Import/CadExchange.h"
#include "Scene/Scene.h"
#include "Serialization/EntityIO.h"
#include "Serialization/JsonSerializer.h"
#include "Dwg/Read/DwgReader.h"
#include <cstdio>

using namespace MiniCAD;
namespace {
    struct Sink : IDrawSink {
        int Lines = 0;
        void DrawLine(const Math::Point3&, const Math::Point3&, const Math::Color4&, bool) override { ++Lines; }
    };
}
int main()
{
    int failures = 0;
    auto check = [&](bool okay, const char* message) { std::printf("[%s] %s\n", okay ? "PASS" : "FAIL", message); failures += !okay; };
    for (std::uint32_t flags = 0; flags < 16; ++flags) {
        Face3DEntity face(1, {0, 0, 1}, {10, 0, 2}, {10, 10, 3}, {0, 10, 4}, flags);
        Sink sink; face.Draw(sink, false, false);
        int edges = 0; for (int i = 0; i < 4; ++i) edges += !(flags & (1u << i));
        check(sink.Lines == edges, "wireframe honors every invisible-edge combination");
        auto clone = face.Clone(2);
        const auto* copied = dynamic_cast<const Face3DEntity*>(clone.get());
        check(copied && copied->GetInvisibleEdges() == flags && copied->GetRectangle().P4.z == 4,
            "clone retains face type, WCS height and edge flags");
        JsonSerializer writer; EntityIO::Write(writer, face);
        JsonSerializer reader; const auto restored = reader.Parse(writer.Dump()) ? EntityIO::Read(reader) : nullptr;
        const auto* loaded = dynamic_cast<const Face3DEntity*>(restored.get());
        check(loaded && loaded->GetInvisibleEdges() == flags && loaded->GetRectangle().P2.z == 2,
            "native serialization retains surface and invisible edges");
        Scene scene; scene.AddEntity(face.Clone(scene.NextObjectID()));
        for (auto kind : {CadFileKind::Dwg, CadFileKind::Dxf}) {
            const auto bytes = ExportCad(scene, kind);
            Scene result;
            check(!bytes.empty() && ImportCad(bytes, result) && result.EntityCount() == 1,
                "face imports through DWG and DXF reader into Scene");
            int matches = 0;
            result.ForEachObject([&](const Object& object) {
                const auto* e = dynamic_cast<const Face3DEntity*>(&object);
                if (e && e->GetInvisibleEdges() == flags && e->GetRectangle().P3.z == 3 && e->GetRectangle().P4.y == 10) ++matches;
            });
            check(matches == 1, "exchange retains all corners and each invisible-edge flag");
            if (kind == CadFileKind::Dwg) {
                const auto db = MiniDWG::ReadDwg(bytes);
                int nativeFaces = 0;
                if (db) for (const auto& [handle, object] : db->Objects()) nativeFaces += dynamic_cast<const MiniDWG::Face3D*>(object.get()) != nullptr;
                check(nativeFaces == 1, "export keeps standard 3DFACE identity instead of replacing with polylines");
            }
        }
    }
    Face3DEntity triangle(3, {0, 0, 0}, {10, 0, 0}, {5, 5, 0}, {5, 5, 0});
    Sink sink; triangle.Draw(sink, false, false);
    check(sink.Lines == 3, "triangle skips repeated third/fourth corner zero-length edge");
    return failures ? 1 : 0;
}

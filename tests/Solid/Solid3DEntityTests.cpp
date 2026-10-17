// Solid3DEntity 测试：线框显示、包围盒、克隆、移动 / 旋转 / 镜像 / 缩放及其撤销、复制、夹点拖动、
// 捕捉特征点、JSON / .mcad 读写往返（含没有内核时的显示与原样保存）
#include "Core/Entity/Solid3DEntity.hpp"
#include "Solid/SMLib/SMLibKernel.h"
#include "Document/Document.h"
#include "Document/FeaturePoints.h"
#include "Document/Command/MoveCommand.h"
#include "Document/Command/RotateMoveCommand.h"
#include "Document/Command/MirrorMoveCommand.h"
#include "Document/Command/CopyCommand.h"
#include "Document/Command/EntityScale.h"
#include "Document/Command/DragEntitiesCommand.h"
#include "Editor/Grip/Solid3DGripHandler.h"
#include "Scene/Scene.h"
#include "Serialization/EntityIO.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/SerializeHelpers.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numbers>

using namespace MiniCAD;
using Math::Point3;
using Math::Vec3;

namespace
{
    int g_failed = 0;
    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        g_failed += ok ? 0 : 1;
    }

    constexpr double kPi = std::numbers::pi;

    bool Near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }
    bool NearPoint(const Point3& a, const Point3& b, double tol = 1e-6) { return Near(a.x, b.x, tol) && Near(a.y, b.y, tol) && Near(a.z, b.z, tol); }
    bool NearBox(const AABB& b, const Point3& mn, const Point3& mx, double tol = 1e-4) { return NearPoint(b.Min, mn, tol) && NearPoint(b.Max, mx, tol); }

    struct LineSink : IDrawSink
    {
        int    Lines = 0;
        double Length = 0.0;
        void DrawLine(const Point3& a, const Point3& b, const Math::Color4&, bool) override
        {
            ++Lines;
            Length += (b - a).Length();
        }
    };

    const Solid3DEntity* SolidIn(const Scene& scene, Object::ObjectID id)
    {
        const Object* o = scene.GetEntity(id);
        return o && o->IsKindOf<Solid3DEntity>() ? static_cast<const Solid3DEntity*>(o) : nullptr;
    }

    bool HasPoint(const std::vector<Point3>& pts, const Point3& p)
    {
        for (const auto& q : pts)
            if (NearPoint(q, p, 1e-6))
                return true;
        return false;
    }

    // 10 × 20 × 30 的长方体，角点在 (1, 2, 3)
    std::unique_ptr<Solid3DEntity> MakeBox(Solid::ISolidKernel& k, Object::ObjectID id)
    {
        return Solid3DEntity::Create(id, k, k.Box({ 1, 2, 3 }, 10, 20, 30));
    }

    void TestCreateAndDraw(Solid::ISolidKernel& k)
    {
        auto box = MakeBox(k, 1);
        Check(box != nullptr, "create from a kernel body");
        if (!box) return;
        Check(!box->GetBytes().empty() && box->GetLocalWire().size() == 12, "keeps kernel data and the 12 edges");
        Check(NearBox(box->GetBoundingBox(), { 1, 2, 3 }, { 11, 22, 33 }), "bounding box from the wire");

        LineSink sink;
        box->Draw(sink, false, false);
        Check(sink.Lines == 12 && Near(sink.Length, 4 * (10 + 20 + 30), 1e-6), "draws the 12 edges (total length 240)");

        auto clone = box->Clone(2);
        const auto* c = dynamic_cast<const Solid3DEntity*>(clone.get());
        Check(c && c->GetID() == 2 && &c->GetBytes() == &box->GetBytes(), "clone shares the kernel data");

        Check(Solid3DEntity::Create(3, k, nullptr) == nullptr, "create from a null body fails");
    }

    void TestTransforms(Solid::ISolidKernel& k)
    {
        auto box = MakeBox(k, 1);
        if (!box) return;

        box->ApplyTransform(Solid3DEntity::MoveMatrix({ 100, 0, 0 }));
        Check(NearBox(box->GetBoundingBox(), { 101, 2, 3 }, { 111, 22, 33 }), "move only changes the transform");

        // 绕原点逆时针转 90°：(x, y) → (−y, x)
        auto rot = MakeBox(k, 2);
        rot->ApplyTransform(Solid3DEntity::RotateMatrix({ 0, 0, 0 }, kPi / 2));
        Check(NearBox(rot->GetBoundingBox(), { -22, 1, 3 }, { -2, 11, 33 }), "rotate 90° counter-clockwise about the origin");

        // 以 x = 0 为镜像线：x → −x
        auto mir = MakeBox(k, 3);
        mir->ApplyTransform(Solid3DEntity::MirrorMatrix({ 0, 0, 0 }, { 0, 1, 0 }));
        Check(NearBox(mir->GetBoundingBox(), { -11, 2, 3 }, { -1, 22, 33 }), "mirror in the line x = 0");

        // 以 (1, 2, 3) 为基点放大 2 倍
        auto sc = MakeBox(k, 4);
        Check(ScaleEntityInPlace(*sc, { 1, 2, 3 }, 2.0), "ScaleEntityInPlace supports solids");
        Check(NearBox(sc->GetBoundingBox(), { 1, 2, 3 }, { 21, 42, 63 }), "scale ×2 about the corner");

        // 变换后的内核实体与显示一致
        auto world = sc->GetWorldBody(k);
        Check(world && Near(k.Volume(world), 6000.0 * 8, 1e-3), "world body follows the transform (volume ×8)");
        Check(sc->GetBody(k) && Near(k.Volume(sc->GetBody(k)), 6000.0, 1e-3), "local body is unchanged");
    }

    void TestCommandsAndUndo(Solid::ISolidKernel& k)
    {
        Scene scene;
        const auto id = scene.NextObjectID();
        scene.AddEntity(MakeBox(k, id));

        MoveCommand move({ id }, { 5, 0, 0 }, scene);
        Check(move.Execute(scene) && NearBox(SolidIn(scene, id)->GetBoundingBox(), { 6, 2, 3 }, { 16, 22, 33 }), "MoveCommand moves the solid");
        move.Undo(scene);
        Check(NearBox(SolidIn(scene, id)->GetBoundingBox(), { 1, 2, 3 }, { 11, 22, 33 }), "MoveCommand undo");

        RotateMoveCommand rotate({ id }, { 0, 0, 0 }, kPi, scene);
        Check(rotate.Execute(scene) && NearBox(SolidIn(scene, id)->GetBoundingBox(), { -11, -22, 3 }, { -1, -2, 33 }), "RotateMoveCommand rotates 180°");
        rotate.Undo(scene);
        Check(NearBox(SolidIn(scene, id)->GetBoundingBox(), { 1, 2, 3 }, { 11, 22, 33 }), "RotateMoveCommand undo");

        MirrorMoveCommand mirror({ id }, MirrorAxis{ { 0, 0, 0 }, { 1, 0, 0 } }, scene);
        Check(mirror.Execute(scene) && NearBox(SolidIn(scene, id)->GetBoundingBox(), { 1, -22, 3 }, { 11, -2, 33 }), "MirrorMoveCommand mirrors in the X axis");
        mirror.Undo(scene);
        Check(NearBox(SolidIn(scene, id)->GetBoundingBox(), { 1, 2, 3 }, { 11, 22, 33 }), "MirrorMoveCommand undo");

        CopyCommand copy({ id }, { 0, 50, 0 });
        Check(copy.Execute(scene) && copy.GetNewIds().size() == 1 && scene.EntityCount() == 2, "CopyCommand adds a copy");
        const auto* copied = copy.GetNewIds().empty() ? nullptr : SolidIn(scene, copy.GetNewIds()[0]);
        Check(copied && NearBox(copied->GetBoundingBox(), { 1, 52, 3 }, { 11, 72, 33 }), "the copy is moved, the original stays");
        Check(NearBox(SolidIn(scene, id)->GetBoundingBox(), { 1, 2, 3 }, { 11, 22, 33 }), "copying leaves the original in place");
        copy.Undo(scene);
        Check(scene.EntityCount() == 1, "CopyCommand undo removes the copy");
    }

    void TestGripDrag(Solid::ISolidKernel& k)
    {
        Scene scene;
        const auto id = scene.NextObjectID();
        scene.AddEntity(MakeBox(k, id));
        auto* solid = static_cast<Solid3DEntity*>(scene.GetEntity(id));

        Solid3DGripHandler handler;
        std::vector<Grip> grips;
        handler.BuildGrips(solid, grips);
        Check(grips.size() == 1 && NearPoint(grips[0].WorldPos, { 6, 12, 18 }), "one grip at the bounding-box center");
        if (grips.empty()) return;

        auto state = handler.BeginDrag(solid, grips[0]);
        handler.UpdateDrag(solid, state.get(), grips[0], { 16, 12, 18 }, grips);
        Check(NearBox(solid->GetBoundingBox(), { 11, 2, 3 }, { 21, 22, 33 }) && NearPoint(grips[0].WorldPos, { 16, 12, 18 }), "dragging the grip moves the solid");

        DragEntityEntry entry{};
        Check(handler.EndDrag(solid, state.get(), entry) && entry.Kind == DragEntityEntry::Kind::Transform, "end drag records the transform");
        DragEntitiesCommand cmd({ entry });
        cmd.Undo(scene);
        Check(NearBox(SolidIn(scene, id)->GetBoundingBox(), { 1, 2, 3 }, { 11, 22, 33 }), "drag undo restores the position");
        cmd.Execute(scene);
        Check(NearBox(SolidIn(scene, id)->GetBoundingBox(), { 11, 2, 3 }, { 21, 22, 33 }), "drag redo");

        auto state2 = handler.BeginDrag(solid, grips[0]);
        handler.CancelDrag(solid, state2.get());
        Check(NearBox(solid->GetBoundingBox(), { 11, 2, 3 }, { 21, 22, 33 }), "cancel drag keeps the position");
    }

    void TestFeaturePoints(Solid::ISolidKernel& k)
    {
        auto box = MakeBox(k, 1);
        std::vector<Point3> ends, mids;
        CollectFeaturePoints(*box, FeatureKind::Endpoint, ends);
        CollectFeaturePoints(*box, FeatureKind::Midpoint, mids);
        Check(HasPoint(ends, { 1, 2, 3 }) && HasPoint(ends, { 11, 22, 33 }) && HasPoint(ends, { 11, 2, 33 }), "endpoints include the box corners");
        Check(mids.size() == 12 && HasPoint(mids, { 6, 2, 3 }) && HasPoint(mids, { 1, 12, 33 }) && HasPoint(mids, { 11, 22, 18 }), "one midpoint per edge");

        box->ApplyTransform(Solid3DEntity::MoveMatrix({ 0, 0, 100 }));
        ends.clear();
        CollectFeaturePoints(*box, FeatureKind::Endpoint, ends);
        Check(HasPoint(ends, { 1, 2, 103 }), "feature points follow the transform");

        auto cyl = Solid3DEntity::Create(2, k, k.Cylinder({ 5, 5, 0 }, 2, 10));
        std::vector<Point3> centers, cylEnds;
        CollectFeaturePoints(*cyl, FeatureKind::Center, centers);
        CollectFeaturePoints(*cyl, FeatureKind::Endpoint, cylEnds);
        Check(HasPoint(centers, { 5, 5, 0 }) && HasPoint(centers, { 5, 5, 10 }), "cylinder: centers of the two circular edges");
        Check(HasPoint(cylEnds, { 5, 5, 0 }) && HasPoint(cylEnds, { 5, 5, 10 }), "circle centers also snap as endpoints (as for circles)");
    }

    void TestJsonRoundTrip(Solid::ISolidKernel& k)
    {
        auto box = MakeBox(k, 7);
        box->ApplyTransform(Solid3DEntity::RotateMatrix({ 0, 0, 0 }, kPi / 2));

        JsonSerializer writer;
        Check(EntityIO::Write(writer, *box), "EntityIO writes Solid3DEntity");
        JsonSerializer reader;
        auto restored = reader.Parse(writer.Dump()) ? EntityIO::Read(reader) : nullptr;
        const auto* loaded = dynamic_cast<const Solid3DEntity*>(restored.get());
        Check(loaded && loaded->GetID() == 7, "EntityIO reads it back");
        if (!loaded) return;
        Check(loaded->GetBytes() == box->GetBytes(), "kernel data survives (base64)");
        Check(NearBox(loaded->GetBoundingBox(), { -22, 1, 3 }, { -2, 11, 33 }), "transform and wire survive: displays without a kernel");
        auto world = loaded->GetWorldBody(k);
        Check(world && Near(k.Volume(world), 6000.0, 1e-3), "the body loads lazily from the saved data");
    }

    void TestDocumentRoundTrip(Solid::ISolidKernel& k)
    {
        const auto dir = std::filesystem::temp_directory_path();
        for (const char* ext : { ".mcad", ".json" })
        {
            const std::string path = (dir / (std::string("minicad_solid3d_test") + ext)).string();
            {
                Document doc;
                Scene& scene = doc.GetScene();
                const auto a = scene.NextObjectID();
                auto box = MakeBox(k, a);
                box->ApplyTransform(Solid3DEntity::MoveMatrix({ 0, 0, 50 }));
                scene.AddEntity(std::move(box));
                const auto b = scene.NextObjectID();
                scene.AddEntity(Solid3DEntity::Create(b, k, k.Boolean(Solid::BooleanOp::Difference, k.Box({ 0, 0, 0 }, 10, 10, 10), k.Cylinder({ 5, 5, -1 }, 2, 12))));
                Check(doc.SaveAs(path), ext[1] == 'm' ? ".mcad: save" : ".json: save");
            }

            Document doc;
            const bool loaded = doc.LoadFromFile(path);
            int solids = 0;
            double volume = 0.0;
            bool moved = false;
            doc.GetScene().ForEachObject([&](const Object& o)
            {
                if (!o.IsKindOf<Solid3DEntity>()) return;
                const auto& s = static_cast<const Solid3DEntity&>(o);
                ++solids;
                if (auto w = s.GetWorldBody(k)) volume += k.Volume(w);
                moved = moved || NearBox(s.GetBoundingBox(), { 1, 2, 53 }, { 11, 22, 83 });
            });
            Check(loaded && solids == 2, ext[1] == 'm' ? ".mcad: both solids load" : ".json: both solids load");
            Check(moved, ext[1] == 'm' ? ".mcad: the transform is kept" : ".json: the transform is kept");
            Check(Near(volume, 6000 + 1000 - 40 * kPi, 1e-3), ext[1] == 'm' ? ".mcad: kernel bodies are intact" : ".json: kernel bodies are intact");
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }

    void TestBase64()
    {
        bool ok = true;
        for (size_t n = 0; n < 40; ++n)
        {
            std::vector<uint8_t> bytes(n);
            for (size_t i = 0; i < n; ++i) bytes[i] = static_cast<uint8_t>(i * 37 + 11);
            ok = ok && Ser::Base64Decode(Ser::Base64Encode(bytes)) == bytes;
        }
        Check(ok, "base64 round trip for lengths 0..39");
        Check(Ser::Base64Encode({ 'M', 'a', 'n' }) == "TWFu" && Ser::Base64Encode({ 'M' }) == "TQ==", "base64 matches the standard alphabet");
        Check(Ser::Base64Decode("T?Fu").empty() && Ser::Base64Decode("TWF").empty(), "invalid base64 decodes to nothing");
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    auto kernel = Solid::CreateSMLibKernel();

    TestCreateAndDraw(*kernel);
    TestTransforms(*kernel);
    TestCommandsAndUndo(*kernel);
    TestGripDrag(*kernel);
    TestFeaturePoints(*kernel);
    TestJsonRoundTrip(*kernel);
    TestDocumentRoundTrip(*kernel);
    TestBase64();

    std::printf("\n%d failed\n", g_failed);
    return g_failed;
}

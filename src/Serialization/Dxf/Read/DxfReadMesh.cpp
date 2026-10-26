// DXF 读取：细分网格 MESH（AcDbSubDMesh）。
//
// 组码（与权威工具转出的样例逐项核对）：
//     71  版本           72  混合折痕        91  细分级别
//     92  顶点数         93  面数据项数      94  边数        95  折痕数
//     10/20/30  顶点（按顺序，三连）
//     90  面数据（「个数 + 该面的索引」重复），随后接边对
//     140 折痕值
//
// 【关键】93 是**项数**不是面数：项数 = Σ(每个面的顶点数 + 1)。
// 切分方式：先收 93（面的项数上限）与 94（边数），再按它们把 90 序列切开：
// 先消耗 93 项得到面，剩下按 94 个两元组得到边。
// 组码 310/160 是 AcDbEntity 级的代理图形数据，由公共实体数据处理，这里只认几何组码。
#include "Dxf/Read/DxfReaderImpl.h"
#include "Database/Mesh.h"

namespace MiniDWG::DxfRead
{
    void Reader::ReadMesh(ReadItem& item, const Record& r)
    {
        auto* mesh = dynamic_cast<Mesh*>(item.Object.get());
        if (mesh == nullptr)
            return;

        // 第一遍：取控制数与几何序列。
        // 控件（71/72/91/92/93/94/95）都在 `AcDbSubDMesh` 子类标记之后、几何之前，
        // 但为了不依赖字段顺序，统一先收集再切分。
        std::vector<double>       coords;     // 10/20/30 依次压入
        std::vector<std::int32_t> n90;        // 全部 90
        std::vector<double>       creases;    // 140
        std::int32_t faceItems = -1;          // 93
        std::int32_t edgeCount = -1;          // 94

        Process(item, r, [&](const DxfGroup& g, std::size_t&, const ParseState&) -> bool {
            switch (g.Code)
            {
            case 71: mesh->Version          = static_cast<std::int16_t>(g.Value.AsInt()); return true;
            case 72: mesh->BlendCrease      = g.Value.AsInt() != 0;                       return true;
            case 91: mesh->SubdivisionLevel = static_cast<std::int32_t>(g.Value.AsInt()); return true;
            case 93: faceItems = static_cast<std::int32_t>(g.Value.AsInt());              return true;
            case 94: edgeCount = static_cast<std::int32_t>(g.Value.AsInt());              return true;
            case 92: case 95:                                                             return true;   // 由几何序列长度隐含
            case 10: case 20: case 30:
                coords.push_back(g.Value.AsDouble());
                return true;
            case 90:  n90.push_back(static_cast<std::int32_t>(g.Value.AsInt()));          return true;
            case 140: creases.push_back(g.Value.AsDouble());                              return true;
            default: return false;
            }
        });

        // ── 顶点：10/20/30 三连 ──
        for (std::size_t i = 0; i + 2 < coords.size(); i += 3)
            mesh->Vertices.push_back(XYZ{ coords[i], coords[i + 1], coords[i + 2] });

        // ── 面：消耗 93 项（每面 1 项"个数" + n 项索引）──
        const auto nv = static_cast<std::int32_t>(mesh->Vertices.size());
        const std::size_t total = n90.size();
        std::size_t k = 0;
        std::int32_t consumed = 0;
        while (k < total)
        {
            if (faceItems >= 0 && consumed >= faceItems)
                break;
            const std::int32_t n = n90[k];
            // 不是合法的面顶点数 ⇒ 面到此为止，后面是边对
            if (n < 3 || n > nv) break;
            ++k;
            ++consumed;
            if (faceItems >= 0 && n > faceItems - consumed) break;   // 越界保护
            if (k + static_cast<std::size_t>(n) > total) break;

            std::vector<int> face;
            face.reserve(static_cast<std::size_t>(n));
            bool ok = true;
            for (std::int32_t j = 0; j < n; ++j)
            {
                const std::int32_t idx = n90[k++];
                if (idx < 0 || idx >= nv) { ok = false; break; }
                face.push_back(static_cast<int>(idx));
            }
            if (!ok) break;
            consumed += n;
            mesh->Faces.push_back(std::move(face));
        }

        // ── 边：剩下的两两成对（94 给了个数就按它，否则全取）──
        std::int32_t made = 0;
        for (; k + 1 < total; k += 2)
        {
            if (edgeCount >= 0 && made >= edgeCount) break;
            mesh->Edges.emplace_back(static_cast<int>(n90[k]), static_cast<int>(n90[k + 1]));
            ++made;
        }

        mesh->CreaseValues = std::move(creases);
        // OverrideOption 只存在于 DWG 位流尾部，DXF 无对应组码，保持默认 0
    }
}

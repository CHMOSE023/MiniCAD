# ── MiniDWG（DWG/DXF 读写库，源码在 src/Serialization 下的 Codec/Database/Dwg/Dxf）──
# 头文件以 src/Serialization 为根（"Database/CadDatabase.h"），命名空间 MiniDWG。
# 移植自 ACadSharp（MIT），见根目录 THIRD_PARTY_NOTICES.md。

set(MINIDWG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/Serialization")

# ── Database ────────────────────────────────────────────────────────
set(MINIDWG_DATABASE_SOURCES
    "${MINIDWG_SRC}/Database/CadVersion.cpp"
    "${MINIDWG_SRC}/Database/CadFileFormat.cpp"
    "${MINIDWG_SRC}/Database/CadDatabase.cpp"
    "${MINIDWG_SRC}/Database/UnknownObjects.cpp"
    "${MINIDWG_SRC}/Database/DxfValue.cpp"
    "${MINIDWG_SRC}/Database/Color.cpp"
)

# ── Codec ───────────────────────────────────────────────────────────
set(MINIDWG_CODEC_SOURCES
    "${MINIDWG_SRC}/Codec/CodePage.cpp"
)

# ── Dxf ─────────────────────────────────────────────────────────────
set(MINIDWG_DXF_SOURCES
    "${MINIDWG_SRC}/Dxf/Read/DxfStreamReader.cpp"
    "${MINIDWG_SRC}/Dxf/Read/DxfReader.cpp"
    "${MINIDWG_SRC}/Dxf/Read/DxfReadEntities.cpp"
    "${MINIDWG_SRC}/Dxf/Read/DxfReadMultiLeader.cpp"
    "${MINIDWG_SRC}/Dxf/Read/DxfReadBuild.cpp"
    "${MINIDWG_SRC}/Dxf/Write/DxfStreamWriter.cpp"
    "${MINIDWG_SRC}/Dxf/Write/DxfWriter.cpp"
    "${MINIDWG_SRC}/Dxf/Write/DxfWriteEntities.cpp"
    "${MINIDWG_SRC}/Dxf/Write/DxfWriteMultiLeader.cpp"
)

# ── Dwg ─────────────────────────────────────────────────────────────
set(MINIDWG_DWG_SOURCES
    "${MINIDWG_SRC}/Dwg/Read/DwgBitReader.cpp"
    "${MINIDWG_SRC}/Dwg/Read/DwgDecompress.cpp"
    "${MINIDWG_SRC}/Dwg/Read/DwgFile.cpp"
    "${MINIDWG_SRC}/Dwg/Read/DwgReader.cpp"
    "${MINIDWG_SRC}/Dwg/Read/DwgReadHeader.cpp"
    "${MINIDWG_SRC}/Dwg/Read/DwgReadObjects.cpp"
    "${MINIDWG_SRC}/Dwg/Read/DwgReadEntities.cpp"
    "${MINIDWG_SRC}/Dwg/Read/DwgReadMultiLeader.cpp"
    "${MINIDWG_SRC}/Dwg/Read/DwgReadBuild.cpp"
    "${MINIDWG_SRC}/Dwg/Write/DwgBitWriter.cpp"
    "${MINIDWG_SRC}/Dwg/Write/DwgCompress.cpp"
    "${MINIDWG_SRC}/Dwg/Write/DwgWriter.cpp"
    "${MINIDWG_SRC}/Dwg/Write/DwgWriteHeader.cpp"
    "${MINIDWG_SRC}/Dwg/Write/DwgWriteObjects.cpp"
    "${MINIDWG_SRC}/Dwg/Write/DwgWriteEntities.cpp"
    "${MINIDWG_SRC}/Dwg/Write/DwgWriteMultiLeader.cpp"
    "${MINIDWG_SRC}/Dwg/Write/DwgFileLayout.cpp"
)

# ── 生成代码（原 MiniDWG 仓库 tools/ 下的脚本生成，不要手工修改）────────
set(MINIDWG_GENERATED_SOURCES
    "${MINIDWG_SRC}/Database/Generated/DxfMeta.g.cpp"          # tools/dxfgen/dxfgen.py
    "${MINIDWG_SRC}/Codec/Generated/CodePageTables.g.cpp"      # tools/codepages/gen_codepages.py
)

# ── MiniDWG 核心库（与平台无关，零外部依赖）─────────────────────────────
add_library(MiniDWG STATIC
    ${MINIDWG_DATABASE_SOURCES}
    ${MINIDWG_CODEC_SOURCES}
    ${MINIDWG_DXF_SOURCES}
    ${MINIDWG_DWG_SOURCES}
    ${MINIDWG_GENERATED_SOURCES}
)

target_include_directories(MiniDWG
    PUBLIC ${MINIDWG_SRC}
)

if(MSVC)
    target_compile_options(MiniDWG PRIVATE /utf-8 /W4)
endif()

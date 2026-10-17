# ── MiniCAD 核心库（平台无关的 CAD 内核：场景、文档、编辑器、视口、文字、导入导出）──
# 源码在 src 下，头文件以 src 为根（"Core/Entity/LineEntity.hpp"），命名空间 MiniCAD。
# 依赖 MiniDWG（只在 Import 中使用，PRIVATE 链接，公开头文件不暴露 MiniDWG 类型）。

set(MINICAD_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# ── Scene ───────────────────────────────────────────────────────────
set(MINICAD_SCENE_SOURCES
    "${MINICAD_SRC}/Scene/Scene.cpp"
    "${MINICAD_SRC}/Scene/Layer.cpp"
    "${MINICAD_SRC}/Scene/LayerManager.cpp"
    "${MINICAD_SRC}/Scene/LineTypeTable.cpp"
    "${MINICAD_SRC}/Scene/BlockTable.cpp"
    "${MINICAD_SRC}/Scene/DimStyleTable.cpp"
    "${MINICAD_SRC}/Scene/HatchPatternLibrary.cpp"
    "${MINICAD_SRC}/Scene/TextStyleTable.cpp"
    "${MINICAD_SRC}/Scene/MLineStyleTable.cpp"
)

# ── Document ────────────────────────────────────────────────────────
set(MINICAD_DOCUMENT_SOURCES
    "${MINICAD_SRC}/Document/Document.cpp"
    "${MINICAD_SRC}/Document/DocumentManager.cpp"
    "${MINICAD_SRC}/Document/RecentFiles.cpp"
    "${MINICAD_SRC}/Document/CommandStack/CommandStack.cpp"
    "${MINICAD_SRC}/Document/FeaturePoints.cpp"
    "${MINICAD_SRC}/Document/DimAssoc.cpp"
    "${MINICAD_SRC}/Document/Command/EntityTranslate.cpp"
    "${MINICAD_SRC}/Document/Command/EntityRotate.cpp"
    "${MINICAD_SRC}/Document/Command/EntityScale.cpp"
    "${MINICAD_SRC}/Document/Command/EntityStretch.cpp"
    "${MINICAD_SRC}/Document/Command/EntityFillet.cpp"
    "${MINICAD_SRC}/Document/Command/EntityChamfer.cpp"
    "${MINICAD_SRC}/Document/Command/EntityExplode.cpp"
    "${MINICAD_SRC}/Document/Command/EntityMirror.cpp"
    "${MINICAD_SRC}/Document/Command/MoveCommand.cpp"
    "${MINICAD_SRC}/Document/Command/CopyCommand.cpp"
    "${MINICAD_SRC}/Document/Command/MirrorMoveCommand.cpp"
    "${MINICAD_SRC}/Document/Command/MirrorCopyCommand.cpp"
    "${MINICAD_SRC}/Document/Command/RotateMoveCommand.cpp"
    "${MINICAD_SRC}/Document/Command/RotateCopyCommand.cpp"
    "${MINICAD_SRC}/Document/Command/ArrayCommand.cpp"
)

# ── Serialization ───────────────────────────────────────────────────
set(MINICAD_SERIALIZATION_SOURCES
    "${MINICAD_SRC}/Serialization/TreeSerializer.cpp"
    "${MINICAD_SRC}/Serialization/JsonSerializer.cpp"
    "${MINICAD_SRC}/Serialization/BinarySerializer.cpp"
    "${MINICAD_SRC}/Serialization/EntityIO.cpp"
)

# ── Import（DWG/DXF 经 MiniDWG 与 Scene 互转）──────────────────────
set(MINICAD_IMPORT_SOURCES
    "${MINICAD_SRC}/Import/CadExchange.cpp"
)

# ── Editor ──────────────────────────────────────────────────────────
set(MINICAD_EDITOR_SOURCES
    "${MINICAD_SRC}/Editor/Editor.cpp"
    "${MINICAD_SRC}/Editor/Overlay/EntityOverlay.cpp"
    "${MINICAD_SRC}/Editor/Resolver/InputResolver.cpp"
    "${MINICAD_SRC}/Editor/Snap/SnapEngine.cpp"
    "${MINICAD_SRC}/Editor/Picking/Picking.cpp"
    "${MINICAD_SRC}/Editor/Properties/PropertyTable.cpp"
    "${MINICAD_SRC}/Editor/Grip/GripEditor.cpp"
    "${MINICAD_SRC}/Editor/Constraint/ConstraintEngine.cpp"
)

# ── Viewport ────────────────────────────────────────────────────────
set(MINICAD_VIEWPORT_SOURCES
    "${MINICAD_SRC}/Viewport/Viewport.cpp"
    "${MINICAD_SRC}/Viewport/Camera.cpp"
)

# ── Text ────────────────────────────────────────────────────────────
set(MINICAD_TEXT_SOURCES
    "${MINICAD_SRC}/Text/Font/FontEngine.cpp"
    "${MINICAD_SRC}/Text/Font/SHXFont.cpp"
    "${MINICAD_SRC}/Text/Font/SHXCompositeFont.cpp"
    "${MINICAD_SRC}/Text/Font/TTFFont.cpp"
    "${MINICAD_SRC}/Core/Image/ImageLibrary.cpp"
    "${MINICAD_SRC}/Text/Font/EncodingGBK.cpp"
    "${MINICAD_SRC}/Text/Layout/TextLayoutEngine.cpp"
    "${MINICAD_SRC}/Text/Parser/SHXParser.cpp"
    "${MINICAD_SRC}/Text/Parser/SHXVM.cpp"
)

# ── GeomKernel ──────────────────────────────────────────────────────
set(MINICAD_GEOM_SOURCES
    "${MINICAD_SRC}/Core/GeomKernel/CurveIntersect.cpp"
)

# ── 源文件汇总 ──────────────────────────────────────────────────────
set(MINICAD_SOURCES
    "${MINICAD_SRC}/MiniCADEngine.cpp"
    ${MINICAD_SCENE_SOURCES}
    ${MINICAD_DOCUMENT_SOURCES}
    ${MINICAD_EDITOR_SOURCES}
    ${MINICAD_SERIALIZATION_SOURCES}
    ${MINICAD_IMPORT_SOURCES}
    ${MINICAD_VIEWPORT_SOURCES}
    ${MINICAD_TEXT_SOURCES}
    ${MINICAD_GEOM_SOURCES}
)

add_library(MiniCAD STATIC ${MINICAD_SOURCES})

target_include_directories(MiniCAD
    PUBLIC
        ${MINICAD_SRC}
        ${CMAKE_CURRENT_SOURCE_DIR}/3rd
)

# DWG/DXF 只在库内部的导入导出代码中使用，公开头文件不暴露 MiniDWG 类型
target_link_libraries(MiniCAD PRIVATE MiniDWG)

if(MSVC)
    target_compile_options(MiniCAD PRIVATE /utf-8)
    target_compile_definitions(MiniCAD PRIVATE
        UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# 旧版 WASM 接口（wasm/：导出 C 函数，界面由 HTML/JS 前端实现）。核心库在 MINICAD_WEB 下改走那套前端的路径
# （文字输入交给 JS、块名自动命名、文字用纹理图集），与网页版（apps/Web，界面与桌面版相同）互斥，默认关闭
option(MINICAD_BUILD_LEGACY_WASM "Build the legacy C-API WASM target (wasm/) instead of the MiniGUI web app" OFF)
if(EMSCRIPTEN AND MINICAD_BUILD_LEGACY_WASM)
    target_compile_definitions(MiniCAD PUBLIC MINICAD_WEB)
endif()

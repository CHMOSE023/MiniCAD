cmake_minimum_required(VERSION 3.20)
project(MiniCAD VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 静态链接 MSVC 运行库（/MT、/MTd），拷到其他电脑上运行时不再依赖 MSVCP140(D).dll / VCRUNTIME140(D).dll
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# 三个库的头文件各有自己的 include 根：MiniCADLib → src，MiniGUI → src/UI，MiniDWG → src/Serialization。
# 各根之下的 "目录/文件.h" 相对路径不能重复，否则会按搜索顺序互相遮挡（编译器不报错）。

# ── MiniDWG（DWG/DXF 读写库）────────────────────────────────────────
include(cmake/MiniDWG.cmake)

# ── MiniGUI（保留模式界面库，桌面端使用）─────────────────────────────
if(NOT EMSCRIPTEN)
    include(cmake/MiniGUI.cmake)
endif()

# ── Scene ───────────────────────────────────────────────────────────
set(SCENE_SOURCES
    "src/Scene/Scene.cpp"
    "src/Scene/Layer.cpp"
    "src/Scene/LayerManager.cpp"
    "src/Scene/LineTypeTable.cpp"
    "src/Scene/BlockTable.cpp"
    "src/Scene/DimStyleTable.cpp"
    "src/Scene/HatchPatternLibrary.cpp"
    "src/Scene/TextStyleTable.cpp"
    "src/Scene/MLineStyleTable.cpp"
)

# ── Document ────────────────────────────────────────────────────────
set(DOCUMENT_SOURCES   
   "src/Document/Document.cpp"
   "src/Document/DocumentManager.cpp"
   "src/Document/CommandStack/CommandStack.cpp"
   "src/Document/FeaturePoints.cpp"
   "src/Document/DimAssoc.cpp"
   "src/Document/Command/EntityTranslate.cpp"
   "src/Document/Command/EntityRotate.cpp"
   "src/Document/Command/EntityScale.cpp"
   "src/Document/Command/EntityStretch.cpp"
   "src/Document/Command/EntityFillet.cpp"
   "src/Document/Command/EntityChamfer.cpp"
   "src/Document/Command/EntityExplode.cpp"
   "src/Document/Command/EntityMirror.cpp"
   "src/Document/Command/MoveCommand.cpp"
   "src/Document/Command/CopyCommand.cpp"
   "src/Document/Command/MirrorMoveCommand.cpp"
   "src/Document/Command/MirrorCopyCommand.cpp"
   "src/Document/Command/RotateMoveCommand.cpp"
   "src/Document/Command/RotateCopyCommand.cpp"
   "src/Document/Command/ArrayCommand.cpp"
)

# ── Serialization ────────────────────────────────────────────────────────── 
set(SERIALIZATION_SOURCES
    "src/Serialization/TreeSerializer.cpp"
    "src/Serialization/JsonSerializer.cpp"
    "src/Serialization/BinarySerializer.cpp"
    "src/Serialization/EntityIO.cpp"
)

# ── Import（DWG/DXF 经 MiniDWG 与 Scene 互转）──────────────────────
set(IMPORT_SOURCES
    "src/Import/CadExchange.cpp"
)

# ── Editor ────────────────────────────────────────────────────────── 
set(EDITOR_SOURCES
    "src/Editor/Editor.cpp"
    "src/Editor/Overlay/EntityOverlay.cpp"
    "src/Editor/Resolver/InputResolver.cpp"
    "src/Editor/Snap/SnapEngine.cpp"
    "src/Editor/Picking/Picking.cpp"
    "src/Editor/Properties/PropertyTable.cpp"
    "src/Editor/Grip/GripEditor.cpp"
    "src/Editor/Constraint/ConstraintEngine.cpp"
)

# ── Viewport ────────────────────────────────────────────────────────
set(VIEWPORT_SOURCES
    "src/Viewport/Viewport.cpp"
    "src/Viewport/Camera.cpp"
)

# ── Text ────────────────────────────────────────────────────────────
set(TEXT_SOURCES
    "src/Text/Font/FontEngine.cpp"
    "src/Text/Font/SHXFont.cpp"
    "src/Text/Font/SHXCompositeFont.cpp"
    "src/Text/Font/TTFFont.cpp"
    "src/Core/Image/ImageLibrary.cpp"
    "src/Text/Font/EncodingGBK.cpp"
    "src/Text/Layout/TextLayoutEngine.cpp"
    "src/Text/Parser/SHXParser.cpp"
    "src/Text/Parser/SHXVM.cpp"
)

# ── GeomKernel ──────────────────────────────────────────────────────
set(GEOM_SOURCES
    "src/Core/GeomKernel/CurveIntersect.cpp"
)

# ── 库源文件汇总 ─────────────────────────────────────────────────────
set(MINICADLIB_SOURCES
    "src/MiniCADEngine.cpp"
    ${SCENE_SOURCES}
    ${DOCUMENT_SOURCES}
    ${EDITOR_SOURCES}
    ${SERIALIZATION_SOURCES}
    ${IMPORT_SOURCES}
    ${VIEWPORT_SOURCES}
    ${TEXT_SOURCES}
    ${GEOM_SOURCES}
)

add_library(MiniCADLib STATIC ${MINICADLIB_SOURCES})

target_include_directories(MiniCADLib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/3rd
)

# DWG/DXF 只在库内部的导入导出代码中使用，公开头文件不暴露 MiniDWG 类型
target_link_libraries(MiniCADLib PRIVATE MiniDWG)

if(MSVC)
    target_compile_options(MiniCADLib PRIVATE /utf-8)
    target_compile_definitions(MiniCADLib PRIVATE
        UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

if(EMSCRIPTEN)
    target_compile_definitions(MiniCADLib PUBLIC MINICAD_WEB)
endif()

# ── 平台应用 ────────────────────────────────────────────────────────
if(WIN32 AND NOT EMSCRIPTEN)
    add_subdirectory("apps/Win32")
endif()

if(EMSCRIPTEN)
    add_subdirectory(wasm)
endif()

# ── 测试（桌面端）：tests/Core、tests/UI、tests/Serialization ─────────
option(MINICAD_BUILD_TESTS "Build MiniCAD tests" ON)
if(MINICAD_BUILD_TESTS AND NOT EMSCRIPTEN)
    enable_testing()
    add_subdirectory(tests)
endif()

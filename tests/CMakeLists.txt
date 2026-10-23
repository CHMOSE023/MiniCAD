# ── 测试 ────────────────────────────────────────────────────────────
# Core/          MiniCAD 核心库测试   → MiniCADTests
# UI/            MiniGUI 单元测试        → MiniGUITests
# UI/Golden/     MiniGUI 截图对比测试    → MiniGUIGolden（基准图片在 UI/Golden/baseline）
# Serialization/ MiniDWG 单元测试        → MiniDWGTests
# NodeGraph/     节点式参数化建模 → NodeGraphTests；NodeSolidTests（MINICAD_WITH_SOLID 时）
# Solid/         SMLib 冒烟测试、ISolidKernel 接口测试、三维实体、建模命令、出图、三维视图 → SMLibSmokeTests、SolidKernelTests、Solid3DEntityTests、SolidToolTests、SolidDrawingTests、SolidViewTests（MINICAD_WITH_SOLID 时）
# Data/          测试数据：Dwg/ 样例图纸（来自 ACadSharp，MIT），UI/ 截图测试用图标
# 各测试程序输出到构建目录根（out/<preset>/），与 ctest 一起使用

set(MINICAD_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Data")

add_executable(MiniCADTianzhengTests "Core/TianzhengWallTests.cpp")
target_include_directories(MiniCADTianzhengTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(MiniCADTianzhengTests PRIVATE MiniCAD MiniDWG)
if(MSVC)
    target_compile_options(MiniCADTianzhengTests PRIVATE /utf-8)
endif()
add_test(NAME MiniCADTianzhengTests COMMAND MiniCADTianzhengTests)

add_executable(MiniCADTchImportTests "Core/TchBlockInsertTests.cpp" "Core/TchProxyGraphicsTests.cpp")
target_link_libraries(MiniCADTchImportTests PRIVATE MiniCAD MiniDWG)
if(MSVC)
    target_compile_options(MiniCADTchImportTests PRIVATE /utf-8)
endif()
add_test(NAME MiniCADTchImportTests COMMAND MiniCADTchImportTests)

add_executable(MiniCADCadPreserveTests "Core/CadPreserveTests.cpp")
target_link_libraries(MiniCADCadPreserveTests PRIVATE MiniCAD MiniDWG)
if(MSVC)
    target_compile_options(MiniCADCadPreserveTests PRIVATE /utf-8)
endif()
add_test(NAME MiniCADCadPreserveTests COMMAND MiniCADCadPreserveTests)

add_executable(MiniCADTchOpeningTests "Core/TchOpeningTests.cpp")
target_link_libraries(MiniCADTchOpeningTests PRIVATE MiniCAD MiniDWG)
if(MSVC)
    target_compile_options(MiniCADTchOpeningTests PRIVATE /utf-8)
endif()
add_test(NAME MiniCADTchOpeningTests COMMAND MiniCADTchOpeningTests)

add_executable(MiniCADTchDimensionTests "Core/TchDimensionTests.cpp")
target_link_libraries(MiniCADTchDimensionTests PRIVATE MiniCAD MiniDWG)
if(MSVC)
    target_compile_options(MiniCADTchDimensionTests PRIVATE /utf-8)
endif()
add_test(NAME MiniCADTchDimensionTests COMMAND MiniCADTchDimensionTests)

add_executable(MiniCADTchSymbolTests "Core/TchSymbolTests.cpp")
target_link_libraries(MiniCADTchSymbolTests PRIVATE MiniCAD MiniDWG)
if(MSVC)
    target_compile_options(MiniCADTchSymbolTests PRIVATE /utf-8)
endif()
add_test(NAME MiniCADTchSymbolTests COMMAND MiniCADTchSymbolTests)

add_executable(MiniCADTchStairTests "Core/TchStairTests.cpp")
target_link_libraries(MiniCADTchStairTests PRIVATE MiniCAD MiniDWG)
if(MSVC)
    target_compile_options(MiniCADTchStairTests PRIVATE /utf-8)
endif()
add_test(NAME MiniCADTchStairTests COMMAND MiniCADTchStairTests)

add_executable(MiniCADTchPathArrayTests "Core/TchPathArrayTests.cpp")
target_link_libraries(MiniCADTchPathArrayTests PRIVATE MiniCAD MiniDWG)
if(MSVC)
    target_compile_options(MiniCADTchPathArrayTests PRIVATE /utf-8)
endif()
add_test(NAME MiniCADTchPathArrayTests COMMAND MiniCADTchPathArrayTests)

# 天正对象显示几何与天正 2014 分解结果比对（期望数据 Core/TchGeometryRef.inc 由 Core/tch_geomref.py 生成）
add_executable(MiniCADTchGeometryTests "Core/TchGeometryTests.cpp")
target_link_libraries(MiniCADTchGeometryTests PRIVATE MiniCAD MiniDWG)
target_compile_definitions(MiniCADTchGeometryTests PRIVATE MINICAD_TEST_DATA_DIR="${MINICAD_TEST_DATA_DIR}")
if(MSVC)
    target_compile_options(MiniCADTchGeometryTests PRIVATE /utf-8)
endif()
add_test(NAME MiniCADTchGeometryTests COMMAND MiniCADTchGeometryTests)

add_executable(MiniCADFace3DTests "Core/Face3DTests.cpp")
target_link_libraries(MiniCADFace3DTests PRIVATE MiniCAD MiniDWG)
if(MSVC)
    target_compile_options(MiniCADFace3DTests PRIVATE /utf-8)
endif()
add_test(NAME MiniCADFace3DTests COMMAND MiniCADFace3DTests)

function(minicad_test_output target)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    )
endfunction()

# ── MiniCAD 核心库测试 ────────────────────────────────────────────
if(WIN32)
    add_executable(MiniCADTests
        "Core/SpatialIndexTests.cpp"
        "Core/EllipseArcTests.cpp"
        "Core/HatchPatternTests.cpp"
        "Core/DimensionTests.cpp"
        "Core/TextStyleTests.cpp"
        "Core/SolidTests.cpp"
        "Core/WipeoutTests.cpp"
        "Core/ImageTests.cpp"
        "Core/MLineTests.cpp"
        "Core/TableTests.cpp"
        "Core/RegionTests.cpp"
        "Core/RegionBooleanTests.cpp"
        "Core/ChangeAttrTests.cpp"
        "Core/LayerCommandTests.cpp"
        "Core/PropertyTableTests.cpp"
        "Core/ScaleTests.cpp"
        "Core/StretchTests.cpp"
        "Core/FilletTests.cpp"
        "Core/ChamferTests.cpp"
        "Core/ExplodeTests.cpp"
        "Core/PolylineOpsTests.cpp"
        "Core/DimAssocTests.cpp"
        "Core/CadExchangeTests.cpp"
        "Core/AttribAxisTests.cpp"
        "Core/PolylineWidthTests.cpp"
        "Core/BulgeDirectionTests.cpp"
        "Core/PointStyleTests.cpp"
        "Core/LineTypeScaleTests.cpp"
        "Core/HatchDotsTests.cpp"
        "Core/MatrixInverseTests.cpp"
        "Core/RecentFilesTests.cpp"
        "Core/BlockFixTests.cpp"
        "Core/MiddleDoubleClickZoomTests.cpp"
        "Core/LayoutTests.cpp"
        "Core/MLineToolTests.cpp"
        "Core/MLineImportTests.cpp"
        "Core/MLineEditTests.cpp"
        "Core/TransformSinkTests.cpp"
    )
    target_link_libraries(MiniCADTests PRIVATE MiniCAD)
    target_compile_definitions(MiniCADTests PRIVATE MINIDWG_SAMPLE_DIR="${MINICAD_TEST_DATA_DIR}/Dwg")
    target_compile_definitions(MiniCADTests PRIVATE MINICAD_TCH_SAMPLE_DIR="${MINICAD_TEST_DATA_DIR}/Tch")
    target_compile_definitions(MiniCADTests PRIVATE MINICAD_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
    if(MSVC)
        target_compile_options(MiniCADTests PRIVATE /utf-8)
        target_compile_definitions(MiniCADTests PRIVATE UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    minicad_test_output(MiniCADTests)
    add_test(NAME MiniCADTests COMMAND MiniCADTests)

    add_executable(MiniCADDrawingOpenTests "Core/DrawingOpenTests.cpp")
    target_link_libraries(MiniCADDrawingOpenTests PRIVATE MiniCAD)
    target_compile_definitions(MiniCADDrawingOpenTests PRIVATE MINICAD_DRAWING_OPEN_TEST_MAIN UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(MiniCADDrawingOpenTests PRIVATE /utf-8)
    endif()
    minicad_test_output(MiniCADDrawingOpenTests)
    add_test(NAME MiniCADDrawingOpenTests COMMAND MiniCADDrawingOpenTests)

    # 本地诊断工具：tools/ 不进仓库（.gitignore），只在本机有源文件时编译
    if(EXISTS "${CMAKE_SOURCE_DIR}/tools/inspect_dwg.cpp")
        add_executable(MiniCADDwgInspect "${CMAKE_SOURCE_DIR}/tools/inspect_dwg.cpp")
        target_link_libraries(MiniCADDwgInspect PRIVATE MiniCAD MiniDWG)
        if(MSVC)
            target_compile_options(MiniCADDwgInspect PRIVATE /utf-8)
        endif()
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/tools/tch_patch.cpp")
        add_executable(MiniCADTchPatch "${CMAKE_SOURCE_DIR}/tools/tch_patch.cpp")
        target_link_libraries(MiniCADTchPatch PRIVATE MiniDWG)
        if(MSVC)
            target_compile_options(MiniCADTchPatch PRIVATE /utf-8)
        endif()
    endif()
endif()

# ── MiniGUI 单元测试（与平台无关，只链接核心库）─────────────────────────
add_executable(MiniGUITests
    "UI/TestMain.cpp"
    "UI/LayoutTests.cpp"
    "UI/EventTests.cpp"
    "UI/TextTests.cpp"
    "UI/InputTests.cpp"
    "UI/WidgetTests.cpp"
    "UI/EditingTests.cpp"
    "UI/HostTests.cpp"
    "UI/StyleTests.cpp"
    "UI/DataTests.cpp"
    "UI/DockTests.cpp"
    "UI/FrameTests.cpp"
    "UI/ConsoleTests.cpp"
)
target_include_directories(MiniGUITests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/UI)
target_link_libraries(MiniGUITests PRIVATE MiniGUI)
if(MSVC)
    target_compile_options(MiniGUITests PRIVATE /utf-8 /W4)
endif()
minicad_test_output(MiniGUITests)
add_test(NAME MiniGUITests COMMAND MiniGUITests)

# ── MiniGUI 截图对比测试：软件光栅 vs 基准图片，D3D11 vs 软件光栅 ─────────
# 基准图片需要更新时：out\<preset>\MiniGUIGolden.exe --update，检查图片后再提交
if(WIN32)
    add_executable(MiniGUIGolden
        "UI/Golden/GoldenMain.cpp"
        "UI/Golden/PngIO.cpp"
        "UI/Golden/D3D11Capture.cpp"
        "UI/Golden/Demo/DemoNodes.cpp"
        "UI/Golden/Demo/Gallery.cpp"
    )
    target_include_directories(MiniGUIGolden PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/UI/Golden
        ${CMAKE_CURRENT_SOURCE_DIR}/UI/Golden/Demo
        ${CMAKE_SOURCE_DIR}/3rd
    )
    target_link_libraries(MiniGUIGolden PRIVATE
        MiniGUI
        MiniGUI_Software
        MiniGUI_D3D11
        MiniGUI_Win32
        d3d11
        dxgi
    )
    target_compile_definitions(MiniGUIGolden PRIVATE
        MINIGUI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/UI/Golden/baseline"
        MINIGUI_GOLDEN_OUT="${CMAKE_BINARY_DIR}/golden_out"
        MINIGUI_ASSETS_DIR="${MINICAD_TEST_DATA_DIR}/UI"
        UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    if(MSVC)
        target_compile_options(MiniGUIGolden PRIVATE /utf-8 /W4)
    endif()
    minicad_test_output(MiniGUIGolden)
    add_test(NAME MiniGUIGolden COMMAND MiniGUIGolden)
endif()

# ── MiniDWG 单元测试（与平台无关，只链接核心库）─────────────────────────
add_executable(MiniDWGTests
    "Serialization/TestMain.cpp"
    "Serialization/VersionTests.cpp"
    "Serialization/FileFormatTests.cpp"
    "Serialization/DatabaseTests.cpp"
    "Serialization/ModelTests.cpp"
    "Serialization/DxfReadTests.cpp"
    "Serialization/DxfWriteTests.cpp"
    "Serialization/DwgReadTests.cpp"
    "Serialization/DwgWriteTests.cpp"
)
target_include_directories(MiniDWGTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Serialization)
target_link_libraries(MiniDWGTests PRIVATE MiniDWG)
# 样例图纸来自 ACadSharp（MIT），同一张图的各版本 DWG 与 ASCII/Binary DXF
target_compile_definitions(MiniDWGTests PRIVATE MINIDWG_SAMPLES_DIR="${MINICAD_TEST_DATA_DIR}/Dwg")
if(MSVC)
    target_compile_options(MiniDWGTests PRIVATE /utf-8 /W4)
endif()
minicad_test_output(MiniDWGTests)
add_test(NAME MiniDWGTests COMMAND MiniDWGTests)

# ── 节点图（docs/NodeGraph/PLAN.md）──────────────────────────────────
add_executable(NodeGraphTests "NodeGraph/NodeGraphTests.cpp")
target_link_libraries(NodeGraphTests PRIVATE MiniCAD MiniDWG)
if(MSVC)
    target_compile_options(NodeGraphTests PRIVATE /utf-8)
endif()
add_test(NAME NodeGraphTests COMMAND NodeGraphTests)

if(MINICAD_WITH_SOLID)
    add_executable(NodeSolidTests "NodeGraph/NodeSolidTests.cpp")
    target_link_libraries(NodeSolidTests PRIVATE MiniCADSolid MiniDWG)
    if(MSVC)
        target_compile_options(NodeSolidTests PRIVATE /utf-8)
    endif()
    target_compile_definitions(NodeSolidTests PRIVATE MINICAD_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")   # 示例节点图文件
    add_test(NAME NodeSolidTests COMMAND NodeSolidTests)
endif()

# ── SMLib 冒烟测试（静态链接的实体建模内核）─────────────────────────────
if(MINICAD_WITH_SOLID)
    add_executable(SMLibSmokeTests "Solid/SMLibSmokeTests.cpp")
    target_link_libraries(SMLibSmokeTests PRIVATE SM_API)
    if(MSVC)
        target_compile_options(SMLibSmokeTests PRIVATE /utf-8)
    endif()
    add_test(NAME SMLibSmokeTests COMMAND SMLibSmokeTests)

    add_executable(SolidKernelTests "Solid/SolidKernelTests.cpp")
    target_link_libraries(SolidKernelTests PRIVATE MiniCADSolid)
    if(MSVC)
        target_compile_options(SolidKernelTests PRIVATE /utf-8)
    endif()
    add_test(NAME SolidKernelTests COMMAND SolidKernelTests)

    add_executable(Solid3DEntityTests "Solid/Solid3DEntityTests.cpp")
    target_link_libraries(Solid3DEntityTests PRIVATE MiniCADSolid MiniDWG)
    if(MSVC)
        target_compile_options(Solid3DEntityTests PRIVATE /utf-8)
    endif()
    add_test(NAME Solid3DEntityTests COMMAND Solid3DEntityTests)

    add_executable(SolidToolTests "Solid/SolidToolTests.cpp")
    target_link_libraries(SolidToolTests PRIVATE MiniCADSolid MiniDWG)
    if(MSVC)
        target_compile_options(SolidToolTests PRIVATE /utf-8)
    endif()
    add_test(NAME SolidToolTests COMMAND SolidToolTests)

    add_executable(SolidDrawingTests "Solid/SolidDrawingTests.cpp")
    target_link_libraries(SolidDrawingTests PRIVATE MiniCADSolid MiniDWG)
    if(MSVC)
        target_compile_options(SolidDrawingTests PRIVATE /utf-8)
    endif()
    add_test(NAME SolidDrawingTests COMMAND SolidDrawingTests)

    add_executable(SolidViewTests "Solid/SolidViewTests.cpp")
    target_link_libraries(SolidViewTests PRIVATE MiniCADSolid MiniDWG)
    if(MSVC)
        target_compile_options(SolidViewTests PRIVATE /utf-8)
    endif()
    add_test(NAME SolidViewTests COMMAND SolidViewTests)
endif()

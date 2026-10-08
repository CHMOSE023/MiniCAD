# ── 测试 ────────────────────────────────────────────────────────────
# Core/          MiniCADLib 核心库测试   → MiniCADTests
# UI/            MiniGUI 单元测试        → MiniGUITests
# UI/Golden/     MiniGUI 截图对比测试    → MiniGUIGolden（基准图片在 UI/Golden/baseline）
# Serialization/ MiniDWG 单元测试        → MiniDWGTests
# Data/          测试数据：Dwg/ 样例图纸（来自 ACadSharp，MIT），UI/ 截图测试用图标
# 各测试程序输出到构建目录根（out/<preset>/），与 ctest 一起使用

set(MINICAD_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Data")

function(minicad_test_output target)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    )
endfunction()

# ── MiniCADLib 核心库测试 ────────────────────────────────────────────
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
    )
    target_link_libraries(MiniCADTests PRIVATE MiniCADLib)
    target_compile_definitions(MiniCADTests PRIVATE MINIDWG_SAMPLE_DIR="${MINICAD_TEST_DATA_DIR}/Dwg")
    if(MSVC)
        target_compile_options(MiniCADTests PRIVATE /utf-8)
        target_compile_definitions(MiniCADTests PRIVATE UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    minicad_test_output(MiniCADTests)
    add_test(NAME MiniCADTests COMMAND MiniCADTests)
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

# ── WASM target ──────────────────────────────────────────────────────────────
add_executable(WASM
    "${CMAKE_SOURCE_DIR}/wasm/WebRenderer.cpp"
    "${CMAKE_SOURCE_DIR}/wasm/MiniCADExports.cpp"
)

target_include_directories(WASM PRIVATE
    "${CMAKE_SOURCE_DIR}/wasm"
)

target_link_libraries(WASM PRIVATE MiniCADLib)

# ── Exported C symbols ───────────────────────────────────────────────────────
set(MINICAD_EXPORTS
    _malloc
    _free
    _MiniCAD_Init
    _MiniCAD_Resize
    _MiniCAD_Tick
    _MiniCAD_MouseMove
    _MiniCAD_MouseDown
    _MiniCAD_MouseUp
    _MiniCAD_Wheel
    _MiniCAD_KeyDown
    _MiniCAD_KeyUp
    _MiniCAD_RunCommand
    _MiniCAD_GetScreenLineData
    _MiniCAD_GetScreenLineCount
    _MiniCAD_GetScreenTriData
    _MiniCAD_GetScreenTriCount
    _MiniCAD_GetWorldLineData
    _MiniCAD_GetWorldLineCount
    _MiniCAD_GetWorldTriData
    _MiniCAD_GetWorldTriCount
    _MiniCAD_GetTextData
    _MiniCAD_GetTextCount
    _MiniCAD_GetScreenVP
    _MiniCAD_GetWorldVP
    _MiniCAD_SetFontTexId
    _MiniCAD_GetFontTexId
    _MiniCAD_GetPrompt
    _MiniCAD_GetLineCount
    _MiniCAD_GetLine
    _MiniCAD_ConsumeScrollToBottom
    _MiniCAD_StartLine
    _MiniCAD_StartPoint
    _MiniCAD_StartRect
    _MiniCAD_StartCircle
    _MiniCAD_StartArc
    _MiniCAD_StartEllipse
    _MiniCAD_StartPolyline
    _MiniCAD_StartSpline
    _MiniCAD_StartText
    _MiniCAD_StartMText
    _MiniCAD_StartMove
    _MiniCAD_StartCopy
    _MiniCAD_StartMirror
    _MiniCAD_StartRotate
    _MiniCAD_DeleteSelected
    _MiniCAD_Undo
    _MiniCAD_Redo
    _MiniCAD_IsSnapEnabled
    _MiniCAD_IsOrthoEnabled
    _MiniCAD_ToggleSnap
    _MiniCAD_ToggleOrtho
    _MiniCAD_GetSnapModes
    _MiniCAD_SetSnapModes
    _MiniCAD_SetSnapRadius
    _MiniCAD_IsTextInputActive
    _MiniCAD_IsMTextInputActive
    _MiniCAD_SubmitTextInput
    _MiniCAD_SubmitMTextInput
    _MiniCAD_GetMouseWorldX
    _MiniCAD_GetMouseWorldY
    _MiniCAD_CopySelected
    _MiniCAD_CutSelected
    _MiniCAD_Paste
    _MiniCAD_NewDocument
    _MiniCAD_GetDocumentCount
    _MiniCAD_GetDocumentName
    _MiniCAD_IsDocumentDirty
    _MiniCAD_GetActiveDocument
    _MiniCAD_SetActiveDocument
    _MiniCAD_CloseDocument
    _MiniCAD_SaveActiveDocument
    _MiniCAD_GetSaveDataSize
    _MiniCAD_LoadDocument
    _MiniCAD_IsGridShown
    _MiniCAD_IsAxisShown
    _MiniCAD_IsGizmoShown
    _MiniCAD_ToggleGrid
    _MiniCAD_ToggleAxis
    _MiniCAD_ToggleGizmo
    _MiniCAD_StartTool
)

list(JOIN MINICAD_EXPORTS "," EXPORTS_STR)

option(MINICAD_WEB_DEBUG "Enable WASM debug build" OFF)

if(MINICAD_WEB_DEBUG)
    set(OPT_FLAGS -g3 -O0 -sASSERTIONS=2 -sSAFE_HEAP=1)
else()
    set(OPT_FLAGS -O2)
endif()

target_link_options(WASM PRIVATE
    ${OPT_FLAGS}
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8,HEAPF32,HEAP32,HEAPU8
    -sEXPORTED_FUNCTIONS=[${EXPORTS_STR}]
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=33554432   # 32 MB
    -sNO_EXIT_RUNTIME=1
    -sENVIRONMENT=web
    -sMODULARIZE=1
    -sEXPORT_NAME=createMiniCADModule
    --no-entry
    --preload-file "${CMAKE_SOURCE_DIR}/assets/fonts@/fonts"
)

target_compile_options(WASM PRIVATE
    ${OPT_FLAGS}
    -std=c++20
    -fexceptions
)

set_target_properties(WASM PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Wasm"
    OUTPUT_NAME "minicad"
    SUFFIX ".js"
)

# Copy WASM output to apps/Web/public for Vite
add_custom_command(TARGET WASM POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/Wasm/minicad.js"
        "${CMAKE_SOURCE_DIR}/out/Web/public/minicad.js"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/Wasm/minicad.wasm"
        "${CMAKE_SOURCE_DIR}/out/Web/public/minicad.wasm"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/Wasm/minicad.data"
        "${CMAKE_SOURCE_DIR}/out/Web/public/minicad.data"
    COMMENT "Copying WASM output to apps/Web/public"
)

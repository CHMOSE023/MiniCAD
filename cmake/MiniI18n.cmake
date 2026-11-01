# ── MiniI18n（界面文字的翻译，见 docs/I18n/PLAN.md）──────────────────────
# 只用标准库；MiniCAD、MiniGUI、MiniDWG 都链接它。头文件以 src/I18n 为根（"MiniI18n/I18n.h"），命名空间 MiniI18n。
# 翻译文件在 assets/i18n/<语言>.json，由共用应用（apps/Common 的 GUI/Language.cpp）读入后 SetCatalog

set(MINII18N_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/I18n")

add_library(MiniI18n STATIC
    "${MINII18N_SRC}/MiniI18n/I18n.cpp"
)

target_include_directories(MiniI18n
    PUBLIC ${MINII18N_SRC}
)

if(MSVC)
    target_compile_options(MiniI18n PRIVATE /utf-8 /W4)
endif()

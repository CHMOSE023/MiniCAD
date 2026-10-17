# 每次构建时由 apps/Win32/CMakeLists.txt 以 cmake -P 调用，生成 MiniCADVersion.h。
# 参数：VERSION（三段，来自根 CMakeLists.txt 的 MINICAD_VERSION）、SOURCE_DIR（仓库根）、INPUT、OUTPUT。
# 版本号第四段为构建号，取 git 提交数（git rev-list --count HEAD）；没有 git 或不是完整克隆时为 0。
# configure_file 内容不变时不改写文件，构建号没变就不会重新编译资源。

string(REPLACE "." ";" MINICAD_VERSION_PARTS "${VERSION}")
list(GET MINICAD_VERSION_PARTS 0 MINICAD_VERSION_MAJOR)
list(GET MINICAD_VERSION_PARTS 1 MINICAD_VERSION_MINOR)
list(GET MINICAD_VERSION_PARTS 2 MINICAD_VERSION_PATCH)
set(MINICAD_VERSION "${VERSION}")

set(MINICAD_VERSION_BUILD 0)
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_COMMIT_COUNT
        RESULT_VARIABLE GIT_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    # FILEVERSION 每段是 16 位
    if(GIT_RESULT EQUAL 0 AND GIT_COMMIT_COUNT MATCHES "^[0-9]+$" AND GIT_COMMIT_COUNT LESS 65536)
        set(MINICAD_VERSION_BUILD ${GIT_COMMIT_COUNT})
    endif()
endif()

configure_file("${INPUT}" "${OUTPUT}" @ONLY)

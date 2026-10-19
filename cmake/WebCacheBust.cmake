# 网页版构建后处理：给 index.html 引用的 .js 以及 .wasm / .data 加上内容哈希参数（?v=...）。
# 静态托管（如 Cloudflare 的浏览器缓存 TTL）会让浏览器缓存 .js 数小时，而 .wasm 不缓存，
# 发版后旧 .js 配新 .wasm 会在实例化时报 "function import requires a callable"。
# 用法：cmake -DHTML=<.../index.html> -P WebCacheBust.cmake

get_filename_component(dir  "${HTML}" DIRECTORY)
get_filename_component(stem "${HTML}" NAME_WE)

# 哈希取 .js 与 .wasm 的内容：两者任一变化，版本参数就变
set(sig "")
foreach(ext js wasm data)
    if(EXISTS "${dir}/${stem}.${ext}")
        file(MD5 "${dir}/${stem}.${ext}" h)
        string(APPEND sig "${h}")
    endif()
endforeach()
string(MD5 sig "${sig}")
string(SUBSTRING "${sig}" 0 12 ver)

file(READ "${HTML}" html)
# Emscripten 生成的 <script src="index.js">（Release 下压缩为 src=index.js，不带引号）；
# 重复构建时先去掉旧参数
string(REGEX REPLACE "src=\"?([^\" >?]+\\.js)(\\?v=[0-9a-f]*)?\"?" "src=\"\\1?v=${ver}\"" html "${html}")
# 外壳页面里 locateFile 用的占位符（或上次写入的版本）；压缩后形如 kBuildTag=""
string(REGEX REPLACE "kBuildTag( ?= ?)\"[^\"]*\"" "kBuildTag\\1\"${ver}\"" html "${html}")
file(WRITE "${HTML}" "${html}")

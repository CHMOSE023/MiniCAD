# 核对 AGENTS.md 与 .agents/ 下的文档是否还和代码一致。
#   pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/scripts/check-docs.ps1             # 引用检查 + 按改动提醒
#   pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/scripts/check-docs.ps1 -Base main  # 改动按与 main 的差异算
#   pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/scripts/check-docs.ps1 -Paths src/Import/TchWall.h   # 改这些文件要看哪些文档
# 1. 引用检查：文档里 `反引号` 中的路径、`类::成员`、MINICAD_* 环境变量，在代码里找不到就报出来
# 2. 改动提醒：文档开头的 covers 声明了它覆盖的代码；这些代码改了而文档没改，提醒可能需要更新
# 退出码 = 找不到的引用个数（改动提醒不计入）
[CmdletBinding(PositionalBinding = $false)]
param([string]$Base = 'HEAD', [string[]]$Paths)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location $root
try {
    $docs = @(Get-Item AGENTS.md) + @(Get-ChildItem .agents -Recurse -Filter *.md)
    $codeDirs = 'src', 'apps', 'tests', 'tools', 'cmake', '.agents'
    # 文档里的路径可以相对仓库根目录，也可以相对各库的 include 根（如 `GUI/MainCommands.cpp`、`Core/Log.h`）；
    # 不带扩展名的按类名找 .h / .hpp / .cpp（如 `GUI/MainFrame`）
    $bases = '', 'src', 'src/UI', 'src/Serialization', 'apps/Common/src', 'apps/Common/src/GUI', 'apps/Win32/src', 'apps/Web/src', 'assets', 'tests'
    function Test-DocPath([string]$p) {
        foreach ($b in $bases) {
            $full = if ($b) { Join-Path (Join-Path $root $b) $p } else { Join-Path $root $p }
            if (Test-Path $full) { return $true }
            if ([IO.Path]::GetExtension($p) -eq '') { foreach ($ext in '.h', '.hpp', '.cpp') { if (Test-Path "$full$ext") { return $true } } }
        }
        return $false
    }

    # 代码里出现过的单词（标识符、环境变量），用于判断 `类::成员` 是否还在
    $words = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($f in Get-ChildItem $codeDirs -Recurse -File -Include *.h, *.hpp, *.cpp, *.cjs, *.ps1, *.cmake, CMakeLists.txt) {
        foreach ($m in [regex]::Matches([IO.File]::ReadAllText($f.FullName), '[A-Za-z_][A-Za-z0-9_]*')) { [void]$words.Add($m.Value) }
    }

    $missing = @()
    foreach ($doc in $docs) {
        $rel  = [IO.Path]::GetRelativePath($root, $doc.FullName).Replace('\', '/')
        $text = [IO.File]::ReadAllText($doc.FullName)
        # 去掉代码块（示例代码与命令不逐字核对）
        $text = [regex]::Replace($text, '(?s)```.*?```', '')
        foreach ($m in [regex]::Matches($text, '`([^`\r\n]+)`')) {
            $s = $m.Groups[1].Value.Trim()
            if ($s -match '[<>*|{}\s]' -or $s -match '^(out|tests/Data|/)' ) { continue }   # 占位、通配、多选、运行时路径、虚拟文件系统路径
            if ($s -match '^[\w.-]+(/[\w.-]+)+/?$' -and $s -notmatch '^\.\.?/') {
                # 路径：相对仓库根目录；`Foo.*` 这类已被上面跳过
                if (-not (Test-DocPath $s)) { $missing += "$rel：路径 $s" }
            } elseif ($s -match '^([A-Za-z_]\w*::)+(~?[A-Za-z_]\w*)(\(\))?$') {
                foreach ($part in ($s -replace '\(\)$', '' -split '::')) {
                    if (-not $words.Contains($part.TrimStart('~'))) { $missing += "$rel：$s（找不到 $part）"; break }
                }
            } elseif ($s -match '^(MINICAD_[A-Z0-9_]+)(=.*)?$') {
                if (-not $words.Contains($Matches[1])) { $missing += "$rel：环境变量 $($Matches[1])" }
            }
        }
    }

    # 改动提醒
    $changed = if ($Paths) { @($Paths | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().Replace('\', '/') } | Where-Object { $_ }) }
               else { @(git diff --name-only $Base 2>$null) + @(git ls-files --others --exclude-standard 2>$null) | Where-Object { $_ } | Sort-Object -Unique }
    $hints = @()
    foreach ($doc in $docs) {
        $head = Get-Content $doc.FullName -TotalCount 12 -Encoding utf8
        $line = $head | Where-Object { $_ -match '^covers:\s*(.+)$' } | Select-Object -First 1
        if (-not $line) { continue }
        $patterns = ($line -replace '^covers:\s*', '') -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }
        $rel = [IO.Path]::GetRelativePath($root, $doc.FullName).Replace('\', '/')
        if ($changed -contains $rel) { continue }   # 文档本身也改了
        $hit = @($changed | Where-Object { $f = $_; $patterns | Where-Object { $f -like $_ } } | Select-Object -First 3)
        if ($hit) { $hints += "$rel ← 改了 $($hit -join '、')" }
    }

    if ($missing) {
        Write-Output "文档引用找不到（$($missing.Count) 处，改名 / 删除后文档没跟上）："
        $missing | ForEach-Object { "  $_" }
    } else {
        Write-Output "文档引用检查：$($docs.Count) 份文档，没有找不到的引用"
    }
    if ($hints) {
        Write-Output '代码改了、对应文档没改（如果行为、接口或约束变了，请一起更新）：'
        $hints | ForEach-Object { "  $_" }
    }
    exit $missing.Count
} finally { Pop-Location }

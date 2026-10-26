# 按需编译并运行测试，只输出汇总。
#   pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/skills/test/run.ps1                 # 按 git 改动挑测试
#   pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/skills/test/run.ps1 MiniDWGTests    # 指定测试（可多个，逗号或空格分隔）
#   pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/skills/test/run.ps1 all             # 全部 ctest
# 选项：-Preset release；-Base <提交>（改动按与该提交的差异算，默认 HEAD，即工作区里还没提交的改动）
# 完整输出在 out/tmp/test/<目标>.txt；退出码 = 编译失败 1，否则为失败的测试程序个数
[CmdletBinding(PositionalBinding = $false)]   # 只有测试名按位置传，-Preset / -Base 要写名字
param(
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Tests,
    [string]$Preset = 'debug',
    [string]$Base = 'HEAD'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$bin  = Join-Path $root "out/$Preset"
$logs = Join-Path $root 'out/tmp/test'
New-Item -ItemType Directory -Force $logs | Out-Null
# 本机从项目目录启动的程序写不了系统 TEMP，测试的临时文件放到项目内
$env:TEMP = Join-Path $root 'out/tmp'; $env:TMP = $env:TEMP

# 顺带核对 .agents 文档与代码是否一致（不影响退出码），见 .agents/scripts/check-docs.ps1
function Invoke-DocCheck {
    Write-Output ''
    & pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root '.agents/scripts/check-docs.ps1') -Base $Base
}

# ── 改动 → 测试目标 ─────────────────────────────────────────────
# 按顺序匹配，一个文件可命中多条。'MiniCADWin' 只编译不运行；'js' 表示运行 tests/Web/*.cjs
$tch = 'MiniCADTchImportTests','MiniCADTchOpeningTests','MiniCADTchDimensionTests','MiniCADTchSymbolTests',
       'MiniCADTchStairTests','MiniCADTchPathArrayTests','MiniCADTchGeometryTests','MiniCADTianzhengTests'
$solid = 'SolidKernelTests','Solid3DEntityTests','SolidToolTests','SolidDrawingTests','SolidViewTests'
$rules = @(
    @('^tests/Solid/(\w+)\.cpp$',                         '$1'),
    @('^tests/NodeGraph/NodeSolid',                       'NodeSolidTests'),
    @('^tests/NodeGraph/',                                'NodeGraphTests'),
    @('^tests/Serialization/',                            'MiniDWGTests'),
    @('^tests/UI/',                                       'MiniGUITests'),
    @('^tests/Web/',                                      'js'),
    @('^tests/Core/TianzhengWallTests',                   'MiniCADTianzhengTests'),
    @('^tests/Core/(TchBlockInsert|TchProxyGraphics)Tests', 'MiniCADTchImportTests'),
    @('^tests/Core/(Tch\w+|CadPreserve|DrawingOpen|Face3D)Tests\.cpp$', 'MiniCAD$1Tests'),
    @('^tests/Core/(?!Tch|Tianzheng|CadPreserve|DrawingOpen|Face3D)', 'MiniCADTests'),
    @('^src/Serialization/(Codec|Database|Dwg|Dxf)/',     'MiniDWGTests','MiniCADTests'),
    @('^src/Import/Tch',                                  ($tch + 'MiniCADTests')),
    @('^src/Import/',                                     'MiniCADTests','MiniCADCadPreserveTests','MiniCADDrawingOpenTests'),
    @('^src/Document/(Document|DocumentManager)\.',       'MiniCADDrawingOpenTests'),
    @('^src/Core/Entity/Face3D',                          'MiniCADFace3DTests'),
    @('^src/Solid/|Solid3DEntity|MeshEntity',             $solid),
    @('^src/NodeGraph/Builtin(Solid|Surface|Form|Mesh)',  'NodeSolidTests'),
    @('^src/NodeGraph/',                                  'NodeGraphTests'),
    @('^src/UI/',                                         'MiniGUITests'),
    @('^src/(Core|Scene|Document|Editor|Serialization|Text|Viewport|Render|Import)/', 'MiniCADTests'),
    @('^apps/Web/',                                       'js'),
    @('^apps/(Common|Win32)/|^assets/ui/',                'MiniCADWin')
)

function Get-ChangedFiles {
    Push-Location $root
    try {
        $files  = @(git diff --name-only $Base 2>$null)
        $files += @(git ls-files --others --exclude-standard 2>$null)
        return $files | Where-Object { $_ } | Sort-Object -Unique
    } finally { Pop-Location }
}

function Get-AllCtestTargets {
    $list = & ctest --test-dir $bin -N 2>$null | Select-String -Pattern 'Test\s+#\d+:\s+(\S+)' | ForEach-Object { $_.Matches[0].Groups[1].Value }
    return @($list)
}

$targets = [System.Collections.Generic.List[string]]::new()
$Tests = @($Tests | ForEach-Object { $_ -split '[,\s]+' } | Where-Object { $_ })
if ($Tests.Count -eq 1 -and $Tests[0] -eq 'all') {
    Get-AllCtestTargets | ForEach-Object { $targets.Add($_) }
    $targets.Add('js')
} elseif ($Tests.Count -gt 0) {
    $Tests | ForEach-Object { $targets.Add($_) }
} else {
    $changed = Get-ChangedFiles
    if (-not $changed) { Write-Output "没有改动（与 $Base 相比），没有要跑的测试。可以指定测试名或 all。"; exit 0 }
    foreach ($f in $changed) {
        foreach ($r in $rules) {
            $m = [regex]::Match($f, $r[0])
            if (-not $m.Success) { continue }
            foreach ($t in $r[1..($r.Count - 1)] | ForEach-Object { $_ }) {
                $name = $t.Replace('$1', $m.Groups[1].Value)   # 规则里的 $1 换成捕获组（如 tests/Solid/X.cpp → X）
                if (-not $targets.Contains($name)) { $targets.Add($name) }
            }
        }
    }
    Write-Output ("改动 {0} 个文件 → {1}" -f @($changed).Count, ($(if ($targets.Count) { $targets -join ', ' } else { '（没有对应的测试）' })))
    if ($targets.Count -eq 0) { Invoke-DocCheck; exit 0 }
}

# ── 编译 ────────────────────────────────────────────────────────
$buildTargets = @($targets | Where-Object { $_ -ne 'js' })
if ($buildTargets.Count -gt 0) {
    $vcvars = if ($env:MINICAD_VCVARS) { $env:MINICAD_VCVARS } else { 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat' }
    $build  = "cmake --build `"$bin`" --target $($buildTargets -join ' ')"
    $cmd    = if ($env:VCINSTALLDIR) { $build } else { "call `"$vcvars`" >nul 2>&1 && $build" }
    $buildLog = Join-Path $logs 'build.txt'
    $sw = [Diagnostics.Stopwatch]::StartNew()
    cmd /c "$cmd 2>&1" | Out-File -Encoding utf8 $buildLog
    $buildCode = $LASTEXITCODE
    if ($buildCode -ne 0) {
        Write-Output "编译失败（$([int]$sw.Elapsed.TotalSeconds) s），完整输出 $buildLog："
        Select-String -Path $buildLog -Pattern 'error|FAILED|unknown target' | Select-Object -First 30 | ForEach-Object { '  ' + $_.Line.Trim() }
        exit 1
    }
    Write-Output "编译通过（$([int]$sw.Elapsed.TotalSeconds) s）：$($buildTargets -join ', ')"
}

# ── 运行 ────────────────────────────────────────────────────────
$failedPrograms = 0
$rows = @()
foreach ($t in $targets) {
    if ($t -eq 'MiniCADWin') { $rows += [pscustomobject]@{ 测试 = 'MiniCADWin'; 结果 = '只编译（界面改动请实际运行或 --selftest）'; 用时 = '' }; continue }
    if ($t -eq 'js') {
        foreach ($js in Get-ChildItem (Join-Path $root 'tests/Web') -Filter *.cjs) {
            $log = Join-Path $logs "$($js.BaseName).txt"
            $sw = [Diagnostics.Stopwatch]::StartNew()
            & node $js.FullName *> $log
            $code = $LASTEXITCODE
            if ($code -ne 0) { $failedPrograms++ }
            $rows += [pscustomobject]@{ 测试 = $js.Name; 结果 = $(if ($code -eq 0) { '通过' } else { "失败（见 $log）" }); 用时 = '{0:N1} s' -f $sw.Elapsed.TotalSeconds }
        }
        continue
    }
    # 用 minicad_test_output 的程序在 out/<preset>/，其余在 out/<preset>/tests/
    $exe = @((Join-Path $bin "$t.exe"), (Join-Path $bin "tests/$t.exe")) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $exe) { $rows += [pscustomobject]@{ 测试 = $t; 结果 = '找不到程序（目标名不对或未启用）'; 用时 = '' }; $failedPrograms++; continue }
    $log = Join-Path $logs "$t.txt"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    Push-Location $bin
    try { & $exe *> $log; $code = $LASTEXITCODE } finally { Pop-Location }
    $text   = Get-Content $log -Encoding utf8
    $passed = @($text | Select-String -Pattern '^\[\s*(通过|PASS)\s*\]').Count
    $failed = @($text | Select-String -Pattern '^\[\s*(失败|FAIL)\s*\]')
    $result = if ($code -eq 0) { "通过（$passed 项）" }
              elseif ($code -lt 0 -or $code -gt 100000) { "崩溃（退出码 $code，最后的输出见 $log）" }
              elseif ($failed.Count -eq 0) { "退出码 $code 但没有失败行：可能是 abort / 未捕获的异常（退出码 3），见 $log" }
              else { "失败 $code 项" }
    if ($code -ne 0) { $failedPrograms++ }
    $rows += [pscustomobject]@{ 测试 = $t; 结果 = $result; 用时 = '{0:N1} s' -f $sw.Elapsed.TotalSeconds }
    foreach ($line in $failed | Select-Object -First 15) { $rows += [pscustomobject]@{ 测试 = ''; 结果 = '  ' + $line.Line.Trim(); 用时 = '' } }
}
$rows | Format-Table -AutoSize -Wrap | Out-String -Width 200 | Write-Output
Write-Output "完整输出：$logs"
Invoke-DocCheck
exit $failedPrograms

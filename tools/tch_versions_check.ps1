# 多版本天正图纸解析核对（本机图纸、不入库）。
# 用法: pwsh -File tools/tch_versions_check.ps1 -Dir "C:\...\天正解析" [-Inspect "out\release\tests\MiniCADDwgInspect.exe"]
# 逐个用 MiniCADDwgInspect 打开图纸，统计「显示为普通几何/轮廓」的类型数（成功解析）
# 与「无法恢复显示」的类型数（失败），任一文件成功类型 < 阈值 则报错退出。
param(
    [string]$Dir = "C:\Users\Admin\Desktop\test\天正解析",
    [string]$Inspect = "out\release\tests\MiniCADDwgInspect.exe",
    [int]$MinParsed = 14
)
$ErrorActionPreference = "Continue"
$esc = [char]27
$ansi = "$esc\[[0-9;]*m"
$root = if ($PSScriptRoot) { Split-Path -Parent $PSScriptRoot } else { (Get-Location).Path }
Push-Location $root
try {
    if (-not (Test-Path $Inspect)) { Write-Error "找不到 $Inspect，先编译 MiniCADDwgInspect"; return 2 }
    $tmp = Join-Path $root "out\tmp\tch_versions_check"
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    $files = Get-ChildItem -LiteralPath $Dir -Filter "*.dwg" | Sort-Object Name
    if ($files.Count -eq 0) { Write-Error "$Dir 里没有 .dwg"; return 2 }
    $env:TEMP = Join-Path $root "out\tmp"; $env:TMP = $env:TEMP
    $bad = 0
    Write-Output ("{0,-24} {1,8} {2,8} {3,8}" -f "文件", "实体", "解析", "失败")
    foreach ($f in $files) {
        $log = Join-Path $tmp ($f.BaseName + ".txt")
        & (Resolve-Path $Inspect).Path $f.FullName *> $log 2>&1
        $lines = Get-Content $log -Encoding utf8 | ForEach-Object { ($_ -replace $ansi, "") -replace '^MiniCADDwgInspect\.exe : ', '' }
        $ents = 0
        if (($lines | Select-String -Pattern 'MiniCAD loaded=' | Select-Object -First 1).Line -match 'entities=(\d+)') { $ents = [int]$Matches[1] }
        $parsed = ($lines | Select-String -Pattern '显示为普通(几何|轮廓)' | Measure-Object).Count
        $failed = ($lines | Select-String -Pattern '无法恢复显示' | Measure-Object).Count
        Write-Output ("{0,-24} {1,8} {2,8} {3,8}" -f $f.Name, $ents, $parsed, $failed)
        if ($parsed -lt $MinParsed) { Write-Output ("  !! 解析类型数 {0} < {1}" -f $parsed, $MinParsed); $bad++ }
    }
    Write-Output ("合计 {0} 个文件，不达标 {1} 个" -f $files.Count, $bad)
    return $bad
} finally { Pop-Location }

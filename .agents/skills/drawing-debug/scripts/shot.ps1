# 用桌面版离屏截图：打开图纸、全部显示，把模型页、第一张布局页、第一个 OLE 对象的视口存成 BMP，再转成 PNG。
#   pwsh -NoProfile -ExecutionPolicy Bypass -File .agents/skills/drawing-debug/scripts/shot.ps1 图纸 [-Pause 秒] [-Release] [-Views "x0,y0,x1,y1;..."]
# -Views：模型页再按这些世界坐标窗口各截一张 view<序号>.png（与 AutoCAD 的 ZOOM W 对照）
# 输出在 out/tmp/shots/<图纸名>/：*.png 与 bench.txt（含日志）
[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Drawing,
    [int]$Pause = 0,
    [switch]$Release,
    [string]$Views = ''
)
$ErrorActionPreference = 'Stop'
$root    = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$preset  = if ($Release) { 'release' } else { 'debug' }
$exe     = Join-Path $root "out/$preset/MiniCADWin/MiniCADWin.exe"
$file    = (Resolve-Path ($Drawing -join ' ')).Path
if (-not (Test-Path $exe)) { Write-Output "找不到 $exe，先编译 MiniCADWin"; exit 1 }

$out = Join-Path $root ("out/tmp/shots/" + [IO.Path]::GetFileNameWithoutExtension($file))
New-Item -ItemType Directory -Force $out | Out-Null
Get-ChildItem $out -Include *.bmp, *.png -Recurse | Remove-Item -Force

# 本机从项目目录启动的程序写不了系统 TEMP
$env:TEMP = Join-Path $root 'out/tmp'; $env:TMP = $env:TEMP
$env:MINICAD_BENCH_SHOT = $out
if ($Pause -gt 0) { $env:MINICAD_BENCH_PAUSE = "$Pause" } else { Remove-Item Env:MINICAD_BENCH_PAUSE -ErrorAction SilentlyContinue }
if ($Views) { $env:MINICAD_BENCH_VIEWS = $Views } else { Remove-Item Env:MINICAD_BENCH_VIEWS -ErrorAction SilentlyContinue }

$log = Join-Path $out 'bench.txt'
Push-Location (Split-Path $exe)
try { & $exe --bench $file *> $log; $code = $LASTEXITCODE } finally { Pop-Location; Remove-Item Env:MINICAD_BENCH_SHOT, Env:MINICAD_BENCH_PAUSE, Env:MINICAD_BENCH_VIEWS -ErrorAction SilentlyContinue }

Add-Type -AssemblyName System.Drawing
$pngs = foreach ($bmp in Get-ChildItem $out -Filter *.bmp) {
    $png = [IO.Path]::ChangeExtension($bmp.FullName, '.png')
    $img = [System.Drawing.Image]::FromFile($bmp.FullName)
    try { $img.Save($png, [System.Drawing.Imaging.ImageFormat]::Png); "$png（$($img.Width)×$($img.Height)）" } finally { $img.Dispose() }
    Remove-Item $bmp.FullName
}
Write-Output "退出码 $code；日志 $log"
# -Views 的每张截图：画面左上、右下角的世界坐标（适配时扣掉了功能区、面板挡住的留边，画面不以窗口为中心）
Select-String -Path $log -Pattern '画面左上' -Encoding utf8 | ForEach-Object { '  ' + ($_.Line -replace '^.*（', '（') }
if ($pngs) { $pngs | ForEach-Object { "截图：$_" } } else { Write-Output '没有生成截图，看日志' }
Select-String -Path $log -Pattern 'WARN|ERROR' -Encoding utf8 | Select-Object -First 15 | ForEach-Object { '  ' + $_.Line.Trim() }

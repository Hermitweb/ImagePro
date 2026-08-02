#Requires -Version 5.1
<#
.SYNOPSIS
    收集 ImagePro 运行时依赖到部署目录（供 vpk pack 打包，不构建、不压缩）。
.DESCRIPTION
    给定已构建的 build 目录（含 ImagePro.exe），运行 windeployqt 并递归收集
    Qt / libvips / MinGW 运行时 / velopack.dll / 翻译文件到目标部署目录。
#>
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [string]$DeployDir = "$PSScriptRoot/deploy/ImagePro",
    [string]$MsysRoot = "C:/msys64",
    [string]$SourceRoot = "$PSScriptRoot"
)

$ErrorActionPreference = "Stop"
$mingwBin = "$MsysRoot/mingw64/bin"
$env:PATH = "$mingwBin;$env:PATH"

if (-not (Test-Path "$BuildDir/ImagePro.exe")) {
    throw "ImagePro.exe not found in BuildDir: $BuildDir"
}

# 1. 准备部署目录
Write-Host "==> Preparing deploy directory: $DeployDir"
if (Test-Path $DeployDir) { Remove-Item -Recurse -Force $DeployDir }
New-Item -ItemType Directory -Force -Path $DeployDir | Out-Null

# 2. 主程序 + Qt 依赖
Write-Host "==> Copying ImagePro.exe and Qt dependencies..."
Copy-Item "$BuildDir/ImagePro.exe" $DeployDir

# windeployqt 在不同 MSYS2 版本中命名不一（windeployqt-qt5.exe / windeployqt.exe）。
$windeployqt = @("$mingwBin/windeployqt-qt5.exe", "$mingwBin/windeployqt.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $windeployqt) {
    throw "windeployqt not found in $mingwBin (install mingw-w64-x86_64-qt5-tools)."
}
Write-Host "==> Using windeployqt: $windeployqt"
& $windeployqt "$DeployDir/ImagePro.exe" `
    --release --no-translations --no-compiler-runtime --no-opengl-sw --dir $DeployDir

# 3. 编译器运行时与 libvips
Write-Host "==> Copying compiler runtime and libvips..."
foreach ($dll in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll',
                   'libvips-42.dll', 'libvips-cpp-42.dll')) {
    Copy-Item "$mingwBin/$dll" $DeployDir -ErrorAction SilentlyContinue
}

# 4. 递归收集剩余依赖
Write-Host "==> Collecting transitive dependencies..."
$systemDlls = @(
    'KERNEL32.dll', 'KERNELBASE.dll', 'ntdll.dll', 'msvcrt.dll',
    'USER32.dll', 'GDI32.dll', 'SHELL32.dll', 'ole32.dll', 'OLEAUT32.dll',
    'COMDLG32.dll', 'ADVAPI32.dll', 'WS2_32.dll', 'SHLWAPI.dll',
    'COMCTL32.dll', 'WINMM.dll', 'IMM32.dll', 'OPENGL32.dll',
    'd3d11.dll', 'dxgi.dll', 'D3Dcompiler_47.dll', 'libEGL.dll', 'libGLESv2.dll'
)

function Get-Dependencies($file) {
    $deps = & "$mingwBin/objdump.exe" -p $file 2>$null |
        Select-String "DLL Name:\s+(.+)" |
        ForEach-Object { $_.Matches.Groups[1].Value.Trim() }
    return $deps | Where-Object { $systemDlls -notcontains $_ }
}

function Copy-Dependency($dllName) {
    $src = Join-Path $mingwBin $dllName
    $dst = Join-Path $DeployDir $dllName
    if (-not (Test-Path $src)) { return }
    if (Test-Path $dst) { return }
    Copy-Item $src $dst -Force
    foreach ($dep in Get-Dependencies $src) { Copy-Dependency $dep }
}

foreach ($dll in (Get-ChildItem $DeployDir -Filter "*.dll")) {
    foreach ($dep in Get-Dependencies $dll.FullName) { Copy-Dependency $dep }
}
foreach ($exe in (Get-ChildItem $DeployDir -Filter "*.exe")) {
    foreach ($dep in Get-Dependencies $exe.FullName) { Copy-Dependency $dep }
}

# 5. 翻译文件
Write-Host "==> Copying translations..."
$transDir = "$SourceRoot/translations"
if (Test-Path $transDir) {
    Copy-Item "$transDir/*.qm" $DeployDir -ErrorAction SilentlyContinue
}

# 6. velopack.dll（WITH_VELOPACK 构建时 CMake 已拷到 exe 旁；兜底再确认一次）
$vpkSrc = "$BuildDir/velopack.dll"
if (Test-Path $vpkSrc) {
    Copy-Item $vpkSrc "$DeployDir/velopack.dll" -Force
    Write-Host "==> velopack.dll included."
} else {
    Write-Host "==> WARNING: velopack.dll not found in build dir (non-Velopack build?)."
}

Write-Host "==> Deploy directory ready: $DeployDir"
Write-Host "==> File count: $((Get-ChildItem $DeployDir).Count)"

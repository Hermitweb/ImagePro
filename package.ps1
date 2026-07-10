#Requires -Version 5.1
<#
.SYNOPSIS
    打包 ImagePro Windows 64 位发布包。
.DESCRIPTION
    使用 MSYS2 MinGW64 工具链构建 Release，收集 Qt/vips 依赖，生成 zip。
#>
param(
    [string]$MsysRoot = "C:/msys64",
    [string]$BuildType = "Release",
    [string]$Version = "1.0.0"
)

$ErrorActionPreference = "Stop"

$mingwBin = "$MsysRoot/mingw64/bin"
$deployDir = "$PSScriptRoot/deploy/ImagePro"
$buildDir = "$PSScriptRoot/build"

# 确保 MSYS2 工具链优先
$env:PATH = "$mingwBin;$env:PATH"

# 1. 配置并构建
Write-Host "==> Configuring CMake..."
cmake -B $buildDir -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=$BuildType -DBUILD_TESTS=OFF .

Write-Host "==> Building..."
cmake --build $buildDir -j4

# 2. 清理并创建部署目录
Write-Host "==> Preparing deploy directory..."
if (Test-Path $deployDir) {
    Remove-Item -Recurse -Force $deployDir
}
New-Item -ItemType Directory -Force -Path $deployDir | Out-Null

# 3. 复制主程序与 Qt 依赖
Write-Host "==> Deploying Qt dependencies..."
Copy-Item "$buildDir/ImagePro.exe" $deployDir
& windeployqt-qt5.exe "$deployDir/ImagePro.exe" `
    --release --no-translations --no-compiler-runtime --no-opengl-sw --dir $deployDir

# 4. 复制编译器运行时与 vips
Write-Host "==> Copying compiler runtime and libvips..."
$runtimeDlls = @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')
foreach ($dll in $runtimeDlls) {
    Copy-Item "$mingwBin/$dll" $deployDir -ErrorAction SilentlyContinue
}
$vipsDlls = @('libvips-42.dll', 'libvips-cpp-42.dll')
foreach ($dll in $vipsDlls) {
    Copy-Item "$mingwBin/$dll" $deployDir -ErrorAction SilentlyContinue
}

# 5. 递归收集剩余依赖
Write-Host "==> Collecting transitive dependencies..."
$systemDlls = @(
    'KERNEL32.dll', 'KERNELBASE.dll', 'ntdll.dll', 'msvcrt.dll',
    'USER32.dll', 'GDI32.dll', 'SHELL32.dll', 'ole32.dll', 'OLEAUT32.dll',
    'COMDLG32.dll', 'ADVAPI32.dll', 'WS2_32.dll', 'SHLWAPI.dll',
    'COMCTL32.dll', 'WINMM.dll', 'IMM32.dll', 'OPENGL32.dll',
    'd3d11.dll', 'dxgi.dll', 'D3Dcompiler_47.dll', 'libEGL.dll', 'libGLESv2.dll'
)

function Get-Dependencies($file) {
    $deps = & objdump -p $file 2>$null | Select-String "DLL Name:\s+(.+)" | ForEach-Object { $_.Matches.Groups[1].Value.Trim() }
    return $deps | Where-Object { $systemDlls -notcontains $_ }
}

function Copy-Dependency($dllName) {
    $src = Join-Path $mingwBin $dllName
    $dst = Join-Path $deployDir $dllName
    if (-not (Test-Path $src)) { return }
    if (Test-Path $dst) { return }
    Copy-Item $src $dst -Force
    foreach ($dep in Get-Dependencies $src) {
        Copy-Dependency $dep
    }
}

foreach ($dll in (Get-ChildItem $deployDir -Filter "*.dll")) {
    foreach ($dep in Get-Dependencies $dll.FullName) {
        Copy-Dependency $dep
    }
}
foreach ($exe in (Get-ChildItem $deployDir -Filter "*.exe")) {
    foreach ($dep in Get-Dependencies $exe.FullName) {
        Copy-Dependency $dep
    }
}

# 6. 复制翻译文件
Write-Host "==> Copying translations..."
$transDir = "$PSScriptRoot/translations"
if (Test-Path $transDir) {
    Copy-Item "$transDir/*.qm" $deployDir -ErrorAction SilentlyContinue
}

# 7. 打包
Write-Host "==> Creating zip archive..."
$commit = (git rev-parse --short HEAD)
$zipName = "ImagePro-v$Version-$commit-win64.zip"
Compress-Archive -Path "$deployDir/*" -DestinationPath "$PSScriptRoot/deploy/$zipName" -Force

$zip = Get-Item "$PSScriptRoot/deploy/$zipName"
Write-Host "==> Done: $($zip.FullName) ($([math]::Round($zip.Length/1MB,2)) MB)"

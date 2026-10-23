#Requires -Version 5.1
<#
.SYNOPSIS
    按架构构建影图 ImagePro，并产出安装包与便捷版。
.DESCRIPTION
    两条工具链路线：

    【MSYS2 路线】x64 / x64-ucrt / arm64
      x64      MINGW64（msvcrt）或 UCRT64
      arm64    CLANGARM64（aarch64，含 Qt5 与 libvips）

      ⚠ ARM64 必须在**原生 ARM64 Windows**（Windows 11 on ARM）上构建：
        MSYS2 的 CLANGARM64 前缀里所有程序——包括编译器本身——都是 ARM64 原生
        程序，不是「可在 x64 上运行的交叉编译器」。在 x64 主机上直接调用会得到
        "not a valid application for this OS platform" 这类无信息量的错误，
        所以本脚本会在预检阶段明确拦下并给出替代方案。
        CI 里由 windows-11-arm runner（原生 ARM64）执行同一套流程。

    【MSVC 路线】x86
      MSYS2 没有 32 位 Qt5，也没有 32 位 libvips，x86 只能走
      「MSVC + Qt 官方 win32_msvc2019 包 + libvips 官方预编译包」。
      需要通过 -QtRoot / -VipsRoot 指定这两份预编译包的根目录。

    每个架构的构建目录相互独立（build-<arch>），互不干扰。
.PARAMETER Arch
    x64 | x64-ucrt | x86 | arm64
.PARAMETER MsysRoot
    MSYS2 根目录，默认 C:/msys64
.PARAMETER BuildType
    Release | Debug | RelWithDebInfo，默认 Release
.PARAMETER HostToolsBin
    cmake / ninja / pkg-config 的查找目录。留空时按
    「架构前缀 → MSYS2 msys 子系统 → mingw64/bin」顺序自动查找。
.PARAMETER QtRoot
    【x86 / MSVC 路线】Qt 根目录，例如 D:/devtools/qt/5.15.2/msvc2019
.PARAMETER VipsRoot
    【x86 / MSVC 路线】libvips 预编译包解压根目录（含 include/ 与 lib/）
.PARAMETER NoTest
    跳过单元测试（交叉架构下无法在本机运行测试）
.EXAMPLE
    ./build-arch.ps1 -Arch x64
.EXAMPLE
    ./build-arch.ps1 -Arch arm64
.EXAMPLE
    ./build-arch.ps1 -Arch x86 -QtRoot D:/devtools/qt/5.15.2/msvc2019 `
                     -VipsRoot D:/devtools/libvips-ia32-full
#>
param(
    [ValidateSet('x64', 'x64-ucrt', 'x86', 'arm64')]
    [string]$Arch = 'x64',
    [string]$MsysRoot = 'C:/msys64',
    [string]$BuildType = 'Release',
    [string]$HostToolsBin = '',
    [string]$QtRoot = '',
    [string]$VipsRoot = '',
    [switch]$NoTest
)

$ErrorActionPreference = 'Stop'

function Get-PeMachine($path) {
    if (-not (Test-Path $path)) { return $null }
    try {
        $fs = [System.IO.File]::OpenRead($path)
        $br = New-Object System.IO.BinaryReader($fs)
        $fs.Position = 0x3C
        $peOff = $br.ReadInt32()
        $fs.Position = $peOff + 4
        $m = $br.ReadUInt16()
        $br.Close(); $fs.Close()
        switch ($m) {
            0x8664 { return 'x64' }
            0xAA64 { return 'arm64' }
            0x014C { return 'x86' }
            default { return ('0x{0:X}' -f $m) }
        }
    } catch { return $null }
}

# ── MSVC 路线（x86）──────────────────────────────────────────────
# MSYS2 既没有 32 位 Qt5，也没有 32 位 libvips，x86 只能走 MSVC。
if ($Arch -eq 'x86') {
    $buildDir = "$PSScriptRoot/build-x86"

    if (-not $QtRoot -or -not $VipsRoot) {
        throw @"
x86 需要两份预编译包，请用 -QtRoot / -VipsRoot 指定：
  -QtRoot  D:/devtools/qt/5.15.2/msvc2019
           （Qt 官方 win32_msvc2019 包，可用 aqtinstall 获取：
            python -m aqt install-qt windows desktop 5.15.2 win32_msvc2019 -O D:/devtools/qt）
  -VipsRoot D:/devtools/libvips-ia32-full
           （libvips 官方 win32-ia32 包：
            https://github.com/lovell/sharp-libvips/releases/download/v8.16.1/libvips-8.16.1-win32-ia32.tar.gz）
"@
    }
    foreach ($need in @("$QtRoot/lib/cmake/Qt5/Qt5Config.cmake",
                        "$QtRoot/bin/windeployqt.exe",
                        "$VipsRoot/include/vips/vips.h",
                        "$VipsRoot/lib/libvips.lib")) {
        if (-not (Test-Path $need)) {
            throw "缺少预编译包文件：$need"
        }
    }

    # 进入 Visual Studio 开发者环境（PowerShell 原生方式，无需 cmd）。
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "找不到 vswhere，请安装 Visual Studio 2022（含 C++ 桌面开发工作负载）。"
    }
    $vsRoot = & $vswhere -latest -products * `
                          -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                          -property installationPath
    if (-not $vsRoot) {
        # 组件 ID 可能因安装方式不同而查不到，退回到「取最新实例」再自行校验工具是否存在。
        Write-Warning "未按组件 ID 找到 VS 实例，改为取最新实例并逐项校验工具。"
        $vsRoot = & $vswhere -latest -products * -property installationPath
    }
    if (-not $vsRoot) { throw "找不到 Visual Studio 实例。" }
    $clExe = Get-ChildItem (Join-Path $vsRoot 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
             ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x86\cl.exe' } |
             Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $clExe) { throw "Visual Studio 实例里没有 x86 编译器（$vsRoot）。请安装「MSVC v143 x86/x64 生成工具」。" }

    $devShell = Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1'
    if (-not (Test-Path $devShell)) { throw "找不到 Launch-VsDevShell.ps1：$devShell" }
    Write-Host "==> 进入 VS 开发者环境：$vsRoot (x86)"
    # Launch-VsDevShell 的 -HostArch 只接受 x86 / amd64（不是 x64）。
    & $devShell -Arch x86 -HostArch amd64 -SkipAutomaticLocation | Out-Null

    # 强制 MSVC 输出英文诊断。
    # 中文 VS 下 cl.exe 的 /showIncludes 前缀是「注意: 包含文件:」，
    # CMake 会把它原样写进 rules.ninja 的 msvc_deps_prefix，
    # 而 Ninja 的词法器不接受非 ASCII 字节 —— 表现为
    #   rules.ninja:23: expected newline, got lexing error
    # 这类看不出根因的构建失败。
    $env:VSLANG = '1033'

    $cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    if (-not $cmake) { $cmake = 'C:/msys64/mingw64/bin/cmake.exe' }
    if (-not (Test-Path $cmake)) { throw '找不到 cmake。' }

    Write-Host "==> 架构: x86（MSVC + Qt 官方包 + libvips 预编译包）"

    # 生成器：Visual Studio 17 2022（-A Win32）。
    # 不用 Ninja 的原因：中文 VS 下 cl.exe 的 /showIncludes 前缀是
    # 「注意: 包含文件: 」，CMake 会把它写进 rules.ninja 的 msvc_deps_prefix，
    # 而 Ninja 的词法器不接受非 ASCII 字节：
    #   rules.ninja:23: expected newline, got lexing error
    # 该前缀由 VS 的安装语言决定，VSLANG=1033 也改不动（资源已烘焙）。
    # VS 生成器不走 Ninja 的依赖解析，因此不受影响。
    $generator = 'Visual Studio 17 2022'
    $generatorArgs = @('-A', 'Win32')

    # 切换生成器时旧构建目录的缓存不兼容，先清掉。
    # 走受信任的可恢复删除通道（mavis-trash），不直接抹掉。
    $cacheFile = Join-Path $buildDir 'CMakeCache.txt'
    if (Test-Path $cacheFile) {
        $oldGen = (Select-String -Path $cacheFile -Pattern '^CMAKE_GENERATOR:INTERNAL=(.+)$' |
                   Select-Object -First 1).Matches.Groups[1].Value
        if ($oldGen -and $oldGen -ne $generator) {
            Write-Host "==> 生成器由 '$oldGen' 切换为 '$generator'，重建构建目录"
            $trash = Join-Path $env:USERPROFILE '.minimax\bin\mavis-trash.cmd'
            if (Test-Path $trash) {
                & $trash -- $buildDir
            } else {
                Remove-Item -LiteralPath $buildDir -Recurse -Force
            }
        }
    }

    Write-Host "==> 配置 CMake..."
    & $cmake -S $PSScriptRoot -B $buildDir -G $generator @generatorArgs `
        -DBUILD_TESTS=ON `
        -DIMAGEPRO_WERROR=ON `
        "-DCMAKE_PREFIX_PATH=$QtRoot" `
        "-DIMAGEPRO_VIPS_ROOT=$VipsRoot" `
        "-DCMAKE_INSTALL_PREFIX=$buildDir/install"
    if ($LASTEXITCODE -ne 0) { throw '配置失败' }

    Write-Host "==> 编译..."
    & $cmake --build $buildDir --config Release -j8
    if ($LASTEXITCODE -ne 0) { throw '编译失败' }

    if (-not $NoTest) {
        Write-Host "==> 单元测试..."
        # VS 生成器没有 test.vcxproj，cmake --build --target test 会报
        # MSB1009「项目文件不存在」，只能直接调 ctest（多配置需带 -C）。
        $ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
        if (-not (Test-Path $ctest)) { $ctest = 'ctest.exe' }
        & $ctest --test-dir $buildDir -C Release --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw '测试失败' }
    }

    Write-Host "==> 打包（ZIP + NSIS + 便捷版）..."
    # CPack 的 NSIS 生成器在自己的 PATH / 注册表里找 makensis，
    # 只设 CPACK_NSIS_MAKE_NSIS 不够（它还需要能定位到 NSIS 的
    # Plugins / Include 目录）。这里把 makensis 所在目录临时加进 PATH，
    # 打包完再还原 —— 避免把 x64 的 DLL 目录长期留在 x86 构建的 PATH 里。
    $makensisDirs = @('C:/msys64/mingw64/bin', 'C:/Program Files (x86)/NSIS',
                      'C:/Program Files/NSIS') |
                    Where-Object { Test-Path "$_/makensis.exe" }
    $savedPath = $env:PATH
    if ($makensisDirs) {
        $env:PATH = ($makensisDirs -join ';') + ';' + $env:PATH
    }
    Push-Location $buildDir
    try {
        & cpack -G ZIP -B package -C Release
        if ($LASTEXITCODE -ne 0) { throw '打包失败' }
        & cpack -G NSIS -B package -C Release
        if ($LASTEXITCODE -ne 0) { throw 'NSIS 打包失败' }
        & $cmake --build . --config Release --target portable
        if ($LASTEXITCODE -ne 0) { throw '便捷版打包失败' }
    } finally {
        Pop-Location
        $env:PATH = $savedPath
    }

    Write-Host ""
    Write-Host "==> 产物（$buildDir/package）："
    Get-ChildItem "$buildDir/package" -File -ErrorAction SilentlyContinue |
        Select-Object Name, @{n = 'MB'; e = { [math]::Round($_.Length / 1MB, 1) } } |
        Format-Table -AutoSize
    return
}

# ── MSYS2 路线（x64 / x64-ucrt / arm64）──────────────────────────
# 架构 → MSYS2 环境、前缀目录、包前缀
# 注意 dir（目录名）与 pkg（包名前缀）不是一回事：
#   MSYS2 的 aarch64 前缀目录叫 clangarm64，包前缀却叫 mingw-w64-clang-aarch64。
# 把包前缀当目录用会得到「找不到编译器」这种误导性的报错。
$archMap = @{
    'x64'      = @{ msystem = 'MINGW64';   dir = 'mingw64';   pkg = 'mingw-w64-x86_64';       pe = 'x64'; variant = '' }
    'x64-ucrt' = @{ msystem = 'UCRT64';    dir = 'ucrt64';    pkg = 'mingw-w64-ucrt-x86_64';   pe = 'x64'; variant = 'ucrt' }
    'arm64'    = @{ msystem = 'CLANGARM64'; dir = 'clangarm64'; pkg = 'mingw-w64-clang-aarch64'; pe = 'arm64'; variant = '' }
}

if (-not $archMap.ContainsKey($Arch)) { throw "不支持的架构：$Arch" }
$cfg = $archMap[$Arch]

$envDir = "$MsysRoot/$($cfg.msystem).exe"
if (-not (Test-Path $envDir)) { throw "找不到 MSYS2 环境：$envDir" }

$prefixPath = "$MsysRoot/$($cfg.dir)"
$buildDir = "$PSScriptRoot/build-$Arch"
$msysBin = "$prefixPath/bin"

# ── 工具链架构预检 ──────────────────────────────────────────────
# MSYS2 的 clangarm64 前缀里所有程序（含编译器、moc、windeployqt）
# 都是 ARM64 原生程序，不是「可在 x64 上跑的交叉编译器」。
# 因此 ARM64 必须在原生 ARM64 Windows（Windows 11 on ARM）上构建，
# x64 主机上无法交叉编译 —— 与 libvips 没有 MSVC 预编译包是同一个约束。

# 宿主操作系统架构（Windows ARM64 可通过模拟运行 x64 程序，反之不行）
$hostOsArch = switch ($env:PROCESSOR_ARCHITECTURE) {
    'AMD64' { 'x64' }
    'ARM64' { 'arm64' }
    'x86'   { 'x86' }
    default { 'unknown' }
}

$compilerExe = if ($cfg.pe -eq 'arm64') { "$msysBin/aarch64-w64-mingw32-clang++.exe" } else { "$msysBin/g++.exe" }
$compilerPe = Get-PeMachine $compilerExe

if ($null -eq $compilerPe) {
    throw "找不到编译器：$compilerExe`n请先安装： pacman -S $($cfg.pkg)-gcc"
}
if ($compilerPe -ne $cfg.pe) {
    throw "工具链架构异常：$compilerExe 是 $compilerPe，应为 $($cfg.pe)。请检查 MSYS2 前缀是否被替换过。"
}
if ($compilerPe -eq 'arm64' -and $hostOsArch -ne 'arm64') {
    throw @"
MSYS2 的 CLANGARM64 环境里全部是 ARM64 原生程序（编译器本身也是），
不能在 x64 主机上交叉编译。ARM64 版本需要在以下两种方式之一产出：
  1. 在原生 ARM64 Windows（Windows 11 on ARM）上运行本脚本；
  2. 交给 CI 的 windows-11-arm runner（.github/workflows/ci.yml 的 build-arch 矩阵）。
"@
}
Write-Host "==> 预检通过：工具链 $compilerPe / 宿主 $hostOsArch"

# ── 查找工具 ────────────────────────────────────────────────────
# cmake / ninja / pkg-config 是与目标架构无关的宿主程序。
# 优先用架构前缀自带的（ARM64 原生机上就在 clangarm64/bin 里），
# 其次退到 MSYS2 的 msys 子系统，最后退到 mingw64/bin（ARM64 上靠模拟运行）。
function Find-HostTool($name) {
    $candidates = @()
    if ($HostToolsBin) { $candidates += $HostToolsBin }
    $candidates += $msysBin
    $candidates += "$MsysRoot/usr/bin"
    $candidates += "$MsysRoot/mingw64/bin"

    foreach ($dir in $candidates) {
        foreach ($n in @($name, "$name.exe", "$name-qt5.exe")) {
            $p = Join-Path $dir $n
            if (Test-Path $p) { return $p }
        }
    }
    return $null
}

$tools = @{}
foreach ($t in @('cmake', 'ninja', 'pkg-config')) {
    $found = Find-HostTool $t
    if (-not $found) {
        $pkgs = "$($cfg.pkg)-$t"
        throw "缺少工具：$t。请先安装： pacman -S $pkgs"
    }
    $tools[$t] = $found
}
Write-Host "==> cmake      = $($tools['cmake'])"
Write-Host "==> ninja      = $($tools['ninja'])"
Write-Host "==> pkg-config = $($tools['pkg-config'])"

# 依赖库（Qt5 / libvips）必须来自目标架构前缀。
foreach ($lib in @('lib/cmake/Qt5/Qt5Config.cmake', 'lib/pkgconfig/vips-cpp.pc')) {
    if (-not (Test-Path "$prefixPath/$lib")) {
        throw "目标前缀缺少依赖：$prefixPath/$lib`n请先安装： pacman -S $($cfg.pkg)-qt5 $($cfg.pkg)-libvips"
    }
}

$env:PATH = "$msysBin;$MsysRoot/usr/bin;$env:PATH"
# pkg-config 必须看到目标前缀的 .pc 文件，否则会解析到宿主（x64）的头文件路径。
$env:PKG_CONFIG_PATH = "$prefixPath/lib/pkgconfig;$prefixPath/share/pkgconfig"

Write-Host "==> 架构: $Arch（$($cfg.msystem) / $($cfg.pkg)）"

# ── 配置 ──────────────────────────────────────────────────────
# 变体标识（如 ucrt）要进入产物名，否则同一架构的多个运行时变体产物同名。
$variantArgs = @()
if ($cfg.variant) { $variantArgs = @("-DIMAGEPRO_PACKAGE_VARIANT=$($cfg.variant)") }

Write-Host "==> 配置 CMake..."
& $tools['cmake'] -S $PSScriptRoot -B $buildDir -G Ninja `
    -DCMAKE_BUILD_TYPE=$BuildType `
    -DBUILD_TESTS=ON `
    -DIMAGEPRO_WERROR=ON `
    "-DCMAKE_PREFIX_PATH=$prefixPath" `
    "-DCMAKE_INSTALL_PREFIX=$buildDir/install" `
    "-DCMAKE_MAKE_PROGRAM=$($tools['ninja'])" `
    "-DPKG_CONFIG_EXECUTABLE=$($tools['pkg-config'])" `
    @variantArgs
if ($LASTEXITCODE -ne 0) { throw '配置失败' }

# ── 编译 ──────────────────────────────────────────────────────
Write-Host "==> 编译..."
& $tools['cmake'] --build $buildDir -j8
if ($LASTEXITCODE -ne 0) { throw '编译失败' }

# ── 测试 ──────────────────────────────────────────────────────
# 交叉架构（如在 x64 机器上构建 arm64）无法在本机运行测试，
# 这类情况由 CI 的 build-arch 矩阵作业在对应架构的机器上执行。
if (-not $NoTest) {
    Write-Host "==> 单元测试..."
    & $tools['cmake'] --build $buildDir --target test
    if ($LASTEXITCODE -ne 0) { throw '测试失败' }
}

# ── 打包 ──────────────────────────────────────────────────────
Write-Host "==> 打包（ZIP + NSIS + 便捷版）..."
Push-Location $buildDir
try {
    & $tools['cmake'] --build . --target package
    if ($LASTEXITCODE -ne 0) { throw '打包失败' }
    & $tools['cmake'] --build . --target portable
    if ($LASTEXITCODE -ne 0) { throw '便捷版打包失败' }
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "==> 产物（$buildDir/package）："
Get-ChildItem "$buildDir/package" -File -ErrorAction SilentlyContinue |
    Select-Object Name, @{n = 'MB'; e = { [math]::Round($_.Length / 1MB, 1) } } |
    Format-Table -AutoSize
Get-ChildItem $buildDir -Filter '*-portable-*.zip' -ErrorAction SilentlyContinue |
    Select-Object Name, @{n = 'MB'; e = { [math]::Round($_.Length / 1MB, 1) } } |
    Format-Table -AutoSize

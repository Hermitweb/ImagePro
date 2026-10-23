# 运行时依赖收集（Windows）。
#
# 目标有两个：
#   1. `cmake --install` / CPack 产出的目录是「可直接运行」的，
#      而不是只含一个依赖系统 PATH 的 exe；
#   2. **构建目录里的 exe 本身也能原地运行**（开发调试、CI 冒烟启动依赖它）。
#
# 做法：
#   1. 用 windeployqt（MSYS2 包 mingw-w64-x86_64-qt5-tools）把 Qt 运行库、
#      平台插件、imageformats 插件收集到 ${CMAKE_BINARY_DIR}/runtime/；
#   2. 额外递归收集 MinGW 运行库与 libvips 的 DLL 到同一目录；
#   3. 把 runtime/ 同步到可执行文件旁边 —— 这一步不能省：
#      runtime/ 是为打包准备的暂存目录，构建目录里的 exe 同样需要能跑起来。
#      少了这一步，exe 会因找不到 Qt5Network.dll 等依赖而根本起不来，
#      表现为进程卡在系统的「缺少 DLL」弹窗上，而不是启动失败；
#   4. install() 规则只安装 runtime/ 目录（保持 plugins 子目录结构）。
#
# 单独收集到 runtime/ 而不是就地放在 exe 旁边，有两个好处：
#   * install(DIRECTORY) 只需指向一个确定的目录，不必扫描整个构建目录
#     —— 扫描整个构建目录会把测试可执行文件、安装前缀甚至产物自身都卷进去；
#   * 「哪些文件属于运行时产物」有明确边界，便于打包脚本复用。
#
# windeployqt 不可用时（非 Windows、或未装 qt5-tools）跳过，
# 不阻塞构建 —— 安装包完整性由 CI 的 release 作业保证。

if(NOT WIN32)
    return()
endif()

set(IMAGEPRO_RUNTIME_DIR "${CMAKE_BINARY_DIR}/runtime")
file(MAKE_DIRECTORY "${IMAGEPRO_RUNTIME_DIR}")

# ── 目标架构判定 ────────────────────────────────────────────────
# CMake 的 CMAKE_SYSTEM_PROCESSOR 在不同工具链下写法不一：
#   MinGW   → x86_64 / aarch64 / i686
#   MSVC    → AMD64 / ARM64 / x86
# 统一归一化成 x64 / arm64 / x86，供 PE 头校验使用。
function(_imagepro_normalize_arch proc out)
    string(TOLOWER "${proc}" _p)
    if(_p MATCHES "^(x86_64|amd64|x64)$")
        set(${out} "x64" PARENT_SCOPE)
    elseif(_p MATCHES "^(aarch64|arm64)$")
        set(${out} "arm64" PARENT_SCOPE)
    elseif(_p MATCHES "^(i[3-6]86|x86)$")
        set(${out} "x86" PARENT_SCOPE)
    else()
        set(${out} "unknown" PARENT_SCOPE)
    endif()
endfunction()

# 读取 PE 文件头里的 Machine 字段，判断该可执行文件的真实架构。
# 用它做「工具架构 == 目标架构」的校验。
function(_imagepro_pe_arch exe out)
    set(${out} "unknown" PARENT_SCOPE)
    if(NOT EXISTS "${exe}")
        return()
    endif()

    file(READ "${exe}" _mz LIMIT 2 HEX)
    string(TOLOWER "${_mz}" _mz)
    if(NOT _mz STREQUAL "4d5a")
        return()
    endif()

    file(READ "${exe}" _off OFFSET 60 LIMIT 4 HEX)
    string(SUBSTRING "${_off}" 0 2 _o1)
    string(SUBSTRING "${_off}" 2 2 _o2)
    string(SUBSTRING "${_off}" 4 2 _o3)
    string(SUBSTRING "${_off}" 6 2 _o4)
    math(EXPR _peoff "0x${_o4}${_o3}${_o2}${_o1}")

    # PE 结构：偏移 peoff 处是 "PE\0\0"（4 字节），紧随其后的 2 字节是 Machine。
    # 读 6 个字节得到 12 个十六进制字符：
    #   [0..4)  = "5045"  PE 签名前两字节
    #   [4..8)  = "0000"  PE 签名后两字节
    #   [8..12) = Machine（2 字节，小端）
    file(READ "${exe}" _mach OFFSET ${_peoff} LIMIT 6 HEX)
    string(SUBSTRING "${_mach}" 8 2 _m1)
    string(SUBSTRING "${_mach}" 10 2 _m2)
    math(EXPR _m "0x${_m2}${_m1}")

    if(_m EQUAL 0x8664)
        set(${out} "x64" PARENT_SCOPE)
    elseif(_m EQUAL 0xaa64)
        set(${out} "arm64" PARENT_SCOPE)
    elseif(_m EQUAL 0x014c)
        set(${out} "x86" PARENT_SCOPE)
    endif()
endfunction()

_imagepro_normalize_arch("${CMAKE_SYSTEM_PROCESSOR}" IMAGEPRO_TARGET_ARCH)

# MSVC 下 CMAKE_SYSTEM_PROCESSOR 报的是**宿主**架构：
# 用 Ninja 生成器交叉编译 x86 时它仍是 AMD64。
# 目标架构要从编译器路径推断：…/bin/Hostx64/<target>/cl.exe
if(MSVC AND CMAKE_CXX_COMPILER)
    file(TO_CMAKE_PATH "${CMAKE_CXX_COMPILER}" _cc_path)
    string(REGEX REPLACE ".*/bin/Host[^/]*/([^/]*)/.*$" "\\1" _cc_target "${_cc_path}")
    _imagepro_normalize_arch("${_cc_target}" _cc_target_norm)
    if(NOT _cc_target_norm STREQUAL "unknown")
        set(IMAGEPRO_TARGET_ARCH "${_cc_target_norm}")
    endif()
    unset(_cc_path)
    unset(_cc_target)
    unset(_cc_target_norm)
endif()

message(STATUS "ImagePro: 目标架构 = ${IMAGEPRO_TARGET_ARCH}"
               "（CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}）")

# ── 工具链 bin 目录 ────────────────────────────────────────────
# windeployqt / objdump 必须与目标架构一致：
# MSYS2 的 clangarm64 前缀里它们是 ARM64 原生程序，
# 若误用 mingw64 的 x64 版本，就会把 x64 的 Qt DLL 拷进 ARM64 包，
# 产物在 ARM64 机器上直接起不来 —— 这种错误在运行前完全看不出来。
set(IMAGEPRO_TOOLCHAIN_BIN "" CACHE PATH
    "提供与目标架构一致的 windeployqt/objdump 的 bin 目录")
if(NOT IMAGEPRO_TOOLCHAIN_BIN AND CMAKE_CXX_COMPILER)
    get_filename_component(_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(IMAGEPRO_TOOLCHAIN_BIN "${_compiler_dir}")
endif()
message(STATUS "ImagePro: 工具链 bin = ${IMAGEPRO_TOOLCHAIN_BIN}")

# Qt 自身的 bin 目录：官方预编译包（MSVC 路线）里 windeployqt 就在这儿。
#
# ⚠ 不能靠拼接 Qt5Core_DIR 的字符串来推前缀：
#   CI 里 CMAKE_PREFIX_PATH 是 MSYS 风格路径（/mingw-w64-ucrt-x86_64），
#   拼出来的 "/mingw-w64-ucrt-x86_64/bin" 会被 Windows 解析成
#   C:\mingw-w64-ucrt-x86_64\bin（不存在），
#   于是 Qt5Test.dll 没被收集 → 测试进程直接崩（0xC0000135，且无任何输出）。
# 改从 Qt 的导入目标取绝对路径。
set(_imagepro_qt_bin "")
if(TARGET Qt5::Core)
    get_target_property(_qt5core_location Qt5::Core IMPORTED_LOCATION)
    if(_qt5core_location)
        get_filename_component(_qt5core_libdir "${_qt5core_location}" DIRECTORY)
        get_filename_component(_qt5_prefix "${_qt5core_libdir}/.." ABSOLUTE)
        set(_imagepro_qt_bin "${_qt5_prefix}/bin")
    endif()
endif()
if(NOT _imagepro_qt_bin AND Qt5Core_DIR)
    get_filename_component(_qt_core_dir "${Qt5Core_DIR}" ABSOLUTE)
    get_filename_component(_qt_lib_dir "${_qt_core_dir}" DIRECTORY)
    get_filename_component(_qt_prefix "${_qt_lib_dir}/../.." ABSOLUTE)
    set(_imagepro_qt_bin "${_qt_prefix}/bin")
endif()
message(STATUS "ImagePro: Qt bin = ${_imagepro_qt_bin}")

# windeployqt 的查找必须**按目录顺序**逐个探测，不能交给 find_program：
# find_program 是「名字优先」的循环 —— 先在所有路径里找 windeployqt-qt5.exe，
# 再找 windeployqt.exe。于是在装了 MSYS2（提供 windeployqt-qt5.exe）、
# 又装了 Qt 官方包（提供 windeployqt.exe）的机器上，它会先命中 PATH 里的
# MSYS2 版本，把错误架构的 Qt DLL 部署进来。
set(_wdq_candidates
    "${IMAGEPRO_TOOLCHAIN_BIN}/windeployqt-qt5.exe"
    "${IMAGEPRO_TOOLCHAIN_BIN}/windeployqt.exe"
    "${_imagepro_qt_bin}/windeployqt.exe"
    "${_imagepro_qt_bin}/windeployqt-qt5.exe"
    "C:/msys64/mingw64/bin/windeployqt-qt5.exe"
    "C:/msys64/mingw64/bin/windeployqt.exe"
    "C:/msys64/mingw64/share/qt5/bin/windeployqt.exe"
    "C:/msys64/clangarm64/bin/windeployqt-qt5.exe"
    "C:/Qt/5.15.2/mingw_64/bin/windeployqt.exe"
)

set(IMAGEPRO_WINDEPLOYQT "")
foreach(_cand IN LISTS _wdq_candidates)
    if(EXISTS "${_cand}")
        set(IMAGEPRO_WINDEPLOYQT "${_cand}")
        break()
    endif()
endforeach()

if(NOT IMAGEPRO_WINDEPLOYQT)
    message(STATUS "ImagePro: 未找到 windeployqt，安装包将不含 Qt 运行库"
                   "（本地开发不受影响；CI release 作业会安装 qt5-tools）。")
    return()
endif()

# 架构校验：工具与目标不一致时，宁可不用，也不要产出错误的包。
# 校验结果放在 _wdq_path，不去改 IMAGEPRO_WINDEPLOYQT 本身 ——
# 后者会被 tests/CMakeLists.txt 复用（测试程序需要同一个 windeployqt）。
_imagepro_pe_arch("${IMAGEPRO_WINDEPLOYQT}" _wdq_arch)
set(_wdq_path "${IMAGEPRO_WINDEPLOYQT}")
if(NOT IMAGEPRO_TARGET_ARCH STREQUAL "unknown"
   AND NOT _wdq_arch STREQUAL "${IMAGEPRO_TARGET_ARCH}")
    message(WARNING
        "ImagePro: windeployqt 架构(${_wdq_arch})与目标架构(${IMAGEPRO_TARGET_ARCH})不一致，"
        "已跳过 Qt 运行库收集（否则会拷入错误架构的 Qt DLL）。")
    set(_wdq_path "")
endif()

if(NOT _wdq_path)
    return()
endif()

message(STATUS "ImagePro: windeployqt = ${_wdq_path}")

# Qt5Test.dll 只有测试程序链接，主程序不链接，windeployqt 不会收集它。
# 缺了它测试程序会以 0xC0000135（找不到 DLL）失败。
set(_qt5test_cmd "")
if(EXISTS "${_imagepro_qt_bin}/Qt5Test.dll")
    list(APPEND _qt5test_cmd COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_imagepro_qt_bin}/Qt5Test.dll" "${IMAGEPRO_RUNTIME_DIR}")
endif()

add_custom_command(TARGET ImagePro POST_BUILD
    COMMAND "${_wdq_path}"
            "$<TARGET_FILE:ImagePro>"
            --release --no-translations --no-compiler-runtime --no-opengl-sw
            --dir "${IMAGEPRO_RUNTIME_DIR}"
    ${_qt5test_cmd}
    COMMENT "收集 Qt 运行时依赖到 runtime/"
    VERBATIM
)

# libvips 及其 glib 依赖链：用 objdump 递归解析 DLL 依赖后整体拷贝。
find_program(IMAGEPRO_OBJDUMP NAMES objdump objdump.exe
    HINTS "${IMAGEPRO_TOOLCHAIN_BIN}" "C:/msys64/mingw64/bin")

if(IMAGEPRO_OBJDUMP)
    # 可解析性校验：binutils 的 PE 后端只认 i386 / x86_64，
    # x64 的 objdump 读 aarch64 的导入表会直接报 "file format not recognized"，
    # 于是依赖一个也收集不到（静默失败，装完才发现缺 DLL）。
    # 反过来 x64 的 objdump 读 x86 的 PE 没问题。
    _imagepro_pe_arch("${IMAGEPRO_OBJDUMP}" _objdump_arch)
    set(_objdump_can_read "")
    if(_objdump_arch STREQUAL "x64")
        set(_objdump_can_read "x64;x86")
    elseif(_objdump_arch STREQUAL "x86")
        set(_objdump_can_read "x86")
    elseif(_objdump_arch STREQUAL "arm64")
        set(_objdump_can_read "arm64")
    endif()
    if(NOT IMAGEPRO_TARGET_ARCH STREQUAL "unknown"
       AND NOT "${_objdump_can_read}" MATCHES "(^|;)${IMAGEPRO_TARGET_ARCH}(;|$)")
        message(WARNING
            "ImagePro: objdump 架构(${_objdump_arch})无法解析目标架构(${IMAGEPRO_TARGET_ARCH})的 PE，"
            "已跳过 libvips 依赖递归收集。")
        set(_objdump_path "")
    else()
        set(_objdump_path "${IMAGEPRO_OBJDUMP}")
    endif()
else()
    set(_objdump_path "")
endif()

# ── 运行时 DLL 来源目录 ─────────────────────────────────────────
# MinGW 路线：MSYS2 前缀的 bin 目录（里面是 libgcc / libstdc++ / libvips / glib…）。
# MSVC  路线：libvips 官方预编译包的 lib 目录（libvips-42.dll 等）。
set(_runtime_source_dirs "")
set(_copy_mingw_runtime ON)

if(MSVC)
    set(_copy_mingw_runtime OFF)
    if(VIPS_RUNTIME_DIRS)
        list(APPEND _runtime_source_dirs ${VIPS_RUNTIME_DIRS})
    endif()
else()
    if(_objdump_path)
        get_filename_component(_toolchain_bin_dir "${_objdump_path}" DIRECTORY)
        list(APPEND _runtime_source_dirs "${_toolchain_bin_dir}")
    endif()
endif()

if(_objdump_path AND _runtime_source_dirs)
    message(STATUS "ImagePro: objdump = ${_objdump_path}，"
                   "运行时依赖来源 = ${_runtime_source_dirs}")

    list(JOIN _runtime_source_dirs ";" _runtime_source_dirs_joined)

    set(_collect_runtime_deps
        "${CMAKE_COMMAND}"
        -DOBJDUMP_EXE=${_objdump_path}
        "-DRUNTIME_SOURCE_DIRS=${_runtime_source_dirs_joined}"
        -DCOPY_MINGW_RUNTIME=${_copy_mingw_runtime}
        -DTARGET_DIR=${IMAGEPRO_RUNTIME_DIR}
        -DSOURCE_EXE=$<TARGET_FILE:ImagePro>
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CollectRuntimeDeps.cmake")

    add_custom_command(TARGET ImagePro POST_BUILD
        COMMAND ${_collect_runtime_deps}
        COMMENT "递归收集 libvips / 运行库依赖"
        VERBATIM
    )
else()
    message(STATUS "ImagePro: 跳过 libvips 依赖递归收集。")
endif()

# ── MSVC 运行库（vcruntime140 等）────────────────────────────────
# MSVC 路线不能依赖系统预装 VC++ 运行库：干净的 Windows 机器上
# 只有 UCRT，没有 vcruntime140.dll，程序会直接起不来。
# 从 Visual Studio 的 redist 目录把对应的 CRT DLL 拷进 runtime/。
if(MSVC)
    set(_msvc_redist "")
    foreach(_vcroot
            "$ENV{VCToolsRedistDir}"
            "E:/Microsoft Visual Studio/2022/Community/VC/Redist/MSVC"
            "E:/Microsoft Visual Studio/2022/BuildTools/VC/Redist/MSVC"
            "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Redist/MSVC"
            "C:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Redist/MSVC")
        if(NOT _vcroot)
            continue()
        endif()
        if(IS_DIRECTORY "${_vcroot}")
            set(_msvc_redist "${_vcroot}")
            break()
        endif()
    endforeach()

    if(_msvc_redist)
        # 目录结构：VC/Redist/MSVC/<版本>/<架构>/Microsoft.VC143.CRT/*.dll
        # （<版本> 下还有 v143/、onecore/ 等目录，只取与目标架构对应的那一支。）
        set(_crt_arch_dirs
            "${_msvc_redist}/${IMAGEPRO_TARGET_ARCH}"
            "${_msvc_redist}/v143/Microsoft.VC143.CRT/${IMAGEPRO_TARGET_ARCH}"
        )

        set(_crt_copied 0)
        foreach(_crt_dir IN LISTS _crt_arch_dirs)
            if(NOT IS_DIRECTORY "${_crt_dir}")
                continue()
            endif()
            file(GLOB _crt_dlls
                "${_crt_dir}/*.dll"
                "${_crt_dir}/Microsoft.VC143.CRT/*.dll")
            foreach(_dll IN LISTS _crt_dlls)
                file(COPY "${_dll}" DESTINATION "${IMAGEPRO_RUNTIME_DIR}")
                math(EXPR _crt_copied "${_crt_copied} + 1")
            endforeach()
        endforeach()
        message(STATUS "ImagePro: MSVC 运行库已拷贝 ${_crt_copied} 个 DLL（${_msvc_redist}）")
    else()
        message(WARNING "ImagePro: 未找到 MSVC redist 目录，安装包可能缺少 vcruntime140.dll。")
    endif()
endif()

# 把 runtime/ 同步到可执行文件旁边，让「直接从构建目录运行」也能工作。
add_custom_command(TARGET ImagePro POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${IMAGEPRO_RUNTIME_DIR}" "$<TARGET_FILE_DIR:ImagePro>"
    COMMENT "同步运行时依赖到可执行文件目录"
    VERBATIM
)

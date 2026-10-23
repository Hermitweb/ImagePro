# 递归收集 DLL 依赖（CMake 脚本模式，由 cmake/RuntimeDeps.cmake 调用）。
#
# 用法（脚本模式）：
#   cmake -DOBJDUMP_EXE=<objdump> "-DRUNTIME_SOURCE_DIRS=<dir1>;<dir2>"
#         -DCOPY_MINGW_RUNTIME=ON -DTARGET_DIR=<输出目录>
#         -DSOURCE_EXE=<主程序> -P CollectRuntimeDeps.cmake
#
# 原理：
#   objdump -p <dll> 会列出 "DLL Name: xxx.dll" 形式的导入表条目。
#   对主程序与目标目录中的每个 dll 求依赖，剔除 Windows 系统 DLL，
#   其余从 RUNTIME_SOURCE_DIRS 拷贝到目标目录，并对新拷入的 DLL 继续递归。
#
# RUNTIME_SOURCE_DIRS 支持多个目录（分号分隔）：
#   MinGW 路线 → MSYS2 前缀 bin（含 libgcc / libstdc++ / libvips / glib…）
#   MSVC  路线 → libvips 官方预编译包的 lib（含 libvips-42.dll）
#
# COPY_MINGW_RUNTIME=OFF 时不拷贝 libgcc/libstdc++/libwinpthread
# （MSVC 路线用不到，拷进去反而是多余的 x64 DLL）。
#
# 该脚本是幂等的：已存在的文件不重复拷贝。

if(NOT OBJDUMP_EXE OR NOT TARGET_DIR)
    message(FATAL_ERROR "CollectRuntimeDeps: 缺少 OBJDUMP_EXE / TARGET_DIR 参数")
endif()

if(NOT RUNTIME_SOURCE_DIRS)
    if(MINGW_BIN_DIR)
        set(RUNTIME_SOURCE_DIRS "${MINGW_BIN_DIR}")
    else()
        message(FATAL_ERROR "CollectRuntimeDeps: 缺少 RUNTIME_SOURCE_DIRS 参数")
    endif()
endif()

# Windows 系统 DLL：随操作系统提供，无需分发。
set(_system_dlls
    KERNEL32.dll KERNELBASE.dll ntdll.dll msvcrt.dll USER32.dll GDI32.dll
    SHELL32.dll ole32.dll OLEAUT32.dll COMDLG32.dll ADVAPI32.dll WS2_32.dll
    SHLWAPI.dll COMCTL32.dll WINMM.dll IMM32.dll OPENGL32.dll d3d11.dll
    dxgi.dll D3Dcompiler_47.dll libEGL.dll libGLESv2.dll
    api-ms-win-crt-runtime-l1-1-0.dll api-ms-win-crt-heap-l1-1-0.dll
    api-ms-win-crt-string-l1-1-0.dll api-ms-win-crt-stdio-l1-1-0.dll
    api-ms-win-crt-math-l1-1-0.dll api-ms-win-crt-locale-l1-1-0.dll
    api-ms-win-crt-convert-l1-1-0.dll api-ms-win-crt-time-l1-1-0.dll
    api-ms-win-crt-filesystem-l1-1-0.dll api-ms-win-crt-environment-l1-1-0.dll
    api-ms-win-crt-process-l1-1-0.dll
)

# MinGW 运行时：不是主程序的导入依赖（链接期就定下了），必须显式拷贝。
if(COPY_MINGW_RUNTIME)
    set(_mingw_runtime_dlls
        libgcc_s_seh-1.dll
        libstdc++-6.dll
        libwinpthread-1.dll
    )

    foreach(_dll IN LISTS _mingw_runtime_dlls)
        foreach(_src_dir IN LISTS RUNTIME_SOURCE_DIRS)
            if(NOT EXISTS "${_src_dir}/${_dll}")
                continue()
            endif()
            if(EXISTS "${TARGET_DIR}/${_dll}")
                continue()
            endif()
            file(COPY "${_src_dir}/${_dll}" DESTINATION "${TARGET_DIR}")
        endforeach()
    endforeach()
endif()

# 递归收集：把待扫描文件的依赖逐个处理。
function(_collect_deps file)
    if(file STREQUAL "")
        return()
    endif()
    if(NOT EXISTS "${file}")
        return()
    endif()

    execute_process(
        COMMAND "${OBJDUMP_EXE}" -p "${file}"
        OUTPUT_VARIABLE _dump
        ERROR_QUIET
        RESULT_VARIABLE _rc
    )
    if(NOT _rc EQUAL 0)
        return()
    endif()

    string(REGEX MATCHALL "DLL Name:[ \t]*([^\r\n]+)" _matches "${_dump}")
    foreach(_m IN LISTS _matches)
        string(REGEX REPLACE "DLL Name:[ \t]*" "" _dll "${_m}")
        string(STRIP "${_dll}" _dll)

        if(_dll STREQUAL "")
            continue()
        endif()
        if("${_dll}" IN_LIST _system_dlls)
            continue()
        endif()
        # 目标目录已有的依赖无需再处理（可能由 windeployqt 拷入）。
        if(EXISTS "${TARGET_DIR}/${_dll}")
            continue()
        endif()

        set(_src "")
        foreach(_src_dir IN LISTS RUNTIME_SOURCE_DIRS)
            if(EXISTS "${_src_dir}/${_dll}")
                set(_src "${_src_dir}/${_dll}")
                break()
            endif()
        endforeach()
        if(NOT _src)
            continue()
        endif()

        file(COPY "${_src}" DESTINATION "${TARGET_DIR}")
        set(_collected "${_collected}" PARENT_SCOPE)

        # 新拷入的 DLL 可能还有依赖，继续递归。
        _collect_deps("${TARGET_DIR}/${_dll}")
    endforeach()
endfunction()

set(_collected "")
file(GLOB _targets "${TARGET_DIR}/*.dll")
foreach(_t IN LISTS _targets)
    _collect_deps("${_t}")
endforeach()

# 主程序自身的依赖（含 libvips）。
if(SOURCE_EXE)
    _collect_deps("${SOURCE_EXE}")
endif()

list(REMOVE_DUPLICATES _collected)
list(LENGTH _collected _n)
message(STATUS "CollectRuntimeDeps: 额外收集 ${_n} 个依赖 DLL")

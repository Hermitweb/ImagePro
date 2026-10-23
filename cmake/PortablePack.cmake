# 便捷版（绿色版）打包收尾（CMake 脚本模式）。
#
# 由 cmake/Packaging.cmake 的 `portable` 目标调用。
# 前置步骤（拷贝 exe / 运行时依赖 / 翻译）已由该目标完成，
# 这里负责：写便携模式标记 → 打包成 ZIP。
#
# 用法：
#   cmake -DOUTPUT=<zip> -DSTAGE=<目录> -P PortablePack.cmake

if(NOT OUTPUT OR NOT STAGE)
    message(FATAL_ERROR "PortablePack: 缺少 OUTPUT 或 STAGE 参数")
endif()

# 便携模式标记：程序启动时看到它，就配置与数据放进程序目录。
file(WRITE "${STAGE}/portable.mode" "ImagePro portable mode\n")

# 便捷版说明
file(WRITE "${STAGE}/使用说明.txt"
"影图 ImagePro —— 便捷版（绿色版）\n"
"================================\n"
"\n"
"无需安装：解压后直接运行 ImagePro.exe 即可。\n"
"\n"
"· 配置、日志、预设等数据都保存在本目录的 data\\ 子目录中，\n"
"  不写入系统配置区，也不写注册表。\n"
"· 整个目录可以随意拷贝到 U 盘或移动硬盘上运行。\n"
"· 卸载 = 直接删除本目录。\n"
"· 便携模式与「安装版」可以同时存在，互不影响。\n")

# 打包
get_filename_component(_output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_dir}")

# 打包。
#
# Windows 上用 PowerShell 的 Compress-Archive 而不是 cmake -E tar：
# libarchive 在中文 Windows（ANSI 代码页 GBK）上会把路径名转码失败，
# 报 "Can't translate pathname to current locale"。Compress-Archive 是
# Windows 10/11 自带的，且没有这个坑。
# 非 Windows 平台回退到 cmake -E tar。
if(WIN32)
    find_program(POWERSHELL_EXE NAMES pwsh powershell)
    if(NOT POWERSHELL_EXE)
        message(FATAL_ERROR "PortablePack: 找不到 PowerShell，无法打包")
    endif()
    execute_process(
        COMMAND "${POWERSHELL_EXE}" -NoProfile -NonInteractive -Command
                "Compress-Archive -Path '${STAGE}\\*' -DestinationPath '${OUTPUT}' -Force"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
    )
else()
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar cf "${OUTPUT}" --format=zip -- .
        WORKING_DIRECTORY "${STAGE}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
    )
endif()
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "PortablePack: 打包失败：${_error}")
endif()

message(STATUS "PortablePack: 已生成 ${OUTPUT}")

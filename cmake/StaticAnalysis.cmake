# 静态分析集成（clang-tidy）。
#
# 提供一个构建目标：
#   tidy   对全部源文件运行 clang-tidy，发现任何告警即以非零码退出
#
# 用 `--warnings-as-errors=*` 把「有告警」变成「构建失败」，
# 从而让静态分析真正成为门禁而不是一份躺在日志里的报告。
#
# 工具链一致性：clang-tidy 必须与编译器同属一套（都用 MinGW），
# 否则解析 MSYS2 的 /mingw64/include 路径会失败。用同一个 MSYS2 环境里的
# clang-tidy 即可满足（CI 安装 mingw-w64-x86_64-clang-tools-extra）。

# 工具链必须与编译器一致（说明见文件头的注释）：
# 用 Windows 版 LLVM 的 clang-tidy 去解析 MSYS2 的 Qt 头文件会因为它不识别
# MSYS2 风格的 include 路径，报几千条「文件找不到」的假告警。
# 因此把「构建所用工具链自带的目录」放在最前面。
find_program(IMAGEPRO_CLANG_TIDY
    NAMES clang-tidy clang-tidy-23 clang-tidy-22 clang-tidy-21 clang-tidy-20
          clang-tidy-19 clang-tidy-18 clang-tidy-17 clang-tidy-16
    HINTS
        "${IMAGEPRO_TOOLCHAIN_BIN}"
        "C:/msys64/ucrt64/bin"
        "C:/msys64/mingw64/bin"
        "C:/msys64/clangarm64/bin"
        "C:/Program Files/LLVM/bin"
        "E:/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin"
    DOC "clang-tidy 可执行文件路径"
)

if(NOT IMAGEPRO_CLANG_TIDY)
    message(WARNING "ImagePro: 未找到 clang-tidy，tidy 目标不可用。")
    return()
endif()

message(STATUS "ImagePro: clang-tidy = ${IMAGEPRO_CLANG_TIDY}")

file(GLOB_RECURSE IMAGEPRO_TIDY_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(SORT IMAGEPRO_TIDY_FILES)
list(LENGTH IMAGEPRO_TIDY_FILES _tidy_file_count)
message(STATUS "ImagePro: 静态分析覆盖 ${_tidy_file_count} 个源文件")

# --warnings-as-errors=* ：任何告警都让命令返回非零 → 门禁生效。
# --quiet ：只输出告警本身，不输出每个文件的 "N warnings generated" 噪音。
add_custom_target(tidy
    COMMAND "${IMAGEPRO_CLANG_TIDY}"
            -p "${CMAKE_BINARY_DIR}"
            --quiet
            --warnings-as-errors=*
            ${IMAGEPRO_TIDY_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "clang-tidy: 静态分析门禁（任何告警即失败）"
    VERBATIM
)

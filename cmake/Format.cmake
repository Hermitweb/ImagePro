# 代码格式工具集成（clang-format）。
#
# 提供两个 CMake 目标：
#   format        原地格式化所有受管源文件
#   format-check  只检查不修改，CI 用作质量门禁（有差异则失败退出）
#
# clang-format 不在 PATH 时自动跳过目标定义（保持可配置、可构建），
# 但 CI 作业会显式检查工具存在性，避免门禁被静默跳过。
#
# 文件列表由 CMake 显式枚举，而不是把目录直接传给 clang-format：
# 各版本 clang-format 对目录递归的支持不一致（19 已移除 --recursive），
# 显式列表跨版本稳定，也不会误伤 src/ 之外的第三方文件。

find_program(IMAGEPRO_CLANG_FORMAT
    NAMES clang-format clang-format-19 clang-format-18 clang-format-17 clang-format-16
    HINTS
        "C:/Program Files/LLVM/bin"
        "E:/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin"
        "C:/msys64/mingw64/bin"
    DOC "clang-format 可执行文件路径"
)

if(IMAGEPRO_CLANG_FORMAT)
    message(STATUS "ImagePro: clang-format = ${IMAGEPRO_CLANG_FORMAT}")

    file(GLOB_RECURSE IMAGEPRO_FORMAT_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
    )
    list(SORT IMAGEPRO_FORMAT_FILES)
    list(LENGTH IMAGEPRO_FORMAT_FILES _fmt_file_count)
    message(STATUS "ImagePro: 格式门禁覆盖 ${_fmt_file_count} 个源文件")

    # 注意：clang-format 的 --Werror 是布尔开关（"发现格式差异即报错"），
    # 不是可带取值的选项。
    set(_format_cmd
        "${IMAGEPRO_CLANG_FORMAT}"
        "--style=file"
    )

    add_custom_target(format
        COMMAND ${_format_cmd} -i
                ${IMAGEPRO_FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "clang-format: 格式化 src/ 与 tests/"
        VERBATIM
    )

    # format-check 用 --dry-run --Werror 让「有格式差异」直接变成非零退出码，
    # CI 即可据此判定门禁失败。
    add_custom_target(format-check
        COMMAND ${_format_cmd} --dry-run --Werror
                ${IMAGEPRO_FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "clang-format: 检查格式（CI 门禁）"
        VERBATIM
    )
else()
    message(WARNING "ImagePro: 未找到 clang-format，format / format-check 目标不可用。")
endif()

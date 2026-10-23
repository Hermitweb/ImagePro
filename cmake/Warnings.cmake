# 警告集与「警告即错误」开关。
#
# 用法：
#   imagepro_apply_warnings(<target> [PRIVATE|PUBLIC|INTERFACE])
#
# 设计原则：
#   * 所有目标统一使用同一套警告集，避免「库严格、目标宽松」导致的头文件不一致；
#   * -Werror 由 IMAGEPRO_WERROR 选项控制，默认 OFF（本地开发不被历史问题阻塞），
#     CI 与发布构建必须置 ON，作为质量门禁；
#   * MSVC 与 GCC/Clang 分别处理，MSVC 需要 /utf-8 防止中文源码字符串被误判编码。
#
# 选入标准：能暴露真实缺陷、且对本仓库不产生噪声。
# 明确排除：-Wold-style-cast / -Wuseless-cast / -Wdouble-promotion /
#           -Wswitch-default / -Wconversion
#   这些在 Qt 惯用写法（QString 隐式转换、QColor 整型重载、枚举 switch）下
#   噪声远大于收益，反而会让门禁失去意义。

option(IMAGEPRO_WERROR "Treat compiler warnings as errors (enable in CI)" OFF)

set(IMAGEPRO_WARNING_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wcast-align
    -Woverloaded-virtual
    -Wformat=2
    -Wundef
    -Wimplicit-fallthrough
    -Wmissing-declarations
)

set(IMAGEPRO_MSVC_WARNING_FLAGS
    /W4
    /permissive-
    /utf-8
    /Zc:__cplusplus
    /Zc:preprocessor
)

function(imagepro_apply_warnings target scope)
    if(MSVC)
        target_compile_options(${target} ${scope} ${IMAGEPRO_MSVC_WARNING_FLAGS})
    else()
        target_compile_options(${target} ${scope} ${IMAGEPRO_WARNING_FLAGS})
    endif()

    if(IMAGEPRO_WERROR)
        if(MSVC)
            target_compile_options(${target} ${scope} /WX)
        else()
            target_compile_options(${target} ${scope} -Werror)
        endif()
        message(STATUS "ImagePro: 警告即错误已启用 (-Werror / /WX)")
    endif()
endfunction()

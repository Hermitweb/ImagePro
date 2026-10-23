# 查找 libvips。
#
# 支持两条路线，输出的变量名与 pkg_check_modules 一致，
# 上层 CMakeLists 不必关心走的是哪条：
#
#   A) pkg-config —— MinGW / MSYS2 默认路线。
#      pkg_check_modules(vips-cpp) 直接给出头文件、库与必要的编译宏。
#
#   B) 预编译根目录 —— MSVC 路线，用 -DIMAGEPRO_VIPS_ROOT=<dir> 指定。
#      MSYS2 没有 32 位（i686）的 Qt5 与 libvips，x86 版本只能走
#      「MSVC + Qt 官方包 + libvips 官方预编译包」；
#      该预编译包（libvips-<ver>-win32-ia32.tar.gz）自带：
#        include/vips/**            libvips 头文件
#        include/glib-2.0/**        glib 头文件（vips.h 会 include <glib.h>）
#        lib/glib-2.0/include/      glibconfig.h（glib 头文件依赖它）
#        lib/libvips.lib            MSVC 导入库
#        lib/libvips-42.dll         运行时 DLL
#
# 输出：
#   VIPS_FOUND
#   VIPS_INCLUDE_DIRS
#   VIPS_LIBRARIES
#   VIPS_CFLAGS_OTHER
#   VIPS_RUNTIME_DIRS   —— 运行时 DLL 所在目录（B 路线才有意义）

include(FindPackageHandleStandardArgs)

set(_imagepro_vips_root "${IMAGEPRO_VIPS_ROOT}")

# ── 路线 A：pkg-config ────────────────────────────────────────
if(NOT VIPS_FOUND)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PC_VIPS QUIET vips-cpp)
    endif()

    if(PC_VIPS_FOUND)
        set(VIPS_VERSION "${PC_VIPS_VERSION}")
        set(VIPS_INCLUDE_DIRS ${PC_VIPS_INCLUDE_DIRS})
        set(VIPS_LIBRARIES ${PC_VIPS_LIBRARIES})
        set(VIPS_CFLAGS_OTHER ${PC_VIPS_CFLAGS_OTHER})
        set(VIPS_LIBRARY_DIRS ${PC_VIPS_LIBRARY_DIRS})
        set(_imagepro_vips_route "pkg-config")
    endif()
endif()

# ── 路线 B：预编译根目录（MSVC）────────────────────────────────
if(NOT VIPS_FOUND AND _imagepro_vips_root)
    find_path(VIPS_VIPS_INCLUDE_DIR
        NAMES vips/vips.h
        HINTS "${_imagepro_vips_root}/include"
        NO_DEFAULT_PATH
    )
    # vipscpp.h 是 libvips 的 C++ 绑定。本项目只用 C API（vips/vips.h），
    # 官方 Windows 预编译包不带 C++ 绑定头文件，因此这里只作可选检查。
    find_path(VIPS_VIPSCPP_INCLUDE_DIR
        NAMES vips/vipscpp.h
        HINTS "${_imagepro_vips_root}/include"
        NO_DEFAULT_PATH
    )
    find_path(VIPS_GLIB_INCLUDE_DIR
        NAMES glib.h
        HINTS "${_imagepro_vips_root}/include/glib-2.0"
        NO_DEFAULT_PATH
    )
    find_path(VIPS_GLIBCONFIG_DIR
        NAMES glibconfig.h
        HINTS
            "${_imagepro_vips_root}/lib/glib-2.0/include"
            "${_imagepro_vips_root}/include/glib-2.0"
        NO_DEFAULT_PATH
    )
    find_path(VIPS_GOBJECT_INCLUDE_DIR
        NAMES glib-object.h
        HINTS "${_imagepro_vips_root}/include/glib-2.0"
        NO_DEFAULT_PATH
    )

    # 导入库：MSVC 为 libvips.lib，MinGW 为 libvips.dll.a / libvips.so
    find_library(VIPS_LIBRARY
        NAMES vips libvips
        HINTS "${_imagepro_vips_root}/lib"
        NO_DEFAULT_PATH
    )

    # 运行时 DLL 目录（Windows 预编译包把 DLL 放在 lib/ 或 bin/ 下）
    set(VIPS_RUNTIME_DIRS "")
    foreach(_sub lib bin)
        if(IS_DIRECTORY "${_imagepro_vips_root}/${_sub}")
            list(APPEND VIPS_RUNTIME_DIRS "${_imagepro_vips_root}/${_sub}")
        endif()
    endforeach()

    if(VIPS_VIPS_INCLUDE_DIR AND VIPS_LIBRARY)
        set(VIPS_INCLUDE_DIRS
            "${VIPS_VIPS_INCLUDE_DIR}"
        )
        if(VIPS_VIPSCPP_INCLUDE_DIR)
            list(APPEND VIPS_INCLUDE_DIRS "${VIPS_VIPSCPP_INCLUDE_DIR}")
        endif()
        if(VIPS_GLIB_INCLUDE_DIR)
            list(APPEND VIPS_INCLUDE_DIRS "${VIPS_GLIB_INCLUDE_DIR}")
        endif()
        if(VIPS_GOBJECT_INCLUDE_DIR)
            list(APPEND VIPS_INCLUDE_DIRS "${VIPS_GOBJECT_INCLUDE_DIR}")
        endif()
        if(VIPS_GLIBCONFIG_DIR)
            list(APPEND VIPS_INCLUDE_DIRS "${VIPS_GLIBCONFIG_DIR}")
        endif()

        set(VIPS_LIBRARIES "${VIPS_LIBRARY}")

        # glib 头文件里有大量「导出/隐藏」宏依赖调用方是否静态链接。
        # 官方 Windows 预编译包把 glib 静态并进了 libvips.dll，
        # 动态链接的一方必须告诉 glib「不要 __declspec(dllimport)」。
        # ⚠ 必须走编译**定义**（target_compile_definitions）：
        #   写成编译选项会被当成**源文件名**，MSVC 报
        #   "error C1083: Cannot open source file: 'G_STATIC_CONST=1'"。
        list(APPEND VIPS_COMPILE_DEFINITIONS "G_STATIC_CONST=1")

        set(_imagepro_vips_route "预编译包")
    endif()
endif()

find_package_handle_standard_args(Libvips
    REQUIRED_VARS VIPS_LIBRARIES VIPS_INCLUDE_DIRS
    VERSION_VAR VIPS_VERSION
)

if(LIBVIPS_FOUND AND NOT _imagepro_vips_route)
    set(_imagepro_vips_route "pkg-config")
endif()
if(LIBVIPS_FOUND)
    message(STATUS "ImagePro: libvips 找到（${_imagepro_vips_route}），"
                   "版本 ${VIPS_VERSION}，头文件 ${VIPS_INCLUDE_DIRS}")
endif()

mark_as_advanced(VIPS_VIPS_INCLUDE_DIR VIPS_VIPSCPP_INCLUDE_DIR
                 VIPS_GLIB_INCLUDE_DIR VIPS_GLIBCONFIG_DIR
                 VIPS_GOBJECT_INCLUDE_DIR VIPS_LIBRARY)

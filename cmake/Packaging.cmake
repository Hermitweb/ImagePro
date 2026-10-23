# 发布打包（CPack）。
#
# 产出两种形态：
#   * 安装版（NSIS）：注册到「控制面板 → 程序和功能」，带开始菜单/桌面快捷方式，
#     可选注册后台服务，卸载时清理干净；
#   * 压缩包（ZIP）：解压即用。
#
# 另有一个「便捷版」目标（portable），见文件末尾 —— 它把配置放在程序目录内，
# 解压即用、不写注册表、不装服务。
#
# 正式发布走 Velopack（见 .github/workflows/ci.yml 的 release 作业）。

set(CPACK_PACKAGE_NAME "ImagePro")
set(CPACK_PACKAGE_VENDOR "yingtu")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "ImagePro")

# 产物名里的架构标识必须用**真实目标架构**。
# MSVC 下 CMAKE_SYSTEM_PROCESSOR 报的是宿主架构（Win32 工程也报 AMD64），
# 若直接用它，x86 的安装包会被命名成 win64 —— 名字与内容不符，
# 用户下载后无法分辨。
if(IMAGEPRO_TARGET_ARCH STREQUAL "x64")
    set(CPACK_SYSTEM_NAME "win64")
elseif(IMAGEPRO_TARGET_ARCH STREQUAL "x86")
    set(CPACK_SYSTEM_NAME "win32")
elseif(IMAGEPRO_TARGET_ARCH STREQUAL "arm64")
    set(CPACK_SYSTEM_NAME "winarm64")
endif()

# 同一目标架构可能有多个运行时变体（x64 的 msvcrt 与 ucrt64）。
# 变体标识必须进入产物文件名，否则两个变体的产物在 Release 里同名，
# 上传时互相覆盖，用户只会拿到其中一个。
set(IMAGEPRO_PACKAGE_VARIANT "" CACHE STRING
    "产物变体标识（如 ucrt），追加到架构名之后")
if(IMAGEPRO_PACKAGE_VARIANT)
    set(CPACK_SYSTEM_NAME "${CPACK_SYSTEM_NAME}-${IMAGEPRO_PACKAGE_VARIANT}")
endif()
# 注意：不要设置 CPACK_PACKAGE_EXECUTABLES。
# 设置后 CPack 会认为可执行文件位于 $INSTDIR\bin\ 下，开始菜单快捷方式会指向
# 一个不存在的路径（程序实际安装在 $INSTDIR\ 根目录）。
set(CPACK_STRIP_FILES ON)
set(CPACK_VERBATIM_VARIABLES ON)

# 源码与配置归档：便于离线复现与问题追溯。
set(CPACK_SOURCE_GENERATOR "ZIP")
set(CPACK_SOURCE_IGNORE_FILES
    "/build.*/"
    "/deploy.*/"
    "/\\.git/"
    "/\\.github/"
    ".*\\.log"
    "/tests/fixtures/"
)

# ── 便捷版开关 ─────────────────────────────────────────────────
# 便捷版（绿色版）：解压即用，配置与数据都在程序目录内。
# 开启后安装包不再注册服务、不写注册表（便捷版本就不该改动系统状态）。
option(IMAGEPRO_PORTABLE "生成便捷版（绿色版）：配置随程序目录，无需安装" OFF)

if(WIN32)
    set(CPACK_GENERATOR "ZIP")

    find_program(IMAGEPRO_MAKENSIS
        NAMES makensis makensis.exe
        HINTS
            "C:/msys64/mingw64/bin"
            "C:/Program Files (x86)/NSIS"
            "C:/Program Files/NSIS"
        DOC "NSIS makensis 可执行文件路径"
    )

    if(IMAGEPRO_MAKENSIS)
        list(APPEND CPACK_GENERATOR "NSIS")
        # 把探测到的 makensis 显式交给 CPack。
        # CPack 的 NSIS 生成器只在自己的 PATH / 注册表里找 makensis，
        # 不会用 find_program 的结果 —— 在没把 NSIS 目录加进 PATH 的环境里
        # （例如 MSVC 开发者命令行）会在打包阶段才报
        # "Cannot find NSIS compiler makensis"。
        set(CPACK_NSIS_MAKE_NSIS "${IMAGEPRO_MAKENSIS}")
        message(STATUS "ImagePro: makensis = ${IMAGEPRO_MAKENSIS}（启用 NSIS 安装程序）")

        # 「控制面板 → 程序和功能」里显示的名称与版本。
        set(CPACK_NSIS_PACKAGE_NAME "影图")
        set(CPACK_NSIS_DISPLAY_NAME "影图 (ImagePro) ${PROJECT_VERSION}")
        # 卸载程序的名字。默认会取 CPACK_NSIS_PACKAGE_NAME，生成
        # 「影图 (ImagePro).exe」—— 与主程序重名，装完在目录里看着像有两个 exe。
        set(CPACK_NSIS_UNINSTALL_NAME "uninstall")

        # 允许用户在安装向导里改安装目录；默认装到 Program Files。
        set(CPACK_NSIS_MODIFY_PATH ON)
        set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

        # 「应用和功能」里显示的图标与发布者信息。
        set(CPACK_NSIS_INSTALLED_ICON_NAME "${CPACK_PACKAGE_NAME}.exe")
        set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/Hermitweb/ImagePro")
        set(CPACK_NSIS_PACKAGE_VENDOR "${CPACK_PACKAGE_VENDOR}")
        set(CPACK_NSIS_PACKAGE_VERSION "${PROJECT_VERSION}")

        # 安装向导的界面：中文 + 经典大图标向导。
        set(CPACK_NSIS_ENABLE_LANGUAGES "SimpChinese")
        set(CPACK_PACKAGE_INSTALL_DIRECTORY "ImagePro")
        set(CPACK_NSIS_PACKAGE_INSTALL_DIRECTORY "ImagePro")

        # ── 快捷方式 ─────────────────────────────────────────────
        # 开始菜单快捷方式由 CPack 自动创建（Start Menu 组）。
        # 桌面快捷方式需要自己写：CPack 的 NSIS 模板没有对应变量。
        #
        # 这里用 CMake 的 bracket 语法 [=[ ... ]=]：普通引号字符串里
        # 写出的 \" 会被 CPack 再转义成 NSIS 的 `" ，导致 NSIS 把
        # 引号当成字符串定界符而报 "unterminated string"。
        # bracket 语法不做反斜杠转义，值里直接就是给 NSIS 的原文。
        #
        # 代价：bracket 语法同时会阻止 ${} 展开，所以下面写的是字面量。
        # 这处不一致由下面的检查兜住 —— 真要改名时会在配置阶段直接报错，
        # 而不是生成一个指向不存在路径的安装包。
        if(NOT CPACK_PACKAGE_NAME STREQUAL "ImagePro")
            message(FATAL_ERROR
                "NSIS 附加命令里的可执行文件名写死为 ImagePro.exe，"
                "与 CPACK_PACKAGE_NAME(${CPACK_PACKAGE_NAME}) 不一致，请同步修改。")
        endif()

        # 开始菜单的「影图」文件夹由 CPack 自动创建（取自 CPACK_NSIS_PACKAGE_NAME），
        # 但主程序的快捷方式需要自己写：
        # 不设置 CPACK_PACKAGE_EXECUTABLES 时 CPack 不知道该为谁建快捷方式；
        # 设置了又会把目标指向并不存在的 $INSTDIR\bin\（程序实际装在 $INSTDIR 根目录）。
        #
        # 第三个参数是图标文件：这样快捷方式在资源管理器里显示应用图标，
        # 而不是 Windows 的默认程序图标。
        set(CPACK_NSIS_CREATE_ICONS_EXTRA [=[
  CreateShortCut "$SMPROGRAMS\$STARTMENU_FOLDER\影图.lnk" "$INSTDIR\ImagePro.exe" "$INSTDIR\ImagePro.exe"
  CreateShortCut "$DESKTOP\影图.lnk" "$INSTDIR\ImagePro.exe" "$INSTDIR\ImagePro.exe"
]=])
        # 卸载时删除快捷方式。
        #
        # ⚠ 这里必须用 $MUI_TEMP 而不是 $STARTMENU_FOLDER：
        # 安装时 $STARTMENU_FOLDER 是运行期变量（用户可在向导里改），
        # 但卸载段里 CPack 用 MUI_STARTMENU_GETFOLDER 把它读进的是 $MUI_TEMP，
        # $STARTMENU_FOLDER 在卸载时是**空的** ——
        # 用它拼出来的路径会打到 $SMPROGRAMS\影图.lnk（不存在），
        # 真正位于 $SMPROGRAMS\影图\影图.lnk 的快捷方式删不掉，
        # 于是开始菜单里留下一个装着残留快捷方式的空文件夹。
        set(CPACK_NSIS_DELETE_ICONS_EXTRA [=[
  Delete "$DESKTOP\影图.lnk"
  Delete "$SMPROGRAMS\$MUI_TEMP\影图.lnk"
  RMDir "$SMPROGRAMS\$MUI_TEMP"
]=])

        if(NOT IMAGEPRO_PORTABLE)
            # ── 后台服务 ─────────────────────────────────────────
            # 安装器以管理员权限运行（装到 Program Files 必然提权），
            # 因此可以直接调用程序自身的服务注册命令。
            # 用 --silent 避免在安装过程中再弹一次对话框卡住向导。
            set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS [=[
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ImagePro" "InstallLocation" "$INSTDIR"
  IfFileExists "$INSTDIR\ImagePro.exe" 0 +2
  ExecWait '"$INSTDIR\ImagePro.exe" --install-service --silent'
]=])

            # 卸载时先停服务再删服务，否则服务还指向已被删除的 exe。
            set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
  IfFileExists "$INSTDIR\ImagePro.exe" 0 +2
  ExecWait '"$INSTDIR\ImagePro.exe" --uninstall-service --silent'
]=])
        endif()
    else()
        message(STATUS "ImagePro: 未找到 makensis，跳过 NSIS 安装程序（ZIP 仍可用）。")
    endif()
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)

# ── 便捷版（绿色版）────────────────────────────────────────────
# 与安装版的差别只有一处：配置与数据落在程序目录内（靠 portable.mode 标记），
# 不写注册表、不注册服务、卸载 = 直接删目录。
#
# 实现方式：按普通方式把运行时依赖与 exe 收进暂存目录，写入便携标记，
# 最后打成 ZIP。
#
# 该目标始终可用（便捷版是独立产物，与安装版并存）；
# IMAGEPRO_PORTABLE 只影响**安装器**是否注册后台服务。
set(_portable_dir "${CMAKE_BINARY_DIR}/portable")
# 便捷版压缩包名同样用真实目标架构（MSVC 的 CMAKE_SYSTEM_PROCESSOR 是宿主架构）。
if(IMAGEPRO_TARGET_ARCH STREQUAL "unknown" OR NOT IMAGEPRO_TARGET_ARCH)
    set(_portable_arch "${CMAKE_SYSTEM_PROCESSOR}")
else()
    set(_portable_arch "${IMAGEPRO_TARGET_ARCH}")
endif()
if(IMAGEPRO_PACKAGE_VARIANT)
    set(_portable_arch "${_portable_arch}-${IMAGEPRO_PACKAGE_VARIANT}")
endif()
set(_portable_zip
    "${CMAKE_BINARY_DIR}/ImagePro-${PROJECT_VERSION}-portable-${_portable_arch}.zip")

add_custom_target(portable
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_portable_dir}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_portable_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_BINARY_DIR}/runtime" "${_portable_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:ImagePro>" "${_portable_dir}/${CPACK_PACKAGE_NAME}.exe"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_portable_dir}/translations"
    # 从主程序旁的 translations/ 取 .qm，而不是 ${CMAKE_BINARY_DIR}/translations/：
    # 多配置生成器（MSVC）下 .qm 落在 <build>/<Config>/translations/，
    # <build>/translations/ 是空的 —— 便捷版会因此缺翻译文件。
    COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE_DIR:ImagePro>/translations/ImagePro_zh_CN.qm"
            "${_portable_dir}/translations/ImagePro_zh_CN.qm"
    COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE_DIR:ImagePro>/translations/ImagePro_en.qm"
            "${_portable_dir}/translations/ImagePro_en.qm"
    COMMAND ${CMAKE_COMMAND}
            "-DOUTPUT=${_portable_zip}"
            "-DSTAGE=${_portable_dir}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PortablePack.cmake"
    DEPENDS ImagePro
    COMMENT "生成便捷版（绿色版）压缩包"
    VERBATIM
)

cmake_minimum_required(VERSION 3.16)

project(ImagePro
    VERSION 1.5.0
    DESCRIPTION "影图 — 基于 Qt 5 + libvips 的跨平台桌面图像处理工具"
    HOMEPAGE_URL "https://github.com/Hermitweb/ImagePro"
    LANGUAGES CXX
)

# ── 工程化模块 ────────────────────────────────────────────────────
# 警告集、格式化、打包、运行时依赖收集均拆为独立模块，避免主文件膨胀。
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC OFF)
# AUTORCC 关闭：resources.qrc 引用了 qt5_add_translation 生成的 .qm，
# 而 AUTORCC 不会把这些生成文件登记为 rcc 的依赖，可能把旧的 .qm 静默嵌进程序
# （改了 .ts 却不生效），干净构建时也可能因文件尚未生成而失败。
# 改为下面用 qt5_add_resources 显式生成并声明依赖。
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC ON)

# 供 clang-tidy / IDE 读取精确的编译命令。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Prefer MSYS2 MinGW toolchain when available
if(NOT DEFINED CMAKE_PREFIX_PATH)
    list(APPEND CMAKE_PREFIX_PATH "C:/msys64/mingw64")
endif()

include(cmake/Warnings.cmake)

# VS 18（Visual Studio 2026）移除了 stdext 命名空间，而 Qt 5.15 的头文件（qlist.h / qvector.h / qvarlengtharray.h）
# 仍在用它构造受检数组迭代器 → "'stdext': identifier not found"。
# 强制先包含兼容头，把相关宏改写为等价的「直接用指针」形式。
#
# 必须放在**创建任何目标之前**：add_compile_options 只对之后创建的目标生效，
# 放在 add_executable 之后时 ImagePro 自己拿不到这个选项。
if(MSVC)
    add_compile_options("/FI${CMAKE_CURRENT_SOURCE_DIR}/src/msvc_qt_compat.h")
endif()

# ── 依赖 ─────────────────────────────────────────────────────────
find_package(Qt5 REQUIRED COMPONENTS Widgets Concurrent Svg Test Network LinguistTools)

# libvips 查找走 cmake/FindLibvips.cmake：
#   * MinGW / MSYS2 → pkg-config（vips-cpp）
#   * MSVC（x86 发布版）→ -DIMAGEPRO_VIPS_ROOT=<预编译包解压根目录>
# 两条路线输出的变量名一致，上层代码无须区分。
find_package(Libvips REQUIRED)

# 启用 libvips 优化路径：流式加载、缩略图、格式转换均走 libvips，
# 避免 QImage 全图加载导致大内存占用。
add_compile_definitions(USE_LIBVIPS)

# ── 源文件清单 ───────────────────────────────────────────────────
set(LIB_SOURCES
    src/app/AutoProcessService.cpp
    src/app/ImageProApp.cpp
    src/app/SingleInstanceGuard.cpp
    src/app/StitchPreviewState.cpp
    src/app/ThemeManager.cpp
    src/app/ToastManager.cpp
    src/app/VpkUpdateManager.cpp
    src/app/WindowsService.cpp
    src/core/CompressEngine.cpp
    src/core/ConvertEngine.cpp
    src/core/EditEngine.cpp
    src/core/ExportManager.cpp
    src/core/ImageItem.cpp
    src/core/ImageListModel.cpp
    src/core/MosaicFilter.cpp
    src/core/PdfEngine.cpp
    src/core/ResizeEngine.cpp
    src/core/StitchEngine.cpp
    src/core/WatermarkEngine.cpp
    src/ui/dialogs/ResultDialog.cpp
    src/ui/ImageEditorWidget.cpp
    src/ui/ImageListWidget.cpp
    src/ui/MainWindow.cpp
    src/ui/PreviewWidget.cpp
    src/ui/PropertyPanel.cpp
    src/ui/StitchCanvas.cpp
    src/ui/StatusBarWidget.cpp
    src/ui/ToolBarWidget.cpp
    src/utils/EditAction.cpp
    src/utils/ErrorHandler.cpp
    src/utils/ErrorLevel.cpp
    src/utils/FileUtils.cpp
    src/utils/FlowLayout.cpp
    src/utils/ImageLoader.cpp
    src/utils/ImageProError.cpp
    src/utils/Logger.cpp
    src/utils/PresetData.cpp
    src/utils/PresetManager.cpp
    src/utils/ResizePresetManager.cpp
    src/utils/ResizeSettings.cpp
    src/utils/StitchPresetManager.cpp
)

set(HEADERS
    src/app/AutoProcessService.h
    src/app/ImageProApp.h
    src/app/SingleInstanceGuard.h
    src/app/StitchPreviewState.h
    src/app/ThemeManager.h
    src/app/ToastManager.h
    src/app/VpkUpdateManager.h
    src/app/WindowsService.h
    src/core/CompressEngine.h
    src/core/ConvertEngine.h
    src/core/EditEngine.h
    src/core/ExportManager.h
    src/core/ImageItem.h
    src/core/ImageListModel.h
    src/core/MosaicFilter.h
    src/core/PdfEngine.h
    src/core/ResizeEngine.h
    src/core/StitchEngine.h
    src/core/WatermarkEngine.h
    src/ui/CollapsibleSection.h
    src/ui/dialogs/ResultDialog.h
    src/ui/ImageEditorWidget.h
    src/ui/ImageListWidget.h
    src/ui/MainWindow.h
    src/ui/PreviewWidget.h
    src/ui/PropertyPanel.h
    src/ui/StitchCanvas.h
    src/ui/StatusBarWidget.h
    src/ui/ToolBarWidget.h
    src/utils/EditAction.h
    src/utils/AppPaths.h
    src/utils/ErrorHandler.h
    src/utils/ErrorLevel.h
    src/utils/FileUtils.h
    src/utils/FlowLayout.h
    src/utils/ImageLoader.h
    src/utils/ImageProError.h
    src/utils/Logger.h
    src/utils/PresetData.h
    src/utils/PresetManager.h
    src/utils/ResizePresetManager.h
    src/utils/ResizeSettings.h
    src/utils/StitchPreset.h
    src/utils/StitchPresetManager.h
)

set(TS_FILES
    translations/ImagePro_zh_CN.ts
    translations/ImagePro_en.ts
)

# ── 国际化与资源 ─────────────────────────────────────────────────
set_source_files_properties(${TS_FILES} PROPERTIES OUTPUT_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/translations")
qt5_add_translation(QM_FILES ${TS_FILES})

# 显式生成资源（替代 AUTORCC），并让 rcc 产物依赖 ${QM_FILES}：
# 保证 lrelease 先跑完，改 .ts 后重新构建一定能拿到新的 .qm。
qt5_add_resources(RCC_SOURCES resources/resources.qrc)
set_property(SOURCE ${RCC_SOURCES} APPEND PROPERTY OBJECT_DEPENDS "${QM_FILES}")

qt5_wrap_cpp(MOC_SOURCES ${HEADERS})

# ImageListWidget.cpp contains a private Q_OBJECT class (ImageListView) that needs
# its own moc output included inline, mimicking the old AUTOMOC behavior.
set(IMAGELISTWIDGET_MOC "${CMAKE_CURRENT_BINARY_DIR}/ImageListWidget.moc")
qt5_generate_moc(src/ui/ImageListWidget.cpp ${IMAGELISTWIDGET_MOC})
set_source_files_properties(${IMAGELISTWIDGET_MOC} PROPERTIES HEADER_FILE_ONLY TRUE)

# ── 核心库 ───────────────────────────────────────────────────────
add_library(ImageProLib STATIC ${LIB_SOURCES} ${HEADERS} ${MOC_SOURCES} ${IMAGELISTWIDGET_MOC})

target_include_directories(ImageProLib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${VIPS_INCLUDE_DIRS}
)

# libvips 的 pkg-config 附带其它依赖的编译宏（如 -DUHDR_USING_SHARED_LIBRARY），
# 必须原样传递，否则头文件里的导出宏与实际链接方式不符。
target_compile_options(ImageProLib PRIVATE ${VIPS_CFLAGS_OTHER})
# libvips 预编译包路径需要的编译定义（如 G_STATIC_CONST）。
target_compile_definitions(ImageProLib PRIVATE ${VIPS_COMPILE_DEFINITIONS})
imagepro_apply_warnings(ImageProLib PRIVATE)

target_link_libraries(ImageProLib PUBLIC
    Qt5::Widgets
    Qt5::Concurrent
    Qt5::Svg
    Qt5::Test
    # Network：单实例守卫用 QLocalServer/QLocalSocket 做跨进程激活。
    Qt5::Network
    ${VIPS_LIBRARIES}
)

# 单点版本号宏：about 框 / 更新检查 / vpk pack 共用 project() 版本。
target_compile_definitions(ImageProLib PUBLIC IMAGEPRO_VERSION="${PROJECT_VERSION}")

# ── Velopack 自动更新集成（默认关闭）────────────────────────────
# 本地 dev 与 master CI 关闭以保持构建绿色（本地无 .NET/vpk/velopack 原生库）；
# 仅 release 作业开启（-DWITH_VELOPACK=ON -DVELOPACK_ROOT=<解压目录>）。
option(WITH_VELOPACK "Enable Velopack auto-update integration" OFF)
if(WITH_VELOPACK)
    if(NOT DEFINED VELOPACK_ROOT OR NOT IS_DIRECTORY "${VELOPACK_ROOT}")
        message(FATAL_ERROR "WITH_VELOPACK=ON requires -DVELOPACK_ROOT=<extracted velopack_libc zip dir>")
    endif()

    # velopack_libc zip 的 lib/ 下含多平台二进制；取 win-x64 MSVC 版本。
    file(GLOB _vpk_src_dll "${VELOPACK_ROOT}/lib/velopack_libc_win_x64_msvc.dll")
    list(LENGTH _vpk_src_dll _vpk_n)
    if(NOT _vpk_n EQUAL 1)
        message(FATAL_ERROR "Expected velopack_libc_win_x64_msvc.dll under VELOPACK_ROOT/lib; found ${_vpk_n}.")
    endif()
    list(GET _vpk_src_dll 0 _vpk_src_dll)

    # MSVC 的 .dll.lib 不能被 MinGW ld 可靠链接，改用 gendef+dlltool 生成 MinGW 导入库。
    # 关键：将源 dll 统一暂存为 velopack.dll——
    #   ① gendex 据此产出 velopack.def（gendef 无 -o，按 dll 基名命名 .def）；
    #   ② dlltool -D velopack.dll 使导入表引用 velopack.dll；
    #   ③ 运行时 exe 按 velopack.dll 加载，与下方 POST_BUILD 拷贝一致。
    find_program(GENDEF_EXECUTABLE gendef)
    find_program(DLLTOOL_EXECUTABLE dlltool)
    if(NOT GENDEF_EXECUTABLE OR NOT DLLTOOL_EXECUTABLE)
        message(FATAL_ERROR "gendef 和 dlltool 是 WITH_VELOPACK=ON 必需的（MSYS2 安装 mingw-w64-x86_64-tools-git）。")
    endif()

    set(VELOPACK_IMPORT_DIR "${CMAKE_BINARY_DIR}/velopack-import")
    set(VELOPACK_DLL         "${VELOPACK_IMPORT_DIR}/velopack.dll")
    set(VELOPACK_DEF         "${VELOPACK_IMPORT_DIR}/velopack.def")
    set(VELOPACK_IMPORT_LIB  "${VELOPACK_IMPORT_DIR}/libvelopack.dll.a")

    add_custom_command(
        OUTPUT  "${VELOPACK_IMPORT_LIB}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VELOPACK_IMPORT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_vpk_src_dll}" "${VELOPACK_DLL}"
        COMMAND "${GENDEF_EXECUTABLE}" "${VELOPACK_DLL}"
        COMMAND "${DLLTOOL_EXECUTABLE}" -d "${VELOPACK_DEF}" -l "${VELOPACK_IMPORT_LIB}" -D velopack.dll
        WORKING_DIRECTORY "${VELOPACK_IMPORT_DIR}"
        DEPENDS "${_vpk_src_dll}"
        COMMENT "Staging velopack.dll + generating MinGW import library"
        VERBATIM
    )
    add_custom_target(velopack_import_lib DEPENDS "${VELOPACK_IMPORT_LIB}")

    add_library(velopack::velopack SHARED IMPORTED GLOBAL)
    set_target_properties(velopack::velopack PROPERTIES
        IMPORTED_LOCATION             "${VELOPACK_DLL}"
        IMPORTED_IMPLIB               "${VELOPACK_IMPORT_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${VELOPACK_ROOT}/include"
    )

    target_link_libraries(ImageProLib PUBLIC velopack::velopack)
    target_compile_definitions(ImageProLib PUBLIC WITH_VELOPACK)
endif()

# ── Windows 资源与兼容性清单 ───────────────────────────────────
# app.manifest 声明 Win10/11 兼容性、DPI 感知（PerMonitorV2）、长路径与 UTF-8 代码页。
# 缺少它时 Windows 会把程序当成 Win7 之前的应用：界面被位图拉伸而模糊、
# 不认超过 260 字符的路径、版本探测 API 返回伪造值。
set(IMAGEPRO_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.ico")

# 清单里的版本必须是四段式（x.y.z.w），与 PE 资源版本一致。
if(PROJECT_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(IMAGEPRO_MANIFEST_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}.0")
else()
    set(IMAGEPRO_MANIFEST_VERSION "${PROJECT_VERSION}.0")
endif()

configure_file(resources/app.manifest.in "${CMAKE_CURRENT_BINARY_DIR}/app.manifest" @ONLY)
set(IMAGEPRO_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/app.manifest")

# 清单的嵌入方式按工具链分开：
#
#   MinGW / Clang：清单编成独立的 manifest.rc（RT_MANIFEST 资源）。
#   MSVC：MSVC 链接器自己会生成并嵌入一份清单（资源 ID 1），
#          若资源文件里再放一份 → CVT1100「资源重复」，随后 LNK1123。
#          正确做法是把清单交给链接器合并：/MANIFESTINPUT。
#
# 清单与图标拆成两个 .rc 而不是把清单塞进 app.rc：
# configure_file 只做**一次**替换，被替换进来的值里若再含 @VAR@，
# 不会被二次展开（图标那份里就会出现字面量 "@IMAGEPRO_MANIFEST@"）。
configure_file(resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
set(IMAGEPRO_RC_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/app.rc")

if(NOT MSVC)
    configure_file(resources/manifest.rc.in "${CMAKE_CURRENT_BINARY_DIR}/manifest.rc" @ONLY)
    list(APPEND IMAGEPRO_RC_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/manifest.rc")
endif()

# ── 可执行程序 ───────────────────────────────────────────────────
add_executable(ImagePro WIN32 src/main.cpp ${RCC_SOURCES} ${QM_FILES} ${IMAGEPRO_RC_SOURCES})

if(MSVC)
    # 把我们的清单交给链接器合并（MSVC 官方推荐做法）。
    # 链接器默认已开启 /MANIFEST（VS 工程模板里 GenerateManifest=true），
    # 无需重复指定；这里只补输入。
    target_link_options(ImagePro PRIVATE "/MANIFESTINPUT:${IMAGEPRO_MANIFEST}")
endif()

target_link_libraries(ImagePro PRIVATE ImageProLib)
imagepro_apply_warnings(ImagePro PRIVATE)

if(WITH_VELOPACK)
    # IMPORTED 目标不会自动追踪生成的导入库，需显式依赖。
    add_dependencies(ImagePro velopack_import_lib)
    # 把暂存的 velopack.dll 拷到 exe 旁，本地运行与 vpk 打包均可加载。
    add_custom_command(TARGET ImagePro POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VELOPACK_DLL}" "$<TARGET_FILE_DIR:ImagePro>/velopack.dll"
        COMMENT "Copying velopack.dll next to ImagePro.exe"
    )
endif()

# Copy generated .qm files next to the binary so they can also be loaded from disk.
# 注意目标位置：ImageProApp 探测的是 applicationDirPath()/translations，
# 之前这里拷回源码目录（lrelease 的 OUTPUT_LOCATION），实际不起作用。
add_custom_command(TARGET ImagePro POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ImagePro>/translations"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${QM_FILES} "$<TARGET_FILE_DIR:ImagePro>/translations/"
    COMMENT "Copying translation files next to the binary..."
)

# 运行时依赖收集（Windows）。必须在 add_executable 之后引入：
# 该模块向 ImagePro 目标追加 POST_BUILD 命令。
include(cmake/RuntimeDeps.cmake)

# 格式化与静态分析放在 RuntimeDeps 之后：
# 它们需要知道「构建用的工具链自带的 clang-format / clang-tidy 在哪里」。
include(cmake/Format.cmake)
include(cmake/StaticAnalysis.cmake)

# ── 安装规则 ─────────────────────────────────────────────────────
# 让 `cmake --install` / CPack 产出可直接运行的目录，而不只是裸 exe。
install(TARGETS ImagePro RUNTIME DESTINATION .)
install(FILES ${QM_FILES} DESTINATION translations)
install(FILES resources/icons/app.ico DESTINATION .)
install(FILES LICENSE.txt DESTINATION .)

# 运行时依赖（Qt 插件与 DLL、libvips、MinGW 运行库）位于 runtime/。
# 只安装这个目录，保留 platforms/、styles/、imageformats/ 等插件子目录结构。
install(DIRECTORY "${CMAKE_BINARY_DIR}/runtime/"
        DESTINATION .
        FILES_MATCHING
        PATTERN "*.dll"
)

# ── 测试 ─────────────────────────────────────────────────────────
option(BUILD_TESTS "Build test executables" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── 打包 ─────────────────────────────────────────────────────────
# 必须在 install() 规则之后引入（CPack 依赖已定义的 install 规则）。
include(cmake/Packaging.cmake)

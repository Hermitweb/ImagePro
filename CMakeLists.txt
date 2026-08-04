cmake_minimum_required(VERSION 3.16)
project(ImagePro VERSION 1.4.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Prefer MSYS2 MinGW toolchain when available
if(NOT DEFINED CMAKE_PREFIX_PATH)
    list(APPEND CMAKE_PREFIX_PATH "C:/msys64/mingw64")
endif()

find_package(Qt5 REQUIRED COMPONENTS Widgets Concurrent Svg Test LinguistTools)

find_package(PkgConfig REQUIRED)
pkg_check_modules(VIPS REQUIRED vips-cpp)

# 启用 libvips 优化路径：流式加载、缩略图、格式转换均走 libvips，
# 避免 QImage 全图加载导致大内存占用。
add_compile_definitions(USE_LIBVIPS)

set(LIB_SOURCES
    src/app/ApplicationState.cpp
    src/app/ImageProApp.cpp
    src/app/ListSelectionState.cpp
    src/app/PreviewInteractionState.cpp
    src/app/StitchPreviewState.cpp
    src/app/ThemeManager.cpp
    src/app/ToastManager.cpp
    src/app/VpkUpdateManager.cpp
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
    src/ui/dialogs/ProgressDialog.cpp
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
    src/app/ApplicationState.h
    src/app/ImageProApp.h
    src/app/ListSelectionState.h
    src/app/PreviewInteractionState.h
    src/app/StitchPreviewState.h
    src/app/ThemeManager.h
    src/app/ToastManager.h
    src/app/VpkUpdateManager.h
    src/core/CompressEngine.h
    src/core/ConvertEngine.h
    src/core/EditEngine.h
    src/core/ExportManager.h
    src/core/ImageItem.h
    src/core/ImageListModel.h
    src/core/PdfEngine.h
    src/core/ResizeEngine.h
    src/core/StitchEngine.h
    src/core/WatermarkEngine.h
    src/ui/CollapsibleSection.h
    src/ui/dialogs/ProgressDialog.h
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

set(RESOURCE_FILES
    resources/resources.qrc
)

set_source_files_properties(${TS_FILES} PROPERTIES OUTPUT_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/translations")
qt5_add_translation(QM_FILES ${TS_FILES})

qt5_wrap_cpp(MOC_SOURCES ${HEADERS})

# ImageListWidget.cpp contains a private Q_OBJECT class (ImageListView) that needs
# its own moc output included inline, mimicking the old AUTOMOC behavior.
set(IMAGELISTWIDGET_MOC "${CMAKE_CURRENT_BINARY_DIR}/ImageListWidget.moc")
qt5_generate_moc(src/ui/ImageListWidget.cpp ${IMAGELISTWIDGET_MOC})
set_source_files_properties(${IMAGELISTWIDGET_MOC} PROPERTIES HEADER_FILE_ONLY TRUE)

add_library(ImageProLib STATIC ${LIB_SOURCES} ${HEADERS} ${MOC_SOURCES} ${IMAGELISTWIDGET_MOC})

target_include_directories(ImageProLib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${VIPS_INCLUDE_DIRS}
)

target_compile_options(ImageProLib PRIVATE
    ${VIPS_CFLAGS_OTHER}
    -Wall
    -Wextra
)

target_link_libraries(ImageProLib PUBLIC
    Qt5::Widgets
    Qt5::Concurrent
    Qt5::Svg
    Qt5::Test
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

add_executable(ImagePro WIN32 src/main.cpp ${RESOURCE_FILES} ${QM_FILES} resources/app.rc)

target_link_libraries(ImagePro PRIVATE ImageProLib)

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

# Copy generated .qm files next to the binary so resources can reference them
add_custom_command(TARGET ImagePro POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/translations"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${QM_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/translations/"
    COMMENT "Copying translation files..."
)

# Optional test executables
option(BUILD_TESTS "Build test executables" OFF)
if(BUILD_TESTS)
    enable_testing()

    # libvips 在 Windows 下根据可执行文件位置推导模块目录时，
    # 测试程序位于 build 目录而非安装目录，可能定位到不存在的路径并触发崩溃。
    # 为测试固定一个空的模块目录，禁用可选模块加载，避免 ctest 下出现 SEGFAULT。
    set(VIPS_EMPTY_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}/vips-modules-empty")
    file(MAKE_DIRECTORY "${VIPS_EMPTY_MODULE_DIR}")

    function(add_imagepro_test NAME SOURCE)
        add_executable(${NAME} ${SOURCE})
        set_target_properties(${NAME} PROPERTIES AUTOMOC ON)
        target_link_libraries(${NAME} PRIVATE ImageProLib)
        add_test(NAME ${NAME} COMMAND ${NAME})
        set_tests_properties(${NAME} PROPERTIES
            ENVIRONMENT "VIPS_MODULE_PATH=${VIPS_EMPTY_MODULE_DIR}")
    endfunction()

    add_imagepro_test(test_save tests/test_save.cpp)
    add_imagepro_test(test_engines tests/test_engines.cpp)
    add_imagepro_test(test_stitch tests/test_stitch.cpp)
endif()

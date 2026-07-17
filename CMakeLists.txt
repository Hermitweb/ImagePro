cmake_minimum_required(VERSION 3.16)
project(ImagePro VERSION 1.0.0 LANGUAGES CXX)

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

set(LIB_SOURCES
    src/app/ApplicationState.cpp
    src/app/ImageProApp.cpp
    src/app/ListSelectionState.cpp
    src/app/PreviewInteractionState.cpp
    src/app/StitchPreviewState.cpp
    src/app/ThemeManager.cpp
    src/app/ToastManager.cpp
    src/core/CompressEngine.cpp
    src/core/ConvertEngine.cpp
    src/core/EditEngine.cpp
    src/core/ExportManager.cpp
    src/core/ImageItem.cpp
    src/core/ImageListModel.cpp
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

add_executable(ImagePro WIN32 src/main.cpp ${RESOURCE_FILES} ${QM_FILES})

target_link_libraries(ImagePro PRIVATE ImageProLib)

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

    function(add_imagepro_test NAME SOURCE)
        add_executable(${NAME} ${SOURCE})
        target_link_libraries(${NAME} PRIVATE ImageProLib)
        add_test(NAME ${NAME} COMMAND ${NAME})
    endfunction()

    add_imagepro_test(test_save tests/test_save.cpp)
    add_imagepro_test(test_engines tests/test_engines.cpp)
    add_imagepro_test(test_stitch tests/test_stitch.cpp)
endif()

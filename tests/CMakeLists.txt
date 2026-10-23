# 测试注册。
#
# 约定：
#   * 每个测试程序是一个独立的可执行文件，避免相互污染全局状态；
#   * 统一通过 IMAGEPRO_FIXTURES_DIR 宏定位 tests/fixtures，
#     测试不再依赖「从哪个目录运行」；
#   * 每个用例都有超时上限，避免 CI 因挂死而无限等待；
#   * 用例带 label，便于按维度筛选（ctest -L unit / -L engine / ...）。

set(IMAGEPRO_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# libvips 在 Windows 下根据可执行文件位置推导模块目录时，
# 测试程序位于 build 目录而非安装目录，可能定位到不存在的路径并触发崩溃。
# 为测试固定一个空的模块目录，禁用可选模块加载，避免 ctest 下出现 SEGFAULT。
set(VIPS_EMPTY_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}/vips-modules-empty")
file(MAKE_DIRECTORY "${VIPS_EMPTY_MODULE_DIR}")

# 注册一个测试程序。
#   add_imagepro_test(<target> <source> [LABELS <l1> [l2 ...]])
# NAME / SOURCE 为位置参数，LABELS 为可选关键字参数。
function(add_imagepro_test name source)
    cmake_parse_arguments(ARG "" "" "LABELS" ${ARGN})

    add_executable(${name} ${source})
    set_target_properties(${name} PROPERTIES AUTOMOC ON)

    target_link_libraries(${name} PRIVATE ImageProLib)
    imagepro_apply_warnings(${name} PRIVATE)

    target_compile_definitions(${name} PRIVATE
        IMAGEPRO_FIXTURES_DIR="${IMAGEPRO_FIXTURES_DIR}"
        IMAGEPRO_TRANSLATIONS_DIR="${CMAKE_SOURCE_DIR}/translations"
    )

    add_test(NAME ${name} COMMAND ${name})

    # 多配置生成器（MSVC / Visual Studio）下，测试可执行文件落在
    # <build>/tests/<Config>/，而运行时依赖（Qt DLL、libvips、MSVC CRT）
    # 收集在 <build>/runtime/ 并同步到主程序所在的 <build>/<Config>/。
    # 测试程序因此找不到 DLL，表现为 0xC0000135（STATUS_DLL_NOT_FOUND）。
    # 这里把运行库一并复制到测试目录；依赖 ImagePro 保证 runtime/ 已收集完毕。
    # GENERATOR_IS_MULTI_CONFIG 是**全局属性**，不是变量
    # （用同名变量去判断会永远得到空值，条件恒假）。
    get_property(_imagepro_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_imagepro_multi_config AND IMAGEPRO_RUNTIME_DIR)
        # 多配置生成器（MSVC / Visual Studio）下，测试可执行文件落在
        # <build>/tests/<Config>/，而运行时依赖（Qt DLL、libvips、MSVC CRT）
        # 收集在 <build>/runtime/ —— 测试程序因此找不到 DLL，
        # 表现为 0xC0000135（STATUS_DLL_NOT_FOUND）。
        #
        # 同步动作放在**一个独立目标**里做，而不是每个测试目标各自拷贝：
        # 多个测试项目并行构建时，各自往同一个目录拷贝会互相抢文件锁，
        # 随机报 "Error copying directory ... Permission denied"。
        if(NOT TARGET imagepro_test_runtime)
            add_custom_target(imagepro_test_runtime
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${IMAGEPRO_RUNTIME_DIR}" "${CMAKE_BINARY_DIR}/tests/$<CONFIG>"
                DEPENDS ImagePro
                COMMENT "同步运行时依赖到测试可执行文件目录"
                VERBATIM
            )
        endif()
        add_dependencies(${name} imagepro_test_runtime)
    endif()

    set_tests_properties(${name} PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "VIPS_MODULE_PATH=${VIPS_EMPTY_MODULE_DIR}"
    )

    if(ARG_LABELS)
        set_tests_properties(${name} PROPERTIES LABELS "${ARG_LABELS}")
    endif()
endfunction()

# ── 现有测试 ─────────────────────────────────────────────────────
# 注意：本文件位于 tests/ 下，源文件路径相对本目录给出。
add_imagepro_test(test_save    test_save.cpp    LABELS unit io)
add_imagepro_test(test_engines test_engines.cpp LABELS unit engine)
add_imagepro_test(test_stitch  test_stitch.cpp  LABELS unit engine)

# ── 新增测试 ─────────────────────────────────────────────────────
# 预设子系统：JSON 往返、损坏文件容错、版本兼容、内置预设稳定性。
add_imagepro_test(test_presets test_presets.cpp LABELS unit config)

# 编辑引擎：标注渲染、裁剪、马赛克、撤销重做历史。
add_imagepro_test(test_edit    test_edit.cpp    LABELS unit engine)

# 工具层：文件路径工具、错误上报管线、预设校验规则。
add_imagepro_test(test_utils   test_utils.cpp   LABELS unit utils)

# 应用层：单实例守卫（重复实例检测与跨进程激活）。
add_imagepro_test(test_app     test_app.cpp     LABELS unit app)

# 模型层：列表加载失败的汇总上报（批量失败不能逐张刷屏）。
add_imagepro_test(test_model   test_model.cpp   LABELS unit model)

# UI 层：主窗口布局不变量（面板宽度、滚动区、状态栏输出尺寸）。
add_imagepro_test(test_ui      test_ui.cpp      LABELS unit ui)

# ── fixtures 生成工具 ────────────────────────────────────────────
# 手动运行，不注册为 ctest 用例：
#   cmake --build build --target generate_fixtures
#   ./generate_fixtures [输出目录]
# 此前该文件未注册进构建，tests/fixtures 也就无法重新生成。
add_executable(generate_fixtures fixtures/generate.cpp)
target_link_libraries(generate_fixtures PRIVATE Qt5::Gui)
imagepro_apply_warnings(generate_fixtures PRIVATE)

# ── 便捷目标 ─────────────────────────────────────────────────────
# 一键跑完整测试套件：cmake --build build --target check
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "运行 ImagePro 完整测试套件"
    VERBATIM
)

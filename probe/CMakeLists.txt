# 探针式审计：驱动 ImageProLib 的真实操作路径（加载/保存/转换/缩放/压缩/
# 拼接/水印/PDF/编辑/预设/文件工具），输出逐条 PASS/FAIL/INFO。
#
# 用法（独立构建目录，避免影响常规构建）：
#   cmake -B build-probe -DIMAGEPRO_BUILD_PROBES=ON
#   cmake --build build-probe --target probe_audit
#   ./build-probe/probe_audit.exe
#
# 不注册进 ctest：探针写临时文件、断言的是「行为契约」而非精确回归值。

add_executable(probe_audit probe_main.cpp)
target_link_libraries(probe_audit PRIVATE ImageProLib)
# 输出到构建根目录：RuntimeDeps 已把 Qt/libvips 运行库同步到 ImagePro.exe 旁边，
# 放同一目录即可直接运行，无需再拷一份。
set_target_properties(probe_audit PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# 性能基准：100 张图片场景的加载/拼接/批处理实测数据。
add_executable(perf_probe perf_main.cpp)
target_link_libraries(perf_probe PRIVATE ImageProLib)
set_target_properties(perf_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

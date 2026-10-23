# 影图 ImagePro

基于 Qt 5 + libvips 的跨平台桌面图像处理工具。

![版本](https://img.shields.io/badge/version-1.5.0-blue)
![平台](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)
![许可](https://img.shields.io/badge/license-proprietary-orange)

---

## 功能模块

- **拼接 (Stitch)**：纵向、横向、网格拼接多张图片，支持统一尺寸、间距、背景色、自动去白边。
- **转换 (Convert)**：批量转换图片格式（PNG/JPG/WebP/BMP/TIFF 等）。
- **压缩 (Compress)**：按质量、目标大小或智能模式压缩图片。
- **水印 (Watermark)**：文字/图片水印，支持位置、平铺、透明度、旋转、边距。
- **尺寸 (Resize)**：百分比、像素、预设尺寸调整，支持宽高比锁定、插值算法。
- **编辑 (Edit)**：矩形、椭圆、箭头、文字、画笔、马赛克、裁剪等标注工具。
- **批处理 (Batch)**：对选中的多张图片批量执行转换/压缩/水印/尺寸操作。

## 界面特点

- 左侧缩略图列表（仅显示缩略图，悬浮显示文件名和大小）。
- 右侧属性面板（宽度 200–260px，紧凑布局，可折叠分组）。
- 中间预览区支持缩放、旋转、原图/效果对比。
- 底部状态栏显示图片总数与选中数量。
- 支持 Ctrl+点击多选，右键菜单操作作用于选中图片。
- 支持中文/英文界面切换，主题切换（亮色/暗色），主题选择重启后保持。
- 语言切换**无需重启**：界面即时换语言，用户已调好的参数保留。
- 单实例运行：重复启动会把已有窗口提到前台，而不是再开一个窗口。

---

## 构建环境

| 依赖 | 版本 |
|---|---|
| Qt | 5.15+ |
| CMake | 3.16+ |
| libvips | 8.x（`pkg-config` 查找 `vips-cpp`，或 `-DIMAGEPRO_VIPS_ROOT=<预编译包根目录>`） |
| 编译器 | MinGW-w64（MSYS2，主线路）/ MSVC（x86 发布版） |

MSYS2 下安装依赖：

```bash
pacman -S mingw-w64-x86_64-{qt5,libvips,gcc,cmake,ninja,pkg-config}
```

## 构建与测试

```bash
# 配置
cmake -B build -G Ninja -DBUILD_TESTS=ON

# 编译
cmake --build build -j8

# 测试
ctest --test-dir build --output-on-failure
# 等价便捷目标
cmake --build build --target check
```

### 构建选项

| 选项 | 默认 | 说明 |
|---|---|---|
| `BUILD_TESTS` | `OFF` | 编译测试程序并注册到 CTest |
| `IMAGEPRO_WERROR` | `OFF` | 警告即错误。CI 与发布构建请开启 |
| `WITH_VELOPACK` | `OFF` | 启用自动更新，需同时指定 `-DVELOPACK_ROOT=<解压目录>` |

CI 使用的严格组合：

```bash
cmake -B build -G Ninja -DBUILD_TESTS=ON -DIMAGEPRO_WERROR=ON
```

### 代码格式与静态分析

```bash
cmake --build build --target format        # 格式化
cmake --build build --target format-check  # 只检查（CI 门禁）
cmake --build build --target tidy           # clang-tidy（CI 阻断门禁，任何告警即失败）
```

> clang-tidy 必须与编译器同属 MSYS2 的 MinGW 工具链（`mingw-w64-x86_64-clang-tools-extra`），
> 否则无法解析 `/mingw64/include` 下的 Qt 头文件。

### 打包

```bash
# 安装到指定前缀（产物可直接运行，含运行时依赖）
cmake --install build --prefix ./dist

# CPack 打包：ZIP 压缩包 + NSIS 安装程序
cd build && cpack            # 两种格式都出
cpack -G NSIS                # 仅安装程序（需 makensis）
```

正式发布走 Velopack（见 `.github/workflows/ci.yml` 的 release 作业）。

### 多架构构建

发布矩阵覆盖三种架构。三者的工具链来源不同，这是各自的硬约束决定的：

| 架构 | 工具链 | 说明 |
|---|---|---|
| **x64** | MSYS2 `UCRT64` | 发布主线路：MSYS2 已把 libvips 从 msvcrt（MINGW64）仓库移除，只有 UCRT64 / CLANG64 / CLANGARM64 还提供 |
| **x86** | MSVC + Qt 官方 `win32_msvc2019` 包 + libvips 官方 `win32-ia32` 包 | MSYS2 **没有** 32 位 Qt5，也没有 32 位 libvips，只能走预编译包 |
| **ARM64** | MSYS2 `CLANGARM64`（aarch64） | **必须在原生 ARM64 Windows（Windows 11 on ARM）上构建** |

一键构建脚本 `build-arch.ps1`：

```powershell
# x64（MSYS2 UCRT64，发行主线路）
./build-arch.ps1 -Arch x64-ucrt

# x86（MSVC）：先准备两份预编译包
python -m aqt install-qt windows desktop 5.15.2 win32_msvc2019 -O D:/devtools/qt
#   https://github.com/lovell/sharp-libvips/releases/download/v8.16.1/libvips-8.16.1-win32-ia32.tar.gz
./build-arch.ps1 -Arch x86 `
                 -QtRoot  D:/devtools/qt/5.15.2/msvc2019 `
                 -VipsRoot D:/devtools/libvips-ia32-full

# ARM64（需在 Windows 11 on ARM 上运行）
./build-arch.ps1 -Arch arm64
```

> **为什么 ARM64 不能在 x64 机器上交叉编译？**
> MSYS2 的 `CLANGARM64` 前缀里所有程序——**包括编译器本身**——都是 ARM64 原生程序，
> 不是「可在 x64 上运行的交叉编译器」。在 x64 主机上直接调用只会得到
> `not a valid application for this OS platform`。`build-arch.ps1` 会在预检阶段
> 识别这一点并直接给出替代方案，而不是让你去猜。
> CI 里由 `windows-11-arm` runner（原生 ARM64）执行同一套流程。

CI 的 `build-arch` 矩阵与之对应：x64 / x64-ucrt 跑在 `windows-latest`，
ARM64 跑在 `windows-11-arm`，x86 跑在 `windows-latest` 的 MSVC 路线。

**当前验证状态**：

| 架构 | 构建 | 单元测试 | 安装包 / 便捷版 | 安装→卸载闭环 |
|---|---|---|---|---|
| x64 | ✅ 本地 | ✅ 9/9 | ✅ | ✅ 注册表/快捷方式/服务全部验证，卸载后无残留 |
| x86 | ✅ 本地（MSVC） | ✅ 9/9 | ✅ | ✅ 同上 |
| ARM64 | ⏳ 由 CI 的 `windows-11-arm` 执行 | ⏳ | ⏳ | ⏳ |

> ARM64 之所以无法本地验证，是环境硬约束而非项目缺陷 —— 见上面的说明。

---

## 项目结构

```
ImagePro/
├── CMakeLists.txt          # 顶层编排：依赖、目标、安装、测试、打包
├── cmake/                  # 可复用构建模块（警告/格式化/依赖/打包）
├── docs/
│   └── ARCHITECTURE.md     # 架构说明与工程约定
├── resources/              # 图标、主题、资源文件
│   ├── icons/
│   ├── themes/
│   └── resources.qrc
├── src/                    # 源码
│   ├── app/                # 应用层：主题、Toast、更新
│   ├── core/               # 领域层：图像处理引擎（无 UI 依赖）
│   ├── ui/                 # 表现层：QWidget 组件
│   └── utils/              # 基础设施：日志、错误、文件、预设
├── tests/                  # 单元测试
├── translations/           # 国际化翻译文件
├── .clang-format           # 代码格式标准
└── .clang-tidy             # 静态分析配置
```

分层依赖规则（详见 [架构说明](docs/ARCHITECTURE.md)）：

```
ui  ──►  core  ──►  utils
```

`core/` 不得依赖 `ui/`——所有引擎必须能在无 GUI 环境下运行与测试。

---

## 测试

| 测试程序 | 覆盖范围 |
|---|---|
| `test_save` | 保存/加载往返、逐像素比对、alpha 保留、缺失文件错误路径 |
| `test_engines` | 转换 / 压缩 / 尺寸 / 水印引擎的端到端处理 |
| `test_stitch` | 拼接尺寸、颜色断言、统一尺寸分支 |
| `test_presets` | 预设 JSON 往返、类别持久化、无效预设拒绝、损坏文件备份、旧格式兼容 |
| `test_edit` | 马赛克越界防护、标注渲染、裁剪边界、命中测试、滤镜 |
| `test_utils` | 唯一输出路径、原子占位、错误上报管线、跨线程队列投递、元类型注册 |
| `test_app` | 单实例守卫：重复实例被拒、跨进程激活、非主实例退出不影响主实例 |
| `test_model` | 批量失败汇总上报、缩略图绘制路径不解码、旋转复用已解码缩略图 |
| `test_ui` | 主窗口布局不变量：面板宽度、滚动区、缩略图铺满、状态栏输出尺寸 |

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build -L unit      # 仅单元测试
```

测试通过 `IMAGEPRO_FIXTURES_DIR` 宏定位 `tests/fixtures/`，不依赖运行目录。

---

## 国际化

- **源语言统一为英文**，中文只出现在 `translations/ImagePro_zh_CN.ts` 的译文里。
  把中文写进 `tr()` 会让该处在任何语言下都只显示中文。
- 切换语言**无需重启**：Qt 投递 `LanguageChange` 事件，各控件自行重译；
  属性面板整体重建并用 `WidgetState` 还原用户已填的取值。
- 门禁由 `tests/test_ui.cpp` 把关：`.ts` 里不得有未译条目，源串不得含中文。

更新翻译：

```bash
# 从源码提取源串（会移除过时条目）
lupdate src -ts translations/ImagePro_zh_CN.ts translations/ImagePro_en.ts \
          -locations none -no-obsolete
# 补齐新增条目的译文
python translations/fill_missing.py translations/ImagePro_zh_CN.ts translations/ImagePro_en.ts
# 构建时 lrelease 自动重新生成 .qm 并编入资源
cmake --build build -j8
```

---

## 配置与数据

| 内容 | 位置 |
|---|---|
| 预设 | `AppConfigLocation/grid_presets.json` 等 |
| 界面状态 | `AppConfigLocation/ui-state.json` |
| 日志 | `AppDataLocation/logs/` |

可用环境变量覆盖，便于测试隔离与便携模式：

- `IMAGEPRO_CONFIG_DIR` —— 配置目录
- `IMAGEPRO_DATA_DIR` —— 数据目录（日志）

---

## 文档

- [架构说明](docs/ARCHITECTURE.md) —— 分层、数据流、内存所有权、工程约定、已知风险
- [贡献指南](CONTRIBUTING.md) —— 环境、构建、测试要求、提交流程
- [变更日志](CHANGELOG.md)

## 许可证

本项目为内部项目，保留所有权利。

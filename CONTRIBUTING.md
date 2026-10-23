# 贡献指南

感谢参与影图（ImagePro）的开发。本文档约定从环境准备到合入的完整流程。

---

## 1. 环境准备

| 依赖 | 版本 | 安装（MSYS2 / MinGW-w64） |
|---|---|---|
| Qt | 5.15+ | `mingw-w64-x86_64-qt5` |
| libvips | 42+ | `mingw-w64-x86_64-libvips` |
| 编译器 | GCC 13+ | `mingw-w64-x86_64-gcc` |
| 构建 | CMake 3.16+ / Ninja | `mingw-w64-x86_64-cmake` `mingw-w64-x86_64-ninja` |
| 打包工具 | clang-format / clang-tidy | `mingw-w64-x86_64-clang-tools-extra` |
| 安装包 | makensis（可选） | `mingw-w64-x86_64-nsis` |

```bash
pacman -S mingw-w64-x86_64-{qt5,libvips,gcc,cmake,ninja,pkg-config,clang-tools-extra,nsis}
```

> **工具链必须一致**：clang-tidy 要与 gcc 同属 MSYS2 的 MinGW。
> 用 Windows 版 LLVM 的 clang-tidy 解析不了 `/mingw64/include` 下的 Qt 头文件，
> 会报「file not found」并产生上万个假告警。

---

## 2. 构建与测试

```bash
# 配置（开发）
cmake -B build -G Ninja -DBUILD_TESTS=ON

# 编译
cmake --build build -j8

# 测试（唯一的验收入口）
ctest --test-dir build --output-on-failure
# 或
cmake --build build --target check
```

### 常用构建选项

| 选项 | 默认 | 说明 |
|---|---|---|
| `BUILD_TESTS` | `OFF` | 编译测试程序并注册到 CTest |
| `IMAGEPRO_WERROR` | `OFF` | 警告即错误。**CI 必须开启** |
| `WITH_VELOPACK` | `OFF` | 启用自动更新。需同时指定 `-DVELOPACK_ROOT=<解压目录>` |

CI 使用的是严格组合：

```bash
cmake -B build -G Ninja -DBUILD_TESTS=ON -DIMAGEPRO_WERROR=ON
```

### 本地验证自动更新（Velopack）

`WITH_VELOPACK=ON` 需要 Velopack 的 libc 发行包与导入库生成工具：

```bash
pacman -S mingw-w64-x86_64-tools-git   # 提供 gendef / dlltool

# 下载 velopack_libc（CI 的 release 作业做同样的事）
curl -L -o velopack_libc.zip \
  https://github.com/velopack/velopack/releases/latest/download/velopack_libc_1.2.161.zip
unzip velopack_libc.zip -d velopack-libc

cmake -B build-vpk -G Ninja -DWITH_VELOPACK=ON \
      -DVELOPACK_ROOT="$PWD/velopack-libc"
cmake --build build-vpk -j8
```

链接通过即说明 C++ API 符号与 DLL 一致（ABI 正确）；再启动一次 `build-vpk/ImagePro.exe`
验证运行时加载。改动 `VpkUpdateManager` 后请务必走一遍这两步。

---

## 3. 代码规范

### 格式化

格式的唯一标准是仓库根目录的 `.clang-format`。

```bash
# 格式化全部受管源文件
cmake --build build --target format

# 只检查不修改（CI 门禁）
cmake --build build --target format-check
```

提交前请务必跑一次 `format`。CI 会在 `format-check` 失败时拒绝合入。

### 静态分析

`.clang-tidy` 定义检查集，CI 中是**阻断门禁**：

```bash
cmake --build build --target tidy
```

任何告警都会让命令返回非零，从而让 CI 失败。基线清理已经完成
（143 → 0），因此新增告警一律视为回归。

处理告警的优先级：

1. **能修就修** —— 多数告警指向真实缺陷（整型溢出、变量遮蔽、缺省分支、
   有符号位运算、浮点循环计数器）。
2. **就地豁免** —— 确实无法规避的（如 Win32 句柄转换、libvips 的 C API 形态），
   用 `// NOLINTNEXTLINE` 并写明原因。
3. **不要靠放宽检查集来"解决"** —— `.clang-tidy` 里每条排除项都必须附上理由；
   新增排除项需要在 PR 里说明为什么这条检查在本项目不适用。

### 提交信息

采用 Conventional Commits：

```
<type>(<scope>): <subject>

<body>

<footer>
```

| type | 用途 |
|---|---|
| `feat` | 新功能 |
| `fix` | 缺陷修复 |
| `refactor` | 不改变外部行为的重构 |
| `perf` | 性能优化 |
| `test` | 仅测试改动 |
| `docs` | 仅文档改动 |
| `build` | 构建系统改动 |
| `ci` | CI 配置改动 |
| `chore` | 杂项（依赖、脚本） |

示例：

```
fix(stitch): 修复缩放分支内重复释放 libvips 图像导致的释放后使用

缩放结果为惰性管线，管线自身持有原图依赖引用。
分支内再 unref 一次会使原图引用计数提前归零，
后续 vips_insert 读取已释放内存。删除 4 处分支内的 unref，
只在循环末尾释放我们持有的那一个引用。

Closes #123
```

### 架构约束

详见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。两条硬性规则：

1. `src/core/` 不得依赖 `src/ui/`。引擎必须能在无 GUI 环境下运行和测试。
2. `src/utils/` 不得依赖 `src/core/` 或 `src/ui/`。

---

## 4. 测试要求

### 必须补测试的场景

修复缺陷时，**必须**同时提交一条在修复前会失败的测试。这条规则没有例外。

新增引擎功能时，覆盖：正常路径 + 至少一条错误路径。

### 测试结构

- 每个测试文件是一个独立可执行文件，放在 `tests/` 下。
- 在 `tests/CMakeLists.txt` 注册：

```cmake
add_imagepro_test(test_myfeature test_myfeature.cpp LABELS unit engine)
```

- 测试数据用 `QTemporaryDir`，不要写进源码树。
- 需要固定素材时用 `IMAGEPRO_FIXTURES_DIR` 宏定位 `tests/fixtures/`，
  不要依赖「从哪个目录运行」。
- 用例要有实际断言。**只断言 `!isNull()` 或文件存在，等于没测。**
  涉及像素的改动要做逐像素比对。

### 断言像素时注意行距

Qt 对 `Format_RGB888` / `Format_Grayscale8` 每行按 4 字节对齐填充。
若要覆盖行填充相关的缺陷，请刻意选用非 4 倍数的宽度（如 101），
否则缺陷不会暴露。

---

## 5. 国际化约定

- **源串一律用英文**。把中文写进 `tr()` 会让该处在任何语言下都只显示中文
  —— 这类问题在代码评审时很难发现，靠 `tests/test_ui.cpp` 的门禁兜住。
- 新增用户可见文本时：
  1. 用英文写 `tr("...")`；
  2. 跑 `lupdate` 更新 `.ts`；
  3. 在 `ImagePro_zh_CN.ts` 里补上中文译文，`ImagePro_en.ts` 里译文等于源串。
- 需要在语言切换后立刻更新的界面元素（菜单、面板、工具栏），必须响应
  `QEvent::LanguageChange`：要么逐控件重设文本，要么整体重建
  （重建时务必用 `WidgetState` 把用户已填的取值还原）。

---

## 6. 配置与数据约定

- 所有配置目录解析走 `AppPaths::appConfigDir()`，不要直接调 `QStandardPaths`。
- 写用户数据一律用 `QSaveFile` 原子写，并检查 `write()` / `commit()` 返回值。
- 配置文件格式变更时：加 `version` 字段，读取时兼容旧格式，
  解析失败先备份为 `.corrupt.bak` 再覆盖，绝不静默丢用户数据。

---

## 7. 提交流程

1. 从 `master` 切出特性分支：`git checkout -b feature/xxx`。
2. 本地跑通：`cmake --build build --target format check`。
3. 提交，推送，开 PR。
4. CI 必须全绿：构建（`-Werror`）→ 测试 → 格式检查。
5. 合并到 `master`。发布走 `v*` tag，由 CI 的 release 作业产出安装包。

---

## 8. 报告缺陷

请附上：

- 影图版本（关于对话框里可见）
- 操作系统版本
- 复现步骤
- 预期结果 vs 实际结果
- 日志文件（`%APPDATA%/yingtu/ImagePro/logs/` 下的当天日志）

日志已带线程 ID 与调用点上下文，能大幅缩短定位时间。

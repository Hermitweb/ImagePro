# 变更日志

本文件记录影图（ImagePro）的显著变更。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

---

## [1.5.0] — 2026-10-24

本次分四个阶段推进：先把此前累积的修复真正编译验证、把测试与构建体系补齐到工业级标准；
再针对 UI 层（操作逻辑 / 布局 / 性能）做一轮以「实测 + 截图」为依据的优化；
接着补齐中英文即时切换；最后完成 Windows 10/11 系统性兼容、后台服务、
安装/卸载闭环与 x86 / x64 / ARM64 多架构发布链。

### 工程化整合：修复

- **英文界面里大面积显示中文**：有 33 处 `tr()` 直接把中文当源串（列表右键菜单、
  空白提示、窗口标题、整个拼接画布）。源语言统一为英文后，中文进 `.ts` 译文。
  这是英文界面最大的缺口 —— 那些位置此前在任何语言下都只显示中文。
- **中文界面里混着英文**：有 20 条 `.ts` 条目的译文被填成了英文原文
  （`Output directory is empty`、`Filter`、`Preview: %1` 等），导致中文界面
  出现「至少需要 2 张图片；Output directory is empty」这种半中半英的句子。
- **语言切换必须重启应用**：改为**免重启即时切换**。
  - 各控件响应 Qt 的 `LanguageChange` 事件自行重译；
  - 属性面板整体重建（文本全部由 `tr()` 产生），并用新增的 `WidgetState`
    通用取值快照把用户已调的方向/间距/质量等参数原样还原；
  - 装/卸翻译器会连发多次 `LanguageChange`，加了切换闸门让界面只重建一次
    （属性面板重建实测耗时从 ~8s 降到 ~1.5s）。
- **工具按钮图标在英文界面仍是汉字**：改为按语言取字形
  （中文界面「拼/转/压/水/编/尺/批」，英文界面 S/V/Z/W/E/R/B），
  翻译键用独立的 `Icon: xxx` 一组，不与正文混用。
- **工具栏按钮文字被挤掉**：「移除」「清空」在窄工具栏里只剩图标，
    补上 tooltip（与文字同步重译），图标不再无从辨认。

### 工程化整合：新增

- **翻译完整性门禁**（`tests/test_ui.cpp`）：`.ts` 里不得有未译条目，
  源串不得含中文 —— 这道自动化门禁直接堵住上面两类问题的回归。
  另外还逐项比对中英两个界面的可见文本：英文原文含字母却与中译文一字不差的
  条目超过阈值即失败。这条检查专门针对「`tr()` 源串被写成运行时字符串后，
  lupdate 收录不到、把译文当过时条目删掉，中文界面静默退回英文」——
  开发过程中确实发生过（工具栏 8 个按钮标签整体退回英文）。
- **双语渲染验证**：测试会分别载入中英文翻译渲染窗口并断言标题，
  同时产出 `ui-zh.png` / `ui-en.png` 两张截图供人工目视比对。
- **免重启切换的回归测试**：运行时装/卸翻译器，断言界面标题与面板标签
  随之切换，且用户已改的下拉框取值不丢。

### Windows 打包与部署

- **Windows 10/11 兼容性清单**：新增 `resources/app.manifest.in`，声明
  PerMonitorV2 DPI 感知（125%/150% 缩放下界面不再被系统位图拉伸）、
  Win10/11 supportedOS、长路径（>260 字符）、UTF-8 活动代码页。
  缺少清单时 Windows 会把程序当成 Win7 之前的应用：界面发虚、路径被截断、
  版本探测 API 返回伪造值。清单的路径与版本号由 CMake 注入。
- **Windows 后台服务**：新增「影图 自动处理服务」——
  - `ImagePro.exe --install-service` / `--uninstall-service` / `--service` /
    `--service-status`；
  - 注册/卸载会自动触发 UAC 提权（程序清单声明 asInvoker，不长期持有管理员权限）；
  - 服务本体监视指定目录，把新出现的图片按配置自动处理（转 WebP/JPEG、
    压缩、缩放）后输出，配置在 `%APPDATA%\...\service.json`；
  - 服务模式用 `QCoreApplication` 而非 `QApplication` —— 服务运行在无桌面会话的
    环境里，GUI 应用会启动失败。
- **安装包（NSIS）**：注册到「控制面板 → 程序和功能」（含显示名、版本、
  发布者、安装位置、卸载命令、图标、主页链接），创建桌面与开始菜单快捷方式，
  安装时自动注册后台服务，卸载时先停服务再清理。
- **便捷版（绿色版）**：解压即用。程序目录下的 `portable.mode` 标记让配置、
  日志、预设全部落在程序目录内，不写系统配置区、不写注册表、不装服务；
  卸载就是删目录。便捷版与安装版可并存。
- **多架构发布矩阵**：三种架构各自打通了一条**实测可行**的构建链 ——
  - **x64**：MSYS2 `MINGW64`（主线路）/ `UCRT64`；
  - **x86**：MSVC + Qt 官方 `win32_msvc2019` 包 + libvips 官方 `win32-ia32` 包。
    MSYS2 既没有 32 位 Qt5 也没有 32 位 libvips，这是唯一可行路径；
  - **ARM64**：MSYS2 `CLANGARM64`（aarch64）。

  配套改动：
  - 新增 `cmake/FindLibvips.cmake` —— 统一 pkg-config（MinGW）与
    `-DIMAGEPRO_VIPS_ROOT=<预编译包根目录>`（MSVC）两条查找路线，
    输出变量名一致，上层代码无须区分；
  - `cmake/RuntimeDeps.cmake` 增加 **PE 头架构校验**：读 windeployqt / objdump
    自身的 PE Machine 字段，确认与目标架构一致。此前 x64 的 windeployqt 一旦被
    ARM64 构建复用，会把 x64 的 Qt DLL 拷进 ARM64 包 —— 产物在目标机器上
    直接起不来，而构建过程全程无任何报错；
  - MSVC 路线补齐 **VC++ 运行库**（vcruntime140 等）随包分发，
    否则干净的 Windows 机器上程序起不来；
  - `build-arch.ps1` 增加 **宿主架构预检**：ARM64 工具链在 x64 主机上会直接
    给出「需要在原生 ARM64 Windows 构建 / 交给 CI 的 windows-11-arm」，
    而不是抛 `not a valid application for this OS platform`；
  - CI `build-arch` 矩阵修正：ARM64 改跑在原生 `windows-11-arm` runner 上
    （MSYS2 的 CLANGARM64 前缀里连编译器都是 ARM64 原生程序，x64 上无法交叉编译），
    x86 走完整的 MSVC + aqtinstall + libvips 预编译包链路；
    同时修掉一个会让**整个 workflow 无法加载**的硬错误 ——
    `${{ env:RUNNER_TEMP }}` 写成了冒号（GitHub 的表达式是 `env.RUNNER_TEMP`），
    表达式解析器直接拒绝整个文件，CI 表现为「0 秒失败 / workflow file issue」。
- **命令行自检的中文是乱码**：`QTextStream(FILE*)` + `setCodec("UTF-8")` 在 Windows 上
  会把 QString 先转成 UTF-8、再把每个字节当 Latin-1 码位转一次 —— **双重编码**
  （「影」写成 `C3 A5 C2 BD C2 B1` 而不是 `E5 BD B1`），终端里看到的是「å½±å›¾」。
  改为 `QString::toUtf8()` + `fwrite()` 直接写字节，只有一次编码。
  同时：GUI 子系统程序默认没有自己的控制台，现在会挂到父进程的控制台上；
  且**仅当标准输出句柄无效时**才把 stdout 重绑到 `CONOUT$` ——
  无条件 `freopen` 会把 `> file` / `|` 的重定向目标丢掉，输出反而进不了管道。
- **卸载后开始菜单文件夹残留**：CPack 的 NSIS 模板只删自己建的快捷方式。
  我们额外建的「影图」快捷方式删不掉，卸载后开始菜单里留下一个装着残留
  快捷方式的空文件夹。
  根因在 NSIS 变量：安装时快捷方式建在 `$SMPROGRAMS\$STARTMENU_FOLDER\`，
  但**卸载段里 `$STARTMENU_FOLDER` 是空的** —— CPack 用
  `MUI_STARTMENU_GETFOLDER` 把该值读进的是 `$MUI_TEMP`。
  用空变量拼出来的路径打到不存在的 `$SMPROGRAMS\影图.lnk`，
  真正位于 `$SMPROGRAMS\影图\影图.lnk` 的快捷方式纹丝不动。
  卸载命令改用 `$MUI_TEMP` 后彻底清理干净。
- **x86 测试全部报 0xC0000135（找不到 DLL）**：两个原因叠加。
  1. 多配置生成器（MSVC）下测试程序落在 `tests/<Config>/`，而运行库收集在
     `runtime/`；且 `windeployqt` 只为主程序收集依赖，主程序不链接 `Qt5Test`，
     `Qt5Test.dll` 从来没被收集过 —— 只有测试程序链接它。
     现在 `Qt5Test.dll` 会被显式收进 `runtime/`，并由一个独立目标统一同步到
     测试目录。
  2. 同步动作原先挂在每个测试目标的 POST_BUILD 上：多个测试项目**并行**构建时
     各自往同一个目录拷贝，互相抢文件锁，随机报
     `Error copying directory ... Permission denied`。
     改为单一目标 `imagepro_test_runtime` 统一同步。
- **MSYS2 已把 libvips 从 msvcrt（MINGW64）仓库移除**：项目依赖 libvips，
  而仓库里只剩 UCRT64 / CLANG64 / CLANGARM64 提供。
  CI 上所有 MINGW64 作业因「target not found: mingw-w64-x86_64-libvips」在装依赖时就失败。
  发布主线路改为 **UCRT64**（通用的 MSYS2 作业与多架构矩阵全部切换），
  并把包名逐条核对到各环境**实际存在**的名字：
  `qt5` 元包在 ucrt64 仓库没有（拆成 qt5-base / qt5-svg / qt5-tools）、
  `pkg-config` 的包名是 `pkgconf`、`tools-git` 已改名 `tools`、
  clangarm64 的编译器包是 `clang` 而不是 `gcc`。
- **MSVC 目标的架构判定错误**：`CMAKE_SYSTEM_PROCESSOR` 在 MSVC 下报的是**宿主**
  架构（Win32 工程仍是 AMD64），导致运行库校验与产物命名都按 x64 处理
  （x86 的安装包会被命名成 `win64`）。改为从编译器路径
  （`bin/Hostx64/<target>/cl.exe`）推断真实目标架构。
- **命令行自检**：`--version` / `--help` / `--service-status`，
  控制台输出固定为 UTF-8（此前在中文系统上会显示乱码）。

### UI 修复

- **旋转/翻转会触发 UI 线程同步解码**：缩略图缓存把「原始解码结果」和「变换后结果」
  合二为一，变换一改就把解码结果也丢掉，下一次绘制只能在 UI 线程重新解码整张大图。
  现在拆成两级缓存：原始解码结果保留，变换后重新做一次几何变换即可。
  90° 的整数倍旋转改用 `FastTransformation`（像素精确重排，既更快也更清晰）。
- **列表每帧重采样**：委托每次绘制都对整张缩略图做一次双线性缩放，滚动时每帧对每个
  可见单元格重复一遍。改为按源图 cacheKey 缓存缩放结果，尺寸变化时整体失效。
- **状态栏逐文件读盘**：拼接输出尺寸的估算原先逐个打开文件读图片头，在主线程上对上百张图
  执行，每次选择变化都卡一下。新增基于「已知图片尺寸」的纯内存估算重载。
- **状态栏拼接输出尺寸永远显示不出来**：`updateStatusBar()` 在拼接分支设置完尺寸后
  又无条件覆盖了一次（此时尺寸为空 → 标签被隐藏）。修正为只设置一次。
- **图片尺寸异步到达后状态栏不刷新**：尺寸是异步加载出来的，而 `MainWindow` 根本没监听
  模型的 `dataChanged`，输出尺寸会一直是空的，直到用户碰巧做了别的操作。
  现按尺寸相关角色过滤后刷新。
- **属性面板内容被裁掉**：八个工具面板的控件数量差异很大，窗口高度不够时内容会被裁掉
  且无法访问。设置区改为放进滚动区，底部「保存」按钮固定可见。
- **属性面板右侧内容横向溢出**：拼接输出文件名的预览标签里是一整段没有空格的路径，
  `QLabel` 即使开了 wordWrap，其 minimumSizeHint 仍按「最长的一个词」计算（即整个文件名），
  把面板撑到 360px 宽后被裁掉。改为：标签宽度跟随视口、文本按可用宽度做中间省略
  （完整名放进 tooltip）。
- **列表缩略图不铺满面板宽度**：网格度量只在「窗口 resize」时重算，而分隔条分配面板宽度
  触发的是**视口 resize**，于是缩略图停留在早期估算的尺寸（面板里大片留白）。
  现在视口 resize 时同步重算。

### UI 测试

- 新增 `tests/test_ui.cpp` —— 主窗口布局不变量（三栏结构、面板宽度上下限、
  中栏吸收多余宽度、属性面板滚动区存在、状态栏输出尺寸正确），并把窗口渲染成 PNG
  落到构建目录，供人工目视检查布局。
- 新增 `tests/test_model.cpp` —— 批量失败汇总上报（5 个坏图 → 恰好 1 条上报）、
  缩略图绘制路径绝不解码、旋转后复用已解码的缩略图。

### 中英文切换：修复

- **拼接引擎编译阻断**：`ImageListWidget.cpp` 的私有 `ImageListView` 类中
  `emit removeRequested(...)` 无对应信号，Delete 键移除请求无法编译。
  新增内部信号并在外层做信号→信号桥接，使 Delete 键与右键「移除」走同一路径。
- **水印定位未使用变量**：`WatermarkEngine` 中 `rows` 声明后从未使用，改为 `constexpr`。
- **`FileUtils` 误报**：以 `QStringList() << ...` 临时量做 range-for 触发 GCC
  `-Wmaybe-uninitialized` 误报，改为具名静态数组。
- **「1 x N」拼接预设语义错误**：该内置预设数据是 `rows=1/columns=1` 且方向为 Grid，
  套用后只输出一格，与名称及描述（Single row）完全不符。改为 `direction=Horizontal`，
  真正排成一行。
- **错误上报管线丢线程归属**：`ErrorHandler` 单例若首次在工作线程被访问，
  会创建在没有事件循环的线程里，排队的 `errorOccurred` 投递无处处理。
  改为挂到 `QCoreApplication` 上，固定归属主线程。
- **错误上报单例双重释放（堆损坏）**：单例改为「故意泄漏」的指针形式后仍有问题——
  静态局部对象会在退出时被析构，而父对象 `QCoreApplication` 会先析构并 delete
  掉这个子对象，随后静态析构又对同一块已释放内存调用析构函数，导致堆损坏
  （退出码 `0xC0000374`）。改为静态指针持有，生命周期由父对象唯一负责。
- **输出路径 TOCTOU 竞态**：`generateUniqueOutputPath()` 采用「先判断存在、再返回路径」，
  两步之间存在竞态窗口；两个并发批处理任务可能选中同名文件互相覆盖，导致用户数据丢失。
  新增 `FileUtils::reserveUniqueOutputPath()`，用 `QIODevice::NewOnly` 把
  「检查存在」与「占用名字」合并为一步原子操作；各引擎改用它，并在写入失败时
  调用 `discardReservedPath()` 清理占位，避免留下 0 字节的「假成功」产物。
- **`FlowLayout` 析构期间的虚调用**：析构函数里调用可虚的 `takeAt()`，
  绕过了虚派发 —— 若存在派生类会取到错误的实现，导致 item 泄漏或重复释放。
  改为直接遍历内部列表 `m_itemList`。
- **大图尺寸计算的整型溢出**：`ImageLoader` 中 `width * height * 4` 一类的
  缓冲区尺寸与指针偏移在 int 中求值，超大图会溢出成负数。改为用
  `qsizetype` / `size_t` 计算。
- **`QLockFile::unlock()` 的陷阱**：该函数会**无条件删除**锁文件。
  若非持锁者调用（例如被拒绝的第二实例在退出时清理自己），
  会把主实例的锁文件删掉，单实例保护随之失效。
  现在只在真正持锁时才解锁。
- **`QWindows` 命名管道允许重名绑定**：`QLocalServer::listen()` 在 Windows 上
  无法检测重复实例（`CreateNamedPipe` 对已存在管道名允许再次创建），
  单实例判定因此改用 `QLockFile`。
- **`ExportManager::copyToFolder` 的数据丢失窗口**：原实现先
  `QFile::remove(dest)` 再 `QFile::copy()`，一旦复制失败（磁盘满、权限不足），
  目标文件已被删除，用户原有文件就此丢失。改用 `QSaveFile` 原子替换，
  提交前目标文件保持不变。
- **构建目录里的 exe 缺少运行时依赖**：运行时依赖被收集到 `runtime/`（打包暂存目录）后，
  构建目录中的 exe 旁边没有 DLL，直接运行会卡在系统的「缺少 DLL」弹窗上。
  现在 `runtime/` 会同步到可执行文件旁边，构建树与安装树都可直接运行。
- **静态分析漏扫头文件**：`HeaderFilterRegex` 写成 `src/.*`，而 Windows 上 clang-tidy
  报出的头文件路径是混合分隔符（`D:\proj\src\utils/FileUtils.h`），正则匹配不到，
  头文件里的问题被静默忽略。改为与分隔符无关的 `.*[\\/]src[\\/].*`。
- **批量加载失败刷屏**：缩略图批量加载逐张走 `loadInfo()`，每张坏图都触发一次
  错误上报与用户提示，几百张坏图会把日志和提示完全淹没。
  新增 `ImageLoader::loadInfoQuiet()`（不上报），`ImageListModel` 改为收集失败项，
  在本批加载结束时汇总成一条上报（含数量与前 5 个路径）。
  回归测试 `test_model` 断言「5 个坏图 → 恰好 1 条上报」。
- **汇总上报的触发时机错误**：`QFutureWatcher::finished` 回调时
  `m_pendingImageDataTasks` 仍持有刚完成的那一批，不能用它是否为空来判断批次结束；
  原写法导致失败项永远不会被上报。

### 中英文切换：新增

- **单实例保护**：新增 `SingleInstanceGuard`，用 `QLockFile` 判定重复实例、
  用 `QLocalServer` 跨进程传递「把窗口提到前台」的请求。
  第二次启动表现为已有窗口被提到前台，而不是弹出第二个窗口或一条提示。
- **静态分析阻断门禁**：新增 `.clang-tidy` 配置与 `tidy` 构建目标
  （`--warnings-as-errors=*`，任何告警即失败），CI 中作为阻断作业。
- **NSIS 安装包**：`cmake/Packaging.cmake` 补齐 NSIS 生成器配置
  （中文界面、安装向导、开始菜单快捷方式、卸载清理、许可文件），
  `cpack -G NSIS` 可直接产出安装程序；新增 `LICENSE.txt` 声明第三方组件许可。
- **CMake 模块化**：顶层 `CMakeLists.txt` 拆出 `cmake/` 下的可复用模块
  （警告集、格式化、静态分析、运行时依赖收集、打包），主文件只做编排。
- **严格警告集与 `-Werror` 开关**：新增 `IMAGEPRO_WERROR` 选项，
  CI 以警告即错误构建；警告集在 `-Wall -Wextra` 基础上增加
  `-Wpedantic -Wshadow -Wnon-virtual-dtor -Wcast-align -Woverloaded-virtual
  -Wformat=2 -Wundef -Wimplicit-fallthrough -Wmissing-declarations`。
- **测试体系扩充**：测试从 3 个程序扩展到 7 个，覆盖预设子系统、编辑引擎、
  工具层与应用层：
  - `test_presets` —— 预设 JSON 往返、类别持久化、无效预设拒绝、
    内置预设 id 稳定性、损坏文件备份、旧格式兼容。
  - `test_edit` —— 马赛克在 RGB888/灰度图上的越界防护、标注渲染、
    裁剪边界、命中测试、灰度滤镜、文字标注。
  - `test_utils` —— 唯一输出路径、原子占位、错误上报、跨线程队列投递、
    元类型注册。
  - `test_app` —— 单实例守卫：重复实例被拒、激活请求送达、
    非主实例退出不影响主实例的保护。
- **测试基础设施**：`tests/CMakeLists.txt` 统一注册，提供超时上限与标签；
  fixtures 通过 `IMAGEPRO_FIXTURES_DIR` 宏定位，不再依赖运行目录；
  新增 `check` 目标一键跑全量测试。
- **可配置配置目录**：新增 `AppPaths`，支持 `IMAGEPRO_CONFIG_DIR` /
  `IMAGEPRO_DATA_DIR` 覆盖配置与数据目录 —— 单元测试可隔离用户真实配置，
  同时支持便携模式。
- **代码规范工具链**：新增 `.clang-format`（4 空格缩进、Allman 大括号、
  100 列）与 `.clang-tidy`，并提供 `format` / `format-check` / `tidy` 构建目标。
- **发布工程化**：补齐 `install()` 规则与 CPack（ZIP / NSIS）配置；
  新增 `cmake/RuntimeDeps.cmake`，用 `windeployqt` + `objdump` 递归收集
  Qt 与 libvips 运行时依赖，使安装产物可直接运行。
- **工程文档**：新增 `docs/ARCHITECTURE.md`、`CONTRIBUTING.md`、`CHANGELOG.md`。
- **CI 质量门禁**：构建作业改用 `-DIMAGEPRO_WERROR=ON`；新增格式检查、
  静态分析（阻断）与打包验证三个作业。

### 中英文切换：变更

- 移除 4 组死代码：`ApplicationState`、`ListSelectionState`、
  `PreviewInteractionState`、`ui/dialogs/ProgressDialog`（均无任何引用），
  并同步清理翻译文件中的失效上下文。
- 预设与 UI 状态的持久化路径统一走 `AppPaths::appConfigDir()`，
  不再各自直接调用 `QStandardPaths`。
- **仓库卫生**：新增 `.gitattributes` 固定文本/二进制属性，避免跨平台行尾差异；
  `.gitignore` 覆盖构建、运行时、日志、测试产物与打包产物；
  清理仓库根目录累积 29 MB 的历史调试日志。

### 已知问题

- `ImageListModel` 的缩略图失败已改为汇总上报，但**尚未纳入单元测试**（需要构造
  带异步加载的模型，测试成本较高）。
- clang-tidy 目前只扫 `src/` 下的 .cpp 及其包含的头文件，`tests/` 下的测试代码未纳入。
- Velopack 的 ABI 已本地验证（下载 `velopack_libc` 后以 `-DWITH_VELOPACK=ON` 完整
  编译并启动成功），但**更新流程的端到端验证**仍需 CI 的 tag 构建产出真实发布包。
- **可执行文件没有 VERSIONINFO 资源**：资源管理器「属性 →详细信息」里的
  文件版本 / 产品版本是空的（版本号目前只体现在清单、About 框、
  安装包显示名与产物文件名上）。
  补这一项需要同时兼容两套方言 —— `rc.exe` 与 GNU `windres` 的 VERSIONINFO
  块语法并不完全一致，一份模板未必两边都能编过；
  待 MSVC 与 MinGW 两条链路都有稳定的模板写法后再补。

---

## [1.4.1] — 2026-07

### 修复

- 修复 PDF 导出的国际化缺失。
- 重新设计拼接交互，替换应用图标，修复更新检查 404。
- 修复拼接预览、语言单选按钮与关于对话框样式，修正安装包命名。

---

## [1.2.0]

### 新增

- 修复 VIPS 纵向坐标 bug，重设计拼接交互。
- 引入 Velopack 自动更新与 GitHub Release CI。
- 日志增加线程 ID 与 ERROR/FATAL 调用点上下文。

---

## [1.0.x]

初始公开发布版本：拼接、转换、压缩、水印、尺寸、编辑、批处理七大模块，
中英文界面与亮/暗主题。

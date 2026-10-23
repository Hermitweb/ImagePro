# 影图 ImagePro 架构说明

> 版本基线：v1.5.0　　最后更新：2026-10-24

本文描述影图的代码组织、关键数据流与工程约定。阅读源码前先读本文，可以少走很多弯路。

---

## 1. 技术栈

| 层面 | 选型 | 说明 |
|---|---|---|
| 界面 | Qt 5.15 Widgets | 桌面端，Windows 为主 |
| 图像处理 | libvips（`vips-cpp`） | 全流程走 libvips，避免 QImage 全图解码带来的内存峰值 |
| 并发 | QtConcurrent + QFutureWatcher | 缩略图异步加载、批量处理 |
| 构建 | CMake ≥ 3.16 + Ninja/Makefiles | 模块化，见 `cmake/` |
| 打包 | Velopack（发布）/ CPack（本地复现） | 自动更新由 Velopack 承担 |
| 国际化 | Qt Linguist（`.ts` → `.qm`） | 中英文，构建期编译进资源 |

**关键约束：图像数据流经由 libvips，而非 QImage。**
`ImageLoader` 是唯一负责 QImage ↔ libvips 转换的地方，其余模块拿到的是 `QImage` 或 `VipsImage*`。
新增功能时若要绕开 `ImageLoader` 直接操作像素，必须先确认行距（`bytesPerLine`）与通道数语义。

---

## 2. 目录结构

```
ImagePro/
├── CMakeLists.txt          # 顶层编排：依赖、目标、安装、测试、打包
├── cmake/                  # 可复用构建模块
│   ├── Warnings.cmake      # 警告集与 -Werror 开关
│   ├── Format.cmake        # clang-format 目标（format / format-check）
│   ├── RuntimeDeps.cmake   # Windows 运行时依赖收集
│   ├── CollectRuntimeDeps.cmake  # objdump 递归解析 DLL 依赖
│   └── Packaging.cmake     # CPack 配置
├── src/
│   ├── app/                # 应用层：主题、Toast、更新、主窗口装配
│   ├── core/               # 领域层：图像处理引擎（无 UI 依赖）
│   ├── ui/                 # 表现层：QWidget 组件
│   ├── utils/              # 基础设施：日志、错误、文件、预设
│   └── main.cpp            # 进程入口
├── tests/                  # 单元测试（每个测试一个独立可执行文件）
├── translations/           # .ts 源文件 + .qm 产物
├── resources/              # 图标、主题样式表、qrc
└── docs/                   # 架构与流程文档
```

### 分层依赖规则

```
        ui  ──────►  core  ──────►  utils
         │                          ▲
         └──────────────────────────┘
```

- `core/` **不得**依赖 `ui/`。所有引擎必须能在无 GUI 环境下运行与测试。
- `utils/` 不依赖 `core/`，也不依赖 `ui/`。
- `ui/` 可以依赖 `core/` 与 `utils/`。

违反这条规则的典型反例是把 `QProgressDialog` 传进引擎——引擎应通过信号报告进度，
由 UI 层决定如何呈现。

---

## 3. 核心模块

### 3.1 引擎层（`src/core/`）

| 模块 | 职责 | 对外接口 |
|---|---|---|
| `ImageLoader` | 加载/保存/缩略图/压缩到目标体积；libvips 与 QImage 的唯一桥梁 | 静态方法，全进程唯一入口 |
| `ImageItem` | 单张图片的数据载体：路径、尺寸、旋转/翻转状态、缩略图缓存 | 值语义 + 缓存失效 |
| `ImageListModel` | 图片列表模型；异步加载缩略图并按 `id` 回写结果 | QAbstractListModel |
| `CompressEngine` | 质量 / 目标体积 / 智能压缩 | `process()` |
| `ConvertEngine` | 批量格式转换 | `process()` |
| `ResizeEngine` | 百分比 / 像素 / 预设尺寸调整 | `process()` |
| `WatermarkEngine` | 文字与图片水印，含平铺 | `process()` |
| `StitchEngine` | 纵向 / 横向 / 网格拼接 | `process()` |
| `EditEngine` | 标注渲染（矩形/椭圆/箭头/画笔/文字/裁剪/马赛克/滤镜） | `render()` / `renderWithSelection()` |
| `MosaicFilter` | 六种马赛克风格（自由函数） | `applyMosaic()` |
| `PdfEngine` | 多图导出 PDF | `process()` |
| `ExportManager` | 导出路径与命名策略 | 静态方法 |

**引擎统一约定**

1. 输入是文件路径列表，不是 `QImage` 列表——引擎内部按需流式加载，内存占用与图片总数无关。
2. 失败通过 `bool* ok` 与 `error()` 信号双通道上报，不靠返回值猜测。
3. 引擎不弹窗、不写 `QSettings`、不碰 UI 控件。

### 3.2 表现层（`src/ui/`）

- `MainWindow` —— 装配中枢：持有模型、属性面板、预览区、工具栏，串联所有信号。
- `PropertyPanel` —— 右侧参数面板（200–260px），按工具类型切换分组。
- `PreviewWidget` —— 中央预览区，缩放/旋转/原图对比。
- `ImageListWidget` —— 左侧缩略图列表，内含私有 `ImageListView`（Q_OBJECT）。
- `ImageEditorWidget` —— 标注编辑器，维护 `m_history` 撤销栈。
- `StitchCanvas` —— 拼接画布。
- `CollapsibleSection` —— 可折叠分组控件。
- `ToastManager` —— 右下角浮动提示。

### 3.3 基础设施（`src/utils/`）

| 模块 | 职责 | 注意事项 |
|---|---|---|
| `AppPaths` | 配置/数据目录解析 | 支持 `IMAGEPRO_CONFIG_DIR` / `IMAGEPRO_DATA_DIR` 覆盖 |
| `Logger` | 分级日志，按天分文件，定期清理 | 日志目录来自 `AppPaths::appDataDir()` |
| `ErrorHandler` | 统一错误上报与错误码表 | 单例**必须**归属主线程 |
| `ImageProError` | 错误码 + 元类型声明 | 跨线程投递前必须 `qRegisterMetaType` |
| `FileUtils` | 路径生成、格式判定、体积格式化 | 输出路径用 `reserveUniqueOutputPath` 原子占位 |
| `PresetManager` / `ResizePresetManager` / `StitchPresetManager` | 三套预设的 JSON 持久化 | 原子写入 + 版本号 + 损坏备份 |
| `FlowLayout` | 流式布局 | 委托与图片列表共用 |

---

## 4. 关键数据流

### 4.1 图片加载与缩略图

```
用户拖入文件
  → ImageListModel::addImages
  → 构建 ImageItem（状态 = Loading）
  → QtConcurrent::mapped 提交 ImageDataTask
      └─ 工作线程：ImageLoader::loadPreview() → QImage
                   （绝不创建 QPixmap——QPixmap 只能在 GUI 线程用）
  → 结果携带 ImageItem::id()
  → GUI 线程 onImageDataLoaded：按 id 匹配回写（不是按行号）
  → 缩略图转 QPixmap，标记 Loaded / Failed
```

**为什么按 id 而不是行号**：删除、重排、复制都会改变行号，按行号回写会把
A 的缩略图写到 B 的行上。按 id 匹配是唯一稳定的方式。

**两级缩略图缓存**（`ImageItem`）：

| 缓存 | 内容 | 失效时机 |
|---|---|---|
| `m_rawThumbnailCache` | 原始解码结果（未应用旋转/翻转） | 仅在图片本身变化时 |
| `m_thumbnailCache` | 已应用当前旋转/翻转的结果 | 旋转/翻转时 |

分成两级的意义：旋转/翻转只需要重做一次几何变换，**不必重新解码文件**。
若只保留一级缓存，变换一改就把解码结果也丢掉，下一次绘制（`data()` → `paintEvent`
链路，UI 线程）只能同步解码整张大图 —— 这是列表操作时最明显的卡顿来源。

**绘制路径绝不解码**：`ImageItem::thumbnail()` 在两级缓存都未命中时返回空图，
由异步加载补上。缩略图未就绪时显示占位符，而不是在 UI 线程同步解码。

**列表绘制的缩放结果也要缓存**：委托的 `paint()` 需要把缩略图缩放并按中心裁切到
方形单元格。`QPixmap::scaled(..., SmoothTransformation)` 每次绘制都要对整张图
重采样，滚动时每帧对每个可见单元格都重做一遍。委托按源图 `cacheKey` 缓存缩放结果，
目标尺寸变化时整体失效。

### 4.1.1 属性面板的滚动与宽度

属性面板固定 200~260px 宽，八个工具面板的控件数量差异很大。两条硬性约束：

1. **设置区必须放进 `QScrollArea`**：窗口高度不够时（最小 600px）内容会被裁掉
   且无法访问 —— 拼接与水印面板尤其容易超出。滚动区只包住设置区，
   底部「保存」按钮固定可见。
2. **滚动区内容的宽度必须跟随视口**：`QStackedWidget` 的 `sizeHint` 由最长的一行
   内容决定（带时间戳的拼接文件名可达 300+ px）。`QScrollArea` 的 `widgetResizable`
   只有在「widget 尺寸已等于视口」时才会去设置 widget 的最大宽度，一旦两者恰好相等
   这条路径就不执行，widget 会停留在自己的 sizeHint 上而横向溢出。
   因此这里监听**视口**的 resize 事件，显式把宽度锁定为视口宽度。

另外：无空格的长文本（文件名、路径）不能用 `wordWrap` 折行 —— `QLabel` 的
`minimumSizeHint` 按「最长的一个词」计算，整段路径反而是最长的词。
这类文本应做**中间省略**（`QFontMetrics::elidedText`），完整内容放进 tooltip。

### 4.2 批量处理

```
用户点「应用」
  → PropertyPanel 收集参数 → MainWindow 构造引擎
  → QProgressDialog 显示进度（无 Cancel——引擎不可取消）
  → QtConcurrent::run 在工作线程执行 engine.process()
  → 进度通过 queued signal 回 GUI 线程更新对话框
  → 完成后关闭对话框、弹出结果对话框
```

进度对话框由 `QPointer` 捕获，worker 侧 `.data()` 并判空，避免异步删除后的悬垂访问。

### 4.2.1 输出路径的原子占位

所有引擎写出文件前，先用 `FileUtils::reserveUniqueOutputPath()` **原子占位**目标文件名：
它以 `QIODevice::NewOnly` 打开文件，把「检查存在」与「占用名字」合并为一步系统调用。

为什么必须这样：原先的 `generateUniqueOutputPath()` 是「先 `QFile::exists()` 判断、再返回路径」，
两步之间存在 TOCTOU 窗口。两个批处理任务并发运行时，可能选中同一个文件名而互相覆盖，
造成用户数据丢失。原子占位让两个任务不可能拿到同一个名字。

配套约定：**写入失败必须调用 `FileUtils::discardReservedPath()`** 清理占位文件，
否则会留下 0 字节的「假成功」产物。`discardReservedPath` 只删除 0 字节文件，
已写入内容的产物不会被误删。

注意区分：用户显式指定的输出路径（`explicitOutputPath`）**不做占位**，
因为那是用户自己的目标文件，写入失败时不得删除。

### 4.3 错误上报

```
引擎失败点
  → ErrorHandler::instance().report(ErrorCode::XXX, 详情)
      ├─ 记入 lastError
      ├─ 按 ErrorLevel 写日志（带错误码，如 IMGPRO-3009）
      └─ 发出 errorOccurred 信号
            └─ MainWindow 订阅 → ToastManager 弹出提示
```

**上报职责的划分**（避免同一个失败被上报两次）：

| 层级 | 负责上报的错误码 | 理由 |
|---|---|---|
| `ImageLoader` | `FILE_NOT_FOUND`、`FILE_CORRUPTED`、`FORMAT_UNSUPPORTED`、`OUTPUT_TOO_LARGE` | 这些是**读取端的成因**，上层只看到「返回了 false」，无从推断 |
| 各引擎 | `EMPTY_FOLDER`、`WRITE_FAILED`、`PERMISSION_DENIED`、`INSUFFICIENT_IMAGES` | 这些是**操作级结果**，只有引擎知道上下文 |
| `ImageProApp` | `LIBVIPS_INIT_FAILED` | 启动期致命错误 |

因此 `ImageLoader` 的**写入**失败不上报（由引擎统一上报 `WRITE_FAILED`），
避免同一次失败弹两次提示。

**批量路径必须静默**：一次拖入几百张图时，逐张上报会让日志和用户提示完全被
失败项淹没。为此 `ImageLoader` 提供两个入口：

| 接口 | 是否上报 | 使用者 |
|---|---|---|
| `loadInfo()` | 是 | 单张操作、引擎内部 |
| `loadInfoQuiet()` | 否 | 批量路径（`ImageListModel` 的缩略图加载） |

`ImageListModel` 用 `loadInfoQuiet()` 加载，把失败项收集到 `m_loadFailurePaths`，
待本批加载结束时汇总成**一条**错误上报（含数量与前 5 个路径）。
既保留诊断价值，又不会刷屏。

**汇总上报的触发时机**：在 `processImageDataQueue()` 里判断「队列已空」时上报。
注意不能用 `m_pendingImageDataTasks` 是否为空来判断批次结束 ——
`QFutureWatcher::finished` 回调触发时，它仍持有刚完成的那一批。

**用户主动重试例外**：用户在界面上点「重试」时（`ImageItem::retryLoad()`），
走的是会上报的 `loadInfo()`。那是用户明确的一次操作，逐条反馈是正确的。

要点：

- 引擎**只负责上报错误码**，不决定如何呈现。新增错误码无需改动引擎。
- `ErrorHandler` 单例必须归属主线程，且**只能用指针持有**：
  静态局部对象会在退出时被析构，而它的父对象（`QCoreApplication`）会先析构并
  delete 掉这个子对象 —— 随后静态析构又对同一块已释放内存调用析构函数，
  造成双重释放与堆损坏。
- `ImageProError` 必须注册为 Qt 元类型，否则跨线程队列连接会在运行时被静默拒绝。

### 4.4 单实例保护

`SingleInstanceGuard`（`src/app/`）由两个机制协作完成，二者分工明确：

| 机制 | 职责 | 为什么用它 |
|---|---|---|
| **QLockFile** | 判定「有没有别人在跑」 | 原子文件锁（Windows 上是 `CREATE_NEW` 语义），全平台可靠 |
| **QLocalServer** | 传递「请把窗口显示到前台」 | 让第二次启动表现为「已有窗口被提到前台」而不是弹出第二个窗口 |

**为什么不能只用 QLocalServer**：Windows 上 `QLocalServer` 底层是命名管道，
`CreateNamedPipe` 对已存在的管道名**允许**再次创建，因此第二个进程 `listen()`
依然成功，根本检测不到冲突。QLockFile 没有这个问题。

**陈旧锁的处理**：`setStaleLockTime(0)` —— 不按时间判定陈旧，只按「锁文件里记录的
进程是否还活着」判定。这样运行数小时的实例不会被误判为陈旧（默认 30 秒阈值会
造成这种误判），而崩溃残留的锁会因进程已死被自动接管。

**两条必须遵守的生命周期约束**（写错会静默破坏单实例保护）：

1. 非主实例退出时**不得**调用 `QLockFile::unlock()` ——
   `unlock()` 会**无条件删除**锁文件，非持锁者调用会删掉主实例的锁。
   因此用 `m_hasLock` 标志区分。
2. 非主实例退出时**不得**调用 `QLocalServer::removeServer()` ——
   那会摘掉主实例的监听套接字，导致后续启动收不到激活请求。
   因此用 `m_isPrimary` 标志区分。

两条都有对应的回归测试（`tests/test_app.cpp`）。

### 4.5 拼接与 libvips 内存所有权

`StitchEngine` 是全项目最容易写错的地方。三条路径的所有权必须严格区分：

| 情况 | `scaled` | 谁负责释放原图 |
|---|---|---|
| 未缩放 | `scaled == img` | 不释放，引用转移给 `infos` |
| 缩放成功 | `scaled != img` | 循环末尾 `g_object_unref(img)` 释放我们持有的那一个引用 |
| 缩放失败 | `scaled == img`（tmp 为空） | 不释放，保留原图 |

**缩放分支内绝不能 `g_object_unref(scaled)`**：缩放结果是惰性管线，管线自身持有原图的依赖引用，
提前释放会导致释放后使用（use-after-free），拼接时读取已释放内存。

---

## 5. 工程约定

### 5.1 编译期

- C++17，`-Wall -Wextra` 起步的严格警告集，`IMAGEPRO_WERROR=ON` 时警告即错误。
- `AUTOMOC` 关闭，改为 `qt5_wrap_cpp` 显式处理，避免 moc 产物与源码不同步。
- `AUTORCC` 关闭，`.qm` 通过 `OBJECT_DEPENDS` 显式声明为 rcc 的前置依赖，
  确保 `lrelease` 先跑，翻译不会静默过期。

### 5.2 配置与数据

- 配置目录：`AppPaths::appConfigDir()`，默认 `QStandardPaths::AppConfigLocation`。
- 可用 `IMAGEPRO_CONFIG_DIR` 覆盖 → 支持单元测试隔离与便携模式。
- 预设文件写入一律走 `QSaveFile`（原子写），并检查 `write()` / `commit()` 返回值。
- 预设文件带 `version` 字段，读取时兼容旧的裸数组格式；解析失败先备份 `.corrupt.bak`。

### 5.3 测试

- 每个测试是独立可执行文件，避免全局状态互相污染。
- 通过 `IMAGEPRO_FIXTURES_DIR` 宏定位 fixtures，不依赖运行目录。
- `ctest --output-on-failure` 是唯一的验收入口。
- 新增缺陷修复时，必须同时补一条能在修复前失败的测试。

### 5.4 代码风格

- `.clang-format` 定义唯一格式；`cmake --build build --target format` 格式化，
  `--target format-check` 是 CI 门禁。
- 4 空格缩进、Allman 大括号、`Type& name`、引用/指针修饰符在类型侧。

### 5.5 国际化

**源语言统一为英文**，中文只出现在 `.ts` 的译文里。这不是风格偏好 ——
早期代码里有 33 处 `tr()` 直接把中文当源串，导致这些位置在任何语言下都只显示中文
（列表右键菜单、空白提示、窗口标题、整个拼接画布）。

翻译文件：`translations/ImagePro_{zh_CN,en}.ts`，构建期由 `lrelease` 编译成 `.qm`
并编入资源（`:/i18n/...`）。

**免重启切换**：Qt 在装/卸翻译器后会向所有顶层控件投递 `QEvent::LanguageChange`，
各控件据此重译：

| 控件 | 重译方式 |
|---|---|
| `MainWindow` | 重建菜单栏与预览工具栏（菜单项与动作二十多条，逐条维护对应关系极易漏） |
| `PropertyPanel` | 重建八个工具面板（文本全部由 `build*Panel()` 的 `tr()` 产生） |
| `ToolBarWidget` / `StatusBarWidget` | 逐个控件重设文本与图标 |
| `ImageListWidget` / `PreviewWidget` | 右键菜单与画布文字在绘制/弹出时翻译，只需处理常驻标签 |

两条关键约定：

1. **重建不能丢用户已填的值**。`PropertyPanel` 重建前用 `WidgetState`
   （`src/utils/WidgetState.h`）快照各控件取值，重建后按同样顺序还原 ——
   否则一次语言切换会把用户刚调好的方向、间距、质量全部清空。
2. **切换期间只重建一次**。装/卸翻译器会连发好几次 `LanguageChange`，
   `ImageProApp::isLanguageSwitching()` 作为闸门：期间控件忽略这些事件，
   切换结束后由应用统一补发一次。

**门禁**：`tests/test_ui.cpp` 会检查 `.ts` 里没有未译条目、且源串不含中文；
并逐项比对中英两个界面的可见文本 —— 英文原文含字母却与中译文一字不差的条目
超过阈值即失败。这条检查专门针对一类难发现的回归：把 `tr()` 的源串写成
运行时字符串后，lupdate 收录不到，会把对应译文当成过时条目删掉，
中文界面于是静默退回英文（曾发生在工具栏的 8 个按钮标签上）。

**注意**：`.ts` 的时间戳倒退（比如从版本库恢复旧文件）会让 `lrelease` 认为
`.qm` 已是最新而不重新生成。恢复旧文件后记得 touch 一下 `.ts`。

### 5.7 Windows 部署形态

同一份代码产出三种形态，差别只在「配置放哪、要不要动系统状态」：

| 形态 | 配置/数据位置 | 注册表 | 后台服务 | 卸载方式 |
|---|---|---|---|---|
| 安装版（NSIS） | `%APPDATA%` | 写「程序和功能」项 | 可选（安装时注册） | 控制面板卸载 |
| 便捷版（绿色版） | 程序目录 `data/` | 不写 | 不注册 | 删目录 |
| 便携标记模式 | 同便捷版 | 不写 | 不注册 | 删目录 |

**便携模式判定**：程序目录下存在 `portable.mode` 即进入便携模式
（`AppPaths::isPortableMode()`）。相比「用启动器设置环境变量」，
标记文件方式从桌面快捷方式、任务栏、命令行启动都能识别，也不需要额外的
`.cmd` 包装。

**单实例与服务可以并存**：服务模式不占用单实例锁（锁是给桌面实例用的），
因此后台自动处理与用户同时使用图形界面不冲突。

**服务模式用 QCoreApplication**：Windows 服务运行在 Session 0（无桌面会话），
此时创建 `QApplication`（GUI）会失败。

### 5.7.1 多架构构建

同一份代码产出三种架构，**工具链来源各不相同**——这是各自的硬约束决定的：

| 架构 | 工具链 | 关键约束 |
|---|---|---|
| x64 | MSYS2 `MINGW64` / `UCRT64` | 主线路，本地开发与发布 |
| x86 | MSVC + Qt 官方 `win32_msvc2019` + libvips 官方 `win32-ia32` | MSYS2 没有 32 位 Qt5，也没有 32 位 libvips |
| ARM64 | MSYS2 `CLANGARM64` | 只能在**原生 ARM64 Windows** 上构建 |

**ARM64 为什么不能交叉编译**：MSYS2 的 `CLANGARM64` 前缀里所有程序
——**包括编译器本身**——都是 ARM64 原生 PE，不是「可在 x64 上运行的交叉编译器」。
在 x64 主机上调用只会得到 `not a valid application for this OS platform`。
`build-arch.ps1` 因此在预检阶段读编译器的 PE Machine 字段，
发现「工具链是 ARM64 而宿主不是」就立即报错并指明替代方案（原生 ARM64 机器，
或 CI 的 `windows-11-arm` runner）。

**构建系统的双工具链支持**：

- `cmake/FindLibvips.cmake` 统一两条 libvips 查找路线 —— pkg-config（MinGW）与
  `-DIMAGEPRO_VIPS_ROOT=<预编译包根目录>`（MSVC），输出变量名一致；
- `cmake/RuntimeDeps.cmake` 读取 windeployqt / objdump 的 **PE Machine 字段**，
  校验它们与目标架构匹配。此前 x64 的 windeployqt 一旦被 ARM64 构建复用，
  会把 x64 的 Qt DLL 拷进 ARM64 包 —— 构建全程无报错，产物在目标机器上直接起不来；
- MSVC 路线额外把 VC++ 运行库（vcruntime140 等）随包分发，
  否则干净的 Windows 机器上程序起不来；
- MSVC 路线使用 **Visual Studio 生成器**而非 Ninja：中文 VS 下 `cl.exe` 的
  `/showIncludes` 前缀是「注意: 包含文件: 」，CMake 会把它写进 `rules.ninja` 的
  `msvc_deps_prefix`，而 Ninja 的词法器不接受非 ASCII 字节
  （`rules.ninja: expected newline, got lexing error`）。该前缀由 VS 安装语言决定，
  `VSLANG=1033` 改不动。

**find_program 的一个坑**：`find_program` 是「名字优先」循环 —— 先在所有路径里找
第一个名字，再找第二个。装了 MSYS2（`windeployqt-qt5.exe`）又装了 Qt 官方包
（`windeployqt.exe`）的机器上，它会先命中 PATH 里的 MSYS2 版本。
因此 windeployqt 改为按目录顺序逐个探测，不交给 `find_program`。

### 5.8 静态分析

- `.clang-tidy` 定义检查集；`cmake --build build --target tidy` 是 CI **阻断门禁**
  （`--warnings-as-errors=*`，任何告警都让构建失败）。
- 工具链必须与编译器一致：clang-tidy 要与 gcc 同属 MSYS2 的 MinGW，
  否则解析 `/mingw64/include` 下的 Qt 头文件会失败。
- **头文件也在扫描范围内**：`HeaderFilterRegex` 必须与路径分隔符无关
  （`.*[\\/]src[\\/].*`）。Windows 上 clang-tidy 报出的头文件路径是混合分隔符
  （如 `D:\proj\src\utils/FileUtils.h`），写成 `src/.*` 会匹配不到，
  导致头文件里的问题被静默忽略。
- 配置里显式排除了几项与 Qt 惯用法冲突的检查，每条都写明了理由：
  `portability-avoid-pragma-once`、`performance-enum-size`、
  `performance-use-std-move`、`clang-diagnostic-sentinel`。
- 确实无法规避的个别告警用 `// NOLINTNEXTLINE` 就地豁免并附上原因，
  不通过放宽检查集来掩盖。

---

## 6. 已知风险与技术债

| 项 | 位置 | 说明 |
|---|---|---|
| `ErrorHandler` 接线未铺满 | 各引擎 | 读取端成因（ImageLoader）与操作级结果（六个引擎）均已接线；`ImageListModel` 的缩略图失败已改为汇总上报 |
| 静态分析尚未覆盖非 src 目录 | `cmake/StaticAnalysis.cmake` | `src/` 下的 .cpp 与其包含的头文件均已覆盖；`tests/` 下的测试代码未纳入 |
| ARM64 产物未在本地验证 | `.github/workflows/ci.yml` | MSYS2 的 CLANGARM64 前缀里全部是 ARM64 原生程序（含编译器），**无法在 x64 主机上交叉编译**。ARM64 构建与测试由 CI 的 `windows-11-arm` runner（原生 ARM64）执行，x64/x86 两条链路已本地实测通过 |
| x86 的 clang-tidy 未覆盖 | `cmake/StaticAnalysis.cmake` | tidy 门禁绑定 MSYS2 的 MinGW 工具链（要解析 `/mingw64/include` 下的 Qt 头文件）。MSVC 构建走 `/W4 /WX` 编译期警告门禁，但静态分析只在 x64 的 MinGW 构建上跑 |

---

## 7. 相关文档

- [构建与测试](../README.md#构建与测试)
- [贡献指南](../CONTRIBUTING.md)
- [变更日志](../CHANGELOG.md)

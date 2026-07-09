# 影图 ImagePro

基于 Qt 5 + libvips 的跨平台桌面图像处理工具。

## 功能模块

- **拼接 (Stitch)**：纵向、横向、网格拼接多张图片，支持统一尺寸、间距、背景色、自动去白边。
- **转换 (Convert)**：批量转换图片格式（PNG/JPG/WebP/BMP/TIFF 等）。
- **压缩 (Compress)**：按质量、目标大小或智能模式压缩图片。
- **水印 (Watermark)**：文字/图片水印，支持位置、平铺、透明度、旋转、边距。
- **尺寸 (Resize)**：百分比、像素、预设尺寸调整，支持宽高比锁定、插值算法。
- **编辑 (Edit)**：矩形、椭圆、箭头、文字、画笔、马赛克、裁剪等标注工具。
- **批处理 (Batch)**：对选中的多张图片批量执行转换/压缩/水印/尺寸操作。

## 界面特点

- 左侧缩略图列表（52×52，仅显示缩略图，悬浮显示文件名和大小）。
- 右侧属性面板（宽度 200–260px，紧凑布局）。
- 中间预览区支持缩放、旋转、原图/效果对比。
- 底部状态栏显示图片总数与选中数量。
- 支持 Ctrl+点击多选，右键菜单操作作用于选中图片。
- 支持中文/英文界面切换，主题切换（亮色/暗色）。

## 构建环境

- Qt 5.15+
- CMake 3.16+
- libvips（通过 pkg-config 查找）
- MinGW-w64（MSYS2）

## 构建步骤

```bash
cd imagepro
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON
cmake --build . -j4
ctest --output-on-failure
```

## 项目结构

```
imagepro/
├── CMakeLists.txt          # 构建配置
├── resources/              # 图标、主题、资源文件
│   ├── icons/
│   ├── themes/
│   └── resources.qrc
├── src/                    # 源码
│   ├── app/                # 应用入口、主题管理
│   ├── core/               # 图像处理引擎
│   ├── ui/                 # 界面组件
│   └── utils/              # 工具类
├── tests/                  # 单元测试
└── translations/           # 国际化翻译文件
```

## 依赖说明

libvips 通过系统 pkg-config 查找（`pkg_check_modules(VIPS REQUIRED vips-cpp)`）。
在 MSYS2 环境中可通过以下命令安装：

```bash
pacman -S mingw-w64-x86_64-libvips
```

## 测试

```bash
cd build
./test_save.exe
./test_engines.exe
./test_stitch.exe
```

## 许可证

本项目为内部项目，保留所有权利。

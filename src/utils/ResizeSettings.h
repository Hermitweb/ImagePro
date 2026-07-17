#pragma once

#include <QSize>
#include <QString>

namespace yingtu {

enum class ResizeMode {
    Percentage,
    Pixel,
    Preset
};

enum class Interpolation {
    Nearest,
    Bilinear,
    Bicubic,
    Lanczos
};

struct ResizeSettings {
    ResizeMode mode = ResizeMode::Percentage;
    int percentage = 100;
    int targetWidth = 800;
    int targetHeight = 600;
    bool lockAspectRatio = true;
    bool fitWithinOriginal = false;
    Interpolation interpolation = Interpolation::Bilinear;
    QString outputFormat = QStringLiteral("original");
    int quality = 90;
    QString outputDir;
    QString explicitOutputDir; // 批量：若非空，直接使用该目录
    QString explicitOutputPath; // 单张：若非空，直接保存到该路径
};

} // namespace yingtu

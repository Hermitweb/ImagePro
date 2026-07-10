#include "ImageProError.h"

namespace yingtu {

ImageProError::ImageProError(ErrorCode code)
    : m_code(code)
{
}

ErrorCode ImageProError::code() const
{
    return m_code;
}

QString ImageProError::codeString() const
{
    return QStringLiteral("IMGPRO-%1").arg(static_cast<int>(m_code), 4, 10, QLatin1Char('0'));
}

QString ImageProError::message() const
{
    switch (m_code) {
    case ErrorCode::UNKNOWN:
        return QStringLiteral("未知错误");
    case ErrorCode::FILE_NOT_FOUND:
        return QStringLiteral("文件不存在或已被移动");
    case ErrorCode::FILE_LOCKED:
        return QStringLiteral("文件被其他进程占用");
    case ErrorCode::FORMAT_UNSUPPORTED:
        return QStringLiteral("不支持的图片格式");
    case ErrorCode::FILE_CORRUPTED:
        return QStringLiteral("图片已损坏，无法读取");
    case ErrorCode::EMPTY_FOLDER:
        return QStringLiteral("该文件夹中没有图片");
    case ErrorCode::BATCH_TOO_LARGE:
        return QStringLiteral("批量过大，已截断处理");
    case ErrorCode::SELECTION_TOO_LARGE:
        return QStringLiteral("选中项过多，请减少选择");
    case ErrorCode::RANGE_START_MISSING:
        return QStringLiteral("请先选中起始项");

    case ErrorCode::INSUFFICIENT_IMAGES:
        return QStringLiteral("至少需要 2 张图片");
    case ErrorCode::SIZE_MISMATCH:
        return QStringLiteral("图片尺寸不一致，请先统一");
    case ErrorCode::OUTPUT_TOO_LARGE:
        return QStringLiteral("输出图过大，请降低分辨率");
    case ErrorCode::EXIF_READ_FAILED:
        return QStringLiteral("EXIF 信息已损坏，仅保留图像数据");
    case ErrorCode::AVIF_ENCODER_MISSING:
        return QStringLiteral("AVIF 编码器未找到");
    case ErrorCode::TARGET_TOO_SMALL:
        return QStringLiteral("目标大小过小，画质损失严重");
    case ErrorCode::ALREADY_SMALLER:
        return QStringLiteral("原图已小于目标大小，无需压缩");
    case ErrorCode::SMART_ESTIMATE_FAILED:
        return QStringLiteral("无法达到目标大小，建议调整参数");
    case ErrorCode::PREVIEW_UNSUPPORTED:
        return QStringLiteral("该格式不支持预览");

    case ErrorCode::DISK_FULL:
        return QStringLiteral("磁盘空间不足，请更换保存路径");
    case ErrorCode::PERMISSION_DENIED:
        return QStringLiteral("无写权限，请更换保存路径");
    case ErrorCode::PATH_TOO_LONG:
        return QStringLiteral("路径过长，请缩短路径");
    case ErrorCode::PATH_INVALID_CHARS:
        return QStringLiteral("路径包含特殊字符，可能无法保存");
    case ErrorCode::REFERENCE_IMAGE_MISSING:
        return QStringLiteral("引用图片缺失");
    case ErrorCode::WRITE_FAILED:
        return QStringLiteral("保存失败");
    case ErrorCode::CONFIG_WRITE_FAILED:
        return QStringLiteral("配置文件写入失败");
    case ErrorCode::CONFIG_CORRUPTED:
        return QStringLiteral("配置文件已损坏，已重置为默认");
    case ErrorCode::CONFIG_VERSION_MISMATCH:
        return QStringLiteral("配置版本不兼容");
    case ErrorCode::CONFIG_RESET_PERMISSION_DENIED:
        return QStringLiteral("无权限重置配置");

    case ErrorCode::LIBVIPS_INIT_FAILED:
        return QStringLiteral("图片引擎初始化失败，请重装软件");
    case ErrorCode::OUT_OF_MEMORY:
        return QStringLiteral("内存不足，请关闭其他程序");
    case ErrorCode::THREAD_POOL_EXHAUSTED:
        return QStringLiteral("系统繁忙，请稍后再试");
    case ErrorCode::RENDER_LIMIT_EXCEEDED:
        return QStringLiteral("超出渲染上限");
    case ErrorCode::MEMORY_DEGRADED:
        return QStringLiteral("内存不足，已降级处理");

    case ErrorCode::USER_CANCELLED:
        return QStringLiteral("操作已取消");
    case ErrorCode::OPERATION_TIMEOUT:
        return QStringLiteral("操作超时");
    }
    return QStringLiteral("未知错误");
}

ErrorLevel ImageProError::level() const
{
    switch (m_code) {
    case ErrorCode::UNKNOWN:
        return ErrorLevel::Error;
    case ErrorCode::FILE_NOT_FOUND:
    case ErrorCode::FORMAT_UNSUPPORTED:
    case ErrorCode::EMPTY_FOLDER:
    case ErrorCode::RANGE_START_MISSING:
    case ErrorCode::ALREADY_SMALLER:
    case ErrorCode::EXIF_READ_FAILED:
    case ErrorCode::PATH_INVALID_CHARS:
    case ErrorCode::REFERENCE_IMAGE_MISSING:
    case ErrorCode::MEMORY_DEGRADED:
        return ErrorLevel::Info;

    case ErrorCode::FILE_LOCKED:
    case ErrorCode::BATCH_TOO_LARGE:
    case ErrorCode::SELECTION_TOO_LARGE:
    case ErrorCode::INSUFFICIENT_IMAGES:
    case ErrorCode::SIZE_MISMATCH:
    case ErrorCode::OUTPUT_TOO_LARGE:
    case ErrorCode::TARGET_TOO_SMALL:
    case ErrorCode::SMART_ESTIMATE_FAILED:
    case ErrorCode::PREVIEW_UNSUPPORTED:
    case ErrorCode::AVIF_ENCODER_MISSING:
    case ErrorCode::PATH_TOO_LONG:
    case ErrorCode::CONFIG_VERSION_MISMATCH:
    case ErrorCode::THREAD_POOL_EXHAUSTED:
    case ErrorCode::OPERATION_TIMEOUT:
        return ErrorLevel::Warning;

    case ErrorCode::FILE_CORRUPTED:
    case ErrorCode::DISK_FULL:
    case ErrorCode::PERMISSION_DENIED:
    case ErrorCode::WRITE_FAILED:
    case ErrorCode::CONFIG_WRITE_FAILED:
    case ErrorCode::CONFIG_RESET_PERMISSION_DENIED:
    case ErrorCode::OUT_OF_MEMORY:
    case ErrorCode::RENDER_LIMIT_EXCEEDED:
    case ErrorCode::USER_CANCELLED:
        return ErrorLevel::Error;

    case ErrorCode::CONFIG_CORRUPTED:
    case ErrorCode::LIBVIPS_INIT_FAILED:
        return ErrorLevel::Fatal;
    }
    return ErrorLevel::Error;
}

bool ImageProError::recoverable() const
{
    switch (m_code) {
    case ErrorCode::UNKNOWN:
        return false;
    case ErrorCode::FILE_LOCKED:
    case ErrorCode::INSUFFICIENT_IMAGES:
    case ErrorCode::SIZE_MISMATCH:
    case ErrorCode::OUTPUT_TOO_LARGE:
    case ErrorCode::TARGET_TOO_SMALL:
    case ErrorCode::DISK_FULL:
    case ErrorCode::PERMISSION_DENIED:
    case ErrorCode::PATH_TOO_LONG:
    case ErrorCode::PATH_INVALID_CHARS:
    case ErrorCode::WRITE_FAILED:
    case ErrorCode::CONFIG_WRITE_FAILED:
    case ErrorCode::CONFIG_RESET_PERMISSION_DENIED:
    case ErrorCode::OUT_OF_MEMORY:
    case ErrorCode::THREAD_POOL_EXHAUSTED:
    case ErrorCode::MEMORY_DEGRADED:
        return true;

    case ErrorCode::FILE_NOT_FOUND:
    case ErrorCode::FORMAT_UNSUPPORTED:
    case ErrorCode::FILE_CORRUPTED:
    case ErrorCode::EMPTY_FOLDER:
    case ErrorCode::BATCH_TOO_LARGE:
    case ErrorCode::SELECTION_TOO_LARGE:
    case ErrorCode::RANGE_START_MISSING:
    case ErrorCode::EXIF_READ_FAILED:
    case ErrorCode::AVIF_ENCODER_MISSING:
    case ErrorCode::ALREADY_SMALLER:
    case ErrorCode::SMART_ESTIMATE_FAILED:
    case ErrorCode::PREVIEW_UNSUPPORTED:
    case ErrorCode::REFERENCE_IMAGE_MISSING:
    case ErrorCode::CONFIG_CORRUPTED:
    case ErrorCode::CONFIG_VERSION_MISMATCH:
    case ErrorCode::LIBVIPS_INIT_FAILED:
    case ErrorCode::RENDER_LIMIT_EXCEEDED:
    case ErrorCode::USER_CANCELLED:
    case ErrorCode::OPERATION_TIMEOUT:
        return false;
    }
    return false;
}

} // namespace yingtu

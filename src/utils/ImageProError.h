#pragma once

#include "ErrorLevel.h"
#include <QString>

namespace yingtu {

enum class ErrorCode
{
    UNKNOWN = 0,

    // 1xxx 文件模块
    FILE_NOT_FOUND = 1001,
    FILE_LOCKED = 1002,
    FORMAT_UNSUPPORTED = 1003,
    FILE_CORRUPTED = 1004,
    EMPTY_FOLDER = 1010,
    BATCH_TOO_LARGE = 1011,
    SELECTION_TOO_LARGE = 1014,
    RANGE_START_MISSING = 1015,

    // 2xxx 处理模块
    INSUFFICIENT_IMAGES = 2001,
    SIZE_MISMATCH = 2002,
    OUTPUT_TOO_LARGE = 2003,
    EXIF_READ_FAILED = 2004,
    AVIF_ENCODER_MISSING = 2005,
    TARGET_TOO_SMALL = 2006,
    ALREADY_SMALLER = 2007,
    SMART_ESTIMATE_FAILED = 2008,
    PREVIEW_UNSUPPORTED = 2009,

    // 3xxx 保存模块
    DISK_FULL = 3001,
    PERMISSION_DENIED = 3002,
    PATH_TOO_LONG = 3003,
    PATH_INVALID_CHARS = 3004,
    REFERENCE_IMAGE_MISSING = 3008,
    WRITE_FAILED = 3009,
    CONFIG_WRITE_FAILED = 3010,
    CONFIG_CORRUPTED = 3011,
    CONFIG_VERSION_MISMATCH = 3012,
    CONFIG_RESET_PERMISSION_DENIED = 3013,

    // 4xxx 系统模块
    LIBVIPS_INIT_FAILED = 4001,
    OUT_OF_MEMORY = 4002,
    THREAD_POOL_EXHAUSTED = 4003,
    RENDER_LIMIT_EXCEEDED = 4004,
    MEMORY_DEGRADED = 4005,

    // 5xxx 取消模块
    USER_CANCELLED = 5001,
    OPERATION_TIMEOUT = 5002,
};

class ImageProError
{
public:
    explicit ImageProError(ErrorCode code);

    ErrorCode code() const;
    QString codeString() const;
    QString message() const;
    ErrorLevel level() const;
    bool recoverable() const;

private:
    ErrorCode m_code;
};

} // namespace yingtu

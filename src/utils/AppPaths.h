#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

namespace yingtu {

/**
 * 便携模式检测。
 *
 * 若可执行文件所在目录存在 `portable.mode` 标记文件，则配置与数据都放在
 * 程序目录内（`<程序目录>/data`），不写入用户配置区、不写注册表。
 *
 * 相比「用一个启动器设置环境变量」的做法，标记文件方式更可靠：
 * 从桌面快捷方式、任务栏、命令行直接启动都能识别，也不需要额外的 .cmd 包装。
 */
inline bool isPortableMode()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    return QFileInfo::exists(exeDir + QStringLiteral("/portable.mode"));
}

/**
 * 应用配置目录。
 *
 * 默认使用 QStandardPaths 的 AppConfigLocation（Windows 上为
 * %APPDATA%/yingtu/影图 一类的路径）。
 *
 * 可用环境变量 IMAGEPRO_CONFIG_DIR 覆盖，作用有三：
 *   1. 单元测试用临时目录隔离，避免污染用户真实配置，也让用例可重复；
 *   2. 便携模式（绿色版）——把程序和数据放同一目录，双击即用；
 *   3. 排障时把配置指向别处，复现问题而不动用户数据。
 */
inline QString appConfigDir()
{
    const QString override = qEnvironmentVariable("IMAGEPRO_CONFIG_DIR");
    if (!override.isEmpty())
        return QDir(override).absolutePath();
    if (isPortableMode())
        return QCoreApplication::applicationDirPath() + QStringLiteral("/data");
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

/**
 * 应用数据目录（日志等运行时产物）。可用 IMAGEPRO_DATA_DIR 覆盖。
 */
inline QString appDataDir()
{
    const QString override = qEnvironmentVariable("IMAGEPRO_DATA_DIR");
    if (!override.isEmpty())
        return QDir(override).absolutePath();
    if (isPortableMode())
        return QCoreApplication::applicationDirPath() + QStringLiteral("/data");
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

} // namespace yingtu

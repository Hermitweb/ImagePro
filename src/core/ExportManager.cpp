#include "ExportManager.h"
#include "utils/ErrorHandler.h"
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QUrl>

namespace yingtu {

bool ExportManager::showInFolder(const QString& filePath)
{
    if (filePath.isEmpty())
        return false;

    QFileInfo fi(filePath);
    QString dir = fi.absolutePath();

#ifdef Q_OS_WIN
    QString param = QDir::toNativeSeparators(dir);
    if (fi.isFile()) {
        QStringList args;
        args << QStringLiteral("/select,") << QDir::toNativeSeparators(filePath);
        return QProcess::startDetached(QStringLiteral("explorer.exe"), args);
    }
    return QProcess::startDetached(QStringLiteral("explorer.exe"), QStringList() << param);
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
#endif
}

bool ExportManager::openFile(const QString& filePath)
{
    if (filePath.isEmpty())
        return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}

QString ExportManager::copyToFolder(const QString& filePath, const QString& targetFolder)
{
    if (filePath.isEmpty() || targetFolder.isEmpty())
        return QString();

    QFileInfo fi(filePath);
    const QString dest = QDir(targetFolder).absoluteFilePath(fi.fileName());

    QFile src(filePath);
    if (!src.open(QIODevice::ReadOnly)) {
        ErrorHandler::instance().report(ErrorCode::FILE_NOT_FOUND, filePath);
        return QString();
    }

    // 用 QSaveFile 写：它先写临时文件，commit() 时再原子替换目标文件。
    //
    // 原实现是「先 QFile::remove(dest) 再 QFile::copy()」——
    // 一旦复制失败（磁盘满、权限不足、被占用），目标文件已经被删掉了，
    // 用户原有的文件就此丢失。QSaveFile 在提交前目标文件保持不变，
    // 只有数据完整落盘后才替换，从根上消除这个数据丢失窗口。
    QSaveFile out(dest);
    if (!out.open(QIODevice::WriteOnly)) {
        src.close();
        ErrorHandler::instance().report(ErrorCode::PERMISSION_DENIED, dest);
        return QString();
    }

    const QByteArray data = src.readAll();
    src.close();

    if (out.write(data) != data.size()) {
        out.cancelWriting();
        ErrorHandler::instance().report(ErrorCode::WRITE_FAILED, dest);
        return QString();
    }
    if (!out.commit()) {
        ErrorHandler::instance().report(ErrorCode::WRITE_FAILED, dest);
        return QString();
    }

    return dest;
}

QStringList ExportManager::copyToFolder(const QStringList& filePaths, const QString& targetFolder)
{
    QStringList copied;
    for (const QString& path : filePaths) {
        QString dest = copyToFolder(path, targetFolder);
        if (!dest.isEmpty())
            copied.append(dest);
    }
    return copied;
}

} // namespace yingtu

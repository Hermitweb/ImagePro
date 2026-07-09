#include "ExportManager.h"
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
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
    QString dest = QDir(targetFolder).absoluteFilePath(fi.fileName());
    if (QFile::exists(dest))
        QFile::remove(dest);
    if (QFile::copy(filePath, dest))
        return dest;
    return QString();
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

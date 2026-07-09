#pragma once

#include <QString>
#include <QStringList>

namespace yingtu {

class ExportManager
{
public:
    static bool showInFolder(const QString& filePath);
    static bool openFile(const QString& filePath);
    static QString copyToFolder(const QString& filePath, const QString& targetFolder);
    static QStringList copyToFolder(const QStringList& filePaths, const QString& targetFolder);
};

} // namespace yingtu

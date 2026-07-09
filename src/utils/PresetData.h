#pragma once

#include <QString>
#include <QStringList>

namespace yingtu {

struct GridPreset {
    QString id;
    QString name;
    int rows = 1;
    int columns = 1;
    int cellWidth = 0;
    int cellHeight = 0;
    bool lockAspectRatio = true;
    int spacing = 0;
    QString background = QStringLiteral("transparent"); // transparent/white/custom
    QString bgColor = QStringLiteral("#FFFFFF");
    QStringList tags;
    QString category = QStringLiteral("Custom");
    bool isBuiltIn = false;

    bool isValid() const { return !id.isEmpty() && !name.isEmpty(); }
};

} // namespace yingtu

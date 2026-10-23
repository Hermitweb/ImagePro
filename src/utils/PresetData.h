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

    // rows/columns 会被当作网格除数使用，必须为正；否则该预设应判为无效，
    // 避免除零或退化输出（此前只校验 id/name，0 或负值也会被当作有效预设）。
    bool isValid() const { return !id.isEmpty() && !name.isEmpty() && rows > 0 && columns > 0; }
};

} // namespace yingtu

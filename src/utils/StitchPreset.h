#pragma once

#include <QString>

namespace yingtu {

struct StitchPreset {
    QString id;
    QString name;
    int rows = 1;
    int columns = 1;
    QString category = QStringLiteral("Custom");
    bool isBuiltIn = false;
    QString description;
};

} // namespace yingtu

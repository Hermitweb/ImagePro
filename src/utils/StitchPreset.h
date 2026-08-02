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
    // 预设的拼接方向：0=Vertical, 1=Horizontal, 2=Grid（与 StitchSettings::Direction 对应）。
    // Grid 预设用 rows/columns 作为网格行列数；Vertical/Horizontal 预设忽略 rows/columns。
    int direction = 2; // 默认 Grid，兼容旧数据
};

} // namespace yingtu

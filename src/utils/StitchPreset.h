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

    // rows/columns 对 Grid 预设会作为网格行列数使用，必须为正。
    // 否则预设会被 loadPresets 过滤掉却在保存时返回成功，重启后「静默消失」。
    bool isValid() const { return !id.isEmpty() && !name.isEmpty() && rows > 0 && columns > 0; }
};

} // namespace yingtu

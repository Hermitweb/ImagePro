#pragma once

#include "PresetData.h"
#include <QList>
#include <QString>

namespace yingtu {

class PresetManager
{
public:
    static PresetManager& instance();

    QList<GridPreset> loadPresets() const;
    bool savePreset(const GridPreset& preset);
    bool deletePreset(const QString& id);
    bool presetExists(const QString& name) const;

    QList<GridPreset> builtInPresets() const;

private:
    PresetManager() = default;
    QString presetsFilePath() const;
};

} // namespace yingtu

#pragma once

#include "StitchPreset.h"
#include <QList>
#include <QString>

namespace yingtu {

class StitchPresetManager
{
public:
    static StitchPresetManager& instance();

    QList<StitchPreset> loadPresets() const;
    bool savePreset(const StitchPreset& preset);
    bool deletePreset(const QString& id);

    QList<StitchPreset> builtInPresets() const;
    QStringList categories() const;

private:
    StitchPresetManager() = default;
    QString presetsFilePath() const;
};

} // namespace yingtu

#pragma once

#include <QString>
#include <QStringList>
#include <QList>

namespace yingtu {

struct ResizePreset {
    QString id;
    QString name;
    int width = 0;
    int height = 0;
    QString category = QStringLiteral("Custom");
    bool isBuiltIn = false;
    QString description;

    bool isValid() const { return !id.isEmpty() && !name.isEmpty() && width > 0 && height > 0; }
};

class ResizePresetManager
{
public:
    static ResizePresetManager& instance();

    QList<ResizePreset> loadPresets() const;
    bool savePreset(const ResizePreset& preset);
    bool deletePreset(const QString& id);
    bool presetExists(const QString& name) const;

    QStringList categories() const;
    QList<ResizePreset> builtInPresets() const;

private:
    ResizePresetManager() = default;
    QString presetsFilePath() const;
};

} // namespace yingtu

#include "PresetManager.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

namespace yingtu {

PresetManager& PresetManager::instance()
{
    static PresetManager mgr;
    return mgr;
}

QString PresetManager::presetsFilePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/grid_presets.json");
}

QList<GridPreset> PresetManager::builtInPresets() const
{
    QList<GridPreset> result;
    auto add = [&result](const QString& name, int rows, int columns, const QString& category) {
        GridPreset p;
        p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        p.name = name;
        p.rows = rows;
        p.columns = columns;
        p.category = category;
        p.isBuiltIn = true;
        result.append(p);
    };

    add(QStringLiteral("Custom"), 1, 1, QStringLiteral("Custom"));
    add(QStringLiteral("2 x 2"), 2, 2, QStringLiteral("Social"));
    add(QStringLiteral("3 x 3"), 3, 3, QStringLiteral("Social"));
    add(QStringLiteral("2 x 3"), 2, 3, QStringLiteral("Social"));
    add(QStringLiteral("3 x 4"), 3, 4, QStringLiteral("Poster"));
    add(QStringLiteral("4 x 4"), 4, 4, QStringLiteral("Poster"));

    return result;
}

QList<GridPreset> PresetManager::loadPresets() const
{
    QList<GridPreset> result = builtInPresets();

    QFile file(presetsFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return result;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray())
        return result;

    QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();
        GridPreset p;
        p.id = obj.value(QStringLiteral("id")).toString();
        p.name = obj.value(QStringLiteral("name")).toString();
        p.rows = obj.value(QStringLiteral("rows")).toInt(1);
        p.columns = obj.value(QStringLiteral("columns")).toInt(1);
        p.cellWidth = obj.value(QStringLiteral("cellWidth")).toInt(0);
        p.cellHeight = obj.value(QStringLiteral("cellHeight")).toInt(0);
        p.lockAspectRatio = obj.value(QStringLiteral("lockAspectRatio")).toBool(true);
        p.spacing = obj.value(QStringLiteral("spacing")).toInt(0);
        p.background = obj.value(QStringLiteral("background")).toString(QStringLiteral("transparent"));
        p.bgColor = obj.value(QStringLiteral("bgColor")).toString(QStringLiteral("#FFFFFF"));
        QJsonArray tagArr = obj.value(QStringLiteral("tags")).toArray();
        for (const QJsonValue& t : tagArr)
            p.tags.append(t.toString());
        p.isBuiltIn = obj.value(QStringLiteral("isBuiltIn")).toBool(false);
        if (p.isValid())
            result.append(p);
    }
    return result;
}

bool PresetManager::savePreset(const GridPreset& preset)
{
    QList<GridPreset> presets;
    QFile file(presetsFilePath());
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue& val : arr) {
                QJsonObject obj = val.toObject();
                GridPreset p;
                p.id = obj.value(QStringLiteral("id")).toString();
                if (!p.id.isEmpty()) {
                    p.name = obj.value(QStringLiteral("name")).toString();
                    p.rows = obj.value(QStringLiteral("rows")).toInt(1);
                    p.columns = obj.value(QStringLiteral("columns")).toInt(1);
                    p.cellWidth = obj.value(QStringLiteral("cellWidth")).toInt(0);
                    p.cellHeight = obj.value(QStringLiteral("cellHeight")).toInt(0);
                    p.lockAspectRatio = obj.value(QStringLiteral("lockAspectRatio")).toBool(true);
                    p.spacing = obj.value(QStringLiteral("spacing")).toInt(0);
                    p.background = obj.value(QStringLiteral("background")).toString(QStringLiteral("transparent"));
                    p.bgColor = obj.value(QStringLiteral("bgColor")).toString(QStringLiteral("#FFFFFF"));
                    QJsonArray tagArr = obj.value(QStringLiteral("tags")).toArray();
                    for (const QJsonValue& t : tagArr)
                        p.tags.append(t.toString());
                    p.isBuiltIn = obj.value(QStringLiteral("isBuiltIn")).toBool(false);
                    presets.append(p);
                }
            }
        }
    }
    file.close();

    bool found = false;
    for (auto& p : presets) {
        if (p.id == preset.id) {
            p = preset;
            found = true;
            break;
        }
    }
    if (!found)
        presets.append(preset);

    QJsonArray arr;
    for (const auto& p : presets) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = p.id;
        obj[QStringLiteral("name")] = p.name;
        obj[QStringLiteral("rows")] = p.rows;
        obj[QStringLiteral("columns")] = p.columns;
        obj[QStringLiteral("cellWidth")] = p.cellWidth;
        obj[QStringLiteral("cellHeight")] = p.cellHeight;
        obj[QStringLiteral("lockAspectRatio")] = p.lockAspectRatio;
        obj[QStringLiteral("spacing")] = p.spacing;
        obj[QStringLiteral("background")] = p.background;
        obj[QStringLiteral("bgColor")] = p.bgColor;
        QJsonArray tagArr;
        for (const QString& t : p.tags)
            tagArr.append(t);
        obj[QStringLiteral("tags")] = tagArr;
        obj[QStringLiteral("isBuiltIn")] = p.isBuiltIn;
        arr.append(obj);
    }

    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}

bool PresetManager::deletePreset(const QString& id)
{
    QList<GridPreset> presets;
    QFile file(presetsFilePath());
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue& val : arr) {
                QJsonObject obj = val.toObject();
                GridPreset p;
                p.id = obj.value(QStringLiteral("id")).toString();
                if (p.id != id) {
                    p.name = obj.value(QStringLiteral("name")).toString();
                    p.rows = obj.value(QStringLiteral("rows")).toInt(1);
                    p.columns = obj.value(QStringLiteral("columns")).toInt(1);
                    p.cellWidth = obj.value(QStringLiteral("cellWidth")).toInt(0);
                    p.cellHeight = obj.value(QStringLiteral("cellHeight")).toInt(0);
                    p.lockAspectRatio = obj.value(QStringLiteral("lockAspectRatio")).toBool(true);
                    p.spacing = obj.value(QStringLiteral("spacing")).toInt(0);
                    p.background = obj.value(QStringLiteral("background")).toString(QStringLiteral("transparent"));
                    p.bgColor = obj.value(QStringLiteral("bgColor")).toString(QStringLiteral("#FFFFFF"));
                    QJsonArray tagArr = obj.value(QStringLiteral("tags")).toArray();
                    for (const QJsonValue& t : tagArr)
                        p.tags.append(t.toString());
                    p.isBuiltIn = obj.value(QStringLiteral("isBuiltIn")).toBool(false);
                    presets.append(p);
                }
            }
        }
    }
    file.close();

    QJsonArray arr;
    for (const auto& p : presets) {
        QJsonObject obj;
        obj[QStringLiteral("id")] = p.id;
        obj[QStringLiteral("name")] = p.name;
        obj[QStringLiteral("rows")] = p.rows;
        obj[QStringLiteral("columns")] = p.columns;
        obj[QStringLiteral("cellWidth")] = p.cellWidth;
        obj[QStringLiteral("cellHeight")] = p.cellHeight;
        obj[QStringLiteral("lockAspectRatio")] = p.lockAspectRatio;
        obj[QStringLiteral("spacing")] = p.spacing;
        obj[QStringLiteral("background")] = p.background;
        obj[QStringLiteral("bgColor")] = p.bgColor;
        QJsonArray tagArr;
        for (const QString& t : p.tags)
            tagArr.append(t);
        obj[QStringLiteral("tags")] = tagArr;
        obj[QStringLiteral("isBuiltIn")] = p.isBuiltIn;
        arr.append(obj);
    }

    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}

bool PresetManager::presetExists(const QString& name) const
{
    for (const auto& p : loadPresets()) {
        if (p.name.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

} // namespace yingtu

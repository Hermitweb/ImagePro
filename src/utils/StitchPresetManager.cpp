#include "StitchPresetManager.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace yingtu {

StitchPresetManager& StitchPresetManager::instance()
{
    static StitchPresetManager mgr;
    return mgr;
}

QString StitchPresetManager::presetsFilePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/stitch_presets.json");
}

QList<StitchPreset> StitchPresetManager::loadPresets() const
{
    QList<StitchPreset> result = builtInPresets();

    QFile file(presetsFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return result;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray())
        return result;

    QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();
        StitchPreset p;
        p.id = obj.value(QStringLiteral("id")).toString();
        p.name = obj.value(QStringLiteral("name")).toString();
        p.rows = obj.value(QStringLiteral("rows")).toInt(1);
        p.columns = obj.value(QStringLiteral("columns")).toInt(1);
        p.category = obj.value(QStringLiteral("category")).toString(QStringLiteral("Custom"));
        p.isBuiltIn = obj.value(QStringLiteral("isBuiltIn")).toBool(false);
        p.description = obj.value(QStringLiteral("description")).toString();
        if (!p.id.isEmpty() && !p.name.isEmpty() && p.rows > 0 && p.columns > 0)
            result.append(p);
    }
    return result;
}

bool StitchPresetManager::savePreset(const StitchPreset& preset)
{
    QList<StitchPreset> presets;
    QFile file(presetsFilePath());
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue& val : arr) {
                QJsonObject obj = val.toObject();
                StitchPreset p;
                p.id = obj.value(QStringLiteral("id")).toString();
                p.name = obj.value(QStringLiteral("name")).toString();
                p.rows = obj.value(QStringLiteral("rows")).toInt(1);
                p.columns = obj.value(QStringLiteral("columns")).toInt(1);
                p.category = obj.value(QStringLiteral("category")).toString(QStringLiteral("Custom"));
                p.isBuiltIn = obj.value(QStringLiteral("isBuiltIn")).toBool(false);
                p.description = obj.value(QStringLiteral("description")).toString();
                if (!p.id.isEmpty())
                    presets.append(p);
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
        obj[QStringLiteral("category")] = p.category;
        obj[QStringLiteral("isBuiltIn")] = p.isBuiltIn;
        obj[QStringLiteral("description")] = p.description;
        arr.append(obj);
    }

    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}

bool StitchPresetManager::deletePreset(const QString& id)
{
    QList<StitchPreset> presets;
    QFile file(presetsFilePath());
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue& val : arr) {
                QJsonObject obj = val.toObject();
                StitchPreset p;
                p.id = obj.value(QStringLiteral("id")).toString();
                if (p.id != id) {
                    p.name = obj.value(QStringLiteral("name")).toString();
                    p.rows = obj.value(QStringLiteral("rows")).toInt(1);
                    p.columns = obj.value(QStringLiteral("columns")).toInt(1);
                    p.category = obj.value(QStringLiteral("category")).toString(QStringLiteral("Custom"));
                    p.isBuiltIn = obj.value(QStringLiteral("isBuiltIn")).toBool(false);
                    p.description = obj.value(QStringLiteral("description")).toString();
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
        obj[QStringLiteral("category")] = p.category;
        obj[QStringLiteral("isBuiltIn")] = p.isBuiltIn;
        obj[QStringLiteral("description")] = p.description;
        arr.append(obj);
    }

    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}

QStringList StitchPresetManager::categories() const
{
    QSet<QString> set;
    for (const auto& p : loadPresets()) {
        if (!p.category.isEmpty())
            set.insert(p.category);
    }
    QStringList list = set.values();
    std::sort(list.begin(), list.end());
    return list;
}

QList<StitchPreset> StitchPresetManager::builtInPresets() const
{
    QList<StitchPreset> result;
    auto add = [&result](const QString& name, int rows, int columns, const QString& category, const QString& desc) {
        StitchPreset p;
        p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        p.name = name;
        p.rows = rows;
        p.columns = columns;
        p.category = category;
        p.isBuiltIn = true;
        p.description = desc;
        result.append(p);
    };

    // 常用
    add(QStringLiteral("Custom"), 1, 1, QStringLiteral("常用"), QStringLiteral("Use current rows/columns"));
    add(QStringLiteral("1 x N"), 1, 1, QStringLiteral("常用"), QStringLiteral("Single row"));

    // 社交媒体
    add(QStringLiteral("2 x 2"), 2, 2, QStringLiteral("社交媒体"), QStringLiteral("4 grids"));
    add(QStringLiteral("3 x 3"), 3, 3, QStringLiteral("社交媒体"), QStringLiteral("9 grids"));
    add(QStringLiteral("2 x 3"), 2, 3, QStringLiteral("社交媒体"), QStringLiteral("6 grids"));

    // 海报/证件
    add(QStringLiteral("3 x 4"), 3, 4, QStringLiteral("海报/证件"), QStringLiteral("12 grids"));
    add(QStringLiteral("4 x 4"), 4, 4, QStringLiteral("海报/证件"), QStringLiteral("16 grids"));

    return result;
}

} // namespace yingtu

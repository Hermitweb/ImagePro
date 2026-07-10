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

    // 经典布局
    add(QStringLiteral("九宫格"), 3, 3, QStringLiteral("经典布局"), QStringLiteral("9 grids"));
    add(QStringLiteral("四宫格"), 2, 2, QStringLiteral("经典布局"), QStringLiteral("4 grids"));
    add(QStringLiteral("六宫格(2x3)"), 2, 3, QStringLiteral("经典布局"), QStringLiteral("6 grids"));
    add(QStringLiteral("六宫格(3x2)"), 3, 2, QStringLiteral("经典布局"), QStringLiteral("6 grids"));
    add(QStringLiteral("十二宫格"), 3, 4, QStringLiteral("经典布局"), QStringLiteral("12 grids"));
    add(QStringLiteral("十六宫格"), 4, 4, QStringLiteral("经典布局"), QStringLiteral("16 grids"));

    // 社交分享
    add(QStringLiteral("朋友圈九宫格"), 3, 3, QStringLiteral("社交分享"), QStringLiteral("WeChat 9 grids"));
    add(QStringLiteral("朋友圈4张"), 2, 2, QStringLiteral("社交分享"), QStringLiteral("WeChat 4 grids"));
    add(QStringLiteral("朋友圈6张"), 2, 3, QStringLiteral("社交分享"), QStringLiteral("WeChat 6 grids"));
    add(QStringLiteral("小红书封面"), 3, 2, QStringLiteral("社交分享"), QStringLiteral("Xiaohongshu 3x2"));
    add(QStringLiteral("Instagram拼图"), 3, 3, QStringLiteral("社交分享"), QStringLiteral("Instagram 9 grids"));

    // 证件照片
    add(QStringLiteral("证件照排版(1寸6张)"), 2, 3, QStringLiteral("证件照片"), QStringLiteral("1寸 6张"));
    add(QStringLiteral("证件照排版(1寸8张)"), 2, 4, QStringLiteral("证件照片"), QStringLiteral("1寸 8张"));
    add(QStringLiteral("证件照排版(2寸4张)"), 2, 2, QStringLiteral("证件照片"), QStringLiteral("2寸 4张"));
    add(QStringLiteral("证件照排版(2寸6张)"), 2, 3, QStringLiteral("证件照片"), QStringLiteral("2寸 6张"));

    // 相册排版
    add(QStringLiteral("相册横排"), 1, 3, QStringLiteral("相册排版"), QStringLiteral("1x3 horizontal"));
    add(QStringLiteral("相册竖排"), 3, 1, QStringLiteral("相册排版"), QStringLiteral("3x1 vertical"));
    add(QStringLiteral("相册双排"), 2, 3, QStringLiteral("相册排版"), QStringLiteral("2x3 album"));
    add(QStringLiteral("相册三排"), 3, 3, QStringLiteral("相册排版"), QStringLiteral("3x3 album"));

    // 海报设计
    add(QStringLiteral("海报拼贴(4格)"), 2, 2, QStringLiteral("海报设计"), QStringLiteral("Poster 4 grids"));
    add(QStringLiteral("海报拼贴(6格)"), 3, 2, QStringLiteral("海报设计"), QStringLiteral("Poster 6 grids"));
    add(QStringLiteral("海报拼贴(8格)"), 2, 4, QStringLiteral("海报设计"), QStringLiteral("Poster 8 grids"));
    add(QStringLiteral("海报拼贴(9格)"), 3, 3, QStringLiteral("海报设计"), QStringLiteral("Poster 9 grids"));

    return result;
}

} // namespace yingtu

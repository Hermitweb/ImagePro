#include "ResizePresetManager.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace yingtu {

ResizePresetManager& ResizePresetManager::instance()
{
    static ResizePresetManager mgr;
    return mgr;
}

QString ResizePresetManager::presetsFilePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/resize_presets.json");
}

QList<ResizePreset> ResizePresetManager::builtInPresets() const
{
    QList<ResizePreset> result;
    auto add = [&result](const QString& name, int w, int h, const QString& category, const QString& desc) {
        ResizePreset p;
        p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        p.name = name;
        p.width = w;
        p.height = h;
        p.category = category;
        p.isBuiltIn = true;
        p.description = desc;
        result.append(p);
    };

    // 社交媒体
    add(QStringLiteral("微信朋友圈"), 1080, 1080, QStringLiteral("社交媒体"), QStringLiteral("1:1"));
    add(QStringLiteral("微博头条"), 1000, 1000, QStringLiteral("社交媒体"), QStringLiteral("1:1"));
    add(QStringLiteral("小红书封面"), 1242, 1660, QStringLiteral("社交媒体"), QStringLiteral("3:4"));
    add(QStringLiteral("Instagram"), 1080, 1080, QStringLiteral("社交媒体"), QStringLiteral("1:1"));

    // 证件照
    add(QStringLiteral("一寸"), 295, 413, QStringLiteral("证件照"), QStringLiteral("295x413"));
    add(QStringLiteral("二寸"), 413, 626, QStringLiteral("证件照"), QStringLiteral("413x626"));
    add(QStringLiteral("小一寸"), 260, 378, QStringLiteral("证件照"), QStringLiteral("260x378"));

    // 电商
    add(QStringLiteral("淘宝主图"), 800, 800, QStringLiteral("电商"), QStringLiteral("1:1"));
    add(QStringLiteral("京东主图"), 800, 800, QStringLiteral("电商"), QStringLiteral("1:1"));
    add(QStringLiteral("详情页"), 790, 1000, QStringLiteral("电商"), QStringLiteral("790x1000"));

    // 屏幕壁纸
    add(QStringLiteral("1080P"), 1920, 1080, QStringLiteral("屏幕壁纸"), QStringLiteral("16:9"));
    add(QStringLiteral("2K"), 2560, 1440, QStringLiteral("屏幕壁纸"), QStringLiteral("16:9"));
    add(QStringLiteral("4K"), 3840, 2160, QStringLiteral("屏幕壁纸"), QStringLiteral("16:9"));
    add(QStringLiteral("手机壁纸"), 1080, 1920, QStringLiteral("屏幕壁纸"), QStringLiteral("9:16"));

    // 打印
    add(QStringLiteral("A4 (150dpi)"), 1240, 1754, QStringLiteral("打印"), QStringLiteral("A4"));
    add(QStringLiteral("A4 (300dpi)"), 2480, 3508, QStringLiteral("打印"), QStringLiteral("A4"));
    add(QStringLiteral("5寸照片"), 1500, 1050, QStringLiteral("打印"), QStringLiteral("5寸"));
    add(QStringLiteral("6寸照片"), 1800, 1200, QStringLiteral("打印"), QStringLiteral("6寸"));

    return result;
}

QList<ResizePreset> ResizePresetManager::loadPresets() const
{
    QList<ResizePreset> result = builtInPresets();

    QFile file(presetsFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return result;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray())
        return result;

    QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();
        ResizePreset p;
        p.id = obj.value(QStringLiteral("id")).toString();
        p.name = obj.value(QStringLiteral("name")).toString();
        p.width = obj.value(QStringLiteral("width")).toInt(0);
        p.height = obj.value(QStringLiteral("height")).toInt(0);
        p.category = obj.value(QStringLiteral("category")).toString(QStringLiteral("Custom"));
        p.isBuiltIn = obj.value(QStringLiteral("isBuiltIn")).toBool(false);
        p.description = obj.value(QStringLiteral("description")).toString();
        if (p.isValid())
            result.append(p);
    }
    return result;
}

bool ResizePresetManager::savePreset(const ResizePreset& preset)
{
    QList<ResizePreset> presets;
    QFile file(presetsFilePath());
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue& val : arr) {
                QJsonObject obj = val.toObject();
                ResizePreset p;
                p.id = obj.value(QStringLiteral("id")).toString();
                if (!p.id.isEmpty()) {
                    p.name = obj.value(QStringLiteral("name")).toString();
                    p.width = obj.value(QStringLiteral("width")).toInt(0);
                    p.height = obj.value(QStringLiteral("height")).toInt(0);
                    p.category = obj.value(QStringLiteral("category")).toString(QStringLiteral("Custom"));
                    p.isBuiltIn = obj.value(QStringLiteral("isBuiltIn")).toBool(false);
                    p.description = obj.value(QStringLiteral("description")).toString();
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
        obj[QStringLiteral("width")] = p.width;
        obj[QStringLiteral("height")] = p.height;
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

bool ResizePresetManager::deletePreset(const QString& id)
{
    QList<ResizePreset> presets;
    QFile file(presetsFilePath());
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue& val : arr) {
                QJsonObject obj = val.toObject();
                ResizePreset p;
                p.id = obj.value(QStringLiteral("id")).toString();
                if (p.id != id) {
                    p.name = obj.value(QStringLiteral("name")).toString();
                    p.width = obj.value(QStringLiteral("width")).toInt(0);
                    p.height = obj.value(QStringLiteral("height")).toInt(0);
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
        obj[QStringLiteral("width")] = p.width;
        obj[QStringLiteral("height")] = p.height;
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

bool ResizePresetManager::presetExists(const QString& name) const
{
    for (const auto& p : loadPresets()) {
        if (p.name.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QStringList ResizePresetManager::categories() const
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

} // namespace yingtu

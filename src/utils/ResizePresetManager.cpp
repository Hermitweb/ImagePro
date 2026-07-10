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
    add(QStringLiteral("微信头像"), 200, 200, QStringLiteral("社交媒体"), QStringLiteral("1:1"));
    add(QStringLiteral("微信朋友圈"), 1080, 1080, QStringLiteral("社交媒体"), QStringLiteral("1:1"));
    add(QStringLiteral("微信朋友圈封面"), 750, 400, QStringLiteral("社交媒体"), QStringLiteral("15:8"));
    add(QStringLiteral("微信公众号封面"), 900, 383, QStringLiteral("社交媒体"), QStringLiteral("900x383"));
    add(QStringLiteral("微博头像"), 400, 400, QStringLiteral("社交媒体"), QStringLiteral("1:1"));
    add(QStringLiteral("微博封面"), 920, 300, QStringLiteral("社交媒体"), QStringLiteral("46:15"));
    add(QStringLiteral("抖音头像"), 500, 500, QStringLiteral("社交媒体"), QStringLiteral("1:1"));
    add(QStringLiteral("抖音视频封面"), 1080, 1920, QStringLiteral("社交媒体"), QStringLiteral("9:16"));
    add(QStringLiteral("小红书封面"), 1080, 1440, QStringLiteral("社交媒体"), QStringLiteral("3:4"));
    add(QStringLiteral("Instagram头像"), 180, 180, QStringLiteral("社交媒体"), QStringLiteral("1:1"));
    add(QStringLiteral("Instagram帖子"), 1080, 1080, QStringLiteral("社交媒体"), QStringLiteral("1:1"));

    // 证件照
    add(QStringLiteral("一寸"), 295, 413, QStringLiteral("证件照"), QStringLiteral("25x35mm"));
    add(QStringLiteral("二寸"), 413, 579, QStringLiteral("证件照"), QStringLiteral("35x49mm"));
    add(QStringLiteral("小一寸"), 260, 378, QStringLiteral("证件照"), QStringLiteral("22x32mm"));
    add(QStringLiteral("小二寸"), 389, 567, QStringLiteral("证件照"), QStringLiteral("33x48mm"));
    add(QStringLiteral("大一寸"), 339, 567, QStringLiteral("证件照"), QStringLiteral("28x48mm"));
    add(QStringLiteral("大二寸"), 358, 531, QStringLiteral("证件照"), QStringLiteral("30x45mm"));
    add(QStringLiteral("简历照片"), 358, 441, QStringLiteral("证件照"), QStringLiteral("30x37mm"));
    add(QStringLiteral("学生证照片"), 260, 378, QStringLiteral("证件照"), QStringLiteral("22x32mm"));

    // 电商
    add(QStringLiteral("淘宝主图"), 800, 800, QStringLiteral("电商"), QStringLiteral("1:1"));
    add(QStringLiteral("淘宝详情图"), 750, 1000, QStringLiteral("电商"), QStringLiteral("3:4"));
    add(QStringLiteral("京东主图"), 800, 800, QStringLiteral("电商"), QStringLiteral("1:1"));
    add(QStringLiteral("拼多多主图"), 750, 750, QStringLiteral("电商"), QStringLiteral("1:1"));
    add(QStringLiteral("横幅广告"), 728, 90, QStringLiteral("电商"), QStringLiteral("728x90"));
    add(QStringLiteral("竖版海报"), 600, 900, QStringLiteral("电商"), QStringLiteral("2:3"));
    add(QStringLiteral("横版海报"), 900, 600, QStringLiteral("电商"), QStringLiteral("3:2"));

    // 网站/应用
    add(QStringLiteral("网站Logo"), 80, 80, QStringLiteral("网站应用"), QStringLiteral("1:1"));
    add(QStringLiteral("Favicon"), 32, 32, QStringLiteral("网站应用"), QStringLiteral("1:1"));
    add(QStringLiteral("App图标"), 1024, 1024, QStringLiteral("网站应用"), QStringLiteral("1:1"));
    add(QStringLiteral("启动页"), 1080, 1920, QStringLiteral("网站应用"), QStringLiteral("9:16"));

    // 屏幕壁纸
    add(QStringLiteral("1080P"), 1920, 1080, QStringLiteral("屏幕壁纸"), QStringLiteral("16:9"));
    add(QStringLiteral("2K"), 2560, 1440, QStringLiteral("屏幕壁纸"), QStringLiteral("16:9"));
    add(QStringLiteral("4K"), 3840, 2160, QStringLiteral("屏幕壁纸"), QStringLiteral("16:9"));
    add(QStringLiteral("笔记本壁纸"), 1366, 768, QStringLiteral("屏幕壁纸"), QStringLiteral("16:9"));
    add(QStringLiteral("手机壁纸"), 1080, 1920, QStringLiteral("屏幕壁纸"), QStringLiteral("9:16"));

    // 视频
    add(QStringLiteral("视频缩略图"), 1280, 720, QStringLiteral("视频"), QStringLiteral("16:9"));
    add(QStringLiteral("YouTube封面"), 2560, 1440, QStringLiteral("视频"), QStringLiteral("16:9"));
    add(QStringLiteral("B站封面"), 1146, 717, QStringLiteral("视频"), QStringLiteral("1146x717"));

    // 打印
    add(QStringLiteral("A4 (150dpi)"), 1240, 1754, QStringLiteral("打印"), QStringLiteral("A4"));
    add(QStringLiteral("A4 (300dpi)"), 2480, 3508, QStringLiteral("打印"), QStringLiteral("A4"));
    add(QStringLiteral("5寸照片"), 1500, 1050, QStringLiteral("打印"), QStringLiteral("5寸"));
    add(QStringLiteral("6寸照片"), 1800, 1200, QStringLiteral("打印"), QStringLiteral("6寸"));
    add(QStringLiteral("名片"), 1063, 626, QStringLiteral("打印"), QStringLiteral("90x54mm"));

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

#include "ResizePresetManager.h"
#include "AppPaths.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <algorithm>

namespace yingtu {

namespace {

// 预设文件 schema 版本。写入时附带；读取时兼容旧格式（裸数组）。
constexpr int kPresetFileVersion = 1;

QJsonObject resizePresetToJson(const ResizePreset& p)
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = p.id;
    obj[QStringLiteral("name")] = p.name;
    obj[QStringLiteral("width")] = p.width;
    obj[QStringLiteral("height")] = p.height;
    obj[QStringLiteral("category")] = p.category;
    obj[QStringLiteral("isBuiltIn")] = p.isBuiltIn;
    obj[QStringLiteral("description")] = p.description;
    return obj;
}

// 从 JSON 对象还原预设。id 为空视为无效条目。
bool resizePresetFromJson(const QJsonObject& obj, ResizePreset* out)
{
    if (!out)
        return false;
    ResizePreset p;
    p.id = obj.value(QStringLiteral("id")).toString();
    if (p.id.isEmpty())
        return false;
    p.name = obj.value(QStringLiteral("name")).toString();
    p.width = obj.value(QStringLiteral("width")).toInt(0);
    p.height = obj.value(QStringLiteral("height")).toInt(0);
    p.category = obj.value(QStringLiteral("category")).toString(QStringLiteral("Custom"));
    p.isBuiltIn = obj.value(QStringLiteral("isBuiltIn")).toBool(false);
    p.description = obj.value(QStringLiteral("description")).toString();
    *out = p;
    return true;
}

// 解析预设文件内容。兼容两种根节点：
//   旧格式：裸数组 [ {...}, ... ]
//   新格式：{ "version": 1, "presets": [ ... ] }
// parseOk 用于区分「文件不存在/内容损坏」与「合法但为空」。
QList<ResizePreset> parsePresets(const QByteArray& data, bool* parseOk)
{
    QList<ResizePreset> result;
    if (parseOk)
        *parseOk = false;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError)
        return result;

    QJsonArray arr;
    if (doc.isArray()) {
        arr = doc.array();
    } else if (doc.isObject() && doc.object().contains(QStringLiteral("presets"))) {
        // 必须是带 "presets" 键的新格式对象。若只判断 isObject()，任何其它合法
        // JSON 对象（缺少该键）都会被当成「解析成功且内容为空」，后续保存就会
        // 静默覆盖用户预设，而且不会走备份分支。
        arr = doc.object().value(QStringLiteral("presets")).toArray();
    } else {
        return result;
    }

    for (const auto& val : arr) {
        ResizePreset p;
        if (resizePresetFromJson(val.toObject(), &p))
            result.append(p);
    }
    if (parseOk)
        *parseOk = true;
    return result;
}

// 读取已存在的预设文件。文件不存在 → 空列表且 parseOk = true（可安全创建）；
// 文件存在但解析失败 → parseOk = false，调用方应先备份再覆盖。
QList<ResizePreset> readExistingPresets(const QString& path, bool* parseOk)
{
    QList<ResizePreset> result;
    if (parseOk)
        *parseOk = true;

    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return result;

    bool ok = false;
    result = parsePresets(file.readAll(), &ok);
    file.close();
    if (!ok && parseOk)
        *parseOk = false;
    return result;
}

// 原子写入：QSaveFile 先写临时文件再重命名，避免写失败（磁盘满/权限不足）时
// 把原有预设文件截断成损坏内容。原实现忽略 write() 返回值且无条件返回 true，
// 会造成用户预设永久丢失且无任何报错。
bool writePresetsAtomically(const QString& path, const QList<ResizePreset>& presets)
{
    QJsonArray arr;
    for (const ResizePreset& p : presets)
        arr.append(resizePresetToJson(p));

    QJsonObject root;
    root[QStringLiteral("version")] = kPresetFileVersion;
    root[QStringLiteral("presets")] = arr;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

// 文件存在但内容无法解析时，先留一份 .corrupt.bak，避免用户自定义预设
// 在下一次保存时被静默覆盖掉。
void backupCorruptPresetFile(const QString& path)
{
    if (!QFile::exists(path))
        return;
    const QString backup = path + QStringLiteral(".corrupt.bak");
    QFile::remove(backup);
    QFile::copy(path, backup);
}

} // namespace

ResizePresetManager& ResizePresetManager::instance()
{
    static ResizePresetManager mgr;
    return mgr;
}

QString ResizePresetManager::presetsFilePath() const
{
    QString dir = appConfigDir();
    QDir().mkpath(dir);
    return dir + QStringLiteral("/resize_presets.json");
}

QList<ResizePreset> ResizePresetManager::builtInPresets() const
{
    QList<ResizePreset> result;
    // id 必须稳定：之前用 QUuid 每次调用都生成新 id，导致内置预设的 id 在
    // 每次 loadPresets() 后都变化（按 id 定位失败、保存会追加重复项）。
    // 这里由名称派生，跨运行稳定且唯一。
    auto add = [&result](const QString& name, int w, int h, const QString& category,
                         const QString& desc) {
        ResizePreset p;
        p.id = QStringLiteral("builtin-resize-") + name;
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
    add(QStringLiteral("微信朋友圈"), 1080, 1080, QStringLiteral("社交媒体"),
        QStringLiteral("1:1"));
    add(QStringLiteral("微信朋友圈封面"), 750, 400, QStringLiteral("社交媒体"),
        QStringLiteral("15:8"));
    add(QStringLiteral("微信公众号封面"), 900, 383, QStringLiteral("社交媒体"),
        QStringLiteral("900x383"));
    add(QStringLiteral("微博头像"), 400, 400, QStringLiteral("社交媒体"), QStringLiteral("1:1"));
    add(QStringLiteral("微博封面"), 920, 300, QStringLiteral("社交媒体"), QStringLiteral("46:15"));
    add(QStringLiteral("抖音头像"), 500, 500, QStringLiteral("社交媒体"), QStringLiteral("1:1"));
    add(QStringLiteral("抖音视频封面"), 1080, 1920, QStringLiteral("社交媒体"),
        QStringLiteral("9:16"));
    add(QStringLiteral("小红书封面"), 1080, 1440, QStringLiteral("社交媒体"),
        QStringLiteral("3:4"));
    add(QStringLiteral("Instagram头像"), 180, 180, QStringLiteral("社交媒体"),
        QStringLiteral("1:1"));
    add(QStringLiteral("Instagram帖子"), 1080, 1080, QStringLiteral("社交媒体"),
        QStringLiteral("1:1"));

    // 证件照
    add(QStringLiteral("一寸"), 295, 413, QStringLiteral("证件照"), QStringLiteral("25x35mm"));
    add(QStringLiteral("二寸"), 413, 579, QStringLiteral("证件照"), QStringLiteral("35x49mm"));
    add(QStringLiteral("小一寸"), 260, 378, QStringLiteral("证件照"), QStringLiteral("22x32mm"));
    add(QStringLiteral("小二寸"), 389, 567, QStringLiteral("证件照"), QStringLiteral("33x48mm"));
    add(QStringLiteral("大一寸"), 339, 567, QStringLiteral("证件照"), QStringLiteral("28x48mm"));
    add(QStringLiteral("大二寸"), 358, 531, QStringLiteral("证件照"), QStringLiteral("30x45mm"));
    add(QStringLiteral("简历照片"), 358, 441, QStringLiteral("证件照"), QStringLiteral("30x37mm"));
    add(QStringLiteral("学生证照片"), 260, 378, QStringLiteral("证件照"),
        QStringLiteral("22x32mm"));

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
    add(QStringLiteral("笔记本壁纸"), 1366, 768, QStringLiteral("屏幕壁纸"),
        QStringLiteral("16:9"));
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

    bool parseOk = false;
    const QList<ResizePreset> stored = readExistingPresets(presetsFilePath(), &parseOk);
    if (!parseOk)
        return result; // 文件损坏：只返回内置项，且不触碰原文件

    for (const ResizePreset& p : stored) {
        if (p.isValid())
            result.append(p);
    }
    return result;
}

bool ResizePresetManager::savePreset(const ResizePreset& preset)
{
    // 与 StitchPresetManager 保持一致：无效预设会被 loadPresets() 的 isValid()
    // 过滤掉，保存返回成功却读不回来，对用户表现为「预设静默消失」。
    if (!preset.isValid())
        return false;

    const QString path = presetsFilePath();

    bool parseOk = true;
    QList<ResizePreset> presets = readExistingPresets(path, &parseOk);
    if (!parseOk)
        backupCorruptPresetFile(path);

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

    return writePresetsAtomically(path, presets);
}

bool ResizePresetManager::deletePreset(const QString& id)
{
    const QString path = presetsFilePath();

    bool parseOk = true;
    QList<ResizePreset> presets = readExistingPresets(path, &parseOk);
    if (!parseOk)
        backupCorruptPresetFile(path);

    for (int i = 0; i < presets.size(); ++i) {
        if (presets.at(i).id == id) {
            presets.removeAt(i);
            break;
        }
    }

    return writePresetsAtomically(path, presets);
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

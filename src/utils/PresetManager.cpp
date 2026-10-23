#include "PresetManager.h"
#include "AppPaths.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace yingtu {

namespace {

// 预设文件 schema 版本。写入时附带；读取时兼容旧格式（裸数组）。
constexpr int kPresetFileVersion = 1;

QJsonObject gridPresetToJson(const GridPreset& p)
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = p.id;
    obj[QStringLiteral("name")] = p.name;
    obj[QStringLiteral("category")] = p.category;
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
    return obj;
}

// 从 JSON 对象还原预设。id 为空视为无效条目。
bool gridPresetFromJson(const QJsonObject& obj, GridPreset* out)
{
    if (!out)
        return false;
    GridPreset p;
    p.id = obj.value(QStringLiteral("id")).toString();
    if (p.id.isEmpty())
        return false;
    p.name = obj.value(QStringLiteral("name")).toString();
    // category 之前既没写入也没读取，导致用户预设的类别在重启后静默回退为默认值。
    p.category = obj.value(QStringLiteral("category")).toString(QStringLiteral("Custom"));
    p.rows = obj.value(QStringLiteral("rows")).toInt(1);
    p.columns = obj.value(QStringLiteral("columns")).toInt(1);
    p.cellWidth = obj.value(QStringLiteral("cellWidth")).toInt(0);
    p.cellHeight = obj.value(QStringLiteral("cellHeight")).toInt(0);
    p.lockAspectRatio = obj.value(QStringLiteral("lockAspectRatio")).toBool(true);
    p.spacing = obj.value(QStringLiteral("spacing")).toInt(0);
    p.background = obj.value(QStringLiteral("background")).toString(QStringLiteral("transparent"));
    p.bgColor = obj.value(QStringLiteral("bgColor")).toString(QStringLiteral("#FFFFFF"));
    const QJsonArray tagArr = obj.value(QStringLiteral("tags")).toArray();
    for (const QJsonValue& t : tagArr)
        p.tags.append(t.toString());
    p.isBuiltIn = obj.value(QStringLiteral("isBuiltIn")).toBool(false);
    *out = p;
    return true;
}

// 解析预设文件内容。兼容两种根节点：
//   旧格式：裸数组 [ {...}, ... ]
//   新格式：{ "version": 1, "presets": [ ... ] }
// parseOk 用于区分「文件不存在/内容损坏」与「合法但为空」。
QList<GridPreset> parsePresets(const QByteArray& data, bool* parseOk)
{
    QList<GridPreset> result;
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
        GridPreset p;
        if (gridPresetFromJson(val.toObject(), &p))
            result.append(p);
    }
    if (parseOk)
        *parseOk = true;
    return result;
}

// 读取已存在的预设文件。文件不存在 → 空列表且 parseOk = true（可安全创建）；
// 文件存在但解析失败 → parseOk = false，调用方应先备份再覆盖。
QList<GridPreset> readExistingPresets(const QString& path, bool* parseOk)
{
    QList<GridPreset> result;
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
bool writePresetsAtomically(const QString& path, const QList<GridPreset>& presets)
{
    QJsonArray arr;
    for (const GridPreset& p : presets)
        arr.append(gridPresetToJson(p));

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

// 文件存在但内容无法解析时，先留一份 .corrupt.bak。
// 否则下一次保存会直接用内置项覆盖，用户自定义预设就彻底没了。
void backupCorruptPresetFile(const QString& path)
{
    if (!QFile::exists(path))
        return;
    const QString backup = path + QStringLiteral(".corrupt.bak");
    QFile::remove(backup);
    QFile::copy(path, backup);
}

} // namespace

PresetManager& PresetManager::instance()
{
    static PresetManager mgr;
    return mgr;
}

QString PresetManager::presetsFilePath() const
{
    QString dir = appConfigDir();
    QDir().mkpath(dir);
    return dir + QStringLiteral("/grid_presets.json");
}

QList<GridPreset> PresetManager::builtInPresets() const
{
    QList<GridPreset> result;
    // id 必须稳定：之前用 QUuid 每次调用都生成新 id，导致按 id 定位预设失败、
    // 保存内置预设会追加重复项、deletePreset(id) 永远匹配不上内置项。
    auto add = [&result](const QString& id, const QString& name, int rows, int columns,
                         const QString& category) {
        GridPreset p;
        p.id = id;
        p.name = name;
        p.rows = rows;
        p.columns = columns;
        p.category = category;
        p.isBuiltIn = true;
        result.append(p);
    };

    add(QStringLiteral("builtin-custom"), QStringLiteral("Custom"), 1, 1, QStringLiteral("Custom"));
    add(QStringLiteral("builtin-2x2"), QStringLiteral("2 x 2"), 2, 2, QStringLiteral("Social"));
    add(QStringLiteral("builtin-3x3"), QStringLiteral("3 x 3"), 3, 3, QStringLiteral("Social"));
    add(QStringLiteral("builtin-2x3"), QStringLiteral("2 x 3"), 2, 3, QStringLiteral("Social"));
    add(QStringLiteral("builtin-3x4"), QStringLiteral("3 x 4"), 3, 4, QStringLiteral("Poster"));
    add(QStringLiteral("builtin-4x4"), QStringLiteral("4 x 4"), 4, 4, QStringLiteral("Poster"));

    return result;
}

QList<GridPreset> PresetManager::loadPresets() const
{
    QList<GridPreset> result = builtInPresets();

    bool parseOk = false;
    const QList<GridPreset> stored = readExistingPresets(presetsFilePath(), &parseOk);
    if (!parseOk)
        return result; // 文件损坏：只返回内置项，且不触碰原文件

    for (const GridPreset& p : stored) {
        if (p.isValid())
            result.append(p);
    }
    return result;
}

bool PresetManager::savePreset(const GridPreset& preset)
{
    // 与 StitchPresetManager 保持一致：无效预设会被 loadPresets() 的 isValid()
    // 过滤掉，保存返回成功却读不回来，对用户表现为「预设静默消失」。
    if (!preset.isValid())
        return false;

    const QString path = presetsFilePath();

    bool parseOk = true;
    QList<GridPreset> presets = readExistingPresets(path, &parseOk);
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

bool PresetManager::deletePreset(const QString& id)
{
    const QString path = presetsFilePath();

    bool parseOk = true;
    QList<GridPreset> presets = readExistingPresets(path, &parseOk);
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

bool PresetManager::presetExists(const QString& name) const
{
    for (const auto& p : loadPresets()) {
        if (p.name.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

} // namespace yingtu

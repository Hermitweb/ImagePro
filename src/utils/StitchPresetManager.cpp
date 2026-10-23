#include "StitchPresetManager.h"
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

QJsonObject stitchPresetToJson(const StitchPreset& p)
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = p.id;
    obj[QStringLiteral("name")] = p.name;
    obj[QStringLiteral("rows")] = p.rows;
    obj[QStringLiteral("columns")] = p.columns;
    obj[QStringLiteral("category")] = p.category;
    obj[QStringLiteral("isBuiltIn")] = p.isBuiltIn;
    obj[QStringLiteral("description")] = p.description;
    obj[QStringLiteral("direction")] = p.direction;
    return obj;
}

// 从 JSON 对象还原预设。id 为空视为无效条目。
bool stitchPresetFromJson(const QJsonObject& obj, StitchPreset* out)
{
    if (!out)
        return false;
    StitchPreset p;
    p.id = obj.value(QStringLiteral("id")).toString();
    if (p.id.isEmpty())
        return false;
    p.name = obj.value(QStringLiteral("name")).toString();
    p.rows = obj.value(QStringLiteral("rows")).toInt(1);
    p.columns = obj.value(QStringLiteral("columns")).toInt(1);
    p.category = obj.value(QStringLiteral("category")).toString(QStringLiteral("Custom"));
    p.isBuiltIn = obj.value(QStringLiteral("isBuiltIn")).toBool(false);
    p.description = obj.value(QStringLiteral("description")).toString();
    p.direction = obj.value(QStringLiteral("direction")).toInt(2);
    *out = p;
    return true;
}

// 解析预设文件内容。兼容两种根节点：
//   旧格式：裸数组 [ {...}, ... ]
//   新格式：{ "version": 1, "presets": [ ... ] }
// parseOk 用于区分「文件不存在/内容损坏」与「合法但为空」。
QList<StitchPreset> parsePresets(const QByteArray& data, bool* parseOk)
{
    QList<StitchPreset> result;
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
        StitchPreset p;
        if (stitchPresetFromJson(val.toObject(), &p))
            result.append(p);
    }
    if (parseOk)
        *parseOk = true;
    return result;
}

// 读取已存在的预设文件。文件不存在 → 空列表且 parseOk = true（可安全创建）；
// 文件存在但解析失败 → parseOk = false，调用方应先备份再覆盖。
QList<StitchPreset> readExistingPresets(const QString& path, bool* parseOk)
{
    QList<StitchPreset> result;
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
bool writePresetsAtomically(const QString& path, const QList<StitchPreset>& presets)
{
    QJsonArray arr;
    for (const StitchPreset& p : presets)
        arr.append(stitchPresetToJson(p));

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

StitchPresetManager& StitchPresetManager::instance()
{
    static StitchPresetManager mgr;
    return mgr;
}

QString StitchPresetManager::presetsFilePath() const
{
    QString dir = appConfigDir();
    QDir().mkpath(dir);
    return dir + QStringLiteral("/stitch_presets.json");
}

QList<StitchPreset> StitchPresetManager::loadPresets() const
{
    QList<StitchPreset> result = builtInPresets();

    bool parseOk = false;
    const QList<StitchPreset> stored = readExistingPresets(presetsFilePath(), &parseOk);
    if (!parseOk)
        return result; // 文件损坏：只返回内置项，且不触碰原文件

    for (const StitchPreset& p : stored) {
        if (p.isValid())
            result.append(p);
    }
    return result;
}

bool StitchPresetManager::savePreset(const StitchPreset& preset)
{
    // rows/columns 非正的预设会被 loadPresets 过滤掉，保存成功却读不回来，
    // 对用户表现为「预设静默消失」。这里直接拒绝。
    if (!preset.isValid())
        return false;

    const QString path = presetsFilePath();

    bool parseOk = true;
    QList<StitchPreset> presets = readExistingPresets(path, &parseOk);
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

bool StitchPresetManager::deletePreset(const QString& id)
{
    const QString path = presetsFilePath();

    bool parseOk = true;
    QList<StitchPreset> presets = readExistingPresets(path, &parseOk);
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
    // id 必须稳定：之前用 QUuid 每次调用都生成新 id，导致按 id 定位预设失败、
    // 保存内置预设会追加重复项、deletePreset(id) 永远匹配不上内置项。
    // 这里由名称派生，跨运行稳定且唯一。
    // direction 与 StitchSettings::Direction 对应：
    //   0=Vertical, 1=Horizontal, 2=Grid（默认）。
    // Grid 预设用 rows/columns 描述网格；Horizontal/Vertical 忽略 rows/columns。
    auto add = [&result](const QString& name, int rows, int columns, const QString& category,
                         const QString& desc, int direction = 2) {
        StitchPreset p;
        p.id = QStringLiteral("builtin-stitch-") + name;
        p.name = name;
        p.rows = rows;
        p.columns = columns;
        p.category = category;
        p.isBuiltIn = true;
        p.description = desc;
        p.direction = direction;
        result.append(p);
    };

    // 常用
    add(QStringLiteral("Custom"), 1, 1, QStringLiteral("常用"),
        QStringLiteral("Use current rows/columns"));
    // 「1 x N」表示单行排布（N 张图排成一行），不是 1×1 网格。
    // 此前数据是 rows=1/columns=1 且 direction=Grid，套用后只输出 1 格，与名称和
    // 描述（"Single row"）完全不符。这里改为 Horizontal 方向：applyStitchPreset 对
    // 非 Grid 方向会忽略 rows/columns，从而把所选图片排成一行。
    add(QStringLiteral("1 x N"), 1, 1, QStringLiteral("常用"), QStringLiteral("Single row"), 1);

    // 经典布局
    add(QStringLiteral("九宫格"), 3, 3, QStringLiteral("经典布局"), QStringLiteral("9 grids"));
    add(QStringLiteral("四宫格"), 2, 2, QStringLiteral("经典布局"), QStringLiteral("4 grids"));
    add(QStringLiteral("六宫格(2x3)"), 2, 3, QStringLiteral("经典布局"), QStringLiteral("6 grids"));
    add(QStringLiteral("六宫格(3x2)"), 3, 2, QStringLiteral("经典布局"), QStringLiteral("6 grids"));
    add(QStringLiteral("十二宫格"), 3, 4, QStringLiteral("经典布局"), QStringLiteral("12 grids"));
    add(QStringLiteral("十六宫格"), 4, 4, QStringLiteral("经典布局"), QStringLiteral("16 grids"));

    // 社交分享
    add(QStringLiteral("朋友圈九宫格"), 3, 3, QStringLiteral("社交分享"),
        QStringLiteral("WeChat 9 grids"));
    add(QStringLiteral("朋友圈4张"), 2, 2, QStringLiteral("社交分享"),
        QStringLiteral("WeChat 4 grids"));
    add(QStringLiteral("朋友圈6张"), 2, 3, QStringLiteral("社交分享"),
        QStringLiteral("WeChat 6 grids"));
    add(QStringLiteral("小红书封面"), 3, 2, QStringLiteral("社交分享"),
        QStringLiteral("Xiaohongshu 3x2"));
    add(QStringLiteral("Instagram拼图"), 3, 3, QStringLiteral("社交分享"),
        QStringLiteral("Instagram 9 grids"));

    // 证件照片
    add(QStringLiteral("证件照排版(1寸6张)"), 2, 3, QStringLiteral("证件照片"),
        QStringLiteral("1寸 6张"));
    add(QStringLiteral("证件照排版(1寸8张)"), 2, 4, QStringLiteral("证件照片"),
        QStringLiteral("1寸 8张"));
    add(QStringLiteral("证件照排版(2寸4张)"), 2, 2, QStringLiteral("证件照片"),
        QStringLiteral("2寸 4张"));
    add(QStringLiteral("证件照排版(2寸6张)"), 2, 3, QStringLiteral("证件照片"),
        QStringLiteral("2寸 6张"));

    // 相册排版
    add(QStringLiteral("相册横排"), 1, 3, QStringLiteral("相册排版"),
        QStringLiteral("1x3 horizontal"));
    add(QStringLiteral("相册竖排"), 3, 1, QStringLiteral("相册排版"),
        QStringLiteral("3x1 vertical"));
    add(QStringLiteral("相册双排"), 2, 3, QStringLiteral("相册排版"), QStringLiteral("2x3 album"));
    add(QStringLiteral("相册三排"), 3, 3, QStringLiteral("相册排版"), QStringLiteral("3x3 album"));

    // 海报设计
    add(QStringLiteral("海报拼贴(4格)"), 2, 2, QStringLiteral("海报设计"),
        QStringLiteral("Poster 4 grids"));
    add(QStringLiteral("海报拼贴(6格)"), 3, 2, QStringLiteral("海报设计"),
        QStringLiteral("Poster 6 grids"));
    add(QStringLiteral("海报拼贴(8格)"), 2, 4, QStringLiteral("海报设计"),
        QStringLiteral("Poster 8 grids"));
    add(QStringLiteral("海报拼贴(9格)"), 3, 3, QStringLiteral("海报设计"),
        QStringLiteral("Poster 9 grids"));

    return result;
}

} // namespace yingtu

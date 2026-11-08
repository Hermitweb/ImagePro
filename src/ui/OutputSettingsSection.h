#pragma once

#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace yingtu {

/**
 * 统一的「输出设置」内容区：输出目录（可编辑 + 历史 + 浏览 + 新建），
 * 可选的文件名模板行与实时预览、可选的校验标签。
 *
 * 之前只有拼接面板配了完整输出设置，其余工具的输出位置藏在最后一步的
 * 文件对话框里 —— 用户每换一个工具都要重新理解一次。现在八个工具面板
 * 共用这段式样，位置、行为、文案完全一致，学一次就会用。
 *
 * 历史列表由 PropertyPanel 统一维护（跨工具共享最近 5 个目录），
 * 本控件只负责展示与编辑。
 */
class OutputSettingsSection : public QWidget
{
    Q_OBJECT
public:
    /**
     * @param withFileNameTemplate 拼接工具需要文件名模板与实时预览；
     *                             其余工具只配目录，传 false。
     */
    explicit OutputSettingsSection(bool withFileNameTemplate, QWidget* parent = nullptr);

    QString directory() const;
    void setDirectory(const QString& dir);

    void setHistory(const QStringList& paths);

    QString fileNameTemplate() const;
    void setFileNameTemplate(const QString& templ);

    /** 模板预览（灰色小字）。空串时隐藏。 */
    void setTemplatePreview(const QString& text);
    /** 校验提示（带 🔴⚠️✅ 前缀的文本由调用方拼装）。空串时隐藏。 */
    void setValidationText(const QString& text, bool isError, bool isWarning);

    /** 目录输入框。拼接校验逻辑（setInputError 描边）直接作用于它。 */
    QComboBox* dirCombo() const { return m_dir; }
    QLineEdit* fileNameTemplateEdit() const { return m_template; }

    /** 目录是否存在且可写（供各面板校验共用）。 */
    static bool canWriteToPath(const QString& path);

signals:
    /** 用户编辑或选择了目录（程序化 setDirectory/setHistory 不触发）。 */
    void directoryEdited();
    /**
     * 目录被「提交」：浏览选中、新建成功或编辑完成且路径存在可写。
     * PropertyPanel 在此把路径记入跨工具共享的历史。
     */
    void directoryCommitted();
    /** 用户编辑了文件名模板（未启用模板行时不触发）。 */
    void fileNameTemplateEdited();

private:
    QWidget* buildDirRow(QWidget* parent);
    QWidget* buildFormRow(const QString& label, QWidget* widget);
    void updateCreateButton();

    QComboBox* m_dir = nullptr;
    QPushButton* m_browseBtn = nullptr;
    QPushButton* m_createBtn = nullptr;
    QLineEdit* m_template = nullptr;
    QLabel* m_templatePreview = nullptr;
    QLabel* m_validation = nullptr;
    bool m_updating = false;
};

} // namespace yingtu

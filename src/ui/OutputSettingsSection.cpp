#include "OutputSettingsSection.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace yingtu {

OutputSettingsSection::OutputSettingsSection(bool withFileNameTemplate, QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    layout->addWidget(buildDirRow(this));

    if (withFileNameTemplate) {
        m_template = new QLineEdit(this);
        m_template->setPlaceholderText(tr("e.g. {name}_{index}"));
        connect(m_template, &QLineEdit::textChanged, this,
                [this]() { emit fileNameTemplateEdited(); });
        layout->addWidget(buildFormRow(tr("File Name:"), m_template));

        m_templatePreview = new QLabel(this);
        m_templatePreview->setStyleSheet(QStringLiteral("color: gray; font-size: 12px;"));
        // 预览的文件名通常是一整段没有空格的路径（如
        // wechat_screenshot_001_stitched_20261023.png）。QLabel 即使开了 wordWrap，
        // 其 minimumSizeHint 仍按「最长的一个词」计算 —— 也就是整个文件名，
        // 于是这个标签会把面板撑得远宽于 200~260px，内容被裁掉且无法看到。
        // 用 Ignored 策略允许它被压缩到视口宽度，配合 wordWrap 正常折行。
        m_templatePreview->setMinimumWidth(0);
        m_templatePreview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(m_templatePreview);
    }

    m_validation = new QLabel(this);
    m_validation->setWordWrap(true);
    m_validation->setMinimumWidth(0);
    m_validation->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_validation->setVisible(false);
    layout->addWidget(m_validation);
}

QWidget* OutputSettingsSection::buildFormRow(const QString& label, QWidget* widget)
{
    QWidget* row = new QWidget(this);
    QHBoxLayout* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    l->addWidget(new QLabel(label, row));
    l->addWidget(widget, 1);
    return row;
}

QWidget* OutputSettingsSection::buildDirRow(QWidget* parent)
{
    QWidget* dirRow = new QWidget(parent);
    QHBoxLayout* dirLayout = new QHBoxLayout(dirRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    dirLayout->setSpacing(6);
    dirLayout->addWidget(new QLabel(tr("Output Dir:"), dirRow));

    m_dir = new QComboBox(dirRow);
    m_dir->setEditable(true);
    m_dir->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_dir, &QComboBox::editTextChanged, this, [this]() {
        if (m_updating)
            return;
        updateCreateButton();
        emit directoryEdited();
    });
    // 编辑完成且路径有效时视为「提交」，由 PropertyPanel 记入历史。
    if (m_dir->lineEdit()) {
        connect(m_dir->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
            const QString path = m_dir->currentText();
            if (!path.isEmpty() && QFileInfo::exists(path) && canWriteToPath(path))
                emit directoryCommitted();
        });
    }
    dirLayout->addWidget(m_dir, 1);

    m_browseBtn = new QPushButton(tr("Browse..."), dirRow);
    connect(m_browseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Output Directory"),
                                                        m_dir->currentText());
        if (dir.isEmpty())
            return;
        m_dir->setCurrentText(dir);
        updateCreateButton();
        emit directoryEdited();
        emit directoryCommitted();
    });
    dirLayout->addWidget(m_browseBtn);

    // 目录不存在时才出现「创建」按钮：避免用户输了一个还不存在的路径后
    // 在校验标签上看到红色错误却不知道怎么修。
    m_createBtn = new QPushButton(tr("Create"), dirRow);
    connect(m_createBtn, &QPushButton::clicked, this, [this]() {
        const QString path = m_dir->currentText();
        if (path.isEmpty())
            return;
        if (!QDir().mkpath(path)) {
            QMessageBox::warning(this, tr("Error"), tr("Failed to create output directory."));
            return;
        }
        updateCreateButton();
        emit directoryEdited();
        emit directoryCommitted();
    });
    m_createBtn->setVisible(false);
    dirLayout->addWidget(m_createBtn);

    return dirRow;
}

QString OutputSettingsSection::directory() const
{
    return m_dir ? m_dir->currentText() : QString();
}

void OutputSettingsSection::setDirectory(const QString& dir)
{
    if (!m_dir || m_dir->currentText() == dir)
        return;
    // 程序化写入必须静默：combo 的 currentIndexChanged 已被 PropertyPanel
    // 统一接到 onSettingsChanged，不屏蔽的话一次历史同步会触发一串预览请求。
    {
        const QSignalBlocker blocker(m_dir);
        m_dir->setCurrentText(dir);
    }
    m_updating = false;
    updateCreateButton();
}

void OutputSettingsSection::setHistory(const QStringList& paths)
{
    if (!m_dir)
        return;
    m_updating = true;
    {
        const QSignalBlocker blocker(m_dir);
        const QString current = m_dir->currentText();
        m_dir->clear();
        m_dir->addItems(paths);
        m_dir->setCurrentText(current);
    }
    m_updating = false;
    updateCreateButton();
}

QString OutputSettingsSection::fileNameTemplate() const
{
    return m_template ? m_template->text() : QString();
}

void OutputSettingsSection::setFileNameTemplate(const QString& templ)
{
    if (!m_template || m_template->text() == templ)
        return;
    m_updating = true;
    m_template->setText(templ);
    m_updating = false;
}

void OutputSettingsSection::setTemplatePreview(const QString& text)
{
    if (!m_templatePreview)
        return;
    if (text.isEmpty()) {
        m_templatePreview->setVisible(false);
        return;
    }
    const int available = m_templatePreview->width() > 0 ? m_templatePreview->width() : 200;
    m_templatePreview->setText(
        m_templatePreview->fontMetrics().elidedText(text, Qt::ElideMiddle, available));
    m_templatePreview->setToolTip(text);
    m_templatePreview->setVisible(true);
}

void OutputSettingsSection::setValidationText(const QString& text, bool isError, bool isWarning)
{
    if (!m_validation)
        return;
    if (text.isEmpty()) {
        m_validation->setVisible(false);
        return;
    }
    const QColor color = isError
                             ? QColor(0xE8, 0x11, 0x23)
                             : (isWarning ? QColor(0xFF, 0xC1, 0x07) : QColor(0x10, 0x7C, 0x10));
    m_validation->setText(text);
    m_validation->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(color.name()));
    m_validation->setVisible(true);
}

void OutputSettingsSection::updateCreateButton()
{
    if (!m_dir || !m_createBtn)
        return;
    const QString path = m_dir->currentText();
    m_createBtn->setVisible(!path.isEmpty() && !QFileInfo::exists(path));
}

bool OutputSettingsSection::canWriteToPath(const QString& path)
{
    if (path.isEmpty())
        return false;
    QFileInfo info(path);
    if (!info.exists()) {
        QDir parentDir = info.dir();
        if (!parentDir.exists())
            return false;
        return QFileInfo(parentDir.absolutePath()).isWritable();
    }
    return info.isDir() && info.isWritable();
}

} // namespace yingtu

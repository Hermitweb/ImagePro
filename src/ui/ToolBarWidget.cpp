#include "ToolBarWidget.h"
#include "app/ImageProApp.h"
#include <QButtonGroup>
#include <QCoreApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QToolButton>
#include <QVariant>

namespace yingtu {

static QIcon createTextIcon(const QString& text, const QColor& color = QColor(64, 158, 255))
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(color);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 12, QFont::Bold));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, text);
    return QIcon(pixmap);
}

/**
 * 工具按钮的图标字形。
 *
 * 中文界面用汉字（拼/转/压/水/编/尺/批），英文界面用首字母。
 * 字形本身是「图标」而不是正文，所以单独用一组 "Icon: xxx" 翻译键，
 * 源语言仍统一为英文。
 */
QString ToolBarWidget::toolGlyph(ToolType tool) const
{
    switch (tool) {
    case ToolType::Stitch:
        return tr("Icon: Stitch");
    case ToolType::Convert:
        return tr("Icon: Convert");
    case ToolType::Compress:
        return tr("Icon: Compress");
    case ToolType::Watermark:
        return tr("Icon: Watermark");
    case ToolType::Edit:
        return tr("Icon: Edit");
    case ToolType::Resize:
        return tr("Icon: Resize");
    case ToolType::Batch:
        return tr("Icon: Batch");
    case ToolType::Pdf:
        return tr("Icon: PDF");
    default:
        return QString();
    }
}

QIcon ToolBarWidget::toolIcon(ToolType tool) const
{
    const QString glyph = toolGlyph(tool);
    if (glyph.isEmpty())
        return QIcon();
    return createTextIcon(glyph);
}

ToolBarWidget::ToolBarWidget(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(48);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(6);

    m_addButton = new QToolButton(this);
    m_addButton->setText(tr("Add Images"));
    m_addButton->setToolTip(tr("Add Images"));
    m_addButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    m_addButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_addButton, &QToolButton::clicked, this, &ToolBarWidget::addImagesClicked);
    layout->addWidget(m_addButton);

    m_removeButton = new QToolButton(this);
    m_removeButton->setText(tr("Remove"));
    m_removeButton->setToolTip(tr("Remove"));
    m_removeButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    connect(m_removeButton, &QToolButton::clicked, this, &ToolBarWidget::removeImageClicked);
    layout->addWidget(m_removeButton);

    m_clearButton = new QToolButton(this);
    m_clearButton->setText(tr("Clear"));
    m_clearButton->setToolTip(tr("Clear"));
    m_clearButton->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
    connect(m_clearButton, &QToolButton::clicked, this, &ToolBarWidget::clearImagesClicked);
    layout->addWidget(m_clearButton);

    layout->addSpacing(20);

    createButton(ToolType::Stitch, tr("Stitch"), QStringLiteral("F1"));
    createButton(ToolType::Convert, tr("Convert"), QStringLiteral("F2"));
    createButton(ToolType::Compress, tr("Compress"), QStringLiteral("F3"));
    createButton(ToolType::Watermark, tr("Watermark"), QStringLiteral("F4"));
    createButton(ToolType::Edit, tr("Edit"), QStringLiteral("F5"));
    createButton(ToolType::Resize, tr("Resize"), QStringLiteral("F6"));
    createButton(ToolType::Batch, tr("Batch"), QStringLiteral("F7"));
    createButton(ToolType::Pdf, tr("PDF"), QStringLiteral("F8"));

    layout->addStretch();
    setLayout(layout);

    setCurrentTool(ToolType::Stitch);
}

void ToolBarWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && !ImageProApp::isLanguageSwitching())
        retranslateUi();
}

void ToolBarWidget::retranslateUi()
{
    m_addButton->setText(tr("Add Images"));
    m_addButton->setToolTip(tr("Add Images"));
    m_removeButton->setText(tr("Remove"));
    m_removeButton->setToolTip(tr("Remove"));
    m_clearButton->setText(tr("Clear"));
    m_clearButton->setToolTip(tr("Clear"));

    for (auto it = m_toolButtons.constBegin(); it != m_toolButtons.constEnd(); ++it) {
        const ToolType tool = static_cast<ToolType>(it.key());
        QToolButton* btn = it.value();
        btn->setText(translateSource(toolSourceText(tool)) + QStringLiteral(" (") +
                     btn->property("shortcut").toString() + QStringLiteral(")"));
        btn->setToolTip(btn->text());
        btn->setIcon(toolIcon(tool));
    }
}

QString ToolBarWidget::translateSource(const QString& source) const
{
    const QByteArray utf8 = source.toUtf8();
    return QCoreApplication::translate(metaObject()->className(), utf8.constData());
}

QString ToolBarWidget::toolSourceText(ToolType tool)
{
    switch (tool) {
    case ToolType::Stitch:
        return QStringLiteral("Stitch");
    case ToolType::Convert:
        return QStringLiteral("Convert");
    case ToolType::Compress:
        return QStringLiteral("Compress");
    case ToolType::Watermark:
        return QStringLiteral("Watermark");
    case ToolType::Edit:
        return QStringLiteral("Edit");
    case ToolType::Resize:
        return QStringLiteral("Resize");
    case ToolType::Batch:
        return QStringLiteral("Batch");
    case ToolType::Pdf:
        return QStringLiteral("PDF");
    default:
        return QString();
    }
}

void ToolBarWidget::createButton(ToolType tool, const QString& translatedText,
                                 const QString& shortcut)
{
    QToolButton* btn = new QToolButton(this);
    // 文本由调用方用 tr() 字面量传入（lupdate 才能收录）；
    // 语言切换时按 toolSourceText(tool) 取英文源串重新翻译。
    btn->setText(translatedText + QStringLiteral(" (") + shortcut + QStringLiteral(")"));
    btn->setToolTip(btn->text());
    btn->setIcon(toolIcon(tool));
    btn->setCheckable(true);
    btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btn->setProperty("toolType", QVariant::fromValue(tool));
    btn->setProperty("shortcut", shortcut);
    m_group->addButton(btn);
    m_toolButtons.insert(static_cast<int>(tool), btn);
    layout()->addWidget(btn);

    connect(btn, &QToolButton::toggled, this, [this, tool](bool checked) {
        if (checked)
            emit toolChanged(tool);
    });
}

ToolType ToolBarWidget::currentTool() const
{
    QAbstractButton* checked = m_group->checkedButton();
    if (!checked)
        return ToolType::None;
    return checked->property("toolType").value<ToolType>();
}

void ToolBarWidget::setCurrentTool(ToolType tool)
{
    for (QAbstractButton* btn : m_group->buttons()) {
        if (btn->property("toolType").value<ToolType>() == tool) {
            btn->setChecked(true);
            return;
        }
    }
}

} // namespace yingtu

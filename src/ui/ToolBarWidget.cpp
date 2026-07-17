#include "ToolBarWidget.h"
#include <QButtonGroup>
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

static QIcon toolIcon(ToolType tool)
{
    switch (tool) {
    case ToolType::Stitch:
        return createTextIcon(QStringLiteral("拼"));
    case ToolType::Convert:
        return createTextIcon(QStringLiteral("转"));
    case ToolType::Compress:
        return createTextIcon(QStringLiteral("压"));
    case ToolType::Watermark:
        return createTextIcon(QStringLiteral("水"));
    case ToolType::Edit:
        return createTextIcon(QStringLiteral("编"));
    case ToolType::Resize:
        return createTextIcon(QStringLiteral("尺"));
    case ToolType::Batch:
        return createTextIcon(QStringLiteral("批"));
    case ToolType::Pdf:
        return createTextIcon(QStringLiteral("P"));
    default:
        return QIcon();
    }
}

ToolBarWidget::ToolBarWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(48);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(6);

    m_addButton = new QToolButton(this);
    m_addButton->setText(tr("Add Images"));
    m_addButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    m_addButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_addButton, &QToolButton::clicked, this, &ToolBarWidget::addImagesClicked);
    layout->addWidget(m_addButton);

    m_removeButton = new QToolButton(this);
    m_removeButton->setText(tr("Remove"));
    m_removeButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    connect(m_removeButton, &QToolButton::clicked, this, &ToolBarWidget::removeImageClicked);
    layout->addWidget(m_removeButton);

    m_clearButton = new QToolButton(this);
    m_clearButton->setText(tr("Clear"));
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

void ToolBarWidget::createButton(ToolType tool, const QString& text, const QString& shortcut)
{
    QToolButton* btn = new QToolButton(this);
    btn->setText(text + QStringLiteral(" (") + shortcut + QStringLiteral(")"));
    btn->setIcon(toolIcon(tool));
    btn->setCheckable(true);
    btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btn->setProperty("toolType", QVariant::fromValue(tool));
    m_group->addButton(btn);
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

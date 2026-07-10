#include "ToolBarWidget.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QToolButton>
#include <QVariant>

namespace yingtu {

ToolBarWidget::ToolBarWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(48);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    m_addButton = new QToolButton(this);
    m_addButton->setText(tr("Add Images"));
    m_addButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_addButton, &QToolButton::clicked, this, &ToolBarWidget::addImagesClicked);
    layout->addWidget(m_addButton);

    m_removeButton = new QToolButton(this);
    m_removeButton->setText(tr("Remove"));
    connect(m_removeButton, &QToolButton::clicked, this, &ToolBarWidget::removeImageClicked);
    layout->addWidget(m_removeButton);

    m_clearButton = new QToolButton(this);
    m_clearButton->setText(tr("Clear"));
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

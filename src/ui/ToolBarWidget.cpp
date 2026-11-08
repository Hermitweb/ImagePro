#include "ToolBarWidget.h"
#include "app/ImageProApp.h"
#include "app/ThemeManager.h"
#include <QButtonGroup>
#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QSvgRenderer>
#include <QToolButton>
#include <QVariant>

namespace yingtu {

/**
 * 把 resources/icons/tools/ 下的线性 SVG 渲染为主题色图标。
 *
 * SVG 里用 @COLOR@ 占位（Qt 的 SVG 渲染器不支持 currentColor），
 * 渲染时按当前主题替换为前景色；多个尺寸一次生成，保证高分屏清晰。
 * 主题切换时由 updateToolIcons() 统一重刷。
 */
static QIcon themedIcon(const QString& baseName)
{
    QFile file(QStringLiteral(":/icons/icons/tools/%1.svg").arg(baseName));
    if (!file.open(QIODevice::ReadOnly))
        return QIcon();
    const QColor color = ThemeManager::instance().isDark() ? QColor(0xE6, 0xE6, 0xE6)
                                                           : QColor(0x1A, 0x1A, 0x1A);
    QByteArray data = file.readAll();
    data.replace("@COLOR@", color.name().toUtf8());

    QSvgRenderer renderer(data);
    if (!renderer.isValid())
        return QIcon();

    QIcon icon;
    for (int size : {16, 20, 24, 32, 48}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter, QRect(0, 0, size, size));
        icon.addPixmap(pixmap);
    }
    return icon;
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
    m_addButton->setIcon(themedIcon(QStringLiteral("tool-add")));
    m_addButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_addButton, &QToolButton::clicked, this, &ToolBarWidget::addImagesClicked);
    layout->addWidget(m_addButton);

    m_removeButton = new QToolButton(this);
    m_removeButton->setText(tr("Remove"));
    m_removeButton->setToolTip(tr("Remove"));
    m_removeButton->setIcon(themedIcon(QStringLiteral("tool-remove")));
    connect(m_removeButton, &QToolButton::clicked, this, &ToolBarWidget::removeImageClicked);
    layout->addWidget(m_removeButton);

    m_clearButton = new QToolButton(this);
    m_clearButton->setText(tr("Clear"));
    m_clearButton->setToolTip(tr("Clear"));
    m_clearButton->setIcon(themedIcon(QStringLiteral("tool-clear")));
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

    // 主题切换：所有图标按新主题前景色重新渲染。
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            &ToolBarWidget::updateToolIcons);
}

void ToolBarWidget::updateToolIcons()
{
    m_addButton->setIcon(themedIcon(QStringLiteral("tool-add")));
    m_removeButton->setIcon(themedIcon(QStringLiteral("tool-remove")));
    m_clearButton->setIcon(themedIcon(QStringLiteral("tool-clear")));
    for (auto it = m_toolButtons.constBegin(); it != m_toolButtons.constEnd(); ++it)
        it.value()->setIcon(toolIcon(static_cast<ToolType>(it.key())));
}

QString ToolBarWidget::toolIconName(ToolType tool)
{
    switch (tool) {
    case ToolType::Stitch:
        return QStringLiteral("tool-stitch");
    case ToolType::Convert:
        return QStringLiteral("tool-convert");
    case ToolType::Compress:
        return QStringLiteral("tool-compress");
    case ToolType::Watermark:
        return QStringLiteral("tool-watermark");
    case ToolType::Edit:
        return QStringLiteral("tool-edit");
    case ToolType::Resize:
        return QStringLiteral("tool-resize");
    case ToolType::Batch:
        return QStringLiteral("tool-batch");
    case ToolType::Pdf:
        return QStringLiteral("tool-pdf");
    default:
        return QString();
    }
}

QIcon ToolBarWidget::toolIcon(ToolType tool) const
{
    const QString name = toolIconName(tool);
    if (name.isEmpty())
        return QIcon();
    return themedIcon(name);
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

#pragma once

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace yingtu {

// 自定义折叠面板：带 ▼/▶ 箭头，可展开/折叠内容区
class CollapsibleSection : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int contentHeight READ contentHeight WRITE setContentHeight)

public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr)
        : QWidget(parent)
    {
        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        m_header = new QToolButton(this);
        m_header->setCheckable(true);
        m_header->setChecked(true);
        m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_header->setStyleSheet(QStringLiteral(
            "QToolButton {"
            "  border: none;"
            "  border-radius: 4px;"
            "  padding: 6px 4px;"
            "  text-align: left;"
            "  font-weight: bold;"
            "  background-color: transparent;"
            "}"
            "QToolButton:hover { background-color: rgba(128,128,128,0.12); }"
            "QToolButton:checked { background-color: rgba(128,128,128,0.08); }"));
        setTitle(title);
        connect(m_header, &QToolButton::toggled, this, [this](bool checked) {
            setExpanded(checked);
        });

        mainLayout->addWidget(m_header);

        m_contentContainer = new QWidget(this);
        m_contentContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        QVBoxLayout* contentLayout = new QVBoxLayout(m_contentContainer);
        contentLayout->setContentsMargins(4, 4, 4, 4);
        contentLayout->setSpacing(8);
        m_contentLayout = contentLayout;
        mainLayout->addWidget(m_contentContainer);

        m_animation = new QPropertyAnimation(this);
        m_animation->setTargetObject(this);
        m_animation->setPropertyName("contentHeight");
        m_animation->setDuration(150);
        m_animation->setEasingCurve(QEasingCurve::OutQuad);
    }

    void setContent(QWidget* content)
    {
        if (!content)
            return;
        if (m_content && m_content != content) {
            m_contentLayout->removeWidget(m_content);
            m_content->setParent(nullptr);
        }
        m_content = content;
        m_contentLayout->addWidget(content);
        updateContentHeight();
    }

    QWidget* content() const { return m_content; }

    bool expanded() const { return m_expanded; }

    void setExpanded(bool expanded)
    {
        if (m_expanded == expanded)
            return;
        m_expanded = expanded;
        m_header->setChecked(expanded);
        updateArrow();

        int start = contentHeight();
        int end = expanded ? m_contentLayout->sizeHint().height() : 0;
        if (m_animation->state() == QPropertyAnimation::Running)
            m_animation->stop();
        m_animation->setStartValue(start);
        m_animation->setEndValue(end);
        m_animation->start();

        emit expandedChanged(expanded);
    }

    QString title() const { return m_title; }

    void setTitle(const QString& title)
    {
        m_title = title;
        updateArrow();
    }

    int contentHeight() const
    {
        return m_contentContainer ? m_contentContainer->height() : 0;
    }

    void setContentHeight(int height)
    {
        if (m_contentContainer) {
            m_contentContainer->setFixedHeight(height);
        }
    }

signals:
    void expandedChanged(bool expanded);

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        if (m_expanded && m_animation->state() != QPropertyAnimation::Running && m_contentContainer)
            m_contentContainer->setFixedHeight(m_contentLayout->sizeHint().height());
    }

private:
    void updateArrow()
    {
        QString arrow = m_expanded ? QStringLiteral("▼ ") : QStringLiteral("▶ ");
        m_header->setText(arrow + m_title);
    }

    void updateContentHeight()
    {
        if (!m_contentContainer || !m_content)
            return;
        int h = m_expanded ? m_contentLayout->sizeHint().height() : 0;
        m_contentContainer->setFixedHeight(h);
    }

    QToolButton* m_header = nullptr;
    QWidget* m_contentContainer = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    QWidget* m_content = nullptr;
    QPropertyAnimation* m_animation = nullptr;
    QString m_title;
    bool m_expanded = true;
};

} // namespace yingtu

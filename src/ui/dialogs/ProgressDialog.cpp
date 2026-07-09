#include "ProgressDialog.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace yingtu {

ProgressDialog::ProgressDialog(const QString& title, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    setMinimumWidth(360);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setWordWrap(true);
    layout->addWidget(m_messageLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    layout->addWidget(m_progressBar);

    m_cancelButton = new QPushButton(tr("Cancel"), this);
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        emit cancelled();
        reject();
    });

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(m_cancelButton);
    layout->addLayout(btnLayout);
}

void ProgressDialog::setProgress(int percent)
{
    if (m_progressBar)
        m_progressBar->setValue(qBound(0, percent, 100));
}

void ProgressDialog::setMessage(const QString& message)
{
    if (m_messageLabel)
        m_messageLabel->setText(message);
}

} // namespace yingtu

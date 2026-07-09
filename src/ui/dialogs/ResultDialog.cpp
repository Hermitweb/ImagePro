#include "ResultDialog.h"
#include "core/ExportManager.h"
#include <QDesktopServices>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace yingtu {

static QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024 * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

ResultDialog::ResultDialog(const ResultInfo& info, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(info.title.isEmpty() ? tr("Processing Complete") : info.title);
    resize(480, 360);

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* messageLabel = new QLabel(info.message, this);
    messageLabel->setWordWrap(true);
    messageLabel->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: bold;"));
    layout->addWidget(messageLabel);

    QListWidget* list = new QListWidget(this);
    list->addItems(info.files);
    layout->addWidget(list);

    QGridLayout* infoLayout = new QGridLayout();
    int row = 0;
    if (info.totalFileSize > 0) {
        infoLayout->addWidget(new QLabel(tr("File Size:"), this), row, 0);
        infoLayout->addWidget(new QLabel(formatFileSize(info.totalFileSize), this), row, 1);
        ++row;
    }
    if (info.resolution.isValid() && info.resolution.width() > 0 && info.resolution.height() > 0) {
        infoLayout->addWidget(new QLabel(tr("Resolution:"), this), row, 0);
        infoLayout->addWidget(new QLabel(QStringLiteral("%1 x %2")
                                             .arg(info.resolution.width())
                                             .arg(info.resolution.height()), this),
                              row, 1);
        ++row;
    }
    if (row > 0)
        layout->addLayout(infoLayout);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* openFolderBtn = new QPushButton(tr("Open Folder"), this);
    connect(openFolderBtn, &QPushButton::clicked, this, [this, info]() {
        if (!info.files.isEmpty())
            ExportManager::showInFolder(info.files.first());
        accept();
    });
    btnLayout->addWidget(openFolderBtn);

    QPushButton* closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);
}

} // namespace yingtu

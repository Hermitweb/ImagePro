#include "ResultDialog.h"
#include "core/ExportManager.h"
#include "utils/FileUtils.h"
#include <QDesktopServices>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace yingtu {

ResultDialog::ResultDialog(const ResultInfo& info, QWidget* parent) : QDialog(parent)
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
    // 双击单条结果直接用系统打开（导出完成的图片常需要立即查看）。
    connect(list, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* item) {
        if (item)
            ExportManager::openFile(item->text());
    });
    layout->addWidget(list);

    QGridLayout* infoLayout = new QGridLayout();
    int row = 0;
    if (info.totalFileSize > 0) {
        infoLayout->addWidget(new QLabel(tr("File Size:"), this), row, 0);
        infoLayout->addWidget(new QLabel(FileUtils::formatFileSize(info.totalFileSize), this), row,
                              1);
        ++row;
    }
    if (info.resolution.isValid() && info.resolution.width() > 0 && info.resolution.height() > 0) {
        infoLayout->addWidget(new QLabel(tr("Resolution:"), this), row, 0);
        infoLayout->addWidget(new QLabel(QStringLiteral("%1 x %2")
                                             .arg(info.resolution.width())
                                             .arg(info.resolution.height()),
                                         this),
                              row, 1);
        ++row;
    }
    if (row > 0)
        layout->addLayout(infoLayout);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    // 用系统打开第一个结果：ExportManager::openFile 此前已实现但零调用。
    QPushButton* openFileBtn = new QPushButton(tr("Open File"), this);
    connect(openFileBtn, &QPushButton::clicked, this, [this, info]() {
        if (!info.files.isEmpty())
            ExportManager::openFile(info.files.first());
    });
    btnLayout->addWidget(openFileBtn);

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

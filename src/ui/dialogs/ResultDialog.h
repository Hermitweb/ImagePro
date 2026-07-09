#pragma once

#include <QDialog>
#include <QSize>
#include <QStringList>

namespace yingtu {

struct ResultInfo {
    QString title;
    QString message;
    QStringList files;
    qint64 totalFileSize = 0;
    QSize resolution;
};

class ResultDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ResultDialog(const ResultInfo& info, QWidget* parent = nullptr);
};

} // namespace yingtu

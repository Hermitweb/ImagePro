#pragma once

#include <QDialog>

class QProgressBar;
class QLabel;
class QPushButton;

namespace yingtu {

class ProgressDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProgressDialog(const QString& title, QWidget* parent = nullptr);

    void setProgress(int percent);
    void setMessage(const QString& message);

signals:
    void cancelled();

private:
    QLabel* m_messageLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QPushButton* m_cancelButton = nullptr;
};

} // namespace yingtu

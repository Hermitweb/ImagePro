#pragma once

#include <QApplication>

namespace yingtu {

class ImageProApp : public QApplication
{
    Q_OBJECT
public:
    ImageProApp(int& argc, char** argv);

    void initialize();

    static QStringList supportedLanguages();
    static QString languageName(const QString& code);
    void setLanguage(const QString& code);
    QString currentLanguage() const;

private:
    void loadTranslations(const QString& code);

    QTranslator* m_appTranslator = nullptr;
    QTranslator* m_qtTranslator = nullptr;
    QString m_currentLanguage;
};

} // namespace yingtu

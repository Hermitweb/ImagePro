#pragma once

#include <QApplication>

namespace yingtu {

class ImageProApp : public QApplication
{
    Q_OBJECT
public:
    ImageProApp(int& argc, char** argv);

    /**
     * 是否正处于语言切换过程中。
     *
     * 装/卸翻译器各触发一次 LanguageChange 事件，一次切换会连发好几次。
     * 控件在这期间应当**忽略**这些事件，等切换结束后由应用统一补发一次，
     * 否则属性面板会被反复重建（既慢又闪）。
     */
    static bool isLanguageSwitching();

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

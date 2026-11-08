#pragma once

#include <QHash>
#include <QWidget>

class QButtonGroup;
class QIcon;
class QToolButton;

namespace yingtu {

enum class ToolType {
    None = -1,
    Stitch,
    Convert,
    Compress,
    Watermark,
    Edit,
    Resize,
    Batch,
    Pdf
};

class ToolBarWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ToolBarWidget(QWidget* parent = nullptr);

    ToolType currentTool() const;
    void setCurrentTool(ToolType tool);

signals:
    void toolChanged(ToolType tool);
    void addImagesClicked();
    void clearImagesClicked();
    void removeImageClicked();

protected:
    // 语言切换：重新套用按钮文本与图标。
    void changeEvent(QEvent* event) override;

private:
    void createButton(ToolType tool, const QString& translatedText, const QString& shortcut);
    void retranslateUi();
    /** 主题切换后重刷所有按钮图标（SVG 按新主题前景色重新渲染）。 */
    void updateToolIcons();

    // 工具按钮的图标名（resources/icons/tools/ 下不含扩展名）。
    static QString toolIconName(ToolType tool);
    // 工具按钮的图标。
    QIcon toolIcon(ToolType tool) const;
    // 工具按钮的源串（英文）。语言切换时按源串重新取译文。
    static QString toolSourceText(ToolType tool);
    // 按本类上下文翻译一个运行时字符串（源串不是字面量时不能用 tr()）。
    QString translateSource(const QString& source) const;

    QButtonGroup* m_group = nullptr;
    QToolButton* m_addButton = nullptr;
    QToolButton* m_removeButton = nullptr;
    QToolButton* m_clearButton = nullptr;
    // 工具按钮：类型 → 按钮。语言切换时按类型重新取译文与图标。
    // 键用 int：ToolType 是作用域枚举，QHash 需要显式的 qHash 重载。
    QHash<int, QToolButton*> m_toolButtons;
};

} // namespace yingtu

Q_DECLARE_METATYPE(yingtu::ToolType)

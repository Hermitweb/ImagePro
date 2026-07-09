#pragma once

#include <QWidget>

class QButtonGroup;
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
    Batch
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

private:
    void createButton(ToolType tool, const QString& text, const QString& shortcut);

    QButtonGroup* m_group = nullptr;
    QToolButton* m_addButton = nullptr;
    QToolButton* m_removeButton = nullptr;
    QToolButton* m_clearButton = nullptr;
};

} // namespace yingtu

Q_DECLARE_METATYPE(yingtu::ToolType)

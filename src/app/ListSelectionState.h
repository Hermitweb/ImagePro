#pragma once

#include <QMetaObject>
#include <QObject>
#include <QVector>

namespace yingtu {

class ListSelectionState : public QObject
{
    Q_OBJECT
public:
    explicit ListSelectionState(QObject* parent = nullptr);

    static ListSelectionState& instance();

    int singleSelected() const;
    bool hasSelection() const;
    QVector<int> selectedRows() const;
    int selectedCount() const;
    bool isSelected(int row) const;

    void setSelection(const QVector<int>& rows);
    void setSingleSelection(int row);
    void clearSelection();

signals:
    void selectionChanged(const QVector<int>& rows);
    void singleSelectionChanged(int row);

private:
    QVector<int> m_selectedRows;
};

} // namespace yingtu

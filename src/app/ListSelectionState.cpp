#include "ListSelectionState.h"

#include <QThread>
#include <algorithm>

namespace yingtu {

ListSelectionState::ListSelectionState(QObject* parent)
    : QObject(parent)
{
}

ListSelectionState& ListSelectionState::instance()
{
    static ListSelectionState state;
    return state;
}

int ListSelectionState::singleSelected() const
{
    return m_selectedRows.size() == 1 ? m_selectedRows.first() : -1;
}

bool ListSelectionState::hasSelection() const
{
    return !m_selectedRows.isEmpty();
}

QVector<int> ListSelectionState::selectedRows() const
{
    return m_selectedRows;
}

int ListSelectionState::selectedCount() const
{
    return m_selectedRows.size();
}

bool ListSelectionState::isSelected(int row) const
{
    return m_selectedRows.contains(row);
}

void ListSelectionState::setSelection(const QVector<int>& rows)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, rows]() {
            setSelection(rows);
        }, Qt::QueuedConnection);
        return;
    }

    QVector<int> sorted = rows;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    if (m_selectedRows == sorted)
        return;

    m_selectedRows = sorted;
    emit selectionChanged(m_selectedRows);
    emit singleSelectionChanged(singleSelected());
}

void ListSelectionState::setSingleSelection(int row)
{
    if (row < 0) {
        clearSelection();
        return;
    }
    setSelection(QVector<int>{row});
}

void ListSelectionState::clearSelection()
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this]() {
            clearSelection();
        }, Qt::QueuedConnection);
        return;
    }

    if (m_selectedRows.isEmpty())
        return;

    m_selectedRows.clear();
    emit selectionChanged(m_selectedRows);
    emit singleSelectionChanged(-1);
}

} // namespace yingtu

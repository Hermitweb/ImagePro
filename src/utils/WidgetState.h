#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QList>
#include <QPlainTextEdit>
#include <QSlider>
#include <QSpinBox>
#include <QVariant>
#include <QWidget>

namespace yingtu {

/**
 * 通用控件取值快照。
 *
 * 用途：界面按语言重建之后，把用户填过的值原样还原回去 ——
 * 否则一次语言切换就会把用户刚调好的参数全部清空。
 *
 * 配对方式：按控件树遍历顺序记录「值 + 控件类型」，还原时按同样顺序配对。
 * 重建所用的构建函数顺序固定，因此重建前后第 N 个同类控件一一对应。
 * 若结构发生变化（类型对不上），该条会被跳过而不是硬套 —— 宁可少还原一项，
 * 也不要给控件写入不属于它的值。
 */
struct WidgetValue {
    enum class Kind {
        Check,
        Combo,
        Spin,
        DoubleSpin,
        Slider,
        LineEdit,
        TextEdit
    };
    Kind kind = Kind::Check;
    QVariant value;
};

/** 遍历 root 的后代控件，按类型记录其取值。 */
inline QList<WidgetValue> captureWidgetValues(QWidget* root)
{
    QList<WidgetValue> result;
    if (!root)
        return result;

    const QList<QWidget*> widgets = root->findChildren<QWidget*>();
    result.reserve(widgets.size());
    for (QWidget* widget : widgets) {
        WidgetValue entry;
        if (auto* check = qobject_cast<QCheckBox*>(widget)) {
            entry.kind = WidgetValue::Kind::Check;
            entry.value = check->isChecked();
        } else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            entry.kind = WidgetValue::Kind::Combo;
            entry.value = combo->currentIndex();
        } else if (auto* spin = qobject_cast<QSpinBox*>(widget)) {
            entry.kind = WidgetValue::Kind::Spin;
            entry.value = spin->value();
        } else if (auto* dspin = qobject_cast<QDoubleSpinBox*>(widget)) {
            entry.kind = WidgetValue::Kind::DoubleSpin;
            entry.value = dspin->value();
        } else if (auto* slider = qobject_cast<QSlider*>(widget)) {
            entry.kind = WidgetValue::Kind::Slider;
            entry.value = slider->value();
        } else if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
            entry.kind = WidgetValue::Kind::LineEdit;
            entry.value = edit->text();
        } else if (auto* text = qobject_cast<QPlainTextEdit*>(widget)) {
            entry.kind = WidgetValue::Kind::TextEdit;
            entry.value = text->toPlainText();
        } else {
            continue;
        }
        result.append(entry);
    }
    return result;
}

/** 把快照中的取值写回 root 的后代控件。 */
inline void applyWidgetValues(QWidget* root, const QList<WidgetValue>& values)
{
    if (!root)
        return;

    int index = 0;
    const QList<QWidget*> widgets = root->findChildren<QWidget*>();
    for (QWidget* widget : widgets) {
        WidgetValue::Kind kind = WidgetValue::Kind::Check;
        bool known = true;

        if (qobject_cast<QCheckBox*>(widget)) {
            kind = WidgetValue::Kind::Check;
        } else if (qobject_cast<QComboBox*>(widget)) {
            kind = WidgetValue::Kind::Combo;
        } else if (qobject_cast<QSpinBox*>(widget)) {
            kind = WidgetValue::Kind::Spin;
        } else if (qobject_cast<QDoubleSpinBox*>(widget)) {
            kind = WidgetValue::Kind::DoubleSpin;
        } else if (qobject_cast<QSlider*>(widget)) {
            kind = WidgetValue::Kind::Slider;
        } else if (qobject_cast<QLineEdit*>(widget)) {
            kind = WidgetValue::Kind::LineEdit;
        } else if (qobject_cast<QPlainTextEdit*>(widget)) {
            kind = WidgetValue::Kind::TextEdit;
        } else {
            known = false;
        }
        if (!known)
            continue;

        if (index >= values.size())
            break;
        const WidgetValue& saved = values.at(index);
        ++index;
        if (saved.kind != kind)
            continue; // 结构变了：跳过，不硬套

        if (auto* check = qobject_cast<QCheckBox*>(widget)) {
            check->setChecked(saved.value.toBool());
        } else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            const int current = saved.value.toInt();
            if (current >= 0 && current < combo->count())
                combo->setCurrentIndex(current);
        } else if (auto* spin = qobject_cast<QSpinBox*>(widget)) {
            spin->setValue(saved.value.toInt());
        } else if (auto* dspin = qobject_cast<QDoubleSpinBox*>(widget)) {
            dspin->setValue(saved.value.toDouble());
        } else if (auto* slider = qobject_cast<QSlider*>(widget)) {
            slider->setValue(saved.value.toInt());
        } else if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
            edit->setText(saved.value.toString());
        } else if (auto* text = qobject_cast<QPlainTextEdit*>(widget)) {
            text->setPlainText(saved.value.toString());
        }
    }
}

} // namespace yingtu

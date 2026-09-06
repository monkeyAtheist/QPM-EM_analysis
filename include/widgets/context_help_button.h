#pragma once

#include <QCursor>
#include <QObject>
#include <QString>
#include <QToolButton>
#include <QToolTip>
#include <QWidget>

namespace UiWidgets
{
inline QToolButton *contextHelpButton(QWidget *parent,
                                      const QString &title,
                                      const QString &description)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("ContextHelpButton"));
    button->setText(QStringLiteral("?"));
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setFixedSize(26, 26);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::WhatsThisCursor);
    button->setAccessibleName(title);
    button->setAccessibleDescription(description);

    const QString tip = QStringLiteral(
        "<qt><b>%1</b><br/><br/><span>%2</span></qt>")
                            .arg(title.toHtmlEscaped(), description.toHtmlEscaped());
    button->setToolTip(tip);
    button->setToolTipDuration(30000);

    // Hover shows the normal Qt tooltip. A click pins the same text long enough to
    // be useful on touchpads and for users who move the pointer while reading.
    QObject::connect(button, &QToolButton::clicked, button, [button] {
        QToolTip::showText(QCursor::pos(), button->toolTip(), button);
    });
    return button;
}
} // namespace UiWidgets

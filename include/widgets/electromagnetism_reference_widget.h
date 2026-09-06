#pragma once

#include <QWidget>

class QEvent;
class QTabWidget;

class ElectromagnetismReferenceWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ElectromagnetismReferenceWidget(QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;

private:
    void rebuildPages();
    QTabWidget *m_tabs = nullptr;
};

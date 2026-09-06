#pragma once

#include <QDialog>
#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui { class DialogElectroStat; }
QT_END_NAMESPACE

class DialogElectroStat final : public QDialog
{
    Q_OBJECT

public:
    explicit DialogElectroStat(QWidget *parent = nullptr);
    ~DialogElectroStat() override;

private slots:

private:
    std::unique_ptr<Ui::DialogElectroStat> ui;
};

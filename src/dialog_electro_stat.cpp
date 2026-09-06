#include "dialog_electro_stat.h"
#include "ui_dialog_electro_stat.h"

DialogElectroStat::DialogElectroStat(QWidget *parent)
    : QDialog(parent),
      ui(std::make_unique<Ui::DialogElectroStat>())
{
    ui->setupUi(this);
}

DialogElectroStat::~DialogElectroStat() = default;

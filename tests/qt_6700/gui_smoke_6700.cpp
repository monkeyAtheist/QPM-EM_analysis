#include "widgets/em_calculator_widget.h"

#include <QApplication>
#include <QClipboard>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QtTest>

class CalculatorGuiSmoke : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("QTsignalAppTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Calculator6700"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
    }
    void computeAndInvalidate()
    {
        EmCalculatorWidget widget;
        widget.resize(960, 750);
        widget.show();
        auto *tabs = widget.findChild<QTabWidget *>(QStringLiteral("emCalculatorTabs"));
        auto *expr = widget.findChild<QLineEdit *>(QStringLiteral("emScientificExpression"));
        auto *calc = widget.findChild<QPushButton *>(QStringLiteral("emCalculateButton"));
        auto *result = widget.findChild<QLineEdit *>(QStringLiteral("emScientificResult"));
        QVERIFY(tabs && expr && calc && result);
        tabs->setCurrentIndex(0);
        expr->setText(QStringLiteral("ke*(2 nC)/(10 cm)^2"));
        QTest::mouseClick(calc, Qt::LeftButton);
        QVERIFY(!result->text().isEmpty());
        // The representation can be 1797 V/m or 1.797 kV/m; both use the same engine.
        const double number = result->text().toDouble();
        QVERIFY(number > 0.0);
        expr->setText(QStringLiteral("ke*(3 nC)/(10 cm)^2"));
        QVERIFY(result->text().isEmpty()); // stale result must not be copied
    }
    void notesSelectionAndCopy()
    {
        EmCalculatorWidget widget;
        widget.resize(960, 750);
        widget.show();
        auto *tabs = widget.findChild<QTabWidget *>(QStringLiteral("emCalculatorTabs"));
        auto *notes = widget.findChild<QPlainTextEdit *>(QStringLiteral("emNotesEditor"));
        QVERIFY(tabs && notes);
        tabs->setCurrentIndex(tabs->count()-1);
        notes->setPlainText(QStringLiteral("Laplace rail: F = B*I*L"));
        notes->selectAll();
        notes->copy();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("Laplace rail: F = B*I*L"));
    }
};
QTEST_MAIN(CalculatorGuiSmoke)
#include "gui_smoke_6700.moc"

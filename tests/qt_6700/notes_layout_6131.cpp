#include "widgets/em_calculator_widget.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QLabel>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QToolButton>
#include <QWidget>
#include <QtTest>

class NotesLayout6131 : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("QTsignalAppTests"));
        QCoreApplication::setApplicationName(QStringLiteral("NotesLayout6131"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QStandardPaths::setTestModeEnabled(true);
    }
    void compactAndExpandable()
    {
        EmCalculatorWidget widget;
        widget.resize(1360, 900);
        widget.show();
        auto *tabs = widget.findChild<QTabWidget *>(QStringLiteral("emCalculatorTabs"));
        auto *modes = widget.findChild<QTabWidget *>(QStringLiteral("notebookModeTabs"));
        auto *advanced = widget.findChild<QWidget *>(QStringLiteral("notesDrawingAdvancedPanel"));
        auto *toggle = widget.findChild<QToolButton *>(QStringLiteral("notesDrawingDetailsToggle"));
        auto *help = widget.findChild<QToolButton *>(QStringLiteral("notesDrawingHelp"));
        auto *canvas = widget.findChild<QWidget *>(QStringLiteral("notesInkCanvas"));
        auto *tool = widget.findChild<QComboBox *>(QStringLiteral("notesDrawingTool"));
        QVERIFY(tabs && modes && advanced && toggle && help && canvas && tool);

        tabs->setCurrentIndex(tabs->count()-1); // Notes
        modes->setCurrentIndex(1);               // Drawing
        QCoreApplication::processEvents();
        QVERIFY(!advanced->isVisible());
        QVERIFY(canvas->isVisible());
        // At this window size the drawing viewport should have enough height
        // to be usable without invoking the maximized drawing dialog.
        QVERIFY2(canvas->height() >= 450, "Compact notebook canvas height is too small");

        toggle->click();
        QVERIFY(advanced->isVisible());
        toggle->click();
        QVERIFY(!advanced->isVisible());

        // Picking electronic-symbol mode must reveal its configuration panel.
        tool->setCurrentIndex(9);
        QVERIFY(advanced->isVisible());
        QVERIFY(toggle->isChecked());
    }
};
QTEST_MAIN(NotesLayout6131)
#include "notes_layout_6131.moc"

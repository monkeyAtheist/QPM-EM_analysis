#include <QApplication>
#include <QCoreApplication>
#include <QDebug>

#include "app_diagnostics.h"
#include "mainwindow.h"
#include "ui_style_manager.h"

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("QTsignalApp"));
    QApplication::setApplicationName(QStringLiteral("QTsignalApp"));
    QApplication::setApplicationVersion(QStringLiteral("6.14.0"));

    AppDiagnostics::initialize();
    AppDiagnostics::installQtMessageHandler();
    qInfo("QApplication constructed; log: %s", qPrintable(AppDiagnostics::logFilePath()));

    qInfo().noquote() << QStringLiteral("Applying saved UI style");
    UiStyle::captureSystemDefaults(application);
    UiStyle::applySaved(application);
    qInfo().noquote() << QStringLiteral("UI style applied");

    qInfo().noquote() << QStringLiteral("Constructing MainWindow");
    MainWindow window;
    qInfo().noquote() << QStringLiteral("MainWindow constructed");
    window.show();
    qInfo().noquote() << QStringLiteral("MainWindow show() returned; entering event loop");

    QObject::connect(&application, &QCoreApplication::aboutToQuit, &application, [] {
        qInfo().noquote() << QStringLiteral("Application about to quit normally");
    });

    return application.exec();
}

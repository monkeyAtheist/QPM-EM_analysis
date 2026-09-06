#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QTextStream>

#include <cstdio>

#include "mainwindow.h"
#include "ui_style_manager.h"

namespace
{
QString startupLogPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/QTsignalApp_startup.log");
}

void startupTrace(const QString &message)
{
    QFile file(startupLogPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))
           << QStringLiteral(" | ") << message << '\n';
}

void startupQtMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    const char *kind = "DEBUG";
    switch (type)
    {
    case QtInfoMsg: kind = "INFO"; break;
    case QtWarningMsg: kind = "WARNING"; break;
    case QtCriticalMsg: kind = "CRITICAL"; break;
    case QtFatalMsg: kind = "FATAL"; break;
    case QtDebugMsg: default: break;
    }
    startupTrace(QStringLiteral("Qt %1: %2").arg(QString::fromLatin1(kind), message));
    const QByteArray local = message.toLocal8Bit();
    std::fprintf(stderr, "Qt %s: %s\n", kind, local.constData());
    std::fflush(stderr);
}
}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("QTsignalApp"));
    QApplication::setApplicationName(QStringLiteral("QTsignalApp"));
    QApplication::setApplicationVersion(QStringLiteral("6.2.0"));

    QFile::remove(startupLogPath());
    startupTrace(QStringLiteral("QApplication constructed"));
    qInstallMessageHandler(startupQtMessageHandler);

    startupTrace(QStringLiteral("Applying saved UI style"));
    UiStyle::captureSystemDefaults(application);
    UiStyle::applySaved(application);
    startupTrace(QStringLiteral("UI style applied"));

    startupTrace(QStringLiteral("Constructing MainWindow"));
    MainWindow window;
    startupTrace(QStringLiteral("MainWindow constructed"));
    window.show();
    startupTrace(QStringLiteral("MainWindow show() returned; entering event loop"));

    QObject::connect(&application, &QCoreApplication::aboutToQuit, &application, [] {
        startupTrace(QStringLiteral("Application about to quit normally"));
    });

    return application.exec();
}

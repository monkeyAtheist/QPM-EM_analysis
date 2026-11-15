#include "app_diagnostics.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class LoggingTest : public QObject
{
    Q_OBJECT
private slots:
    void persistsSessionAndQtWarning()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        qputenv("QTSIGNALAPP_LOG_DIR", tmp.path().toLocal8Bit());
        QCoreApplication::setOrganizationName(QStringLiteral("QTsignalApp"));
        QCoreApplication::setApplicationName(QStringLiteral("QTsignalAppLogsTest"));
        QCoreApplication::setApplicationVersion(QStringLiteral("6.7.0"));
        AppDiagnostics::initialize();
        AppDiagnostics::installQtMessageHandler();
        qWarning("QTest log marker 6700");
        QFile file(AppDiagnostics::logFilePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray first = file.readAll();
        QVERIFY(first.contains("SESSION START"));
        QVERIFY(first.contains("QTest log marker 6700"));
        file.close();
        AppDiagnostics::initialize(); // Should append, never truncate an existing session.
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray second = file.readAll();
        QVERIFY(second.size() > first.size());
        QVERIFY(second.contains("QTest log marker 6700"));
    }
};
QTEST_GUILESS_MAIN(LoggingTest)
#include "logging_6700.moc"

#include "app_diagnostics.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>
#include <QtGlobal>

#include <cstdio>

namespace
{
constexpr qint64 MaxLogBytes = 2 * 1024 * 1024;
constexpr int ArchivedLogs = 3;
QMutex g_logMutex;
QString g_logDirectory;

QString writableLogFolder()
{
    const QByteArray override = qgetenv("QTSIGNALAPP_LOG_DIR");
    const QString base = override.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs")
        : QString::fromLocal8Bit(override);
    if (!base.isEmpty() && QDir().mkpath(base))
    {
        QFile probe(QDir(base).filePath(QStringLiteral(".write-probe")));
        if (probe.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            probe.close();
            probe.remove();
            return QDir(base).absolutePath();
        }
    }

    // Application installation may be read-only (Program Files). A temp folder
    // remains preferable to losing errors entirely if the user location fails.
    const QString fallback = QDir::tempPath() + QStringLiteral("/QTsignalApp/logs");
    if (QDir().mkpath(fallback)) return QDir(fallback).absolutePath();
    return {};
}

void rotateIfFull(const QString &path)
{
    if (QFileInfo(path).size() < MaxLogBytes) return;
    QFile::remove(path + QStringLiteral(".3"));
    for (int index = ArchivedLogs - 1; index >= 1; --index)
    {
        const QString current = path + QStringLiteral(".%1").arg(index);
        QFile::rename(current, path + QStringLiteral(".%1").arg(index + 1));
    }
    QFile::rename(path, path + QStringLiteral(".1"));
}

QString logPathUnderLock()
{
    return g_logDirectory.isEmpty() ? QString() : QDir(g_logDirectory).filePath(QStringLiteral("QTsignalApp.log"));
}

void appendUnderLock(const QString &entry)
{
    const QString path = logPathUnderLock();
    if (path.isEmpty()) return;
    rotateIfFull(path);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    QTextStream output(&file);
    output << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))
           << QStringLiteral(" | ") << entry << '\n';
}

const char *levelName(QtMsgType type)
{
    switch (type)
    {
    case QtDebugMsg: return "DEBUG";
    case QtInfoMsg: return "INFO";
    case QtWarningMsg: return "WARNING";
    case QtCriticalMsg: return "CRITICAL";
    case QtFatalMsg: return "FATAL";
    }
    return "UNKNOWN";
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    QString entry = QStringLiteral("%1 | %2").arg(QString::fromLatin1(levelName(type)), message);
    if (context.file && context.line > 0)
        entry += QStringLiteral(" (%1:%2)").arg(QString::fromLocal8Bit(context.file)).arg(context.line);
    {
        const QMutexLocker lock(&g_logMutex);
        appendUnderLock(entry);
    }
    // Keep stderr useful when launched from QPM; do not call qWarning/qDebug
    // here, as they would recursively invoke this handler.
    const QByteArray encoded = entry.toLocal8Bit();
    std::fprintf(stderr, "%s\n", encoded.constData());
    std::fflush(stderr);
}
} // namespace

void AppDiagnostics::initialize()
{
    const QMutexLocker lock(&g_logMutex);
    g_logDirectory = writableLogFolder();
    appendUnderLock(QStringLiteral("SESSION START | QTsignalApp %1 | log retention: %2 x %3 MiB")
                        .arg(QCoreApplication::applicationVersion())
                        .arg(ArchivedLogs + 1).arg(MaxLogBytes / (1024 * 1024)));
}

void AppDiagnostics::installQtMessageHandler()
{
    qInstallMessageHandler(messageHandler);
}

QString AppDiagnostics::logDirectory()
{
    const QMutexLocker lock(&g_logMutex);
    return g_logDirectory;
}

QString AppDiagnostics::logFilePath()
{
    const QMutexLocker lock(&g_logMutex);
    return logPathUnderLock();
}

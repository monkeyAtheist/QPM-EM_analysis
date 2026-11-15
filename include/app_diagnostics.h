#pragma once

#include <QString>

// Centralised logging, independent of the installation/executable directory.
// The log path is initialized after QApplication's organization/application names.
namespace AppDiagnostics
{
void initialize();
void installQtMessageHandler();
QString logDirectory();
QString logFilePath();
} // namespace AppDiagnostics

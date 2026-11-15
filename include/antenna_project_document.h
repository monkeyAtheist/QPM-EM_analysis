#pragma once

// QtCore-only Antenna Designer persistence contract.
// No widgets, meshes or EM solver dependencies: suitable for small, deterministic tests.
#include <QJsonObject>
#include <QString>

namespace AntennaProjectDocument
{
inline constexpr int kSchemaVersion = 19;

// Reject future projects or invalid metadata before touching live geometry.
bool checkAntennaProjectSchema(const QJsonObject &root, int &sourceVersion,
                               QString &migrationNotice, QString &error);
QJsonObject migrateAntennaProjectToCurrent(QJsonObject root, int sourceVersion);
bool validateAntennaProjectDocument(const QJsonObject &root, QString &error);
} // namespace AntennaProjectDocument

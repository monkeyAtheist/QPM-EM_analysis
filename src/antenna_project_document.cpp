#include "antenna_project_document.h"

#include <QJsonArray>
#include <QJsonValue>

#include <cmath>

namespace AntennaProjectDocument
{
namespace
{
constexpr int AntennaProjectSchemaVersion = kSchemaVersion;
constexpr int AntennaProjectMinimumSchemaVersion = 1;
constexpr qsizetype AntennaProjectMaximumCollectionSize = 100000;

bool validateFiniteJsonValue(const QJsonValue &value, const QString &path, QString &error)
{
    if (value.isDouble())
    {
        const double number = value.toDouble();
        if (!std::isfinite(number))
        {
            error = QStringLiteral("%1 contains a non-finite numeric value.").arg(path);
            return false;
        }
        return true;
    }
    if (value.isArray())
    {
        const QJsonArray array = value.toArray();
        if (array.size() > AntennaProjectMaximumCollectionSize)
        {
            error = QStringLiteral("%1 contains %2 entries; the safety limit is %3.")
                        .arg(path).arg(array.size()).arg(AntennaProjectMaximumCollectionSize);
            return false;
        }
        for (qsizetype i = 0; i < array.size(); ++i)
            if (!validateFiniteJsonValue(array.at(i), QStringLiteral("%1[%2]").arg(path).arg(i), error)) return false;
        return true;
    }
    if (value.isObject())
    {
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            if (!validateFiniteJsonValue(it.value(), QStringLiteral("%1.%2").arg(path).arg(it.key()), error)) return false;
    }
    return true;
}

bool validateOptionalFiniteNumber(const QJsonObject &object, const QString &key, const QString &context, QString &error)
{
    if (!object.contains(key)) return true;
    const QJsonValue value = object.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
    {
        error = QStringLiteral("%1.%2 must be a finite number.").arg(context).arg(key);
        return false;
    }
    return true;
}

bool validateOptionalNumberRange(const QJsonObject &object, const QString &key, double minimum, double maximum,
                                 const QString &context, QString &error)
{
    if (!object.contains(key)) return true;
    const QJsonValue value = object.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < minimum || value.toDouble() > maximum)
    {
        error = QStringLiteral("%1.%2 is outside the supported range [%3, %4].")
                    .arg(context).arg(key).arg(minimum, 0, 'g', 12).arg(maximum, 0, 'g', 12);
        return false;
    }
    return true;
}

bool validateOptionalIntegerRange(const QJsonObject &object, const QString &key, int minimum, int maximum,
                                  const QString &context, QString &error)
{
    if (!object.contains(key)) return true;
    const QJsonValue value = object.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble()) ||
        std::floor(value.toDouble()) != value.toDouble() || value.toDouble() < minimum || value.toDouble() > maximum)
    {
        error = QStringLiteral("%1.%2 must be an integer in [%3, %4].")
                    .arg(context).arg(key).arg(minimum).arg(maximum);
        return false;
    }
    return true;
}

bool validateRequiredFiniteNumber(const QJsonObject &object, const QString &key, const QString &context, QString &error)
{
    if (!object.contains(key) || !object.value(key).isDouble() || !std::isfinite(object.value(key).toDouble()))
    {
        error = QStringLiteral("%1.%2 is missing or is not a finite number.").arg(context).arg(key);
        return false;
    }
    return true;
}

bool validateProjectArrayType(const QJsonObject &root, const QString &key, QString &error)
{
    if (!root.contains(key)) return true;
    if (!root.value(key).isArray())
    {
        error = QStringLiteral("Project field '%1' must be a JSON array.").arg(key);
        return false;
    }
    if (root.value(key).toArray().size() > AntennaProjectMaximumCollectionSize)
    {
        error = QStringLiteral("Project field '%1' exceeds the safety limit of %2 entries.")
                    .arg(key).arg(AntennaProjectMaximumCollectionSize);
        return false;
    }
    return true;
}

} // namespace

bool checkAntennaProjectSchema(const QJsonObject &root, int &sourceVersion, QString &migrationNotice, QString &error)
{
    sourceVersion = AntennaProjectMinimumSchemaVersion;
    migrationNotice.clear();
    error.clear();

    if (!root.contains(QStringLiteral("version")))
    {
        migrationNotice = QStringLiteral("Legacy antenna project without an explicit schema version; interpreted as v1 and migrated in memory to v%1.")
                              .arg(AntennaProjectSchemaVersion);
    }
    else
    {
        const QJsonValue versionValue = root.value(QStringLiteral("version"));
        if (!versionValue.isDouble() || !std::isfinite(versionValue.toDouble()) ||
            std::floor(versionValue.toDouble()) != versionValue.toDouble())
        {
            error = QStringLiteral("Invalid antenna project schema version. An integer version is required.");
            return false;
        }
        // JSON numbers are doubles: reject an out-of-range value before narrowing to int.
        if (versionValue.toDouble() < static_cast<double>(AntennaProjectMinimumSchemaVersion) ||
            versionValue.toDouble() > static_cast<double>(AntennaProjectSchemaVersion))
        {
            const double requested = versionValue.toDouble();
            error = requested > AntennaProjectSchemaVersion
                ? QStringLiteral("This antenna project uses a newer unsupported schema version (%1); maximum supported is v%2.")
                      .arg(requested, 0, 'g', 17).arg(AntennaProjectSchemaVersion)
                : QStringLiteral("Invalid antenna project schema version (%1).")
                      .arg(requested, 0, 'g', 17);
            return false;
        }
        sourceVersion = static_cast<int>(versionValue.toDouble());
        if (sourceVersion < AntennaProjectMinimumSchemaVersion)
        {
            error = QStringLiteral("Antenna project schema v%1 is older than the oldest supported schema v%2.")
                        .arg(sourceVersion).arg(AntennaProjectMinimumSchemaVersion);
            return false;
        }
        if (sourceVersion > AntennaProjectSchemaVersion)
        {
            error = QStringLiteral("This antenna project uses schema v%1, but this QTsignalApp build supports up to v%2. "
                                   "Open it with a newer QTsignalApp version; the file was not modified.")
                        .arg(sourceVersion).arg(AntennaProjectSchemaVersion);
            return false;
        }
        if (sourceVersion < AntennaProjectSchemaVersion)
            migrationNotice = QStringLiteral("Antenna project schema v%1 was migrated in memory to v%2. The original file is unchanged until you save it.")
                                  .arg(sourceVersion).arg(AntennaProjectSchemaVersion);
    }

    if (root.contains(QStringLiteral("units")) && root.value(QStringLiteral("units")).toString() != QStringLiteral("m"))
    {
        error = QStringLiteral("Unsupported antenna project units. This build only accepts SI geometry stored with units='m'.");
        return false;
    }
    return true;
}

QJsonObject migrateAntennaProjectToCurrent(QJsonObject root, int sourceVersion)
{
    Q_UNUSED(sourceVersion);
    if (!root.contains(QStringLiteral("surfaces")) && root.value(QStringLiteral("planes")).isArray())
        root.insert(QStringLiteral("surfaces"), root.value(QStringLiteral("planes")));

    if (root.value(QStringLiteral("feeds")).isArray())
    {
        QJsonArray migratedFeeds;
        for (const QJsonValue &value : root.value(QStringLiteral("feeds")).toArray())
        {
            if (!value.isObject()) { migratedFeeds.append(value); continue; }
            QJsonObject feed = value.toObject();
            if (!feed.contains(QStringLiteral("referenceOhm")) && feed.contains(QStringLiteral("sourceOhm")))
                feed.insert(QStringLiteral("referenceOhm"), feed.value(QStringLiteral("sourceOhm")));
            migratedFeeds.append(feed);
        }
        root.insert(QStringLiteral("feeds"), migratedFeeds);
    }

    if (!root.contains(QStringLiteral("units"))) root.insert(QStringLiteral("units"), QStringLiteral("m"));
    root.insert(QStringLiteral("version"), AntennaProjectSchemaVersion);
    return root;
}

bool validateAntennaProjectDocument(const QJsonObject &root, QString &error)
{
    error.clear();
    if (!validateFiniteJsonValue(root, QStringLiteral("project"), error)) return false;
    for (const QString &key : {QStringLiteral("wires"), QStringLiteral("surfaces"), QStringLiteral("planes"),
                               QStringLiteral("dielectrics"), QStringLiteral("feeds"), QStringLiteral("constraints"),
                               QStringLiteral("groups"), QStringLiteral("individualDirectorVariables")})
        if (!validateProjectArrayType(root, key, error)) return false;

    const QJsonArray wires = root.value(QStringLiteral("wires")).toArray();
    for (qsizetype i = 0; i < wires.size(); ++i)
    {
        if (!wires.at(i).isObject()) { error = QStringLiteral("wires[%1] must be a JSON object.").arg(i); return false; }
        const QJsonObject o = wires.at(i).toObject();
        const QString context = QStringLiteral("wires[%1]").arg(i);
        for (const QString &key : {QStringLiteral("x1"), QStringLiteral("y1"), QStringLiteral("x2"), QStringLiteral("y2")})
            if (!validateRequiredFiniteNumber(o, key, context, error)) return false;
        for (const QString &key : {QStringLiteral("z1"), QStringLiteral("z2")})
            if (!validateOptionalFiniteNumber(o, key, context, error)) return false;
        if (!validateOptionalNumberRange(o, QStringLiteral("radiusM"), 1e-12, 1e12, context, error)) return false;
        const double z1 = o.value(QStringLiteral("z1")).toDouble(0.0);
        const double z2 = o.value(QStringLiteral("z2")).toDouble(0.0);
        const double dx = o.value(QStringLiteral("x2")).toDouble() - o.value(QStringLiteral("x1")).toDouble();
        const double dy = o.value(QStringLiteral("y2")).toDouble() - o.value(QStringLiteral("y1")).toDouble();
        const double dz = z2 - z1;
        if (!(dx*dx + dy*dy + dz*dz > 1e-30))
        {
            error = QStringLiteral("%1 is a zero-length wire and cannot be meshed safely.").arg(context);
            return false;
        }
    }

    const QJsonArray surfaces = root.contains(QStringLiteral("surfaces")) ? root.value(QStringLiteral("surfaces")).toArray()
                                                                          : root.value(QStringLiteral("planes")).toArray();
    for (qsizetype i = 0; i < surfaces.size(); ++i)
    {
        if (!surfaces.at(i).isObject()) { error = QStringLiteral("surfaces[%1] must be a JSON object.").arg(i); return false; }
        const QJsonObject o = surfaces.at(i).toObject();
        const QString context = QStringLiteral("surfaces[%1]").arg(i);
        for (const QString &key : {QStringLiteral("cx"), QStringLiteral("cy"), QStringLiteral("cz"),
                                   QStringLiteral("yawDeg"), QStringLiteral("pitchDeg"), QStringLiteral("rollDeg")})
            if (!validateOptionalFiniteNumber(o, key, context, error)) return false;
        for (const QString &key : {QStringLiteral("radiusM"), QStringLiteral("widthM"), QStringLiteral("heightM"),
                                   QStringLiteral("focalLengthM"), QStringLiteral("feedWidthM"), QStringLiteral("meshHintM")})
            if (!validateOptionalNumberRange(o, key, 1e-12, 1e12, context, error)) return false;
        for (const QString &key : {QStringLiteral("innerRadiusM"), QStringLiteral("feedLengthM"),
                                   QStringLiteral("insetDepthM"), QStringLiteral("notchGapM")})
            if (!validateOptionalNumberRange(o, key, 0.0, 1e12, context, error)) return false;
        if (!validateOptionalIntegerRange(o, QStringLiteral("surfaceType"), 0, 5, context, error) ||
            !validateOptionalIntegerRange(o, QStringLiteral("orientation"), 0, 2, context, error)) return false;
        if (o.contains(QStringLiteral("innerRadiusM")) && o.contains(QStringLiteral("radiusM")) &&
            o.value(QStringLiteral("innerRadiusM")).toDouble() >= o.value(QStringLiteral("radiusM")).toDouble())
        {
            error = QStringLiteral("%1.innerRadiusM must be smaller than radiusM.").arg(context);
            return false;
        }
    }

    const QJsonArray dielectrics = root.value(QStringLiteral("dielectrics")).toArray();
    for (qsizetype i = 0; i < dielectrics.size(); ++i)
    {
        if (!dielectrics.at(i).isObject()) { error = QStringLiteral("dielectrics[%1] must be a JSON object.").arg(i); return false; }
        const QJsonObject o = dielectrics.at(i).toObject();
        const QString context = QStringLiteral("dielectrics[%1]").arg(i);
        for (const QString &key : {QStringLiteral("cx"), QStringLiteral("cy"), QStringLiteral("cz"),
                                   QStringLiteral("yawDeg"), QStringLiteral("pitchDeg"), QStringLiteral("rollDeg")})
            if (!validateOptionalFiniteNumber(o, key, context, error)) return false;
        for (const QString &key : {QStringLiteral("widthM"), QStringLiteral("heightM"), QStringLiteral("thicknessM")})
            if (!validateOptionalNumberRange(o, key, 1e-12, 1e12, context, error)) return false;
        if (!validateOptionalNumberRange(o, QStringLiteral("relativePermittivity"), 1.0, 1e9, context, error) ||
            !validateOptionalNumberRange(o, QStringLiteral("lossTangent"), 0.0, 1e6, context, error) ||
            !validateOptionalNumberRange(o, QStringLiteral("fieldFillFactor"), 0.0, 1.0, context, error) ||
            !validateOptionalIntegerRange(o, QStringLiteral("orientation"), 0, 2, context, error)) return false;
    }

    const QJsonArray feeds = root.value(QStringLiteral("feeds")).toArray();
    for (qsizetype i = 0; i < feeds.size(); ++i)
    {
        if (!feeds.at(i).isObject()) { error = QStringLiteral("feeds[%1] must be a JSON object.").arg(i); return false; }
        const QJsonObject o = feeds.at(i).toObject();
        const QString context = QStringLiteral("feeds[%1]").arg(i);
        for (const QString &key : {QStringLiteral("x"), QStringLiteral("y")})
            if (!validateRequiredFiniteNumber(o, key, context, error)) return false;
        for (const QString &key : {QStringLiteral("z"), QStringLiteral("voltageV"), QStringLiteral("phaseDeg")})
            if (!validateOptionalFiniteNumber(o, key, context, error)) return false;
        if (o.contains(QStringLiteral("referenceOhm")) &&
            !validateOptionalNumberRange(o, QStringLiteral("referenceOhm"), 1e-12, 1e12, context, error)) return false;
        if (o.contains(QStringLiteral("sourceOhm")) &&
            !validateOptionalNumberRange(o, QStringLiteral("sourceOhm"), 1e-12, 1e12, context, error)) return false;
    }

    const QJsonArray constraints = root.value(QStringLiteral("constraints")).toArray();
    for (qsizetype i = 0; i < constraints.size(); ++i)
    {
        if (!constraints.at(i).isObject()) { error = QStringLiteral("constraints[%1] must be a JSON object.").arg(i); return false; }
        const QJsonObject o = constraints.at(i).toObject();
        const QString context = QStringLiteral("constraints[%1]").arg(i);
        for (const QString &key : {QStringLiteral("fixedX"), QStringLiteral("fixedY"), QStringLiteral("fixedZ")})
            if (!validateOptionalFiniteNumber(o, key, context, error)) return false;
        if (!validateOptionalIntegerRange(o, QStringLiteral("type"), 0, 13, context, error) ||
            !validateOptionalIntegerRange(o, QStringLiteral("aKind"), 0, 3, context, error) ||
            !validateOptionalIntegerRange(o, QStringLiteral("bKind"), 0, 3, context, error) ||
            !validateOptionalIntegerRange(o, QStringLiteral("aAnchor"), 0, 2, context, error) ||
            !validateOptionalIntegerRange(o, QStringLiteral("bAnchor"), 0, 2, context, error) ||
            !validateOptionalNumberRange(o, QStringLiteral("valueM"), 0.0, 1e12, context, error) ||
            !validateOptionalNumberRange(o, QStringLiteral("valueDeg"), 0.0, 180.0, context, error) ||
            !validateOptionalIntegerRange(o, QStringLiteral("solveMode"), 0, 1, context, error) ||
            !validateOptionalIntegerRange(o, QStringLiteral("fixedMask"), 1, 7, context, error)) return false;
    }

    const QJsonArray groups = root.value(QStringLiteral("groups")).toArray();
    for (qsizetype i = 0; i < groups.size(); ++i)
    {
        if (!groups.at(i).isObject()) { error = QStringLiteral("groups[%1] must be a JSON object.").arg(i); return false; }
        const QJsonObject o = groups.at(i).toObject();
        if (o.contains(QStringLiteral("members")) && !o.value(QStringLiteral("members")).isArray())
        { error = QStringLiteral("groups[%1].members must be a JSON array.").arg(i); return false; }
        for (const QJsonValue &member : o.value(QStringLiteral("members")).toArray())
            if (!member.isString()) { error = QStringLiteral("groups[%1].members must only contain geometry IDs.").arg(i); return false; }
    }

    const QJsonArray individualVariables = root.value(QStringLiteral("individualDirectorVariables")).toArray();
    for (qsizetype i = 0; i < individualVariables.size(); ++i)
    {
        if (!individualVariables.at(i).isObject())
        { error = QStringLiteral("individualDirectorVariables[%1] must be a JSON object.").arg(i); return false; }
        const QJsonObject o = individualVariables.at(i).toObject();
        const QString context = QStringLiteral("individualDirectorVariables[%1]").arg(i);
        for (const QString &key : {QStringLiteral("lengthMin"), QStringLiteral("lengthMax"),
                                   QStringLiteral("positionMin"), QStringLiteral("positionMax")})
            if (!validateOptionalFiniteNumber(o, key, context, error)) return false;
    }
    return true;
}

} // namespace AntennaProjectDocument

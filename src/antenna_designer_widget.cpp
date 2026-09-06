#include "widgets/antenna_designer_widget.h"
#include "antenna_surface_geometry.h"
#include "antenna_validation_bench.h"
#include "numerical_em_solvers.h"
#include "pec_surface_mom.h"
#include "hybrid_wire_surface_mom.h"
#include "em_calculator_model.h"
#include "microstrip_models.h"
#include "widgets/field_profile_plot.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QFormLayout>
#include <QGraphicsEllipseItem>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QGridLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineF>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QProgressDialog>
#include <QResizeEvent>
#include <QPolygonF>
#include <QScrollArea>
#include <QScrollBar>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QVector3D>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace
{
constexpr double C0 = 299792458.0;
constexpr double MmPerM = 1000.0;
constexpr int AntennaProjectSchemaVersion = 19;
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

QDoubleSpinBox *numberBox(QWidget *parent, double value, double min, double max, int decimals = 6)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setDecimals(decimals);
    s->setRange(min, max);
    s->setValue(value);
    s->setKeyboardTracking(false);
    return s;
}

QPointF toSceneMm(const QPointF &pM)
{
    return QPointF(pM.x() * MmPerM, -pM.y() * MmPerM);
}

QPointF fromSceneMm(const QPointF &pMm)
{
    return QPointF(pMm.x() / MmPerM, -pMm.y() / MmPerM);
}

QString coordText(const QPointF &p)
{
    return QStringLiteral("(%1, %2)").arg(p.x(), 0, 'g', 6).arg(p.y(), 0, 'g', 6);
}

QPointF projectEditorPoint(const QPointF &xy, double z, int plane)
{
    if (plane == 1) return QPointF(xy.x(), z);      // XZ
    if (plane == 2) return QPointF(xy.y(), z);      // YZ
    return xy;                                       // XY
}

double hiddenEditorCoordinate(const QPointF &xy, double z, int plane)
{
    if (plane == 1) return xy.y();                   // XZ hides Y
    if (plane == 2) return xy.x();                   // YZ hides X
    return z;                                        // XY hides Z
}

void assignEditorProjection(QPointF &xy, double &z, int plane, const QPointF &projected)
{
    if (plane == 1) { xy.setX(projected.x()); z = projected.y(); return; }
    if (plane == 2) { xy.setY(projected.x()); z = projected.y(); return; }
    xy = projected;
}

void setHiddenEditorCoordinate(QPointF &xy, double &z, int plane, double hidden)
{
    if (plane == 1) { xy.setY(hidden); return; }
    if (plane == 2) { xy.setX(hidden); return; }
    z = hidden;
}

double distance3D(const QPointF &a, double az, const QPointF &b, double bz)
{
    const double dx=a.x()-b.x(), dy=a.y()-b.y(), dz=az-bz;
    return std::sqrt(dx*dx+dy*dy+dz*dz);
}

double segmentLength3D(const QPointF &a, double az, const QPointF &b, double bz)
{
    return distance3D(a,az,b,bz);
}

QString coordText3D(const QPointF &xy, double z)
{
    return QStringLiteral("(%1, %2, %3)").arg(xy.x(),0,'g',6).arg(xy.y(),0,'g',6).arg(z,0,'g',6);
}

QVector3D orientedPlanePoint(double cx, double cy, double cz, int orientation, double u, double v, double normal = 0.0)
{
    AntennaSurface::SurfaceSpec spec;
    spec.center={cx,cy,cz}; spec.baseOrientation=orientation;
    const auto q=AntennaSurface::pointFromLocal(spec,u,v,normal);
    return QVector3D(q.x,q.y,q.z);
}

QVector3D orientedPointEuler(double cx, double cy, double cz, int orientation, double yawDeg, double pitchDeg, double rollDeg, double u, double v, double normal = 0.0)
{
    AntennaSurface::SurfaceSpec spec;
    spec.center={cx,cy,cz}; spec.baseOrientation=orientation; spec.yawDeg=yawDeg; spec.pitchDeg=pitchDeg; spec.rollDeg=rollDeg;
    const auto q=AntennaSurface::pointFromLocal(spec,u,v,normal);
    return QVector3D(q.x,q.y,q.z);
}

QString surfaceKindText(int kind)
{
    return QString::fromLatin1(AntennaSurface::kindName(static_cast<AntennaSurface::SurfaceKind>(std::clamp(kind,0,5))));
}

int surfaceKindFromText(const QString &text)
{
    const QString t=text.trimmed().toLower();
    if(t.contains(QStringLiteral("disk"))||t.contains(QStringLiteral("annul"))) return 1;
    if(t.contains(QStringLiteral("cyl"))) return 2;
    if(t.contains(QStringLiteral("con"))) return 3;
    if(t.contains(QStringLiteral("parab"))||t.contains(QStringLiteral("dish"))) return 4;
    if(t.contains(QStringLiteral("inset"))||t.contains(QStringLiteral("microstrip"))) return 5;
    if(t.contains(QStringLiteral("rect"))||t.contains(QStringLiteral("sheet"))||t.contains(QStringLiteral("plane"))) return 0;
    return -1;
}

QString planeOrientationText(int orientation)
{
    if (orientation == 1) return QStringLiteral("XZ");
    if (orientation == 2) return QStringLiteral("YZ");
    return QStringLiteral("XY");
}

double pointSegmentDistance(const QPointF &p, const QPointF &a, const QPointF &b, QPointF *projection = nullptr)
{
    const QPointF ab = b - a;
    const double denom = QPointF::dotProduct(ab, ab);
    double t = 0.0;
    if (denom > 1e-24)
        t = std::clamp(QPointF::dotProduct(p - a, ab) / denom, 0.0, 1.0);
    const QPointF q = a + ab * t;
    if (projection) *projection = q;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

struct Matrix3
{
    double m[3][3]{{1.0,0.0,0.0},{0.0,1.0,0.0},{0.0,0.0,1.0}};
};

Matrix3 matrixMultiply(const Matrix3 &a, const Matrix3 &b)
{
    Matrix3 out{};
    for(int r=0;r<3;++r)for(int c=0;c<3;++c){out.m[r][c]=0.0;for(int k=0;k<3;++k)out.m[r][c]+=a.m[r][k]*b.m[k][c];}
    return out;
}

Matrix3 matrixTranspose(const Matrix3 &a)
{
    Matrix3 out{};for(int r=0;r<3;++r)for(int c=0;c<3;++c)out.m[r][c]=a.m[c][r];return out;
}

Matrix3 axisRotationMatrix(int axis,double angleDeg)
{
    const double a=angleDeg*NumericalEM::Pi/180.0,c=std::cos(a),q=std::sin(a);Matrix3 r{};
    if(axis==0){r.m[1][1]=c;r.m[1][2]=-q;r.m[2][1]=q;r.m[2][2]=c;}
    else if(axis==1){r.m[0][0]=c;r.m[0][2]=q;r.m[2][0]=-q;r.m[2][2]=c;}
    else{r.m[0][0]=c;r.m[0][1]=-q;r.m[1][0]=q;r.m[1][1]=c;}
    return r;
}

Matrix3 vectorRotationMatrix(NumericalEM::Vec3 axis,double angleDeg)
{
    const double n=NumericalEM::norm(axis);if(n<=1e-15)return Matrix3{};axis=axis*(1.0/n);
    const double a=angleDeg*NumericalEM::Pi/180.0,c=std::cos(a),s=std::sin(a),t=1.0-c;Matrix3 r{};
    r.m[0][0]=t*axis.x*axis.x+c;      r.m[0][1]=t*axis.x*axis.y-s*axis.z;r.m[0][2]=t*axis.x*axis.z+s*axis.y;
    r.m[1][0]=t*axis.x*axis.y+s*axis.z;r.m[1][1]=t*axis.y*axis.y+c;      r.m[1][2]=t*axis.y*axis.z-s*axis.x;
    r.m[2][0]=t*axis.x*axis.z-s*axis.y;r.m[2][1]=t*axis.y*axis.z+s*axis.x;r.m[2][2]=t*axis.z*axis.z+c;
    return r;
}

Matrix3 eulerMatrix(double yawDeg,double pitchDeg,double rollDeg)
{
    return matrixMultiply(axisRotationMatrix(2,yawDeg),matrixMultiply(axisRotationMatrix(1,pitchDeg),axisRotationMatrix(0,rollDeg)));
}

NumericalEM::Vec3 matrixVector(const Matrix3 &m,const NumericalEM::Vec3 &v)
{
    return {m.m[0][0]*v.x+m.m[0][1]*v.y+m.m[0][2]*v.z,
            m.m[1][0]*v.x+m.m[1][1]*v.y+m.m[1][2]*v.z,
            m.m[2][0]*v.x+m.m[2][1]*v.y+m.m[2][2]*v.z};
}

NumericalEM::Vec3 rotateAroundAxis(const NumericalEM::Vec3 &point,const NumericalEM::Vec3 &pivot,int axis,double angleDeg)
{
    return pivot+matrixVector(axisRotationMatrix(std::clamp(axis,0,2),angleDeg),point-pivot);
}

NumericalEM::Vec3 rotateAroundVector(const NumericalEM::Vec3 &point,const NumericalEM::Vec3 &pivot,const NumericalEM::Vec3 &axis,double angleDeg)
{
    return pivot+matrixVector(vectorRotationMatrix(axis,angleDeg),point-pivot);
}

void matrixToEuler(const Matrix3 &r,double &yawDeg,double &pitchDeg,double &rollDeg)
{
    const double sp=std::clamp(-r.m[2][0],-1.0,1.0);const double pitch=std::asin(sp);const double cp=std::cos(pitch);double yaw=0.0,roll=0.0;
    if(std::abs(cp)>1e-9){yaw=std::atan2(r.m[1][0],r.m[0][0]);roll=std::atan2(r.m[2][1],r.m[2][2]);}
    else{yaw=std::atan2(-r.m[0][1],r.m[1][1]);roll=0.0;}
    yawDeg=yaw*180.0/NumericalEM::Pi;pitchDeg=pitch*180.0/NumericalEM::Pi;rollDeg=roll*180.0/NumericalEM::Pi;
}

void rotateEulerWorldAxis(double &yawDeg,double &pitchDeg,double &rollDeg,int axis,double angleDeg)
{
    const Matrix3 combined=matrixMultiply(axisRotationMatrix(std::clamp(axis,0,2),angleDeg),eulerMatrix(yawDeg,pitchDeg,rollDeg));
    matrixToEuler(combined,yawDeg,pitchDeg,rollDeg);
}

Matrix3 surfaceFrameMatrix(int orientation,double yawDeg,double pitchDeg,double rollDeg)
{
    AntennaSurface::SurfaceSpec spec;spec.baseOrientation=std::clamp(orientation,0,2);spec.yawDeg=yawDeg;spec.pitchDeg=pitchDeg;spec.rollDeg=rollDeg;const auto f=AntennaSurface::frameAxes(spec);Matrix3 r{};
    r.m[0][0]=f.u.x;r.m[1][0]=f.u.y;r.m[2][0]=f.u.z;
    r.m[0][1]=f.v.x;r.m[1][1]=f.v.y;r.m[2][1]=f.v.z;
    r.m[0][2]=f.normal.x;r.m[1][2]=f.normal.y;r.m[2][2]=f.normal.z;return r;
}

void mirrorSurfaceEuler(int orientation,double yawDeg,double pitchDeg,double rollDeg,int axis,double &outYaw,double &outPitch,double &outRoll)
{
    Matrix3 reflection{};reflection.m[axis][axis]=-1.0;
    Matrix3 localNormalFlip{};localNormalFlip.m[2][2]=-1.0;
    const Matrix3 worldFrame=surfaceFrameMatrix(orientation,yawDeg,pitchDeg,rollDeg);
    const Matrix3 mirroredFrame=matrixMultiply(reflection,matrixMultiply(worldFrame,localNormalFlip));
    const Matrix3 base=surfaceFrameMatrix(orientation,0.0,0.0,0.0);
    const Matrix3 eulerOnly=matrixMultiply(mirroredFrame,matrixTranspose(base));
    matrixToEuler(eulerOnly,outYaw,outPitch,outRoll);
}
}

class AntennaDesignerWidget::Canvas final : public QGraphicsView
{
public:
    enum class Tool { Select, AddWire, AddFeed, Delete };

    explicit Canvas(QWidget *parent = nullptr)
        : QGraphicsView(parent), m_scene(new QGraphicsScene(this))
    {
        setScene(m_scene);
        setRenderHint(QPainter::Antialiasing, true);
        setDragMode(QGraphicsView::RubberBandDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorViewCenter);
        // 2D CAD overlays are screen-space widgets while the scene/grid are world-space.
        // Full viewport updates prevent QGraphicsView scroll blits from leaving stale copies
        // of the background or overlay-painted graphics while panning.
        setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
        setCacheMode(QGraphicsView::CacheNone);
        setMouseTracking(true);
        setMinimumSize(520, 420);
        setFocusPolicy(Qt::StrongFocus);
        m_scene->setSceneRect(-500, -350, 1000, 700);

        // v5.8.8: the active construction tool is exposed directly inside the viewport.
        // The outer combo remains only as a hidden shared state holder so 2D and 3D stay synchronized.
        // Parent all screen-space controls to the QGraphicsView itself, not to viewport().
        // QAbstractScrollArea may scroll children of viewport(), which was the cause of the
        // duplicated/misplaced Grid and Tool controls seen after a middle-button pan.
        m_toolCombo = new QComboBox(this);
        m_toolCombo->addItems({QStringLiteral("Select / transform"),
                               QStringLiteral("Draw wire"),
                               QStringLiteral("Place feed"),
                               QStringLiteral("Delete near"),
                               QStringLiteral("Draw PEC rectangle (3D)"),
                               QStringLiteral("Draw substrate (3D)"),
                               QStringLiteral("Measure distance / angle (3D)"),
                               QStringLiteral("Pick custom pivot (3D)")});
        m_toolCombo->setToolTip(QStringLiteral("Construction tool. 3D-only tools automatically switch to the 3D editor."));
        QObject::connect(m_toolCombo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(!m_syncingToolCombo && toolChangeRequested)toolChangeRequested(index);});

        m_gridPanel = new QFrame(this);
        m_gridPanel->setFrameShape(QFrame::StyledPanel);m_gridPanel->setAutoFillBackground(true);
        auto *gridLayout=new QHBoxLayout(m_gridPanel);gridLayout->setContentsMargins(5,2,5,2);gridLayout->setSpacing(4);
        gridLayout->addWidget(new QLabel(QStringLiteral("Grid"),m_gridPanel));
        m_gridSpin=new QDoubleSpinBox(m_gridPanel);m_gridSpin->setRange(1.0,1e5);m_gridSpin->setDecimals(2);m_gridSpin->setSuffix(QStringLiteral(" mm"));m_gridSpin->setKeyboardTracking(false);m_gridSpin->setValue(m_gridMm);m_gridSpin->setMinimumWidth(108);m_gridSpin->setMaximumWidth(120);gridLayout->addWidget(m_gridSpin);
        QObject::connect(m_gridSpin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double value){if(m_syncingGridEditor)return;m_gridMm=std::max(1.0,value);syncStatusLabel();viewport()->update();if(gridChangeRequested)gridChangeRequested(value);});

        m_viewPanel=new QFrame(this);m_viewPanel->setFrameShape(QFrame::StyledPanel);m_viewPanel->setAutoFillBackground(true);
        auto *viewLayout=new QHBoxLayout(m_viewPanel);viewLayout->setContentsMargins(3,2,3,2);viewLayout->setSpacing(3);
        const std::array<QString,3> planeNames{QStringLiteral("XY"),QStringLiteral("XZ"),QStringLiteral("YZ")};
        for(int plane=0;plane<3;++plane)
        {
            auto *button=new QToolButton(m_viewPanel);button->setText(planeNames[static_cast<std::size_t>(plane)]);button->setCheckable(true);button->setAutoExclusive(true);button->setFixedSize(36,24);m_planeButtons[static_cast<std::size_t>(plane)]=button;viewLayout->addWidget(button);
            QObject::connect(button,&QToolButton::clicked,this,[this,plane]{m_editPlane=plane;syncPlaneButtons();syncStatusLabel();viewport()->update();if(editPlaneChangeRequested)editPlaneChangeRequested(plane);});
        }
        m_fitButton=new QToolButton(m_viewPanel);m_fitButton->setText(QStringLiteral("Fit"));m_fitButton->setFixedSize(44,24);m_fitButton->setToolTip(QStringLiteral("Fit the complete 2D geometry in the current orthographic projection."));viewLayout->addWidget(m_fitButton);
        QObject::connect(m_fitButton,&QToolButton::clicked,this,[this]{fitGeometry();});

        m_statusLabel=new QLabel(this);m_statusLabel->setForegroundRole(QPalette::PlaceholderText);m_statusLabel->setAttribute(Qt::WA_TransparentForMouseEvents,true);
        syncPlaneButtons();syncStatusLabel();layoutViewportControls();
    }

    QGraphicsScene *drawingScene() const { return m_scene; }
    void setTool(Tool tool)
    {
        m_tool = tool;
        setDragMode(tool == Tool::Select ? QGraphicsView::RubberBandDrag : QGraphicsView::NoDrag);
        setCursor(tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    }
    void setGridMm(double mm) { m_gridMm = std::max(1.0, mm); syncGridEditor(); syncStatusLabel(); viewport()->update(); }
    void setEditContext(int plane, double coordinateM, double gridMm)
    {
        m_editPlane = std::clamp(plane, 0, 2);
        m_planeCoordinateM = coordinateM;
        m_gridMm = std::max(1.0, gridMm);
        syncGridEditor();
        syncPlaneButtons();
        syncStatusLabel();
        viewport()->update();
    }
    void setToolSelectorIndex(int index)
    {
        if(!m_toolCombo)return;m_syncingToolCombo=true;m_toolCombo->setCurrentIndex(std::clamp(index,0,m_toolCombo->count()-1));m_syncingToolCombo=false;
    }
    double gridMm() const { return m_gridMm; }

    void ensureNavigationBounds()
    {
        QRectF content = m_scene->itemsBoundingRect();
        if (!content.isValid() || content.width() < 1.0 || content.height() < 1.0)
            content = QRectF(-500, -350, 1000, 700);
        content = content.united(QRectF(-500, -500, 1000, 1000));
        const double margin = std::max({1000.0, content.width(), content.height()});
        m_scene->setSceneRect(content.adjusted(-margin, -margin, margin, margin));
    }

    void centerOrigin()
    {
        ensureNavigationBounds();
        centerOn(QPointF(0.0, 0.0));
    }

    std::function<void(int)> editPlaneChangeRequested;
    std::function<void(const QPointF &, const QPointF &)> wireCreated;
    std::function<void(const QPointF &)> feedCreated;
    std::function<void(const QPointF &)> deleteRequested;
    std::function<void(const QPointF &, double, const QPointF &)> nodeMoveRequested;
    std::function<void(const std::vector<int> &, const std::vector<int> &, const std::vector<int> &, const std::vector<int> &)> objectDeleteRequested;
    std::function<void(const std::vector<int> &, const std::vector<int> &, const std::vector<int> &, const std::vector<int> &, const NumericalEM::Vec3 &)> objectTranslateRequested;
    std::function<void(int)> toolChangeRequested;
    std::function<void(double)> gridChangeRequested;
    std::function<void(int,const NumericalEM::Vec3 &)> primitiveSetupRequested;
    std::function<void(const std::vector<int> &, const std::vector<int> &, const std::vector<int> &,
                       const std::vector<int> &, bool, const NumericalEM::Vec3 &)> constraintSetupRequested;

    void fitGeometry()
    {
        QRectF r = m_scene->itemsBoundingRect();
        if (!r.isValid() || r.width() < 1.0 || r.height() < 1.0)
            r = QRectF(-500, -350, 1000, 700);
        r = r.adjusted(-100, -100, 100, 100);
        if (r.width() < 1000) r.adjust(-0.5 * (1000 - r.width()), 0, 0.5 * (1000 - r.width()), 0);
        if (r.height() < 700) r.adjust(0, -0.5 * (700 - r.height()), 0, 0.5 * (700 - r.height()));
        ensureNavigationBounds();
        fitInView(r, Qt::KeepAspectRatio);
    }

    void syncGridEditor()
    {
        if(!m_gridSpin)return;m_syncingGridEditor=true;m_gridSpin->setValue(m_gridMm);m_syncingGridEditor=false;
    }

    QString hiddenAxisName() const
    {
        return m_editPlane==0 ? QStringLiteral("Z") : (m_editPlane==1 ? QStringLiteral("Y") : QStringLiteral("X"));
    }

    void syncPlaneButtons()
    {
        for(int plane=0;plane<3;++plane)if(m_planeButtons[static_cast<std::size_t>(plane)])m_planeButtons[static_cast<std::size_t>(plane)]->setChecked(plane==m_editPlane);
    }

    void syncStatusLabel()
    {
        if(!m_statusLabel)return;
        m_statusLabel->setText(QStringLiteral("Construction: %1 = %2 m  ·  snap %3 mm")
                                   .arg(hiddenAxisName()).arg(m_planeCoordinateM,0,'g',5).arg(m_gridMm,0,'g',5));
    }

    void layoutViewportControls()
    {
        const QPoint vp=viewport()->mapTo(this,QPoint(0,0));const int vw=viewport()->width(),vh=viewport()->height();
        if(m_toolCombo)m_toolCombo->setGeometry(vp.x()+std::max(8,(vw-230)/2),vp.y()+8,230,27);
        if(m_gridPanel)m_gridPanel->setGeometry(vp.x()+10,vp.y()+std::max(8,vh-41),158,31);
        if(m_viewPanel)m_viewPanel->setGeometry(vp.x()+std::max(8,vw-178),vp.y()+8,168,30);
        if(m_statusLabel)m_statusLabel->setGeometry(vp.x()+10,vp.y()+8,std::max(80,(vw-230)/2-18),24);
        if(m_statusLabel)m_statusLabel->raise();if(m_toolCombo)m_toolCombo->raise();if(m_gridPanel)m_gridPanel->raise();if(m_viewPanel)m_viewPanel->raise();
    }

    QGraphicsItem *interactiveItemAt(const QPoint &viewportPoint) const
    {
        for(QGraphicsItem *item:items(viewportPoint))
        {
            const QString kind=item->data(0).toString();
            if(kind==QStringLiteral("node")||item->flags().testFlag(QGraphicsItem::ItemIsSelectable))return item;
        }
        return nullptr;
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        // All HUD controls are real widgets. Only the QGraphicsScene is painted here, which
        // keeps panning/zooming free of stale duplicated overlay pixels.
        QGraphicsView::paintEvent(event);
    }

    void drawBackground(QPainter *painter, const QRectF &rect) override
    {
        painter->save();
        const QPalette pal = palette();
        painter->fillRect(rect, pal.color(QPalette::Base));

        const double step = std::max(1.0, m_gridMm);
        const int left = static_cast<int>(std::floor(rect.left() / step));
        const int right = static_cast<int>(std::ceil(rect.right() / step));
        const int top = static_cast<int>(std::floor(rect.top() / step));
        const int bottom = static_cast<int>(std::ceil(rect.bottom() / step));

        QColor grid = pal.color(QPalette::Mid);
        grid.setAlpha(90);
        painter->setPen(QPen(grid, 0));
        for (int ix = left; ix <= right; ++ix)
        {
            const double x = ix * step;
            painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        }
        for (int iy = top; iy <= bottom; ++iy)
        {
            const double y = iy * step;
            painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        }

        QColor axis = pal.color(QPalette::Highlight);
        axis.setAlpha(180);
        painter->setPen(QPen(axis, 0));
        painter->drawLine(QPointF(0, rect.top()), QPointF(0, rect.bottom()));
        painter->drawLine(QPointF(rect.left(), 0), QPointF(rect.right(), 0));
        painter->restore();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QGraphicsView::resizeEvent(event);layoutViewportControls();
    }

    void scrollContentsBy(int dx,int dy) override
    {
        QGraphicsView::scrollContentsBy(dx,dy);
        // Force a clean redraw of world-space graphics after each scroll step. Screen-space
        // controls are children of the view and therefore never participate in the scroll.
        viewport()->update();layoutViewportControls();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        setFocus(Qt::MouseFocusReason);
        if (event->button() == Qt::MiddleButton)
        {
            m_panning = true;
            m_panStart = event->position();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        if (event->button() == Qt::RightButton)
        {
            QMenu menu(this);
            QGraphicsItem *item = interactiveItemAt(event->position().toPoint());
            if(item && item->flags().testFlag(QGraphicsItem::ItemIsSelectable))
            {
                if(!(event->modifiers()&Qt::ControlModifier) && !item->isSelected())m_scene->clearSelection();
                item->setSelected(true);
            }
            const auto selected = m_scene->selectedItems();
            const bool nodePicked=item && item->data(0).toString()==QStringLiteral("node");
            NumericalEM::Vec3 pickedNode{};
            if(nodePicked)
            {
                const QPointF q=item->data(2).toPointF();const double hidden=item->data(3).toDouble();
                if(m_editPlane==0)pickedNode={q.x(),q.y(),hidden};
                else if(m_editPlane==1)pickedNode={q.x(),hidden,q.y()};
                else pickedNode={hidden,q.x(),q.y()};
            }
            if(!selected.empty() || nodePicked)
            {
                QAction *constraint=menu.addAction(QStringLiteral("Add constraint…"));
                constraint->setToolTip(QStringLiteral("Open Geometry → Constraints with the clicked/selected reference(s) preloaded."));
                QObject::connect(constraint,&QAction::triggered,this,[this,nodePicked,pickedNode]{
                    std::set<int>wires,feeds,planes,dielectrics;
                    for(QGraphicsItem *it:m_scene->selectedItems())
                    {
                        const QString kind=it->data(0).toString();const int idx=it->data(1).toInt();
                        if(kind==QStringLiteral("wire"))wires.insert(idx);else if(kind==QStringLiteral("feed"))feeds.insert(idx);
                        else if(kind==QStringLiteral("plane"))planes.insert(idx);else if(kind==QStringLiteral("dielectric"))dielectrics.insert(idx);
                    }
                    if(constraintSetupRequested)constraintSetupRequested(std::vector<int>(wires.begin(),wires.end()),std::vector<int>(feeds.begin(),feeds.end()),std::vector<int>(planes.begin(),planes.end()),std::vector<int>(dielectrics.begin(),dielectrics.end()),nodePicked,pickedNode);
                });
                if(!selected.empty())
                {
                    QAction *del=menu.addAction(QStringLiteral("Delete selected…"));
                    QObject::connect(del,&QAction::triggered,this,[this]{
                        std::set<int>wires,feeds,planes,dielectrics;
                        for(QGraphicsItem *it:m_scene->selectedItems())
                        {
                            const QString kind=it->data(0).toString();const int idx=it->data(1).toInt();
                            if(kind==QStringLiteral("wire"))wires.insert(idx);else if(kind==QStringLiteral("feed"))feeds.insert(idx);
                            else if(kind==QStringLiteral("plane"))planes.insert(idx);else if(kind==QStringLiteral("dielectric"))dielectrics.insert(idx);
                        }
                        if(objectDeleteRequested)objectDeleteRequested(std::vector<int>(wires.begin(),wires.end()),std::vector<int>(feeds.begin(),feeds.end()),std::vector<int>(planes.begin(),planes.end()),std::vector<int>(dielectrics.begin(),dielectrics.end()));
                    });
                }
                menu.addSeparator();
            }
            QMenu *create=menu.addMenu(QStringLiteral("Create"));
            auto addTool=[&](const QString&label,int index){QAction *a=create->addAction(label);QObject::connect(a,&QAction::triggered,this,[this,index]{if(toolChangeRequested)toolChangeRequested(index);});};
            addTool(QStringLiteral("Wire"),1);addTool(QStringLiteral("Feed"),2);
            QMenu *primitive=create->addMenu(QStringLiteral("3D primitive…"));
            const std::array<QString,12> primitiveNames{{QStringLiteral("Circular loop"),QStringLiteral("Circular arc"),QStringLiteral("Rectangular loop"),QStringLiteral("Helix / solenoid wire"),QStringLiteral("Planar spiral"),QStringLiteral("Meander / serpentine wire"),QStringLiteral("Rectangular plane / sheet"),QStringLiteral("Disk / annular sheet"),QStringLiteral("Cylindrical shell"),QStringLiteral("Conical shell"),QStringLiteral("Parabolic reflector / dish"),QStringLiteral("Dielectric substrate / slab")}};
            const QPointF pm=fromSceneMm(mapToScene(event->position().toPoint()));
            NumericalEM::Vec3 primitivePoint{};if(m_editPlane==0)primitivePoint={pm.x(),pm.y(),m_planeCoordinateM};else if(m_editPlane==1)primitivePoint={pm.x(),m_planeCoordinateM,pm.y()};else primitivePoint={m_planeCoordinateM,pm.x(),pm.y()};
            for(int type=0;type<static_cast<int>(primitiveNames.size());++type){QAction *a=primitive->addAction(primitiveNames[static_cast<std::size_t>(type)]);QObject::connect(a,&QAction::triggered,this,[this,type,primitivePoint]{if(primitiveSetupRequested)primitiveSetupRequested(type,primitivePoint);});}
            QAction *fit=menu.addAction(QStringLiteral("Fit view"));QObject::connect(fit,&QAction::triggered,this,[this]{fitGeometry();});
            menu.exec(event->globalPosition().toPoint());event->accept();return;
        }
        if (event->button() == Qt::LeftButton && m_tool == Tool::Select)
        {
            QGraphicsItem *item = interactiveItemAt(event->position().toPoint());
            if (item && item->data(0).toString() == QStringLiteral("node"))
            {
                m_scene->clearSelection();
                m_dragNodeItem = item;
                m_dragNodeStart = item->data(2).toPointF();
                m_dragNodeHidden = item->data(3).toDouble();
                item->setPos(snapped(mapToScene(event->position().toPoint())));
                event->accept();
                return;
            }
            if(item && item->flags().testFlag(QGraphicsItem::ItemIsSelectable))
            {
                const bool additive=(event->modifiers()&Qt::ControlModifier);
                if(additive)
                {
                    item->setSelected(!item->isSelected());
                    event->accept();
                    return;
                }
                if(!item->isSelected())m_scene->clearSelection();
                item->setSelected(true);

                // Direct 2D CAD manipulation: a selected wire/feed/PEC/dielectric can be dragged
                // with the left mouse button. The preview remains in scene coordinates while the
                // committed delta is converted back to the active XY/XZ/YZ construction plane.
                m_dragObjectWires.clear();m_dragObjectFeeds.clear();m_dragObjectPlanes.clear();m_dragObjectDielectrics.clear();
                for(QGraphicsItem *it:m_scene->selectedItems())
                {
                    const QString kind=it->data(0).toString();const int idx=it->data(1).toInt();
                    if(kind==QStringLiteral("wire"))m_dragObjectWires.push_back(idx);
                    else if(kind==QStringLiteral("feed"))m_dragObjectFeeds.push_back(idx);
                    else if(kind==QStringLiteral("plane"))m_dragObjectPlanes.push_back(idx);
                    else if(kind==QStringLiteral("dielectric"))m_dragObjectDielectrics.push_back(idx);
                }
                if(!m_dragObjectWires.empty()||!m_dragObjectFeeds.empty()||!m_dragObjectPlanes.empty()||!m_dragObjectDielectrics.empty())
                {
                    m_draggingObjects=true;
                    m_objectDragStartScene=mapToScene(event->position().toPoint());
                    m_objectDragDeltaScene=QPointF();
                    setCursor(Qt::ClosedHandCursor);
                }
                event->accept();
                return;
            }
            QGraphicsView::mousePressEvent(event);
            return;
        }
        if (event->button() != Qt::LeftButton)
        {
            QGraphicsView::mousePressEvent(event);
            return;
        }

        const QPointF p = snapped(mapToScene(event->position().toPoint()));
        if (m_tool == Tool::AddWire)
        {
            m_wireStart = p;
            m_drawingWire = true;
        }
        else if (m_tool == Tool::AddFeed && feedCreated)
        {
            feedCreated(fromSceneMm(p));
            if(toolChangeRequested)toolChangeRequested(0);
        }
        else if (m_tool == Tool::Delete && deleteRequested)
            deleteRequested(fromSceneMm(p));
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_panning && (event->buttons() & Qt::MiddleButton))
        {
            const QPointF delta = event->position() - m_panStart;
            m_panStart = event->position();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - qRound(delta.x()));
            verticalScrollBar()->setValue(verticalScrollBar()->value() - qRound(delta.y()));
            event->accept();
            return;
        }
        if (m_dragNodeItem && (event->buttons() & Qt::LeftButton))
        {
            m_dragNodeItem->setPos(snapped(mapToScene(event->position().toPoint())));
            event->accept();
            return;
        }
        if(m_draggingObjects && (event->buttons() & Qt::LeftButton))
        {
            m_objectDragDeltaScene=mapToScene(event->position().toPoint())-m_objectDragStartScene;
            // A whole-object drag is deliberately free rather than grid-quantized. Endpoint/node
            // editing keeps the grid snap, while this mode preserves the exact grab offset.
            for(QGraphicsItem *it:m_scene->selectedItems())it->setPos(m_objectDragDeltaScene);
            event->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_panning && event->button() == Qt::MiddleButton)
        {
            m_panning = false;
            setCursor(m_tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && m_dragNodeItem)
        {
            const QPointF endScene = snapped(mapToScene(event->position().toPoint()));
            const QPointF startM = m_dragNodeStart;
            m_dragNodeItem = nullptr;
            if (nodeMoveRequested)
                nodeMoveRequested(startM, m_dragNodeHidden, fromSceneMm(endScene));
            event->accept();
            return;
        }
        if(event->button()==Qt::LeftButton && m_draggingObjects)
        {
            m_draggingObjects=false;
            setCursor(m_tool==Tool::Select?Qt::ArrowCursor:Qt::CrossCursor);
            const QPointF deltaM=fromSceneMm(m_objectDragDeltaScene);
            NumericalEM::Vec3 delta{};
            if(m_editPlane==0)delta={deltaM.x(),deltaM.y(),0.0};
            else if(m_editPlane==1)delta={deltaM.x(),0.0,deltaM.y()};
            else delta={0.0,deltaM.x(),deltaM.y()};
            const bool moved=std::hypot(delta.x,delta.y)>1e-12||std::abs(delta.z)>1e-12;
            if(moved && objectTranslateRequested)
                objectTranslateRequested(m_dragObjectWires,m_dragObjectFeeds,m_dragObjectPlanes,m_dragObjectDielectrics,delta);
            else
                for(QGraphicsItem *it:m_scene->selectedItems())it->setPos(QPointF());
            m_dragObjectWires.clear();m_dragObjectFeeds.clear();m_dragObjectPlanes.clear();m_dragObjectDielectrics.clear();m_objectDragDeltaScene=QPointF();
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && m_tool == Tool::AddWire && m_drawingWire)
        {
            m_drawingWire = false;
            const QPointF end = snapped(mapToScene(event->position().toPoint()));
            if (QLineF(m_wireStart, end).length() >= 1.0 && wireCreated)
            {
                wireCreated(fromSceneMm(m_wireStart), fromSceneMm(end));
                if(toolChangeRequested)toolChangeRequested(0);
            }
            return;
        }
        QGraphicsView::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if(event->key()==Qt::Key_Delete||event->key()==Qt::Key_Backspace)
        {
            std::set<int>wires,feeds,planes,dielectrics;
            for(QGraphicsItem *item:m_scene->selectedItems())
            {
                const QString kind=item->data(0).toString();const int index=item->data(1).toInt();
                if(kind==QStringLiteral("wire"))wires.insert(index);else if(kind==QStringLiteral("feed"))feeds.insert(index);else if(kind==QStringLiteral("plane"))planes.insert(index);else if(kind==QStringLiteral("dielectric"))dielectrics.insert(index);
            }
            if(!wires.empty()||!feeds.empty()||!planes.empty()||!dielectrics.empty())
            {
                if(objectDeleteRequested)objectDeleteRequested(std::vector<int>(wires.begin(),wires.end()),std::vector<int>(feeds.begin(),feeds.end()),std::vector<int>(planes.begin(),planes.end()),std::vector<int>(dielectrics.begin(),dielectrics.end()));
                event->accept();return;
            }
        }
        QGraphicsView::keyPressEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        scale(factor, factor);
        event->accept();
    }

private:
    QPointF snapped(const QPointF &p) const
    {
        const double g = std::max(1.0, m_gridMm);
        return QPointF(std::round(p.x() / g) * g, std::round(p.y() / g) * g);
    }

    QGraphicsScene *m_scene = nullptr;
    Tool m_tool = Tool::Select;
    double m_gridMm = 50.0;
    int m_editPlane = 0;
    double m_planeCoordinateM = 0.0;
    bool m_drawingWire = false;
    QPointF m_wireStart;
    QGraphicsItem *m_dragNodeItem = nullptr;
    QPointF m_dragNodeStart;
    double m_dragNodeHidden = 0.0;
    bool m_panning = false;
    QPointF m_panStart;
    bool m_draggingObjects=false;
    QPointF m_objectDragStartScene,m_objectDragDeltaScene;
    std::vector<int> m_dragObjectWires,m_dragObjectFeeds,m_dragObjectPlanes,m_dragObjectDielectrics;
    QComboBox *m_toolCombo=nullptr;
    QFrame *m_gridPanel=nullptr;
    QDoubleSpinBox *m_gridSpin=nullptr;
    QFrame *m_viewPanel=nullptr;
    std::array<QToolButton*,3> m_planeButtons{{nullptr,nullptr,nullptr}};
    QToolButton *m_fitButton=nullptr;
    QLabel *m_statusLabel=nullptr;
    bool m_syncingToolCombo=false,m_syncingGridEditor=false;
};

class AntennaDesignerWidget::Geometry3DView final : public QWidget
{
public:
    enum class Tool { Select, AddWire, AddFeed, Delete, AddRectPEC, AddDielectric, Measure, PickPivot };

    explicit Geometry3DView(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(520, 420);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);

        // Fixed viewport overlay: camera Euler angles are stacked vertically below the ViewCube
        // to keep the right side narrow on laptop displays.
        m_viewAnglesPanel = new QFrame(this);
        m_viewAnglesPanel->setFrameShape(QFrame::StyledPanel);
        m_viewAnglesPanel->setAutoFillBackground(true);
        auto *angleLayout = new QVBoxLayout(m_viewAnglesPanel);
        angleLayout->setContentsMargins(4,3,4,3);
        angleLayout->setSpacing(2);
        const std::array<QString,3> axisNames{QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("Z")};
        for(int axis=0;axis<3;++axis)
        {
            auto *row=new QWidget(m_viewAnglesPanel);auto *rowLayout=new QHBoxLayout(row);rowLayout->setContentsMargins(0,0,0,0);rowLayout->setSpacing(3);
            auto *label=new QLabel(axisNames[static_cast<std::size_t>(axis)],row);
            label->setToolTip(QStringLiteral("Editable world-to-view Euler rotation around the global %1 axis.").arg(axisNames[static_cast<std::size_t>(axis)]));
            rowLayout->addWidget(label);
            auto *spin=new QDoubleSpinBox(row);
            spin->setRange(-360.0,360.0);spin->setDecimals(1);spin->setSingleStep(5.0);spin->setSuffix(QStringLiteral("°"));spin->setKeyboardTracking(false);spin->setMaximumWidth(72);
            m_viewAngleSpin[static_cast<std::size_t>(axis)]=spin;rowLayout->addWidget(spin);angleLayout->addWidget(row);
            QObject::connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,axis](double value){
                if(m_syncingOverlayEditors)return;
                if(axis==0)m_viewRotXDeg=value;else if(axis==1)m_viewRotYDeg=value;else m_viewRotZDeg=value;
                update();
            });
        }

        // v5.8.8: expose the active construction tool in the viewport itself.
        m_toolPanel = new QFrame(this);m_toolPanel->setFrameShape(QFrame::StyledPanel);m_toolPanel->setAutoFillBackground(true);
        auto *toolLayout=new QHBoxLayout(m_toolPanel);toolLayout->setContentsMargins(5,2,5,2);toolLayout->setSpacing(4);
        toolLayout->addWidget(new QLabel(QStringLiteral("Tool"),m_toolPanel));
        m_toolCombo=new QComboBox(m_toolPanel);m_toolCombo->addItems({QStringLiteral("Select / transform"),QStringLiteral("Draw wire"),QStringLiteral("Place feed"),QStringLiteral("Delete near"),QStringLiteral("Draw PEC rectangle"),QStringLiteral("Draw substrate"),QStringLiteral("Measure distance / angle"),QStringLiteral("Pick custom pivot")});m_toolCombo->setMinimumWidth(205);toolLayout->addWidget(m_toolCombo);
        QObject::connect(m_toolCombo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(!m_syncingToolCombo && editorToolRequested)editorToolRequested(index);});

        // Grid/snap is placed immediately to the left of the discreet navigation arrows.
        m_gridPanel = new QFrame(this);m_gridPanel->setFrameShape(QFrame::StyledPanel);m_gridPanel->setAutoFillBackground(true);
        auto *gridLayout=new QHBoxLayout(m_gridPanel);gridLayout->setContentsMargins(5,2,5,2);gridLayout->setSpacing(4);gridLayout->addWidget(new QLabel(QStringLiteral("Grid"),m_gridPanel));
        m_gridSpin=new QDoubleSpinBox(m_gridPanel);m_gridSpin->setRange(1.0,1e5);m_gridSpin->setDecimals(2);m_gridSpin->setSuffix(QStringLiteral(" mm"));m_gridSpin->setKeyboardTracking(false);m_gridSpin->setMaximumWidth(120);m_gridSpin->setMinimumWidth(108);gridLayout->addWidget(m_gridSpin);
        QObject::connect(m_gridSpin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double value){if(m_syncingGridEditor)return;m_gridM=std::max(1e-6,value/MmPerM);syncConstructionPlaneEditors();update();if(gridChangeRequested)gridChangeRequested(value);});

        // A short click pans once immediately. Holding for half a second starts auto-repeat.
        m_panHoldDelay=new QTimer(this);m_panHoldDelay->setSingleShot(true);m_panHoldDelay->setInterval(500);
        m_panRepeatTimer=new QTimer(this);m_panRepeatTimer->setInterval(80);
        QObject::connect(m_panHoldDelay,&QTimer::timeout,this,[this]{if(m_panHoldDirection>=0){panViewDirection(m_panHoldDirection);m_panRepeatTimer->start();}});
        QObject::connect(m_panRepeatTimer,&QTimer::timeout,this,[this]{if(m_panHoldDirection>=0)panViewDirection(m_panHoldDirection);});

        // Construction-plane controls are intentionally fixed in the upper-left corner.  The
        // XY/XZ/YZ buttons are presets; Rx/Ry/Rz are additional world-axis rotations of the plane.
        m_constructionPlanePanel = new QFrame(this);
        m_constructionPlanePanel->setFrameShape(QFrame::StyledPanel);
        m_constructionPlanePanel->setAutoFillBackground(true);
        auto *planeGrid=new QGridLayout(m_constructionPlanePanel);planeGrid->setContentsMargins(6,4,6,5);planeGrid->setHorizontalSpacing(4);planeGrid->setVerticalSpacing(2);
        auto *planeTitle=new QLabel(QStringLiteral("Construction plane"),m_constructionPlanePanel);planeGrid->addWidget(planeTitle,0,0,1,4);
        m_planeCombo=new QComboBox(m_constructionPlanePanel);m_planeCombo->addItems({QStringLiteral("XY"),QStringLiteral("XZ"),QStringLiteral("YZ")});m_planeCombo->setMaximumWidth(74);planeGrid->addWidget(m_planeCombo,1,0,1,2);
        m_planeInfoLabel=new QLabel(m_constructionPlanePanel);planeGrid->addWidget(m_planeInfoLabel,1,2,1,2);
        planeGrid->addWidget(new QLabel(QStringLiteral("Offset"),m_constructionPlanePanel),2,0);
        m_planeCoordinateSpin=new QDoubleSpinBox(m_constructionPlanePanel);m_planeCoordinateSpin->setRange(-1e6,1e6);m_planeCoordinateSpin->setDecimals(6);m_planeCoordinateSpin->setSuffix(QStringLiteral(" m"));m_planeCoordinateSpin->setKeyboardTracking(false);m_planeCoordinateSpin->setMaximumWidth(110);planeGrid->addWidget(m_planeCoordinateSpin,2,1,1,2);
        const std::array<QString,3> planeAxisNames{QStringLiteral("Rx"),QStringLiteral("Ry"),QStringLiteral("Rz")};
        for(int axis=0;axis<3;++axis)
        {
            auto *label=new QLabel(planeAxisNames[static_cast<std::size_t>(axis)],m_constructionPlanePanel);planeGrid->addWidget(label,3+axis,0);
            auto *spin=new QDoubleSpinBox(m_constructionPlanePanel);spin->setRange(-360.0,360.0);spin->setDecimals(1);spin->setSingleStep(5.0);spin->setSuffix(QStringLiteral("°"));spin->setKeyboardTracking(false);spin->setMaximumWidth(82);m_planeAngleSpin[static_cast<std::size_t>(axis)]=spin;planeGrid->addWidget(spin,3+axis,1,1,2);
            QObject::connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,axis](double value){
                if(m_syncingPlaneEditors)return;if(axis==0)m_planeRotXDeg=value;else if(axis==1)m_planeRotYDeg=value;else m_planeRotZDeg=value;
                if(constructionPlaneOrientationChanged)constructionPlaneOrientationChanged(m_planeRotXDeg,m_planeRotYDeg,m_planeRotZDeg);update();
            });
        }
        m_pickPivotButton=new QPushButton(QStringLiteral("Place pivot"),m_constructionPlanePanel);m_pickPivotButton->setToolTip(QStringLiteral("Pick a custom rotation pivot directly on the current construction plane."));planeGrid->addWidget(m_pickPivotButton,6,0,1,3);
        QObject::connect(m_planeCombo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int plane){requestEditPlane(plane);});
        QObject::connect(m_planeCoordinateSpin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double value){if(m_syncingPlaneEditors)return;m_planeCoordinateM=value;if(planeCoordinateChangeRequested)planeCoordinateChangeRequested(value);update();});
        QObject::connect(m_pickPivotButton,&QPushButton::clicked,this,[this]{if(editorToolRequested)editorToolRequested(7);else setTool(Tool::PickPivot);});

        // Fixed Fusion-like selection inspector. It stays in screen coordinates and is only
        // visible for one selected node/object so the viewport remains uncluttered otherwise.
        m_selectionEditor = new QFrame(this);
        m_selectionEditor->setFrameShape(QFrame::StyledPanel);
        m_selectionEditor->setAutoFillBackground(true);
        auto *selGrid=new QGridLayout(m_selectionEditor);
        selGrid->setContentsMargins(6,4,6,5);selGrid->setHorizontalSpacing(4);selGrid->setVerticalSpacing(3);
        m_selectionEditorTitle=new QLabel(QStringLiteral("Selection"),m_selectionEditor);
        selGrid->addWidget(m_selectionEditorTitle,0,0,1,6);
        const std::array<QString,3> coordNames{QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("Z")};
        for(int axis=0;axis<3;++axis)
        {
            auto *label=new QLabel(coordNames[static_cast<std::size_t>(axis)],m_selectionEditor);
            auto *spin=new QDoubleSpinBox(m_selectionEditor);spin->setRange(-1e9,1e9);spin->setDecimals(4);spin->setSuffix(QStringLiteral(" mm"));spin->setKeyboardTracking(false);spin->setMaximumWidth(112);
            m_selectionCoordSpin[static_cast<std::size_t>(axis)]=spin;
            selGrid->addWidget(label,1,2*axis);selGrid->addWidget(spin,1,2*axis+1);
            QObject::connect(spin,&QDoubleSpinBox::editingFinished,this,[this,axis]{if(!m_syncingOverlayEditors)applySelectionCoordinateEdit(axis);});
        }
        for(int dim=0;dim<3;++dim)
        {
            auto *label=new QLabel(m_selectionEditor);
            auto *spin=new QDoubleSpinBox(m_selectionEditor);spin->setRange(0.0001,1e9);spin->setDecimals(4);spin->setSuffix(QStringLiteral(" mm"));spin->setKeyboardTracking(false);spin->setMaximumWidth(112);
            m_selectionDimLabel[static_cast<std::size_t>(dim)]=label;m_selectionDimSpin[static_cast<std::size_t>(dim)]=spin;
            selGrid->addWidget(label,2,2*dim);selGrid->addWidget(spin,2,2*dim+1);
            QObject::connect(spin,&QDoubleSpinBox::editingFinished,this,[this,dim]{if(!m_syncingOverlayEditors)applySelectionDimensionEdit(dim);});
        }
        m_selectionEditor->hide();
        syncViewAngleEditors();
        syncConstructionPlaneEditors();
        layoutOverlayWidgets();
    }

    std::function<void(int)> editPlaneChangeRequested;
    std::function<void(double)> planeCoordinateChangeRequested;
    std::function<void(double,double,double)> constructionPlaneOrientationChanged;
    std::function<void(int)> editorToolRequested;
    std::function<void(double)> gridChangeRequested;
    std::function<void(int,const NumericalEM::Vec3 &)> primitiveSetupRequested;
    std::function<void()> duplicateRequested;
    std::function<void()> groupCreateRequested;
    std::function<void()> groupUngroupRequested;
    std::function<void(int)> alignRequested;
    std::function<void()> coincidentCentersRequested;
    std::function<void()> matchSurfaceSizeRequested;
    std::function<void(int,const NumericalEM::Vec3 &)> mirrorRequested;
    std::function<void(double,const NumericalEM::Vec3 &,const NumericalEM::Vec3 &)> constructionPlaneRotateRequested;
    std::function<void(const NumericalEM::Vec3 &, const NumericalEM::Vec3 &)> wireCreated;
    std::function<void(const NumericalEM::Vec3 &)> feedCreated;
    std::function<void(const NumericalEM::Vec3 &, const NumericalEM::Vec3 &)> rectPecCreated;
    std::function<void(const NumericalEM::Vec3 &, const NumericalEM::Vec3 &)> dielectricCreated;
    std::function<void(const NumericalEM::Vec3 &)> pivotPicked;
    std::function<void(const NumericalEM::Vec3 &)> deleteRequested;
    std::function<void(const NumericalEM::Vec3 &, const NumericalEM::Vec3 &)> nodeMoveRequested;
    std::function<void(int, const NumericalEM::Vec3 &)> feedMoveRequested;
    std::function<void(int, const NumericalEM::Vec3 &)> wireTranslateRequested;
    std::function<void(int, const NumericalEM::Vec3 &)> planeTranslateRequested;
    std::function<void(int, const NumericalEM::Vec3 &)> dielectricTranslateRequested;
    std::function<void(const std::vector<int> &, const std::vector<int> &, const std::vector<int> &,
                       const std::vector<int> &, const NumericalEM::Vec3 &)> groupTranslateRequested;
    std::function<void(const std::vector<int> &, const std::vector<int> &, const std::vector<int> &,
                       const std::vector<int> &, int, double, const NumericalEM::Vec3 &)> groupRotateRequested;
    std::function<void(int, const NumericalEM::Vec3 &, double, double)> planeResizeRequested;
    std::function<void(int, const NumericalEM::Vec3 &, double, double)> dielectricResizeRequested;
    std::function<void(int, const NumericalEM::Vec3 &, double)> dielectricThicknessRequested;
    std::function<void(int, double)> planeRadiusRequested;
    std::function<void(int)> constraintSelected;
    std::function<void(int)> constraintEditRequested;
    std::function<void(int)> constraintDeleteRequested;
    std::function<void(const std::vector<int> &, const std::vector<int> &, const std::vector<int> &,
                       const std::vector<int> &, bool, const NumericalEM::Vec3 &)> constraintSetupRequested;
    std::function<void(int, double, double)> wireDimensionsRequested;
    std::function<void(int, double, double, double)> feedElectricalRequested;
    std::function<void(const std::vector<int> &, const std::vector<int> &, const std::vector<int> &,
                       const std::vector<int> &, bool, const NumericalEM::Vec3 &)> geometryDeleteSelectionRequested;

    struct ConstraintOverlay
    {
        NumericalEM::Vec3 a;
        NumericalEM::Vec3 b;
        QString label;
        bool ok = true;
        int index = -1;
    };

    struct GroupSelection
    {
        QString id;
        QString name;
        std::set<int> wires;
        std::set<int> feeds;
        std::set<int> planes;
        std::set<int> dielectrics;
    };

    void setObjectSnapEnabled(bool enabled) { m_objectSnapEnabled=enabled; update(); }
    void setGroups(const std::vector<GeometryGroup> &groups)
    {
        m_groupSelections.clear();
        auto wireIndex=[this](const QString&id){for(int i=0;i<static_cast<int>(m_wires.size());++i)if(m_wires[static_cast<std::size_t>(i)].id==id)return i;return -1;};
        auto feedIndex=[this](const QString&id){for(int i=0;i<static_cast<int>(m_feeds.size());++i)if(m_feeds[static_cast<std::size_t>(i)].id==id)return i;return -1;};
        auto planeIndex=[this](const QString&id){for(int i=0;i<static_cast<int>(m_planes.size());++i)if(m_planes[static_cast<std::size_t>(i)].id==id)return i;return -1;};
        auto dielectricIndex=[this](const QString&id){for(int i=0;i<static_cast<int>(m_dielectrics.size());++i)if(m_dielectrics[static_cast<std::size_t>(i)].id==id)return i;return -1;};
        for(const auto &group:groups)
        {
            GroupSelection g;g.id=group.id;g.name=group.name;
            for(const auto&id:group.objectIds)
            {
                if(const int i=wireIndex(id);i>=0)g.wires.insert(i);
                else if(const int i=feedIndex(id);i>=0)g.feeds.insert(i);
                else if(const int i=planeIndex(id);i>=0)g.planes.insert(i);
                else if(const int i=dielectricIndex(id);i>=0)g.dielectrics.insert(i);
            }
            if(g.wires.size()+g.feeds.size()+g.planes.size()+g.dielectrics.size()>=2)m_groupSelections.push_back(std::move(g));
        }
        expandObjectSelectionToGroups();
        refreshSelectionEditor();
        update();
    }
    void setGizmoMode(int mode) { m_gizmoMode=std::clamp(mode,0,1);m_dragRotationAxis=-1;m_rotationPreviewDeg=0.0;update(); }
    void setPivotMode(int mode) { m_pivotMode=std::clamp(mode,0,3);update(); }
    void setCustomPivot(const NumericalEM::Vec3 &p) { m_customPivot=p;update(); }
    void fitGeometry()
    {
        geometryFrame(m_cameraFrameCx,m_cameraFrameCy,m_cameraFrameCz,m_cameraFrameSpan);
        m_cameraFrameValid=true;
        m_zoom = 1.0;
        m_pan = QPointF();
        update();
    }
    void centerWorldOrigin()
    {
        const auto c = camera();
        const QPointF originScreen = project(NumericalEM::Vec3{}, c).q;
        const QPointF target(width() * 0.5, height() * 0.53);
        m_pan += target - originScreen;
        update();
    }
    NumericalEM::Vec3 transformPivot() const
    {
        if(m_pivotMode==1)return {};
        if(m_pivotMode==2)return constructionPlaneOrigin();
        if(m_pivotMode==3)return m_customPivot;
        return objectSelectionCount()>0?selectionCentroid():NumericalEM::Vec3{};
    }
    std::vector<int> selectedWires() const { return asVector(m_selectedWires); }
    std::vector<int> selectedFeeds() const { return asVector(m_selectedFeeds); }
    std::vector<int> selectedPlanes() const { return asVector(m_selectedPlanes); }
    std::vector<int> selectedDielectrics() const { return asVector(m_selectedDielectrics); }
    int selectedObjectCount() const { return objectSelectionCount(); }
    NumericalEM::Vec3 selectedCentroid() const { return selectionCentroid(); }
    void setObjectSelection(const std::vector<int>& wires,const std::vector<int>& feeds,const std::vector<int>& planes,const std::vector<int>& dielectrics)
    {
        clearObjectSelection();
        for(int i:wires)if(i>=0&&i<static_cast<int>(m_wires.size()))m_selectedWires.insert(i);
        for(int i:feeds)if(i>=0&&i<static_cast<int>(m_feeds.size()))m_selectedFeeds.insert(i);
        for(int i:planes)if(i>=0&&i<static_cast<int>(m_planes.size()))m_selectedPlanes.insert(i);
        for(int i:dielectrics)if(i>=0&&i<static_cast<int>(m_dielectrics.size()))m_selectedDielectrics.insert(i);
        // Programmatic selection (table, duplicate, transform completion) follows the same rigid-group
        // semantics as a mouse click. This keeps group behaviour deterministic from every entry point.
        expandObjectSelectionToGroups();
        m_selectionKind=objectSelectionCount()>0?6:0; if(m_selectionKind)m_selectedPoint=selectionCentroid(); refreshSelectionEditor();update();
    }

    void clearSelectionFromController()
    {
        clearObjectSelection();m_selectionKind=0;m_selectedConstraint=-1;refreshSelectionEditor();update();
    }

    void setTool(Tool tool)
    {
        m_tool=tool;
        setCursor(tool==Tool::Select?Qt::ArrowCursor:Qt::CrossCursor);
        m_drawingWire=false;
        m_drawingRect=false;
        m_dragRotationAxis=-1;
        m_rotationPreviewDeg=0.0;
        m_resizing=false;m_thicknessResizing=false;m_radiusResizing=false;
        if(tool!=Tool::Measure)m_measureHover=false;
        update();
    }
    void setToolSelectorIndex(int index)
    {
        if(!m_toolCombo)return;m_syncingToolCombo=true;m_toolCombo->setCurrentIndex(std::clamp(index,0,m_toolCombo->count()-1));m_syncingToolCombo=false;
    }
    void setShowPhysicalWireRadius(bool enabled) { m_showPhysicalWireRadius=enabled; update(); }
    void setEditContext(int plane,double coordinateM,double gridMm)
    {
        m_editPlane=std::clamp(plane,0,2);
        m_planeCoordinateM=coordinateM;
        m_gridM=std::max(1e-6,gridMm/MmPerM);
        if(m_gridSpin){m_syncingGridEditor=true;m_gridSpin->setValue(gridMm);m_syncingGridEditor=false;}
        syncConstructionPlaneEditors();
        update();
    }

    void setConstructionPlaneOrientation(double rxDeg,double ryDeg,double rzDeg)
    {
        m_planeRotXDeg=rxDeg;m_planeRotYDeg=ryDeg;m_planeRotZDeg=rzDeg;
        syncConstructionPlaneEditors();update();
    }

    void setGeometry(const std::vector<WireElement> &wires, const std::vector<FeedPoint> &feeds, const std::vector<PlaneElement> &planes, const std::vector<DielectricElement> &dielectrics)
    {
        m_wires = wires;
        m_feeds = feeds;
        m_planes = planes;
        m_dielectrics = dielectrics;
        m_surfaceCurrentPoints.clear();
        m_surfaceCurrentMagnitude.clear();
        // Preserve a useful selection only if it still exists topologically. Object selections are
        // index based; prune anything deleted by a table edit/undo/load. Node selection remains
        // coordinate based because a shared topological node may belong to several WireElements.
        if(m_selectionKind==1 && !nodeExists(m_selectedPoint)) m_selectionKind=0;
        auto prune=[&](std::set<int>& values,int count){for(auto it=values.begin();it!=values.end();){if(*it<0||*it>=count)it=values.erase(it);else ++it;}};
        prune(m_selectedWires,static_cast<int>(m_wires.size()));
        prune(m_selectedFeeds,static_cast<int>(m_feeds.size()));
        prune(m_selectedPlanes,static_cast<int>(m_planes.size()));
        prune(m_selectedDielectrics,static_cast<int>(m_dielectrics.size()));
        if(m_selectionKind!=1){if(objectSelectionCount()==0)m_selectionKind=0;else{m_selectionKind=6;m_selectedPoint=selectionCentroid();}}
        if(!m_cameraFrameValid){geometryFrame(m_cameraFrameCx,m_cameraFrameCy,m_cameraFrameCz,m_cameraFrameSpan);m_cameraFrameValid=true;}
        refreshSelectionEditor();
        update();
    }

    void setSurfaceCurrentSamples(const std::vector<NumericalEM::Vec3> &points, const std::vector<double> &magnitude)
    {
        m_surfaceCurrentPoints = points;
        m_surfaceCurrentMagnitude = magnitude;
        update();
    }

    void clearSurfaceCurrentSamples()
    {
        m_surfaceCurrentPoints.clear();
        m_surfaceCurrentMagnitude.clear();
        update();
    }

    void clearMeasurement()
    {
        m_measureHasA=false;
        m_measureComplete=false;
        m_measureHover=false;
        update();
    }

    void setConstraintOverlays(const std::vector<ConstraintOverlay> &overlays)
    {
        m_constraintOverlays = overlays;
        bool exists=false;for(const auto&o:m_constraintOverlays)if(o.index==m_selectedConstraint){exists=true;break;}
        if(!exists)m_selectedConstraint=-1;
        update();
    }

    void setSelectedConstraint(int constraintIndex)
    {
        m_selectedConstraint=constraintIndex;
        update();
    }

    void setConstraintProblemObjectIds(const std::set<QString> &ids)
    {
        m_constraintProblemObjectIds = ids;
        update();
    }

    void syncViewAngleEditors()
    {
        m_syncingOverlayEditors=true;
        if(m_viewAngleSpin[0])m_viewAngleSpin[0]->setValue(m_viewRotXDeg);
        if(m_viewAngleSpin[1])m_viewAngleSpin[1]->setValue(m_viewRotYDeg);
        if(m_viewAngleSpin[2])m_viewAngleSpin[2]->setValue(m_viewRotZDeg);
        m_syncingOverlayEditors=false;
    }

    void syncConstructionPlaneEditors()
    {
        m_syncingPlaneEditors=true;
        if(m_planeCombo)m_planeCombo->setCurrentIndex(m_editPlane);
        if(m_planeCoordinateSpin)m_planeCoordinateSpin->setValue(m_planeCoordinateM);
        if(m_planeAngleSpin[0])m_planeAngleSpin[0]->setValue(m_planeRotXDeg);
        if(m_planeAngleSpin[1])m_planeAngleSpin[1]->setValue(m_planeRotYDeg);
        if(m_planeAngleSpin[2])m_planeAngleSpin[2]->setValue(m_planeRotZDeg);
        if(m_planeInfoLabel)m_planeInfoLabel->setText(QStringLiteral("%1=%2 m · %3 mm")
            .arg(hiddenPlaneAxisName()).arg(m_planeCoordinateM,0,'g',4).arg(m_gridM*MmPerM,0,'g',5));
        m_syncingPlaneEditors=false;
    }

    void layoutOverlayWidgets()
    {
        if(m_constructionPlanePanel)m_constructionPlanePanel->setGeometry(10,10,200,178);
        if(m_toolPanel){int tx=(width()-285)/2,ty=8;if(tx<218||tx+285>width()-145){tx=std::max(8,(width()-285)/2);ty=194;}m_toolPanel->setGeometry(tx,ty,285,31);}
        if(m_viewAnglesPanel)m_viewAnglesPanel->setGeometry(std::max(8,width()-100),174,90,90);
        if(m_gridPanel)m_gridPanel->setGeometry(std::max(8,width()-280),std::max(190,height()-50),166,31);
        if(m_selectionEditor)m_selectionEditor->setGeometry(12,std::max(178,height()-104),438,92);
    }

    void refreshSelectionEditor()
    {
        if(!m_selectionEditor)return;
        m_selectionEditKind=0;m_selectionEditIndex=-1;
        NumericalEM::Vec3 center{};QString title;
        std::array<QString,3> dimLabel{};std::array<double,3> dimValue{};std::array<bool,3> dimVisible{{false,false,false}};
        if(m_selectionKind==1)
        {
            m_selectionEditKind=1;center=m_selectedPoint;title=QStringLiteral("Node");
        }
        else if(objectSelectionCount()==1)
        {
            if(m_selectedWires.size()==1)
            {
                const int i=*m_selectedWires.begin();const auto&w=m_wires[static_cast<std::size_t>(i)];m_selectionEditKind=2;m_selectionEditIndex=i;
                center={0.5*(w.aM.x()+w.bM.x()),0.5*(w.aM.y()+w.bM.y()),0.5*(w.azM+w.bzM)};title=QStringLiteral("Wire %1").arg(w.name);
                dimLabel={QStringLiteral("L"),QStringLiteral("Ø"),QString()};dimValue={NumericalEM::norm(NumericalEM::Vec3{w.bM.x()-w.aM.x(),w.bM.y()-w.aM.y(),w.bzM-w.azM})*MmPerM,2.0*w.radiusM*MmPerM,0.0};dimVisible={true,true,false};
            }
            else if(m_selectedFeeds.size()==1)
            {
                const int i=*m_selectedFeeds.begin();const auto&f=m_feeds[static_cast<std::size_t>(i)];m_selectionEditKind=3;m_selectionEditIndex=i;center={f.positionM.x(),f.positionM.y(),f.zM};title=QStringLiteral("Feed %1").arg(f.name);
                // Feed simulation parameters are edited in-place just like a selected wire's
                // length/diameter. Zref is the S-parameter/reference impedance, not a physical
                // series resistor inserted in the thin-wire EFIE source.
                dimLabel={QStringLiteral("|V|"),QStringLiteral("Phase"),QStringLiteral("Zref")};
                dimValue={f.voltageV,f.phaseDeg,f.sourceOhm};dimVisible={true,true,true};
            }
            else if(m_selectedPlanes.size()==1)
            {
                const int i=*m_selectedPlanes.begin();const auto&pl=m_planes[static_cast<std::size_t>(i)];m_selectionEditKind=4;m_selectionEditIndex=i;center={pl.centerM.x(),pl.centerM.y(),pl.zM};title=QStringLiteral("PEC %1").arg(pl.name);
                if(pl.surfaceType==0){dimLabel={QStringLiteral("W"),QStringLiteral("H"),QString()};dimValue={pl.widthM*MmPerM,pl.heightM*MmPerM,0.0};dimVisible={true,true,false};}
                else{dimLabel={QStringLiteral("R"),QString(),QString()};dimValue={pl.radiusM*MmPerM,0.0,0.0};dimVisible={true,false,false};}
            }
            else if(m_selectedDielectrics.size()==1)
            {
                const int i=*m_selectedDielectrics.begin();const auto&d=m_dielectrics[static_cast<std::size_t>(i)];m_selectionEditKind=5;m_selectionEditIndex=i;center={d.centerM.x(),d.centerM.y(),d.zM};title=QStringLiteral("Dielectric %1").arg(d.name);
                dimLabel={QStringLiteral("W"),QStringLiteral("H"),QStringLiteral("t")};dimValue={d.widthM*MmPerM,d.heightM*MmPerM,d.thicknessM*MmPerM};dimVisible={true,true,true};
            }
        }
        if(m_selectionEditKind==0){m_selectionEditor->hide();return;}
        m_syncingOverlayEditors=true;m_selectionEditorTitle->setText(title);
        const std::array<double,3> coord{{center.x*MmPerM,center.y*MmPerM,center.z*MmPerM}};
        for(int a=0;a<3;++a)m_selectionCoordSpin[static_cast<std::size_t>(a)]->setValue(coord[static_cast<std::size_t>(a)]);
        for(int d=0;d<3;++d)
        {
            auto*l=m_selectionDimLabel[static_cast<std::size_t>(d)];auto*sp=m_selectionDimSpin[static_cast<std::size_t>(d)];
            l->setVisible(dimVisible[static_cast<std::size_t>(d)]);sp->setVisible(dimVisible[static_cast<std::size_t>(d)]);
            if(dimVisible[static_cast<std::size_t>(d)])
            {
                l->setText(dimLabel[static_cast<std::size_t>(d)]);
                if(m_selectionEditKind==3)
                {
                    if(d==0){sp->setRange(0.0,1e9);sp->setDecimals(6);sp->setSingleStep(0.1);sp->setSuffix(QStringLiteral(" V"));}
                    else if(d==1){sp->setRange(-3600.0,3600.0);sp->setDecimals(3);sp->setSingleStep(5.0);sp->setSuffix(QStringLiteral("°"));}
                    else{sp->setRange(1e-9,1e9);sp->setDecimals(4);sp->setSingleStep(1.0);sp->setSuffix(QStringLiteral(" Ω"));}
                }
                else
                {
                    sp->setRange(0.0001,1e9);sp->setDecimals(4);sp->setSingleStep(1.0);sp->setSuffix(QStringLiteral(" mm"));
                }
                sp->setValue(dimValue[static_cast<std::size_t>(d)]);
            }
        }
        m_syncingOverlayEditors=false;m_selectionEditor->show();m_selectionEditor->raise();
    }

    NumericalEM::Vec3 selectionEditorCenter() const
    {
        return {m_selectionCoordSpin[0]->value()/MmPerM,m_selectionCoordSpin[1]->value()/MmPerM,m_selectionCoordSpin[2]->value()/MmPerM};
    }

    void applySelectionCoordinateEdit(int)
    {
        if(m_selectionEditKind==0)return;const NumericalEM::Vec3 target=selectionEditorCenter();
        if(m_selectionEditKind==1)
        {
            const NumericalEM::Vec3 old=m_selectedPoint;m_selectedPoint=target;if(nodeMoveRequested)nodeMoveRequested(old,target);return;
        }
        if(m_selectionEditKind==2&&m_selectionEditIndex>=0&&m_selectionEditIndex<static_cast<int>(m_wires.size()))
        {
            const auto&w=m_wires[static_cast<std::size_t>(m_selectionEditIndex)];const NumericalEM::Vec3 old{0.5*(w.aM.x()+w.bM.x()),0.5*(w.aM.y()+w.bM.y()),0.5*(w.azM+w.bzM)};if(wireTranslateRequested)wireTranslateRequested(m_selectionEditIndex,target-old);return;
        }
        if(m_selectionEditKind==3&&feedMoveRequested){feedMoveRequested(m_selectionEditIndex,target);return;}
        if(m_selectionEditKind==4&&m_selectionEditIndex>=0&&m_selectionEditIndex<static_cast<int>(m_planes.size()))
        {const auto&pl=m_planes[static_cast<std::size_t>(m_selectionEditIndex)];const NumericalEM::Vec3 old{pl.centerM.x(),pl.centerM.y(),pl.zM};if(planeTranslateRequested)planeTranslateRequested(m_selectionEditIndex,target-old);return;}
        if(m_selectionEditKind==5&&m_selectionEditIndex>=0&&m_selectionEditIndex<static_cast<int>(m_dielectrics.size()))
        {const auto&d=m_dielectrics[static_cast<std::size_t>(m_selectionEditIndex)];const NumericalEM::Vec3 old{d.centerM.x(),d.centerM.y(),d.zM};if(dielectricTranslateRequested)dielectricTranslateRequested(m_selectionEditIndex,target-old);return;}
    }

    void applySelectionDimensionEdit(int dim)
    {
        if(m_selectionEditKind==3&&m_selectionEditIndex>=0&&feedElectricalRequested)
        {feedElectricalRequested(m_selectionEditIndex,m_selectionDimSpin[0]->value(),m_selectionDimSpin[1]->value(),m_selectionDimSpin[2]->value());return;}
        if(m_selectionEditKind==2&&m_selectionEditIndex>=0&&wireDimensionsRequested)
        {wireDimensionsRequested(m_selectionEditIndex,m_selectionDimSpin[0]->value()/MmPerM,0.5*m_selectionDimSpin[1]->value()/MmPerM);return;}
        if(m_selectionEditKind==4&&m_selectionEditIndex>=0&&m_selectionEditIndex<static_cast<int>(m_planes.size()))
        {
            const auto&pl=m_planes[static_cast<std::size_t>(m_selectionEditIndex)];const NumericalEM::Vec3 c{pl.centerM.x(),pl.centerM.y(),pl.zM};
            if(pl.surfaceType==0&&planeResizeRequested)planeResizeRequested(m_selectionEditIndex,c,m_selectionDimSpin[0]->value()/MmPerM,m_selectionDimSpin[1]->value()/MmPerM);
            else if(pl.surfaceType!=0&&dim==0&&planeRadiusRequested)planeRadiusRequested(m_selectionEditIndex,m_selectionDimSpin[0]->value()/MmPerM);return;
        }
        if(m_selectionEditKind==5&&m_selectionEditIndex>=0&&m_selectionEditIndex<static_cast<int>(m_dielectrics.size()))
        {
            const auto&d=m_dielectrics[static_cast<std::size_t>(m_selectionEditIndex)];const NumericalEM::Vec3 c{d.centerM.x(),d.centerM.y(),d.zM};
            if((dim==0||dim==1)&&dielectricResizeRequested)dielectricResizeRequested(m_selectionEditIndex,c,m_selectionDimSpin[0]->value()/MmPerM,m_selectionDimSpin[1]->value()/MmPerM);
            else if(dim==2&&dielectricThicknessRequested)dielectricThicknessRequested(m_selectionEditIndex,c,m_selectionDimSpin[2]->value()/MmPerM);return;
        }
    }

protected:
    struct Camera
    {
        double cx=0,cy=0,cz=0,span=1,scale=1;
        // World -> camera rotation matrix. The view keeps the historical convention
        // screen=(camera X,-camera Z), depth=camera Y, but now supports X/Y/Z Euler angles.
        double r00=1,r01=0,r02=0,r10=0,r11=1,r12=0,r20=0,r21=0,r22=1;
        QPointF center;
    };
    struct Proj { QPointF q; double depth=0; };

    void geometryFrame(double &cx,double &cy,double &cz,double &span) const
    {
        double xmin=1e300,xmax=-1e300,ymin=1e300,ymax=-1e300,zmin=1e300,zmax=-1e300;
        auto bounds=[&](double x,double y,double z){xmin=std::min(xmin,x);xmax=std::max(xmax,x);ymin=std::min(ymin,y);ymax=std::max(ymax,y);zmin=std::min(zmin,z);zmax=std::max(zmax,z);};
        for(const auto&w:m_wires){bounds(w.aM.x(),w.aM.y(),w.azM);bounds(w.bM.x(),w.bM.y(),w.bzM);}
        for(const auto&f:m_feeds)bounds(f.positionM.x(),f.positionM.y(),f.zM);
        for(const auto&pl:m_planes){const auto mesh=AntennaSurface::triangulate(pl.toSurfaceSpec(),800);for(const auto&t:mesh)for(const auto&q:{t.a,t.b,t.c})bounds(q.x,q.y,q.z);}
        for(const auto&d:m_dielectrics){AntennaSurface::SurfaceSpec spec;spec.center={d.centerM.x(),d.centerM.y(),d.zM};spec.baseOrientation=d.orientation;spec.yawDeg=d.yawDeg;spec.pitchDeg=d.pitchDeg;spec.rollDeg=d.rollDeg;for(double n:{-0.5*d.thicknessM,0.5*d.thicknessM})for(double u:{-0.5*d.widthM,0.5*d.widthM})for(double v:{-0.5*d.heightM,0.5*d.heightM}){const auto q=AntennaSurface::pointFromLocal(spec,u,v,n);bounds(q.x,q.y,q.z);}}
        if(xmin>1e200){xmin=ymin=zmin=-0.5;xmax=ymax=zmax=0.5;}
        cx=0.5*(xmin+xmax);cy=0.5*(ymin+ymax);cz=0.5*(zmin+zmax);
        span=std::max({xmax-xmin,ymax-ymin,zmax-zmin,m_gridM*8.0,1e-4});
    }

    Camera camera() const
    {
        Camera c;
        if(m_cameraFrameValid){c.cx=m_cameraFrameCx;c.cy=m_cameraFrameCy;c.cz=m_cameraFrameCz;c.span=m_cameraFrameSpan;}
        else geometryFrame(c.cx,c.cy,c.cz,c.span);
        c.center=QPointF(width()*0.5+m_pan.x(),height()*0.53+m_pan.y());
        c.scale=0.72*std::max(100,std::min(width(),height()))/std::max(1e-9,c.span)*m_zoom;
        const double rx=m_viewRotXDeg*NumericalEM::Pi/180.0;
        const double ry=m_viewRotYDeg*NumericalEM::Pi/180.0;
        const double rz=m_viewRotZDeg*NumericalEM::Pi/180.0;
        const double cxr=std::cos(rx),sxr=std::sin(rx),cyr=std::cos(ry),syr=std::sin(ry),czr=std::cos(rz),szr=std::sin(rz);
        // R = Rx * Ry * Rz. ry=0 reproduces the historical pitch/yaw camera exactly.
        c.r00=cyr*czr; c.r01=-cyr*szr; c.r02=syr;
        c.r10=cxr*szr + sxr*syr*czr; c.r11=cxr*czr - sxr*syr*szr; c.r12=-sxr*cyr;
        c.r20=sxr*szr - cxr*syr*czr; c.r21=sxr*czr + cxr*syr*szr; c.r22=cxr*cyr;
        return c;
    }

    static Proj project(const NumericalEM::Vec3 &r,const Camera &c)
    {
        const double x=r.x-c.cx,y=r.y-c.cy,z=r.z-c.cz;
        const double xc=c.r00*x+c.r01*y+c.r02*z;
        const double yc=c.r10*x+c.r11*y+c.r12*z;
        const double zc=c.r20*x+c.r21*y+c.r22*z;
        return {QPointF(c.center.x()+c.scale*xc,c.center.y()-c.scale*zc),yc};
    }

    NumericalEM::Vec3 constructionPlaneOrigin() const
    {
        if(m_editPlane==0)return {0,0,m_planeCoordinateM};
        if(m_editPlane==1)return {0,m_planeCoordinateM,0};
        return {m_planeCoordinateM,0,0};
    }

    void constructionPlaneFrame(NumericalEM::Vec3 &origin,NumericalEM::Vec3 &u,NumericalEM::Vec3 &v,NumericalEM::Vec3 &normal) const
    {
        origin=constructionPlaneOrigin();
        const Matrix3 r=surfaceFrameMatrix(m_editPlane,m_planeRotZDeg,m_planeRotYDeg,m_planeRotXDeg);
        u=unitOr({r.m[0][0],r.m[1][0],r.m[2][0]},{1,0,0});
        v=unitOr({r.m[0][1],r.m[1][1],r.m[2][1]},{0,1,0});
        normal=unitOr({r.m[0][2],r.m[1][2],r.m[2][2]},{0,0,1});
    }

    bool pointOnConstructionPlaneRaw(const QPointF &screen,NumericalEM::Vec3 &out) const
    {
        NumericalEM::Vec3 origin{},u{},v{},normal{};constructionPlaneFrame(origin,u,v,normal);
        return pointOnArbitraryPlane(screen,origin,normal,out);
    }

    NumericalEM::Vec3 snapPointToConstructionGrid(const NumericalEM::Vec3 &point) const
    {
        NumericalEM::Vec3 origin{},u{},v{},normal{};constructionPlaneFrame(origin,u,v,normal);
        const NumericalEM::Vec3 d=point-origin;const double du=snap(NumericalEM::dot(d,u)),dv=snap(NumericalEM::dot(d,v));
        return origin+u*du+v*dv;
    }

    bool pointOnConstructionPlane(const QPointF &screen,NumericalEM::Vec3 &out) const
    {
        NumericalEM::Vec3 raw{};if(!pointOnConstructionPlaneRaw(screen,raw))return false;out=snapPointToConstructionGrid(raw);
        return std::isfinite(out.x)&&std::isfinite(out.y)&&std::isfinite(out.z);
    }

    double snap(double x) const { return std::round(x/std::max(1e-9,m_gridM))*std::max(1e-9,m_gridM); }

    bool nodeExists(const NumericalEM::Vec3 &p) const
    {
        for(const auto&w:m_wires)for(const auto&q:{NumericalEM::Vec3{w.aM.x(),w.aM.y(),w.azM},NumericalEM::Vec3{w.bM.x(),w.bM.y(),w.bzM}})if(NumericalEM::norm(q-p)<1e-8)return true;
        return false;
    }

    std::vector<NumericalEM::Vec3> uniqueNodes() const
    {
        std::vector<NumericalEM::Vec3> n;
        auto add=[&](const NumericalEM::Vec3&p){for(const auto&q:n)if(NumericalEM::norm(p-q)<1e-8)return;n.push_back(p);};
        for(const auto&w:m_wires){add({w.aM.x(),w.aM.y(),w.azM});add({w.bM.x(),w.bM.y(),w.bzM});}
        return n;
    }

    int objectSelectionCount() const
    {
        return static_cast<int>(m_selectedWires.size()+m_selectedFeeds.size()+m_selectedPlanes.size()+m_selectedDielectrics.size());
    }

    void clearObjectSelection()
    {
        m_selectedWires.clear();m_selectedFeeds.clear();m_selectedPlanes.clear();m_selectedDielectrics.clear();
        m_selectedFeed=-1;m_selectedWire=-1;
    }

    void expandObjectSelectionToGroups()
    {
        if(m_groupSelections.empty()||objectSelectionCount()==0)return;
        bool changed=true;
        while(changed)
        {
            changed=false;
            for(const auto &g:m_groupSelections)
            {
                bool touches=false;
                for(int i:g.wires)if(m_selectedWires.count(i)){touches=true;break;}
                if(!touches)for(int i:g.feeds)if(m_selectedFeeds.count(i)){touches=true;break;}
                if(!touches)for(int i:g.planes)if(m_selectedPlanes.count(i)){touches=true;break;}
                if(!touches)for(int i:g.dielectrics)if(m_selectedDielectrics.count(i)){touches=true;break;}
                if(!touches)continue;
                const auto before=objectSelectionCount();
                m_selectedWires.insert(g.wires.begin(),g.wires.end());
                m_selectedFeeds.insert(g.feeds.begin(),g.feeds.end());
                m_selectedPlanes.insert(g.planes.begin(),g.planes.end());
                m_selectedDielectrics.insert(g.dielectrics.begin(),g.dielectrics.end());
                changed|=objectSelectionCount()!=before;
            }
        }
    }

    bool selectionTouchesGroup() const
    {
        for(const auto &g:m_groupSelections)
        {
            for(int i:g.wires)if(m_selectedWires.count(i))return true;
            for(int i:g.feeds)if(m_selectedFeeds.count(i))return true;
            for(int i:g.planes)if(m_selectedPlanes.count(i))return true;
            for(int i:g.dielectrics)if(m_selectedDielectrics.count(i))return true;
        }
        return false;
    }

    NumericalEM::Vec3 selectionCentroid() const
    {
        NumericalEM::Vec3 sum{};double count=0.0;
        for(int i:m_selectedWires)if(i>=0&&i<static_cast<int>(m_wires.size())){const auto&w=m_wires[static_cast<std::size_t>(i)];sum=sum+NumericalEM::Vec3{0.5*(w.aM.x()+w.bM.x()),0.5*(w.aM.y()+w.bM.y()),0.5*(w.azM+w.bzM)};count+=1.0;}
        for(int i:m_selectedFeeds)if(i>=0&&i<static_cast<int>(m_feeds.size())){const auto&f=m_feeds[static_cast<std::size_t>(i)];sum=sum+NumericalEM::Vec3{f.positionM.x(),f.positionM.y(),f.zM};count+=1.0;}
        for(int i:m_selectedPlanes)if(i>=0&&i<static_cast<int>(m_planes.size())){const auto&pl=m_planes[static_cast<std::size_t>(i)];sum=sum+NumericalEM::Vec3{pl.centerM.x(),pl.centerM.y(),pl.zM};count+=1.0;}
        for(int i:m_selectedDielectrics)if(i>=0&&i<static_cast<int>(m_dielectrics.size())){const auto&d=m_dielectrics[static_cast<std::size_t>(i)];sum=sum+NumericalEM::Vec3{d.centerM.x(),d.centerM.y(),d.zM};count+=1.0;}
        return count>0.0?sum*(1.0/count):NumericalEM::Vec3{};
    }

    static std::vector<int> asVector(const std::set<int>& values)
    {
        return std::vector<int>(values.begin(),values.end());
    }

    int hitFeed(const QPointF &screen,const Camera &c) const
    {
        int best=-1;double dmin=12.0;
        for(int i=0;i<static_cast<int>(m_feeds.size());++i){const auto&f=m_feeds[static_cast<std::size_t>(i)];const double d=QLineF(screen,project({f.positionM.x(),f.positionM.y(),f.zM},c).q).length();if(d<dmin){dmin=d;best=i;}}
        return best;
    }
    bool hitNode(const QPointF &screen,const Camera &c,NumericalEM::Vec3 &node) const
    {
        double dmin=11.0;bool found=false;
        for(const auto&q:uniqueNodes()){const double d=QLineF(screen,project(q,c).q).length();if(d<dmin){dmin=d;node=q;found=true;}}
        return found;
    }
    int hitWire(const QPointF &screen,const Camera &c) const
    {
        int best=-1;double dmin=9.0;
        for(int i=0;i<static_cast<int>(m_wires.size());++i)
        {
            const auto &w=m_wires[static_cast<std::size_t>(i)];
            const QPointF a=project({w.aM.x(),w.aM.y(),w.azM},c).q;
            const QPointF b=project({w.bM.x(),w.bM.y(),w.bzM},c).q;
            const double d=pointSegmentDistance(screen,a,b);
            if(d<dmin){dmin=d;best=i;}
        }
        return best;
    }
    int hitPlane(const QPointF &screen,const Camera &c) const
    {
        int best=-1;double bestDepth=std::numeric_limits<double>::infinity();
        for(int i=0;i<static_cast<int>(m_planes.size());++i)
        {
            const auto mesh=AntennaSurface::triangulate(m_planes[static_cast<std::size_t>(i)].toSurfaceSpec(),1200);
            for(const auto&t:mesh)
            {
                QPolygonF poly;double dep=0.0;
                for(const auto&q:{t.a,t.b,t.c}){const auto pr=project(q,c);poly<<pr.q;dep+=pr.depth;}
                if(poly.containsPoint(screen,Qt::OddEvenFill)&&std::abs(dep/3.0)<bestDepth){bestDepth=std::abs(dep/3.0);best=i;}
            }
        }
        return best;
    }

    int hitDielectric(const QPointF &screen,const Camera &c) const
    {
        int best=-1;double dmin=14.0;
        const std::array<std::array<int,4>,6> faces{{{{0,1,3,2}},{{4,6,7,5}},{{0,4,5,1}},{{2,3,7,6}},{{0,2,6,4}},{{1,5,7,3}}}};
        for(int i=0;i<static_cast<int>(m_dielectrics.size());++i)
        {
            const auto&d=m_dielectrics[static_cast<std::size_t>(i)];AntennaSurface::SurfaceSpec spec;spec.center={d.centerM.x(),d.centerM.y(),d.zM};spec.baseOrientation=d.orientation;spec.yawDeg=d.yawDeg;spec.pitchDeg=d.pitchDeg;spec.rollDeg=d.rollDeg;
            std::array<NumericalEM::Vec3,8> q{};int qi=0;for(double n:{-0.5*d.thicknessM,0.5*d.thicknessM})for(double v:{-0.5*d.heightM,0.5*d.heightM})for(double u:{-0.5*d.widthM,0.5*d.widthM})q[qi++]=AntennaSurface::pointFromLocal(spec,u,v,n);
            bool inside=false;for(const auto&face:faces){QPolygonF poly;for(int idx:face)poly<<project(q[static_cast<std::size_t>(idx)],c).q;if(poly.containsPoint(screen,Qt::OddEvenFill)){inside=true;break;}}
            if(inside)return i;
            const double dist=QLineF(screen,project({d.centerM.x(),d.centerM.y(),d.zM},c).q).length();if(dist<dmin){dmin=dist;best=i;}
        }
        return best;
    }
    int hitGizmoAxis(const QPointF &screen,const Camera &c) const
    {
        if(m_selectionKind==0) return -1;
        const auto origin=project(m_selectedPoint,c).q;
        const double axisLen=0.16*c.span/std::max(0.25,m_zoom);
        for(int axis=0;axis<3;++axis)
        {
            NumericalEM::Vec3 e{};if(axis==0)e.x=axisLen;else if(axis==1)e.y=axisLen;else e.z=axisLen;
            const auto end=project(m_selectedPoint+e,c).q;
            QPointF proj;const double d=pointSegmentDistance(screen,origin,end,&proj);
            if(d<=7.0 && QLineF(origin,end).length()>12.0)return axis;
        }
        return -1;
    }

    NumericalEM::Vec3 rotationRingPoint(int axis,double angleRad,double radius,const NumericalEM::Vec3 &pivot) const
    {
        const double ca=std::cos(angleRad),sa=std::sin(angleRad);
        if(axis==0)return {pivot.x,pivot.y+radius*ca,pivot.z+radius*sa};
        if(axis==1)return {pivot.x+radius*sa,pivot.y,pivot.z+radius*ca};
        return {pivot.x+radius*ca,pivot.y+radius*sa,pivot.z};
    }

    double nearestRotationRingAngle(const QPointF &screen,const Camera &c,int axis,double *distancePx=nullptr) const
    {
        const NumericalEM::Vec3 pivot=transformPivot();const double radius=0.19*c.span/std::max(0.25,m_zoom);double best=std::numeric_limits<double>::infinity(),bestAngle=0.0;
        constexpr int Samples=144;
        for(int i=0;i<Samples;++i){const double a=2.0*NumericalEM::Pi*i/Samples;const QPointF q=project(rotationRingPoint(axis,a,radius,pivot),c).q;const double d=QLineF(screen,q).length();if(d<best){best=d;bestAngle=a;}}
        if(distancePx)*distancePx=best;return bestAngle;
    }

    int hitRotationAxis(const QPointF &screen,const Camera &c,double *angleRad=nullptr) const
    {
        if(m_selectionKind!=6||objectSelectionCount()==0)return -1;int bestAxis=-1;double best=9.0,bestAngle=0.0;
        for(int axis=0;axis<3;++axis){double d=0.0;const double a=nearestRotationRingAngle(screen,c,axis,&d);if(d<best){best=d;bestAxis=axis;bestAngle=a;}}
        if(bestAxis>=0&&angleRad)*angleRad=bestAngle;return bestAxis;
    }

    static double wrappedAngleDeg(double deg)
    {
        while(deg>180.0)deg-=360.0;while(deg<-180.0)deg+=360.0;return deg;
    }

    NumericalEM::Vec3 previewPoint(const NumericalEM::Vec3 &point,bool selected) const
    {
        if(!selected||m_dragRotationAxis<0||std::abs(m_rotationPreviewDeg)<1e-12)return point;
        return rotateAroundAxis(point,transformPivot(),m_dragRotationAxis,m_rotationPreviewDeg);
    }

    bool feedFollowsSelectedWire(int feedIndex) const
    {
        if(feedIndex<0||feedIndex>=static_cast<int>(m_feeds.size())||m_selectedWires.empty())return false;
        const auto&f=m_feeds[static_cast<std::size_t>(feedIndex)];const NumericalEM::Vec3 p{f.positionM.x(),f.positionM.y(),f.zM};const double tol=std::max(1e-7,m_gridM*0.20);
        for(int wi:m_selectedWires)if(wi>=0&&wi<static_cast<int>(m_wires.size())){const auto&w=m_wires[static_cast<std::size_t>(wi)];const NumericalEM::Vec3 a{w.aM.x(),w.aM.y(),w.azM},b{w.bM.x(),w.bM.y(),w.bzM},ab=b-a;const double d2=NumericalEM::dot(ab,ab);double dist=NumericalEM::norm(p-a);if(d2>1e-30){const double t=std::clamp(NumericalEM::dot(p-a,ab)/d2,0.0,1.0);dist=NumericalEM::norm(p-(a+ab*t));}if(dist<=tol)return true;}
        return false;
    }

    void beginSelectionDrag(const QPointF &screen,int axis)
    {
        m_dragAxis=axis;m_dragStartScreen=screen;m_dragStartWorld=m_selectedPoint;m_dragCursorStartWorld=m_selectedPoint;
        if(axis<0){NumericalEM::Vec3 hit{};if(pointOnConstructionPlaneRaw(screen,hit))m_dragCursorStartWorld=hit;}
        m_draggingSelection=true;
    }

    NumericalEM::Vec3 axisDraggedPoint(const QPointF &screen,const Camera &c) const
    {
        NumericalEM::Vec3 out=m_dragStartWorld;
        const double axisLen=0.16*c.span/std::max(0.25,m_zoom);NumericalEM::Vec3 e{};if(m_dragAxis==0)e.x=axisLen;else if(m_dragAxis==1)e.y=axisLen;else e.z=axisLen;
        const QPointF a=project(m_dragStartWorld,c).q,b=project(m_dragStartWorld+e,c).q;
        const QPointF sv=b-a;const double l2=QPointF::dotProduct(sv,sv);if(l2<1e-12)return out;
        const double factor=QPointF::dotProduct(screen-m_dragStartScreen,sv)/l2;
        out=out+e*factor;
        if(m_dragAxis==0)out.x=snap(out.x);else if(m_dragAxis==1)out.y=snap(out.y);else out.z=snap(out.z);
        return out;
    }

    std::vector<NumericalEM::Vec3> snapAnchors() const
    {
        std::vector<NumericalEM::Vec3> anchors;
        for(int i=0;i<static_cast<int>(m_wires.size());++i)if(!m_selectedWires.count(i)){const auto&w=m_wires[static_cast<std::size_t>(i)];anchors.push_back({w.aM.x(),w.aM.y(),w.azM});anchors.push_back({w.bM.x(),w.bM.y(),w.bzM});anchors.push_back({0.5*(w.aM.x()+w.bM.x()),0.5*(w.aM.y()+w.bM.y()),0.5*(w.azM+w.bzM)});}
        for(int i=0;i<static_cast<int>(m_feeds.size());++i)if(!m_selectedFeeds.count(i)){const auto&f=m_feeds[static_cast<std::size_t>(i)];anchors.push_back({f.positionM.x(),f.positionM.y(),f.zM});}
        for(int i=0;i<static_cast<int>(m_planes.size());++i)if(!m_selectedPlanes.count(i)){const auto&pl=m_planes[static_cast<std::size_t>(i)];anchors.push_back({pl.centerM.x(),pl.centerM.y(),pl.zM});}
        for(int i=0;i<static_cast<int>(m_dielectrics.size());++i)if(!m_selectedDielectrics.count(i)){const auto&d=m_dielectrics[static_cast<std::size_t>(i)];anchors.push_back({d.centerM.x(),d.centerM.y(),d.zM});}
        return anchors;
    }

    NumericalEM::Vec3 nearestMagneticAnchor(const NumericalEM::Vec3 &point,const Camera &c,bool excludeSelection,bool *foundOut=nullptr) const
    {
        const double tolerance=std::max(1e-8,12.0/std::max(1e-9,c.scale));
        double best=tolerance;NumericalEM::Vec3 bestPoint=point;bool found=false;
        auto consider=[&](const NumericalEM::Vec3&q){const double d=NumericalEM::norm(point-q);if(d<best){best=d;bestPoint=q;found=true;}};
        for(int i=0;i<static_cast<int>(m_wires.size());++i)
        {
            if(excludeSelection&&m_selectedWires.count(i))continue;
            const auto&w=m_wires[static_cast<std::size_t>(i)];const NumericalEM::Vec3 a{w.aM.x(),w.aM.y(),w.azM},b{w.bM.x(),w.bM.y(),w.bzM},ab=b-a;
            consider(a);consider(b);
            const double d2=NumericalEM::dot(ab,ab);if(d2>1e-30){const double t=std::clamp(NumericalEM::dot(point-a,ab)/d2,0.0,1.0);consider(a+ab*t);}
        }
        for(int i=0;i<static_cast<int>(m_feeds.size());++i)if(!(excludeSelection&&m_selectedFeeds.count(i))){const auto&f=m_feeds[static_cast<std::size_t>(i)];consider({f.positionM.x(),f.positionM.y(),f.zM});}
        if(foundOut)*foundOut=found;return bestPoint;
    }

    NumericalEM::Vec3 magneticConstructionPoint(const NumericalEM::Vec3 &point,const Camera &c) const
    {
        if(!m_objectSnapEnabled)return point;bool found=false;const auto q=nearestMagneticAnchor(point,c,false,&found);return found?q:point;
    }

    NumericalEM::Vec3 objectSnappedPoint(NumericalEM::Vec3 q,const Camera &c) const
    {
        if(!m_objectSnapEnabled||m_selectionKind!=6)return q;
        const double tolerance=std::max(1e-8,12.0/std::max(1e-9,c.scale));

        // Axis-constrained drags keep the historical coordinate snap behaviour.
        if(m_dragAxis>=0)
        {
            double best=tolerance;bool found=false;double bestCoord=0.0;
            for(const auto&a:snapAnchors())
            {
                const double qa=m_dragAxis==0?q.x:m_dragAxis==1?q.y:q.z;
                const double aa=m_dragAxis==0?a.x:m_dragAxis==1?a.y:a.z;
                const double d=std::abs(qa-aa);
                if(d<best){best=d;bestCoord=aa;found=true;}
            }
            if(found){if(m_dragAxis==0)q.x=bestCoord;else if(m_dragAxis==1)q.y=bestCoord;else q.z=bestCoord;}
            return q;
        }

        // Free dragging behaves like a CAD "magnetic/vertex snap": if any moved wire endpoint
        // approaches an existing wire endpoint or segment, correct the whole rigid selection so
        // that the endpoint lands exactly on the target. This avoids almost-connected geometry.
        const NumericalEM::Vec3 rigidDelta=q-m_dragStartWorld;
        double best=tolerance;NumericalEM::Vec3 bestCorrection{};bool found=false;
        auto considerMovedPoint=[&](const NumericalEM::Vec3 &source){
            const NumericalEM::Vec3 candidate=source+rigidDelta;bool hit=false;
            const NumericalEM::Vec3 target=nearestMagneticAnchor(candidate,c,true,&hit);
            if(!hit)return;const NumericalEM::Vec3 correction=target-candidate;const double d=NumericalEM::norm(correction);
            if(d<best){best=d;bestCorrection=correction;found=true;}
        };
        for(int wi:m_selectedWires)if(wi>=0&&wi<static_cast<int>(m_wires.size()))
        {
            const auto&w=m_wires[static_cast<std::size_t>(wi)];
            considerMovedPoint({w.aM.x(),w.aM.y(),w.azM});considerMovedPoint({w.bM.x(),w.bM.y(),w.bzM});
        }
        for(int fi:m_selectedFeeds)if(fi>=0&&fi<static_cast<int>(m_feeds.size()))
        {
            const auto&f=m_feeds[static_cast<std::size_t>(fi)];considerMovedPoint({f.positionM.x(),f.positionM.y(),f.zM});
        }

        if(found)
        {
            NumericalEM::Vec3 origin{},u{},v{},normal{};constructionPlaneFrame(origin,u,v,normal);
            q=q+u*NumericalEM::dot(bestCorrection,u)+v*NumericalEM::dot(bestCorrection,v);
            return q;
        }

        // Surfaces still get a lightweight center-to-anchor snap.
        double bestCenter=tolerance;NumericalEM::Vec3 bestAnchor{};bool centerFound=false;
        for(const auto&a:snapAnchors())
        {
            NumericalEM::Vec3 origin{},u{},v{},normal{};constructionPlaneFrame(origin,u,v,normal);const NumericalEM::Vec3 qa=q-a;
            const double d=std::hypot(NumericalEM::dot(qa,u),NumericalEM::dot(qa,v));
            if(d<bestCenter){bestCenter=d;bestAnchor=a;centerFound=true;}
        }
        if(centerFound)
        {
            NumericalEM::Vec3 origin{},u{},v{},normal{};constructionPlaneFrame(origin,u,v,normal);const NumericalEM::Vec3 delta=bestAnchor-q;
            q=q+u*NumericalEM::dot(delta,u)+v*NumericalEM::dot(delta,v);
        }
        return q;
    }

    void addSelectionInRect(const QRectF &rect,const Camera &c)
    {
        for(int i=0;i<static_cast<int>(m_wires.size());++i){const auto&w=m_wires[static_cast<std::size_t>(i)];const QPointF a=project({w.aM.x(),w.aM.y(),w.azM},c).q,b=project({w.bM.x(),w.bM.y(),w.bzM},c).q;QRectF bb(a,b);bb=bb.normalized().adjusted(-3,-3,3,3);if(rect.contains(a)||rect.contains(b)||rect.intersects(bb))m_selectedWires.insert(i);}
        for(int i=0;i<static_cast<int>(m_feeds.size());++i){const auto&f=m_feeds[static_cast<std::size_t>(i)];if(rect.contains(project({f.positionM.x(),f.positionM.y(),f.zM},c).q))m_selectedFeeds.insert(i);}
        for(int i=0;i<static_cast<int>(m_planes.size());++i){const auto mesh=AntennaSurface::triangulate(m_planes[static_cast<std::size_t>(i)].toSurfaceSpec(),600);bool hit=false;for(const auto&t:mesh){QPolygonF poly;for(const auto&q:{t.a,t.b,t.c})poly<<project(q,c).q;if(rect.intersects(poly.boundingRect())){hit=true;break;}}if(hit)m_selectedPlanes.insert(i);}
        for(int i=0;i<static_cast<int>(m_dielectrics.size());++i){const auto&d=m_dielectrics[static_cast<std::size_t>(i)];if(rect.contains(project({d.centerM.x(),d.centerM.y(),d.zM},c).q))m_selectedDielectrics.insert(i);}
        if(objectSelectionCount()>0){expandObjectSelectionToGroups();m_selectionKind=6;m_selectedPoint=selectionCentroid();}else m_selectionKind=0;
        refreshSelectionEditor();
    }


    std::array<NumericalEM::Vec3,4> planeRectangleCorners(const NumericalEM::Vec3 &a,const NumericalEM::Vec3 &b) const
    {
        NumericalEM::Vec3 origin{},u{},v{},normal{};constructionPlaneFrame(origin,u,v,normal);
        const NumericalEM::Vec3 da=a-origin,db=b-origin;const double ua=NumericalEM::dot(da,u),va=NumericalEM::dot(da,v),ub=NumericalEM::dot(db,u),vb=NumericalEM::dot(db,v);
        return {{origin+u*ua+v*va,origin+u*ub+v*va,origin+u*ub+v*vb,origin+u*ua+v*vb}};
    }

    bool validPlaneRectangle(const NumericalEM::Vec3 &a,const NumericalEM::Vec3 &b) const
    {
        const double minSide=std::max(1e-9,0.25*m_gridM);
        NumericalEM::Vec3 origin{},u{},v{},normal{};constructionPlaneFrame(origin,u,v,normal);const NumericalEM::Vec3 d=b-a;
        return std::abs(NumericalEM::dot(d,u))>=minSide&&std::abs(NumericalEM::dot(d,v))>=minSide;
    }

    QString measurementText() const
    {
        if(!m_measureHasA)return QString();
        const NumericalEM::Vec3 b=(m_measureComplete||m_measureHover)?m_measureB:m_measureA;
        const NumericalEM::Vec3 d=b-m_measureA;const double length=NumericalEM::norm(d);
        NumericalEM::Vec3 origin{},u{},v{},normal{};constructionPlaneFrame(origin,u,v,normal);const double bearing=std::atan2(NumericalEM::dot(d,v),NumericalEM::dot(d,u))*180.0/NumericalEM::Pi;
        const double azimuth=std::atan2(d.y,d.x)*180.0/NumericalEM::Pi;
        const double elevation=std::atan2(d.z,std::hypot(d.x,d.y))*180.0/NumericalEM::Pi;
        auto axisAngle=[&](double component){if(length<=1e-15)return 0.0;return std::acos(std::clamp(component/length,-1.0,1.0))*180.0/NumericalEM::Pi;};
        return QStringLiteral("d=%1 mm | ΔX=%2 ΔY=%3 ΔZ=%4 mm | plane=%5° | az=%6° el=%7°\naxis angles: αX=%8° αY=%9° αZ=%10°")
            .arg(length*MmPerM,0,'g',7).arg(d.x*MmPerM,0,'g',6).arg(d.y*MmPerM,0,'g',6).arg(d.z*MmPerM,0,'g',6)
            .arg(bearing,0,'f',2).arg(azimuth,0,'f',2).arg(elevation,0,'f',2).arg(axisAngle(d.x),0,'f',2).arg(axisAngle(d.y),0,'f',2).arg(axisAngle(d.z),0,'f',2);
    }


    AntennaSurface::SurfaceSpec dielectricSurfaceSpec(const DielectricElement &d) const
    {
        AntennaSurface::SurfaceSpec spec;
        spec.center={d.centerM.x(),d.centerM.y(),d.zM};
        spec.baseOrientation=d.orientation;
        spec.yawDeg=d.yawDeg;spec.pitchDeg=d.pitchDeg;spec.rollDeg=d.rollDeg;
        spec.widthM=d.widthM;spec.heightM=d.heightM;
        return spec;
    }

    static NumericalEM::Vec3 unitOr(const NumericalEM::Vec3 &v,const NumericalEM::Vec3 &fallback)
    {
        const double n=NumericalEM::norm(v);return n>1e-12?v*(1.0/n):fallback;
    }

    void localFrame(const AntennaSurface::SurfaceSpec &spec,NumericalEM::Vec3 &center,
                    NumericalEM::Vec3 &u,NumericalEM::Vec3 &v,NumericalEM::Vec3 &normal) const
    {
        center=AntennaSurface::pointFromLocal(spec,0,0,0);
        u=unitOr(AntennaSurface::pointFromLocal(spec,1,0,0)-center,{1,0,0});
        v=unitOr(AntennaSurface::pointFromLocal(spec,0,1,0)-center,{0,1,0});
        const NumericalEM::Vec3 uvCross{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
        normal=unitOr(AntennaSurface::pointFromLocal(spec,0,0,1)-center,uvCross);
        normal=unitOr(normal,uvCross);
    }

    bool screenRay(const QPointF &screen,NumericalEM::Vec3 &origin,NumericalEM::Vec3 &direction) const
    {
        const Camera c=camera();if(!(c.scale>1e-12))return false;
        // project() uses an orthographic world->camera matrix R with:
        //   screen X = camera X, screen Y = -camera Z, depth = camera Y.
        // A screen point therefore corresponds to camera coordinates
        // (xc, 0, zc) and the picking ray follows camera +Y.  Since R is
        // orthonormal, camera->world is R^T.  Keeping this inverse derived
        // directly from the matrix also makes picking follow the full XYZ
        // ViewCube rotation introduced in 5.8.3.
        const double xc=(screen.x()-c.center.x())/c.scale;
        const double zc=-(screen.y()-c.center.y())/c.scale;
        origin={c.cx+c.r00*xc+c.r20*zc,
                c.cy+c.r01*xc+c.r21*zc,
                c.cz+c.r02*xc+c.r22*zc};
        direction=unitOr({c.r10,c.r11,c.r12},{0,0,-1});
        return true;
    }

    bool pointOnArbitraryPlane(const QPointF &screen,const NumericalEM::Vec3 &planePoint,
                               const NumericalEM::Vec3 &normal,NumericalEM::Vec3 &out) const
    {
        NumericalEM::Vec3 p0{},dir{};if(!screenRay(screen,p0,dir))return false;
        const double denom=NumericalEM::dot(dir,normal);if(std::abs(denom)<1e-9)return false;
        out=p0+dir*(NumericalEM::dot(planePoint-p0,normal)/denom);return true;
    }

    double snapSignedLocal(double value) const
    {
        if(!(m_gridM>1e-12))return value;
        const double snapped=std::round(value/m_gridM)*m_gridM;
        return std::abs(snapped)>=0.25*m_gridM?snapped:value;
    }

    bool resizeHandleGeometry(int &kind,int &objectIndex,std::array<NumericalEM::Vec3,4> &handles,
                              NumericalEM::Vec3 *centerOut=nullptr,NumericalEM::Vec3 *uOut=nullptr,
                              NumericalEM::Vec3 *vOut=nullptr,NumericalEM::Vec3 *normalOut=nullptr) const
    {
        kind=0;objectIndex=-1;if(objectSelectionCount()!=1)return false;
        AntennaSurface::SurfaceSpec spec;double width=0.0,height=0.0;
        if(m_selectedPlanes.size()==1)
        {
            const int i=*m_selectedPlanes.begin();if(i<0||i>=static_cast<int>(m_planes.size()))return false;
            const auto&pl=m_planes[static_cast<std::size_t>(i)];if(pl.surfaceType!=0)return false;
            spec=pl.toSurfaceSpec();width=pl.widthM;height=pl.heightM;kind=1;objectIndex=i;
        }
        else if(m_selectedDielectrics.size()==1)
        {
            const int i=*m_selectedDielectrics.begin();if(i<0||i>=static_cast<int>(m_dielectrics.size()))return false;
            const auto&d=m_dielectrics[static_cast<std::size_t>(i)];spec=dielectricSurfaceSpec(d);width=d.widthM;height=d.heightM;kind=2;objectIndex=i;
        }
        else return false;
        const double hu=0.5*width,hv=0.5*height;
        handles={AntennaSurface::pointFromLocal(spec,-hu,-hv,0),AntennaSurface::pointFromLocal(spec,hu,-hv,0),
                 AntennaSurface::pointFromLocal(spec,hu,hv,0),AntennaSurface::pointFromLocal(spec,-hu,hv,0)};
        NumericalEM::Vec3 center{},u{},v{},n{};localFrame(spec,center,u,v,n);
        if(centerOut)*centerOut=center;if(uOut)*uOut=u;if(vOut)*vOut=v;if(normalOut)*normalOut=n;
        return true;
    }

    int hitResizeHandle(const QPointF &screen,const Camera &c,int &kind,int &objectIndex,
                        std::array<NumericalEM::Vec3,4> &handles,NumericalEM::Vec3 *centerOut=nullptr,
                        NumericalEM::Vec3 *uOut=nullptr,NumericalEM::Vec3 *vOut=nullptr,NumericalEM::Vec3 *normalOut=nullptr) const
    {
        if(!resizeHandleGeometry(kind,objectIndex,handles,centerOut,uOut,vOut,normalOut))return -1;
        int best=-1;double dmin=9.0;
        for(int i=0;i<4;++i){const double d=QLineF(screen,project(handles[static_cast<std::size_t>(i)],c).q).length();if(d<dmin){dmin=d;best=i;}}
        return best;
    }

    void orientedRectangleMetrics(const NumericalEM::Vec3 &opposite,const NumericalEM::Vec3 &corner,
                                  const NumericalEM::Vec3 &u,const NumericalEM::Vec3 &v,
                                  NumericalEM::Vec3 &center,double &widthM,double &heightM) const
    {
        const NumericalEM::Vec3 d=corner-opposite;const double du=NumericalEM::dot(d,u),dv=NumericalEM::dot(d,v);
        center=opposite+u*(0.5*du)+v*(0.5*dv);widthM=std::abs(du);heightM=std::abs(dv);
    }

    bool dielectricThicknessGeometry(int &objectIndex,std::array<NumericalEM::Vec3,2> &handles,
                                     NumericalEM::Vec3 &normal) const
    {
        objectIndex=-1;if(objectSelectionCount()!=1||m_selectedDielectrics.size()!=1)return false;
        const int i=*m_selectedDielectrics.begin();if(i<0||i>=static_cast<int>(m_dielectrics.size()))return false;
        const auto&d=m_dielectrics[static_cast<std::size_t>(i)];const auto spec=dielectricSurfaceSpec(d);
        NumericalEM::Vec3 center{},u{},v{};localFrame(spec,center,u,v,normal);
        handles={center-normal*(0.5*d.thicknessM),center+normal*(0.5*d.thicknessM)};objectIndex=i;return true;
    }

    int hitThicknessHandle(const QPointF &screen,const Camera &c,int &objectIndex,
                           std::array<NumericalEM::Vec3,2> &handles,NumericalEM::Vec3 &normal) const
    {
        if(!dielectricThicknessGeometry(objectIndex,handles,normal))return -1;int best=-1;double dmin=9.0;
        for(int i=0;i<2;++i){const double d=QLineF(screen,project(handles[static_cast<std::size_t>(i)],c).q).length();if(d<dmin){dmin=d;best=i;}}
        return best;
    }

    bool planeRadiusGeometry(int &objectIndex,NumericalEM::Vec3 &center,NumericalEM::Vec3 &u,
                             NumericalEM::Vec3 &v,NumericalEM::Vec3 &normal,NumericalEM::Vec3 &handle) const
    {
        objectIndex=-1;if(objectSelectionCount()!=1||m_selectedPlanes.size()!=1)return false;
        const int i=*m_selectedPlanes.begin();if(i<0||i>=static_cast<int>(m_planes.size()))return false;
        const auto&pl=m_planes[static_cast<std::size_t>(i)];if(pl.surfaceType<1||pl.surfaceType>4)return false;
        const auto spec=pl.toSurfaceSpec();localFrame(spec,center,u,v,normal);handle=center+u*pl.radiusM;objectIndex=i;return true;
    }

    bool hitRadiusHandle(const QPointF &screen,const Camera &c,int &objectIndex,NumericalEM::Vec3 &center,
                         NumericalEM::Vec3 &u,NumericalEM::Vec3 &v,NumericalEM::Vec3 &normal,NumericalEM::Vec3 &handle) const
    {
        if(!planeRadiusGeometry(objectIndex,center,u,v,normal,handle))return false;
        return QLineF(screen,project(handle,c).q).length()<9.0;
    }

    static double screenDistanceToSegment(const QPointF &p,const QPointF &a,const QPointF &b)
    {
        const QPointF ab=b-a;const double l2=QPointF::dotProduct(ab,ab);if(l2<1e-12)return QLineF(p,a).length();
        const double t=std::clamp(QPointF::dotProduct(p-a,ab)/l2,0.0,1.0);return QLineF(p,a+ab*t).length();
    }

    int hitConstraintOverlay(const QPointF &screen,const Camera &c) const
    {
        int best=-1;double dmin=11.0;
        for(const auto&o:m_constraintOverlays)
        {
            const QPointF a=project(o.a,c).q,b=project(o.b,c).q,mid=0.5*(a+b);
            const QRectF labelRect(mid.x()+3,mid.y()-21,226,25);
            if(labelRect.contains(screen))return o.index;
            const double d=screenDistanceToSegment(screen,a,b);if(d<dmin){dmin=d;best=o.index;}
        }
        return best;
    }

    struct ViewCubeFace
    {
        QPolygonF polygon;
        double depth=0.0;
        int axis=0;
        int sign=1;
        QString label;
    };

    QRectF viewCubeRect() const { return QRectF(width()-130.0,28.0,86.0,86.0); }
    QRectF viewRotateLeftRect() const { return QRectF(width()-154.0,56.0,22.0,30.0); }
    QRectF viewRotateRightRect() const { return QRectF(width()-40.0,56.0,22.0,30.0); }
    QRectF viewRotateUpRect() const { return QRectF(width()-98.0,3.0,22.0,22.0); }
    QRectF viewRotateDownRect() const { return QRectF(width()-98.0,118.0,22.0,22.0); }
    QRectF viewFitRect() const { return QRectF(width()-114.0,145.0,54.0,24.0); }
    QRectF panUpRect() const { return QRectF(width()-70.0,height()-84.0,22.0,22.0); }
    QRectF panDownRect() const { return QRectF(width()-70.0,height()-38.0,22.0,22.0); }
    QRectF panLeftRect() const { return QRectF(width()-93.0,height()-61.0,22.0,22.0); }
    QRectF panRightRect() const { return QRectF(width()-47.0,height()-61.0,22.0,22.0); }
    QRectF panStepRect() const { return QRectF(width()-70.0,height()-61.0,22.0,22.0); }
    QRectF panStepLabelRect() const { return QRectF(width()-112.0,height()-16.0,106.0,14.0); }

    QString hiddenPlaneAxisName() const
    {
        return m_editPlane==0 ? QStringLiteral("Z") : (m_editPlane==1 ? QStringLiteral("Y") : QStringLiteral("X"));
    }

    void requestEditPlane(int plane)
    {
        plane=std::clamp(plane,0,2);
        if(plane==m_editPlane)return;
        m_editPlane=plane;
        syncConstructionPlaneEditors();
        update();
        if(editPlaneChangeRequested)editPlaneChangeRequested(plane);
    }

    Proj cubeProject(const NumericalEM::Vec3 &r) const
    {
        const QRectF box=viewCubeRect();
        const QPointF center=box.center();
        const double scale=0.31*std::min(box.width(),box.height());
        const double rx=m_viewRotXDeg*NumericalEM::Pi/180.0,ry=m_viewRotYDeg*NumericalEM::Pi/180.0,rz=m_viewRotZDeg*NumericalEM::Pi/180.0;
        const double cxr=std::cos(rx),sxr=std::sin(rx),cyr=std::cos(ry),syr=std::sin(ry),czr=std::cos(rz),szr=std::sin(rz);
        const double r00=cyr*czr,r01=-cyr*szr,r02=syr;
        const double r10=cxr*szr+sxr*syr*czr,r11=cxr*czr-sxr*syr*szr,r12=-sxr*cyr;
        const double r20=sxr*szr-cxr*syr*czr,r21=sxr*czr+cxr*syr*szr,r22=cxr*cyr;
        const double xc=r00*r.x+r01*r.y+r02*r.z;
        const double yc=r10*r.x+r11*r.y+r12*r.z;
        const double zc=r20*r.x+r21*r.y+r22*r.z;
        return {QPointF(center.x()+scale*xc,center.y()-scale*zc),yc};
    }

    std::vector<ViewCubeFace> viewCubeFaces() const
    {
        const std::array<NumericalEM::Vec3,8> v{{
            {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
            {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
        }};
        struct Def{std::array<int,4> index;int axis;int sign;const char *label;};
        const std::array<Def,6> defs{{
            {{{0,3,7,4}},0,-1,"LEFT"}, {{{1,5,6,2}},0,1,"RIGHT"},
            {{{0,4,5,1}},1,-1,"FRONT"},{{{3,2,6,7}},1,1,"BACK"},
            {{{0,1,2,3}},2,-1,"BOTTOM"},{{{4,7,6,5}},2,1,"TOP"}
        }};
        std::vector<ViewCubeFace> faces;faces.reserve(defs.size());
        for(const auto&d:defs)
        {
            ViewCubeFace f;f.axis=d.axis;f.sign=d.sign;f.label=QString::fromLatin1(d.label);double dep=0.0;
            for(int i:d.index){const auto pr=cubeProject(v[static_cast<std::size_t>(i)]);f.polygon<<pr.q;dep+=pr.depth;}
            f.depth=dep/4.0;faces.push_back(std::move(f));
        }
        std::sort(faces.begin(),faces.end(),[](const auto&a,const auto&b){return a.depth>b.depth;}); // far -> near
        return faces;
    }

    int encodeCubeFace(int axis,int sign) const { return 2*axis+(sign>0?1:0); }

    int hitViewCubeFace(const QPointF &screen) const
    {
        if(!viewCubeRect().adjusted(-4,-4,4,4).contains(screen))return -1;
        const auto faces=viewCubeFaces();
        for(auto it=faces.rbegin();it!=faces.rend();++it)if(it->polygon.containsPoint(screen,Qt::OddEvenFill))return encodeCubeFace(it->axis,it->sign);
        return -1;
    }

    void orientFromCubeFace(int face)
    {
        const int axis=face/2;const int sign=(face%2)?1:-1;
        m_viewRotYDeg=0.0;
        if(axis==0){m_viewRotZDeg=sign>0?-90.0:90.0;m_viewRotXDeg=0.0;}
        else if(axis==1){m_viewRotZDeg=sign>0?180.0:0.0;m_viewRotXDeg=0.0;}
        else{m_viewRotZDeg=0.0;m_viewRotXDeg=sign>0?90.0:-90.0;}
        m_pan=QPointF();syncViewAngleEditors();update();
    }

    void rotateViewQuarterStep(int direction)
    {
        m_viewRotZDeg += direction>=0 ? 45.0 : -45.0;
        while(m_viewRotZDeg>180.0)m_viewRotZDeg-=360.0;
        while(m_viewRotZDeg<-180.0)m_viewRotZDeg+=360.0;
        syncViewAngleEditors();update();
    }

    void rotateViewPitchStep(int direction)
    {
        m_viewRotXDeg += direction>=0 ? 45.0 : -45.0;
        while(m_viewRotXDeg>180.0)m_viewRotXDeg-=360.0;
        while(m_viewRotXDeg<-180.0)m_viewRotXDeg+=360.0;
        syncViewAngleEditors();update();
    }

    double panStepMm() const
    {
        static constexpr std::array<double,4> steps{{1.0,10.0,50.0,100.0}};
        return steps[static_cast<std::size_t>(std::clamp(m_panStepIndex,0,3))];
    }

    void cyclePanStep()
    {
        m_panStepIndex=(m_panStepIndex+1)%4;update();
    }

    void panViewByStep(int dx,int dy)
    {
        const Camera c=camera();
        const double pixels=(panStepMm()/MmPerM)*c.scale;
        m_pan+=QPointF(dx*pixels,dy*pixels);
        update();
    }

    void panViewDirection(int direction)
    {
        if(direction==0)panViewByStep(0,1);else if(direction==1)panViewByStep(0,-1);else if(direction==2)panViewByStep(1,0);else if(direction==3)panViewByStep(-1,0);
    }

    void startPanHold(int direction)
    {
        stopPanHold();m_panHoldDirection=direction;panViewDirection(direction);if(m_panHoldDelay)m_panHoldDelay->start();
    }

    void stopPanHold()
    {
        if(m_panHoldDelay)m_panHoldDelay->stop();if(m_panRepeatTimer)m_panRepeatTimer->stop();m_panHoldDirection=-1;
    }

    void drawViewCube(QPainter &p,const QPalette &pal)
    {
        const auto faces=viewCubeFaces();
        for(const auto&f:faces)
        {
            const int id=encodeCubeFace(f.axis,f.sign);
            QColor fill=pal.color(id==m_hoverCubeFace?QPalette::Highlight:QPalette::Button);
            fill.setAlpha(id==m_hoverCubeFace?205:225);
            p.setPen(QPen(pal.color(QPalette::Mid),1.0));p.setBrush(fill);p.drawPolygon(f.polygon);
            const QRectF br=f.polygon.boundingRect();
            if(br.width()>20.0&&br.height()>14.0){p.setPen(id==m_hoverCubeFace?pal.color(QPalette::HighlightedText):pal.color(QPalette::ButtonText));p.drawText(br,Qt::AlignCenter,f.label);}
        }
        p.setBrush(Qt::NoBrush);
        auto drawNav=[&](const QRectF&r,const QString&text,bool hover){
            QColor bg=pal.color(hover?QPalette::Highlight:QPalette::Base);bg.setAlpha(225);
            p.setPen(QPen(pal.color(QPalette::Mid),1.0));p.setBrush(bg);p.drawRoundedRect(r,4,4);
            p.setPen(hover?pal.color(QPalette::HighlightedText):pal.color(QPalette::Text));p.drawText(r,Qt::AlignCenter,text);
        };
        drawNav(viewRotateLeftRect(),QStringLiteral("↶"),m_hoverCubeControl==2);
        drawNav(viewRotateRightRect(),QStringLiteral("↷"),m_hoverCubeControl==3);
        drawNav(viewRotateUpRect(),QStringLiteral("↥"),m_hoverCubeControl==4);
        drawNav(viewRotateDownRect(),QStringLiteral("↧"),m_hoverCubeControl==5);
        drawNav(viewFitRect(),QStringLiteral("Fit"),m_hoverCubeControl==1);
        // Discreet keyboard/touch fallback for camera panning when no middle mouse button is available.
        const std::array<std::pair<QRectF,QString>,4> panButtons{{{panUpRect(),QStringLiteral("↑")},{panDownRect(),QStringLiteral("↓")},{panLeftRect(),QStringLiteral("←")},{panRightRect(),QStringLiteral("→")}}};
        for(const auto &button:panButtons)drawNav(button.first,button.second,false);
        drawNav(panStepRect(),QStringLiteral("▪"),false);
        p.setPen(pal.color(QPalette::PlaceholderText));
        QFont small=p.font();small.setPointSizeF(std::max(6.0,small.pointSizeF()-2.0));p.setFont(small);
        p.drawText(panStepLabelRect(),Qt::AlignCenter,QStringLiteral("step %1 mm").arg(panStepMm(),0,'g',4));
        p.setBrush(Qt::NoBrush);
    }

    NumericalEM::Vec3 arbitraryAxisDraggedPoint(const QPointF &screen,const QPointF &screenStart,
                                                const NumericalEM::Vec3 &worldStart,const NumericalEM::Vec3 &axis,
                                                const Camera &c) const
    {
        const double axisLen=0.16*c.span/std::max(0.25,m_zoom);const QPointF a=project(worldStart,c).q,b=project(worldStart+axis*axisLen,c).q;
        const QPointF sv=b-a;const double l2=QPointF::dotProduct(sv,sv);if(l2<1e-12)return worldStart;
        const double factor=QPointF::dotProduct(screen-screenStart,sv)/l2;return worldStart+axis*(axisLen*factor);
    }

    bool selectObjectForContext(const QPointF &sp)
    {
        const Camera c=camera();
        const int fi=hitFeed(sp,c);if(fi>=0){clearObjectSelection();m_selectedFeeds.insert(fi);expandObjectSelectionToGroups();m_selectionKind=6;m_selectedPoint=selectionCentroid();refreshSelectionEditor();update();return true;}
        NumericalEM::Vec3 node{};if(hitNode(sp,c,node)){clearObjectSelection();m_selectionKind=1;m_selectedPoint=node;refreshSelectionEditor();update();return true;}
        const int wi=hitWire(sp,c);if(wi>=0){clearObjectSelection();m_selectedWires.insert(wi);expandObjectSelectionToGroups();m_selectionKind=6;m_selectedPoint=selectionCentroid();refreshSelectionEditor();update();return true;}
        const int di=hitDielectric(sp,c);if(di>=0){clearObjectSelection();m_selectedDielectrics.insert(di);expandObjectSelectionToGroups();m_selectionKind=6;m_selectedPoint=selectionCentroid();refreshSelectionEditor();update();return true;}
        const int pi=hitPlane(sp,c);if(pi>=0){clearObjectSelection();m_selectedPlanes.insert(pi);expandObjectSelectionToGroups();m_selectionKind=6;m_selectedPoint=selectionCentroid();refreshSelectionEditor();update();return true;}
        return false;
    }

    void placePivotAtScreen(const QPointF &sp)
    {
        NumericalEM::Vec3 p{};if(!pointOnConstructionPlane(sp,p))return;m_customPivot=p;m_pivotMode=3;if(pivotPicked)pivotPicked(p);update();
    }

    void showContextMenu(QMouseEvent *event)
    {
        const QPointF sp=event->position();
        const Camera c=camera();const int fi=hitFeed(sp,c),wi=hitWire(sp,c),pi=hitPlane(sp,c),di=hitDielectric(sp,c);NumericalEM::Vec3 node{};const bool ni=hitNode(sp,c,node);
        const bool clickedAny=fi>=0||wi>=0||pi>=0||di>=0||ni;
        const bool clickedSelected=(fi>=0&&m_selectedFeeds.count(fi))||(wi>=0&&m_selectedWires.count(wi))||(pi>=0&&m_selectedPlanes.count(pi))||(di>=0&&m_selectedDielectrics.count(di))||(ni&&m_selectionKind==1&&NumericalEM::norm(node-m_selectedPoint)<1e-8);
        if(clickedAny&&!clickedSelected)selectObjectForContext(sp);
        QMenu menu(this);
        const bool hasSelection=objectSelectionCount()>0||m_selectionKind==1;
        if(hasSelection)
        {
            if(objectSelectionCount()>0)
            {
                QAction *dup=menu.addAction(QStringLiteral("Duplicate"));QObject::connect(dup,&QAction::triggered,this,[this]{if(duplicateRequested)duplicateRequested();});
                if(objectSelectionCount()>1)
                {
                    QAction *groupAction=menu.addAction(QStringLiteral("Group selected"));
                    QObject::connect(groupAction,&QAction::triggered,this,[this]{if(groupCreateRequested)groupCreateRequested();});
                }
                if(selectionTouchesGroup())
                {
                    QAction *ungroupAction=menu.addAction(QStringLiteral("Ungroup selected group(s)"));
                    QObject::connect(ungroupAction,&QAction::triggered,this,[this]{if(groupUngroupRequested)groupUngroupRequested();});
                }
                QMenu *rotate=menu.addMenu(QStringLiteral("Rotate"));
                NumericalEM::Vec3 cpOrigin{},cpU{},cpV{},cpNormal{};constructionPlaneFrame(cpOrigin,cpU,cpV,cpNormal);
                QAction *planePos=rotate->addAction(QStringLiteral("+45° in construction plane"));QAction *planeNeg=rotate->addAction(QStringLiteral("-45° in construction plane"));
                QObject::connect(planePos,&QAction::triggered,this,[this,cpNormal]{if(constructionPlaneRotateRequested)constructionPlaneRotateRequested(45.0,transformPivot(),cpNormal);});
                QObject::connect(planeNeg,&QAction::triggered,this,[this,cpNormal]{if(constructionPlaneRotateRequested)constructionPlaneRotateRequested(-45.0,transformPivot(),cpNormal);});
                rotate->addSeparator();
                for(int axis=0;axis<3;++axis)
                {
                    const QString axisName=axis==0?QStringLiteral("X"):axis==1?QStringLiteral("Y"):QStringLiteral("Z");
                    QAction *pos=rotate->addAction(QStringLiteral("+45° around %1").arg(axisName));QAction *neg=rotate->addAction(QStringLiteral("-45° around %1").arg(axisName));
                    QObject::connect(pos,&QAction::triggered,this,[this,axis]{if(groupRotateRequested)groupRotateRequested(asVector(m_selectedWires),asVector(m_selectedFeeds),asVector(m_selectedPlanes),asVector(m_selectedDielectrics),axis,45.0,transformPivot());});
                    QObject::connect(neg,&QAction::triggered,this,[this,axis]{if(groupRotateRequested)groupRotateRequested(asVector(m_selectedWires),asVector(m_selectedFeeds),asVector(m_selectedPlanes),asVector(m_selectedDielectrics),axis,-45.0,transformPivot());});
                }
                if(objectSelectionCount()>1)
                {
                    QMenu *align=menu.addMenu(QStringLiteral("Align"));
                    for(int axis=0;axis<3;++axis){const QString name=axis==0?QStringLiteral("X"):axis==1?QStringLiteral("Y"):QStringLiteral("Z");QAction *a=align->addAction(name);QObject::connect(a,&QAction::triggered,this,[this,axis]{if(alignRequested)alignRequested(axis);});}
                    if((m_selectedPlanes.size()+m_selectedDielectrics.size())>1)
                    {
                        QAction *coincident=menu.addAction(QStringLiteral("Coincident centers"));
                        QObject::connect(coincident,&QAction::triggered,this,[this]{if(coincidentCentersRequested)coincidentCentersRequested();});
                        QAction *match=menu.addAction(QStringLiteral("Match W/H"));
                        QObject::connect(match,&QAction::triggered,this,[this]{if(matchSurfaceSizeRequested)matchSurfaceSizeRequested();});
                    }
                }
                QMenu *mirror=menu.addMenu(QStringLiteral("Mirror copy"));
                for(int axis=0;axis<3;++axis){const QString name=axis==0?QStringLiteral("X"):axis==1?QStringLiteral("Y"):QStringLiteral("Z");QAction *a=mirror->addAction(name);QObject::connect(a,&QAction::triggered,this,[this,axis]{if(mirrorRequested)mirrorRequested(axis,transformPivot());});}
                menu.addSeparator();
            }
            QAction *constraint=menu.addAction(QStringLiteral("Add constraint…"));
            constraint->setToolTip(QStringLiteral("Open Geometry → Constraints with the current 3D selection preloaded as constraint references."));
            QObject::connect(constraint,&QAction::triggered,this,[this]{if(constraintSetupRequested)constraintSetupRequested(asVector(m_selectedWires),asVector(m_selectedFeeds),asVector(m_selectedPlanes),asVector(m_selectedDielectrics),m_selectionKind==1,m_selectedPoint);});
            QAction *pivot=menu.addAction(QStringLiteral("Place rotation pivot here"));QObject::connect(pivot,&QAction::triggered,this,[this,sp]{placePivotAtScreen(sp);});
            QAction *del=menu.addAction(QStringLiteral("Delete selected…"));QObject::connect(del,&QAction::triggered,this,[this]{if(geometryDeleteSelectionRequested)geometryDeleteSelectionRequested(asVector(m_selectedWires),asVector(m_selectedFeeds),asVector(m_selectedPlanes),asVector(m_selectedDielectrics),m_selectionKind==1,m_selectedPoint);});
        }
        else
        {
            QMenu *create=menu.addMenu(QStringLiteral("Create"));
            const std::array<std::pair<QString,int>,4> tools{{{QStringLiteral("Wire"),1},{QStringLiteral("Feed"),2},{QStringLiteral("PEC rectangle"),4},{QStringLiteral("Dielectric substrate"),5}}};
            for(const auto&t:tools){QAction *a=create->addAction(t.first);QObject::connect(a,&QAction::triggered,this,[this,t]{if(editorToolRequested)editorToolRequested(t.second);});}
            QMenu *primitive=create->addMenu(QStringLiteral("3D primitive…"));
            const std::array<QString,12> primitiveNames{{QStringLiteral("Circular loop"),QStringLiteral("Circular arc"),QStringLiteral("Rectangular loop"),QStringLiteral("Helix / solenoid wire"),QStringLiteral("Planar spiral"),QStringLiteral("Meander / serpentine wire"),QStringLiteral("Rectangular plane / sheet"),QStringLiteral("Disk / annular sheet"),QStringLiteral("Cylindrical shell"),QStringLiteral("Conical shell"),QStringLiteral("Parabolic reflector / dish"),QStringLiteral("Dielectric substrate / slab")}};
            NumericalEM::Vec3 primitivePoint{};const bool primitivePointValid=pointOnConstructionPlane(sp,primitivePoint);
            for(int type=0;type<static_cast<int>(primitiveNames.size());++type){QAction *a=primitive->addAction(primitiveNames[static_cast<std::size_t>(type)]);a->setEnabled(primitivePointValid);QObject::connect(a,&QAction::triggered,this,[this,type,primitivePoint]{if(primitiveSetupRequested)primitiveSetupRequested(type,primitivePoint);});}
            QAction *measure=menu.addAction(QStringLiteral("Measure distance / angle"));QObject::connect(measure,&QAction::triggered,this,[this]{if(editorToolRequested)editorToolRequested(6);});
            QAction *pivot=menu.addAction(QStringLiteral("Place rotation pivot here"));QObject::connect(pivot,&QAction::triggered,this,[this,sp]{placePivotAtScreen(sp);});
            menu.addSeparator();QAction *fit=menu.addAction(QStringLiteral("Fit view"));QObject::connect(fit,&QAction::triggered,this,[this]{fitGeometry();});
        }
        menu.exec(event->globalPosition().toPoint());
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);layoutOverlayWidgets();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        setFocus(Qt::MouseFocusReason);
        if(event->button()==Qt::LeftButton)
        {
            const QPointF vp=event->position();
            if(viewFitRect().contains(vp)){fitGeometry();event->accept();return;}
            if(viewRotateLeftRect().contains(vp)){rotateViewQuarterStep(1);event->accept();return;}
            if(viewRotateRightRect().contains(vp)){rotateViewQuarterStep(-1);event->accept();return;}
            if(viewRotateUpRect().contains(vp)){rotateViewPitchStep(1);event->accept();return;}
            if(viewRotateDownRect().contains(vp)){rotateViewPitchStep(-1);event->accept();return;}
            if(panStepRect().contains(vp)){cyclePanStep();event->accept();return;}
            if(panUpRect().contains(vp)){startPanHold(0);event->accept();return;}
            if(panDownRect().contains(vp)){startPanHold(1);event->accept();return;}
            if(panLeftRect().contains(vp)){startPanHold(2);event->accept();return;}
            if(panRightRect().contains(vp)){startPanHold(3);event->accept();return;}
            const int cubeFace=hitViewCubeFace(vp);if(cubeFace>=0){orientFromCubeFace(cubeFace);event->accept();return;}
        }
        if(event->button()==Qt::MiddleButton)
        {m_draggingCamera=true;m_panning=true;m_lastMouse=event->position();event->accept();return;}
        if(event->button()==Qt::RightButton){showContextMenu(event);event->accept();return;}
        if(event->button()!=Qt::LeftButton){QWidget::mousePressEvent(event);return;}
        const QPointF sp=event->position();const Camera c=camera();
        if(m_tool==Tool::Select)
        {
            if(m_gizmoMode==0)
            {
                int resizeKind=0,resizeObject=-1;std::array<NumericalEM::Vec3,4> handles{};
                NumericalEM::Vec3 resizeCenter{},resizeU{},resizeV{},resizeNormal{};
                const int resizeHandle=hitResizeHandle(sp,c,resizeKind,resizeObject,handles,&resizeCenter,&resizeU,&resizeV,&resizeNormal);
                if(resizeHandle>=0)
                {
                    m_resizing=true;m_resizeKind=resizeKind;m_resizeObjectIndex=resizeObject;m_resizeHandleIndex=resizeHandle;
                    m_resizeOpposite=handles[static_cast<std::size_t>((resizeHandle+2)%4)];
                    m_resizePreview=handles[static_cast<std::size_t>(resizeHandle)];m_resizePlanePoint=resizeCenter;
                    m_resizeU=resizeU;m_resizeV=resizeV;m_resizeNormal=resizeNormal;
                    event->accept();return;
                }
                int thicknessObject=-1;std::array<NumericalEM::Vec3,2> thicknessHandles{};NumericalEM::Vec3 thicknessNormal{};
                const int thicknessHandle=hitThicknessHandle(sp,c,thicknessObject,thicknessHandles,thicknessNormal);
                if(thicknessHandle>=0)
                {
                    m_thicknessResizing=true;m_thicknessObjectIndex=thicknessObject;m_thicknessHandleIndex=thicknessHandle;
                    m_thicknessOpposite=thicknessHandles[static_cast<std::size_t>(1-thicknessHandle)];
                    m_thicknessPreview=thicknessHandles[static_cast<std::size_t>(thicknessHandle)];m_thicknessAxis=thicknessNormal;
                    if(thicknessHandle==0)m_thicknessAxis=m_thicknessAxis*(-1.0);m_thicknessDragStartScreen=sp;m_thicknessDragStartWorld=m_thicknessPreview;
                    event->accept();return;
                }
                int radiusObject=-1;NumericalEM::Vec3 radiusCenter{},radiusU{},radiusV{},radiusNormal{},radiusHandle{};
                if(hitRadiusHandle(sp,c,radiusObject,radiusCenter,radiusU,radiusV,radiusNormal,radiusHandle))
                {
                    m_radiusResizing=true;m_radiusObjectIndex=radiusObject;m_radiusCenter=radiusCenter;m_radiusU=radiusU;m_radiusV=radiusV;m_radiusNormal=radiusNormal;
                    m_radiusPreview=radiusHandle;event->accept();return;
                }
            }
            if(m_gizmoMode==1 && m_selectionKind==6)
            {
                double startAngle=0.0;const int rotationAxis=hitRotationAxis(sp,c,&startAngle);
                if(rotationAxis>=0){m_dragRotationAxis=rotationAxis;m_rotationStartAngleRad=startAngle;m_rotationPreviewDeg=0.0;event->accept();return;}
            }
            if(m_gizmoMode==0 || m_selectionKind==1){const int axis=hitGizmoAxis(sp,c);if(axis>=0){beginSelectionDrag(sp,axis);event->accept();return;}}
            const int constraintHit=hitConstraintOverlay(sp,c);
            if(constraintHit>=0)
            {
                clearObjectSelection();m_selectionKind=0;m_selectedConstraint=constraintHit;refreshSelectionEditor();
                if(constraintSelected)constraintSelected(constraintHit);update();event->accept();return;
            }
            const bool additive=(event->modifiers()&Qt::ControlModifier);
            auto selectObject=[&](std::set<int>&bucket,int index)
            {
                m_selectedConstraint=-1;if(constraintSelected)constraintSelected(-1);
                const bool already=bucket.count(index)>0;
                if(additive){if(already)bucket.erase(index);else bucket.insert(index);}
                else if(!(already&&objectSelectionCount()>1)){clearObjectSelection();bucket.insert(index);}
                if(objectSelectionCount()==0){m_selectionKind=0;refreshSelectionEditor();update();return;}
                expandObjectSelectionToGroups();
                m_selectionKind=6;m_selectedPoint=selectionCentroid();refreshSelectionEditor();beginSelectionDrag(sp,-1);update();
            };
            NumericalEM::Vec3 node{};const int fi=hitFeed(sp,c);
            if(fi>=0){selectObject(m_selectedFeeds,fi);event->accept();return;}
            if(hitNode(sp,c,node)){m_selectedConstraint=-1;if(constraintSelected)constraintSelected(-1);clearObjectSelection();m_selectionKind=1;m_selectedPoint=node;refreshSelectionEditor();beginSelectionDrag(sp,-1);update();event->accept();return;}
            const int wi=hitWire(sp,c);if(wi>=0){selectObject(m_selectedWires,wi);event->accept();return;}
            const int di=hitDielectric(sp,c);if(di>=0){selectObject(m_selectedDielectrics,di);event->accept();return;}
            const int pi=hitPlane(sp,c);if(pi>=0){selectObject(m_selectedPlanes,pi);event->accept();return;}
            if(additive){m_boxSelecting=true;m_boxStart=sp;m_boxCurrent=sp;update();event->accept();return;}
            clearObjectSelection();m_selectionKind=0;m_selectedConstraint=-1;if(constraintSelected)constraintSelected(-1);refreshSelectionEditor();m_draggingCamera=true;m_panning=false;m_lastMouse=sp;update();event->accept();return;
        }
        NumericalEM::Vec3 p{};if(!pointOnConstructionPlane(sp,p)){event->accept();return;}
        if(m_tool==Tool::AddWire){p=magneticConstructionPoint(p,c);m_wireStart=p;m_wirePreview=p;m_drawingWire=true;event->accept();return;}
        if(m_tool==Tool::AddFeed){p=magneticConstructionPoint(p,c);if(feedCreated)feedCreated(p);if(editorToolRequested)editorToolRequested(0);event->accept();return;}
        if(m_tool==Tool::AddRectPEC||m_tool==Tool::AddDielectric){m_rectStart=p;m_rectPreview=p;m_drawingRect=true;event->accept();return;}
        if(m_tool==Tool::Measure)
        {
            if(!m_measureHasA||m_measureComplete){m_measureA=p;m_measureB=p;m_measureHasA=true;m_measureComplete=false;m_measureHover=true;}
            else{m_measureB=p;m_measureComplete=true;m_measureHover=false;}
            update();event->accept();return;
        }
        if(m_tool==Tool::PickPivot)
        {
            m_customPivot=p;m_pivotMode=3;
            if(pivotPicked)pivotPicked(p);
            if(editorToolRequested)editorToolRequested(0);
            update();event->accept();return;
        }
        if(m_tool==Tool::Delete){if(deleteRequested)deleteRequested(p);event->accept();return;}
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if(!(event->buttons()&(Qt::LeftButton|Qt::RightButton|Qt::MiddleButton)))
        {
            const int face=hitViewCubeFace(event->position());
            const int control=viewFitRect().contains(event->position())?1:
                              (viewRotateLeftRect().contains(event->position())?2:
                              (viewRotateRightRect().contains(event->position())?3:
                              (viewRotateUpRect().contains(event->position())?4:
                              (viewRotateDownRect().contains(event->position())?5:0))));
            if(face!=m_hoverCubeFace||control!=m_hoverCubeControl){m_hoverCubeFace=face;m_hoverCubeControl=control;update();}
        }
        if(m_resizing && (event->buttons()&Qt::LeftButton))
        {
            NumericalEM::Vec3 q;if(pointOnArbitraryPlane(event->position(),m_resizePlanePoint,m_resizeNormal,q))
            {
                const NumericalEM::Vec3 d=q-m_resizeOpposite;double du=NumericalEM::dot(d,m_resizeU),dv=NumericalEM::dot(d,m_resizeV);
                du=snapSignedLocal(du);dv=snapSignedLocal(dv);m_resizePreview=m_resizeOpposite+m_resizeU*du+m_resizeV*dv;
            }
            update();event->accept();return;
        }
        if(m_thicknessResizing && (event->buttons()&Qt::LeftButton))
        {
            m_thicknessPreview=arbitraryAxisDraggedPoint(event->position(),m_thicknessDragStartScreen,m_thicknessDragStartWorld,m_thicknessAxis,camera());
            update();event->accept();return;
        }
        if(m_radiusResizing && (event->buttons()&Qt::LeftButton))
        {
            NumericalEM::Vec3 q;if(pointOnArbitraryPlane(event->position(),m_radiusCenter,m_radiusNormal,q))
            {
                const NumericalEM::Vec3 d=q-m_radiusCenter;double ru=NumericalEM::dot(d,m_radiusU),rv=NumericalEM::dot(d,m_radiusV);
                double radius=std::hypot(ru,rv);if(m_gridM>1e-12)radius=std::max(0.25*m_gridM,std::round(radius/m_gridM)*m_gridM);
                NumericalEM::Vec3 radial=m_radiusU;if(std::hypot(ru,rv)>1e-12)radial=unitOr(m_radiusU*ru+m_radiusV*rv,m_radiusU);
                m_radiusPreview=m_radiusCenter+radial*radius;
            }
            update();event->accept();return;
        }
        if(m_dragRotationAxis>=0 && (event->buttons()&Qt::LeftButton))
        {
            const double current=nearestRotationRingAngle(event->position(),camera(),m_dragRotationAxis);
            m_rotationPreviewDeg=wrappedAngleDeg((current-m_rotationStartAngleRad)*180.0/NumericalEM::Pi);if(event->modifiers()&Qt::ControlModifier)m_rotationPreviewDeg=15.0*std::round(m_rotationPreviewDeg/15.0);update();event->accept();return;
        }
        if(m_boxSelecting && (event->buttons()&Qt::LeftButton)){m_boxCurrent=event->position();update();event->accept();return;}
        if(m_draggingSelection && (event->buttons()&Qt::LeftButton))
        {
            const Camera c=camera();NumericalEM::Vec3 q=m_dragStartWorld;
            if(m_dragAxis>=0)q=axisDraggedPoint(event->position(),c);
            else
            {
                NumericalEM::Vec3 hit{};
                // Preserve the click-to-object offset during free dragging.  Construction-grid
                // snapping remains for drawing and constrained gizmos; free object moves follow
                // the cursor continuously and only use the optional object-to-object snap below.
                if(pointOnConstructionPlaneRaw(event->position(),hit))q=m_dragStartWorld+(hit-m_dragCursorStartWorld);
            }
            q=objectSnappedPoint(q,c);m_selectedPoint=q;refreshSelectionEditor();update();event->accept();return;
        }
        if(m_drawingWire && (event->buttons()&Qt::LeftButton)){NumericalEM::Vec3 q;if(pointOnConstructionPlane(event->position(),q))m_wirePreview=magneticConstructionPoint(q,camera());update();event->accept();return;}
        if(m_drawingRect && (event->buttons()&Qt::LeftButton)){NumericalEM::Vec3 q;if(pointOnConstructionPlane(event->position(),q))m_rectPreview=q;update();event->accept();return;}
        if(m_tool==Tool::Measure && m_measureHasA && !m_measureComplete)
        {
            NumericalEM::Vec3 q;if(pointOnConstructionPlane(event->position(),q)){m_measureB=q;m_measureHover=true;update();}
        }
        if(m_draggingCamera)
        {
            const QPointF d=event->position()-m_lastMouse;m_lastMouse=event->position();
            if(m_panning)m_pan+=d;else{m_viewRotZDeg+=0.65*d.x();m_viewRotXDeg=std::clamp(m_viewRotXDeg+0.65*d.y(),-179.0,179.0);syncViewAngleEditors();}update();event->accept();return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if(event->button()==Qt::LeftButton && m_panHoldDirection>=0){stopPanHold();event->accept();return;}
        if(event->button()==Qt::LeftButton && m_resizing)
        {
            m_resizing=false;NumericalEM::Vec3 q=m_resizePreview;NumericalEM::Vec3 planeHit{};
            if(pointOnArbitraryPlane(event->position(),m_resizePlanePoint,m_resizeNormal,planeHit))
            {
                const NumericalEM::Vec3 d=planeHit-m_resizeOpposite;double du=snapSignedLocal(NumericalEM::dot(d,m_resizeU)),dv=snapSignedLocal(NumericalEM::dot(d,m_resizeV));
                q=m_resizeOpposite+m_resizeU*du+m_resizeV*dv;
            }
            NumericalEM::Vec3 center{};double widthM=0.0,heightM=0.0;orientedRectangleMetrics(m_resizeOpposite,q,m_resizeU,m_resizeV,center,widthM,heightM);
            const double minSide=std::max(1e-9,0.25*m_gridM);
            if(widthM>=minSide&&heightM>=minSide)
            {
                if(m_resizeKind==1&&planeResizeRequested)planeResizeRequested(m_resizeObjectIndex,center,widthM,heightM);
                else if(m_resizeKind==2&&dielectricResizeRequested)dielectricResizeRequested(m_resizeObjectIndex,center,widthM,heightM);
            }
            m_resizeKind=0;m_resizeObjectIndex=-1;m_resizeHandleIndex=-1;update();event->accept();return;
        }
        if(event->button()==Qt::LeftButton && m_thicknessResizing)
        {
            m_thicknessResizing=false;const NumericalEM::Vec3 q=m_thicknessPreview;const double thickness=NumericalEM::norm(q-m_thicknessOpposite);
            if(thickness>1e-9&&dielectricThicknessRequested)dielectricThicknessRequested(m_thicknessObjectIndex,(q+m_thicknessOpposite)*0.5,thickness);
            m_thicknessObjectIndex=-1;m_thicknessHandleIndex=-1;update();event->accept();return;
        }
        if(event->button()==Qt::LeftButton && m_radiusResizing)
        {
            m_radiusResizing=false;const double radius=NumericalEM::norm(m_radiusPreview-m_radiusCenter);
            if(radius>1e-9&&planeRadiusRequested)planeRadiusRequested(m_radiusObjectIndex,radius);
            m_radiusObjectIndex=-1;update();event->accept();return;
        }
        if(event->button()==Qt::LeftButton && m_dragRotationAxis>=0)
        {
            const int axis=m_dragRotationAxis;const double angle=m_rotationPreviewDeg;const NumericalEM::Vec3 pivot=transformPivot();m_dragRotationAxis=-1;m_rotationPreviewDeg=0.0;
            if(std::abs(angle)>1e-8 && groupRotateRequested && objectSelectionCount()>0)groupRotateRequested(asVector(m_selectedWires),asVector(m_selectedFeeds),asVector(m_selectedPlanes),asVector(m_selectedDielectrics),axis,angle,pivot);
            update();event->accept();return;
        }
        if(event->button()==Qt::LeftButton && m_boxSelecting)
        {
            m_boxSelecting=false;QRectF box(m_boxStart,m_boxCurrent);box=box.normalized();if(box.width()>4.0&&box.height()>4.0)addSelectionInRect(box,camera());update();event->accept();return;
        }
        if(event->button()==Qt::LeftButton && m_draggingSelection)
        {
            m_draggingSelection=false;const NumericalEM::Vec3 end=m_selectedPoint;
            if(NumericalEM::norm(end-m_dragStartWorld)>1e-10)
            {
                if(m_selectionKind==1 && nodeMoveRequested)nodeMoveRequested(m_dragStartWorld,end);
                else
                {
                    const NumericalEM::Vec3 delta=end-m_dragStartWorld;
                    const int count=objectSelectionCount();
                    if(count==1 && m_selectedFeeds.size()==1 && feedMoveRequested)feedMoveRequested(*m_selectedFeeds.begin(),end);
                    else if(count==1 && m_selectedWires.size()==1 && wireTranslateRequested)wireTranslateRequested(*m_selectedWires.begin(),delta);
                    else if(count==1 && m_selectedPlanes.size()==1 && planeTranslateRequested)planeTranslateRequested(*m_selectedPlanes.begin(),delta);
                    else if(count==1 && m_selectedDielectrics.size()==1 && dielectricTranslateRequested)dielectricTranslateRequested(*m_selectedDielectrics.begin(),delta);
                    else if(count>0 && groupTranslateRequested)groupTranslateRequested(asVector(m_selectedWires),asVector(m_selectedFeeds),asVector(m_selectedPlanes),asVector(m_selectedDielectrics),delta);
                }
            }
            event->accept();return;
        }
        if(event->button()==Qt::LeftButton && m_drawingWire)
        {
            m_drawingWire=false;NumericalEM::Vec3 end=m_wirePreview;if(pointOnConstructionPlane(event->position(),end))end=magneticConstructionPoint(end,camera());
            if(NumericalEM::norm(end-m_wireStart)>1e-8 && wireCreated){wireCreated(m_wireStart,end);if(editorToolRequested)editorToolRequested(0);}update();event->accept();return;
        }
        if(event->button()==Qt::LeftButton && m_drawingRect)
        {
            m_drawingRect=false;NumericalEM::Vec3 end=m_rectPreview;pointOnConstructionPlane(event->position(),end);
            if(validPlaneRectangle(m_rectStart,end))
            {
                if(m_tool==Tool::AddRectPEC&&rectPecCreated)rectPecCreated(m_rectStart,end);
                else if(m_tool==Tool::AddDielectric&&dielectricCreated)dielectricCreated(m_rectStart,end);
                if(editorToolRequested)editorToolRequested(0);
            }
            update();event->accept();return;
        }
        if(m_draggingCamera && (event->button()==Qt::LeftButton||event->button()==Qt::MiddleButton)){m_draggingCamera=false;m_panning=false;event->accept();return;}
        QWidget::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if((event->key()==Qt::Key_Delete||event->key()==Qt::Key_Backspace) && m_selectedConstraint>=0)
        {
            const int index=m_selectedConstraint;m_selectedConstraint=-1;if(constraintDeleteRequested)constraintDeleteRequested(index);event->accept();return;
        }
        if((event->key()==Qt::Key_Delete||event->key()==Qt::Key_Backspace) && (objectSelectionCount()>0||m_selectionKind==1))
        {
            if(geometryDeleteSelectionRequested)geometryDeleteSelectionRequested(asVector(m_selectedWires),asVector(m_selectedFeeds),asVector(m_selectedPlanes),asVector(m_selectedDielectrics),m_selectionKind==1,m_selectedPoint);
            event->accept();return;
        }
        if(event->key()==Qt::Key_Escape && m_selectedConstraint>=0)
        {
            m_selectedConstraint=-1;if(constraintSelected)constraintSelected(-1);update();event->accept();return;
        }
        QWidget::keyPressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if(event->button()==Qt::LeftButton)
        {
            const QPointF vp=event->position();
            if(viewFitRect().contains(vp)){fitGeometry();event->accept();return;}
            // A Qt double-click replaces the second press event. Apply the same navigation
            // action here so two rapid clicks mean two deterministic steps instead of the
            // historical fall-through that reset/orbited/zoomed the camera.
            if(viewRotateLeftRect().contains(vp)){rotateViewQuarterStep(1);event->accept();return;}
            if(viewRotateRightRect().contains(vp)){rotateViewQuarterStep(-1);event->accept();return;}
            if(viewRotateUpRect().contains(vp)){rotateViewPitchStep(1);event->accept();return;}
            if(viewRotateDownRect().contains(vp)){rotateViewPitchStep(-1);event->accept();return;}
            if(panStepRect().contains(vp)){cyclePanStep();event->accept();return;}
            if(panUpRect().contains(vp)){panViewByStep(0,1);event->accept();return;}
            if(panDownRect().contains(vp)){panViewByStep(0,-1);event->accept();return;}
            if(panLeftRect().contains(vp)){panViewByStep(1,0);event->accept();return;}
            if(panRightRect().contains(vp)){panViewByStep(-1,0);event->accept();return;}
            const int cubeFace=hitViewCubeFace(vp);if(cubeFace>=0){orientFromCubeFace(cubeFace);event->accept();return;}
        }
        if(event->button()==Qt::LeftButton && m_tool==Tool::Select)
        {
            const int constraintHit=hitConstraintOverlay(event->position(),camera());
            if(constraintHit>=0){m_selectedConstraint=constraintHit;if(constraintSelected)constraintSelected(constraintHit);if(constraintEditRequested)constraintEditRequested(constraintHit);event->accept();return;}
        }
        if(event->button()==Qt::LeftButton){m_viewRotXDeg=24.0;m_viewRotYDeg=0.0;m_viewRotZDeg=-35.0;m_zoom=1.0;m_pan=QPointF();syncViewAngleEditors();update();event->accept();return;}
        QWidget::mouseDoubleClickEvent(event);
    }
    void wheelEvent(QWheelEvent *event) override
    {
        // Deep CAD zoom for precise endpoint / small-wire placement.
        m_zoom=std::clamp(m_zoom*(event->angleDelta().y()>0?1.15:1.0/1.15),0.18,40.0);
        update();event->accept();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing,true);const QPalette pal=palette();p.fillRect(rect(),pal.color(QPalette::Base));
        const Camera c=camera();

        // Active construction plane/grid. It is deliberately finite and camera-projected so the
        // user always sees where a mouse click will be converted back into XYZ coordinates.
        QColor grid=pal.color(QPalette::Mid);grid.setAlpha(75);p.setPen(QPen(grid,0.8,Qt::DotLine));
        const double half=1.55*c.span;const double rawStep=std::max(m_gridM,c.span/44.0);const double step=std::max(m_gridM,std::ceil(rawStep/m_gridM)*m_gridM);
        const int n=std::min(56,std::max(3,static_cast<int>(std::ceil(half/step))));
        NumericalEM::Vec3 planeOrigin{},planeU{},planeV{},planeNormal{};constructionPlaneFrame(planeOrigin,planeU,planeV,planeNormal);
        const NumericalEM::Vec3 cameraCenter{c.cx,c.cy,c.cz};const NumericalEM::Vec3 centerDelta=cameraCenter-planeOrigin;
        const NumericalEM::Vec3 gridCenter=planeOrigin+planeU*NumericalEM::dot(centerDelta,planeU)+planeV*NumericalEM::dot(centerDelta,planeV);
        auto planePoint=[&](double u,double v){return gridCenter+planeU*u+planeV*v;};
        for(int i=-n;i<=n;++i){const double q=i*step;p.drawLine(project(planePoint(-half,q),c).q,project(planePoint(half,q),c).q);p.drawLine(project(planePoint(q,-half),c).q,project(planePoint(q,half),c).q);}

        const auto axisColor=[&](int axis,int alpha){
            QColor color;
            if(axis==0)color=QColor(196,72,72);       // muted red — X
            else if(axis==1)color=QColor(78,164,92); // muted green — Y
            else color=QColor(76,120,202);           // muted blue — Z
            if(pal.color(QPalette::Base).lightness()>165)color=color.darker(112);
            color.setAlpha(alpha);return color;
        };
        const double axisLen=0.28*c.span/std::max(0.25,m_zoom);
        auto drawAxis=[&](int axis,double dx,double dy,double dz,const QString&label){
            const auto a=project({c.cx,c.cy,c.cz},c),b=project({c.cx+dx,c.cy+dy,c.cz+dz},c);
            p.setPen(QPen(axisColor(axis,125),1.45));p.drawLine(a.q,b.q);
            p.setPen(axisColor(axis,165));p.drawText(QRectF(b.q.x()-18,b.q.y()-12,36,24),Qt::AlignCenter,label);
        };
        drawAxis(0,axisLen,0,0,QStringLiteral("+X"));drawAxis(1,0,axisLen,0,QStringLiteral("+Y"));drawAxis(2,0,0,axisLen,QStringLiteral("+Z"));

        for(int di=0;di<static_cast<int>(m_dielectrics.size());++di)
        {
            const auto&d=m_dielectrics[static_cast<std::size_t>(di)];const bool selected=m_selectedDielectrics.count(di)>0;const bool problem=m_constraintProblemObjectIds.count(d.id)>0;
            AntennaSurface::SurfaceSpec spec;spec.center={d.centerM.x(),d.centerM.y(),d.zM};spec.baseOrientation=d.orientation;spec.yawDeg=d.yawDeg;spec.pitchDeg=d.pitchDeg;spec.rollDeg=d.rollDeg;
            std::array<NumericalEM::Vec3,8> q{};int qi=0;for(double nn:{-0.5*d.thicknessM,0.5*d.thicknessM})for(double v:{-0.5*d.heightM,0.5*d.heightM})for(double u:{-0.5*d.widthM,0.5*d.widthM}){const auto qq=AntennaSurface::pointFromLocal(spec,u,v,nn);q[qi++]=previewPoint(qq,selected);}
            const std::array<std::array<int,4>,6> faces{{{{0,1,3,2}},{{4,6,7,5}},{{0,4,5,1}},{{2,3,7,6}},{{0,2,6,4}},{{1,5,7,3}}}};QColor fill=pal.color(QPalette::Highlight);fill.setAlpha(selected?52:24);QColor edge=problem?QColor(210,65,65):(selected?pal.color(QPalette::Text):pal.color(QPalette::Highlight));edge.setAlpha(problem?245:(selected?230:80));p.setPen(QPen(edge,selected?2.0:0.8));p.setBrush(fill);for(const auto&face:faces){QPolygonF poly;for(int idx:face)poly<<project(q[static_cast<std::size_t>(idx)],c).q;p.drawPolygon(poly);}p.setBrush(Qt::NoBrush);p.setPen(edge);p.drawText(project(previewPoint({d.centerM.x(),d.centerM.y(),d.zM},selected),c).q+QPointF(5,14),QStringLiteral("%1 [εr=%2]").arg(d.name).arg(d.relativePermittivity,0,'g',4));
        }

        struct SurfaceDraw{QPolygonF poly;double depth;int planeIndex=-1;};std::vector<SurfaceDraw>surfaces;std::vector<std::tuple<QPointF,QString,int>>labels;
        for(int pi=0;pi<static_cast<int>(m_planes.size());++pi){const auto&pl=m_planes[static_cast<std::size_t>(pi)];const bool selected=m_selectedPlanes.count(pi)>0;const auto mesh=AntennaSurface::triangulate(pl.toSurfaceSpec(),2500);for(const auto&t:mesh){SurfaceDraw sd;double dep=0;for(const auto&q0:{t.a,t.b,t.c}){const auto q=previewPoint(q0,selected);const auto pr=project(q,c);sd.poly<<pr.q;dep+=pr.depth;}sd.depth=dep/3.0;sd.planeIndex=pi;surfaces.push_back(std::move(sd));}const auto q=previewPoint(AntennaSurface::pointFromLocal(pl.toSurfaceSpec(),0,0,0),selected);labels.push_back({project(q,c).q,pl.name+QStringLiteral(" [")+surfaceKindText(pl.surfaceType)+QStringLiteral("]"),pi});}
        std::sort(surfaces.begin(),surfaces.end(),[](const auto&a,const auto&b){return a.depth<b.depth;});QColor sf=pal.color(QPalette::Midlight);QColor se=pal.color(QPalette::PlaceholderText);for(const auto&srf:surfaces){const bool selected=m_selectedPlanes.count(srf.planeIndex)>0;const bool problem=srf.planeIndex>=0&&srf.planeIndex<static_cast<int>(m_planes.size())&&m_constraintProblemObjectIds.count(m_planes[static_cast<std::size_t>(srf.planeIndex)].id)>0;QColor fill=problem?QColor(210,65,65):sf;fill.setAlpha(problem?80:(selected?82:48));QColor edge=problem?QColor(210,65,65):(selected?pal.color(QPalette::Text):se);edge.setAlpha(problem?245:(selected?230:105));p.setPen(QPen(edge,selected?2.0:0.8));p.setBrush(fill);p.drawPolygon(srf.poly);}p.setBrush(Qt::NoBrush);for(const auto&l:labels){const bool selected=m_selectedPlanes.count(std::get<2>(l))>0;p.setPen(selected?pal.color(QPalette::Text):se);p.drawText(std::get<0>(l)+QPointF(5,-5),std::get<1>(l));}

        if(!m_surfaceCurrentPoints.empty()&&m_surfaceCurrentPoints.size()==m_surfaceCurrentMagnitude.size()){double peak=0;for(double q:m_surfaceCurrentMagnitude)peak=std::max(peak,q);if(peak>0){QColor jc=pal.color(QPalette::Highlight);p.setPen(Qt::NoPen);const std::size_t count=std::min<std::size_t>(m_surfaceCurrentPoints.size(),2500);for(std::size_t i=0;i<count;++i){const double u=std::clamp(m_surfaceCurrentMagnitude[i]/peak,0.0,1.0);QColor q=jc;q.setAlpha(55+static_cast<int>(180*u));p.setBrush(q);const auto qp=project(m_surfaceCurrentPoints[i],c).q;const double rad=1.5+4.0*std::sqrt(u);p.drawEllipse(qp,rad,rad);}}}

        struct Seg{QPointF a,b;double depth;QString name;double radiusM=0.0;int wireIndex=-1;};std::vector<Seg>segs;for(int wi=0;wi<static_cast<int>(m_wires.size());++wi){const auto&w=m_wires[static_cast<std::size_t>(wi)];const bool selected=m_selectedWires.count(wi)>0;const auto a=project(previewPoint({w.aM.x(),w.aM.y(),w.azM},selected),c),b=project(previewPoint({w.bM.x(),w.bM.y(),w.bzM},selected),c);segs.push_back({a.q,b.q,0.5*(a.depth+b.depth),w.name,w.radiusM,wi});}std::sort(segs.begin(),segs.end(),[](const auto&a,const auto&b){return a.depth<b.depth;});QColor wc=pal.color(QPalette::Highlight);for(const auto&s:segs){const bool sel=m_selectedWires.count(s.wireIndex)>0;const bool problem=s.wireIndex>=0&&s.wireIndex<static_cast<int>(m_wires.size())&&m_constraintProblemObjectIds.count(m_wires[static_cast<std::size_t>(s.wireIndex)].id)>0;const QColor center=problem?QColor(210,65,65):(sel?pal.color(QPalette::Text):wc);if(m_showPhysicalWireRadius){QColor body=center;body.setAlpha(sel?185:135);const double diameterPx=std::clamp(2.0*std::max(0.0,s.radiusM)*c.scale,1.0,260.0);QPen bodyPen(body,diameterPx,Qt::SolidLine,Qt::RoundCap);p.setPen(bodyPen);p.drawLine(s.a,s.b);p.setPen(QPen(center,problem?2.8:(sel?2.4:1.2),Qt::SolidLine,Qt::RoundCap));p.drawLine(s.a,s.b);}else{p.setPen(QPen(center,problem?5.5:(sel?5.0:3.0),Qt::SolidLine,Qt::RoundCap));p.drawLine(s.a,s.b);}p.setPen(pal.color(QPalette::Text));p.drawText(0.5*(s.a+s.b)+QPointF(5,-5),s.name);}
        QColor fc(255,191,0);if(pal.color(QPalette::Base).lightness()>160)fc=QColor(190,110,0);for(int i=0;i<static_cast<int>(m_feeds.size());++i){const auto&f=m_feeds[static_cast<std::size_t>(i)];const bool selected=m_selectedFeeds.count(i)>0;const bool problem=m_constraintProblemObjectIds.count(f.id)>0;const bool follows=feedFollowsSelectedWire(i);const auto q=project(previewPoint({f.positionM.x(),f.positionM.y(),f.zM},selected||follows),c).q;p.setPen(QPen(problem?QColor(210,65,65):fc,problem?4.5:(selected?4.0:2.5)));p.setBrush(pal.color(QPalette::Base));p.drawEllipse(q,6,6);p.drawLine(q+QPointF(-9,0),q+QPointF(9,0));p.drawLine(q+QPointF(0,-9),q+QPointF(0,9));p.setPen(selected?pal.color(QPalette::Text):fc);p.drawText(q+QPointF(9,-8),f.name);}

        // Persistent geometric constraints are rendered as lightweight CAD annotations between
        // their reference points. Green-ish theme highlight means satisfied; dashed placeholder
        // text means the current geometry still has a residual after the iterative solve.
        for(const auto &overlay:m_constraintOverlays)
        {
            const auto a=project(overlay.a,c).q,b=project(overlay.b,c).q;const bool selected=overlay.index==m_selectedConstraint;
            QColor cc=overlay.ok?pal.color(QPalette::Highlight):pal.color(QPalette::PlaceholderText);
            if(selected)cc=pal.color(QPalette::Text);cc.setAlpha(selected?255:(overlay.ok?210:190));
            p.setPen(QPen(cc,selected?3.0:1.5,selected?Qt::SolidLine:Qt::DashLine));p.drawLine(a,b);
            const QPointF mid=0.5*(a+b);QColor bg=pal.color(QPalette::Base);bg.setAlpha(selected?245:210);
            const QString label=overlay.label+(selected?QStringLiteral("  [selected]"):QString());const QRectF tr(mid.x()+5,mid.y()-19,226,22);p.fillRect(tr,bg);p.setPen(cc);p.drawText(tr,Qt::AlignLeft|Qt::AlignVCenter,label);
        }

        // Topological nodes are explicit handles in 3D, not only in the orthographic view.
        for(const auto&q:uniqueNodes()){const QPointF sp=project(q,c).q;const bool sel=m_selectionKind==1&&NumericalEM::norm(q-m_selectedPoint)<1e-8;QColor nc=pal.color(QPalette::Highlight);nc.setAlpha(sel?255:150);p.setPen(QPen(nc,sel?2.3:1.2));p.setBrush(pal.color(QPalette::Base));p.drawEllipse(sp,sel?5.5:3.8,sel?5.5:3.8);}

        // Keep a custom pivot visible independently of the active gizmo.
        if(m_pivotMode==3)
        {
            const QPointF qp=project(m_customPivot,c).q;QColor pc(214,146,62);pc.setAlpha(215);
            p.setPen(QPen(pc,1.8));p.setBrush(Qt::NoBrush);
            QPolygonF diamond;diamond<<qp+QPointF(0,-5)<<qp+QPointF(5,0)<<qp+QPointF(0,5)<<qp+QPointF(-5,0);
            p.drawPolygon(diamond);p.drawLine(qp+QPointF(-8,0),qp+QPointF(8,0));p.drawLine(qp+QPointF(0,-8),qp+QPointF(0,8));
            p.setPen(pc);p.drawText(qp+QPointF(9,-8),QStringLiteral("Pivot"));
        }

        if(m_drawingWire){p.setPen(QPen(pal.color(QPalette::Highlight),2.0,Qt::DashLine));p.drawLine(project(m_wireStart,c).q,project(m_wirePreview,c).q);}
        if(m_drawingRect)
        {
            const auto corners=planeRectangleCorners(m_rectStart,m_rectPreview);QPolygonF poly;for(const auto&q:corners)poly<<project(q,c).q;
            QColor rc=pal.color(QPalette::Highlight),rf=rc;rf.setAlpha(m_tool==Tool::AddDielectric?28:18);
            p.setPen(QPen(rc,2.0,Qt::DashLine));p.setBrush(rf);p.drawPolygon(poly);p.setBrush(Qt::NoBrush);
            const auto a=m_rectStart,b=m_rectPreview;double w=0.0,h=0.0;if(m_editPlane==0){w=std::abs(b.x-a.x);h=std::abs(b.y-a.y);}else if(m_editPlane==1){w=std::abs(b.x-a.x);h=std::abs(b.z-a.z);}else{w=std::abs(b.y-a.y);h=std::abs(b.z-a.z);}
            p.setPen(pal.color(QPalette::Text));p.drawText(poly.boundingRect().center()+QPointF(8,-8),QStringLiteral("%1 × %2 mm").arg(w*MmPerM,0,'g',6).arg(h*MmPerM,0,'g',6));
        }
        if(m_measureHasA)
        {
            const NumericalEM::Vec3 mb=(m_measureComplete||m_measureHover)?m_measureB:m_measureA;const QPointF ma=project(m_measureA,c).q,mq=project(mb,c).q;
            QColor mc=pal.color(QPalette::Highlight);p.setPen(QPen(mc,2.0,Qt::DashLine));p.drawLine(ma,mq);p.setBrush(pal.color(QPalette::Base));p.drawEllipse(ma,4.5,4.5);p.drawEllipse(mq,4.5,4.5);p.setBrush(Qt::NoBrush);
            const QString mt=measurementText();if(!mt.isEmpty()){QRectF tr(12,height()-78,width()-24,46);QColor bg=pal.color(QPalette::Base);bg.setAlpha(220);p.fillRect(tr,bg);p.setPen(pal.color(QPalette::Text));p.drawText(tr,Qt::AlignCenter,mt);}
        }
        if(m_gizmoMode==0)
        {
            QColor hc=pal.color(QPalette::Highlight);
            int resizeKind=0,resizeObject=-1;std::array<NumericalEM::Vec3,4> handles{};NumericalEM::Vec3 rc{},ru{},rv{},rn{};
            if(resizeHandleGeometry(resizeKind,resizeObject,handles,&rc,&ru,&rv,&rn))
            {
                p.setPen(QPen(hc,1.5));p.setBrush(pal.color(QPalette::Base));
                for(const auto&q:handles){const QPointF hq=project(q,c).q;p.drawRect(QRectF(hq.x()-4.5,hq.y()-4.5,9,9));}
                p.setBrush(Qt::NoBrush);
                if(m_resizing)
                {
                    NumericalEM::Vec3 center{};double rw=0.0,rh=0.0;orientedRectangleMetrics(m_resizeOpposite,m_resizePreview,m_resizeU,m_resizeV,center,rw,rh);
                    const NumericalEM::Vec3 d=m_resizePreview-m_resizeOpposite;const double du=NumericalEM::dot(d,m_resizeU),dv=NumericalEM::dot(d,m_resizeV);
                    const std::array<NumericalEM::Vec3,4> previewCorners{{m_resizeOpposite,m_resizeOpposite+m_resizeU*du,m_resizePreview,m_resizeOpposite+m_resizeV*dv}};
                    QPolygonF preview;for(const auto&q:previewCorners)preview<<project(q,c).q;
                    p.setPen(QPen(hc,2.0,Qt::DashLine));p.drawPolygon(preview);
                    p.drawText(preview.boundingRect().center()+QPointF(8,-8),QStringLiteral("%1 × %2 mm").arg(rw*MmPerM,0,'g',6).arg(rh*MmPerM,0,'g',6));
                }
            }
            int thicknessObject=-1;std::array<NumericalEM::Vec3,2> th{};NumericalEM::Vec3 tn{};
            if(dielectricThicknessGeometry(thicknessObject,th,tn))
            {
                const NumericalEM::Vec3 a=m_thicknessResizing?m_thicknessOpposite:th[0];const NumericalEM::Vec3 b=m_thicknessResizing?m_thicknessPreview:th[1];
                p.setPen(QPen(hc,1.5,Qt::DashLine));p.drawLine(project(a,c).q,project(b,c).q);p.setBrush(pal.color(QPalette::Base));
                for(const auto&q:th){const QPointF hq=project(q,c).q;p.drawEllipse(hq,4.5,4.5);}p.setBrush(Qt::NoBrush);
                const QPointF label=project(b,c).q;p.drawText(label+QPointF(7,-7),QStringLiteral("t=%1 mm").arg(NumericalEM::norm(b-a)*MmPerM,0,'g',6));
            }
            int radiusObject=-1;NumericalEM::Vec3 radiusCenter{},radiusU{},radiusV{},radiusNormal{},radiusHandle{};
            if(planeRadiusGeometry(radiusObject,radiusCenter,radiusU,radiusV,radiusNormal,radiusHandle))
            {
                const NumericalEM::Vec3 h=m_radiusResizing?m_radiusPreview:radiusHandle;p.setPen(QPen(hc,1.5,Qt::DashLine));
                p.drawLine(project(radiusCenter,c).q,project(h,c).q);p.setBrush(pal.color(QPalette::Base));p.drawEllipse(project(h,c).q,4.8,4.8);p.setBrush(Qt::NoBrush);
                p.drawText(project(h,c).q+QPointF(7,-7),QStringLiteral("R=%1 mm").arg(NumericalEM::norm(h-radiusCenter)*MmPerM,0,'g',6));
            }
        }
        if(m_boxSelecting){QRectF box(m_boxStart,m_boxCurrent);box=box.normalized();QColor bc=pal.color(QPalette::Highlight);QColor bf=bc;bf.setAlpha(32);p.setPen(QPen(bc,1.4,Qt::DashLine));p.setBrush(bf);p.drawRect(box);p.setBrush(Qt::NoBrush);}

        if(m_selectionKind!=0)
        {
            const std::array<QString,3>names{QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("Z")};
            if(m_gizmoMode==1 && m_selectionKind==6)
            {
                const NumericalEM::Vec3 pivot=transformPivot();const QPointF o=project(pivot,c).q;const double radius=0.19*c.span/std::max(0.25,m_zoom);
                for(int axis=0;axis<3;++axis){QPolygonF ring;constexpr int N=72;for(int i=0;i<=N;++i){const double a=2.0*NumericalEM::Pi*i/N;ring<<project(rotationRingPoint(axis,a,radius,pivot),c).q;}QColor gc=axisColor(axis,axis==m_dragRotationAxis?250:185);if(axis==m_dragRotationAxis)gc=gc.lighter(125);p.setPen(QPen(gc,axis==m_dragRotationAxis?3.2:2.0));p.drawPolyline(ring);const QPointF label=project(rotationRingPoint(axis,0.0,radius,pivot),c).q;p.setPen(axisColor(axis,220));p.drawText(label+QPointF(4,-4),QStringLiteral("R%1").arg(names[static_cast<std::size_t>(axis)]));}
                p.setPen(pal.color(QPalette::Text));p.drawEllipse(o,3.5,3.5);p.drawText(o+QPointF(8,18),QStringLiteral("Pivot (%1, %2, %3) m").arg(pivot.x,0,'g',5).arg(pivot.y,0,'g',5).arg(pivot.z,0,'g',5));
                if(m_dragRotationAxis>=0)p.drawText(o+QPointF(8,36),QStringLiteral("Rotate %1: %2 deg").arg(names[static_cast<std::size_t>(m_dragRotationAxis)]).arg(m_rotationPreviewDeg,0,'f',1));
            }
            else
            {
                const QPointF o=project(m_selectedPoint,c).q;const double gl=0.16*c.span/std::max(0.25,m_zoom);
                for(int axis=0;axis<3;++axis){NumericalEM::Vec3 e{};if(axis==0)e.x=gl;else if(axis==1)e.y=gl;else e.z=gl;const QPointF b=project(m_selectedPoint+e,c).q;QColor gc=axisColor(axis,axis==m_dragAxis?255:225);if(axis==m_dragAxis)gc=gc.lighter(125);p.setPen(QPen(gc,axis==m_dragAxis?3.8:3.2));p.drawLine(o,b);p.setPen(axisColor(axis,235));p.drawText(b+QPointF(4,-4),names[static_cast<std::size_t>(axis)]);}
            }
        }

        drawViewCube(p,pal);
    }

private:
    QFrame *m_viewAnglesPanel=nullptr,*m_constructionPlanePanel=nullptr,*m_selectionEditor=nullptr;
    QComboBox *m_planeCombo=nullptr;
    QDoubleSpinBox *m_planeCoordinateSpin=nullptr;
    QLabel *m_planeInfoLabel=nullptr;
    QPushButton *m_pickPivotButton=nullptr;
    QLabel *m_selectionEditorTitle=nullptr;
    std::array<QDoubleSpinBox*,3> m_viewAngleSpin{{nullptr,nullptr,nullptr}};
    std::array<QDoubleSpinBox*,3> m_planeAngleSpin{{nullptr,nullptr,nullptr}};
    std::array<QDoubleSpinBox*,3> m_selectionCoordSpin{{nullptr,nullptr,nullptr}};
    std::array<QLabel*,3> m_selectionDimLabel{{nullptr,nullptr,nullptr}};
    std::array<QDoubleSpinBox*,3> m_selectionDimSpin{{nullptr,nullptr,nullptr}};
    bool m_syncingOverlayEditors=false,m_syncingPlaneEditors=false;int m_selectionEditKind=0,m_selectionEditIndex=-1;
    std::vector<WireElement> m_wires;std::vector<FeedPoint> m_feeds;std::vector<PlaneElement> m_planes;std::vector<DielectricElement> m_dielectrics;
    std::vector<ConstraintOverlay> m_constraintOverlays;
    std::vector<GroupSelection> m_groupSelections;
    std::set<QString> m_constraintProblemObjectIds;
    std::vector<NumericalEM::Vec3> m_surfaceCurrentPoints;std::vector<double> m_surfaceCurrentMagnitude;
    Tool m_tool=Tool::Select;int m_editPlane=0;double m_planeCoordinateM=0.0,m_gridM=0.05;
    double m_planeRotXDeg=0.0,m_planeRotYDeg=0.0,m_planeRotZDeg=0.0;
    double m_viewRotXDeg=24.0,m_viewRotYDeg=0.0,m_viewRotZDeg=-35.0,m_zoom=1.0;QPointF m_pan,m_lastMouse;
    double m_cameraFrameCx=0.0,m_cameraFrameCy=0.0,m_cameraFrameCz=0.0,m_cameraFrameSpan=1.0;bool m_cameraFrameValid=false;
    bool m_draggingCamera=false,m_panning=false,m_drawingWire=false,m_drawingRect=false,m_draggingSelection=false,m_boxSelecting=false,m_objectSnapEnabled=false,m_resizing=false;
    bool m_thicknessResizing=false,m_radiusResizing=false;
    bool m_showPhysicalWireRadius=false;
    bool m_measureHasA=false,m_measureComplete=false,m_measureHover=false;
    NumericalEM::Vec3 m_wireStart{},m_wirePreview{},m_rectStart{},m_rectPreview{},m_measureA{},m_measureB{},m_resizeOpposite{},m_resizePreview{},m_resizePlanePoint{},m_resizeU{},m_resizeV{},m_resizeNormal{},m_selectedPoint{},m_dragStartWorld{},m_dragCursorStartWorld{},m_customPivot{};
    NumericalEM::Vec3 m_thicknessOpposite{},m_thicknessPreview{},m_thicknessAxis{},m_thicknessDragStartWorld{};
    NumericalEM::Vec3 m_radiusCenter{},m_radiusU{},m_radiusV{},m_radiusNormal{},m_radiusPreview{};
    QPointF m_dragStartScreen,m_boxStart,m_boxCurrent,m_thicknessDragStartScreen;
    int m_selectionKind=0,m_selectedFeed=-1,m_selectedWire=-1,m_selectedConstraint=-1,m_dragAxis=-1,m_gizmoMode=0,m_pivotMode=0,m_dragRotationAxis=-1,m_resizeKind=0,m_resizeObjectIndex=-1,m_resizeHandleIndex=-1;
    int m_thicknessObjectIndex=-1,m_thicknessHandleIndex=-1,m_radiusObjectIndex=-1;
    int m_hoverCubeFace=-1,m_hoverCubeControl=0,m_panStepIndex=0,m_panHoldDirection=-1;
    double m_rotationStartAngleRad=0.0,m_rotationPreviewDeg=0.0;
    QFrame *m_toolPanel=nullptr,*m_gridPanel=nullptr;QComboBox *m_toolCombo=nullptr;QDoubleSpinBox *m_gridSpin=nullptr;
    QTimer *m_panHoldDelay=nullptr,*m_panRepeatTimer=nullptr;bool m_syncingToolCombo=false,m_syncingGridEditor=false;
    std::set<int> m_selectedWires,m_selectedFeeds,m_selectedPlanes,m_selectedDielectrics;
};

class AntennaDesignerWidget::SmithChart final : public QWidget
{
public:
    explicit SmithChart(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(360, 260);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
    }

    void setSweep(const std::vector<double> &frequencyMHz,
                  const std::vector<std::complex<double>> &gamma,
                  int bestIndex, int resonanceIndex)
    {
        m_frequencyMHz = frequencyMHz;
        m_gamma = gamma;
        m_bestIndex = bestIndex;
        m_resonanceIndex = resonanceIndex;
        update();
    }

    void clearSweep()
    {
        m_frequencyMHz.clear();
        m_gamma.clear();
        m_bestIndex = -1;
        m_resonanceIndex = -1;
        update();
    }

protected:
    void mouseMoveEvent(QMouseEvent *event) override
    {
        const double side = std::max(80.0, std::min(width() - 42.0, height() - 58.0));
        const QPointF c(width() * 0.5, height() * 0.5 + 8.0);
        const double R = side * 0.5;
        auto mapGamma = [&](const std::complex<double> &g) {
            return QPointF(c.x() + R * g.real(), c.y() - R * g.imag());
        };
        int best = -1;
        double bestDist = 14.0;
        for (int i = 0; i < static_cast<int>(m_gamma.size()); ++i)
        {
            const auto &g = m_gamma[static_cast<std::size_t>(i)];
            if (!std::isfinite(g.real()) || !std::isfinite(g.imag())) continue;
            const double d = QLineF(event->position(), mapGamma(g)).length();
            if (d <= bestDist) { bestDist = d; best = i; }
        }
        if (best != m_hoverIndex)
        {
            m_hoverIndex = best;
            update();
        }
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        if (m_hoverIndex != -1)
        {
            m_hoverIndex = -1;
            update();
        }
        QWidget::leaveEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const auto pal = palette();
        p.fillRect(rect(), pal.color(QPalette::Base));
        const double side = std::max(80.0, std::min(width() - 42.0, height() - 58.0));
        const QPointF c(width() * 0.5, height() * 0.5 + 8.0);
        const double R = side * 0.5;
        auto mapGamma = [&](const std::complex<double> &g) {
            return QPointF(c.x() + R * g.real(), c.y() - R * g.imag());
        };
        const QRectF unit(c.x() - R, c.y() - R, 2 * R, 2 * R);
        p.setPen(QPen(pal.color(QPalette::Mid), 1.0));
        p.drawEllipse(unit);
        p.drawLine(mapGamma({-1.0, 0.0}), mapGamma({1.0, 0.0}));

        p.save();
        QPainterPath clip; clip.addEllipse(unit); p.setClipPath(clip);
        QColor grid = pal.color(QPalette::Mid); grid.setAlpha(150);
        p.setPen(QPen(grid, 1.0, Qt::DotLine));
        for (double r : {0.2, 0.5, 1.0, 2.0, 5.0})
        {
            const double cx = r / (1.0 + r), rr = 1.0 / (1.0 + r);
            p.drawEllipse(QRectF(c.x() + R * (cx - rr), c.y() - R * rr, 2 * R * rr, 2 * R * rr));
        }
        for (double x : {0.2, 0.5, 1.0, 2.0, 5.0})
            for (double sign : {-1.0, 1.0})
            {
                const double cy = sign / x, rr = 1.0 / std::abs(x);
                p.drawEllipse(QRectF(c.x() + R * (1.0 - rr), c.y() - R * (cy + rr), 2 * R * rr, 2 * R * rr));
            }
        p.restore();

        if (!m_gamma.empty())
        {
            QPainterPath trace; bool started = false;
            for (const auto &g : m_gamma)
            {
                if (!std::isfinite(g.real()) || !std::isfinite(g.imag())) continue;
                const QPointF q = mapGamma(g);
                if (!started) { trace.moveTo(q); started = true; } else trace.lineTo(q);
            }
            p.setPen(QPen(pal.color(QPalette::Highlight), 2.4));
            p.drawPath(trace);
            if (started)
            {
                p.setPen(Qt::NoPen); p.setBrush(pal.color(QPalette::Highlight));
                p.drawEllipse(mapGamma(m_gamma.front()), 4.5, 4.5);
                p.setBrush(pal.color(QPalette::Text));
                p.drawEllipse(mapGamma(m_gamma.back()), 3.5, 3.5);
            }
        }

        auto drawMarker = [&](int index, const QString &name, int hueShift) {
            if (index < 0 || index >= static_cast<int>(m_gamma.size())) return;
            const auto &g = m_gamma[static_cast<std::size_t>(index)];
            if (!std::isfinite(g.real()) || !std::isfinite(g.imag())) return;
            QColor mc = pal.color(QPalette::Highlight);
            int h = mc.hsvHue(); if (h < 0) h = 205;
            mc.setHsv((h + hueShift) % 360, 210, 235);
            const QPointF q = mapGamma(g);
            p.setPen(QPen(pal.color(QPalette::Base), 1.5)); p.setBrush(mc);
            p.drawEllipse(q, 6.0, 6.0);
            p.setPen(mc);
            p.drawText(QRectF(q.x() + 8, q.y() - 12, 150, 24), Qt::AlignLeft | Qt::AlignVCenter, name);
        };
        if (m_bestIndex >= 0 && m_bestIndex < static_cast<int>(m_frequencyMHz.size()))
            drawMarker(m_bestIndex, QStringLiteral("Best %1 MHz").arg(m_frequencyMHz[static_cast<std::size_t>(m_bestIndex)], 0, 'g', 6), 0);
        if (m_resonanceIndex >= 0 && m_resonanceIndex < static_cast<int>(m_frequencyMHz.size()) && m_resonanceIndex != m_bestIndex)
            drawMarker(m_resonanceIndex, QStringLiteral("X≈0 %1 MHz").arg(m_frequencyMHz[static_cast<std::size_t>(m_resonanceIndex)], 0, 'g', 6), 110);

        if (m_hoverIndex >= 0 && m_hoverIndex < static_cast<int>(m_gamma.size()) && m_hoverIndex < static_cast<int>(m_frequencyMHz.size()))
        {
            const auto &g = m_gamma[static_cast<std::size_t>(m_hoverIndex)];
            if (std::isfinite(g.real()) && std::isfinite(g.imag()))
            {
                const QPointF q = mapGamma(g);
                QColor hc = pal.color(QPalette::Highlight);
                p.setPen(QPen(hc, 1.8));
                p.setBrush(hc);
                p.drawEllipse(q, 5.5, 5.5);
                const double gm = std::abs(g);
                const std::complex<double> denom = std::complex<double>(1.0, 0.0) - g;
                const std::complex<double> z = std::abs(denom) > 1e-12 ? (std::complex<double>(1.0, 0.0) + g) / denom : std::complex<double>(1e12, 0.0);
                const double vswr = gm >= 0.999999 ? std::numeric_limits<double>::infinity() : (1.0 + gm) / std::max(1e-12, 1.0 - gm);
                const double rl = gm > 1e-12 ? -20.0 * std::log10(gm) : 200.0;
                QStringList lines;
                lines << QStringLiteral("f = %1 MHz").arg(m_frequencyMHz[static_cast<std::size_t>(m_hoverIndex)], 0, 'g', 7)
                      << QStringLiteral("Γ = %1 %2 j").arg(g.real(), 0, 'g', 5).arg(g.imag() >= 0.0 ? QStringLiteral("+") + QString::number(g.imag(), 'g', 5) : QString::number(g.imag(), 'g', 5))
                      << QStringLiteral("|Γ| = %1").arg(gm, 0, 'g', 5)
                      << QStringLiteral("z/Z0 = %1 %2 j").arg(z.real(), 0, 'g', 5).arg(z.imag() >= 0.0 ? QStringLiteral("+") + QString::number(z.imag(), 'g', 5) : QString::number(z.imag(), 'g', 5))
                      << QStringLiteral("Return loss = %1 dB").arg(rl, 0, 'g', 5)
                      << QStringLiteral("VSWR = %1").arg(std::isfinite(vswr) ? QString::number(vswr, 'g', 5) : QStringLiteral("∞"));
                const QFontMetrics fm(p.font());
                int boxW = 0;
                for (const QString &line : lines) boxW = std::max(boxW, fm.horizontalAdvance(line));
                const double lineH = fm.height() + 2.0;
                const double boxWidth = std::min(width() * 0.45, double(boxW) + 18.0);
                const double boxHeight = 10.0 + lineH * lines.size();
                double boxX = q.x() + 12.0;
                if (boxX + boxWidth > width() - 8.0) boxX = q.x() - boxWidth - 12.0;
                boxX = std::clamp(boxX, 8.0, std::max(8.0, width() - boxWidth - 8.0));
                double boxY = q.y() - boxHeight - 10.0;
                if (boxY < 30.0) boxY = q.y() + 10.0;
                boxY = std::clamp(boxY, 30.0, std::max(30.0, height() - boxHeight - 28.0));
                QRectF box(boxX, boxY, boxWidth, boxHeight);
                QColor fill = pal.color(QPalette::Base); fill.setAlpha(232);
                p.setBrush(fill); p.setPen(QPen(pal.color(QPalette::Mid), 1.0));
                p.drawRoundedRect(box, 6.0, 6.0);
                p.setPen(pal.color(QPalette::Text));
                for (int i = 0; i < lines.size(); ++i)
                    p.drawText(QRectF(box.left() + 8.0, box.top() + 6.0 + i * lineH, box.width() - 12.0, lineH),
                               Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
            }
        }

        p.setPen(pal.color(QPalette::Text));
        p.drawText(QRectF(8, 4, width() - 16, 24), Qt::AlignCenter, QStringLiteral("Antenna input Smith chart — Γ(f)"));
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(QRectF(8, height() - 23, width() - 16, 18), Qt::AlignCenter,
                   m_gamma.empty() ? QStringLiteral("Run an antenna frequency sweep to populate the chart.")
                                   : QStringLiteral("Hover any sweep point to inspect Γ, normalized impedance, return loss and VSWR. Start: highlighted dot | end: text-color dot | markers: best match and minimum |X|."));
    }

private:
    std::vector<double> m_frequencyMHz;
    std::vector<std::complex<double>> m_gamma;
    int m_bestIndex = -1;
    int m_resonanceIndex = -1;
    int m_hoverIndex = -1;
};

class AntennaDesignerWidget::PolarPatternPlot final : public QWidget
{
public:
    explicit PolarPatternPlot(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(360, 300);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
    }

    void setPattern(const std::vector<double> &angleDeg,
                    const std::vector<double> &normalizedAmplitude,
                    const QString &title,
                    bool elevationThetaConvention = false)
    {
        m_angleDeg = angleDeg;
        m_amplitude = normalizedAmplitude;
        m_title = title;
        m_elevationThetaConvention = elevationThetaConvention;
        update();
    }

    void clearPattern()
    {
        m_angleDeg.clear();
        m_amplitude.clear();
        update();
    }

protected:
    void mouseMoveEvent(QMouseEvent *event) override
    {
        const double topMargin = 34.0;
        const double bottomMargin = 30.0;
        const double side = std::max(100.0, std::min(width() - 56.0, height() - topMargin - bottomMargin));
        const double radius = 0.5 * side;
        const QPointF center(width() * 0.5, topMargin + radius);
        int best = -1;
        double bestDist = 16.0;
        auto unitPoint = [&](double displayDeg, double r) {
            const double a = displayDeg * NumericalEM::Pi / 180.0;
            return QPointF(center.x() + r * std::cos(a), center.y() - r * std::sin(a));
        };
        constexpr double floorDb = -40.0;
        for (std::size_t i = 0; i < m_angleDeg.size() && i < m_amplitude.size(); ++i)
        {
            const double amp = std::clamp(m_amplitude[i], 0.0, 1.0);
            const double db = amp > 1e-12 ? 20.0 * std::log10(amp) : floorDb;
            const double rr = radius * std::clamp((std::max(db, floorDb) - floorDb) / (-floorDb), 0.0, 1.0);
            const double displayDeg = m_elevationThetaConvention ? 90.0 - m_angleDeg[i] : m_angleDeg[i];
            const double d = QLineF(event->position(), unitPoint(displayDeg, rr)).length();
            if (d <= bestDist) { bestDist = d; best = static_cast<int>(i); }
        }
        if (best != m_hoverIndex) { m_hoverIndex = best; update(); }
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        if (m_hoverIndex != -1) { m_hoverIndex = -1; update(); }
        QWidget::leaveEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QPalette pal = palette();
        p.fillRect(rect(), pal.color(QPalette::Base));

        const double topMargin = 34.0;
        const double bottomMargin = 30.0;
        const double side = std::max(100.0, std::min(width() - 56.0, height() - topMargin - bottomMargin));
        const double radius = 0.5 * side;
        const QPointF center(width() * 0.5, topMargin + radius);
        constexpr double floorDb = -40.0;

        p.setPen(pal.color(QPalette::Text));
        p.drawText(QRectF(8, 4, width() - 16, 24), Qt::AlignCenter,
                   m_title.isEmpty() ? QStringLiteral("Normalized radiation pattern") : m_title);

        QColor grid = pal.color(QPalette::Mid);
        grid.setAlpha(150);
        p.setPen(QPen(grid, 1.0, Qt::DotLine));
        for (double db : {0.0, -10.0, -20.0, -30.0, -40.0})
        {
            const double rr = radius * std::clamp((db - floorDb) / (-floorDb), 0.0, 1.0);
            p.drawEllipse(center, rr, rr);
            p.setPen(pal.color(QPalette::PlaceholderText));
            p.drawText(QPointF(center.x() + 4.0, center.y() - rr - 3.0), QStringLiteral("%1 dB").arg(db, 0, 'f', 0));
            p.setPen(QPen(grid, 1.0, Qt::DotLine));
        }

        auto unitPoint = [&](double displayDeg, double r) {
            const double a = displayDeg * NumericalEM::Pi / 180.0;
            return QPointF(center.x() + r * std::cos(a), center.y() - r * std::sin(a));
        };

        for (int deg = 0; deg < 360; deg += 30)
        {
            p.setPen(QPen(grid, 1.0, Qt::DotLine));
            p.drawLine(center, unitPoint(static_cast<double>(deg), radius));
            p.setPen(pal.color(QPalette::PlaceholderText));
            const QPointF q = unitPoint(static_cast<double>(deg), radius + 14.0);
            p.drawText(QRectF(q.x() - 20.0, q.y() - 9.0, 40.0, 18.0), Qt::AlignCenter,
                       QStringLiteral("%1°").arg(deg));
        }

        if (!m_angleDeg.empty() && m_angleDeg.size() == m_amplitude.size())
        {
            QPainterPath path;
            bool started = false;
            double maxAmp = -1.0;
            QPointF maxPoint;
            double maxAngle = 0.0;
            for (std::size_t i = 0; i < m_angleDeg.size(); ++i)
            {
                const double amp = std::clamp(m_amplitude[i], 0.0, 1.0);
                const double db = amp > 1e-12 ? 20.0 * std::log10(amp) : floorDb;
                const double rr = radius * std::clamp((std::max(db, floorDb) - floorDb) / (-floorDb), 0.0, 1.0);
                const double displayDeg = m_elevationThetaConvention ? 90.0 - m_angleDeg[i] : m_angleDeg[i];
                const QPointF q = unitPoint(displayDeg, rr);
                if (!started) { path.moveTo(q); started = true; }
                else path.lineTo(q);
                if (amp > maxAmp) { maxAmp = amp; maxPoint = q; maxAngle = m_angleDeg[i]; }
            }
            const bool fullCircle = m_angleDeg.size() > 2 && std::abs(m_angleDeg.back() - m_angleDeg.front()) >= 350.0;
            if (fullCircle) path.closeSubpath();
            p.setPen(QPen(pal.color(QPalette::Highlight), 2.5));
            p.drawPath(path);
            if (m_hoverIndex >= 0 && m_hoverIndex < static_cast<int>(m_angleDeg.size()) && m_hoverIndex < static_cast<int>(m_amplitude.size()))
            {
                const double amp = std::clamp(m_amplitude[static_cast<std::size_t>(m_hoverIndex)], 0.0, 1.0);
                const double db = amp > 1e-12 ? 20.0 * std::log10(amp) : floorDb;
                const double rr = radius * std::clamp((std::max(db, floorDb) - floorDb) / (-floorDb), 0.0, 1.0);
                const double displayDeg = m_elevationThetaConvention ? 90.0 - m_angleDeg[static_cast<std::size_t>(m_hoverIndex)] : m_angleDeg[static_cast<std::size_t>(m_hoverIndex)];
                const QPointF hq = unitPoint(displayDeg, rr);
                p.setPen(QPen(pal.color(QPalette::Base), 1.2));
                p.setBrush(pal.color(QPalette::Highlight));
                p.drawEllipse(hq, 5.0, 5.0);
                QStringList lines;
                lines << QStringLiteral("%1 = %2°").arg(m_elevationThetaConvention ? QStringLiteral("θ") : QStringLiteral("φ")).arg(m_angleDeg[static_cast<std::size_t>(m_hoverIndex)], 0, 'g', 6)
                      << QStringLiteral("Normalized field = %1").arg(amp, 0, 'g', 5)
                      << QStringLiteral("Relative level = %1 dB").arg(db, 0, 'g', 5);
                const QFontMetrics fm(p.font());
                int boxW = 0; for (const QString &line : lines) boxW = std::max(boxW, fm.horizontalAdvance(line));
                const double lineH = fm.height() + 2.0;
                const double boxWidth = std::min(width() * 0.42, double(boxW) + 18.0);
                const double boxHeight = 10.0 + lineH * lines.size();
                double boxX = hq.x() + 12.0;
                if (boxX + boxWidth > width() - 8.0) boxX = hq.x() - boxWidth - 12.0;
                boxX = std::clamp(boxX, 8.0, std::max(8.0, width() - boxWidth - 8.0));
                double boxY = hq.y() - boxHeight - 8.0;
                if (boxY < topMargin + 2.0) boxY = hq.y() + 10.0;
                boxY = std::clamp(boxY, topMargin + 2.0, std::max(topMargin + 2.0, height() - boxHeight - bottomMargin - 4.0));
                QRectF box(boxX, boxY, boxWidth, boxHeight);
                QColor fill = pal.color(QPalette::Base); fill.setAlpha(232);
                p.setBrush(fill); p.setPen(QPen(pal.color(QPalette::Mid), 1.0)); p.drawRoundedRect(box, 6.0, 6.0);
                p.setPen(pal.color(QPalette::Text));
                for (int i = 0; i < lines.size(); ++i)
                    p.drawText(QRectF(box.left() + 8.0, box.top() + 6.0 + i * lineH, box.width() - 12.0, lineH),
                               Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
            }
            if (maxAmp >= 0.0)
            {
                p.setBrush(pal.color(QPalette::Highlight));
                p.setPen(QPen(pal.color(QPalette::Base), 1.2));
                p.drawEllipse(maxPoint, 5.0, 5.0);
                p.setPen(pal.color(QPalette::Highlight));
                p.drawText(QRectF(maxPoint.x() + 7.0, maxPoint.y() - 12.0, 150.0, 24.0),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           QStringLiteral("max @ %1°").arg(maxAngle, 0, 'g', 5));
            }
        }

        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(QRectF(8, height() - 24, width() - 16, 18), Qt::AlignCenter,
                   m_elevationThetaConvention
                       ? QStringLiteral("θ = 0° points toward +Z; θ = 90° toward +X. Hover the pattern to inspect angle and level. Display floor: −40 dB.")
                       : QStringLiteral("φ = 0° points toward +X; positive angles rotate toward +Y. Hover the pattern to inspect angle and level. Display floor: −40 dB."));
    }

private:
    std::vector<double> m_angleDeg;
    std::vector<double> m_amplitude;
    QString m_title;
    bool m_elevationThetaConvention = false;
    int m_hoverIndex = -1;
};

class AntennaDesignerWidget::RadiationPattern3D final : public QWidget
{
public:
    explicit RadiationPattern3D(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(420, 320);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
    }

    void setColorMapEnabled(bool enabled)
    {
        if (m_colorMapEnabled == enabled) return;
        m_colorMapEnabled = enabled;
        update();
    }

    void setLegendVisible(bool enabled)
    {
        if (m_showLegend == enabled) return;
        m_showLegend = enabled;
        update();
    }

    void setPattern(const std::vector<double> &thetaDeg,
                    const std::vector<double> &phiDeg,
                    const std::vector<double> &normalizedAmplitude,
                    double directivityDbi,
                    double peakGainDbi,
                    double peakRealizedGainDbi,
                    double radiationEfficiency,
                    double maxThetaDeg,
                    double maxPhiDeg)
    {
        m_thetaDeg = thetaDeg;
        m_phiDeg = phiDeg;
        m_amplitude = normalizedAmplitude;
        m_directivityDbi = directivityDbi;
        m_peakGainDbi = peakGainDbi;
        m_peakRealizedGainDbi = peakRealizedGainDbi;
        m_radiationEfficiency = radiationEfficiency;
        m_maxThetaDeg = maxThetaDeg;
        m_maxPhiDeg = maxPhiDeg;
        update();
    }

    void clearPattern()
    {
        m_thetaDeg.clear();
        m_phiDeg.clear();
        m_amplitude.clear();
        update();
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            m_dragging = true;
            m_lastMouse = event->position();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging && (event->buttons() & Qt::LeftButton))
        {
            const QPointF delta = event->position() - m_lastMouse;
            m_lastMouse = event->position();
            m_yawDeg += 0.65 * delta.x();
            m_pitchDeg = std::clamp(m_pitchDeg + 0.65 * delta.y(), -89.0, 89.0);
            m_hoverActive = false;
            update();
            event->accept();
            return;
        }
        m_hoverActive = true;
        m_hoverPosition = event->position();
        update();
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_dragging)
        {
            m_dragging = false;
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        if (m_hoverActive)
        {
            m_hoverActive = false;
            update();
        }
        QWidget::leaveEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            m_yawDeg = -35.0;
            m_pitchDeg = 24.0;
            m_zoom = 1.0;
            update();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        m_zoom = std::clamp(m_zoom * (event->angleDelta().y() > 0 ? 1.10 : 1.0 / 1.10), 0.45, 2.8);
        update();
        event->accept();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QPalette pal = palette();
        p.fillRect(rect(), pal.color(QPalette::Base));
        p.setPen(pal.color(QPalette::Text));
        QString radiationTitle = QStringLiteral("3D radiation pattern — Dmax ≈ %1 dBi | Gmax ≈ %2 dBi")
                                     .arg(m_directivityDbi, 0, 'g', 5).arg(m_peakGainDbi, 0, 'g', 5);
        if (std::isfinite(m_peakRealizedGainDbi))
            radiationTitle += QStringLiteral(" | Greal,max ≈ %1 dBi").arg(m_peakRealizedGainDbi, 0, 'g', 5);
        p.drawText(QRectF(8, 4, width() - 16, 24), Qt::AlignCenter, radiationTitle);

        if (m_thetaDeg.empty() || m_phiDeg.empty() ||
            m_amplitude.size() != m_thetaDeg.size() * m_phiDeg.size())
        {
            p.setPen(pal.color(QPalette::PlaceholderText));
            p.drawText(rect().adjusted(20, 40, -20, -30), Qt::AlignCenter,
                       QStringLiteral("Run a full MoM antenna solve to populate the 3D radiation pattern."));
            return;
        }

        struct Projected { QPointF q; double depth = 0.0; };
        const QPointF center(width() * 0.5, height() * 0.53);
        const double sceneScale = std::max(80.0, std::min(width(), height()) * 0.37) * m_zoom;
        const double yaw = m_yawDeg * NumericalEM::Pi / 180.0;
        const double pitch = m_pitchDeg * NumericalEM::Pi / 180.0;
        const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
        auto project = [&](double x, double y, double z) -> Projected {
            const double x1 = cy*x - sy*y;
            const double y1 = sy*x + cy*y;
            const double z1 = z;
            const double y2 = cp*y1 - sp*z1;
            const double z2 = sp*y1 + cp*z1;
            return {QPointF(center.x() + sceneScale*x1, center.y() - sceneScale*z2), y2};
        };
        auto dirFor = [&](double thetaDeg, double phiDeg, double radius) -> Projected {
            const double th = thetaDeg * NumericalEM::Pi / 180.0;
            const double ph = phiDeg * NumericalEM::Pi / 180.0;
            return project(radius*std::sin(th)*std::cos(ph),
                           radius*std::sin(th)*std::sin(ph),
                           radius*std::cos(th));
        };
        constexpr double floorDb = -40.0;
        auto radial = [&](double amp) {
            const double a = std::clamp(amp, 0.0, 1.0);
            const double db = a > 1e-12 ? 20.0*std::log10(a) : floorDb;
            const double t = std::clamp((std::max(db, floorDb)-floorDb)/(-floorDb), 0.0, 1.0);
            return 0.10 + 0.90*t;
        };
        auto colorForDb = [&](double db) {
            const double u = std::clamp((db - floorDb) / (0.0 - floorDb), 0.0, 1.0);
            QColor c;
            c.setHsv(static_cast<int>((1.0 - u) * 225.0), 215, 235, 170);
            return c;
        };

        // Reference sphere and Cartesian axes make the orientation explicit.
        QColor ref = pal.color(QPalette::Mid); ref.setAlpha(95);
        p.setPen(QPen(ref, 1.0, Qt::DotLine));
        for (int lat : {-60, -30, 0, 30, 60})
        {
            QPainterPath path;
            bool first = true;
            const double th = 90.0 - lat;
            for (int ph = 0; ph <= 360; ph += 10)
            {
                const auto q = dirFor(th, static_cast<double>(ph), 1.0).q;
                if (first) { path.moveTo(q); first=false; } else path.lineTo(q);
            }
            p.drawPath(path);
        }
        for (int ph : {0, 45, 90, 135})
        {
            QPainterPath path;
            bool first = true;
            for (int th = 0; th <= 180; th += 5)
            {
                const auto q = dirFor(static_cast<double>(th), static_cast<double>(ph), 1.0).q;
                if (first) { path.moveTo(q); first=false; } else path.lineTo(q);
            }
            p.drawPath(path);
        }

        auto drawAxis = [&](double x, double y, double z, const QString &label) {
            const QPointF a = project(0,0,0).q;
            const QPointF b = project(x,y,z).q;
            p.setPen(QPen(pal.color(QPalette::PlaceholderText), 1.5));
            p.drawLine(a,b);
            p.drawText(QRectF(b.x()-14,b.y()-11,28,22),Qt::AlignCenter,label);
        };
        drawAxis(1.18,0,0,QStringLiteral("+X"));
        drawAxis(0,1.18,0,QStringLiteral("+Y"));
        drawAxis(0,0,1.18,QStringLiteral("+Z"));

        struct MeshVertex { QPointF q; double depth=0.0; double amp=0.0; double thetaDeg=0.0; double phiDeg=0.0; };
        struct MeshLine { QPointF a,b; double depth=0.0; double amp=0.0; };
        struct MeshFace { QPolygonF poly; double depth=0.0; double db=0.0; };
        std::vector<MeshLine> lines;
        std::vector<MeshFace> faces;
        std::vector<MeshVertex> vertices;
        const std::size_t nt = m_thetaDeg.size(), np = m_phiDeg.size();
        auto sample = [&](std::size_t ti, std::size_t pi) { return m_amplitude[ti*np + pi]; };
        auto pointAt = [&](std::size_t ti, std::size_t pi) {
            const auto projected = dirFor(m_thetaDeg[ti], m_phiDeg[pi], radial(sample(ti,pi)));
            return MeshVertex{projected.q, projected.depth, sample(ti,pi), m_thetaDeg[ti], m_phiDeg[pi]};
        };
        lines.reserve(nt*np*2);
        vertices.reserve(nt*np);
        for (std::size_t ti=0; ti<nt; ++ti)
            for (std::size_t pi=0; pi<np; ++pi)
                vertices.push_back(pointAt(ti,pi));
        auto vertexAt = [&](std::size_t ti, std::size_t pi) -> const MeshVertex& { return vertices[ti*np + pi]; };
        if (m_colorMapEnabled)
        {
            faces.reserve(std::max<std::size_t>(1, (nt>1?nt-1:0)*np));
            for (std::size_t ti=0; ti+1<nt; ++ti)
            {
                for (std::size_t pi=0; pi<np; ++pi)
                {
                    const std::size_t pj=(pi+1)%np;
                    const auto &a=vertexAt(ti,pi), &b=vertexAt(ti,pj), &cV=vertexAt(ti+1,pj), &d=vertexAt(ti+1,pi);
                    const double avgAmp = 0.25*(a.amp+b.amp+cV.amp+d.amp);
                    const double avgDb = avgAmp > 1e-12 ? 20.0 * std::log10(std::clamp(avgAmp, 0.0, 1.0)) : floorDb;
                    MeshFace face; face.depth = 0.25*(a.depth+b.depth+cV.depth+d.depth); face.db = std::max(avgDb, floorDb);
                    face.poly << a.q << b.q << cV.q << d.q;
                    faces.push_back(face);
                }
            }
            std::sort(faces.begin(), faces.end(), [](const MeshFace &a, const MeshFace &b){ return a.depth < b.depth; });
            p.setPen(Qt::NoPen);
            for (const auto &face : faces)
            {
                QColor fc = colorForDb(face.db);
                p.setBrush(fc);
                p.drawPolygon(face.poly);
            }
        }
        for (std::size_t ti=0; ti<nt; ++ti)
        {
            for (std::size_t pi=0; pi<np; ++pi)
            {
                const std::size_t pj=(pi+1)%np;
                const auto &a=vertexAt(ti,pi), &b=vertexAt(ti,pj);
                lines.push_back({a.q,b.q,0.5*(a.depth+b.depth),0.5*(a.amp+b.amp)});
            }
        }
        const std::size_t phiStride = std::max<std::size_t>(1, np/36);
        for (std::size_t pi=0; pi<np; pi+=phiStride)
        {
            for (std::size_t ti=0; ti+1<nt; ++ti)
            {
                const auto &a=vertexAt(ti,pi), &b=vertexAt(ti+1,pi);
                lines.push_back({a.q,b.q,0.5*(a.depth+b.depth),0.5*(a.amp+b.amp)});
            }
        }
        std::sort(lines.begin(), lines.end(), [](const MeshLine &a, const MeshLine &b){ return a.depth < b.depth; });
        QColor accent = pal.color(QPalette::Highlight);
        for (const auto &line : lines)
        {
            QColor c = m_colorMapEnabled ? colorForDb(line.amp > 1e-12 ? 20.0*std::log10(std::clamp(line.amp,0.0,1.0)) : floorDb) : accent;
            c.setAlpha(m_colorMapEnabled ? 200 : 55 + static_cast<int>(180.0*std::clamp(line.amp,0.0,1.0)));
            p.setPen(QPen(c, 0.65 + 1.35*std::clamp(line.amp,0.0,1.0)));
            p.drawLine(line.a,line.b);
        }

        const QPointF maxQ = dirFor(m_maxThetaDeg,m_maxPhiDeg,1.0).q;
        p.setBrush(pal.color(QPalette::Highlight));
        p.setPen(QPen(pal.color(QPalette::Base),1.3));
        p.drawEllipse(maxQ,5.5,5.5);
        p.setPen(pal.color(QPalette::Highlight));
        p.drawText(QRectF(maxQ.x()+8,maxQ.y()-13,190,26),Qt::AlignLeft|Qt::AlignVCenter,
                   QStringLiteral("max θ=%1°, φ=%2°").arg(m_maxThetaDeg,0,'g',4).arg(m_maxPhiDeg,0,'g',4));

        if (m_showLegend && m_colorMapEnabled)
        {
            const double legendW = 18.0;
            const double legendH = std::max(120.0, std::min(height() * 0.45, 220.0));
            const QRectF legendRect(width() - 56.0, 42.0, legendW, legendH);
            for (int i = 0; i < static_cast<int>(legendH); ++i)
            {
                const double u = 1.0 - double(i) / std::max(1.0, legendH - 1.0);
                const double db = floorDb + u * (0.0 - floorDb);
                p.setPen(colorForDb(db));
                p.drawLine(QPointF(legendRect.left(), legendRect.top() + i), QPointF(legendRect.right(), legendRect.top() + i));
            }
            p.setPen(QPen(pal.color(QPalette::Mid), 1.0));
            p.drawRect(legendRect);
            p.setPen(pal.color(QPalette::Text));
            p.drawText(QRectF(legendRect.left() - 34.0, legendRect.top() - 18.0, 92.0, 16.0), Qt::AlignCenter, QStringLiteral("Gain (dBi)"));
            for (double db : {0.0, -10.0, -20.0, -30.0, floorDb})
            {
                const double y = legendRect.bottom() - ((db - floorDb) / (0.0 - floorDb)) * legendRect.height();
                p.drawLine(QPointF(legendRect.right() + 1.0, y), QPointF(legendRect.right() + 6.0, y));
                p.drawText(QRectF(legendRect.right() + 8.0, y - 9.0, 52.0, 18.0), Qt::AlignLeft | Qt::AlignVCenter,
                           QStringLiteral("%1").arg(m_peakGainDbi + db, 0, 'f', 0));
            }
        }

        if (m_hoverActive && !m_dragging)
        {
            int bestIndex = -1;
            double bestDist = 18.0;
            for (int i = 0; i < static_cast<int>(vertices.size()); ++i)
            {
                const double d = QLineF(m_hoverPosition, vertices[static_cast<std::size_t>(i)].q).length();
                if (d <= bestDist) { bestDist = d; bestIndex = i; }
            }
            if (bestIndex >= 0)
            {
                const auto &hv = vertices[static_cast<std::size_t>(bestIndex)];
                const double relDb = hv.amp > 1e-12 ? 20.0 * std::log10(std::clamp(hv.amp, 0.0, 1.0)) : floorDb;
                const double localDirectivityDbi = m_directivityDbi + relDb;
                const double localGainDbi = m_peakGainDbi + relDb;
                const double localRealizedGainDbi = std::isfinite(m_peakRealizedGainDbi) ? m_peakRealizedGainDbi + relDb : std::numeric_limits<double>::quiet_NaN();
                p.setBrush(pal.color(QPalette::Highlight));
                p.setPen(QPen(pal.color(QPalette::Base), 1.2));
                p.drawEllipse(hv.q, 5.0, 5.0);
                QStringList linesInfo;
                linesInfo << QStringLiteral("θ = %1°").arg(hv.thetaDeg, 0, 'g', 5)
                          << QStringLiteral("φ = %1°").arg(hv.phiDeg, 0, 'g', 5)
                          << QStringLiteral("Normalized field = %1").arg(hv.amp, 0, 'g', 5)
                          << QStringLiteral("Relative level = %1 dB").arg(relDb, 0, 'g', 5)
                          << QStringLiteral("Directivity ≈ %1 dBi").arg(localDirectivityDbi, 0, 'g', 5)
                          << QStringLiteral("Gain ≈ %1 dBi (ηrad=%2%)").arg(localGainDbi, 0, 'g', 5).arg(100.0*m_radiationEfficiency, 0, 'g', 4);
                if (std::isfinite(localRealizedGainDbi))
                    linesInfo << QStringLiteral("Realized gain ≈ %1 dBi").arg(localRealizedGainDbi, 0, 'g', 5);
                const QFontMetrics fm(p.font());
                int boxW = 0; for (const QString &line : linesInfo) boxW = std::max(boxW, fm.horizontalAdvance(line));
                const double lineH = fm.height() + 2.0;
                const double boxWidth = std::min(width() * 0.42, double(boxW) + 18.0);
                const double boxHeight = 10.0 + lineH * linesInfo.size();
                double boxX = hv.q.x() + 12.0;
                if (boxX + boxWidth > width() - 8.0) boxX = hv.q.x() - boxWidth - 12.0;
                boxX = std::clamp(boxX, 8.0, std::max(8.0, width() - boxWidth - 8.0));
                double boxY = hv.q.y() - boxHeight - 8.0;
                if (boxY < 28.0) boxY = hv.q.y() + 10.0;
                boxY = std::clamp(boxY, 28.0, std::max(28.0, height() - boxHeight - 26.0));
                QRectF box(boxX, boxY, boxWidth, boxHeight);
                QColor fill = pal.color(QPalette::Base); fill.setAlpha(232);
                p.setBrush(fill); p.setPen(QPen(pal.color(QPalette::Mid), 1.0)); p.drawRoundedRect(box, 6.0, 6.0);
                p.setPen(pal.color(QPalette::Text));
                for (int i = 0; i < linesInfo.size(); ++i)
                    p.drawText(QRectF(box.left() + 8.0, box.top() + 6.0 + i * lineH, box.width() - 12.0, lineH),
                               Qt::AlignLeft | Qt::AlignVCenter, linesInfo[i]);
            }
        }

        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(QRectF(8,height()-24,width()-16,18),Qt::AlignCenter,
                   QStringLiteral("Drag: rotate | wheel: zoom | double-click: reset | hover: inspect θ/φ/directivity/gain | color: absolute gain dBi | radial display: global normalized dB, floor −40 dB"));
    }

private:
    std::vector<double> m_thetaDeg;
    std::vector<double> m_phiDeg;
    std::vector<double> m_amplitude;
    double m_directivityDbi = 0.0;
    double m_peakGainDbi = 0.0;
    double m_peakRealizedGainDbi = std::numeric_limits<double>::quiet_NaN();
    double m_radiationEfficiency = 1.0;
    double m_maxThetaDeg = 0.0;
    double m_maxPhiDeg = 0.0;
    double m_yawDeg = -35.0;
    double m_pitchDeg = 24.0;
    double m_zoom = 1.0;
    bool m_dragging = false;
    QPointF m_lastMouse;
    bool m_colorMapEnabled = true;
    bool m_showLegend = true;
    bool m_hoverActive = false;
    QPointF m_hoverPosition;
};

AntennaDesignerWidget::AntennaDesignerWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    // v5.8: construction, geometry data, presets, simulation and results now live
    // in separate workspaces.  The Designer page therefore keeps almost the full
    // widget area for the orthographic/3D scene instead of sharing it permanently
    // with solver controls and result tables.
    auto *designBox = new QWidget(this);
    auto *design = new QVBoxLayout(designBox);

    // v5.8.1: keep the construction header on one compact horizontal row so the
    // graphics viewport gets the maximum possible vertical area. Global history/help
    // commands live at the far right; view navigation itself is overlaid in the editors.
    auto *topBar = new QHBoxLayout();
    topBar->setContentsMargins(0,0,0,0);
    topBar->setSpacing(6);
    // Design frequency is shared by wavelength-normalized presets and the EM solvers,
    // but it no longer consumes the construction viewport header.  It is exposed in
    // the Presets workspace below.  The per-wire radius is edited on the selected wire;
    // this hidden control only supplies the 5 mm default for newly created conductors.
    m_frequencyMHz = numberBox(designBox, 100.0, 0.001, 1e6, 6);
    m_wireRadiusMm = numberBox(designBox, 5.0, 0.001, 1e4, 4);
    m_gridMm = numberBox(designBox, 50.0, 1.0, 1e5, 2);
    m_frequencyMHz->setMaximumWidth(118);
    m_wireRadiusMm->setMaximumWidth(105);
    m_wireRadiusMm->hide();
    m_gridMm->setMaximumWidth(105);
    m_gridMm->hide(); // shared state holder; edited directly inside the 2D/3D viewport.
    m_editPlane = new QComboBox(designBox);
    m_editPlane->addItems({QStringLiteral("XY"), QStringLiteral("XZ"), QStringLiteral("YZ")});
    m_editPlane->setMaximumWidth(78);
    m_editPlane->setToolTip(QStringLiteral("Orthographic plane used for mouse drawing/editing. Numeric geometry tables always expose all X/Y/Z coordinates."));
    m_activePlaneCoordinateM = numberBox(designBox, 0.0, -1e6, 1e6, 6);
    m_activePlaneCoordinateM->setMaximumWidth(118);
    m_activePlaneCoordinateM->setToolTip(QStringLiteral("Hidden coordinate of newly drawn wires/feeds: Z for XY, Y for XZ, X for YZ."));
    m_editPlane->hide(); // state holder; XY/XZ/YZ is selected directly inside the 2D/3D viewport.
    m_activePlaneCoordinateM->hide(); // edited in the upper-left construction-plane overlay.
    topBar->addStretch(1);
    auto *undoButton = new QPushButton(QStringLiteral("Undo"), designBox);
    undoButton->setToolTip(QStringLiteral("Undo the last geometry edit (Ctrl+Z)."));
    auto *redoButton = new QPushButton(QStringLiteral("Redo"), designBox);
    redoButton->setToolTip(QStringLiteral("Redo the last undone geometry edit (Ctrl+Y / Ctrl+Shift+Z)."));
    auto *helpButton = new QToolButton(designBox);
    helpButton->setText(QStringLiteral("?"));
    helpButton->setAutoRaise(true);
    helpButton->setFixedWidth(28);
    helpButton->setToolTip(QStringLiteral("Antenna editor interaction help"));
    topBar->addWidget(undoButton);
    topBar->addWidget(redoButton);
    topBar->addWidget(helpButton);
    design->addLayout(topBar);

    // v5.8.8: tool selection now lives at the top-center of each graphical viewport.
    // This hidden combo remains the canonical shared state so 2D/3D selectors stay synchronized.
    auto *editorToolCombo = new QComboBox(designBox);
    editorToolCombo->addItems({QStringLiteral("Select / transform"),
                               QStringLiteral("Draw wire"),
                               QStringLiteral("Place feed"),
                               QStringLiteral("Delete near"),
                               QStringLiteral("Draw PEC rectangle (3D)"),
                               QStringLiteral("Draw substrate (3D)"),
                               QStringLiteral("Measure distance / angle (3D)"),
                               QStringLiteral("Pick custom pivot (3D)")});
    editorToolCombo->setToolTip(QStringLiteral("Shared construction-tool state for the 2D and 3D viewport selectors."));
    editorToolCombo->hide();

    // Defaults for one-shot substrate drawing remain internal; the created substrate can be
    // edited immediately from the viewport inspector or the Geometry workspace.
    auto *quickSubThicknessLabel = new QLabel(QStringLiteral("Sub t"), designBox);
    auto *quickSubThicknessMm=numberBox(designBox,1.6,0.001,1e6,4);quickSubThicknessMm->setSuffix(QStringLiteral(" mm"));quickSubThicknessMm->setMaximumWidth(110);
    auto *quickSubErLabel = new QLabel(QStringLiteral("εr"), designBox);
    auto *quickSubEr=numberBox(designBox,4.2,1.0,1e5,4);quickSubEr->setMaximumWidth(95);
    auto *clearMeasureButton=new QPushButton(QStringLiteral("Clear measure"),designBox);
    clearMeasureButton->setToolTip(QStringLiteral("Clear the temporary 3D distance/angle measurement overlay."));
    for (QWidget *w : {static_cast<QWidget*>(quickSubThicknessLabel), static_cast<QWidget*>(quickSubThicknessMm), static_cast<QWidget*>(quickSubErLabel), static_cast<QWidget*>(quickSubEr), static_cast<QWidget*>(clearMeasureButton)}) w->hide();

    // v5.8.7: transformations are no longer hidden behind two tabs.  The two compact
    // sections sit side-by-side so the scene keeps vertical space while the most useful
    // numerical operations remain one click away.  Mirror / surface-relation operations
    // are intentionally context-menu actions to avoid duplicate controls.
    auto *transformStrip = new QWidget(designBox);
    auto *transformStripLayout = new QHBoxLayout(transformStrip);
    transformStripLayout->setContentsMargins(0,0,0,0);
    transformStripLayout->setSpacing(8);

    auto *moveBox = new QGroupBox(QStringLiteral("Move / align"), transformStrip);
    auto *transformTools = new QVBoxLayout(moveBox);
    transformTools->setContentsMargins(6,5,6,5);
    transformTools->setSpacing(4);
    auto *transformPrimaryRow = new QHBoxLayout();
    transformPrimaryRow->setContentsMargins(0,0,0,0);
    transformPrimaryRow->setSpacing(5);
    auto *placementModeCombo = new QComboBox(moveBox);
    placementModeCombo->addItems({QStringLiteral("Free placement"),QStringLiteral("Magnetic anchor")});
    placementModeCombo->setCurrentIndex(0);
    placementModeCombo->setToolTip(QStringLiteral("Object snap mode: Free placement follows the cursor exactly. Magnetic anchor snaps moved wire endpoints and newly drawn wire endpoints to nearby existing wire endpoints/segments using a screen-space tolerance."));
    auto *duplicateButton = new QPushButton(QStringLiteral("Duplicate"), moveBox);
    duplicateButton->setToolTip(QStringLiteral("Duplicate the current 3D object selection (Ctrl+D)."));
    duplicateButton->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    auto *alignXButton = new QPushButton(QStringLiteral("Align X"), moveBox);
    auto *alignYButton = new QPushButton(QStringLiteral("Align Y"), moveBox);
    auto *alignZButton = new QPushButton(QStringLiteral("Align Z"), moveBox);
    for(auto *b:{alignXButton,alignYButton,alignZButton})b->setToolTip(QStringLiteral("Align selected object reference centers on the chosen global axis."));
    transformPrimaryRow->addWidget(placementModeCombo);
    transformPrimaryRow->addWidget(duplicateButton);
    transformPrimaryRow->addWidget(alignXButton); transformPrimaryRow->addWidget(alignYButton); transformPrimaryRow->addWidget(alignZButton);
    transformPrimaryRow->addStretch(1);
    transformTools->addLayout(transformPrimaryRow);

    auto *dxMm = numberBox(moveBox,0.0,-1e9,1e9,4);
    auto *dyMm = numberBox(moveBox,0.0,-1e9,1e9,4);
    auto *dzMm = numberBox(moveBox,0.0,-1e9,1e9,4);
    for(auto *spin:{dxMm,dyMm,dzMm}){spin->setSuffix(QStringLiteral(" mm"));spin->setSingleStep(1.0);spin->setMaximumWidth(108);}
    auto *moveNumericButton = new QPushButton(QStringLiteral("Move ΔXYZ"), moveBox);
    moveNumericButton->setToolTip(QStringLiteral("Translate the current 3D selection by exact ΔX / ΔY / ΔZ offsets."));
    auto *numericMoveRow = new QHBoxLayout();
    numericMoveRow->setContentsMargins(0,0,0,0);
    numericMoveRow->setSpacing(4);
    const auto addDeltaEditor=[&](const QString &name,QDoubleSpinBox *spin){
        auto *pair = new QWidget(moveBox);
        auto *pairLayout = new QHBoxLayout(pair);
        pairLayout->setContentsMargins(0,0,0,0);
        pairLayout->setSpacing(3);
        pairLayout->addWidget(new QLabel(name,pair));
        pairLayout->addWidget(spin);
        numericMoveRow->addWidget(pair);
    };
    addDeltaEditor(QStringLiteral("ΔX"),dxMm);
    addDeltaEditor(QStringLiteral("ΔY"),dyMm);
    addDeltaEditor(QStringLiteral("ΔZ"),dzMm);
    numericMoveRow->addWidget(moveNumericButton);
    numericMoveRow->addStretch(1);
    transformTools->addLayout(numericMoveRow);

    auto *rotateBox = new QGroupBox(QStringLiteral("Rotate / pivot"), transformStrip);
    auto *rotateTools = new QGridLayout(rotateBox);
    rotateTools->setContentsMargins(6,5,6,5);
    rotateTools->setHorizontalSpacing(5);
    rotateTools->setVerticalSpacing(4);
    auto *gizmoModeCombo = new QComboBox(rotateBox);
    gizmoModeCombo->addItems({QStringLiteral("Move gizmo"),QStringLiteral("Rotate gizmo")});
    auto *pivotCombo = new QComboBox(rotateBox);
    pivotCombo->addItems({QStringLiteral("Selection center"),QStringLiteral("World origin"),QStringLiteral("Active plane origin"),QStringLiteral("Custom pivot")});
    auto *pivotXmm=numberBox(rotateBox,0.0,-1e9,1e9,4);auto *pivotYmm=numberBox(rotateBox,0.0,-1e9,1e9,4);auto *pivotZmm=numberBox(rotateBox,0.0,-1e9,1e9,4);
    for(auto*s:{pivotXmm,pivotYmm,pivotZmm}){s->setSuffix(QStringLiteral(" mm"));s->setSingleStep(1.0);s->setMaximumWidth(92);s->setEnabled(false);}
    auto *rotationAxisCombo=new QComboBox(rotateBox);rotationAxisCombo->addItems({QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("Z")});
    auto *rotationDeg=numberBox(rotateBox,90.0,-360000.0,360000.0,3);rotationDeg->setSuffix(QStringLiteral(" deg"));rotationDeg->setSingleStep(5.0);rotationDeg->setMaximumWidth(105);
    auto *rotateButton=new QPushButton(QStringLiteral("Rotate"),rotateBox);
    rotateTools->addWidget(gizmoModeCombo,0,0);
    rotateTools->addWidget(new QLabel(QStringLiteral("Pivot"),rotateBox),0,1); rotateTools->addWidget(pivotCombo,0,2,1,2);
    rotateTools->addWidget(new QLabel(QStringLiteral("Axis"),rotateBox),0,4); rotateTools->addWidget(rotationAxisCombo,0,5); rotateTools->addWidget(rotationDeg,0,6); rotateTools->addWidget(rotateButton,0,7);
    rotateTools->addWidget(new QLabel(QStringLiteral("Custom XYZ"),rotateBox),1,0);
    rotateTools->addWidget(pivotXmm,1,1); rotateTools->addWidget(pivotYmm,1,2); rotateTools->addWidget(pivotZmm,1,3);
    rotateTools->setColumnStretch(8,1);

    transformStripLayout->addWidget(moveBox,3);
    transformStripLayout->addWidget(rotateBox,2);
    design->addWidget(transformStrip);

    auto *geometryViews = new QTabWidget(designBox);
    m_canvas = new Canvas(geometryViews);
    m_geometry3D = new Geometry3DView(geometryViews);
    geometryViews->addTab(m_canvas, QStringLiteral("Orthographic editor"));
    geometryViews->addTab(m_geometry3D, QStringLiteral("3D editor"));
    const auto requestEditPlane=[this](int plane){if(m_editPlane&&m_editPlane->currentIndex()!=plane)m_editPlane->setCurrentIndex(plane);};
    m_canvas->editPlaneChangeRequested=requestEditPlane;
    m_geometry3D->editPlaneChangeRequested=requestEditPlane;
    m_geometry3D->planeCoordinateChangeRequested=[this](double value){if(m_activePlaneCoordinateM&&std::abs(m_activePlaneCoordinateM->value()-value)>1e-12)m_activePlaneCoordinateM->setValue(value);};
    m_geometry3D->constructionPlaneOrientationChanged=[this](double rx,double ry,double rz){m_constructionPlaneRotXDeg=rx;m_constructionPlaneRotYDeg=ry;m_constructionPlaneRotZDeg=rz;};
    m_canvas->toolChangeRequested=[editorToolCombo](int index){editorToolCombo->setCurrentIndex(index);};
    m_geometry3D->editorToolRequested=[editorToolCombo](int index){editorToolCombo->setCurrentIndex(index);};
    m_canvas->gridChangeRequested=[this](double mm){if(m_gridMm&&std::abs(m_gridMm->value()-mm)>1e-9)m_gridMm->setValue(mm);};
    m_geometry3D->gridChangeRequested=[this](double mm){if(m_gridMm&&std::abs(m_gridMm->value()-mm)>1e-9)m_gridMm->setValue(mm);};
    geometryViews->setTabToolTip(0, QStringLiteral("Draw and drag in the selected XY/XZ/YZ projection. Wheel: zoom. Middle drag: pan. Right click opens the CAD context menu."));
    geometryViews->setTabToolTip(1, QStringLiteral("Direct 3D CAD editing on an orientable construction plane. Middle drag: pan, left drag in empty space: orbit, wheel: zoom, right click: context actions. The construction plane is fixed in the upper-left; ViewCube/camera controls stay in the upper-right."));
    design->addWidget(geometryViews, 1);
    designBox->setMinimumWidth(520);

    auto *setupPage = new QWidget(this);
    auto *setupLayout = new QVBoxLayout(setupPage);
    setupLayout->setContentsMargins(4, 4, 4, 4);
    auto *constraintPage = new QWidget(this);
    auto *constraintPageLayout = new QVBoxLayout(constraintPage);
    constraintPageLayout->setContentsMargins(4,4,4,4);
    auto *presetPage = new QWidget(this);
    auto *presetPageLayout = new QVBoxLayout(presetPage);
    presetPageLayout->setContentsMargins(4, 4, 4, 4);
    auto *primitivePage = new QWidget(this);
    auto *primitivePageLayout = new QVBoxLayout(primitivePage);
    primitivePageLayout->setContentsMargins(4, 4, 4, 4);
    auto *feedSimulationPage = new QWidget(this);
    auto *feedSimulationLayout = new QVBoxLayout(feedSimulationPage);
    feedSimulationLayout->setContentsMargins(4, 4, 4, 4);
    auto *momPage = new QWidget(this);
    auto *momPageLayout = new QVBoxLayout(momPage);
    auto *validationPage = new QWidget(this);
    auto *validationPageLayout = new QVBoxLayout(validationPage);
    validationPageLayout->setContentsMargins(4, 4, 4, 4);
    momPageLayout->setContentsMargins(4, 4, 4, 4);
    auto *sweepPage = new QWidget(this);
    auto *sweepPageLayout = new QVBoxLayout(sweepPage);
    sweepPageLayout->setContentsMargins(4, 4, 4, 4);
    auto *optPage = new QWidget(this);
    auto *optPageLayout = new QVBoxLayout(optPage);
    optPageLayout->setContentsMargins(4, 4, 4, 4);
    auto wrapWorkspacePage = [this](QWidget *page) {
        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setWidget(page);
        return scroll;
    };

    auto *feedBox = new QGroupBox(QStringLiteral("Defaults for newly created feeds"), feedSimulationPage);
    auto *feedForm = new QFormLayout(feedBox);
    m_feedVoltageV = numberBox(feedBox, 1.0, 0.0, 1e6, 6);
    m_feedPhaseDeg = numberBox(feedBox, 0.0, -3600.0, 3600.0, 3);
    m_feedSourceOhm = numberBox(feedBox, 50.0, 0.001, 1e6, 4);
    m_feedVoltageV->setToolTip(QStringLiteral("Default magnitude used only when a new feed is created. Existing feeds are edited independently in the table below."));
    m_feedPhaseDeg->setToolTip(QStringLiteral("Default source phase used only for newly created feeds."));
    m_feedSourceOhm->setToolTip(QStringLiteral("Default reference impedance used for Γ/S11/VSWR. It is not inserted as a physical series resistor into the thin-wire MoM."));
    feedForm->addRow(QStringLiteral("Voltage magnitude (V)"), m_feedVoltageV);
    feedForm->addRow(QStringLiteral("Phase (deg)"), m_feedPhaseDeg);
    feedForm->addRow(QStringLiteral("Reference Z0 (Ω)"), m_feedSourceOhm);
    feedSimulationLayout->addWidget(feedBox);

    auto *feedExcitationBox = new QGroupBox(QStringLiteral("Per-feed excitation / port settings"), feedSimulationPage);
    auto *feedExcitationLayout = new QVBoxLayout(feedExcitationBox);
    auto *feedObservedRow = new QHBoxLayout();
    auto *feedObservedLabel = new QLabel(QStringLiteral("Reported / exported feed"), feedExcitationBox);
    m_reportedFeed = new QComboBox(feedExcitationBox);
    m_reportedFeed->setToolTip(QStringLiteral("Feed whose active impedance is exported to the RF/EM tools after a direct wire-MoM solve. All feed rows are still excited simultaneously."));
    feedObservedRow->addWidget(feedObservedLabel);
    feedObservedRow->addWidget(m_reportedFeed, 1);
    feedExcitationLayout->addLayout(feedObservedRow);

    m_feedSimulationTable = new QTableWidget(0, 5, feedExcitationBox);
    m_feedSimulationTable->setHorizontalHeaderLabels({QStringLiteral("Feed"),QStringLiteral("|V| (V)"),QStringLiteral("Phase °"),QStringLiteral("Zref Ω"),QStringLiteral("Voltage phasor")});
    m_feedSimulationTable->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    m_feedSimulationTable->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    m_feedSimulationTable->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    m_feedSimulationTable->horizontalHeader()->setSectionResizeMode(3,QHeaderView::ResizeToContents);
    m_feedSimulationTable->horizontalHeader()->setSectionResizeMode(4,QHeaderView::Stretch);
    m_feedSimulationTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_feedSimulationTable->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::SelectedClicked|QAbstractItemView::EditKeyPressed);
    m_feedSimulationTable->setToolTip(QStringLiteral("Each feed has its own excitation magnitude, phase and S-parameter reference impedance. The MoM solves all feed excitations simultaneously; active Zin at one feed therefore depends on the amplitude/phase of the other driven feeds."));
    feedExcitationLayout->addWidget(m_feedSimulationTable);
    auto *feedExcitationNote = new QLabel(QStringLiteral("All feed voltages are complex phasors applied simultaneously. Zref is used only to compute Γ / S11 / return loss / VSWR; changing Zref does not add a physical source resistance to the EM model. A 0 V feed remains present as a feed marker/zero source in the solve; it is not the same as deleting or electrically opening that port. For a conventional centre-fed wire dipole, use two wire spans sharing ONE electrical centre node and ONE feed at that node; do not place one independent feed on each separated arm."), feedExcitationBox);
    feedExcitationNote->setWordWrap(true);
    feedExcitationNote->setTextInteractionFlags(Qt::TextSelectableByMouse);
    feedExcitationLayout->addWidget(feedExcitationNote);
    feedSimulationLayout->addWidget(feedExcitationBox,1);

    auto *fileBox = new QGroupBox(QStringLiteral("Geometry file"), setupPage);
    auto *fileLayout = new QVBoxLayout(fileBox);
    auto *save = new QPushButton(QStringLiteral("Save antenna..."), fileBox);
    auto *load = new QPushButton(QStringLiteral("Load antenna..."), fileBox);
    auto *clear = new QPushButton(QStringLiteral("New / clear"), fileBox);
    fileLayout->addWidget(save); fileLayout->addWidget(load); fileLayout->addWidget(clear);
    setupLayout->addWidget(fileBox);

    auto *summaryBox = new QGroupBox(QStringLiteral("Geometry summary"), setupPage);
    auto *summaryLayout = new QVBoxLayout(summaryBox);
    m_summary = new QLabel(summaryBox);
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summaryLayout->addWidget(m_summary);
    setupLayout->addWidget(summaryBox);

    auto *constraintBox = new QGroupBox(QStringLiteral("Parametric geometry constraints"), constraintPage);
    constraintBox->setToolTip(QStringLiteral("Persistent CAD constraints stored in .qta. Driven mode uses A as reference and adjusts B; Balanced mode shares supported corrections between A and B. Position-axis locks act as anchors."));
    auto *constraintLayout = new QVBoxLayout(constraintBox);
    auto *constraintForm = new QGridLayout();
    m_constraintType = new QComboBox(constraintBox);
    m_constraintType->addItems({QStringLiteral("Coincident points"),QStringLiteral("Parallel wires"),QStringLiteral("Perpendicular wires"),QStringLiteral("Equal wire length"),QStringLiteral("Point distance"),QStringLiteral("Equal surface W/H"),QStringLiteral("Wire angle"),QStringLiteral("Wire length (B)"),QStringLiteral("Concentric PEC axes"),QStringLiteral("Wire parallel to surface"),QStringLiteral("Wire normal to surface"),QStringLiteral("Point-to-surface distance"),QStringLiteral("Wire tangent to PEC disk"),QStringLiteral("Fix position axes")});
    m_constraintRefA = new QComboBox(constraintBox);m_constraintRefB = new QComboBox(constraintBox);
    m_constraintValueMm = numberBox(constraintBox,10.0,0.0,1e9,5);m_constraintValueMm->setSuffix(QStringLiteral(" mm"));m_constraintValueMm->setEnabled(false);
    m_constraintSolveMode = new QComboBox(constraintBox);m_constraintSolveMode->addItems({QStringLiteral("Driven B (deterministic)"),QStringLiteral("Balanced A/B")});
    m_constraintSolveMode->setToolTip(QStringLiteral("Balanced mode shares positional/dimensional corrections between A and B when supported. Direction/orientation constraints remain reference-driven in this first implementation."));
    m_constraintFixAxes = new QComboBox(constraintBox);
    m_constraintFixAxes->addItem(QStringLiteral("X"),1);m_constraintFixAxes->addItem(QStringLiteral("Y"),2);m_constraintFixAxes->addItem(QStringLiteral("Z"),4);m_constraintFixAxes->addItem(QStringLiteral("XY"),3);m_constraintFixAxes->addItem(QStringLiteral("XZ"),5);m_constraintFixAxes->addItem(QStringLiteral("YZ"),6);m_constraintFixAxes->addItem(QStringLiteral("XYZ"),7);m_constraintFixAxes->setCurrentIndex(6);m_constraintFixAxes->setEnabled(false);
    auto *addConstraint = new QPushButton(QStringLiteral("Add constraint"),constraintBox);
    auto *solveConstraints = new QPushButton(QStringLiteral("Solve now"),constraintBox);
    auto *toggleConstraints = new QPushButton(QStringLiteral("Enable / disable"),constraintBox);
    auto *removeConstraints = new QPushButton(QStringLiteral("Remove selected"),constraintBox);
    constraintForm->addWidget(new QLabel(QStringLiteral("Type"),constraintBox),0,0);constraintForm->addWidget(m_constraintType,0,1,1,3);
    constraintForm->addWidget(new QLabel(QStringLiteral("A (reference)"),constraintBox),1,0);constraintForm->addWidget(m_constraintRefA,1,1,1,3);
    constraintForm->addWidget(new QLabel(QStringLiteral("B (driven)"),constraintBox),2,0);constraintForm->addWidget(m_constraintRefB,2,1,1,3);
    constraintForm->addWidget(new QLabel(QStringLiteral("Value"),constraintBox),3,0);constraintForm->addWidget(m_constraintValueMm,3,1);constraintForm->addWidget(addConstraint,3,2);constraintForm->addWidget(solveConstraints,3,3);
    constraintForm->addWidget(new QLabel(QStringLiteral("Solve mode"),constraintBox),4,0);constraintForm->addWidget(m_constraintSolveMode,4,1,1,2);constraintForm->addWidget(m_constraintFixAxes,4,3);
    constraintLayout->addLayout(constraintForm);
    m_constraintTable = new QTableWidget(constraintBox);m_constraintTable->setColumnCount(8);m_constraintTable->setHorizontalHeaderLabels({QStringLiteral("On"),QStringLiteral("Type"),QStringLiteral("A"),QStringLiteral("B"),QStringLiteral("Value"),QStringLiteral("Mode"),QStringLiteral("Residual"),QStringLiteral("State")});
    m_constraintTable->setSelectionBehavior(QAbstractItemView::SelectRows);m_constraintTable->setSelectionMode(QAbstractItemView::ExtendedSelection);m_constraintTable->setEditTriggers(QAbstractItemView::NoEditTriggers);m_constraintTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);m_constraintTable->setMinimumHeight(135);
    constraintLayout->addWidget(m_constraintTable);
    auto *constraintButtons = new QHBoxLayout();constraintButtons->addWidget(toggleConstraints);constraintButtons->addWidget(removeConstraints);constraintButtons->addStretch(1);constraintLayout->addLayout(constraintButtons);
    m_constraintStatus = new QLabel(constraintBox);m_constraintStatus->setWordWrap(true);constraintLayout->addWidget(m_constraintStatus);
    m_constraintGraphStatus = new QLabel(constraintBox);m_constraintGraphStatus->setWordWrap(true);m_constraintGraphStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);m_constraintGraphStatus->setToolTip(QStringLiteral("Informative dependency-graph and degree-of-freedom estimate. This is not a full symbolic/Jacobian rank analysis."));constraintLayout->addWidget(m_constraintGraphStatus);
    m_constraintDofTable = new QTableWidget(constraintBox);m_constraintDofTable->setColumnCount(4);m_constraintDofTable->setHorizontalHeaderLabels({QStringLiteral("Object"),QStringLiteral("Free / total DOF"),QStringLiteral("Driven by"),QStringLiteral("Locks / state")});m_constraintDofTable->setEditTriggers(QAbstractItemView::NoEditTriggers);m_constraintDofTable->setSelectionMode(QAbstractItemView::NoSelection);m_constraintDofTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);m_constraintDofTable->setMaximumHeight(150);constraintLayout->addWidget(m_constraintDofTable);
    constraintPageLayout->addWidget(constraintBox);
    constraintPageLayout->addStretch(1);
    setupLayout->addStretch(1);

    auto *presetLibraryBox = new QGroupBox(QStringLiteral("Antenna preset library"), presetPage);
    auto *presetLibraryLayout = new QGridLayout(presetLibraryBox);
    m_presetCombo = new QComboBox(presetLibraryBox);
    m_presetCombo->addItems({QStringLiteral("Wire — λ/2 dipole"),
                             QStringLiteral("Wire — V-dipole"),
                             QStringLiteral("Wire — Folded dipole"),
                             QStringLiteral("Wire — Square loop"),
                             QStringLiteral("Array — Yagi (N directors)"),
                             QStringLiteral("3D — Helical antenna"),
                             QStringLiteral("Printed — Probe-fed rectangular patch"),
                             QStringLiteral("Printed — Inset-fed microstrip patch"),
                             QStringLiteral("Ground-plane — λ/4 monopole over PEC ground"),
                             QStringLiteral("Hybrid — Inverted-F antenna")});
    m_presetCombo->setToolTip(QStringLiteral("Educational starting geometries. The λ/2 dipole preset is a ONE-port centre-fed topology: two spans share the same electrical centre node and one feed is placed on that node. Driven length is TOTAL element length, so each arm is approximately λ/4. Select a family, tune dimensions, then build it into the Designer workspace."));
    m_presetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *preset = new QPushButton(QStringLiteral("Build / replace geometry"), presetLibraryBox);
    preset->setToolTip(QStringLiteral("Replace the current antenna geometry with the selected preset. Undo restores the previous geometry."));
    presetLibraryLayout->addWidget(new QLabel(QStringLiteral("Preset"), presetLibraryBox),0,0);
    presetLibraryLayout->addWidget(m_presetCombo,0,1);
    presetLibraryLayout->addWidget(preset,0,2);
    presetLibraryLayout->addWidget(new QLabel(QStringLiteral("Design frequency (MHz)"), presetLibraryBox),1,0);
    m_frequencyMHz->setParent(presetLibraryBox);
    presetLibraryLayout->addWidget(m_frequencyMHz,1,1);
    auto *frequencyHint = new QLabel(QStringLiteral("Shared by λ-normalized presets and single-frequency MoM/RWG/hybrid solves."), presetLibraryBox);
    frequencyHint->setWordWrap(true);
    presetLibraryLayout->addWidget(frequencyHint,1,2);
    presetLibraryLayout->setColumnStretch(1,1);
    presetPageLayout->addWidget(presetLibraryBox);

    auto *presetBox = new QGroupBox(QStringLiteral("Parametric preset dimensions"), presetPage);
    presetBox->setToolTip(QStringLiteral("Dimensions are normalized to the wavelength at Design f. Rebuild the selected preset after changing a parameter."));
    auto *presetForm = new QFormLayout(presetBox);
    m_presetDrivenLambda = numberBox(presetBox, 0.475, 0.05, 3.0, 5);
    m_presetDrivenLambda->setToolTip(QStringLiteral("Total driven-element length, not arm length. For a two-arm dipole, each arm is half this value: exact geometric λ/2 means 0.25 λ per arm. The default 0.475 λ applies the usual practical shortening used to start near resonance."));
    m_presetVOpeningDeg = numberBox(presetBox, 120.0, 10.0, 180.0, 2);
    m_presetFoldedSpacingLambda = numberBox(presetBox, 0.020, 0.001, 0.5, 5);
    m_presetLoopPerimeterLambda = numberBox(presetBox, 1.000, 0.05, 5.0, 5);
    m_presetYagiReflectorLambda = numberBox(presetBox, 0.500, 0.05, 2.0, 5);
    m_presetYagiDirectorLambda = numberBox(presetBox, 0.440, 0.05, 2.0, 5);
    m_presetYagiReflectorSpacingLambda = numberBox(presetBox, 0.200, 0.01, 2.0, 5);
    m_presetYagiDirectorSpacingLambda = numberBox(presetBox, 0.150, 0.01, 2.0, 5);
    m_presetYagiDirectorCount = new QSpinBox(presetBox);
    m_presetYagiDirectorCount->setRange(1, 12);
    m_presetYagiDirectorCount->setValue(1);
    m_presetYagiDirectorPitchLambda = numberBox(presetBox, 0.150, 0.01, 2.0, 5);
    m_presetYagiDirectorTaperLambda = numberBox(presetBox, 0.008, 0.0, 0.10, 5);
    m_presetHelixRadiusLambda = numberBox(presetBox, 0.16, 0.01, 1.0, 5);
    m_presetHelixPitchLambda = numberBox(presetBox, 0.22, 0.01, 1.0, 5);
    m_presetHelixTurns = numberBox(presetBox, 5.0, 0.5, 30.0, 2);
    m_presetPatchEr = numberBox(presetBox, 4.2, 1.0, 100.0, 4);
    m_presetPatchHeightMm = numberBox(presetBox, 1.6, 0.01, 100.0, 4);
    m_presetPatchTanD = numberBox(presetBox, 0.02, 0.0, 1.0, 6);
    m_presetPatchLineZ0 = numberBox(presetBox, 50.0, 1.0, 1000.0, 3);
    m_presetPatchEdgeResistance = numberBox(presetBox, 300.0, 1.0, 5000.0, 2);
    m_presetPatchNotchGapMm = numberBox(presetBox, 0.40, 0.0, 20.0, 4);
    presetForm->addRow(QStringLiteral("Driven total length / λ"), m_presetDrivenLambda);
    presetForm->addRow(QStringLiteral("V opening angle (deg)"), m_presetVOpeningDeg);
    presetForm->addRow(QStringLiteral("Folded spacing / λ"), m_presetFoldedSpacingLambda);
    presetForm->addRow(QStringLiteral("Loop perimeter / λ"), m_presetLoopPerimeterLambda);
    presetForm->addRow(QStringLiteral("Yagi reflector length / λ"), m_presetYagiReflectorLambda);
    presetForm->addRow(QStringLiteral("Yagi director length / λ"), m_presetYagiDirectorLambda);
    presetForm->addRow(QStringLiteral("Yagi reflector spacing / λ"), m_presetYagiReflectorSpacingLambda);
    presetForm->addRow(QStringLiteral("Yagi first director spacing / λ"), m_presetYagiDirectorSpacingLambda);
    presetForm->addRow(QStringLiteral("Yagi director count"), m_presetYagiDirectorCount);
    presetForm->addRow(QStringLiteral("Yagi next-director pitch / λ"), m_presetYagiDirectorPitchLambda);
    presetForm->addRow(QStringLiteral("Yagi length taper / director / λ"), m_presetYagiDirectorTaperLambda);
    presetForm->addRow(QStringLiteral("Helix radius / λ"), m_presetHelixRadiusLambda);
    presetForm->addRow(QStringLiteral("Helix pitch / turn / λ"), m_presetHelixPitchLambda);
    presetForm->addRow(QStringLiteral("Helix turns"), m_presetHelixTurns);
    presetForm->addRow(QStringLiteral("Patch substrate εr"), m_presetPatchEr);
    presetForm->addRow(QStringLiteral("Patch substrate h (mm)"), m_presetPatchHeightMm);
    presetForm->addRow(QStringLiteral("Patch substrate tanδ"), m_presetPatchTanD);
    presetForm->addRow(QStringLiteral("Microstrip target Z0 (Ω)"), m_presetPatchLineZ0);
    presetForm->addRow(QStringLiteral("Patch edge R estimate (Ω)"), m_presetPatchEdgeResistance);
    presetForm->addRow(QStringLiteral("Inset side gap (mm)"), m_presetPatchNotchGapMm);
    m_presetPatchEstimate = new QLabel(presetBox);
    m_presetPatchEstimate->setWordWrap(true);
    m_presetPatchEstimate->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_presetPatchEstimate->setToolTip(QStringLiteral("First-order transmission-line/Hammerstad estimates used only to seed the printed-antenna preset. The hybrid MoM result remains the numerical result to inspect and converge."));
    presetForm->addRow(QStringLiteral("Printed estimate"), m_presetPatchEstimate);
    auto *rebuildPreset = new QPushButton(QStringLiteral("Rebuild selected preset"), presetBox);
    presetForm->addRow(rebuildPreset);
    presetPageLayout->addWidget(presetBox);
    presetPageLayout->addStretch(1);

    auto *primitiveBox = new QGroupBox(QStringLiteral("Parametric antenna element builder"), primitivePage);
    auto *primitiveForm = new QFormLayout(primitiveBox);
    m_primitiveType = new QComboBox(primitiveBox);
    m_primitiveType->addItems({QStringLiteral("Circular loop"),
                               QStringLiteral("Circular arc"),
                               QStringLiteral("Rectangular loop"),
                               QStringLiteral("Helix / solenoid wire"),
                               QStringLiteral("Planar spiral"),
                               QStringLiteral("Meander / serpentine wire"),
                               QStringLiteral("Rectangular plane / sheet"),
                               QStringLiteral("Disk / annular sheet"),
                               QStringLiteral("Cylindrical shell"),
                               QStringLiteral("Conical shell"),
                               QStringLiteral("Parabolic reflector / dish"),
                               QStringLiteral("Dielectric substrate / slab")});
    m_primitiveOrientation = new QComboBox(primitiveBox);
    m_primitiveOrientation->addItems({QStringLiteral("XY plane (normal Z)"), QStringLiteral("XZ plane (normal Y)"), QStringLiteral("YZ plane (normal X)")});
    m_primitiveOriginX = numberBox(primitiveBox, 0.0, -1e6, 1e6, 6);
    m_primitiveOriginY = numberBox(primitiveBox, 0.0, -1e6, 1e6, 6);
    m_primitiveOriginZ = numberBox(primitiveBox, 0.0, -1e6, 1e6, 6);
    m_primitiveYawDeg = numberBox(primitiveBox, 0.0, -36000.0, 36000.0, 2);
    m_primitivePitchDeg = numberBox(primitiveBox, 0.0, -36000.0, 36000.0, 2);
    m_primitiveRollDeg = numberBox(primitiveBox, 0.0, -36000.0, 36000.0, 2);
    m_primitiveRadiusM = numberBox(primitiveBox, 0.20, 1e-6, 1e6, 6);
    m_primitiveInnerRadiusM = numberBox(primitiveBox, 0.03, 0.0, 1e6, 6);
    m_primitiveWidthM = numberBox(primitiveBox, 0.50, 1e-6, 1e6, 6);
    m_primitiveHeightM = numberBox(primitiveBox, 0.30, 1e-6, 1e6, 6);
    m_primitiveFocalLengthM = numberBox(primitiveBox, 0.25, 1e-6, 1e6, 6);
    m_primitivePitchM = numberBox(primitiveBox, 0.08, 0.0, 1e6, 6);
    m_primitiveThicknessM = numberBox(primitiveBox, 0.0016, 1e-6, 1e3, 7);
    m_primitiveDielectricEr = numberBox(primitiveBox, 4.2, 1.0, 1000.0, 5);
    m_primitiveDielectricTanD = numberBox(primitiveBox, 0.02, 0.0, 10.0, 6);
    m_primitiveDielectricFill = numberBox(primitiveBox, 0.65, 0.0, 1.0, 4);
    m_primitiveTurns = numberBox(primitiveBox, 4.0, 0.05, 1000.0, 3);
    m_primitiveStartDeg = numberBox(primitiveBox, 0.0, -36000.0, 36000.0, 2);
    m_primitiveSweepDeg = numberBox(primitiveBox, 360.0, -36000.0, 36000.0, 2);
    m_primitiveGridSpacingM = numberBox(primitiveBox, 0.05, 1e-5, 1e6, 6);
    m_primitiveSegments = new QSpinBox(primitiveBox); m_primitiveSegments->setRange(4, 180); m_primitiveSegments->setValue(32);
    m_primitiveAddFeed = new QCheckBox(QStringLiteral("Add a feed at a generated path node"), primitiveBox);
    m_primitiveAddFeed->setChecked(false);
    m_primitiveClearFirst = new QCheckBox(QStringLiteral("Clear existing geometry before adding"), primitiveBox);
    m_primitiveClearFirst->setChecked(false);
    primitiveForm->addRow(QStringLiteral("Element type"), m_primitiveType);
    primitiveForm->addRow(QStringLiteral("Orientation"), m_primitiveOrientation);
    primitiveForm->addRow(QStringLiteral("Origin X (m)"), m_primitiveOriginX);
    primitiveForm->addRow(QStringLiteral("Origin Y (m)"), m_primitiveOriginY);
    primitiveForm->addRow(QStringLiteral("Origin Z (m)"), m_primitiveOriginZ);
    primitiveForm->addRow(QStringLiteral("Yaw around world Z (deg)"), m_primitiveYawDeg);
    primitiveForm->addRow(QStringLiteral("Pitch around world Y (deg)"), m_primitivePitchDeg);
    primitiveForm->addRow(QStringLiteral("Roll around world X (deg)"), m_primitiveRollDeg);
    primitiveForm->addRow(QStringLiteral("Radius / outer radius (m)"), m_primitiveRadiusM);
    primitiveForm->addRow(QStringLiteral("Inner radius (m)"), m_primitiveInnerRadiusM);
    primitiveForm->addRow(QStringLiteral("Width (m)"), m_primitiveWidthM);
    primitiveForm->addRow(QStringLiteral("Height / axial length (m)"), m_primitiveHeightM);
    primitiveForm->addRow(QStringLiteral("Focal length (m)"), m_primitiveFocalLengthM);
    primitiveForm->addRow(QStringLiteral("Pitch / turn (m)"), m_primitivePitchM);
    primitiveForm->addRow(QStringLiteral("Substrate thickness (m)"), m_primitiveThicknessM);
    primitiveForm->addRow(QStringLiteral("Substrate εr"), m_primitiveDielectricEr);
    primitiveForm->addRow(QStringLiteral("Substrate tanδ"), m_primitiveDielectricTanD);
    primitiveForm->addRow(QStringLiteral("Effective field fill 0..1"), m_primitiveDielectricFill);
    primitiveForm->addRow(QStringLiteral("Turns / folds"), m_primitiveTurns);
    primitiveForm->addRow(QStringLiteral("Start angle (deg)"), m_primitiveStartDeg);
    primitiveForm->addRow(QStringLiteral("Sweep angle (deg)"), m_primitiveSweepDeg);
    primitiveForm->addRow(QStringLiteral("Curve segments / turn"), m_primitiveSegments);
    primitiveForm->addRow(QStringLiteral("Surface grid hint (m)"), m_primitiveGridSpacingM);
    primitiveForm->addRow(m_primitiveAddFeed);
    primitiveForm->addRow(m_primitiveClearFirst);
    auto *buildPrimitiveButton = new QPushButton(QStringLiteral("Add primitive to geometry"), primitiveBox);
    primitiveForm->addRow(buildPrimitiveButton);
    primitivePageLayout->addWidget(primitiveBox);
    m_primitiveHelp = new QLabel(primitivePage);
    m_primitiveHelp->setWordWrap(true);
    m_primitiveHelp->setTextInteractionFlags(Qt::TextSelectableByMouse);
    primitivePageLayout->addWidget(m_primitiveHelp);
    auto *surfaceNote = new QLabel(QStringLiteral("Surface primitives are stored as triangulated PEC geometry. They can be solved alone with the EFIE/RWG surface solver or together with driven wires using the hybrid block-MoM solver. The hybrid stage solves wire and surface currents simultaneously, so PEC planes/reflexors can modify feed impedance and radiation. Euler yaw/pitch/roll are applied after the selected XY/XZ/YZ base orientation."), primitivePage);
    surfaceNote->setWordWrap(true);
    primitivePageLayout->addWidget(surfaceNote);
    primitivePageLayout->addStretch(1);

    auto *momBox = new QGroupBox(QStringLiteral("Generalized thin-wire MoM"), momPage);
    momBox->setToolTip(QStringLiteral("PEC/free-space pulse-basis EFIE. For multiple feeds, phase is referenced to a deterministic path orientation; explicit +/- feed-polarity markers are a future UI improvement."));
    auto *momLayout = new QVBoxLayout(momBox);
    auto *momForm = new QFormLayout();
    m_segmentsPerWavelength = new QSpinBox(momBox);
    m_segmentsPerWavelength->setRange(20, 200);
    m_segmentsPerWavelength->setValue(80);
    m_segmentsPerWavelength->setToolTip(QStringLiteral("Nominal wavelength criterion. The solver may refine further so segment length is also comparable with the physical wire radius."));
    m_maxMomUnknowns = new QSpinBox(momBox);
    m_maxMomUnknowns->setRange(100, 1200);
    m_maxMomUnknowns->setSingleStep(50);
    m_maxMomUnknowns->setValue(450);
    m_maxMomUnknowns->setToolTip(QStringLiteral("Dense MoM matrix size limit. Large values can make a Debug build slow."));
    m_radiusMeshFactor = numberBox(momBox, 3.5, 2.0, 50.0, 2);
    m_radiusMeshFactor->setToolTip(QStringLiteral("Maximum pulse length expressed as a multiple of wire radius. Smaller values refine the mesh strongly; larger values are faster. Always verify convergence by reducing this value and/or increasing segments per wavelength."));
    m_junctionLocalSubdivisions = new QSpinBox(momBox);
    m_junctionLocalSubdivisions->setRange(1, 8);
    m_junctionLocalSubdivisions->setValue(3);
    m_junctionLocalSubdivisions->setToolTip(QStringLiteral("Local T/Y/X refinement. Only the first nominal pulse on each arm connected to a branched junction is split into this many smaller pulses. 1 disables the local refinement; 3 is a useful default without refining the whole antenna."));
    m_junctionTreatment = new QComboBox(momBox);
    m_junctionTreatment->addItem(QStringLiteral("Reduced junction basis (recommended pulse mode)"));
    m_junctionTreatment->addItem(QStringLiteral("KCL Lagrange multipliers (legacy pulse mode)"));
    m_junctionTreatment->setCurrentIndex(0);
    m_junctionTreatment->setToolTip(QStringLiteral("Treatment of T/Y/X junctions when the historical pulse-current basis is selected. The linear rooftop/charge basis enforces ordinary continuity, open-end current and branch KCL directly through its local node basis, so this selector is then informational only."));
    m_currentBasisTreatment = new QComboBox(momBox);
    m_currentBasisTreatment->addItem(QStringLiteral("Pulse current / point matching (legacy baseline)"));
    m_currentBasisTreatment->addItem(QStringLiteral("Linear rooftop + charge (experimental Galerkin)"));
    m_currentBasisTreatment->setCurrentIndex(0);
    m_currentBasisTreatment->setToolTip(QStringLiteral("Pulse mode is the validated 1.0 production default for wire-only and electrically small closed-loop antennas and preserves the historical point-matching regression baseline. For coupled wire+PEC finite-ground antennas, the validation campaign uses the linear rooftop + RWG path as the production basis because its impedance/pattern mesh convergence is substantially better. The canonical validation bench can reveal physical disagreement even when this legacy path is numerically stable. Linear rooftop + charge uses node-centred piecewise-linear current functions: I=0 at free open ends, degree-2 continuity and d−1 KCL-safe modes at a degree-d T/Y/X junction. It reconstructs λ=−(1/jω)dI/ds directly on every mesh span. Since 5.20, rooftop self/near Galerkin terms are integrated with radius-aware composite quadrature on both observation and source spans, removing the strong fixed-4×4 quadrature aliasing seen in Zin. The hybrid wire+PEC solver uses the same improved rooftop wire-wire block plus integrated rooftop↔RWG mutual blocks. The formulation remains experimental: verify mesh convergence, reciprocity and critical results against an independent full-wave method."));
    momForm->addRow(QStringLiteral("Segments / λ"), m_segmentsPerWavelength);
    momForm->addRow(QStringLiteral("Max Δl / radius"), m_radiusMeshFactor);
    momForm->addRow(QStringLiteral("Junction local refine"), m_junctionLocalSubdivisions);
    momForm->addRow(QStringLiteral("Current / charge basis"), m_currentBasisTreatment);
    momForm->addRow(QStringLiteral("Pulse junction formulation"), m_junctionTreatment);
    momForm->addRow(QStringLiteral("Max unknowns"), m_maxMomUnknowns);
    momLayout->addLayout(momForm);
    auto *momSolve = new QPushButton(QStringLiteral("Solve geometry with MoM"), momBox);
    momLayout->addWidget(momSolve);
    m_momStatus = new QLabel(QStringLiteral("Not solved."), momBox);
    m_momStatus->setWordWrap(true);
    m_momFeedResults = new QLabel(momBox);
    m_momFeedResults->setWordWrap(true);
    m_momFeedResults->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_momMetrics = new QLabel(momBox);
    m_momMetrics->setWordWrap(true);
    m_momMetrics->setTextInteractionFlags(Qt::TextSelectableByMouse);
    momLayout->addWidget(m_momStatus);
    momLayout->addWidget(m_momFeedResults);
    momLayout->addWidget(m_momMetrics);

    auto *convergenceBox = new QGroupBox(QStringLiteral("Mesh convergence assistant"), momBox);
    auto *convergenceLayout = new QVBoxLayout(convergenceBox);
    auto *checkConvergenceButton = new QPushButton(QStringLiteral("Check 3-level mesh convergence"), convergenceBox);
    checkConvergenceButton->setToolTip(QStringLiteral("Runs three wire-only MoM solves without far-field post-processing: a coarser mesh, the current mesh and a finer mesh. The reported feed impedance is compared between levels. This is a numerical diagnostic, not an automatic proof of accuracy."));
    convergenceLayout->addWidget(checkConvergenceButton);
    m_momConvergenceTable = new QTableWidget(3, 9, convergenceBox);
    m_momConvergenceTable->setHorizontalHeaderLabels({QStringLiteral("Level"),QStringLiteral("Seg/λ"),QStringLiteral("Δl/r"),QStringLiteral("J refine"),QStringLiteral("Unknowns"),QStringLiteral("Zin"),QStringLiteral("ΔZ to finer"),QStringLiteral("max ΔI/Ipk"),QStringLiteral("KCL/Ipk")});
    m_momConvergenceTable->verticalHeader()->setVisible(false);
    m_momConvergenceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_momConvergenceTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_momConvergenceTable->setMaximumHeight(150);
    m_momConvergenceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    convergenceLayout->addWidget(m_momConvergenceTable);
    m_momConvergenceStatus = new QLabel(QStringLiteral("Not checked."), convergenceBox);
    m_momConvergenceStatus->setWordWrap(true);
    convergenceLayout->addWidget(m_momConvergenceStatus);
    momLayout->addWidget(convergenceBox);
    momPageLayout->addWidget(momBox);

    auto *surfaceMomBox = new QGroupBox(QStringLiteral("PEC surface EFIE / RWG"), momPage);
    surfaceMomBox->setToolTip(QStringLiteral("Educational surface-current EFIE using Rao-Wilton-Glisson basis functions on the triangulated PEC surfaces. Use this standalone block for plane-wave scattering or a direct RWG edge port. For a wire-fed antenna interacting with PEC surfaces, use the hybrid wire + PEC solver below."));
    auto *surfaceMomLayout = new QVBoxLayout(surfaceMomBox);
    auto *surfaceMomForm = new QFormLayout();
    m_surfaceExcitationMode = new QComboBox(surfaceMomBox);
    m_surfaceExcitationMode->addItems({QStringLiteral("Plane-wave scattering"), QStringLiteral("Lumped RWG edge port from feed")});
    m_surfaceExcitationMode->setToolTip(QStringLiteral("Plane wave: induced-current / RCS study. Lumped port: maps the selected geometry feed to the nearest interior RWG edge and estimates driven Zin/radiation. For a wire feed above/near PEC geometry, prefer the hybrid solver below."));
    m_surfacePortFeed = new QComboBox(surfaceMomBox);
    m_surfaceMaxTriangles = new QSpinBox(surfaceMomBox);
    m_surfaceMaxTriangles->setRange(20, 4000); m_surfaceMaxTriangles->setSingleStep(50); m_surfaceMaxTriangles->setValue(500);
    m_surfaceMaxTriangles->setToolTip(QStringLiteral("Maximum total triangle budget used by the RWG solve. The budget is shared among all surface elements; increase gradually because the dense matrix cost grows approximately as N³."));
    m_surfaceMaxUnknowns = new QSpinBox(surfaceMomBox);
    m_surfaceMaxUnknowns->setRange(20, 1200); m_surfaceMaxUnknowns->setSingleStep(25); m_surfaceMaxUnknowns->setValue(350);
    m_surfaceMaxUnknowns->setToolTip(QStringLiteral("Maximum number of interior-edge RWG basis functions. Debug builds can become slow above a few hundred unknowns."));
    m_surfaceIncidenceAzDeg = numberBox(surfaceMomBox, 0.0, -3600.0, 3600.0, 2);
    m_surfaceIncidenceElDeg = numberBox(surfaceMomBox, -90.0, -90.0, 90.0, 2);
    m_surfacePolarizationDeg = numberBox(surfaceMomBox, 0.0, -3600.0, 3600.0, 2);
    m_surfaceFieldVpm = numberBox(surfaceMomBox, 1.0, 1e-9, 1e9, 6);
    m_surfaceSelfRegularization = numberBox(surfaceMomBox, 0.22, 0.02, 1.0, 3);
    m_surfaceSelfRegularization->setEnabled(false);
    m_surfaceSelfRegularization->setToolTip(QStringLiteral("Legacy pre-5.23 equivalent-radius self factor, retained only for project compatibility. Since 5.23, same-triangle RWG terms use Duffy singularity extraction and adjacent triangles use localized composite quadrature, so this value no longer changes the matrix."));
    surfaceMomForm->addRow(QStringLiteral("Excitation"), m_surfaceExcitationMode);
    surfaceMomForm->addRow(QStringLiteral("Port feed"), m_surfacePortFeed);
    surfaceMomForm->addRow(QStringLiteral("Triangle budget"), m_surfaceMaxTriangles);
    surfaceMomForm->addRow(QStringLiteral("Max RWG unknowns"), m_surfaceMaxUnknowns);
    surfaceMomForm->addRow(QStringLiteral("Propagation azimuth (deg)"), m_surfaceIncidenceAzDeg);
    surfaceMomForm->addRow(QStringLiteral("Propagation elevation (deg)"), m_surfaceIncidenceElDeg);
    surfaceMomForm->addRow(QStringLiteral("Polarization rotation (deg)"), m_surfacePolarizationDeg);
    surfaceMomForm->addRow(QStringLiteral("Incident |E| (V/m)"), m_surfaceFieldVpm);
    surfaceMomForm->addRow(QStringLiteral("Legacy self factor (≤5.22)"), m_surfaceSelfRegularization);
    surfaceMomLayout->addLayout(surfaceMomForm);
    auto *surfaceMomSolve = new QPushButton(QStringLiteral("Solve PEC surfaces with RWG"), surfaceMomBox);
    surfaceMomLayout->addWidget(surfaceMomSolve);
    m_surfaceMomStatus = new QLabel(QStringLiteral("Not solved."), surfaceMomBox);
    m_surfaceMomStatus->setWordWrap(true);
    m_surfaceMomMetrics = new QLabel(surfaceMomBox);
    m_surfaceMomMetrics->setWordWrap(true);
    m_surfaceMomMetrics->setTextInteractionFlags(Qt::TextSelectableByMouse);
    surfaceMomLayout->addWidget(m_surfaceMomStatus);
    surfaceMomLayout->addWidget(m_surfaceMomMetrics);
    momPageLayout->addWidget(surfaceMomBox);

    auto *hybridBox = new QGroupBox(QStringLiteral("Hybrid wire + PEC block MoM"), momPage);
    hybridBox->setToolTip(QStringLiteral("Simultaneously solves pulse-basis wire currents and RWG surface currents in one dense block system. Use this for a driven dipole/loop/helix interacting with a ground plane, reflector, disk, cylinder or other triangulated PEC surface."));
    auto *hybridLayout = new QVBoxLayout(hybridBox);
    auto *hybridForm = new QFormLayout();
    m_hybridMaxUnknowns = new QSpinBox(hybridBox);
    m_hybridMaxUnknowns->setRange(50, 1200);
    m_hybridMaxUnknowns->setSingleStep(25);
    m_hybridMaxUnknowns->setValue(450);
    m_hybridMaxUnknowns->setToolTip(QStringLiteral("Maximum total dense unknowns = wire pulse unknowns + RWG surface unknowns. Runtime/memory grow rapidly; start below 300–450 in Debug builds."));
    m_hybridMutualRegularization = numberBox(hybridBox, 0.08, 0.005, 0.50, 3);
    m_hybridMutualRegularization->setToolTip(QStringLiteral("Equivalent-radius regularization used only for very-near wire↔surface mutual interactions. For separated wires and surfaces this has little effect. Verify convergence if a wire approaches a PEC sheet."));
    m_hybridPortMode = new QComboBox(hybridBox);
    m_hybridPortMode->addItems({QStringLiteral("All feeds = interior delta gaps"),
                                QStringLiteral("Selected feed = PEC-referenced wire endpoint"),
                                QStringLiteral("Differential PEC surface lumped port (patch)")});
    m_hybridPortMode->setToolTip(QStringLiteral("Endpoint mode uses an explicit thin-wire terminal. Differential surface mode applies an ideal lumped voltage directly between the two selected PEC terminal patches using RWG-divergence stencils; explicit wires are intentionally excluded from that solve, which is useful for patch/microstrip studies where probe self reactance would otherwise dominate."));
    m_hybridPortFeed = new QComboBox(hybridBox);m_hybridPortFeed->setEnabled(false);
    m_hybridPortSurface = new QComboBox(hybridBox);m_hybridPortSurface->setEnabled(false);
    m_hybridJunctionFeed = new QComboBox(hybridBox);m_hybridJunctionFeed->setEnabled(false);
    m_hybridJunctionSurface = new QComboBox(hybridBox);m_hybridJunctionSurface->setEnabled(false);
    m_hybridMappingToleranceMm = numberBox(hybridBox, 10.0, 0.001, 1e6, 4);
    m_hybridMappingToleranceMm->setToolTip(QStringLiteral("Maximum 3D mapping distance for the endpoint/junction marker to the requested PEC surface. Reduce it after coarse-mesh prototyping; a large value can map to the wrong sheet."));
    m_hybridDifferentialPortRadiusMm = numberBox(hybridBox, 0.0, 0.0, 1e6, 4);
    m_hybridDifferentialPortRadiusMm->setSpecialValueText(QStringLiteral("Auto (mesh-local)"));
    m_hybridDifferentialPortRadiusMm->setSuffix(QStringLiteral(" mm"));
    m_hybridDifferentialPortRadiusMm->setEnabled(false);
    m_hybridDifferentialPortRadiusMm->setToolTip(QStringLiteral("1-sigma radius of the smooth Gaussian potential footprint used by the differential PEC port. A fixed physical radius is strongly recommended for mesh-convergence studies. 0 keeps the mesh-local automatic footprint for compatibility."));
    m_hybridUseDielectric = new QCheckBox(QStringLiteral("Use dielectric substrate regions in hybrid kernels"), hybridBox);
    m_hybridUseDielectric->setChecked(true);
    m_hybridUseDielectric->setToolTip(QStringLiteral("Applies finite dielectric regions to the hybrid kernels. The first two choices are effective-medium models. The 5.25 layered option adds a quasi-static finite-slab image/fringing correction. The 5.26 Sommerfeld option evaluates the kρ-dependent TM scalar-potential spectrum with a passivity-preserving reactive projection. The 5.29 mode adds the guarded same-face HED TE vector-potential correction. The 5.30 mode additionally activates the residual transmitted TE/TM tangential dyadic between opposite slab faces and enforces reciprocal symmetry of the RWG block. The 5.31 mode replaces the TM-only scalar transition with the coupled HED TE/TM longitudinal scalar reflection/transmission spectrum on slab faces. The 5.32 mode propagates the spectrum into the exterior air half-spaces. The 5.33 mode adds the internal dielectric cavity residual for tangential wire↔RWG interactions and activates the matching tangential HED scalar-gradient correction under the same reactive/passivity guard. The 5.34 mode adds a guarded TM VED/normal-current transition for vertical vias/probes, including the VED scalar-gradient path and the reciprocal full HED scalar gradient. The 5.35 mode activates the residual TM rho-z / z-rho mixed vector components using signed cavity derivatives and a J1 Sommerfeld transform, while keeping the same reactive and reciprocal guards. The 5.36 mode attempts a full-complex HED/VED wire↔RWG residual and automatically falls back to the audited 5.35 reactive operator if the driven passive-port check fails. The 5.37 mode adds the first propagating-spectrum TE/TM far-field audit, separates upper/lower exterior radiation and adds a one-sided radiated-power guard. The 5.38 mode locates/refines TE/TM guided-pole candidates of that same finite-slab spectrum. The 5.39 mode adds an air/dielectric/PEC grounded-slab modal audit, checks explicit RWG backing-plane coverage, and reports unit-interface-field modal power normalization; excitation power remains guarded until finite-ground current-to-mode overlap is validated."));
    m_hybridDielectricKernel = new QComboBox(hybridBox);
    m_hybridDielectricKernel->addItems({QStringLiteral("Legacy midpoint fill (4.4)"),
                                        QStringLiteral("Segment-overlap weighted"),
                                        QStringLiteral("Layered slab image / fringing (quasi-static)"),
                                        QStringLiteral("Sommerfeld TM scalar (dynamic, 5.26)"),
                                        QStringLiteral("Sommerfeld TE vector + guarded scalar (5.29)"),
                                        QStringLiteral("Sommerfeld transmitted TE/TM + reciprocity guard (5.30)"),
                                        QStringLiteral("Sommerfeld HED longitudinal scalar + transmitted dyadic (5.31)"),
                                        QStringLiteral("Sommerfeld exterior-height wire/RWG transition (5.32)"),
                                        QStringLiteral("Sommerfeld internal-layer + scalar-gradient transition (5.33)"),
                                        QStringLiteral("Sommerfeld VED / via normal-current transition (5.34)"),
                                        QStringLiteral("Sommerfeld VED off-diagonal rho-z / z-rho (5.35)"),
                                        QStringLiteral("Sommerfeld complex wire/RWG + passive-port guard (5.36)"),
                                        QStringLiteral("Sommerfeld propagating far-field + power guard (5.37)"),
                                        QStringLiteral("Sommerfeld guided-pole audit + guarded surface-wave power (5.38)"),
                                        QStringLiteral("Sommerfeld grounded-PEC modal audit + normalization (5.39)")});
    m_hybridDielectricKernel->setCurrentIndex(1);
    m_hybridDielectricKernel->setToolTip(QStringLiteral("Midpoint fill reproduces the 4.4 behavior. Segment-overlap weighting uses the fraction of the source-observer path inside each slab. The 5.25 layered mode adds the finite electrostatic image/fringing series. The 5.26 mode adds the dynamic TM scalar spectrum with a reactive guard. The 5.29/5.30/5.31 modes progressively add same-face TE vector, transmitted TE/TM vector, and coupled HED longitudinal scalar terms. The 5.32 mode propagates the spectrum into the exterior half-spaces. The 5.33 mode adds the multiple-reflection internal-layer residual for tangential sources/observers and the tangential HED scalar-gradient mutual term. The 5.34 mode additionally activates the guarded TM VED scalar-gradient/normal-vector transition for predominantly normal internal wires and the reciprocal full HED scalar gradient. The 5.35 mode adds the residual TM mixed rho-z / z-rho vector components for via↔tangential-surface coupling. The 5.36 mode retains the full complex wire↔RWG HED/VED residual only when the passive-port monitor remains positive; otherwise it transparently returns the 5.35 reactive solution. The 5.37 mode post-processes the solved coherent moment with finite-slab TE/TM propagating reflection/transmission, reports upper/lower exterior radiation and rejects a complex candidate if propagating radiation plus conductor loss exceeds accepted power. The 5.38 mode additionally scans the bare-slab TE/TM poles. The 5.39 mode uses a grounded air/dielectric/PEC dispersion relation when RWG coverage is detected on the slab back face and reports a W/m modal normalization. Surface-wave excitation power is still guarded rather than inferred from the closure residual. Explicit PEC sheets remain in the MoM matrix; no extra PEC image plane is added."));
    m_hybridFiniteConductivity = new QCheckBox(QStringLiteral("Finite conductor conductivity / thickness"), hybridBox);
    m_hybridFiniteConductivity->setChecked(false);
    m_hybridFiniteConductivity->setToolTip(QStringLiteral("Adds a local surface-impedance boundary term to the RWG matrix. Geometry remains a zero-thickness sheet, but copper loss and finite skin-depth effects can be estimated."));
    m_hybridSurfaceConductivityMSm = numberBox(hybridBox,58.0,0.001,1000.0,4);
    m_hybridSurfaceConductivityMSm->setSuffix(QStringLiteral(" MS/m"));
    m_hybridSurfaceConductivityMSm->setToolTip(QStringLiteral("Bulk conductivity. Copper is approximately 58 MS/m near room temperature."));
    m_hybridSurfaceThicknessUm = numberBox(hybridBox,35.0,0.001,1e6,3);
    m_hybridSurfaceThicknessUm->setSuffix(QStringLiteral(" µm"));
    m_hybridSurfaceThicknessUm->setToolTip(QStringLiteral("Physical metal thickness used by the finite-thickness sheet-impedance model."));
    m_hybridTerminalHalfRwg = new QCheckBox(QStringLiteral("Enable terminal-only half-RWG boundary functions"), hybridBox);
    m_hybridTerminalHalfRwg->setChecked(true);
    m_hybridTerminalHalfRwg->setToolTip(QStringLiteral("Creates half-RWG functions only on boundary edges requested by a mapped feed or galvanic junction. Other open boundaries remain unchanged."));
    m_hybridPortReferenceModel = new QComboBox(hybridBox);
    m_hybridPortReferenceModel->addItems({QStringLiteral("Antenna terminal / ground plane"),QStringLiteral("External coax connector reference plane")});
    m_hybridPortReferenceModel->setToolTip(QStringLiteral("Optional post-solve reference-plane embedding for the selected PEC-referenced endpoint port. The external coax is modeled as a TEM transmission line; it does not replace the MoM field solution at the antenna terminal."));
    m_hybridCoaxInnerRadiusMm = numberBox(hybridBox,0.50,0.001,1000.0,5);
    m_hybridCoaxOuterRadiusMm = numberBox(hybridBox,1.70,0.002,10000.0,5);
    m_hybridCoaxEr = numberBox(hybridBox,2.10,1.0,100.0,5);
    m_hybridCoaxTanD = numberBox(hybridBox,0.0002,0.0,1.0,7);
    m_hybridCoaxLengthMm = numberBox(hybridBox,0.0,0.0,1e6,4);
    m_hybridCoaxLengthMm->setToolTip(QStringLiteral("Electrical reference-plane distance below/outside the modeled antenna terminal. Set 0 to disable the transform even if the coax model is selected."));
    for (auto *w : {m_hybridCoaxInnerRadiusMm,m_hybridCoaxOuterRadiusMm,m_hybridCoaxEr,m_hybridCoaxTanD,m_hybridCoaxLengthMm}) w->setEnabled(false);
    hybridForm->addRow(QStringLiteral("Max total unknowns"), m_hybridMaxUnknowns);
    hybridForm->addRow(QStringLiteral("Mutual regularization"), m_hybridMutualRegularization);
    hybridForm->addRow(QStringLiteral("Feed interpretation"), m_hybridPortMode);
    hybridForm->addRow(QStringLiteral("Driven / negative terminal marker"), m_hybridPortFeed);
    hybridForm->addRow(QStringLiteral("Return / negative PEC"), m_hybridPortSurface);
    hybridForm->addRow(QStringLiteral("Positive terminal / extra junction marker"), m_hybridJunctionFeed);
    hybridForm->addRow(QStringLiteral("Positive / junction PEC"), m_hybridJunctionSurface);
    hybridForm->addRow(QStringLiteral("Mapping tolerance (mm)"), m_hybridMappingToleranceMm);
    hybridForm->addRow(QStringLiteral("Differential port σ"), m_hybridDifferentialPortRadiusMm);
    hybridForm->addRow(m_hybridUseDielectric);
    hybridForm->addRow(QStringLiteral("Dielectric interaction kernel"), m_hybridDielectricKernel);
    hybridForm->addRow(m_hybridTerminalHalfRwg);
    hybridForm->addRow(m_hybridFiniteConductivity);
    hybridForm->addRow(QStringLiteral("Surface conductivity"), m_hybridSurfaceConductivityMSm);
    hybridForm->addRow(QStringLiteral("Surface thickness"), m_hybridSurfaceThicknessUm);
    hybridForm->addRow(QStringLiteral("Port reference plane"), m_hybridPortReferenceModel);
    hybridForm->addRow(QStringLiteral("Coax inner radius a (mm)"), m_hybridCoaxInnerRadiusMm);
    hybridForm->addRow(QStringLiteral("Coax outer radius b (mm)"), m_hybridCoaxOuterRadiusMm);
    hybridForm->addRow(QStringLiteral("Coax dielectric εr"), m_hybridCoaxEr);
    hybridForm->addRow(QStringLiteral("Coax dielectric tanδ"), m_hybridCoaxTanD);
    hybridForm->addRow(QStringLiteral("Coax reference length (mm)"), m_hybridCoaxLengthMm);
    hybridLayout->addLayout(hybridForm);
    auto *junctionHint = new QLabel(QStringLiteral("Patch workflow: the differential PEC surface port is recommended for ideal lumped excitation. Select the ground-side marker/PEC as the negative terminal and the patch-side marker/PEC as the positive terminal. The explicit wire-probe endpoint mode remains available when probe/via current itself is part of the study. Always verify surface-mesh and dielectric-kernel convergence."), hybridBox);
    junctionHint->setWordWrap(true);
    junctionHint->setToolTip(QStringLiteral("Differential surface mode projects an ideal voltage jump through localized RWG divergence stencils and extracts current with the same dual stencil. It excludes explicit wires from that solve, avoiding short-probe self reactance. This is still an ideal lumped-port abstraction, not an exact coax aperture or layered-medium solution."));
    hybridLayout->addWidget(junctionHint);
    auto *hybridSolve = new QPushButton(QStringLiteral("Solve coupled wire + PEC geometry"), hybridBox);
    hybridLayout->addWidget(hybridSolve);
    m_hybridMomStatus = new QLabel(QStringLiteral("Not solved."), hybridBox);
    m_hybridMomStatus->setWordWrap(true);
    m_hybridMomMetrics = new QLabel(hybridBox);
    m_hybridMomMetrics->setWordWrap(true);
    m_hybridMomMetrics->setTextInteractionFlags(Qt::TextSelectableByMouse);
    hybridLayout->addWidget(m_hybridMomStatus);
    hybridLayout->addWidget(m_hybridMomMetrics);
    momPageLayout->addWidget(hybridBox);
    momPageLayout->addStretch(1);

    auto *validationBox = new QGroupBox(QStringLiteral("Canonical physical validation bench"), validationPage);
    auto *validationLayout = new QVBoxLayout(validationBox);
    auto *validationControls = new QHBoxLayout();
    m_validationCase = new QComboBox(validationBox);
    m_validationCase->addItems({QStringLiteral("Thin half-wave dipole"),
                                QStringLiteral("Electrically small circular loop"),
                                QStringLiteral("Quarter-wave monopole + finite PEC ground"),
                                QStringLiteral("Rectangular patch engineering benchmark")});
    m_validationCase->setToolTip(QStringLiteral("Runs a canonical geometry in memory. Your current Designer geometry is never replaced. The frequency comes from Presets → Design frequency."));
    auto *runValidationCase = new QPushButton(QStringLiteral("Run selected benchmark"), validationBox);
    auto *runValidationAll = new QPushButton(QStringLiteral("Run full campaign"), validationBox);
    validationControls->addWidget(new QLabel(QStringLiteral("Case"), validationBox));
    validationControls->addWidget(m_validationCase, 1);
    validationControls->addWidget(runValidationCase);
    validationControls->addWidget(runValidationAll);
    validationLayout->addLayout(validationControls);
    m_validationTable = new QTableWidget(0, 10, validationBox);
    m_validationTable->setHorizontalHeaderLabels({QStringLiteral("Case"),QStringLiteral("Solver"),QStringLiteral("DOF cur→fine"),QStringLiteral("Fine Zin"),QStringLiteral("Mesh ΔZ"),QStringLiteral("Dmax"),QStringLiteral("ΔD ref"),QStringLiteral("Pattern RMS"),QStringLiteral("Primary reference"),QStringLiteral("Verdict")});
    m_validationTable->verticalHeader()->setVisible(false);
    m_validationTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_validationTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_validationTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_validationTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_validationTable->horizontalHeader()->setStretchLastSection(true);
    m_validationTable->setMinimumHeight(210);
    validationLayout->addWidget(m_validationTable);
    m_validationStatus = new QLabel(QStringLiteral("Not run. Canonical benchmarks are independent of the current antenna geometry."), validationBox);
    m_validationStatus->setWordWrap(true);
    m_validationStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_validationStatus->setToolTip(QStringLiteral("The existing FDTD workspace is 2D TMz/TEz and is intentionally not treated as a numerical truth reference for 3D antennas. A 3D FDTD/FEM/NEC comparison would be a different backend."));
    validationLayout->addWidget(m_validationStatus);
    auto *validationNote = new QLabel(QStringLiteral("Method: each MoM basis is solved on a current and finer mesh, then compared with canonical references where meaningful. PASS/WARNING/FAIL is deliberately conservative. A stable mesh alone is not proof of physical accuracy."), validationBox);
    validationNote->setWordWrap(true);
    validationLayout->addWidget(validationNote);
    validationPageLayout->addWidget(validationBox);
    validationPageLayout->addStretch(1);

    auto *sweepBox = new QGroupBox(QStringLiteral("Antenna frequency sweep"), sweepPage);
    sweepBox->setToolTip(QStringLiteral("Auto uses the full Hybrid wire + PEC/RWG + dielectric/Sommerfeld solver whenever PEC/dielectric geometry is present, and the generalized thin-wire MoM for wire-only antennas. Far-field post-processing is skipped during matching sweeps."));
    auto *sweepLayout = new QVBoxLayout(sweepBox);
    auto *sweepForm = new QFormLayout();
    m_sweepStartMHz = numberBox(sweepBox, 70.0, 0.001, 1e6, 6);
    m_sweepStopMHz = numberBox(sweepBox, 130.0, 0.001, 1e6, 6);
    m_sweepPoints = new QSpinBox(sweepBox);
    m_sweepPoints->setRange(3, 101);
    m_sweepPoints->setValue(11);
    m_sweepPoints->setToolTip(QStringLiteral("Dense MoM is solved once per frequency. Start with 7–15 points, then refine around resonance."));
    m_sweepFeed = new QComboBox(sweepBox);
    m_sweepExcitationMode = new QComboBox(sweepBox);
    m_sweepExcitationMode->addItems({QStringLiteral("All configured feeds — preserve amplitudes/phases"),
                                     QStringLiteral("Observed feed only — zero other impressed voltages")});
    m_sweepExcitationMode->setCurrentIndex(0);
    m_sweepExcitationMode->setToolTip(QStringLiteral("All configured feeds reproduces the single-frequency simultaneous-drive solution. Observed-feed-only is useful for diagnostic single-source sweeps; other feed markers remain part of the conductor geometry and are not replaced by matched terminations, so a multi-port S-parameter matrix is not implied."));
    m_sweepSolver = new QComboBox(sweepBox);
    m_sweepSolver->addItems({QStringLiteral("Auto — full geometry when needed"),
                             QStringLiteral("Hybrid wire + PEC/RWG + dielectric"),
                             QStringLiteral("Thin-wire MoM (legacy / fast)")});
    m_sweepSolver->setCurrentIndex(0);
    m_sweepSolver->setToolTip(QStringLiteral("Auto is the Simulation/Designer 1.0 path. Hybrid reuses the same port, mesh, conductivity, dielectric and Sommerfeld settings as the single-frequency Hybrid solve. Thin-wire deliberately ignores PEC/dielectric geometry."));
    sweepForm->addRow(QStringLiteral("Solver"), m_sweepSolver);
    sweepForm->addRow(QStringLiteral("Start (MHz)"), m_sweepStartMHz);
    sweepForm->addRow(QStringLiteral("Stop (MHz)"), m_sweepStopMHz);
    sweepForm->addRow(QStringLiteral("Points"), m_sweepPoints);
    sweepForm->addRow(QStringLiteral("Reported feed"), m_sweepFeed);
    sweepForm->addRow(QStringLiteral("Excitation mode"), m_sweepExcitationMode);
    sweepLayout->addLayout(sweepForm);
    auto *sweepButtons = new QHBoxLayout();
    auto *centerSweep = new QPushButton(QStringLiteral("±30% around design f"), sweepBox);
    auto *runSweep = new QPushButton(QStringLiteral("Run sweep"), sweepBox);
    sweepButtons->addWidget(centerSweep);
    sweepButtons->addWidget(runSweep);
    sweepLayout->addLayout(sweepButtons);
    m_sweepStatus = new QLabel(QStringLiteral("No sweep calculated."), sweepBox);
    m_sweepStatus->setWordWrap(true);
    m_sweepSummary = new QLabel(sweepBox);
    m_sweepSummary->setWordWrap(true);
    m_sweepSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sweepLayout->addWidget(m_sweepStatus);
    sweepLayout->addWidget(m_sweepSummary);
    sweepPageLayout->addWidget(sweepBox);
    sweepPageLayout->addStretch(1);

    auto *optimizerTabs = new QTabWidget(optPage);
    auto *opt1dPage = new QWidget(optimizerTabs);
    auto *opt1dPageLayout = new QVBoxLayout(opt1dPage);
    opt1dPageLayout->setContentsMargins(2, 2, 2, 2);
    auto *optBox = new QGroupBox(QStringLiteral("1D MoM geometry optimizer"), opt1dPage);
    optBox->setToolTip(QStringLiteral("Educational one-variable optimizer. Each candidate geometry is re-meshed and re-solved with the generalized MoM at the target frequency; far-field post-processing is skipped during the search."));
    auto *optLayout = new QVBoxLayout(optBox);
    auto *optForm = new QFormLayout();
    m_optTargetMHz = numberBox(optBox, 100.0, 0.001, 1e6, 6);
    m_optFeed = new QComboBox(optBox);
    m_optVariable = new QComboBox(optBox);
    m_optVariable->addItems({QStringLiteral("Uniform XY dimension scale"),
                             QStringLiteral("Driven connected conductor scale"),
                             QStringLiteral("Parasitic component spacing scale")});
    m_optVariable->setToolTip(QStringLiteral("Uniform: scales all node coordinates about the observed feed. Driven: scales only the conductor component carrying that feed. Parasitic spacing: translates disconnected parasitic components radially relative to that feed while preserving their lengths."));
    m_optObjective = new QComboBox(optBox);
    m_optObjective->addItems({QStringLiteral("Minimize |Γ| (best match)"),
                              QStringLiteral("Minimize |Im(Zin)| / Z0 (resonance)")});
    m_optMinFactor = numberBox(optBox, 0.75, 0.10, 3.0, 5);
    m_optMaxFactor = numberBox(optBox, 1.25, 0.10, 3.0, 5);
    m_optCoarseSamples = new QSpinBox(optBox);
    m_optCoarseSamples->setRange(5, 31); m_optCoarseSamples->setSingleStep(2); m_optCoarseSamples->setValue(7);
    m_optRefineIterations = new QSpinBox(optBox);
    m_optRefineIterations->setRange(0, 16); m_optRefineIterations->setValue(6);
    optForm->addRow(QStringLiteral("Target frequency (MHz)"), m_optTargetMHz);
    optForm->addRow(QStringLiteral("Observe feed"), m_optFeed);
    optForm->addRow(QStringLiteral("Geometry variable"), m_optVariable);
    optForm->addRow(QStringLiteral("Objective"), m_optObjective);
    optForm->addRow(QStringLiteral("Minimum factor"), m_optMinFactor);
    optForm->addRow(QStringLiteral("Maximum factor"), m_optMaxFactor);
    optForm->addRow(QStringLiteral("Coarse samples"), m_optCoarseSamples);
    optForm->addRow(QStringLiteral("Golden refinements"), m_optRefineIterations);
    optLayout->addLayout(optForm);
    auto *optButtons = new QHBoxLayout();
    auto *useDesignFrequency = new QPushButton(QStringLiteral("Use design f"), optBox);
    auto *runOptimization = new QPushButton(QStringLiteral("Optimize"), optBox);
    m_applyOptimized = new QPushButton(QStringLiteral("Apply best geometry"), optBox);
    m_applyOptimized->setEnabled(false);
    optButtons->addWidget(useDesignFrequency);
    optButtons->addWidget(runOptimization);
    optButtons->addWidget(m_applyOptimized);
    optLayout->addLayout(optButtons);
    m_optStatus = new QLabel(QStringLiteral("No optimization calculated."), optBox);
    m_optStatus->setWordWrap(true);
    m_optSummary = new QLabel(optBox);
    m_optSummary->setWordWrap(true);
    m_optSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    optLayout->addWidget(m_optStatus);
    optLayout->addWidget(m_optSummary);
    opt1dPageLayout->addWidget(optBox);
    opt1dPageLayout->addStretch(1);
    optimizerTabs->addTab(opt1dPage, QStringLiteral("1D"));

    auto *multiPage = new QWidget(optimizerTabs);
    auto *multiPageLayout = new QVBoxLayout(multiPage);
    multiPageLayout->setContentsMargins(2, 2, 2, 2);
    auto *multiScroll = new QScrollArea(multiPage);
    multiScroll->setWidgetResizable(true);
    multiScroll->setFrameShape(QFrame::NoFrame);
    auto *multiContent = new QWidget(multiScroll);
    auto *multiContentLayout = new QVBoxLayout(multiContent);
    multiContentLayout->setContentsMargins(2, 2, 2, 2);
    auto *multiBox = new QGroupBox(QStringLiteral("Yagi-array multi-parameter optimizer"), multiContent);
    multiBox->setToolTip(QStringLiteral("Coordinate-search optimizer for one driven component, one reflector and one or more directors. The longest parasitic component is used as the reflector; the remaining parasitics must lie in the forward boom direction and are treated as directors. Every candidate is fully re-meshed and re-solved with MoM. Radiation objectives use a deliberately coarser angular grid during the search; validate the staged result with the normal full-resolution MoM solve."));
    auto *multiLayout = new QVBoxLayout(multiBox);
    auto *multiForm = new QFormLayout();
    m_multiTargetMHz = numberBox(multiBox, 100.0, 0.001, 1e6, 6);
    m_multiFeed = new QComboBox(multiBox);
    m_multiMinFactor = numberBox(multiBox, 0.80, 0.20, 2.0, 4);
    m_multiMaxFactor = numberBox(multiBox, 1.20, 0.20, 2.0, 4);
    m_multiSamplesPerVariable = new QSpinBox(multiBox);
    m_multiSamplesPerVariable->setRange(3, 9); m_multiSamplesPerVariable->setSingleStep(2); m_multiSamplesPerVariable->setValue(5);
    m_multiPasses = new QSpinBox(multiBox);
    m_multiPasses->setRange(1, 6); m_multiPasses->setValue(2);
    multiForm->addRow(QStringLiteral("Target frequency (MHz)"), m_multiTargetMHz);
    multiForm->addRow(QStringLiteral("Observe feed"), m_multiFeed);
    multiForm->addRow(QStringLiteral("Common min factor"), m_multiMinFactor);
    multiForm->addRow(QStringLiteral("Common max factor"), m_multiMaxFactor);
    multiForm->addRow(QStringLiteral("Samples / variable"), m_multiSamplesPerVariable);
    multiForm->addRow(QStringLiteral("Coordinate passes"), m_multiPasses);
    multiLayout->addLayout(multiForm);

    auto *variableBox = new QGroupBox(QStringLiteral("Optimized geometry variables"), multiBox);
    auto *variableLayout = new QVBoxLayout(variableBox);
    m_multiDrivenLength = new QCheckBox(QStringLiteral("Driven-element length"), variableBox); m_multiDrivenLength->setChecked(true);
    m_multiReflectorLength = new QCheckBox(QStringLiteral("Reflector length"), variableBox); m_multiReflectorLength->setChecked(true);
    m_multiDirectorLength = new QCheckBox(QStringLiteral("All director lengths"), variableBox); m_multiDirectorLength->setChecked(true);
    m_multiReflectorSpacing = new QCheckBox(QStringLiteral("Reflector spacing"), variableBox); m_multiReflectorSpacing->setChecked(true);
    m_multiDirectorSpacing = new QCheckBox(QStringLiteral("Director boom positions"), variableBox); m_multiDirectorSpacing->setChecked(true);
    variableLayout->addWidget(m_multiDrivenLength);
    variableLayout->addWidget(m_multiReflectorLength);
    variableLayout->addWidget(m_multiDirectorLength);
    variableLayout->addWidget(m_multiReflectorSpacing);
    variableLayout->addWidget(m_multiDirectorSpacing);
    multiLayout->addWidget(variableBox);

    auto *weightBox = new QGroupBox(QStringLiteral("Multi-objective weights"), multiBox);
    weightBox->setToolTip(QStringLiteral("The scalar cost is the weighted mean of dimensionless penalties: center-frequency |Γ|, 1/D, 1/(front/back power ratio), and an optional multi-frequency matching penalty across the requested fractional band."));
    auto *weightForm = new QFormLayout(weightBox);
    m_multiWeightMatch = numberBox(weightBox, 1.0, 0.0, 100.0, 3);
    m_multiWeightDirectivity = numberBox(weightBox, 0.20, 0.0, 100.0, 3);
    m_multiWeightFrontBack = numberBox(weightBox, 0.20, 0.0, 100.0, 3);
    m_multiWeightBandwidth = numberBox(weightBox, 0.0, 0.0, 100.0, 3);
    m_multiBandwidthHalfSpanPct = numberBox(weightBox, 5.0, 0.1, 40.0, 2);
    m_multiBandwidthSamples = new QSpinBox(weightBox);
    m_multiBandwidthSamples->setRange(3, 9);
    m_multiBandwidthSamples->setSingleStep(1);
    m_multiBandwidthSamples->setValue(5);
    m_multiBandwidthAggregation = new QComboBox(weightBox);
    m_multiBandwidthAggregation->addItems({QStringLiteral("Mean |Γ| over band"), QStringLiteral("Worst-case |Γ| over band")});
    weightForm->addRow(QStringLiteral("Match weight |Γ|"), m_multiWeightMatch);
    weightForm->addRow(QStringLiteral("Directivity weight 1/D"), m_multiWeightDirectivity);
    weightForm->addRow(QStringLiteral("Front/back weight 1/FBR"), m_multiWeightFrontBack);
    weightForm->addRow(QStringLiteral("Band-match weight"), m_multiWeightBandwidth);
    weightForm->addRow(QStringLiteral("Band half-span (%)"), m_multiBandwidthHalfSpanPct);
    weightForm->addRow(QStringLiteral("Band frequency samples"), m_multiBandwidthSamples);
    weightForm->addRow(QStringLiteral("Band aggregation"), m_multiBandwidthAggregation);
    multiLayout->addWidget(weightBox);

    auto *multiConstraintBox = new QGroupBox(QStringLiteral("Geometric constraints"), multiBox);
    multiConstraintBox->setToolTip(QStringLiteral("Candidate geometries are rejected before the MoM solve when adjacent element center spacing is too small or the overall boom length is too large. The constraints are normalized to wavelength at the target frequency."));
    auto *multiConstraintForm = new QFormLayout(multiConstraintBox);
    m_multiMinElementSpacingLambda = numberBox(multiConstraintBox, 0.05, 0.0, 2.0, 4);
    m_multiMaxBoomLengthLambda = numberBox(multiConstraintBox, 4.0, 0.0, 20.0, 4);
    m_multiMaxBoomLengthLambda->setToolTip(QStringLiteral("Set to 0 to disable the maximum boom-length constraint."));
    multiConstraintForm->addRow(QStringLiteral("Min adjacent spacing / λ"), m_multiMinElementSpacingLambda);
    multiConstraintForm->addRow(QStringLiteral("Max boom length / λ"), m_multiMaxBoomLengthLambda);
    multiLayout->addWidget(multiConstraintBox);

    auto *individualBox = new QGroupBox(QStringLiteral("Per-director refinement"), multiBox);
    individualBox->setToolTip(QStringLiteral("Refines each director length and boom position independently. The common target frequency, MoM mesh, objective weights and geometric constraints above are reused. Each director can be fixed or given its own factor interval."));
    auto *individualLayout = new QVBoxLayout(individualBox);
    auto *individualNote = new QLabel(QStringLiteral("Use the grouped optimizer for a fast first pass, then refresh this table and enable only the director dimensions that should be refined independently."), individualBox);
    individualNote->setWordWrap(true);
    individualLayout->addWidget(individualNote);
    m_individualDirectorTable = new QTableWidget(0, 7, individualBox);
    m_individualDirectorTable->setHorizontalHeaderLabels({QStringLiteral("Director"), QStringLiteral("L free"), QStringLiteral("L min"), QStringLiteral("L max"), QStringLiteral("Pos free"), QStringLiteral("Pos min"), QStringLiteral("Pos max")});
    m_individualDirectorTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_individualDirectorTable->verticalHeader()->setVisible(false);
    m_individualDirectorTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_individualDirectorTable->setMinimumHeight(150);
    individualLayout->addWidget(m_individualDirectorTable);
    auto *individualButtons = new QHBoxLayout();
    auto *refreshIndividual = new QPushButton(QStringLiteral("Detect / refresh directors"), individualBox);
    auto *runIndividualOptimization = new QPushButton(QStringLiteral("Optimize individual directors"), individualBox);
    m_applyIndividualOptimized = new QPushButton(QStringLiteral("Apply individual best"), individualBox);
    m_applyIndividualOptimized->setEnabled(false);
    individualButtons->addWidget(refreshIndividual);
    individualButtons->addWidget(runIndividualOptimization);
    individualButtons->addWidget(m_applyIndividualOptimized);
    individualLayout->addLayout(individualButtons);
    m_individualStatus = new QLabel(QStringLiteral("No individual-director optimization calculated."), individualBox);
    m_individualStatus->setWordWrap(true);
    m_individualSummary = new QLabel(individualBox);
    m_individualSummary->setWordWrap(true);
    m_individualSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    individualLayout->addWidget(m_individualStatus);
    individualLayout->addWidget(m_individualSummary);
    multiLayout->addWidget(individualBox);

    auto *multiButtons = new QHBoxLayout();
    auto *multiUseDesignFrequency = new QPushButton(QStringLiteral("Use design f"), multiBox);
    auto *runMultiOptimization = new QPushButton(QStringLiteral("Optimize Yagi array"), multiBox);
    m_applyMultiOptimized = new QPushButton(QStringLiteral("Apply best geometry"), multiBox);
    m_applyMultiOptimized->setEnabled(false);
    multiButtons->addWidget(multiUseDesignFrequency);
    multiButtons->addWidget(runMultiOptimization);
    multiButtons->addWidget(m_applyMultiOptimized);
    multiLayout->addLayout(multiButtons);
    m_multiStatus = new QLabel(QStringLiteral("No multi-parameter optimization calculated."), multiBox);
    m_multiStatus->setWordWrap(true);
    m_multiSummary = new QLabel(multiBox);
    m_multiSummary->setWordWrap(true);
    m_multiSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    multiLayout->addWidget(m_multiStatus);
    multiLayout->addWidget(m_multiSummary);
    multiContentLayout->addWidget(multiBox);
    multiContentLayout->addStretch(1);
    multiScroll->setWidget(multiContent);
    multiPageLayout->addWidget(multiScroll, 1);
    optimizerTabs->addTab(multiPage, QStringLiteral("Yagi array"));

    optPageLayout->addWidget(optimizerTabs, 1);

    m_resultTabs = new QTabWidget(this);
    auto *geometryTab = new QWidget(this);
    auto *geometryLayout = new QVBoxLayout(geometryTab);
    geometryLayout->setContentsMargins(0, 0, 0, 0);
    auto *geometryTableTabs = new QTabWidget(geometryTab);
    auto *wireFeedPage = new QWidget(geometryTableTabs);
    auto *wireFeedLayout = new QVBoxLayout(wireFeedPage);
    wireFeedLayout->setContentsMargins(0,0,0,0);
    auto *tables = new QSplitter(Qt::Horizontal, wireFeedPage);
    m_wireTable = new QTableWidget(0, 9, tables);
    m_wireTable->setHorizontalHeaderLabels({QStringLiteral("Wire"),QStringLiteral("x1 m"),QStringLiteral("y1 m"),QStringLiteral("z1 m"),QStringLiteral("x2 m"),QStringLiteral("y2 m"),QStringLiteral("z2 m"),QStringLiteral("Length m"),QStringLiteral("Radius mm")});
    m_wireTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_wireTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_wireTable->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::SelectedClicked|QAbstractItemView::EditKeyPressed);
    m_wireTable->setToolTip(QStringLiteral("Double-click numerical cells to edit the antenna exactly. Editing a shared endpoint propagates that node to every connected wire/feed. Length is calculated automatically."));
    m_feedTable = new QTableWidget(0, 7, tables);
    m_feedTable->setHorizontalHeaderLabels({QStringLiteral("Feed"),QStringLiteral("x m"),QStringLiteral("y m"),QStringLiteral("z m"),QStringLiteral("V"),QStringLiteral("Phase °"),QStringLiteral("Zref Ω")});
    m_feedTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_feedTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_feedTable->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::SelectedClicked|QAbstractItemView::EditKeyPressed);
    m_feedTable->setToolTip(QStringLiteral("Double-click to edit feed name and exact XYZ position. Excitation magnitude/phase/Zref are mirrored here for convenience and are also available in Simulation → Feeds / ports."));
    tables->setSizes({760, 560});
    wireFeedLayout->addWidget(tables);
    geometryTableTabs->addTab(wireFeedPage, QStringLiteral("Wires / feeds"));

    m_planeTable = new QTableWidget(0, 21, geometryTableTabs);
    m_planeTable->setHorizontalHeaderLabels({QStringLiteral("Surface"),QStringLiteral("Type"),QStringLiteral("cx m"),QStringLiteral("cy m"),QStringLiteral("cz m"),QStringLiteral("Base"),QStringLiteral("Yaw °"),QStringLiteral("Pitch °"),QStringLiteral("Roll °"),QStringLiteral("Radius m"),QStringLiteral("Inner R m"),QStringLiteral("Width m"),QStringLiteral("Height m"),QStringLiteral("Focal m"),QStringLiteral("Mesh hint m"),QStringLiteral("Feed W m"),QStringLiteral("Feed L m"),QStringLiteral("Inset m"),QStringLiteral("Notch gap m"),QStringLiteral("Triangles"),QStringLiteral("Area m²")});
    m_planeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_planeTable->horizontalHeader()->setStretchLastSection(true);
    m_planeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_planeTable->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::SelectedClicked|QAbstractItemView::EditKeyPressed);
    m_planeTable->setToolTip(QStringLiteral("Triangulated PEC surfaces. Edit type, XYZ, base orientation, Euler angles and dimensions numerically. Triangle count/area are derived. Use standalone PEC EFIE/RWG for scattering/surface-only studies, or Hybrid wire + PEC when these surfaces must interact self-consistently with a driven wire antenna."));
    geometryTableTabs->addTab(m_planeTable, QStringLiteral("PEC surfaces"));

    m_dielectricTable = new QTableWidget(0, 13, geometryTableTabs);
    m_dielectricTable->setHorizontalHeaderLabels({QStringLiteral("Dielectric"),QStringLiteral("cx m"),QStringLiteral("cy m"),QStringLiteral("cz m"),QStringLiteral("Base"),QStringLiteral("Yaw °"),QStringLiteral("Pitch °"),QStringLiteral("Roll °"),QStringLiteral("Width m"),QStringLiteral("Height m"),QStringLiteral("Thickness m"),QStringLiteral("εr"),QStringLiteral("tanδ / fill")});
    m_dielectricTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_dielectricTable->horizontalHeader()->setStretchLastSection(true);
    m_dielectricTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dielectricTable->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::SelectedClicked|QAbstractItemView::EditKeyPressed);
    m_dielectricTable->setToolTip(QStringLiteral("Finite dielectric slabs used by the hybrid solver's effective-medium approximation. The last cell is written as tanδ / fill, e.g. 0.02 / 0.65; both values can be edited together."));
    geometryTableTabs->addTab(m_dielectricTable, QStringLiteral("Dielectric substrates"));

    auto *groupPage = new QWidget(geometryTableTabs);
    auto *groupLayout = new QVBoxLayout(groupPage);groupLayout->setContentsMargins(0,0,0,0);
    auto *groupButtons = new QHBoxLayout();
    auto *createGroupButton = new QPushButton(QStringLiteral("Group 3D selection"),groupPage);
    auto *ungroupButton = new QPushButton(QStringLiteral("Ungroup selected"),groupPage);
    auto *selectGroupButton = new QPushButton(QStringLiteral("Select members in 3D"),groupPage);
    createGroupButton->setToolTip(QStringLiteral("Create one persistent rigid CAD group from the current 3D selection. Clicking any member later selects the whole group for rigid transforms."));
    ungroupButton->setToolTip(QStringLiteral("Remove the selected group definition without deleting its geometry."));
    selectGroupButton->setToolTip(QStringLiteral("Select all geometry members of the selected group rows in the 3D editor."));
    groupButtons->addWidget(createGroupButton);groupButtons->addWidget(ungroupButton);groupButtons->addWidget(selectGroupButton);groupButtons->addStretch(1);groupLayout->addLayout(groupButtons);
    m_groupTable = new QTableWidget(0,3,groupPage);
    m_groupTable->setHorizontalHeaderLabels({QStringLiteral("Group"),QStringLiteral("Members"),QStringLiteral("Count")});
    m_groupTable->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    m_groupTable->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    m_groupTable->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    m_groupTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_groupTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_groupTable->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::SelectedClicked|QAbstractItemView::EditKeyPressed);
    m_groupTable->setToolTip(QStringLiteral("Persistent rigid groups. Rename a group by editing the first column. Members are stored by stable geometry IDs and survive table renames / .qta reloads."));
    groupLayout->addWidget(m_groupTable,1);
    geometryTableTabs->addTab(groupPage,QStringLiteral("Groups"));
    geometryLayout->addWidget(geometryTableTabs);

    m_currentMagnitudePlot = new FieldProfilePlot(m_resultTabs);
    m_currentMagnitudePlot->setXAxis(QStringLiteral("Distance along each connected conductor component"), QStringLiteral("m"));
    m_currentPhasePlot = new FieldProfilePlot(m_resultTabs);
    m_currentPhasePlot->setXAxis(QStringLiteral("Distance along each connected conductor component"), QStringLiteral("m"));
    m_chargeMagnitudePlot = new FieldProfilePlot(m_resultTabs);
    m_chargeMagnitudePlot->setXAxis(QStringLiteral("Distance along each original wire span"), QStringLiteral("m"));
    m_azimuthPlot = new FieldProfilePlot(m_resultTabs);
    m_azimuthPlot->setXAxis(QStringLiteral("Azimuth φ in XY plane"), QStringLiteral("deg"));
    m_elevationPlot = new FieldProfilePlot(m_resultTabs);
    m_elevationPlot->setXAxis(QStringLiteral("Polar angle θ, φ = 0° cut"), QStringLiteral("deg"));
    m_azimuthPolarPlot = new PolarPatternPlot(m_resultTabs);
    m_elevationPolarPlot = new PolarPatternPlot(m_resultTabs);
    m_radiation3D = new RadiationPattern3D(m_resultTabs);
    m_azimuthPlot->setToolTip(QStringLiteral("Move the mouse over the graph to inspect the nearest azimuth sample values."));
    m_elevationPlot->setToolTip(QStringLiteral("Move the mouse over the graph to inspect the nearest elevation sample values."));
    m_azimuthPolarPlot->setToolTip(QStringLiteral("Hover the polar cut to read angle and relative level."));
    m_elevationPolarPlot->setToolTip(QStringLiteral("Hover the polar cut to read angle and relative level."));
    m_radiation3D->setToolTip(QStringLiteral("Drag to rotate, wheel to zoom, hover to inspect the nearest angular sample, and use the Results top-right options to toggle the color map/legend."));
    m_resultTabs->addTab(m_currentMagnitudePlot, QStringLiteral("Solved |I|"));
    m_resultTabs->addTab(m_currentPhasePlot, QStringLiteral("Current phase"));
    m_resultTabs->addTab(m_chargeMagnitudePlot, QStringLiteral("Line charge |λ|"));
    m_validationPatternPlot = new FieldProfilePlot(m_resultTabs);
    m_validationPatternPlot->setXAxis(QStringLiteral("Polar angle θ"), QStringLiteral("deg"));
    m_resultTabs->addTab(m_validationPatternPlot, QStringLiteral("Validation pattern"));
    m_resultTabs->addTab(m_azimuthPlot, QStringLiteral("Far field — azimuth"));
    m_resultTabs->addTab(m_elevationPlot, QStringLiteral("Far field — elevation"));
    m_resultTabs->addTab(m_azimuthPolarPlot, QStringLiteral("Polar — azimuth"));
    m_resultTabs->addTab(m_elevationPolarPlot, QStringLiteral("Polar — elevation"));
    m_resultTabs->addTab(m_radiation3D, QStringLiteral("Radiation 3D"));

    m_surfaceCurrentPlot = new FieldProfilePlot(m_resultTabs);
    m_surfaceCurrentPlot->setXAxis(QStringLiteral("PEC triangle index"), QString());
    m_surfaceRcsPlot = new FieldProfilePlot(m_resultTabs);
    m_surfaceRcsPlot->setXAxis(QStringLiteral("Observation angle"), QStringLiteral("deg"));
    m_resultTabs->addTab(m_surfaceCurrentPlot, QStringLiteral("PEC |J|"));
    m_resultTabs->addTab(m_surfaceRcsPlot, QStringLiteral("PEC RCS / radiation"));

    m_sweepZPlot = new FieldProfilePlot(m_resultTabs);
    m_sweepZPlot->setXAxis(QStringLiteral("Frequency"), QStringLiteral("MHz"));
    m_sweepS11Plot = new FieldProfilePlot(m_resultTabs);
    m_sweepS11Plot->setXAxis(QStringLiteral("Frequency"), QStringLiteral("MHz"));
    m_sweepVswrPlot = new FieldProfilePlot(m_resultTabs);
    m_sweepVswrPlot->setXAxis(QStringLiteral("Frequency"), QStringLiteral("MHz"));
    m_sweepSmith = new SmithChart(m_resultTabs);
    m_sweepZPlot->setToolTip(QStringLiteral("Hover to inspect the impedance sweep at the nearest sampled frequency."));
    m_sweepS11Plot->setToolTip(QStringLiteral("Hover to inspect S11 at the nearest sampled frequency."));
    m_sweepVswrPlot->setToolTip(QStringLiteral("Hover to inspect VSWR at the nearest sampled frequency."));
    m_sweepSmith->setToolTip(QStringLiteral("Hover the Smith trace to inspect Γ, normalized impedance, return loss and VSWR."));
    m_resultTabs->addTab(m_sweepZPlot, QStringLiteral("Sweep Zin"));
    m_resultTabs->addTab(m_sweepS11Plot, QStringLiteral("Sweep Γ / S11"));
    m_resultTabs->addTab(m_sweepVswrPlot, QStringLiteral("Sweep VSWR"));
    m_resultTabs->addTab(m_sweepSmith, QStringLiteral("Sweep Smith"));
    m_optimizationPlot = new FieldProfilePlot(m_resultTabs);
    m_optimizationPlot->setXAxis(QStringLiteral("Geometry scale factor"), QString());
    m_resultTabs->addTab(m_optimizationPlot, QStringLiteral("Optimization"));
    m_multiOptimizationPlot = new FieldProfilePlot(m_resultTabs);
    m_multiOptimizationPlot->setXAxis(QStringLiteral("Unique candidate evaluation"), QString());
    m_resultTabs->addTab(m_multiOptimizationPlot, QStringLiteral("Yagi opt"));
    m_individualOptimizationPlot = new FieldProfilePlot(m_resultTabs);
    m_individualOptimizationPlot->setXAxis(QStringLiteral("Unique candidate evaluation"), QString());
    m_resultTabs->addTab(m_individualOptimizationPlot, QStringLiteral("Yagi individual"));
    m_resultTabs->setMinimumHeight(210);
    m_resultTabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *resultTabOptions = new QWidget(m_resultTabs);
    auto *resultTabOptionsLayout = new QHBoxLayout(resultTabOptions);
    resultTabOptionsLayout->setContentsMargins(0, 0, 0, 0);
    resultTabOptionsLayout->setSpacing(8);
    auto *resultOptionsLabel = new QLabel(QStringLiteral("3D view:"), resultTabOptions);
    auto *radiationColorMap = new QCheckBox(QStringLiteral("Color map"), resultTabOptions);
    auto *radiationLegend = new QCheckBox(QStringLiteral("Legend"), resultTabOptions);
    radiationColorMap->setChecked(true);
    radiationLegend->setChecked(true);
    radiationColorMap->setToolTip(QStringLiteral("Color the 3D radiation surface from blue (low gain) to red (maximum gain); the legend is in absolute dBi within the current solver model."));
    radiationLegend->setToolTip(QStringLiteral("Show the normalized dB color legend for the 3D radiation pattern."));
    resultTabOptionsLayout->addWidget(resultOptionsLabel);
    resultTabOptionsLayout->addWidget(radiationColorMap);
    resultTabOptionsLayout->addWidget(radiationLegend);
    m_resultTabs->setCornerWidget(resultTabOptions, Qt::TopRightCorner);
    QObject::connect(radiationColorMap, &QCheckBox::toggled, m_radiation3D, [this](bool on){ if (m_radiation3D) m_radiation3D->setColorMapEnabled(on); });
    QObject::connect(radiationLegend, &QCheckBox::toggled, m_radiation3D, [this](bool on){ if (m_radiation3D) m_radiation3D->setLegendVisible(on); });

    auto *deleteSelected = new QPushButton(QStringLiteral("Delete selected geometry-table rows"), geometryTab);
    deleteSelected->setToolTip(QStringLiteral("Delete the selected wire/feed/PEC/substrate rows from the active geometry table."));
    geometryLayout->addWidget(deleteSelected, 0, Qt::AlignLeft);

    auto *geometryWorkspace = new QTabWidget(this);
    // Historical v5.8 workspace label kept in source for regression traceability: Feed / files / summary
    geometryWorkspace->addTab(geometryTab, QStringLiteral("Element tables"));
    geometryWorkspace->addTab(wrapWorkspacePage(setupPage), QStringLiteral("Files / summary"));
    geometryWorkspace->addTab(wrapWorkspacePage(constraintPage), QStringLiteral("Constraints"));

    auto *presetWorkspace = new QTabWidget(this);
    presetWorkspace->addTab(wrapWorkspacePage(presetPage), QStringLiteral("Classic presets"));
    presetWorkspace->addTab(wrapWorkspacePage(primitivePage), QStringLiteral("3D primitives"));

    auto *simulationWorkspace = new QTabWidget(this);
    simulationWorkspace->addTab(wrapWorkspacePage(feedSimulationPage), QStringLiteral("Feeds / ports"));
    simulationWorkspace->addTab(wrapWorkspacePage(momPage), QStringLiteral("MoM / RWG / Hybrid"));
    simulationWorkspace->addTab(wrapWorkspacePage(validationPage), QStringLiteral("Validation bench"));
    simulationWorkspace->addTab(wrapWorkspacePage(sweepPage), QStringLiteral("Frequency sweep"));
    simulationWorkspace->addTab(wrapWorkspacePage(optPage), QStringLiteral("Optimization"));

    auto *resultWorkspace = new QWidget(this);
    auto *resultWorkspaceLayout = new QVBoxLayout(resultWorkspace);
    resultWorkspaceLayout->setContentsMargins(0, 0, 0, 0);
    resultWorkspaceLayout->setSpacing(5);
    auto *engineeringUseNotice = new QLabel(
        QStringLiteral("Engineering-use notice — Antenna results are model-dependent numerical estimates. "
                       "For critical designs, validate with an independent 3D full-wave solver and/or measurement before engineering sign-off."),
        resultWorkspace);
    engineeringUseNotice->setWordWrap(true);
    engineeringUseNotice->setTextInteractionFlags(Qt::TextSelectableByMouse);
    engineeringUseNotice->setToolTip(
        QStringLiteral("Simulation / Antenna Designer 1.0 is validated against the frozen internal regression gates, "
                       "but its thin-wire/RWG/one-slab layered models are not a general-purpose full-wave qualification backend. "
                       "See SIMULATION_ANTENNA_1_0_REFERENCE.md for equations, conventions, approximations and scope."));
    resultWorkspaceLayout->addWidget(engineeringUseNotice);
    resultWorkspaceLayout->addWidget(m_resultTabs, 1);

    auto *workspaceTabs = new QTabWidget(this);
    m_workspaceTabs = workspaceTabs;
    workspaceTabs->setDocumentMode(true);
    workspaceTabs->addTab(designBox, QStringLiteral("Designer"));
    workspaceTabs->addTab(geometryWorkspace, QStringLiteral("Geometry"));
    workspaceTabs->addTab(presetWorkspace, QStringLiteral("Presets"));
    workspaceTabs->addTab(simulationWorkspace, QStringLiteral("Simulation"));
    workspaceTabs->addTab(resultWorkspace, QStringLiteral("Results"));
    workspaceTabs->setTabToolTip(0, QStringLiteral("Large construction workspace: 2D/3D editing, transforms and view navigation only."));
    workspaceTabs->setTabToolTip(1, QStringLiteral("Exact element tables, geometry files/summary and parametric constraints."));
    workspaceTabs->setTabToolTip(2, QStringLiteral("Classic antenna templates and generic 3D primitive generation."));
    workspaceTabs->setTabToolTip(3, QStringLiteral("Per-feed excitations/ports, thin-wire MoM, PEC RWG, hybrid solves, thin-wire frequency sweeps and optimizers."));
    workspaceTabs->setTabToolTip(4, QStringLiteral("Currents, impedance, radiation, RCS, Smith chart, sweeps and optimization plots. Critical antenna designs require independent full-wave and/or measurement validation before engineering sign-off."));
    root->addWidget(workspaceTabs, 1);

    QObject::connect(editorToolCombo,qOverload<int>(&QComboBox::currentIndexChanged),this,[=](int index){
        if(m_canvas)m_canvas->setToolSelectorIndex(index);
        if(m_geometry3D)m_geometry3D->setToolSelectorIndex(index);
        if(index<=3){
            m_canvas->setTool(static_cast<Canvas::Tool>(index));
            if(m_geometry3D)m_geometry3D->setTool(static_cast<Geometry3DView::Tool>(index));
            return;
        }
        m_canvas->setTool(Canvas::Tool::Select);
        if(!m_geometry3D)return;
        geometryViews->setCurrentWidget(m_geometry3D);
        Geometry3DView::Tool tool=Geometry3DView::Tool::Select;
        if(index==4)tool=Geometry3DView::Tool::AddRectPEC;
        else if(index==5)tool=Geometry3DView::Tool::AddDielectric;
        else if(index==6)tool=Geometry3DView::Tool::Measure;
        else if(index==7)tool=Geometry3DView::Tool::PickPivot;
        m_geometry3D->setTool(tool);
    });
    QObject::connect(clearMeasureButton,&QPushButton::clicked,this,[this]{if(m_geometry3D)m_geometry3D->clearMeasurement();});
    QObject::connect(m_gridMm, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [=](double v) { const int plane=m_editPlane?m_editPlane->currentIndex():0;const double coord=m_activePlaneCoordinateM?m_activePlaneCoordinateM->value():0.0;m_canvas->setEditContext(plane,coord,v); if(m_geometry3D)m_geometry3D->setEditContext(plane,coord,v); });
    QObject::connect(undoButton, &QPushButton::clicked, this, [this]{ undoGeometry(); });
    QObject::connect(redoButton, &QPushButton::clicked, this, [this]{ redoGeometry(); });
    undoButton->setShortcut(QKeySequence::Undo);
    redoButton->setShortcut(QKeySequence::Redo);
    QObject::connect(placementModeCombo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int mode){
        m_magneticSnapEnabled=(mode==1);
        if(m_geometry3D)m_geometry3D->setObjectSnapEnabled(m_magneticSnapEnabled);
    });
    QObject::connect(duplicateButton,&QPushButton::clicked,this,&AntennaDesignerWidget::duplicate3DSelection);
    if(m_geometry3D)m_geometry3D->duplicateRequested=[this]{duplicate3DSelection();};
    if(m_geometry3D)m_geometry3D->groupCreateRequested=[this]{createGroupFrom3DSelection();};
    if(m_geometry3D)m_geometry3D->groupUngroupRequested=[this]{ungroupSelectedGeometryGroups();};
    QObject::connect(alignXButton,&QPushButton::clicked,this,[this]{align3DSelection(0);});
    QObject::connect(alignYButton,&QPushButton::clicked,this,[this]{align3DSelection(1);});
    QObject::connect(alignZButton,&QPushButton::clicked,this,[this]{align3DSelection(2);});
    if(m_geometry3D)m_geometry3D->alignRequested=[this](int axis){align3DSelection(axis);};
    if(m_geometry3D)m_geometry3D->coincidentCentersRequested=[this]{coincideSelectedSurfaceCenters3D();};
    if(m_geometry3D)m_geometry3D->matchSurfaceSizeRequested=[this]{matchSelectedSurfaceDimensions3D();};
    QObject::connect(moveNumericButton,&QPushButton::clicked,this,[this,dxMm,dyMm,dzMm]{GeometryPoint d;d.xyM={dxMm->value()/MmPerM,dyMm->value()/MmPerM};d.zM=dzMm->value()/MmPerM;translate3DSelectionNumerically(d);dxMm->setValue(0.0);dyMm->setValue(0.0);dzMm->setValue(0.0);});
    QObject::connect(gizmoModeCombo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int mode){if(m_geometry3D)m_geometry3D->setGizmoMode(mode);});
    QObject::connect(pivotCombo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,pivotXmm,pivotYmm,pivotZmm](int mode){for(auto*s:{pivotXmm,pivotYmm,pivotZmm})s->setEnabled(mode==3);if(m_geometry3D)m_geometry3D->setPivotMode(mode);});
    const auto updateCustomPivot=[this,pivotXmm,pivotYmm,pivotZmm]{if(m_geometry3D)m_geometry3D->setCustomPivot({pivotXmm->value()/MmPerM,pivotYmm->value()/MmPerM,pivotZmm->value()/MmPerM});};
    QObject::connect(pivotXmm,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[updateCustomPivot](double){updateCustomPivot();});
    QObject::connect(pivotYmm,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[updateCustomPivot](double){updateCustomPivot();});
    QObject::connect(pivotZmm,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[updateCustomPivot](double){updateCustomPivot();});
    const auto currentPivot=[this]{GeometryPoint p;if(m_geometry3D){const auto q=m_geometry3D->transformPivot();p.xyM={q.x,q.y};p.zM=q.z;}return p;};
    QObject::connect(rotateButton,&QPushButton::clicked,this,[this,rotationAxisCombo,rotationDeg,currentPivot]{rotate3DSelection(rotationAxisCombo->currentIndex(),rotationDeg->value(),currentPivot());});
    if(m_geometry3D)m_geometry3D->mirrorRequested=[this](int axis,const NumericalEM::Vec3&p){mirrorCopy3DSelection(axis,{QPointF(p.x,p.y),p.z});};
    if(m_geometry3D)m_geometry3D->constructionPlaneRotateRequested=[this](double angle,const NumericalEM::Vec3&pivot,const NumericalEM::Vec3&axis){rotate3DSelectionAroundVector({QPointF(axis.x,axis.y),axis.z},angle,{QPointF(pivot.x,pivot.y),pivot.z});};
    QObject::connect(helpButton, &QToolButton::clicked, this, [this] {
        QMessageBox::information(this, QStringLiteral("Antenna designer — editor controls"),
            QStringLiteral("2D orthographic editor\n"
                           "• Left drag in Select mode: selection / node editing.\n"
                           "• Middle drag: pan the 2D view. Right click opens the context menu.\n"
                           "• Mouse wheel: zoom around the cursor.\n"
                           "• Plane XY/XZ/YZ selection and Fit are fixed overlays in the upper-right corner of the 2D viewport.\n\n"
                           "3D editor\n"
                           "• Left drag in empty space: orbit the camera.\n"
                           "• Middle drag: pan the camera. Mouse wheel: zoom. Right click opens context actions.\n"
                           "• The Tool selector contains Select, Draw wire, Place feed, Delete, PEC/substrate creation, measurement and pivot picking.\n"
                           "• 3D-only tools automatically switch to the 3D editor; wire/feed tools work in both editors on the active construction plane.\n"
                           "• Ctrl+click: additive mixed selection. Ctrl+drag in empty space: selection rectangle.\n"
                           "• Move/Rotate gizmos and numerical ΔXYZ remain in the compact transform strip; duplicate/align/mirror/surface relations are also available from the selection context menu.\n"
                           "• Rectangular PEC/substrate selections expose local-frame corner handles even after rotation; substrates also expose thickness handles and circular PEC primitives expose a radius handle.\n"
                           "• Persistent constraints are managed in the Geometry → Constraints workspace. Right-click a selected object or node and choose Add constraint… to open that editor with the selected reference(s) preloaded. Supported dimensions can use A→B or Balanced solve mode, and Fix position axes provides X/Y/Z anchors. Constraint annotations are selectable in 3D; double-click a dimension to edit it and press Delete to remove it.\n"
                           "• The ViewCube in the upper-right follows camera orientation. Click a visible cube face for an exact orthographic axis view; side and top/bottom arrows rotate the camera in 45° steps.\n"
                           "• Editable X/Y/Z camera angles are stacked directly below the ViewCube. Four discreet pan arrows use the selectable 1/10/50/100 mm step shown below their center button.\n"
                           "• The Construction plane panel is fixed in the upper-left: XY/XZ/YZ are presets, Offset positions the plane, and Rx/Ry/Rz freely orient it. Place pivot lets you pick a rotation center directly on that plane.\n"
                           "• Free placement follows the cursor exactly. Magnetic anchor uses a small screen-space attraction zone and welds wire endpoints to nearby endpoints or wire segments; an interior hit creates a true T-junction by splitting the target wire.\n"
                           "• Selecting any member of a rigid CAD group selects the complete group for move/rotate/align/duplicate/mirror operations. Create/Ungroup are available from the 3D context menu and Geometry → Groups.\n"
                           "• Selecting one object opens a compact in-view property inspector: wires expose XYZ, length and diameter; feeds/surfaces/substrates expose the relevant XYZ/dimensions. Press Delete/Backspace to request confirmed deletion.\n\n"
                           "Simulation feeds / ports\n"
                           "• Each feed has independent |V|, phase and Zref values in Simulation → Feeds / ports. All complex feed voltages are applied simultaneously by the generalized MoM.\n"
                           "• Reported / exported feed chooses which active input impedance is sent to the RF/EM tools after a direct wire-MoM solve. Zref affects Γ/S11/VSWR reporting only; it is not a physical series resistor.\n\n"
                           "Both editors modify the same antenna geometry. Magnetic feed/wire placement can create true topological nodes, and Undo/Redo covers direct geometry edits and groups."));
    });
    const auto buildPresetAndReturnToDesigner=[this,workspaceTabs]{generatePreset(m_presetCombo ? m_presetCombo->currentIndex() : 0);if(m_geometry3D)m_geometry3D->fitGeometry();workspaceTabs->setCurrentIndex(0);};
    QObject::connect(preset, &QPushButton::clicked, this, buildPresetAndReturnToDesigner);
    QObject::connect(centerSweep, &QPushButton::clicked, this, [this] {
        const double f = std::max(0.001, m_frequencyMHz->value());
        m_sweepStartMHz->setValue(0.70 * f);
        m_sweepStopMHz->setValue(1.30 * f);
    });
    QObject::connect(runSweep, &QPushButton::clicked, this, [this] { solveFrequencySweep(); });
    QObject::connect(rebuildPreset, &QPushButton::clicked, this, buildPresetAndReturnToDesigner);
    auto updatePatchEstimate = [this] {
        if(!m_presetPatchEstimate || !m_presetPatchEr || !m_presetPatchHeightMm || !m_presetPatchLineZ0 || !m_presetPatchEdgeResistance) return;
        const double fHz=std::max(1.0,m_frequencyMHz->value()*1e6);
        const double er=m_presetPatchEr->value();
        const double h=m_presetPatchHeightMm->value()*1e-3;
        const double z0=m_presetPatchLineZ0->value();
        const auto patch=MicrostripModels::rectangularPatch(fHz,h,er);
        const double lineW=MicrostripModels::widthForImpedance(z0,h,er);
        const auto line=MicrostripModels::analyzeLine(fHz,lineW,h,er,m_presetPatchTanD?m_presetPatchTanD->value():0.0,patch.valid?0.5*patch.physicalLengthM:0.0);
        const auto inset=patch.valid?MicrostripModels::insetForResistance(patch.physicalLengthM,m_presetPatchEdgeResistance->value(),z0):MicrostripModels::InsetResult{};
        if(!patch.valid || !line.valid){m_presetPatchEstimate->setText(QStringLiteral("Invalid printed-antenna parameters."));return;}
        m_presetPatchEstimate->setText(QStringLiteral("Wpatch ≈ %1 mm | Lpatch ≈ %2 mm | εeff,patch ≈ %3\n"
                                                        "Wline(%4 Ω) ≈ %5 mm | εeff,line ≈ %6 | λg ≈ %7 mm\n"
                                                        "Inset for Redge=%8 Ω → %9 mm%10")
            .arg(patch.widthM*1e3,0,'g',6).arg(patch.physicalLengthM*1e3,0,'g',6).arg(patch.effectivePermittivity,0,'g',5)
            .arg(z0,0,'g',5).arg(lineW*1e3,0,'g',6).arg(line.effectivePermittivity,0,'g',5).arg(line.guidedWavelengthM*1e3,0,'g',6)
            .arg(m_presetPatchEdgeResistance->value(),0,'g',5).arg(inset.valid?inset.insetDepthM*1e3:0.0,0,'g',6)
            .arg(inset.valid?QString():QStringLiteral(" (target exceeds edge estimate)")));
    };
    for(QDoubleSpinBox *box:{m_presetPatchEr,m_presetPatchHeightMm,m_presetPatchTanD,m_presetPatchLineZ0,m_presetPatchEdgeResistance,m_presetPatchNotchGapMm})
        if(box) QObject::connect(box,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[updatePatchEstimate](double){updatePatchEstimate();});
    QObject::connect(m_frequencyMHz,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[updatePatchEstimate](double){updatePatchEstimate();});
    updatePatchEstimate();
    auto updatePrimitiveHelp = [this] {
        if (!m_primitiveHelp || !m_primitiveType) return;
        switch (m_primitiveType->currentIndex())
        {
            case 0: m_primitiveHelp->setText(QStringLiteral("Circular loop: radius + start angle + curve segments. A complete 360° loop is a closed degree-2 path compatible with the thin-wire MoM.")); break;
            case 1: m_primitiveHelp->setText(QStringLiteral("Circular arc: radius + start/sweep angles. It remains an open wire path; place the feed on an interior generated node, not at an open endpoint.")); break;
            case 2: m_primitiveHelp->setText(QStringLiteral("Rectangular loop: width × height in the selected orientation plane. The bottom edge is split at its midpoint so a feed can be placed on a valid interior node.")); break;
            case 3: m_primitiveHelp->setText(QStringLiteral("Helix / solenoid wire: radius, pitch per turn, turns and segments/turn. The helix axis is normal to the selected orientation plane.")); break;
            case 4: m_primitiveHelp->setText(QStringLiteral("Planar spiral: Archimedean wire spiral from inner radius to outer radius over the requested turns. It lies in the selected orientation plane.")); break;
            case 5: m_primitiveHelp->setText(QStringLiteral("Meander / serpentine: continuous planar zig-zag path inside Width × Height. Turns/folds controls the number of traversals.")); break;
            case 6: m_primitiveHelp->setText(QStringLiteral("Rectangular sheet: meshed PEC surface geometry. Width × height and mesh hint define the triangulation. Use PEC EFIE/RWG for plane-wave current/RCS analysis; the separate thin-wire solve does not yet couple to it.")); break;
            case 7: m_primitiveHelp->setText(QStringLiteral("Disk / annular sheet: outer radius plus optional inner radius. Inner radius = 0 gives a solid disk; a nonzero value creates an annulus.")); break;
            case 8: m_primitiveHelp->setText(QStringLiteral("Cylindrical shell: open lateral PEC surface with Radius and Height/axial length. The cylinder axis is the selected plane normal, then Euler rotations are applied.")); break;
            case 9: m_primitiveHelp->setText(QStringLiteral("Conical shell: open cone from an apex to a circular rim. Radius sets the base radius and Height sets the axial length.")); break;
            case 10: m_primitiveHelp->setText(QStringLiteral("Parabolic reflector: vertex at Origin, rim radius = Radius and focal length = F. Surface follows z_local = r²/(4F), opens along the local normal, and is directly usable by the PEC EFIE/RWG scattering solver.")); break;
            case 11: m_primitiveHelp->setText(QStringLiteral("Dielectric substrate/slab: finite rectangular volume with width × height × thickness, εr and tanδ. The hybrid solver uses it through a localized effective-medium Green-function approximation. Field fill = 1 behaves like a homogeneous filled medium; 0.5–0.8 is useful for open microstrip/patch studies but is not a rigorous layered-medium solution.")); break;
            default: m_primitiveHelp->clear(); break;
        }
    };
    QObject::connect(m_primitiveType, qOverload<int>(&QComboBox::currentIndexChanged), this, [updatePrimitiveHelp](int) { updatePrimitiveHelp(); });
    QObject::connect(buildPrimitiveButton, &QPushButton::clicked, this, [this,workspaceTabs] { buildPrimitive();if(m_geometry3D)m_geometry3D->fitGeometry();workspaceTabs->setCurrentIndex(0); });
    updatePrimitiveHelp();
    QObject::connect(useDesignFrequency, &QPushButton::clicked, this, [this] { if (m_optTargetMHz) m_optTargetMHz->setValue(m_frequencyMHz->value()); });
    QObject::connect(runOptimization, &QPushButton::clicked, this, [this] { optimizeGeometry(); });
    QObject::connect(m_applyOptimized, &QPushButton::clicked, this, [this] { applyOptimizedGeometry(); });
    QObject::connect(multiUseDesignFrequency, &QPushButton::clicked, this, [this] { if (m_multiTargetMHz) m_multiTargetMHz->setValue(m_frequencyMHz->value()); });
    QObject::connect(runMultiOptimization, &QPushButton::clicked, this, [this] { optimizeYagiMulti(); });
    QObject::connect(m_applyMultiOptimized, &QPushButton::clicked, this, [this] { applyMultiOptimizedGeometry(); });
    QObject::connect(refreshIndividual, &QPushButton::clicked, this, [this] { refreshYagiIndividualVariables(); });
    QObject::connect(runIndividualOptimization, &QPushButton::clicked, this, [this] { optimizeYagiIndividual(); });
    QObject::connect(m_applyIndividualOptimized, &QPushButton::clicked, this, [this] {
        if (m_individualOptimizedWires.empty() || m_individualOptimizedFeeds.empty()) return;
        pushGeometryHistory();
        m_wires = m_individualOptimizedWires;
        m_feeds = m_individualOptimizedFeeds;
        rebuildScene(false);
        if (m_individualStatus) m_individualStatus->setText(QStringLiteral("Individual-director optimized geometry applied. Run a full-resolution MoM solve and dense frequency sweep to validate it."));
    });
    QObject::connect(m_constraintType, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int type){
        if(!m_constraintValueMm)return;const bool valueType=(type==4||type==6||type==7||type==11);m_constraintValueMm->setEnabled(valueType);
        if(type==6){m_constraintValueMm->setSuffix(QStringLiteral(" deg"));m_constraintValueMm->setRange(0.0,180.0);m_constraintValueMm->setDecimals(3);if(m_constraintValueMm->value()>180.0)m_constraintValueMm->setValue(90.0);}
        else{m_constraintValueMm->setSuffix(QStringLiteral(" mm"));m_constraintValueMm->setRange(0.0,1e9);m_constraintValueMm->setDecimals(5);}
        if(m_constraintRefB)m_constraintRefB->setEnabled(type!=13);
        if(m_constraintFixAxes)m_constraintFixAxes->setEnabled(type==13);
        if(m_constraintSolveMode)m_constraintSolveMode->setEnabled(type==0||type==3||type==4||type==5||type==11);
    });
    QObject::connect(addConstraint, &QPushButton::clicked, this, [this] { addGeometryConstraint(); });
    QObject::connect(solveConstraints, &QPushButton::clicked, this, [this] { pushGeometryHistory();solveGeometryConstraints();rebuildScene(false); });
    QObject::connect(toggleConstraints, &QPushButton::clicked, this, [this] { toggleSelectedGeometryConstraints(); });
    QObject::connect(removeConstraints, &QPushButton::clicked, this, [this] { removeSelectedGeometryConstraints(); });
    QObject::connect(m_constraintTable, &QTableWidget::itemSelectionChanged, this, [this] {
        if(!m_constraintTable||!m_geometry3D)return;const auto rows=m_constraintTable->selectionModel()->selectedRows();m_geometry3D->setSelectedConstraint(rows.size()==1?rows.front().row():-1);
    });
    QObject::connect(m_constraintTable, &QTableWidget::cellDoubleClicked, this, [this](int row,int){editGeometryConstraintValue(row);});
    QObject::connect(save, &QPushButton::clicked, this, [this] { saveGeometry(); });
    QObject::connect(load, &QPushButton::clicked, this, [this] { loadGeometry(); });
    QObject::connect(clear, &QPushButton::clicked, this, [this] { clearGeometry(); });
    QObject::connect(deleteSelected, &QPushButton::clicked, this, [this] { deleteSelectedRows(); });
    QObject::connect(createGroupButton,&QPushButton::clicked,this,&AntennaDesignerWidget::createGroupFrom3DSelection);
    QObject::connect(ungroupButton,&QPushButton::clicked,this,&AntennaDesignerWidget::ungroupSelectedGeometryGroups);
    QObject::connect(selectGroupButton,&QPushButton::clicked,this,&AntennaDesignerWidget::selectGeometryGroupMembers);
    QObject::connect(m_groupTable,&QTableWidget::itemChanged,this,[this](QTableWidgetItem *item){
        if(!item||item->column()!=0)return;const int r=item->row();if(r<0||r>=static_cast<int>(m_groups.size()))return;
        const QString name=item->text().trimmed();if(name.isEmpty()){refreshGroupTable();return;}if(m_groups[static_cast<std::size_t>(r)].name==name)return;
        pushGeometryHistory();m_groups[static_cast<std::size_t>(r)].name=name;refreshGroupTable();
    });
    QObject::connect(m_wireTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if(!item)return;const int r=item->row(),c=item->column();if(r<0||r>=static_cast<int>(m_wires.size()))return;auto&w=m_wires[static_cast<std::size_t>(r)];
        if(c==7){refreshTables();return;}
        if(c==0){const QString name=item->text().trimmed();if(name.isEmpty()){refreshTables();return;}w.name=name;rebuildScene(false);return;}
        bool ok=false;const double value=item->text().toDouble(&ok);if(!ok||!std::isfinite(value)){refreshTables();return;}
        if(c>=1&&c<=6)
        {
            const bool endpointA=c<=3;QPointF old=endpointA?w.aM:w.bM;double oldZ=endpointA?w.azM:w.bzM;QPointF exact=old;double exactZ=oldZ;
            const int local=endpointA?c:c-3;if(local==1)exact.setX(value);else if(local==2)exact.setY(value);else exactZ=value;
            if(distance3D(old,oldZ,exact,exactZ)<1e-14){refreshTables();return;}
            constexpr double sameTol=1e-8;
            auto replaceNode=[&](QPointF&xy,double&z){if(distance3D(xy,z,old,oldZ)<=sameTol){xy=exact;z=exactZ;}};
            for(auto&wire:m_wires){replaceNode(wire.aM,wire.azM);replaceNode(wire.bM,wire.bzM);}for(auto&feed:m_feeds)replaceNode(feed.positionM,feed.zM);
            m_wires.erase(std::remove_if(m_wires.begin(),m_wires.end(),[](const WireElement&wire){return segmentLength3D(wire.aM,wire.azM,wire.bM,wire.bzM)<1e-9;}),m_wires.end());rebuildScene(false);return;
        }
        if(c==8){if(value<=0.0){refreshTables();return;}w.radiusM=value/MmPerM;rebuildScene(false);}
    });
    QObject::connect(m_feedTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if(!item)return;const int r=item->row(),c=item->column();if(r<0||r>=static_cast<int>(m_feeds.size()))return;auto&f=m_feeds[static_cast<std::size_t>(r)];
        if(c==0){const QString name=item->text().trimmed();if(name.isEmpty()){refreshTables();return;}f.name=name;rebuildScene(false);return;}
        bool ok=false;const double value=item->text().toDouble(&ok);if(!ok||!std::isfinite(value)){refreshTables();return;}
        if(c>=1&&c<=3){if(c==1)f.positionM.setX(value);else if(c==2)f.positionM.setY(value);else f.zM=value;rebuildScene(false);return;}
        if(c==4){if(value<0.0){refreshTables();return;}f.voltageV=value;}
        else if(c==5)f.phaseDeg=value;
        else if(c==6){if(value<=0.0){refreshTables();return;}f.sourceOhm=value;}
        else return;
        refreshTables();clearMomResults();clearSweepResults();clearOptimizationResults();clearMultiOptimizationResults();
    });
    QObject::connect(m_feedSimulationTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if(!item)return;const int r=item->row(),c=item->column();if(r<0||r>=static_cast<int>(m_feeds.size()))return;
        if(c==0||c==4){refreshTables();return;}
        bool ok=false;const double value=item->text().toDouble(&ok);if(!ok||!std::isfinite(value)){refreshTables();return;}
        auto&f=m_feeds[static_cast<std::size_t>(r)];
        if(c==1){if(value<0.0){refreshTables();return;}f.voltageV=value;}
        else if(c==2)f.phaseDeg=value;
        else if(c==3){if(value<=0.0){refreshTables();return;}f.sourceOhm=value;}
        else return;
        refreshTables();clearMomResults();clearSweepResults();clearOptimizationResults();clearMultiOptimizationResults();
    });
    QObject::connect(m_reportedFeed, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int){ clearMomResults(); });
    QObject::connect(m_planeTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if(!item)return;const int r=item->row(),c=item->column();if(r<0||r>=static_cast<int>(m_planes.size()))return;auto&p=m_planes[static_cast<std::size_t>(r)];
        if(c==19||c==20){refreshTables();return;}
        if(c==0){const QString name=item->text().trimmed();if(name.isEmpty()){refreshTables();return;}p.name=name;rebuildScene(false);return;}
        if(c==1){const int kind=surfaceKindFromText(item->text());if(kind<0){refreshTables();return;}p.surfaceType=kind;rebuildScene(false);return;}
        if(c==5){const QString t=item->text().trimmed().toUpper();if(t==QStringLiteral("XY"))p.orientation=0;else if(t==QStringLiteral("XZ"))p.orientation=1;else if(t==QStringLiteral("YZ"))p.orientation=2;else{refreshTables();return;}rebuildScene(false);return;}
        bool ok=false;const double value=item->text().toDouble(&ok);if(!ok||!std::isfinite(value)){refreshTables();return;}
        switch(c){
            case 2:p.centerM.setX(value);break;case 3:p.centerM.setY(value);break;case 4:p.zM=value;break;
            case 6:p.yawDeg=value;break;case 7:p.pitchDeg=value;break;case 8:p.rollDeg=value;break;
            case 9:if(value<=0.0){refreshTables();return;}p.radiusM=value;break;
            case 10:if(value<0.0){refreshTables();return;}p.innerRadiusM=value;break;
            case 11:if(value<=0.0){refreshTables();return;}p.widthM=value;break;
            case 12:if(value<=0.0){refreshTables();return;}p.heightM=value;break;
            case 13:if(value<=0.0){refreshTables();return;}p.focalLengthM=value;break;
            case 14:if(value<=0.0){refreshTables();return;}p.meshHintM=value;break;
            case 15:if(value<=0.0){refreshTables();return;}p.feedWidthM=value;break;
            case 16:if(value<0.0){refreshTables();return;}p.feedLengthM=value;break;
            case 17:if(value<0.0){refreshTables();return;}p.insetDepthM=value;break;
            case 18:if(value<0.0){refreshTables();return;}p.notchGapM=value;break;default:return;}rebuildScene(false);
    });
    QObject::connect(m_dielectricTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if(!item)return;const int r=item->row(),c=item->column();if(r<0||r>=static_cast<int>(m_dielectrics.size()))return;auto&d=m_dielectrics[static_cast<std::size_t>(r)];
        if(c==0){const QString name=item->text().trimmed();if(name.isEmpty()){refreshTables();return;}d.name=name;rebuildScene(false);return;}
        if(c==4){const QString t=item->text().trimmed().toUpper();if(t==QStringLiteral("XY"))d.orientation=0;else if(t==QStringLiteral("XZ"))d.orientation=1;else if(t==QStringLiteral("YZ"))d.orientation=2;else{refreshTables();return;}rebuildScene(false);return;}
        if(c==12){const QStringList parts=item->text().split('/');bool ok1=false,ok2=false;const double tanD=parts.value(0).trimmed().toDouble(&ok1);const double fill=parts.size()>1?parts.value(1).trimmed().toDouble(&ok2):d.fieldFillFactor;if(!ok1||(parts.size()>1&&!ok2)||tanD<0.0||fill<0.0||fill>1.0){refreshTables();return;}d.lossTangent=tanD;d.fieldFillFactor=fill;rebuildScene(false);return;}
        bool ok=false;const double value=item->text().toDouble(&ok);if(!ok||!std::isfinite(value)){refreshTables();return;}
        switch(c){case 1:d.centerM.setX(value);break;case 2:d.centerM.setY(value);break;case 3:d.zM=value;break;case 5:d.yawDeg=value;break;case 6:d.pitchDeg=value;break;case 7:d.rollDeg=value;break;case 8:if(value<=0){refreshTables();return;}d.widthM=value;break;case 9:if(value<=0){refreshTables();return;}d.heightM=value;break;case 10:if(value<=0){refreshTables();return;}d.thicknessM=value;break;case 11:if(value<1.0){refreshTables();return;}d.relativePermittivity=value;break;default:return;}rebuildScene(false);
    });
    QObject::connect(momSolve, &QPushButton::clicked, this, [this] { solveGeneralizedMom(); });
    QObject::connect(checkConvergenceButton, &QPushButton::clicked, this, [this] { checkMomConvergence(); });
    QObject::connect(runValidationCase, &QPushButton::clicked, this, [this] { runPhysicalValidation(false); });
    QObject::connect(runValidationAll, &QPushButton::clicked, this, [this] { runPhysicalValidation(true); });
    QObject::connect(surfaceMomSolve, &QPushButton::clicked, this, [this] { solveSurfaceMom(); });
    QObject::connect(hybridSolve, &QPushButton::clicked, this, [this] { solveHybridMom(); });
    QObject::connect(m_frequencyMHz, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { refreshSummary(); clearMomResults(); clearSweepResults(); clearOptimizationResults(); clearMultiOptimizationResults(); if(m_validationTable)m_validationTable->setRowCount(0); if(m_validationStatus)m_validationStatus->setText(QStringLiteral("Validation stale — design frequency changed.")); if(m_validationPatternPlot){m_validationPatternPlot->clearData();m_validationPatternPlot->clearMarkers();} });
    QObject::connect(m_segmentsPerWavelength, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { clearMomResults(); clearSweepResults(); clearOptimizationResults(); clearMultiOptimizationResults(); });
    QObject::connect(m_radiusMeshFactor, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); clearSweepResults(); clearOptimizationResults(); clearMultiOptimizationResults(); });
    QObject::connect(m_junctionLocalSubdivisions, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { clearMomResults(); clearSweepResults(); clearOptimizationResults(); clearMultiOptimizationResults(); });
    QObject::connect(m_currentBasisTreatment, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        // The reduced-vs-Lagrange selector belongs only to the historical pulse model.
        // Rooftop current continuity is embedded directly in its node-centred basis.
        if (m_junctionTreatment) m_junctionTreatment->setEnabled(index == 0);
        clearMomResults(); clearSweepResults(); clearOptimizationResults(); clearMultiOptimizationResults();
    });
    if (m_junctionTreatment) m_junctionTreatment->setEnabled(!m_currentBasisTreatment || m_currentBasisTreatment->currentIndex() == 0);
    QObject::connect(m_junctionTreatment, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { clearMomResults(); clearSweepResults(); clearOptimizationResults(); clearMultiOptimizationResults(); });
    QObject::connect(m_maxMomUnknowns, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { clearMomResults(); clearSweepResults(); clearOptimizationResults(); clearMultiOptimizationResults(); });
    QObject::connect(m_surfaceExcitationMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int mode) {
        const bool planeWave = mode == 0;
        if (m_surfacePortFeed) m_surfacePortFeed->setEnabled(!planeWave);
        if (m_surfaceIncidenceAzDeg) m_surfaceIncidenceAzDeg->setEnabled(planeWave);
        if (m_surfaceIncidenceElDeg) m_surfaceIncidenceElDeg->setEnabled(planeWave);
        if (m_surfacePolarizationDeg) m_surfacePolarizationDeg->setEnabled(planeWave);
        if (m_surfaceFieldVpm) m_surfaceFieldVpm->setEnabled(planeWave);
        clearMomResults();
    });
    QObject::connect(m_surfacePortFeed, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { clearMomResults(); });
    QObject::connect(m_surfaceMaxTriangles, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { clearMomResults(); });
    QObject::connect(m_surfaceMaxUnknowns, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { clearMomResults(); });
    QObject::connect(m_surfaceIncidenceAzDeg, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); });
    QObject::connect(m_surfaceIncidenceElDeg, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); });
    QObject::connect(m_surfacePolarizationDeg, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); });
    QObject::connect(m_surfaceFieldVpm, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); });
    QObject::connect(m_surfaceSelfRegularization, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); });
    QObject::connect(m_hybridMaxUnknowns, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { clearMomResults(); });
    QObject::connect(m_hybridMutualRegularization, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); });
    QObject::connect(m_hybridPortMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int mode) {
        const bool terminalMode=mode==1||mode==2;
        const bool endpointMode=mode==1;
        if(m_hybridPortFeed)m_hybridPortFeed->setEnabled(terminalMode);
        if(m_hybridPortSurface)m_hybridPortSurface->setEnabled(terminalMode);
        if(m_hybridJunctionFeed)m_hybridJunctionFeed->setEnabled(terminalMode);
        if(m_hybridJunctionSurface)m_hybridJunctionSurface->setEnabled(terminalMode);
        if(m_hybridPortReferenceModel)m_hybridPortReferenceModel->setEnabled(endpointMode);
        if(m_hybridDifferentialPortRadiusMm)m_hybridDifferentialPortRadiusMm->setEnabled(mode==2);
        const bool coax=endpointMode&&m_hybridPortReferenceModel&&m_hybridPortReferenceModel->currentIndex()==1;
        for(auto *w:{m_hybridCoaxInnerRadiusMm,m_hybridCoaxOuterRadiusMm,m_hybridCoaxEr,m_hybridCoaxTanD,m_hybridCoaxLengthMm})if(w)w->setEnabled(coax);
        clearMomResults();
    });
    QObject::connect(m_hybridPortFeed, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { clearMomResults(); });
    QObject::connect(m_hybridPortSurface, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { clearMomResults(); });
    QObject::connect(m_hybridJunctionFeed, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { clearMomResults(); });
    QObject::connect(m_hybridJunctionSurface, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { clearMomResults(); });
    QObject::connect(m_hybridMappingToleranceMm, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); });
    QObject::connect(m_hybridDifferentialPortRadiusMm, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); });
    QObject::connect(m_hybridUseDielectric, &QCheckBox::toggled, this, [this](bool enabled) { if(m_hybridDielectricKernel)m_hybridDielectricKernel->setEnabled(enabled); clearMomResults(); });
    QObject::connect(m_hybridDielectricKernel, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { clearMomResults(); });
    QObject::connect(m_hybridTerminalHalfRwg, &QCheckBox::toggled, this, [this](bool) { clearMomResults(); });
    QObject::connect(m_hybridFiniteConductivity, &QCheckBox::toggled, this, [this](bool enabled) { if(m_hybridSurfaceConductivityMSm)m_hybridSurfaceConductivityMSm->setEnabled(enabled); if(m_hybridSurfaceThicknessUm)m_hybridSurfaceThicknessUm->setEnabled(enabled); clearMomResults(); });
    QObject::connect(m_hybridSurfaceConductivityMSm, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); });
    QObject::connect(m_hybridSurfaceThicknessUm, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); });
    m_hybridSurfaceConductivityMSm->setEnabled(m_hybridFiniteConductivity->isChecked());
    m_hybridSurfaceThicknessUm->setEnabled(m_hybridFiniteConductivity->isChecked());
    QObject::connect(m_hybridPortReferenceModel, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        const bool enabled=index==1&&m_hybridPortMode&&m_hybridPortMode->currentIndex()==1;
        for (auto *w : {m_hybridCoaxInnerRadiusMm,m_hybridCoaxOuterRadiusMm,m_hybridCoaxEr,m_hybridCoaxTanD,m_hybridCoaxLengthMm}) if(w)w->setEnabled(enabled);
        clearMomResults();
    });
    QObject::connect(m_hybridCoaxInnerRadiusMm, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); });
    QObject::connect(m_hybridCoaxOuterRadiusMm, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); });
    QObject::connect(m_hybridCoaxEr, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); });
    QObject::connect(m_hybridCoaxTanD, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); });
    QObject::connect(m_hybridCoaxLengthMm, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMomResults(); });
    QObject::connect(m_sweepStartMHz, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearSweepResults(); });
    QObject::connect(m_sweepStopMHz, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearSweepResults(); });
    QObject::connect(m_sweepPoints, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { clearSweepResults(); });
    QObject::connect(m_sweepFeed, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { clearSweepResults(); });
    QObject::connect(m_sweepExcitationMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { clearSweepResults(); });
    QObject::connect(m_sweepSolver, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { clearSweepResults(); });
    QObject::connect(m_optTargetMHz, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearOptimizationResults(); });
    QObject::connect(m_optFeed, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { clearOptimizationResults(); });
    QObject::connect(m_optVariable, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { clearOptimizationResults(); });
    QObject::connect(m_optObjective, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { clearOptimizationResults(); });
    QObject::connect(m_optMinFactor, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearOptimizationResults(); });
    QObject::connect(m_optMaxFactor, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearOptimizationResults(); });
    QObject::connect(m_optCoarseSamples, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { clearOptimizationResults(); });
    QObject::connect(m_optRefineIterations, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { clearOptimizationResults(); });
    QObject::connect(m_multiTargetMHz, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMultiOptimizationResults(); });
    QObject::connect(m_multiFeed, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { clearMultiOptimizationResults(); });
    QObject::connect(m_multiMinFactor, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMultiOptimizationResults(); });
    QObject::connect(m_multiMaxFactor, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMultiOptimizationResults(); });
    QObject::connect(m_multiSamplesPerVariable, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { clearMultiOptimizationResults(); });
    QObject::connect(m_multiPasses, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { clearMultiOptimizationResults(); });
    for (QCheckBox *box : {m_multiDrivenLength, m_multiReflectorLength, m_multiDirectorLength, m_multiReflectorSpacing, m_multiDirectorSpacing})
        QObject::connect(box, &QCheckBox::toggled, this, [this](bool) { clearMultiOptimizationResults(); });
    for (QDoubleSpinBox *box : {m_multiWeightMatch, m_multiWeightDirectivity, m_multiWeightFrontBack, m_multiWeightBandwidth, m_multiBandwidthHalfSpanPct, m_multiMinElementSpacingLambda, m_multiMaxBoomLengthLambda})
        QObject::connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { clearMultiOptimizationResults(); });
    QObject::connect(m_multiBandwidthSamples, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { clearMultiOptimizationResults(); });
    QObject::connect(m_multiBandwidthAggregation, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { clearMultiOptimizationResults(); });
    QObject::connect(m_individualDirectorTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) { clearMultiOptimizationResults(); });

    const auto setupPrimitiveFromViewport=[this,workspaceTabs,presetWorkspace](int type,const NumericalEM::Vec3 &p){
        if(!m_primitiveType)return;
        m_primitiveType->setCurrentIndex(std::clamp(type,0,m_primitiveType->count()-1));
        if(m_primitiveOrientation)m_primitiveOrientation->setCurrentIndex(m_editPlane?m_editPlane->currentIndex():0);
        if(m_primitiveOriginX)m_primitiveOriginX->setValue(p.x);if(m_primitiveOriginY)m_primitiveOriginY->setValue(p.y);if(m_primitiveOriginZ)m_primitiveOriginZ->setValue(p.z);
        // Context creation intentionally starts from known defaults, then lets the user review
        // dimensions before pressing Add primitive to geometry.  Orientation follows the current construction plane.
        if(m_primitiveYawDeg)m_primitiveYawDeg->setValue(m_constructionPlaneRotZDeg);if(m_primitivePitchDeg)m_primitivePitchDeg->setValue(m_constructionPlaneRotYDeg);if(m_primitiveRollDeg)m_primitiveRollDeg->setValue(m_constructionPlaneRotXDeg);
        if(m_primitiveRadiusM)m_primitiveRadiusM->setValue(0.20);if(m_primitiveInnerRadiusM)m_primitiveInnerRadiusM->setValue(0.03);
        if(m_primitiveWidthM)m_primitiveWidthM->setValue(0.50);if(m_primitiveHeightM)m_primitiveHeightM->setValue(0.30);if(m_primitiveFocalLengthM)m_primitiveFocalLengthM->setValue(0.25);
        if(m_primitivePitchM)m_primitivePitchM->setValue(0.08);if(m_primitiveThicknessM)m_primitiveThicknessM->setValue(0.0016);if(m_primitiveDielectricEr)m_primitiveDielectricEr->setValue(4.2);if(m_primitiveDielectricTanD)m_primitiveDielectricTanD->setValue(0.02);if(m_primitiveDielectricFill)m_primitiveDielectricFill->setValue(0.65);
        if(m_primitiveTurns)m_primitiveTurns->setValue(4.0);if(m_primitiveStartDeg)m_primitiveStartDeg->setValue(0.0);if(m_primitiveSweepDeg)m_primitiveSweepDeg->setValue(360.0);if(m_primitiveSegments)m_primitiveSegments->setValue(32);if(m_primitiveGridSpacingM)m_primitiveGridSpacingM->setValue(0.05);
        if(m_primitiveAddFeed)m_primitiveAddFeed->setChecked(false);if(m_primitiveClearFirst)m_primitiveClearFirst->setChecked(false);
        workspaceTabs->setCurrentWidget(presetWorkspace);presetWorkspace->setCurrentIndex(1);
    };
    m_canvas->primitiveSetupRequested=setupPrimitiveFromViewport;
    if(m_geometry3D)m_geometry3D->primitiveSetupRequested=setupPrimitiveFromViewport;

    const auto setupConstraintFromViewport=[this,workspaceTabs,geometryWorkspace](const std::vector<int>&wires,const std::vector<int>&feeds,const std::vector<int>&planes,const std::vector<int>&dielectrics,bool nodeSelected,const NumericalEM::Vec3&node){
        if(!m_constraintRefA||!m_constraintRefB||!m_constraintType)return;
        ensureGeometryIds();refreshConstraintEditor();
        std::vector<QString> refs;
        auto appendRef=[&](int kind,const QString&id,int anchor=0){const QString data=QStringLiteral("%1|%2|%3").arg(kind).arg(id).arg(anchor);if(std::find(refs.begin(),refs.end(),data)==refs.end())refs.push_back(data);};
        if(nodeSelected)
        {
            constexpr double tol=1e-8;
            for(const auto &wire:m_wires)
            {
                const NumericalEM::Vec3 a{wire.aM.x(),wire.aM.y(),wire.azM},b{wire.bM.x(),wire.bM.y(),wire.bzM};
                if(NumericalEM::norm(node-a)<=tol){appendRef(0,wire.id,1);break;}
                if(NumericalEM::norm(node-b)<=tol){appendRef(0,wire.id,2);break;}
            }
        }
        for(int i:wires)if(i>=0&&i<static_cast<int>(m_wires.size()))appendRef(0,m_wires[static_cast<std::size_t>(i)].id,0);
        for(int i:feeds)if(i>=0&&i<static_cast<int>(m_feeds.size()))appendRef(1,m_feeds[static_cast<std::size_t>(i)].id,0);
        for(int i:planes)if(i>=0&&i<static_cast<int>(m_planes.size()))appendRef(2,m_planes[static_cast<std::size_t>(i)].id,0);
        for(int i:dielectrics)if(i>=0&&i<static_cast<int>(m_dielectrics.size()))appendRef(3,m_dielectrics[static_cast<std::size_t>(i)].id,0);
        if(refs.empty())return;
        auto selectData=[](QComboBox *combo,const QString &data){if(!combo)return false;const int i=combo->findData(data);if(i<0)return false;combo->setCurrentIndex(i);return true;};
        selectData(m_constraintRefA,refs.front());
        if(refs.size()>1)selectData(m_constraintRefB,refs[1]);
        else
        {
            int fallback=-1;for(int i=0;i<m_constraintRefB->count();++i)if(m_constraintRefB->itemData(i).toString()!=refs.front()){fallback=i;break;}
            if(fallback>=0)m_constraintRefB->setCurrentIndex(fallback);
        }
        // One picked reference naturally starts as an axis lock; two or more start as a generic
        // coincident-point relation. The full constraint type/value remains editable before Add.
        m_constraintType->setCurrentIndex(refs.size()>1?0:13);
        workspaceTabs->setCurrentWidget(geometryWorkspace);geometryWorkspace->setCurrentIndex(2);
    };
    m_canvas->constraintSetupRequested=setupConstraintFromViewport;
    if(m_geometry3D)m_geometry3D->constraintSetupRequested=setupConstraintFromViewport;

    m_canvas->wireCreated = [this](const QPointF &aM, const QPointF &bM) { addWire(aM, bM); };
    m_canvas->feedCreated = [this](const QPointF &pM) { addFeed(pM); };
    m_canvas->deleteRequested = [this](const QPointF &pM) { deleteNearest(pM); };
    m_canvas->nodeMoveRequested = [this](const QPointF &oldM, double hiddenM, const QPointF &newM) { moveNode(oldM, hiddenM, newM); };
    m_canvas->objectDeleteRequested = [this](const std::vector<int>&w,const std::vector<int>&f,const std::vector<int>&p,const std::vector<int>&d){deleteGeometrySelection3D(w,f,p,d,false,{});};
    m_canvas->objectTranslateRequested = [this](const std::vector<int>&w,const std::vector<int>&f,const std::vector<int>&p,const std::vector<int>&d,const NumericalEM::Vec3&delta){
        translateSelection3D(w,f,p,d,{QPointF(delta.x,delta.y),delta.z});
        if(m_geometry3D)m_geometry3D->setObjectSelection(w,f,p,d);
    };
    if(m_geometry3D)
    {
        m_geometry3D->wireCreated=[this](const NumericalEM::Vec3&a,const NumericalEM::Vec3&b){addWire3D({QPointF(a.x,a.y),a.z},{QPointF(b.x,b.y),b.z});};
        m_geometry3D->feedCreated=[this](const NumericalEM::Vec3&p){addFeed3D({QPointF(p.x,p.y),p.z});};
        m_geometry3D->rectPecCreated=[this](const NumericalEM::Vec3&a,const NumericalEM::Vec3&b){addRectPec3D({QPointF(a.x,a.y),a.z},{QPointF(b.x,b.y),b.z});};
        m_geometry3D->dielectricCreated=[this,quickSubThicknessMm,quickSubEr](const NumericalEM::Vec3&a,const NumericalEM::Vec3&b){addDielectricSlab3D({QPointF(a.x,a.y),a.z},{QPointF(b.x,b.y),b.z},quickSubThicknessMm->value()/MmPerM,quickSubEr->value());};
        m_geometry3D->pivotPicked=[this,pivotCombo,pivotXmm,pivotYmm,pivotZmm,editorToolCombo](const NumericalEM::Vec3&p){
            pivotCombo->setCurrentIndex(3);pivotXmm->setValue(p.x*MmPerM);pivotYmm->setValue(p.y*MmPerM);pivotZmm->setValue(p.z*MmPerM);
            {QSignalBlocker blocker(editorToolCombo);editorToolCombo->setCurrentIndex(0);}
            m_canvas->setTool(Canvas::Tool::Select);if(m_geometry3D)m_geometry3D->setTool(Geometry3DView::Tool::Select);
        };
        m_geometry3D->planeResizeRequested=[this](int index,const NumericalEM::Vec3&c,double w,double h){resizeRectPec3D(index,{QPointF(c.x,c.y),c.z},w,h);};
        m_geometry3D->dielectricResizeRequested=[this](int index,const NumericalEM::Vec3&c,double w,double h){resizeDielectric3D(index,{QPointF(c.x,c.y),c.z},w,h);};
        m_geometry3D->dielectricThicknessRequested=[this](int index,const NumericalEM::Vec3&c,double t){resizeDielectricThickness3D(index,{QPointF(c.x,c.y),c.z},t);};
        m_geometry3D->planeRadiusRequested=[this](int index,double radius){resizePlaneRadius3D(index,radius);};
        m_geometry3D->wireDimensionsRequested=[this](int index,double length,double radius){resizeWire3D(index,length,radius);};
        m_geometry3D->feedElectricalRequested=[this](int index,double voltageV,double phaseDeg,double referenceOhm){updateFeedExcitation3D(index,voltageV,phaseDeg,referenceOhm);};
        m_geometry3D->deleteRequested=[this](const NumericalEM::Vec3&p){deleteNearest3D({QPointF(p.x,p.y),p.z});};
        m_geometry3D->geometryDeleteSelectionRequested=[this](const std::vector<int>&w,const std::vector<int>&f,const std::vector<int>&p,const std::vector<int>&d,bool node,const NumericalEM::Vec3&q){deleteGeometrySelection3D(w,f,p,d,node,{QPointF(q.x,q.y),q.z});};
        m_geometry3D->nodeMoveRequested=[this](const NumericalEM::Vec3&a,const NumericalEM::Vec3&b){moveNode3D({QPointF(a.x,a.y),a.z},{QPointF(b.x,b.y),b.z});};
        m_geometry3D->feedMoveRequested=[this](int index,const NumericalEM::Vec3&p){moveFeed3D(index,{QPointF(p.x,p.y),p.z});};
        m_geometry3D->wireTranslateRequested=[this](int index,const NumericalEM::Vec3&d){translateWire3D(index,{QPointF(d.x,d.y),d.z});};
        m_geometry3D->planeTranslateRequested=[this](int index,const NumericalEM::Vec3&d){translatePlane3D(index,{QPointF(d.x,d.y),d.z});};
        m_geometry3D->dielectricTranslateRequested=[this](int index,const NumericalEM::Vec3&d){translateDielectric3D(index,{QPointF(d.x,d.y),d.z});};
        m_geometry3D->groupTranslateRequested=[this](const std::vector<int>&w,const std::vector<int>&f,const std::vector<int>&p,const std::vector<int>&d,const NumericalEM::Vec3&delta){translateSelection3D(w,f,p,d,{QPointF(delta.x,delta.y),delta.z});};
        m_geometry3D->groupRotateRequested=[this](const std::vector<int>&w,const std::vector<int>&f,const std::vector<int>&p,const std::vector<int>&d,int axis,double angle,const NumericalEM::Vec3&pivot){if(!m_geometry3D)return;m_geometry3D->setObjectSelection(w,f,p,d);rotate3DSelection(axis,angle,{QPointF(pivot.x,pivot.y),pivot.z});};
        m_geometry3D->constraintSelected=[this](int index){selectGeometryConstraint(index);};
        m_geometry3D->constraintEditRequested=[this](int index){editGeometryConstraintValue(index);};
        m_geometry3D->constraintDeleteRequested=[this](int index){removeGeometryConstraintIndex(index);};
    }

    QObject::connect(m_editPlane, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { const double coord=m_activePlaneCoordinateM?m_activePlaneCoordinateM->value():0.0;const double grid=m_gridMm?m_gridMm->value():50.0;if(m_canvas)m_canvas->setEditContext(m_editPlane->currentIndex(),coord,grid);if(m_geometry3D)m_geometry3D->setEditContext(m_editPlane->currentIndex(),coord,grid); rebuildScene(true); });
    QObject::connect(m_activePlaneCoordinateM, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { const int plane=m_editPlane?m_editPlane->currentIndex():0;const double grid=m_gridMm?m_gridMm->value():50.0;if(m_canvas)m_canvas->setEditContext(plane,m_activePlaneCoordinateM->value(),grid);if(m_geometry3D)m_geometry3D->setEditContext(plane,m_activePlaneCoordinateM->value(),grid); rebuildScene(false); });
    m_canvas->setGridMm(m_gridMm->value());
    if(m_canvas)m_canvas->setEditContext(m_editPlane?m_editPlane->currentIndex():0,m_activePlaneCoordinateM?m_activePlaneCoordinateM->value():0.0,m_gridMm->value());
    if(m_geometry3D)m_geometry3D->setEditContext(m_editPlane?m_editPlane->currentIndex():0,m_activePlaneCoordinateM?m_activePlaneCoordinateM->value():0.0,m_gridMm->value());
    if(m_canvas)m_canvas->setToolSelectorIndex(editorToolCombo->currentIndex());
    if(m_geometry3D)m_geometry3D->setToolSelectorIndex(editorToolCombo->currentIndex());
    if (m_surfacePortFeed) m_surfacePortFeed->setEnabled(m_surfaceExcitationMode && m_surfaceExcitationMode->currentIndex()!=0);
    generateHalfWaveDipole();
    refreshYagiIndividualVariables();
}

void AntennaDesignerWidget::setShowPhysicalWireRadius(bool enabled)
{
    if(m_showPhysicalWireRadius==enabled)return;
    m_showPhysicalWireRadius=enabled;
    if(m_geometry3D)m_geometry3D->setShowPhysicalWireRadius(enabled);
    rebuildScene(false);
}

QPointF AntennaDesignerWidget::snapToExistingNode(const QPointF &pM, double toleranceM) const
{
    const int plane = m_editPlane ? m_editPlane->currentIndex() : 0;
    const double active = m_activePlaneCoordinateM ? m_activePlaneCoordinateM->value() : 0.0;
    QPointF best = pM;
    double bestD = toleranceM;
    auto consider = [&](const QPointF &xy, double z) {
        if (std::abs(hiddenEditorCoordinate(xy,z,plane)-active) > toleranceM) return;
        const QPointF q=projectEditorPoint(xy,z,plane);
        const double d=std::hypot(pM.x()-q.x(),pM.y()-q.y());
        if(d<bestD){bestD=d;best=q;}
    };
    for(const auto&w:m_wires){consider(w.aM,w.azM);consider(w.bM,w.bzM);}
    for(const auto&f:m_feeds)consider(f.positionM,f.zM);
    return best;
}

QPointF AntennaDesignerWidget::snapFeedToWire(const QPointF &pM, double toleranceM) const
{
    const int plane=m_editPlane?m_editPlane->currentIndex():0;
    const double active=m_activePlaneCoordinateM?m_activePlaneCoordinateM->value():0.0;
    QPointF best=snapToExistingNode(pM,toleranceM);
    double bestD=std::hypot(best.x()-pM.x(),best.y()-pM.y());
    for(const auto&w:m_wires)
    {
        // Mouse feed placement intentionally snaps only to conductors lying in the active edit plane.
        if(std::abs(hiddenEditorCoordinate(w.aM,w.azM,plane)-active)>toleranceM ||
           std::abs(hiddenEditorCoordinate(w.bM,w.bzM,plane)-active)>toleranceM) continue;
        QPointF q;
        const double d=pointSegmentDistance(pM,projectEditorPoint(w.aM,w.azM,plane),projectEditorPoint(w.bM,w.bzM,plane),&q);
        if(d<bestD && d<=toleranceM){bestD=d;best=q;}
    }
    return best;
}

void AntennaDesignerWidget::addWire(const QPointF &aMIn, const QPointF &bMIn)
{
    const int plane=m_editPlane?m_editPlane->currentIndex():0;
    const double active=m_activePlaneCoordinateM?m_activePlaneCoordinateM->value():0.0;
    const double tol=std::max(0.001,m_gridMm->value()/MmPerM*0.6);
    const QPointF aProjected=snapToExistingNode(aMIn,tol);
    const QPointF bProjected=snapToExistingNode(bMIn,tol);
    WireElement w;
    w.name=QStringLiteral("W%1").arg(m_wires.size()+1);
    w.radiusM=m_wireRadiusMm->value()/MmPerM;
    setHiddenEditorCoordinate(w.aM,w.azM,plane,active);
    setHiddenEditorCoordinate(w.bM,w.bzM,plane,active);
    assignEditorProjection(w.aM,w.azM,plane,aProjected);
    assignEditorProjection(w.bM,w.bzM,plane,bProjected);
    if(segmentLength3D(w.aM,w.azM,w.bM,w.bzM)<1e-6)return;
    pushGeometryHistory();
    m_wires.push_back(w);
    rebuildScene(false);
}

void AntennaDesignerWidget::addFeed(const QPointF &positionMIn)
{
    const int plane=m_editPlane?m_editPlane->currentIndex():0;
    const double active=m_activePlaneCoordinateM?m_activePlaneCoordinateM->value():0.0;
    const double tol=std::max(0.01,m_gridMm->value()/MmPerM*0.75);
    FeedPoint f;
    f.name=QStringLiteral("F%1").arg(m_feeds.size()+1);
    setHiddenEditorCoordinate(f.positionM,f.zM,plane,active);
    assignEditorProjection(f.positionM,f.zM,plane,snapFeedToWire(positionMIn,tol));
    f.voltageV=m_feedVoltageV->value();f.phaseDeg=m_feedPhaseDeg->value();f.sourceOhm=m_feedSourceOhm->value();
    pushGeometryHistory();
    m_feeds.push_back(f);
    rebuildScene(false);
}

void AntennaDesignerWidget::deleteNearest(const QPointF &positionM)
{
    const int plane=m_editPlane?m_editPlane->currentIndex():0;
    const double active=m_activePlaneCoordinateM?m_activePlaneCoordinateM->value():0.0;
    double best=std::numeric_limits<double>::infinity();bool feedBest=false;std::size_t bestIndex=0;
    for(std::size_t i=0;i<m_wires.size();++i)
    {
        QPointF q;
        const QPointF a=projectEditorPoint(m_wires[i].aM,m_wires[i].azM,plane),b=projectEditorPoint(m_wires[i].bM,m_wires[i].bzM,plane);
        const double d2=pointSegmentDistance(positionM,a,b,&q);
        const double hidden=0.5*(hiddenEditorCoordinate(m_wires[i].aM,m_wires[i].azM,plane)+hiddenEditorCoordinate(m_wires[i].bM,m_wires[i].bzM,plane));
        const double d=std::hypot(d2,hidden-active);
        if(d<best){best=d;feedBest=false;bestIndex=i;}
    }
    for(std::size_t i=0;i<m_feeds.size();++i)
    {
        const QPointF q=projectEditorPoint(m_feeds[i].positionM,m_feeds[i].zM,plane);
        const double d=std::sqrt(std::pow(positionM.x()-q.x(),2)+std::pow(positionM.y()-q.y(),2)+std::pow(hiddenEditorCoordinate(m_feeds[i].positionM,m_feeds[i].zM,plane)-active,2));
        if(d<best){best=d;feedBest=true;bestIndex=i;}
    }
    const double threshold=std::max(0.02,m_gridMm->value()/MmPerM);
    if(best>threshold)return;
    pushGeometryHistory();
    if(feedBest&&bestIndex<m_feeds.size())m_feeds.erase(m_feeds.begin()+static_cast<std::ptrdiff_t>(bestIndex));
    else if(!feedBest&&bestIndex<m_wires.size())m_wires.erase(m_wires.begin()+static_cast<std::ptrdiff_t>(bestIndex));
    rebuildScene(false);
}

void AntennaDesignerWidget::moveNode(const QPointF &oldProjectedM, double oldHiddenM, const QPointF &newProjectedM)
{
    const int plane=m_editPlane?m_editPlane->currentIndex():0;
    constexpr double sameTol=1e-8;
    const double snapTol=std::max(0.001,m_gridMm->value()/MmPerM*0.60);
    QPointF oldXY;double oldZ=0.0;setHiddenEditorCoordinate(oldXY,oldZ,plane,oldHiddenM);assignEditorProjection(oldXY,oldZ,plane,oldProjectedM);
    QPointF newXY=oldXY;double newZ=oldZ;assignEditorProjection(newXY,newZ,plane,newProjectedM);
    double bestD=snapTol;
    auto consider=[&](const QPointF &xy,double z){
        if(distance3D(xy,z,oldXY,oldZ)<=sameTol)return;
        const double d=distance3D(xy,z,newXY,newZ);if(d<bestD){bestD=d;newXY=xy;newZ=z;}
    };
    for(const auto&w:m_wires){consider(w.aM,w.azM);consider(w.bM,w.bzM);}for(const auto&f:m_feeds)consider(f.positionM,f.zM);
    if(distance3D(newXY,newZ,oldXY,oldZ)<=sameTol){rebuildScene(false);return;}
    pushGeometryHistory();
    bool changed=false;
    auto moveIfSame=[&](QPointF &xy,double &z){if(distance3D(xy,z,oldXY,oldZ)<=sameTol){xy=newXY;z=newZ;changed=true;}};
    for(auto&w:m_wires){moveIfSame(w.aM,w.azM);moveIfSame(w.bM,w.bzM);}for(auto&f:m_feeds)moveIfSame(f.positionM,f.zM);
    // 2.6 regression semantic equivalent (now extended with Z): for (auto &w : m_wires) { moveIfSame(w.aM); moveIfSame(w.bM); }
    // 2.6 regression semantic equivalent (now extended with Z): for (auto &f : m_feeds) moveIfSame(f.positionM);
    if(!changed){if(!m_undoHistory.empty())m_undoHistory.pop_back();rebuildScene(false);return;}
    m_wires.erase(std::remove_if(m_wires.begin(),m_wires.end(),[](const WireElement&w){return segmentLength3D(w.aM,w.azM,w.bM,w.bzM)<1e-6;}),m_wires.end());
    rebuildScene(false);
}

AntennaDesignerWidget::GeometrySnapshot AntennaDesignerWidget::geometrySnapshot() const
{
    GeometrySnapshot s;s.wires=m_wires;s.feeds=m_feeds;s.planes=m_planes;s.dielectrics=m_dielectrics;s.constraints=m_constraints;s.groups=m_groups;return s;
}

void AntennaDesignerWidget::pushGeometryHistory()
{
    if(m_restoringGeometryHistory)return;
    m_undoHistory.push_back(geometrySnapshot());
    if(m_undoHistory.size()>100)m_undoHistory.erase(m_undoHistory.begin());
    m_redoHistory.clear();
}

void AntennaDesignerWidget::restoreGeometrySnapshot(const GeometrySnapshot &snapshot)
{
    m_restoringGeometryHistory=true;m_wires=snapshot.wires;m_feeds=snapshot.feeds;m_planes=snapshot.planes;m_dielectrics=snapshot.dielectrics;m_constraints=snapshot.constraints;m_groups=snapshot.groups;rebuildScene(false);m_restoringGeometryHistory=false;
}

void AntennaDesignerWidget::undoGeometry()
{
    if(m_undoHistory.empty())return;
    m_redoHistory.push_back(geometrySnapshot());const auto target=m_undoHistory.back();m_undoHistory.pop_back();restoreGeometrySnapshot(target);
}

void AntennaDesignerWidget::redoGeometry()
{
    if(m_redoHistory.empty())return;
    m_undoHistory.push_back(geometrySnapshot());const auto target=m_redoHistory.back();m_redoHistory.pop_back();restoreGeometrySnapshot(target);
}

AntennaDesignerWidget::GeometryPoint AntennaDesignerWidget::snapPoint3D(const GeometryPoint &point,double toleranceM) const
{
    GeometryPoint best=point;double bestD=toleranceM;
    auto consider=[&](const QPointF &xy,double z){const double d=distance3D(point.xyM,point.zM,xy,z);if(d<bestD){bestD=d;best={xy,z};}};
    for(const auto&w:m_wires){consider(w.aM,w.azM);consider(w.bM,w.bzM);}for(const auto&f:m_feeds)consider(f.positionM,f.zM);
    return best;
}

AntennaDesignerWidget::GeometryPoint AntennaDesignerWidget::snapPointToWireGeometry3D(const GeometryPoint &point,double toleranceM,bool splitWire,
                                                                                          const std::set<QString> &ignoredObjectIds)
{
    GeometryPoint best=point;double bestD=toleranceM;std::size_t bestWire=m_wires.size();double bestT=0.0;
    const NumericalEM::Vec3 p{point.xyM.x(),point.xyM.y(),point.zM};
    auto consider=[&](const NumericalEM::Vec3&q,std::size_t wireIndex,double t){const double d=NumericalEM::norm(p-q);if(d<bestD){bestD=d;best={{q.x,q.y},q.z};bestWire=wireIndex;bestT=t;}};
    for(std::size_t i=0;i<m_wires.size();++i)
    {
        const auto&w=m_wires[i];if(ignoredObjectIds.count(w.id))continue;
        const NumericalEM::Vec3 a{w.aM.x(),w.aM.y(),w.azM},b{w.bM.x(),w.bM.y(),w.bzM},ab=b-a;
        consider(a,i,0.0);consider(b,i,1.0);const double d2=NumericalEM::dot(ab,ab);
        if(d2>1e-30){const double t=std::clamp(NumericalEM::dot(p-a,ab)/d2,0.0,1.0);consider(a+ab*t,i,t);}
    }
    for(const auto&f:m_feeds)if(!ignoredObjectIds.count(f.id)){const NumericalEM::Vec3 q{f.positionM.x(),f.positionM.y(),f.zM};const double d=NumericalEM::norm(p-q);if(d<bestD){bestD=d;best={{q.x,q.y},q.z};bestWire=m_wires.size();bestT=0.0;}}
    if(splitWire&&bestWire<m_wires.size()&&bestT>1e-6&&bestT<1.0-1e-6)
    {
        const QString originalId=m_wires[bestWire].id;WireElement second=m_wires[bestWire];
        m_wires[bestWire].bM=best.xyM;m_wires[bestWire].bzM=best.zM;
        second.aM=best.xyM;second.azM=best.zM;second.name=m_wires[bestWire].name+QStringLiteral("_B");
        second.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_wires.insert(m_wires.begin()+static_cast<std::ptrdiff_t>(bestWire+1),second);
        for(auto&g:m_groups)if(std::find(g.objectIds.begin(),g.objectIds.end(),originalId)!=g.objectIds.end())g.objectIds.push_back(second.id);
        // Endpoint-B constraints still refer to the physical old B endpoint, which now belongs to
        // the second segment. Endpoint-A and whole-wire references deliberately remain on segment A.
        auto transferOldB=[&](ConstraintRef &ref){if(ref.kind==0&&ref.id==originalId&&ref.anchor==2)ref.id=second.id;};
        for(auto&constraint:m_constraints){transferOldB(constraint.a);transferOldB(constraint.b);}
    }
    return best;
}

void AntennaDesignerWidget::splitWiresAtSelectedEndpoints(const std::set<int> &selectedWireIndices)
{
    if(!m_magneticSnapEnabled||selectedWireIndices.empty())return;
    std::set<QString> selectedIds;std::vector<GeometryPoint>endpoints;
    for(int i:selectedWireIndices)if(i>=0&&i<static_cast<int>(m_wires.size())){const auto&w=m_wires[static_cast<std::size_t>(i)];selectedIds.insert(w.id);endpoints.push_back({w.aM,w.azM});endpoints.push_back({w.bM,w.bzM});}
    for(const auto&p:endpoints)snapPointToWireGeometry3D(p,1e-7,true,selectedIds);
}

AntennaDesignerWidget::GeometryPoint AntennaDesignerWidget::snapFeedPoint3D(const GeometryPoint &point,double toleranceM,bool splitWire)
{
    GeometryPoint best=snapPoint3D(point,toleranceM);double bestD=distance3D(point.xyM,point.zM,best.xyM,best.zM);std::size_t bestWire=m_wires.size();double bestT=0.0;
    NumericalEM::Vec3 p{point.xyM.x(),point.xyM.y(),point.zM};
    for(std::size_t i=0;i<m_wires.size();++i)
    {
        const auto&w=m_wires[i];const NumericalEM::Vec3 a{w.aM.x(),w.aM.y(),w.azM},b{w.bM.x(),w.bM.y(),w.bzM},ab=b-a;const double d2=NumericalEM::dot(ab,ab);if(!(d2>1e-30))continue;
        const double t=std::clamp(NumericalEM::dot(p-a,ab)/d2,0.0,1.0);const auto q=a+ab*t;const double d=NumericalEM::norm(p-q);
        if(d<=toleranceM&&d<bestD){bestD=d;best={{q.x,q.y},q.z};bestWire=i;bestT=t;}
    }
    if(splitWire&&bestWire<m_wires.size()&&bestT>1e-6&&bestT<1.0-1e-6)
    {
        const WireElement original=m_wires[bestWire];WireElement second=original;
        m_wires[bestWire].bM=best.xyM;m_wires[bestWire].bzM=best.zM;
        second.aM=best.xyM;second.azM=best.zM;second.name=original.name+QStringLiteral("_B");
        second.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_wires.insert(m_wires.begin()+static_cast<std::ptrdiff_t>(bestWire+1),second);
        for(auto&g:m_groups)if(std::find(g.objectIds.begin(),g.objectIds.end(),original.id)!=g.objectIds.end())g.objectIds.push_back(second.id);
        auto transferOldB=[&](ConstraintRef &ref){if(ref.kind==0&&ref.id==original.id&&ref.anchor==2)ref.id=second.id;};
        for(auto&constraint:m_constraints){transferOldB(constraint.a);transferOldB(constraint.b);}
    }
    return best;
}

void AntennaDesignerWidget::addWire3D(const GeometryPoint &aIn,const GeometryPoint &bIn)
{
    GeometryPoint a=aIn,b=bIn;
    if(m_magneticSnapEnabled)
    {
        // The viewport already applies a 12 px magnetic tolerance; this second pass mainly
        // guarantees exact topological welding and splits a target wire when a T-junction lands
        // in its interior.
        a=snapPointToWireGeometry3D(aIn,1e-6,false);
        b=snapPointToWireGeometry3D(bIn,1e-6,false);
    }
    if(distance3D(a.xyM,a.zM,b.xyM,b.zM)<1e-6)return;
    pushGeometryHistory();
    if(m_magneticSnapEnabled)
    {
        a=snapPointToWireGeometry3D(a,1e-6,true);
        b=snapPointToWireGeometry3D(b,1e-6,true);
    }
    WireElement w;w.name=QStringLiteral("W%1").arg(m_wires.size()+1);w.aM=a.xyM;w.azM=a.zM;w.bM=b.xyM;w.bzM=b.zM;w.radiusM=m_wireRadiusMm->value()/MmPerM;m_wires.push_back(w);rebuildScene(false);
}

void AntennaDesignerWidget::addFeed3D(const GeometryPoint &position)
{
    pushGeometryHistory();GeometryPoint p=position;
    // The viewport performs the human-friendly 12 px attraction. The controller only welds an
    // already-snapped point exactly, so Free placement never acquires a hidden grid-sized snap.
    if(m_magneticSnapEnabled)p=snapFeedPoint3D(position,1e-6,true);
    FeedPoint f;f.name=QStringLiteral("F%1").arg(m_feeds.size()+1);f.positionM=p.xyM;f.zM=p.zM;f.voltageV=m_feedVoltageV->value();f.phaseDeg=m_feedPhaseDeg->value();f.sourceOhm=m_feedSourceOhm->value();m_feeds.push_back(f);rebuildScene(false);
}


void AntennaDesignerWidget::addRectPec3D(const GeometryPoint &a,const GeometryPoint &b)
{
    const int plane=m_editPlane?m_editPlane->currentIndex():0;
    const NumericalEM::Vec3 av{a.xyM.x(),a.xyM.y(),a.zM},bv{b.xyM.x(),b.xyM.y(),b.zM},delta=bv-av,cv=(av+bv)*0.5;
    const Matrix3 frame=surfaceFrameMatrix(plane,m_constructionPlaneRotZDeg,m_constructionPlaneRotYDeg,m_constructionPlaneRotXDeg);
    const NumericalEM::Vec3 u{frame.m[0][0],frame.m[1][0],frame.m[2][0]},v{frame.m[0][1],frame.m[1][1],frame.m[2][1]};
    const double width=std::abs(NumericalEM::dot(delta,u)),height=std::abs(NumericalEM::dot(delta,v));GeometryPoint center{{cv.x,cv.y},cv.z};
    if(width<1e-9||height<1e-9)return;
    pushGeometryHistory();
    PlaneElement pl;pl.name=QStringLiteral("PEC%1").arg(m_planes.size()+1);pl.surfaceType=0;pl.centerM=center.xyM;pl.zM=center.zM;pl.orientation=plane;pl.yawDeg=m_constructionPlaneRotZDeg;pl.pitchDeg=m_constructionPlaneRotYDeg;pl.rollDeg=m_constructionPlaneRotXDeg;pl.widthM=width;pl.heightM=height;
    pl.meshHintM=std::max(1e-6,m_gridMm?m_gridMm->value()/MmPerM:0.05);
    m_planes.push_back(pl);rebuildScene(false);
    if(m_geometry3D)m_geometry3D->setObjectSelection({}, {}, {static_cast<int>(m_planes.size()-1)}, {});
}

void AntennaDesignerWidget::addDielectricSlab3D(const GeometryPoint &a,const GeometryPoint &b,double thicknessM,double relativePermittivity)
{
    const int plane=m_editPlane?m_editPlane->currentIndex():0;
    const NumericalEM::Vec3 av{a.xyM.x(),a.xyM.y(),a.zM},bv{b.xyM.x(),b.xyM.y(),b.zM},delta=bv-av,cv=(av+bv)*0.5;
    const Matrix3 frame=surfaceFrameMatrix(plane,m_constructionPlaneRotZDeg,m_constructionPlaneRotYDeg,m_constructionPlaneRotXDeg);
    const NumericalEM::Vec3 u{frame.m[0][0],frame.m[1][0],frame.m[2][0]},v{frame.m[0][1],frame.m[1][1],frame.m[2][1]};
    const double width=std::abs(NumericalEM::dot(delta,u)),height=std::abs(NumericalEM::dot(delta,v));GeometryPoint center{{cv.x,cv.y},cv.z};
    if(width<1e-9||height<1e-9)return;
    pushGeometryHistory();
    DielectricElement d;d.name=QStringLiteral("SUB%1").arg(m_dielectrics.size()+1);d.centerM=center.xyM;d.zM=center.zM;d.orientation=plane;d.yawDeg=m_constructionPlaneRotZDeg;d.pitchDeg=m_constructionPlaneRotYDeg;d.rollDeg=m_constructionPlaneRotXDeg;d.widthM=width;d.heightM=height;
    d.thicknessM=std::max(1e-9,thicknessM);d.relativePermittivity=std::max(1.0,relativePermittivity);d.lossTangent=0.02;d.fieldFillFactor=0.65;
    m_dielectrics.push_back(d);rebuildScene(false);
    if(m_geometry3D)m_geometry3D->setObjectSelection({}, {}, {}, {static_cast<int>(m_dielectrics.size()-1)});
}

void AntennaDesignerWidget::updateFeedExcitation3D(int feedIndex,double voltageV,double phaseDeg,double referenceOhm)
{
    if(feedIndex<0||feedIndex>=static_cast<int>(m_feeds.size())||!std::isfinite(voltageV)||!std::isfinite(phaseDeg)||!std::isfinite(referenceOhm)||voltageV<0.0||referenceOhm<=0.0)return;
    auto &f=m_feeds[static_cast<std::size_t>(feedIndex)];
    const auto same=[](double a,double b){return std::abs(a-b)<=1e-12*std::max({1.0,std::abs(a),std::abs(b)});};
    if(same(f.voltageV,voltageV)&&same(f.phaseDeg,phaseDeg)&&same(f.sourceOhm,referenceOhm))return;
    pushGeometryHistory();
    f.voltageV=voltageV;f.phaseDeg=phaseDeg;f.sourceOhm=referenceOhm;
    // Electrical feed edits do not alter CAD topology or geometry constraints. Keep the current
    // graphical selection while synchronizing both feed tables and invalidating stale EM results.
    refreshTables();
    clearMomResults();clearSweepResults();clearOptimizationResults();clearMultiOptimizationResults();
    if(m_geometry3D)m_geometry3D->setGeometry(m_wires,m_feeds,m_planes,m_dielectrics);
}

void AntennaDesignerWidget::resizeWire3D(int wireIndex,double lengthM,double radiusM)
{
    if(wireIndex<0||wireIndex>=static_cast<int>(m_wires.size())||lengthM<=1e-9||radiusM<=1e-9)return;
    auto &w=m_wires[static_cast<std::size_t>(wireIndex)];const WireElement old=w;
    const NumericalEM::Vec3 a{old.aM.x(),old.aM.y(),old.azM},b{old.bM.x(),old.bM.y(),old.bzM},ab=b-a;const double oldLength=NumericalEM::norm(ab);if(oldLength<=1e-12)return;
    const NumericalEM::Vec3 dir=ab*(1.0/oldLength),mid=(a+b)*0.5,newA=mid-dir*(0.5*lengthM),newB=mid+dir*(0.5*lengthM);
    const double tol=std::max(1e-7,m_gridMm?m_gridMm->value()/MmPerM*0.20:1e-5);
    std::vector<std::pair<int,double>> attachedFeeds;
    const double ab2=NumericalEM::dot(ab,ab);
    for(int fi=0;fi<static_cast<int>(m_feeds.size());++fi)
    {
        const auto&f=m_feeds[static_cast<std::size_t>(fi)];const NumericalEM::Vec3 fp{f.positionM.x(),f.positionM.y(),f.zM};double t=0.0,dist=NumericalEM::norm(fp-a);
        if(ab2>1e-30){t=std::clamp(NumericalEM::dot(fp-a,ab)/ab2,0.0,1.0);dist=NumericalEM::norm(fp-(a+ab*t));}
        if(dist<=tol)attachedFeeds.emplace_back(fi,t);
    }
    pushGeometryHistory();w.aM={newA.x,newA.y};w.azM=newA.z;w.bM={newB.x,newB.y};w.bzM=newB.z;w.radiusM=radiusM;
    for(const auto &[fi,t]:attachedFeeds){const auto q=newA+(newB-newA)*t;auto&f=m_feeds[static_cast<std::size_t>(fi)];f.positionM={q.x,q.y};f.zM=q.z;}
    rebuildScene(false);if(m_geometry3D)m_geometry3D->setObjectSelection({wireIndex},{},{},{});
}

void AntennaDesignerWidget::resizeRectPec3D(int planeIndex,const GeometryPoint &center,double widthM,double heightM)
{
    if(planeIndex<0||planeIndex>=static_cast<int>(m_planes.size())||widthM<=1e-9||heightM<=1e-9)return;
    auto &pl=m_planes[static_cast<std::size_t>(planeIndex)];if(pl.surfaceType!=0)return;
    pushGeometryHistory();pl.centerM=center.xyM;pl.zM=center.zM;pl.widthM=widthM;pl.heightM=heightM;rebuildScene(false);
    if(m_geometry3D)m_geometry3D->setObjectSelection({}, {}, {planeIndex}, {});
}

void AntennaDesignerWidget::resizeDielectric3D(int dielectricIndex,const GeometryPoint &center,double widthM,double heightM)
{
    if(dielectricIndex<0||dielectricIndex>=static_cast<int>(m_dielectrics.size())||widthM<=1e-9||heightM<=1e-9)return;
    auto &d=m_dielectrics[static_cast<std::size_t>(dielectricIndex)];
    pushGeometryHistory();d.centerM=center.xyM;d.zM=center.zM;d.widthM=widthM;d.heightM=heightM;rebuildScene(false);
    if(m_geometry3D)m_geometry3D->setObjectSelection({}, {}, {}, {dielectricIndex});
}

void AntennaDesignerWidget::resizeDielectricThickness3D(int dielectricIndex,const GeometryPoint &center,double thicknessM)
{
    if(dielectricIndex<0||dielectricIndex>=static_cast<int>(m_dielectrics.size())||thicknessM<=1e-9)return;
    auto &d=m_dielectrics[static_cast<std::size_t>(dielectricIndex)];
    pushGeometryHistory();d.centerM=center.xyM;d.zM=center.zM;d.thicknessM=thicknessM;rebuildScene(false);
    if(m_geometry3D)m_geometry3D->setObjectSelection({}, {}, {}, {dielectricIndex});
}

void AntennaDesignerWidget::resizePlaneRadius3D(int planeIndex,double radiusM)
{
    if(planeIndex<0||planeIndex>=static_cast<int>(m_planes.size())||radiusM<=1e-9)return;
    auto &pl=m_planes[static_cast<std::size_t>(planeIndex)];if(pl.surfaceType<1||pl.surfaceType>4)return;
    pushGeometryHistory();pl.radiusM=radiusM;if(pl.innerRadiusM>=pl.radiusM)pl.innerRadiusM=std::max(0.0,0.8*pl.radiusM);rebuildScene(false);
    if(m_geometry3D)m_geometry3D->setObjectSelection({}, {}, {planeIndex}, {});
}

void AntennaDesignerWidget::coincideSelectedSurfaceCenters3D()
{
    if(!m_geometry3D)return;const auto planes=m_geometry3D->selectedPlanes(),dielectrics=m_geometry3D->selectedDielectrics();
    int count=0;NumericalEM::Vec3 target{};
    for(int i:planes)if(i>=0&&i<static_cast<int>(m_planes.size())){const auto&pl=m_planes[static_cast<std::size_t>(i)];target=target+NumericalEM::Vec3{pl.centerM.x(),pl.centerM.y(),pl.zM};++count;}
    for(int i:dielectrics)if(i>=0&&i<static_cast<int>(m_dielectrics.size())){const auto&d=m_dielectrics[static_cast<std::size_t>(i)];target=target+NumericalEM::Vec3{d.centerM.x(),d.centerM.y(),d.zM};++count;}
    if(count<2)return;target=target*(1.0/count);pushGeometryHistory();
    for(int i:planes)if(i>=0&&i<static_cast<int>(m_planes.size())){auto&pl=m_planes[static_cast<std::size_t>(i)];pl.centerM=QPointF(target.x,target.y);pl.zM=target.z;}
    for(int i:dielectrics)if(i>=0&&i<static_cast<int>(m_dielectrics.size())){auto&d=m_dielectrics[static_cast<std::size_t>(i)];d.centerM=QPointF(target.x,target.y);d.zM=target.z;}
    rebuildScene(false);m_geometry3D->setObjectSelection({}, {}, planes, dielectrics);
}

void AntennaDesignerWidget::matchSelectedSurfaceDimensions3D()
{
    if(!m_geometry3D)return;const auto planes=m_geometry3D->selectedPlanes(),dielectrics=m_geometry3D->selectedDielectrics();
    double refW=0.0,refH=0.0;bool haveRef=false;int eligible=0;
    for(int i:planes)if(i>=0&&i<static_cast<int>(m_planes.size())&&m_planes[static_cast<std::size_t>(i)].surfaceType==0){const auto&pl=m_planes[static_cast<std::size_t>(i)];if(!haveRef){refW=pl.widthM;refH=pl.heightM;haveRef=true;}++eligible;}
    for(int i:dielectrics)if(i>=0&&i<static_cast<int>(m_dielectrics.size())){const auto&d=m_dielectrics[static_cast<std::size_t>(i)];if(!haveRef){refW=d.widthM;refH=d.heightM;haveRef=true;}++eligible;}
    if(!haveRef||eligible<2)return;pushGeometryHistory();
    for(int i:planes)if(i>=0&&i<static_cast<int>(m_planes.size())){auto&pl=m_planes[static_cast<std::size_t>(i)];if(pl.surfaceType==0){pl.widthM=refW;pl.heightM=refH;}}
    for(int i:dielectrics)if(i>=0&&i<static_cast<int>(m_dielectrics.size())){auto&d=m_dielectrics[static_cast<std::size_t>(i)];d.widthM=refW;d.heightM=refH;}
    rebuildScene(false);m_geometry3D->setObjectSelection({}, {}, planes, dielectrics);
}


void AntennaDesignerWidget::ensureGeometryIds()
{
    std::set<QString> used;
    auto assign=[&](auto &container)
    {
        for(auto &v:container)
        {
            if(v.id.isEmpty() || used.count(v.id))
                v.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
            used.insert(v.id);
        }
    };
    assign(m_wires);assign(m_feeds);assign(m_planes);assign(m_dielectrics);
    std::set<QString> constraintIds;
    for(auto &c:m_constraints)
    {
        if(c.id.isEmpty() || constraintIds.count(c.id))c.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        constraintIds.insert(c.id);
    }
    std::set<QString> groupIds;
    for(auto &g:m_groups)
    {
        if(g.id.isEmpty() || groupIds.count(g.id))g.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        groupIds.insert(g.id);
        if(g.name.trimmed().isEmpty())g.name=QStringLiteral("Group %1").arg(groupIds.size());
    }
}

void AntennaDesignerWidget::pruneInvalidGeometryConstraints()
{
    auto exists=[&](const ConstraintRef &ref)
    {
        if(ref.id.isEmpty())return false;
        if(ref.kind==0)return std::any_of(m_wires.begin(),m_wires.end(),[&](const auto&w){return w.id==ref.id;});
        if(ref.kind==1)return std::any_of(m_feeds.begin(),m_feeds.end(),[&](const auto&f){return f.id==ref.id;});
        if(ref.kind==2)return std::any_of(m_planes.begin(),m_planes.end(),[&](const auto&p){return p.id==ref.id;});
        if(ref.kind==3)return std::any_of(m_dielectrics.begin(),m_dielectrics.end(),[&](const auto&d){return d.id==ref.id;});
        return false;
    };
    m_constraints.erase(std::remove_if(m_constraints.begin(),m_constraints.end(),[&](const GeometryConstraint &c){return !exists(c.a)||(c.type!=13&&!exists(c.b));}),m_constraints.end());
}


void AntennaDesignerWidget::pruneInvalidGeometryGroups()
{
    std::set<QString> validIds;
    for(const auto&w:m_wires)validIds.insert(w.id);
    for(const auto&f:m_feeds)validIds.insert(f.id);
    for(const auto&p:m_planes)validIds.insert(p.id);
    for(const auto&d:m_dielectrics)validIds.insert(d.id);
    for(auto &g:m_groups)
    {
        std::vector<QString> kept;std::set<QString> seen;
        for(const auto&id:g.objectIds)if(validIds.count(id)&&!seen.count(id)){kept.push_back(id);seen.insert(id);}
        g.objectIds=std::move(kept);
    }
    m_groups.erase(std::remove_if(m_groups.begin(),m_groups.end(),[](const GeometryGroup&g){return g.objectIds.size()<2;}),m_groups.end());
}

void AntennaDesignerWidget::expandSelectionByGroups(std::set<int> &wireIndices,std::set<int> &feedIndices,
                                                     std::set<int> &planeIndices,std::set<int> &dielectricIndices) const
{
    std::set<QString> selectedIds;
    auto collect=[&](const auto&container,const std::set<int>&indices){for(int i:indices)if(i>=0&&i<static_cast<int>(container.size()))selectedIds.insert(container[static_cast<std::size_t>(i)].id);};
    collect(m_wires,wireIndices);collect(m_feeds,feedIndices);collect(m_planes,planeIndices);collect(m_dielectrics,dielectricIndices);
    bool changed=true;
    while(changed)
    {
        changed=false;
        for(const auto&g:m_groups)
        {
            bool touches=false;for(const auto&id:g.objectIds)if(selectedIds.count(id)){touches=true;break;}
            if(!touches)continue;
            for(const auto&id:g.objectIds)if(selectedIds.insert(id).second)changed=true;
        }
    }
    for(int i=0;i<static_cast<int>(m_wires.size());++i)if(selectedIds.count(m_wires[static_cast<std::size_t>(i)].id))wireIndices.insert(i);
    for(int i=0;i<static_cast<int>(m_feeds.size());++i)if(selectedIds.count(m_feeds[static_cast<std::size_t>(i)].id))feedIndices.insert(i);
    for(int i=0;i<static_cast<int>(m_planes.size());++i)if(selectedIds.count(m_planes[static_cast<std::size_t>(i)].id))planeIndices.insert(i);
    for(int i=0;i<static_cast<int>(m_dielectrics.size());++i)if(selectedIds.count(m_dielectrics[static_cast<std::size_t>(i)].id))dielectricIndices.insert(i);
}

bool AntennaDesignerWidget::selectionTouchesGeometryGroup(const std::vector<int> &wireIndices,const std::vector<int> &feedIndices,
                                                           const std::vector<int> &planeIndices,const std::vector<int> &dielectricIndices) const
{
    std::set<int>w(wireIndices.begin(),wireIndices.end()),f(feedIndices.begin(),feedIndices.end()),p(planeIndices.begin(),planeIndices.end()),d(dielectricIndices.begin(),dielectricIndices.end());
    std::set<QString> ids;auto collect=[&](const auto&container,const std::set<int>&indices){for(int i:indices)if(i>=0&&i<static_cast<int>(container.size()))ids.insert(container[static_cast<std::size_t>(i)].id);};
    collect(m_wires,w);collect(m_feeds,f);collect(m_planes,p);collect(m_dielectrics,d);
    for(const auto&g:m_groups)for(const auto&id:g.objectIds)if(ids.count(id))return true;return false;
}

void AntennaDesignerWidget::refreshGroupTable()
{
    if(!m_groupTable)return;const QSignalBlocker blocker(m_groupTable);m_groupTable->setRowCount(static_cast<int>(m_groups.size()));
    auto labelFor=[this](const QString&id){
        for(const auto&w:m_wires)if(w.id==id)return QStringLiteral("Wire %1").arg(w.name);
        for(const auto&f:m_feeds)if(f.id==id)return QStringLiteral("Feed %1").arg(f.name);
        for(const auto&p:m_planes)if(p.id==id)return QStringLiteral("PEC %1").arg(p.name);
        for(const auto&d:m_dielectrics)if(d.id==id)return QStringLiteral("Dielectric %1").arg(d.name);
        return QStringLiteral("?");
    };
    for(int r=0;r<static_cast<int>(m_groups.size());++r)
    {
        const auto&g=m_groups[static_cast<std::size_t>(r)];QStringList labels;for(const auto&id:g.objectIds)labels<<labelFor(id);
        auto*name=new QTableWidgetItem(g.name);auto*members=new QTableWidgetItem(labels.join(QStringLiteral(", ")));auto*count=new QTableWidgetItem(QString::number(g.objectIds.size()));
        members->setFlags(members->flags()&~Qt::ItemIsEditable);count->setFlags(count->flags()&~Qt::ItemIsEditable);
        m_groupTable->setItem(r,0,name);m_groupTable->setItem(r,1,members);m_groupTable->setItem(r,2,count);
    }
}

void AntennaDesignerWidget::createGroupFrom3DSelection()
{
    if(!m_geometry3D||m_geometry3D->selectedObjectCount()<2)return;ensureGeometryIds();
    const auto wv=m_geometry3D->selectedWires(),fv=m_geometry3D->selectedFeeds(),pv=m_geometry3D->selectedPlanes(),dv=m_geometry3D->selectedDielectrics();
    std::set<int>w(wv.begin(),wv.end()),f(fv.begin(),fv.end()),p(pv.begin(),pv.end()),d(dv.begin(),dv.end());
    expandSelectionByGroups(w,f,p,d);
    std::set<QString> ids;for(int i:w)ids.insert(m_wires[static_cast<std::size_t>(i)].id);for(int i:f)ids.insert(m_feeds[static_cast<std::size_t>(i)].id);for(int i:p)ids.insert(m_planes[static_cast<std::size_t>(i)].id);for(int i:d)ids.insert(m_dielectrics[static_cast<std::size_t>(i)].id);
    if(ids.size()<2)return;pushGeometryHistory();
    m_groups.erase(std::remove_if(m_groups.begin(),m_groups.end(),[&](const GeometryGroup&g){for(const auto&id:g.objectIds)if(ids.count(id))return true;return false;}),m_groups.end());
    GeometryGroup g;g.id=QUuid::createUuid().toString(QUuid::WithoutBraces);g.name=QStringLiteral("Group %1").arg(m_groups.size()+1);g.objectIds.assign(ids.begin(),ids.end());m_groups.push_back(std::move(g));
    rebuildScene(false);
}

void AntennaDesignerWidget::ungroupSelectedGeometryGroups()
{
    std::set<int> rows;if(m_groupTable)for(const auto&i:m_groupTable->selectionModel()->selectedRows())rows.insert(i.row());
    if(rows.empty()&&m_geometry3D)
    {
        std::set<QString> ids;for(int i:m_geometry3D->selectedWires())if(i>=0&&i<static_cast<int>(m_wires.size()))ids.insert(m_wires[static_cast<std::size_t>(i)].id);
        for(int i:m_geometry3D->selectedFeeds())if(i>=0&&i<static_cast<int>(m_feeds.size()))ids.insert(m_feeds[static_cast<std::size_t>(i)].id);
        for(int i:m_geometry3D->selectedPlanes())if(i>=0&&i<static_cast<int>(m_planes.size()))ids.insert(m_planes[static_cast<std::size_t>(i)].id);
        for(int i:m_geometry3D->selectedDielectrics())if(i>=0&&i<static_cast<int>(m_dielectrics.size()))ids.insert(m_dielectrics[static_cast<std::size_t>(i)].id);
        for(int r=0;r<static_cast<int>(m_groups.size());++r)for(const auto&id:m_groups[static_cast<std::size_t>(r)].objectIds)if(ids.count(id)){rows.insert(r);break;}
    }
    if(rows.empty())return;pushGeometryHistory();for(auto it=rows.rbegin();it!=rows.rend();++it)if(*it>=0&&*it<static_cast<int>(m_groups.size()))m_groups.erase(m_groups.begin()+*it);rebuildScene(false);
}

void AntennaDesignerWidget::selectGeometryGroupMembers()
{
    if(!m_groupTable||!m_geometry3D)return;std::set<QString>ids;for(const auto&i:m_groupTable->selectionModel()->selectedRows()){const int r=i.row();if(r>=0&&r<static_cast<int>(m_groups.size()))ids.insert(m_groups[static_cast<std::size_t>(r)].objectIds.begin(),m_groups[static_cast<std::size_t>(r)].objectIds.end());}
    std::vector<int>w,f,p,d;for(int i=0;i<static_cast<int>(m_wires.size());++i)if(ids.count(m_wires[static_cast<std::size_t>(i)].id))w.push_back(i);for(int i=0;i<static_cast<int>(m_feeds.size());++i)if(ids.count(m_feeds[static_cast<std::size_t>(i)].id))f.push_back(i);for(int i=0;i<static_cast<int>(m_planes.size());++i)if(ids.count(m_planes[static_cast<std::size_t>(i)].id))p.push_back(i);for(int i=0;i<static_cast<int>(m_dielectrics.size());++i)if(ids.count(m_dielectrics[static_cast<std::size_t>(i)].id))d.push_back(i);
    m_geometry3D->setObjectSelection(w,f,p,d);
}

QString AntennaDesignerWidget::constraintObjectLabel(const ConstraintRef &ref) const
{
    if(ref.kind==0)for(const auto&w:m_wires)if(w.id==ref.id){QString suffix=ref.anchor==1?QStringLiteral(" [A]"):ref.anchor==2?QStringLiteral(" [B]"):QStringLiteral(" [center]");return QStringLiteral("Wire: %1%2").arg(w.name,suffix);}
    if(ref.kind==1)for(const auto&f:m_feeds)if(f.id==ref.id)return QStringLiteral("Feed: %1").arg(f.name);
    if(ref.kind==2)for(const auto&p:m_planes)if(p.id==ref.id)return QStringLiteral("PEC: %1").arg(p.name);
    if(ref.kind==3)for(const auto&d:m_dielectrics)if(d.id==ref.id)return QStringLiteral("Dielectric: %1").arg(d.name);
    return QStringLiteral("<missing>");
}

AntennaDesignerWidget::GeometryPoint AntennaDesignerWidget::constraintReferencePoint(const ConstraintRef &ref) const
{
    if(ref.kind==0)for(const auto&w:m_wires)if(w.id==ref.id){if(ref.anchor==1)return {w.aM,w.azM};if(ref.anchor==2)return {w.bM,w.bzM};return {{0.5*(w.aM.x()+w.bM.x()),0.5*(w.aM.y()+w.bM.y())},0.5*(w.azM+w.bzM)};}
    if(ref.kind==1)for(const auto&f:m_feeds)if(f.id==ref.id)return {f.positionM,f.zM};
    if(ref.kind==2)for(const auto&p:m_planes)if(p.id==ref.id)return {p.centerM,p.zM};
    if(ref.kind==3)for(const auto&d:m_dielectrics)if(d.id==ref.id)return {d.centerM,d.zM};
    return {};
}

double AntennaDesignerWidget::geometryConstraintResidual(const GeometryConstraint &c) const
{
    auto wireById=[&](const QString&id)->const WireElement*{for(const auto&w:m_wires)if(w.id==id)return &w;return nullptr;};
    auto surfaceDims=[&](const ConstraintRef&r,double&w,double&h)->bool
    {
        if(r.kind==2)for(const auto&p:m_planes)if(p.id==r.id&&p.surfaceType==0){w=p.widthM;h=p.heightM;return true;}
        if(r.kind==3)for(const auto&d:m_dielectrics)if(d.id==r.id){w=d.widthM;h=d.heightM;return true;}
        return false;
    };
    auto surfaceFrame=[&](const ConstraintRef&r,NumericalEM::Vec3&center,NumericalEM::Vec3&normal,NumericalEM::Vec3&u)->bool
    {
        AntennaSurface::SurfaceSpec spec;
        if(r.kind==2)
        {
            for(const auto&p:m_planes)if(p.id==r.id){spec=p.toSurfaceSpec();center={p.centerM.x(),p.centerM.y(),p.zM};const auto f=AntennaSurface::frameAxes(spec);normal=f.normal;u=f.u;return true;}
        }
        if(r.kind==3)
        {
            for(const auto&d:m_dielectrics)if(d.id==r.id){spec.center={d.centerM.x(),d.centerM.y(),d.zM};spec.baseOrientation=d.orientation;spec.yawDeg=d.yawDeg;spec.pitchDeg=d.pitchDeg;spec.rollDeg=d.rollDeg;center=spec.center;const auto f=AntennaSurface::frameAxes(spec);normal=f.normal;u=f.u;return true;}
        }
        return false;
    };
    auto crossNorm=[](const NumericalEM::Vec3&a,const NumericalEM::Vec3&b){return NumericalEM::norm(NumericalEM::Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x});};

    if(c.type==13)
    {
        const auto p=constraintReferencePoint(c.a);double dx=(c.fixedMask&1)?p.xyM.x()-c.fixedPoint.xyM.x():0.0,dy=(c.fixedMask&2)?p.xyM.y()-c.fixedPoint.xyM.y():0.0,dz=(c.fixedMask&4)?p.zM-c.fixedPoint.zM:0.0;return std::sqrt(dx*dx+dy*dy+dz*dz);
    }

    if(c.type==1||c.type==2||c.type==3||c.type==6||c.type==7)
    {
        const auto*a=wireById(c.a.id),*b=wireById(c.b.id);if(c.type==7){if(!b)return std::numeric_limits<double>::infinity();const double lb=segmentLength3D(b->aM,b->azM,b->bM,b->bzM);return std::abs(lb-c.valueM);}
        if(!a||!b)return std::numeric_limits<double>::infinity();
        NumericalEM::Vec3 da{a->bM.x()-a->aM.x(),a->bM.y()-a->aM.y(),a->bzM-a->azM};
        NumericalEM::Vec3 db{b->bM.x()-b->aM.x(),b->bM.y()-b->aM.y(),b->bzM-b->azM};
        const double la=NumericalEM::norm(da),lb=NumericalEM::norm(db);if(la<1e-15||lb<1e-15)return std::numeric_limits<double>::infinity();
        if(c.type==3)return std::abs(la-lb);
        da=da*(1.0/la);db=db*(1.0/lb);
        if(c.type==6){const double angle=std::acos(std::clamp(NumericalEM::dot(da,db),-1.0,1.0))*180.0/3.14159265358979323846;return std::abs(angle-c.valueDeg);}
        if(c.type==2)return std::abs(NumericalEM::dot(da,db));
        return crossNorm(da,db);
    }
    if(c.type==5)
    {
        double aw=0,ah=0,bw=0,bh=0;if(!surfaceDims(c.a,aw,ah)||!surfaceDims(c.b,bw,bh))return std::numeric_limits<double>::infinity();
        return std::hypot(aw-bw,ah-bh);
    }
    if(c.type==8)
    {
        NumericalEM::Vec3 ca{},na{},ua{},cb{},nb{},ub{};if(!surfaceFrame(c.a,ca,na,ua)||!surfaceFrame(c.b,cb,nb,ub))return std::numeric_limits<double>::infinity();
        return std::max(NumericalEM::norm(cb-ca),crossNorm(na,nb));
    }
    if(c.type==9||c.type==10)
    {
        NumericalEM::Vec3 cs{},n{},u{};if(!surfaceFrame(c.a,cs,n,u))return std::numeric_limits<double>::infinity();
        const auto*w=wireById(c.b.id);if(!w)return std::numeric_limits<double>::infinity();NumericalEM::Vec3 d{w->bM.x()-w->aM.x(),w->bM.y()-w->aM.y(),w->bzM-w->azM};const double l=NumericalEM::norm(d);if(l<1e-15)return std::numeric_limits<double>::infinity();d=d*(1.0/l);
        return c.type==9?std::abs(NumericalEM::dot(d,n)):crossNorm(d,n);
    }
    if(c.type==12)
    {
        if(c.a.kind!=2||c.b.kind!=0)return std::numeric_limits<double>::infinity();const PlaneElement*disk=nullptr;for(const auto&p:m_planes)if(p.id==c.a.id&&p.surfaceType==1){disk=&p;break;}if(!disk)return std::numeric_limits<double>::infinity();
        NumericalEM::Vec3 center{},normal{},u{};if(!surfaceFrame(c.a,center,normal,u))return std::numeric_limits<double>::infinity();const auto*w=wireById(c.b.id);if(!w)return std::numeric_limits<double>::infinity();
        NumericalEM::Vec3 a{w->aM.x(),w->aM.y(),w->azM},b{w->bM.x(),w->bM.y(),w->bzM},dir=b-a;const double L=NumericalEM::norm(dir);if(L<1e-15)return std::numeric_limits<double>::infinity();dir=dir*(1.0/L);const auto mid=(a+b)*0.5;auto radial=(mid-center)-normal*NumericalEM::dot(mid-center,normal);const double rr=NumericalEM::norm(radial);if(rr<1e-15)radial=u;else radial=radial*(1.0/rr);
        const double radialErr=std::abs(rr-disk->radiusM);const double tangentErr=std::max(std::abs(NumericalEM::dot(dir,radial)),std::abs(NumericalEM::dot(dir,normal)))*std::max(disk->radiusM,1e-3);
        return std::max(radialErr,tangentErr);
    }
    if(c.type==11)
    {
        NumericalEM::Vec3 cs{},n{},u{};if(!surfaceFrame(c.b,cs,n,u))return std::numeric_limits<double>::infinity();const auto ap=constraintReferencePoint(c.a);const NumericalEM::Vec3 a{ap.xyM.x(),ap.xyM.y(),ap.zM};
        return std::abs(std::abs(NumericalEM::dot(a-cs,n))-c.valueM);
    }
    const auto a=constraintReferencePoint(c.a),b=constraintReferencePoint(c.b);const double d=distance3D(a.xyM,a.zM,b.xyM,b.zM);
    return c.type==4?std::abs(d-c.valueM):d;
}

QString AntennaDesignerWidget::geometryConstraintDiagnostic(int constraintIndex) const
{
    if(constraintIndex<0||constraintIndex>=static_cast<int>(m_constraints.size()))return QStringLiteral("invalid");
    const auto &c=m_constraints[static_cast<std::size_t>(constraintIndex)];if(!c.enabled)return QStringLiteral("disabled");
    auto sameRef=[](const ConstraintRef&a,const ConstraintRef&b){return a.kind==b.kind&&a.id==b.id&&a.anchor==b.anchor;};
    if(c.type==13)
    {
        for(int j=0;j<constraintIndex;++j){const auto&o=m_constraints[static_cast<std::size_t>(j)];if(!o.enabled||o.type!=13||!sameRef(c.a,o.a)||(c.fixedMask&o.fixedMask)==0)continue;const double dx=((c.fixedMask&o.fixedMask)&1)?c.fixedPoint.xyM.x()-o.fixedPoint.xyM.x():0.0,dy=((c.fixedMask&o.fixedMask)&2)?c.fixedPoint.xyM.y()-o.fixedPoint.xyM.y():0.0,dz=((c.fixedMask&o.fixedMask)&4)?c.fixedPoint.zM-o.fixedPoint.zM:0.0;if(std::sqrt(dx*dx+dy*dy+dz*dz)>1e-9)return QStringLiteral("conflict: fixed position");}
        const double r=geometryConstraintResidual(c);if(!std::isfinite(r))return QStringLiteral("invalid reference");return r>1e-7?QStringLiteral("residual"):QStringLiteral("OK");
    }
    for(int j=0;j<constraintIndex;++j)
    {
        const auto&o=m_constraints[static_cast<std::size_t>(j)];if(!o.enabled||o.type!=c.type)continue;
        if(c.type==4&&sameRef(c.a,o.a)&&sameRef(c.b,o.b)&&std::abs(c.valueM-o.valueM)>1e-9)return QStringLiteral("conflict: distance target");
        if(c.type==6&&c.a.id==o.a.id&&c.b.id==o.b.id&&std::abs(c.valueDeg-o.valueDeg)>1e-6)return QStringLiteral("conflict: angle target");
        if(c.type==7&&c.b.kind==o.b.kind&&c.b.id==o.b.id&&std::abs(c.valueM-o.valueM)>1e-9)return QStringLiteral("conflict: length target");
        if(c.type==11&&sameRef(c.a,o.a)&&c.b.kind==o.b.kind&&c.b.id==o.b.id&&std::abs(c.valueM-o.valueM)>1e-9)return QStringLiteral("conflict: point-surface target");
    }
    for(int j=0;j<static_cast<int>(m_constraints.size());++j)
    {
        if(j==constraintIndex)continue;const auto&o=m_constraints[static_cast<std::size_t>(j)];if(!o.enabled||o.type==13)continue;
        if(c.solveMode==0&&o.solveMode==0&&c.type!=7&&o.type!=7&&sameRef(c.a,o.b)&&sameRef(c.b,o.a))return QStringLiteral("warning: dependency cycle");
    }
    auto objectKey=[](const ConstraintRef&r){return QStringLiteral("%1|%2").arg(r.kind).arg(r.id);};
    const QString source=objectKey(c.a),target=objectKey(c.b);std::map<QString,std::vector<QString>> graph;
    for(int j=0;j<static_cast<int>(m_constraints.size());++j){if(j==constraintIndex)continue;const auto&o=m_constraints[static_cast<std::size_t>(j)];if(o.enabled&&o.type!=13&&o.solveMode==0)graph[objectKey(o.a)].push_back(objectKey(o.b));}
    std::set<QString>visited;std::function<bool(const QString&)> reaches=[&](const QString&u){if(u==source)return true;if(!visited.insert(u).second)return false;for(const auto&v:graph[u])if(reaches(v))return true;return false;};
    if(c.solveMode==0&&reaches(target))return QStringLiteral("warning: dependency cycle");
    const double r=geometryConstraintResidual(c);const double tol=(c.type==1||c.type==2||c.type==8||c.type==9||c.type==10)?1e-5:(c.type==6?1e-4:1e-7);
    if(!std::isfinite(r))return QStringLiteral("invalid reference");
    if(r>tol)return QStringLiteral("residual");
    return QStringLiteral("OK");
}

QString AntennaDesignerWidget::geometryConstraintAnalysisText() const
{
    auto key=[](const ConstraintRef&r){return QStringLiteral("%1|%2").arg(r.kind).arg(r.id);};
    std::set<QString> nodes;std::map<QString,std::vector<QString>> graph;int edges=0,imposed=0,issues=0,balanced=0,locks=0;
    const std::array<int,14> rankCost{{3,2,1,1,1,2,1,1,5,1,2,1,3,3}};
    for(int i=0;i<static_cast<int>(m_constraints.size());++i)
    {
        const auto&c=m_constraints[static_cast<std::size_t>(i)];if(!c.enabled)continue;const QString a=key(c.a);nodes.insert(a);
        if(c.type==13){++locks;imposed+=(c.fixedMask&1?1:0)+(c.fixedMask&2?1:0)+(c.fixedMask&4?1:0);}
        else{const QString b=key(c.b);nodes.insert(b);if(c.solveMode==0){graph[a].push_back(b);++edges;}else ++balanced;imposed+=rankCost[static_cast<std::size_t>(std::clamp(c.type,0,13))];}
        const QString d=geometryConstraintDiagnostic(i);if(d.startsWith(QStringLiteral("conflict"))||d.startsWith(QStringLiteral("warning")))++issues;
    }
    std::map<QString,int> state;int backEdges=0;
    std::function<void(const QString&)> dfs=[&](const QString&u){state[u]=1;for(const auto&v:graph[u]){if(state[v]==0)dfs(v);else if(state[v]==1)++backEdges;}state[u]=2;};
    for(const auto&n:nodes)if(state[n]==0)dfs(n);
    int totalDof=static_cast<int>(m_wires.size())*6+static_cast<int>(m_feeds.size())*3;for(const auto&p:m_planes)totalDof+=p.surfaceType==0?8:7;totalDof+=static_cast<int>(m_dielectrics.size())*9;
    const int freeEstimate=std::max(0,totalDof-imposed);QString stateText=issues>0?QStringLiteral("issues detected"):(backEdges>0?QStringLiteral("dependency cycle"):QStringLiteral("graph consistent"));if(imposed>totalDof)stateText+=QStringLiteral(", likely over-constrained");
    return QStringLiteral("Dependency graph: %1 object node(s), %2 directed relation(s), %3 balanced relation(s), %4 position lock(s), %5 back-edge cycle(s) — %6.\nEstimated geometric DOF: %7 total − ~%8 constrained ≈ %9 free. Per-object estimates are listed below; this remains an engineering estimate, not a Jacobian-rank proof.")
        .arg(nodes.size()).arg(edges).arg(balanced).arg(locks).arg(backEdges).arg(stateText).arg(totalDof).arg(imposed).arg(freeEstimate);
}

void AntennaDesignerWidget::solveGeometryConstraints(bool updateStatus)
{
    if(m_applyingGeometryConstraints)return;
    m_applyingGeometryConstraints=true;ensureGeometryIds();pruneInvalidGeometryConstraints();
    auto findWire=[&](const QString&id)->int{for(int i=0;i<static_cast<int>(m_wires.size());++i)if(m_wires[static_cast<std::size_t>(i)].id==id)return i;return -1;};
    auto translateRef=[&](const ConstraintRef&r,const NumericalEM::Vec3&delta)
    {
        if(NumericalEM::norm(delta)<1e-14)return;
        if(r.kind==0)
        {
            const int i=findWire(r.id);if(i<0)return;auto &w=m_wires[static_cast<std::size_t>(i)];const WireElement old=w;
            w.aM+=QPointF(delta.x,delta.y);w.bM+=QPointF(delta.x,delta.y);w.azM+=delta.z;w.bzM+=delta.z;
            const NumericalEM::Vec3 a{old.aM.x(),old.aM.y(),old.azM},b{old.bM.x(),old.bM.y(),old.bzM},ab=b-a;const double ab2=NumericalEM::dot(ab,ab);const double tol=std::max(1e-7,m_gridMm->value()/MmPerM*0.20);
            for(auto&f:m_feeds){const NumericalEM::Vec3 p{f.positionM.x(),f.positionM.y(),f.zM};double dist=NumericalEM::norm(p-a);if(ab2>1e-30){const double t=std::clamp(NumericalEM::dot(p-a,ab)/ab2,0.0,1.0);dist=NumericalEM::norm(p-(a+ab*t));}if(dist<=tol){f.positionM+=QPointF(delta.x,delta.y);f.zM+=delta.z;}}
        }
        else if(r.kind==1)for(auto&f:m_feeds)if(f.id==r.id){f.positionM+=QPointF(delta.x,delta.y);f.zM+=delta.z;break;}
        else if(r.kind==2)for(auto&p:m_planes)if(p.id==r.id){p.centerM+=QPointF(delta.x,delta.y);p.zM+=delta.z;break;}
        else if(r.kind==3)for(auto&d:m_dielectrics)if(d.id==r.id){d.centerM+=QPointF(delta.x,delta.y);d.zM+=delta.z;break;}
    };
    auto lockedMaskForRef=[&](const ConstraintRef&r)
    {
        int mask=0;for(const auto&lock:m_constraints)if(lock.enabled&&lock.type==13&&lock.a.kind==r.kind&&lock.a.id==r.id&&(r.kind!=0||lock.a.anchor==0||lock.a.anchor==r.anchor))mask|=lock.fixedMask;return mask;
    };
    auto balancedTranslation=[&](const ConstraintRef&a,const ConstraintRef&b,const NumericalEM::Vec3&deltaB,bool balanced)
    {
        if(!balanced){translateRef(b,deltaB);return;}
        const int ma=lockedMaskForRef(a),mb=lockedMaskForRef(b);NumericalEM::Vec3 da{},db{};
        auto split=[&](double q,int bit,double&qa,double&qb){const bool la=(ma&bit)!=0,lb=(mb&bit)!=0;if(la&&lb){qa=qb=0.0;}else if(la)qb=q;else if(lb)qa=-q;else{qa=-0.5*q;qb=0.5*q;}};
        split(deltaB.x,1,da.x,db.x);split(deltaB.y,2,da.y,db.y);split(deltaB.z,4,da.z,db.z);translateRef(a,da);translateRef(b,db);
    };

    auto reshapeWire=[&](int index,const NumericalEM::Vec3&newA,const NumericalEM::Vec3&newB)
    {
        if(index<0||index>=static_cast<int>(m_wires.size()))return;auto&w=m_wires[static_cast<std::size_t>(index)];const WireElement old=w;
        const NumericalEM::Vec3 oa{old.aM.x(),old.aM.y(),old.azM},ob{old.bM.x(),old.bM.y(),old.bzM},oab=ob-oa;const double d2=NumericalEM::dot(oab,oab);const double tol=std::max(1e-7,m_gridMm->value()/MmPerM*0.20);
        for(auto&f:m_feeds){const NumericalEM::Vec3 fp{f.positionM.x(),f.positionM.y(),f.zM};double t=0.0,dist=NumericalEM::norm(fp-oa);if(d2>1e-30){t=std::clamp(NumericalEM::dot(fp-oa,oab)/d2,0.0,1.0);dist=NumericalEM::norm(fp-(oa+oab*t));}if(dist<=tol){const auto q=newA+(newB-newA)*t;f.positionM={q.x,q.y};f.zM=q.z;}}
        w.aM={newA.x,newA.y};w.azM=newA.z;w.bM={newB.x,newB.y};w.bzM=newB.z;
    };
    auto getDims=[&](const ConstraintRef&r,double&w,double&h)->bool{if(r.kind==2)for(const auto&p:m_planes)if(p.id==r.id&&p.surfaceType==0){w=p.widthM;h=p.heightM;return true;}if(r.kind==3)for(const auto&d:m_dielectrics)if(d.id==r.id){w=d.widthM;h=d.heightM;return true;}return false;};
    auto setDims=[&](const ConstraintRef&r,double w,double h){if(r.kind==2)for(auto&p:m_planes)if(p.id==r.id&&p.surfaceType==0){p.widthM=w;p.heightM=h;return;}if(r.kind==3)for(auto&d:m_dielectrics)if(d.id==r.id){d.widthM=w;d.heightM=h;return;}};
    auto surfaceFrame=[&](const ConstraintRef&r,NumericalEM::Vec3&center,NumericalEM::Vec3&normal,NumericalEM::Vec3&u)->bool
    {
        AntennaSurface::SurfaceSpec spec;
        if(r.kind==2)for(const auto&p:m_planes)if(p.id==r.id){spec=p.toSurfaceSpec();center={p.centerM.x(),p.centerM.y(),p.zM};const auto f=AntennaSurface::frameAxes(spec);normal=f.normal;u=f.u;return true;}
        if(r.kind==3)for(const auto&d:m_dielectrics)if(d.id==r.id){spec.center={d.centerM.x(),d.centerM.y(),d.zM};spec.baseOrientation=d.orientation;spec.yawDeg=d.yawDeg;spec.pitchDeg=d.pitchDeg;spec.rollDeg=d.rollDeg;center=spec.center;const auto f=AntennaSurface::frameAxes(spec);normal=f.normal;u=f.u;return true;}
        return false;
    };
    auto setSurfaceOrientationFrom=[&](const ConstraintRef&a,const ConstraintRef&b)
    {
        if(a.kind!=2||b.kind!=2)return;
        const PlaneElement*pa=nullptr;for(const auto&p:m_planes)if(p.id==a.id){pa=&p;break;}if(!pa)return;
        for(auto&p:m_planes)if(p.id==b.id){p.orientation=pa->orientation;p.yawDeg=pa->yawDeg;p.pitchDeg=pa->pitchDeg;p.rollDeg=pa->rollDeg;return;}
    };

    for(int pass=0;pass<8;++pass)
    {
        for(const auto&c:m_constraints)
        {
            if(!c.enabled)continue;
            if(c.type==13)continue;
            if(c.type==8)
            {
                NumericalEM::Vec3 ca{},na{},ua{},cb{},nb{},ub{};if(surfaceFrame(c.a,ca,na,ua)&&surfaceFrame(c.b,cb,nb,ub)){translateRef(c.b,ca-cb);setSurfaceOrientationFrom(c.a,c.b);}continue;
            }
            if(c.type==9||c.type==10)
            {
                NumericalEM::Vec3 cs{},normal{},u{};if(!surfaceFrame(c.a,cs,normal,u))continue;const int ib=findWire(c.b.id);if(ib<0)continue;const auto&wb=m_wires[static_cast<std::size_t>(ib)];
                NumericalEM::Vec3 ba{wb.aM.x(),wb.aM.y(),wb.azM},bb{wb.bM.x(),wb.bM.y(),wb.bzM},db=bb-ba;const double lb=NumericalEM::norm(db);if(lb<1e-12)continue;db=db*(1.0/lb);const auto mid=(ba+bb)*0.5;
                NumericalEM::Vec3 target{};
                if(c.type==9){target=db-normal*NumericalEM::dot(db,normal);const double n=NumericalEM::norm(target);target=n>1e-10?target*(1.0/n):u;}
                else target=NumericalEM::dot(db,normal)>=0?normal:normal*(-1.0);
                reshapeWire(ib,mid-target*(0.5*lb),mid+target*(0.5*lb));continue;
            }
            if(c.type==12)
            {
                if(c.a.kind!=2||c.b.kind!=0)continue;const PlaneElement*disk=nullptr;for(const auto&p:m_planes)if(p.id==c.a.id&&p.surfaceType==1){disk=&p;break;}if(!disk)continue;
                NumericalEM::Vec3 center{},normal{},u{};if(!surfaceFrame(c.a,center,normal,u))continue;const int ib=findWire(c.b.id);if(ib<0)continue;const auto&wb=m_wires[static_cast<std::size_t>(ib)];
                NumericalEM::Vec3 ba{wb.aM.x(),wb.aM.y(),wb.azM},bb{wb.bM.x(),wb.bM.y(),wb.bzM},db=bb-ba;const double lb=NumericalEM::norm(db);if(lb<1e-12)continue;db=db*(1.0/lb);const auto oldMid=(ba+bb)*0.5;auto radial=(oldMid-center)-normal*NumericalEM::dot(oldMid-center,normal);const double rr=NumericalEM::norm(radial);radial=rr>1e-10?radial*(1.0/rr):u;
                NumericalEM::Vec3 tangent{normal.y*radial.z-normal.z*radial.y,normal.z*radial.x-normal.x*radial.z,normal.x*radial.y-normal.y*radial.x};const double tn=NumericalEM::norm(tangent);if(tn<1e-12)continue;tangent=tangent*(1.0/tn);if(NumericalEM::dot(tangent,db)<0)tangent=tangent*(-1.0);const auto mid=center+radial*disk->radiusM;reshapeWire(ib,mid-tangent*(0.5*lb),mid+tangent*(0.5*lb));continue;
            }
            if(c.type==11)
            {
                NumericalEM::Vec3 cs{},normal{},u{};if(!surfaceFrame(c.b,cs,normal,u))continue;const auto ap=constraintReferencePoint(c.a);const NumericalEM::Vec3 a{ap.xyM.x(),ap.xyM.y(),ap.zM};const double signedDistance=NumericalEM::dot(a-cs,normal);const double sign=signedDistance<0?-1.0:1.0;const double desired=sign*std::max(0.0,c.valueM);balancedTranslation(c.a,c.b,normal*(signedDistance-desired),c.solveMode==1);continue;
            }
            if(c.type==0||c.type==4)
            {
                const auto ap=constraintReferencePoint(c.a),bp=constraintReferencePoint(c.b);NumericalEM::Vec3 a{ap.xyM.x(),ap.xyM.y(),ap.zM},b{bp.xyM.x(),bp.xyM.y(),bp.zM},v=b-a;double d=NumericalEM::norm(v);NumericalEM::Vec3 correction{};
                if(c.type==0)correction=a-b;
                else{NumericalEM::Vec3 dir=d>1e-12?v*(1.0/d):NumericalEM::Vec3{1,0,0};correction=dir*(std::max(0.0,c.valueM)-d);}
                balancedTranslation(c.a,c.b,correction,c.solveMode==1);continue;
            }
            if(c.type==5){double aw=0,ah=0,bw=0,bh=0;if(getDims(c.a,aw,ah)&&getDims(c.b,bw,bh)){if(c.solveMode==1){const double mw=0.5*(aw+bw),mh=0.5*(ah+bh);setDims(c.a,mw,mh);setDims(c.b,mw,mh);}else setDims(c.b,aw,ah);}continue;}
            const int ia=findWire(c.a.id),ib=findWire(c.b.id);if(ib<0||(c.type!=7&&ia<0))continue;
            if(c.type==7){const auto&wb=m_wires[static_cast<std::size_t>(ib)];NumericalEM::Vec3 ba{wb.aM.x(),wb.aM.y(),wb.azM},bb{wb.bM.x(),wb.bM.y(),wb.bzM},db=bb-ba;const double lb=NumericalEM::norm(db);if(lb<1e-12)continue;const auto mid=(ba+bb)*0.5;db=db*(1.0/lb);const double target=std::max(1e-9,c.valueM);reshapeWire(ib,mid-db*(0.5*target),mid+db*(0.5*target));continue;}
            const auto&wa=m_wires[static_cast<std::size_t>(ia)];const auto&wb=m_wires[static_cast<std::size_t>(ib)];
            NumericalEM::Vec3 aa{wa.aM.x(),wa.aM.y(),wa.azM},ab{wa.bM.x(),wa.bM.y(),wa.bzM},ba{wb.aM.x(),wb.aM.y(),wb.azM},bb{wb.bM.x(),wb.bM.y(),wb.bzM};
            NumericalEM::Vec3 da=ab-aa,db=bb-ba;const double la=NumericalEM::norm(da),lb=NumericalEM::norm(db);if(la<1e-12||lb<1e-12)continue;da=da*(1.0/la);db=db*(1.0/lb);const NumericalEM::Vec3 mid=(ba+bb)*0.5;
            if(c.type==3){if(c.solveMode==1){const double target=0.5*(la+lb);const auto midA=(aa+ab)*0.5;reshapeWire(ia,midA-da*(0.5*target),midA+da*(0.5*target));reshapeWire(ib,mid-db*(0.5*target),mid+db*(0.5*target));}else reshapeWire(ib,mid-db*(0.5*la),mid+db*(0.5*la));continue;}
            NumericalEM::Vec3 target{};
            if(c.type==1){target=NumericalEM::dot(da,db)>=0?da:da*(-1.0);}
            else
            {
                NumericalEM::Vec3 normal=db-da*NumericalEM::dot(db,da);double n=NumericalEM::norm(normal);
                if(n<1e-9){NumericalEM::Vec3 axis=std::abs(da.x)<0.8?NumericalEM::Vec3{1,0,0}:NumericalEM::Vec3{0,1,0};normal={da.y*axis.z-da.z*axis.y,da.z*axis.x-da.x*axis.z,da.x*axis.y-da.y*axis.x};n=NumericalEM::norm(normal);}
                if(n>1e-12)normal=normal*(1.0/n);else normal={0,0,1};
                if(c.type==6){const double theta=std::clamp(c.valueDeg,0.0,180.0)*3.14159265358979323846/180.0;target=da*std::cos(theta)+normal*std::sin(theta);}
                else target=normal;
            }
            reshapeWire(ib,mid-target*(0.5*lb),mid+target*(0.5*lb));
        }
        // Position locks are applied after the relational pass so they behave as anchors rather than
        // as ordinary A→B relations. This also lets balanced constraints redistribute correction to
        // the unlocked side without depending on constraint list order.
        for(const auto&lock:m_constraints)if(lock.enabled&&lock.type==13)
        {
            const auto cur=constraintReferencePoint(lock.a);NumericalEM::Vec3 delta{lock.fixedPoint.xyM.x()-cur.xyM.x(),lock.fixedPoint.xyM.y()-cur.xyM.y(),lock.fixedPoint.zM-cur.zM};if(!(lock.fixedMask&1))delta.x=0;if(!(lock.fixedMask&2))delta.y=0;if(!(lock.fixedMask&4))delta.z=0;translateRef(lock.a,delta);
        }
    }
    int unsatisfied=0,issues=0;for(int i=0;i<static_cast<int>(m_constraints.size());++i)if(m_constraints[static_cast<std::size_t>(i)].enabled){const double r=geometryConstraintResidual(m_constraints[static_cast<std::size_t>(i)]);const int type=m_constraints[static_cast<std::size_t>(i)].type;const double tol=(type==1||type==2||type==8||type==9||type==10)?1e-5:(type==6?1e-4:1e-7);if(!std::isfinite(r)||r>tol)++unsatisfied;const QString diag=geometryConstraintDiagnostic(i);if(diag.startsWith(QStringLiteral("conflict"))||diag.startsWith(QStringLiteral("warning")))++issues;}
    if(updateStatus&&m_constraintStatus)m_constraintStatus->setText(QStringLiteral("%1 persistent constraint(s), %2 unsatisfied, %3 conflict/cycle warning(s).").arg(m_constraints.size()).arg(unsatisfied).arg(issues));if(updateStatus&&m_constraintGraphStatus)m_constraintGraphStatus->setText(geometryConstraintAnalysisText());
    m_applyingGeometryConstraints=false;
}

void AntennaDesignerWidget::refreshConstraintEditor()
{
    if(!m_constraintRefA||!m_constraintRefB||!m_constraintTable)return;
    ensureGeometryIds();pruneInvalidGeometryConstraints();
    const QVariant oldA=m_constraintRefA->currentData(),oldB=m_constraintRefB->currentData();
    QSignalBlocker ba(m_constraintRefA),bb(m_constraintRefB);m_constraintRefA->clear();m_constraintRefB->clear();
    auto add=[&](int kind,const QString&id,int anchor,const QString&label){const QString data=QStringLiteral("%1|%2|%3").arg(kind).arg(id).arg(anchor);m_constraintRefA->addItem(label,data);m_constraintRefB->addItem(label,data);};
    for(const auto&w:m_wires){add(0,w.id,0,QStringLiteral("Wire: %1 [center]").arg(w.name));add(0,w.id,1,QStringLiteral("Wire: %1 [endpoint A]").arg(w.name));add(0,w.id,2,QStringLiteral("Wire: %1 [endpoint B]").arg(w.name));}
    for(const auto&f:m_feeds)add(1,f.id,0,QStringLiteral("Feed: %1").arg(f.name));for(const auto&p:m_planes)add(2,p.id,0,QStringLiteral("PEC: %1").arg(p.name));for(const auto&d:m_dielectrics)add(3,d.id,0,QStringLiteral("Dielectric: %1").arg(d.name));
    int ia=m_constraintRefA->findData(oldA),ib=m_constraintRefB->findData(oldB);if(ia>=0)m_constraintRefA->setCurrentIndex(ia);if(ib>=0)m_constraintRefB->setCurrentIndex(ib);else if(m_constraintRefB->count()>1)m_constraintRefB->setCurrentIndex(1);
    const QSignalBlocker tableBlock(m_constraintTable);m_constraintTable->setRowCount(static_cast<int>(m_constraints.size()));
    const std::array<QString,14>types{QStringLiteral("Coincident points"),QStringLiteral("Parallel wires"),QStringLiteral("Perpendicular wires"),QStringLiteral("Equal wire length"),QStringLiteral("Point distance"),QStringLiteral("Equal surface W/H"),QStringLiteral("Wire angle"),QStringLiteral("Wire length (B)"),QStringLiteral("Concentric PEC axes"),QStringLiteral("Wire parallel to surface"),QStringLiteral("Wire normal to surface"),QStringLiteral("Point-to-surface distance"),QStringLiteral("Wire tangent to PEC disk"),QStringLiteral("Fix position axes")};
    auto maskText=[](int mask){QString t;if(mask&1)t+=QStringLiteral("X");if(mask&2)t+=QStringLiteral("Y");if(mask&4)t+=QStringLiteral("Z");return t.isEmpty()?QStringLiteral("—"):t;};
    for(int r=0;r<static_cast<int>(m_constraints.size());++r)
    {
        const auto&c=m_constraints[static_cast<std::size_t>(r)];const double residual=geometryConstraintResidual(c);QString residualText;
        if(std::isfinite(residual))residualText=(c.type==1||c.type==2||c.type==8||c.type==9||c.type==10)?QString::number(residual,'g',4):(c.type==6?QStringLiteral("%1 deg").arg(residual,0,'g',5):QStringLiteral("%1 mm").arg(residual*MmPerM,0,'g',5));else residualText=QStringLiteral("invalid");
        QString valueText=QStringLiteral("—");if(c.type==4||c.type==7||c.type==11)valueText=QStringLiteral("%1 mm").arg(c.valueM*MmPerM,0,'g',6);else if(c.type==6)valueText=QStringLiteral("%1 deg").arg(c.valueDeg,0,'g',6);else if(c.type==13)valueText=QStringLiteral("Fix %1").arg(maskText(c.fixedMask));
        const QString modeText=c.type==13?QStringLiteral("Lock"):((c.solveMode==1)?QStringLiteral("Balanced"):QStringLiteral("A→B"));
        const QString bLabel=c.type==13?QStringLiteral("—"):constraintObjectLabel(c.b);
        const QStringList values={c.enabled?QStringLiteral("Yes"):QStringLiteral("No"),types[static_cast<std::size_t>(std::clamp(c.type,0,13))],constraintObjectLabel(c.a),bLabel,valueText,modeText,residualText,geometryConstraintDiagnostic(r)};
        for(int col=0;col<values.size();++col){auto*item=new QTableWidgetItem(values[col]);item->setFlags(item->flags()&~Qt::ItemIsEditable);m_constraintTable->setItem(r,col,item);}
    }
    if(m_constraintStatus)
    {
        int enabled=0,issues=0,balanced=0,locks=0;for(int i=0;i<static_cast<int>(m_constraints.size());++i)if(m_constraints[static_cast<std::size_t>(i)].enabled){++enabled;const auto&c=m_constraints[static_cast<std::size_t>(i)];balanced+=c.solveMode==1&&c.type!=13;locks+=c.type==13;const auto d=geometryConstraintDiagnostic(i);if(d.startsWith(QStringLiteral("conflict"))||d.startsWith(QStringLiteral("warning")))++issues;}
        m_constraintStatus->setText(QStringLiteral("%1 constraint(s), %2 enabled, %3 balanced, %4 lock(s), %5 conflict/cycle warning(s). Double-click a 3D dimension to edit it; Delete removes the selected annotation.").arg(m_constraints.size()).arg(enabled).arg(balanced).arg(locks).arg(issues));
    }
    if(m_constraintGraphStatus)m_constraintGraphStatus->setText(geometryConstraintAnalysisText());

    if(m_constraintDofTable)
    {
        struct DofRow{QString id,label;int total=0,imposed=0,driven=0,lockMask=0;bool issue=false;};std::vector<DofRow> rows;
        for(const auto&w:m_wires)rows.push_back({w.id,QStringLiteral("Wire: %1").arg(w.name),6});
        for(const auto&f:m_feeds)rows.push_back({f.id,QStringLiteral("Feed: %1").arg(f.name),3});
        for(const auto&p:m_planes)rows.push_back({p.id,QStringLiteral("PEC: %1").arg(p.name),p.surfaceType==0?8:7});
        for(const auto&d:m_dielectrics)rows.push_back({d.id,QStringLiteral("Dielectric: %1").arg(d.name),9});
        auto rowFor=[&](const QString&id)->DofRow*{for(auto&r:rows)if(r.id==id)return &r;return nullptr;};
        const std::array<int,14> rankCost{{3,2,1,1,1,2,1,1,5,1,2,1,3,3}};
        for(int i=0;i<static_cast<int>(m_constraints.size());++i)
        {
            const auto&c=m_constraints[static_cast<std::size_t>(i)];if(!c.enabled)continue;const int cost=c.type==13?((c.fixedMask&1?1:0)+(c.fixedMask&2?1:0)+(c.fixedMask&4?1:0)):rankCost[static_cast<std::size_t>(std::clamp(c.type,0,13))];
            if(c.type==13){if(auto*r=rowFor(c.a.id)){r->imposed+=cost;r->lockMask|=c.fixedMask;++r->driven;}}
            else if(c.solveMode==1 && (c.type==0||c.type==3||c.type==4||c.type==5||c.type==11))
            {
                const int aCost=cost/2,bCost=cost-aCost;if(auto*r=rowFor(c.a.id)){r->imposed+=aCost;++r->driven;}if(auto*r=rowFor(c.b.id)){r->imposed+=bCost;++r->driven;}
            }
            else if(auto*r=rowFor(c.b.id)){r->imposed+=cost;++r->driven;}
            const QString diag=geometryConstraintDiagnostic(i);if(diag!=QStringLiteral("OK")&&diag!=QStringLiteral("disabled")){if(auto*r=rowFor(c.a.id))r->issue=true;if(c.type!=13)if(auto*r=rowFor(c.b.id))r->issue=true;}
        }
        QSignalBlocker block(m_constraintDofTable);m_constraintDofTable->setRowCount(static_cast<int>(rows.size()));
        for(int i=0;i<static_cast<int>(rows.size());++i){const auto&r=rows[static_cast<std::size_t>(i)];const int free=std::max(0,r.total-r.imposed);QString state;if(r.lockMask)state=QStringLiteral("Fix %1").arg(maskText(r.lockMask));if(r.issue)state+=(state.isEmpty()?QString():QStringLiteral("; "))+QStringLiteral("constraint issue");if(state.isEmpty())state=QStringLiteral("—");const QStringList vals={r.label,QStringLiteral("%1 / %2").arg(free).arg(r.total),QString::number(r.driven),state};for(int c=0;c<vals.size();++c){auto*item=new QTableWidgetItem(vals[c]);item->setFlags(item->flags()&~Qt::ItemIsEditable);m_constraintDofTable->setItem(i,c,item);}}
    }
}

void AntennaDesignerWidget::addGeometryConstraint()
{
    if(!m_constraintType||!m_constraintRefA||!m_constraintRefB)return;
    auto parse=[](const QVariant&v,ConstraintRef&r){const QStringList p=v.toString().split('|');if(p.size()<2||p.size()>3)return false;bool ok=false;const int k=p[0].toInt(&ok);if(!ok||k<0||k>3||p[1].isEmpty())return false;r.kind=k;r.id=p[1];r.anchor=(p.size()==3&&k==0)?std::clamp(p[2].toInt(),0,2):0;return true;};
    GeometryConstraint c;c.id=QUuid::createUuid().toString(QUuid::WithoutBraces);c.type=m_constraintType->currentIndex();
    if(!parse(m_constraintRefA->currentData(),c.a))return;
    if(c.type==13){c.b=c.a;c.fixedMask=m_constraintFixAxes?m_constraintFixAxes->currentData().toInt():7;c.fixedMask=std::clamp(c.fixedMask,1,7);c.fixedPoint=constraintReferencePoint(c.a);c.solveMode=0;}
    else
    {
        if(!parse(m_constraintRefB->currentData(),c.b)||(c.type!=7&&c.a.kind==c.b.kind&&c.a.id==c.b.id&&c.a.anchor==c.b.anchor))return;
        c.solveMode=(m_constraintSolveMode&&m_constraintSolveMode->currentIndex()==1&&(c.type==0||c.type==3||c.type==4||c.type==5||c.type==11))?1:0;
    }
    if((c.type==1||c.type==2||c.type==3||c.type==6)&&(c.a.kind!=0||c.b.kind!=0)){QMessageBox::information(this,QStringLiteral("Constraint"),QStringLiteral("Parallel, perpendicular, equal-length and angle constraints require wire references."));return;}
    if(c.type==7&&c.b.kind!=0){QMessageBox::information(this,QStringLiteral("Constraint"),QStringLiteral("Wire length requires a wire as object B."));return;}
    if(c.type==8&&(c.a.kind!=2||c.b.kind!=2)){QMessageBox::information(this,QStringLiteral("Constraint"),QStringLiteral("Concentric axes currently require two PEC surface primitives."));return;}
    auto circularPec=[&](const ConstraintRef&r){if(r.kind!=2)return false;for(const auto&p:m_planes)if(p.id==r.id)return p.surfaceType>=1&&p.surfaceType<=4;return false;};
    if(c.type==8&&(!circularPec(c.a)||!circularPec(c.b))){QMessageBox::information(this,QStringLiteral("Constraint"),QStringLiteral("Concentric axes require disk, cylinder, cone or paraboloid PEC primitives."));return;}
    if((c.type==9||c.type==10)&&!((c.a.kind==2||c.a.kind==3)&&c.b.kind==0)){QMessageBox::information(this,QStringLiteral("Constraint"),QStringLiteral("Wire/surface orientation constraints use A = PEC/dielectric surface and B = driven wire."));return;}
    if(c.type==11&&!(c.b.kind==2||c.b.kind==3)){QMessageBox::information(this,QStringLiteral("Constraint"),QStringLiteral("Point-to-surface distance requires a PEC or dielectric surface as object B."));return;}
    if(c.type==12){bool disk=false;if(c.a.kind==2)for(const auto&p:m_planes)if(p.id==c.a.id&&p.surfaceType==1){disk=true;break;}if(!disk||c.b.kind!=0){QMessageBox::information(this,QStringLiteral("Constraint"),QStringLiteral("Wire tangent to PEC disk requires A = disk PEC and B = wire."));return;}c.a.anchor=0;c.b.anchor=0;}
    if(c.type==1||c.type==2||c.type==3||c.type==6||c.type==9||c.type==10){c.b.anchor=0;}if(c.type==1||c.type==2||c.type==3||c.type==6){c.a.anchor=0;}if(c.type==7)c.b.anchor=0;
    auto rectangularSurface=[&](const ConstraintRef&r){if(r.kind==3)return true;if(r.kind==2)for(const auto&p:m_planes)if(p.id==r.id)return p.surfaceType==0;return false;};
    if(c.type==5&&(!rectangularSurface(c.a)||!rectangularSurface(c.b))){QMessageBox::information(this,QStringLiteral("Constraint"),QStringLiteral("Equal W/H currently requires rectangular PEC surfaces and/or dielectric substrates."));return;}
    if((c.type==4||c.type==7||c.type==11)&&m_constraintValueMm)c.valueM=std::max(0.0,m_constraintValueMm->value()/MmPerM);if(c.type==6&&m_constraintValueMm)c.valueDeg=std::clamp(m_constraintValueMm->value(),0.0,180.0);
    c.name=QStringLiteral("C%1").arg(m_constraints.size()+1);pushGeometryHistory();m_constraints.push_back(c);solveGeometryConstraints();rebuildScene(false);selectGeometryConstraint(static_cast<int>(m_constraints.size())-1);
}

void AntennaDesignerWidget::removeSelectedGeometryConstraints()
{
    if(!m_constraintTable)return;std::set<int>rows;for(const auto&i:m_constraintTable->selectionModel()->selectedRows())rows.insert(i.row());if(rows.empty())return;pushGeometryHistory();int offset=0;for(int r:rows){const int idx=r-offset;if(idx>=0&&idx<static_cast<int>(m_constraints.size())){m_constraints.erase(m_constraints.begin()+idx);++offset;}}rebuildScene(false);
}

void AntennaDesignerWidget::toggleSelectedGeometryConstraints()
{
    if(!m_constraintTable)return;const auto rows=m_constraintTable->selectionModel()->selectedRows();if(rows.empty())return;pushGeometryHistory();for(const auto&i:rows){const int r=i.row();if(r>=0&&r<static_cast<int>(m_constraints.size()))m_constraints[static_cast<std::size_t>(r)].enabled=!m_constraints[static_cast<std::size_t>(r)].enabled;}solveGeometryConstraints();rebuildScene(false);
}

void AntennaDesignerWidget::selectGeometryConstraint(int constraintIndex)
{
    if(!m_constraintTable)return;
    QSignalBlocker blocker(m_constraintTable);m_constraintTable->clearSelection();
    if(constraintIndex>=0&&constraintIndex<m_constraintTable->rowCount())
    {
        m_constraintTable->selectRow(constraintIndex);m_constraintTable->scrollToItem(m_constraintTable->item(constraintIndex,0),QAbstractItemView::PositionAtCenter);
    }
    if(m_geometry3D)m_geometry3D->setSelectedConstraint(constraintIndex);
}

void AntennaDesignerWidget::removeGeometryConstraintIndex(int constraintIndex)
{
    if(constraintIndex<0||constraintIndex>=static_cast<int>(m_constraints.size()))return;pushGeometryHistory();m_constraints.erase(m_constraints.begin()+constraintIndex);solveGeometryConstraints(false);rebuildScene(false);selectGeometryConstraint(-1);
}

void AntennaDesignerWidget::editGeometryConstraintValue(int constraintIndex)
{
    if(constraintIndex<0||constraintIndex>=static_cast<int>(m_constraints.size()))return;auto &c=m_constraints[static_cast<std::size_t>(constraintIndex)];
    if(c.type==13)
    {
        pushGeometryHistory();c.fixedPoint=constraintReferencePoint(c.a);solveGeometryConstraints();rebuildScene(false);selectGeometryConstraint(constraintIndex);return;
    }
    if(!(c.type==4||c.type==6||c.type==7||c.type==11))
    {
        QMessageBox::information(this,QStringLiteral("Constraint"),QStringLiteral("This relation has no scalar dimension to edit. Select its row to enable/disable or remove it."));return;
    }
    bool ok=false;
    if(c.type==6)
    {
        const double value=QInputDialog::getDouble(this,QStringLiteral("Edit 3D constraint"),QStringLiteral("Angle (deg)"),c.valueDeg,0.0,180.0,4,&ok);if(!ok)return;pushGeometryHistory();c.valueDeg=value;
    }
    else
    {
        const double value=QInputDialog::getDouble(this,QStringLiteral("Edit 3D constraint"),QStringLiteral("Dimension (mm)"),c.valueM*MmPerM,0.0,1e9,6,&ok);if(!ok)return;pushGeometryHistory();c.valueM=value/MmPerM;
    }
    solveGeometryConstraints();rebuildScene(false);selectGeometryConstraint(constraintIndex);
}

void AntennaDesignerWidget::deleteNearest3D(const GeometryPoint &position)
{
    const NumericalEM::Vec3 p{position.xyM.x(),position.xyM.y(),position.zM};double best=std::numeric_limits<double>::infinity();bool feedBest=false;std::size_t bestIndex=0;
    for(std::size_t i=0;i<m_wires.size();++i){const auto&w=m_wires[i];const NumericalEM::Vec3 a{w.aM.x(),w.aM.y(),w.azM},b{w.bM.x(),w.bM.y(),w.bzM},ab=b-a;const double d2=NumericalEM::dot(ab,ab);double d=NumericalEM::norm(p-a);if(d2>1e-30){const double t=std::clamp(NumericalEM::dot(p-a,ab)/d2,0.0,1.0);d=NumericalEM::norm(p-(a+ab*t));}if(d<best){best=d;feedBest=false;bestIndex=i;}}
    for(std::size_t i=0;i<m_feeds.size();++i){const auto&f=m_feeds[i];const double d=NumericalEM::norm(p-NumericalEM::Vec3{f.positionM.x(),f.positionM.y(),f.zM});if(d<best){best=d;feedBest=true;bestIndex=i;}}
    if(best>std::max(0.02,m_gridMm->value()/MmPerM))return;pushGeometryHistory();if(feedBest)m_feeds.erase(m_feeds.begin()+static_cast<std::ptrdiff_t>(bestIndex));else m_wires.erase(m_wires.begin()+static_cast<std::ptrdiff_t>(bestIndex));rebuildScene(false);
}

void AntennaDesignerWidget::deleteGeometrySelection3D(const std::vector<int> &wireIndices,const std::vector<int> &feedIndices,
                                                        const std::vector<int> &planeIndices,const std::vector<int> &dielectricIndices,
                                                        bool selectedNode,const GeometryPoint &nodePosition)
{
    std::set<int>wires(wireIndices.begin(),wireIndices.end()),feeds(feedIndices.begin(),feedIndices.end()),planes(planeIndices.begin(),planeIndices.end()),dielectrics(dielectricIndices.begin(),dielectricIndices.end());
    if(selectedNode)
    {
        const NumericalEM::Vec3 node{nodePosition.xyM.x(),nodePosition.xyM.y(),nodePosition.zM};constexpr double tol=1e-8;
        for(int i=0;i<static_cast<int>(m_wires.size());++i){const auto&w=m_wires[static_cast<std::size_t>(i)];const NumericalEM::Vec3 a{w.aM.x(),w.aM.y(),w.azM},b{w.bM.x(),w.bM.y(),w.bzM};if(NumericalEM::norm(a-node)<=tol||NumericalEM::norm(b-node)<=tol)wires.insert(i);}
        for(int i=0;i<static_cast<int>(m_feeds.size());++i){const auto&f=m_feeds[static_cast<std::size_t>(i)];if(NumericalEM::norm(NumericalEM::Vec3{f.positionM.x(),f.positionM.y(),f.zM}-node)<=tol)feeds.insert(i);}
    }
    if(wires.empty()&&feeds.empty()&&planes.empty()&&dielectrics.empty())return;
    QString detail=QStringLiteral("Delete selected geometry?\n\n%1 wire(s)\n%2 feed(s)\n%3 PEC surface(s)\n%4 dielectric region(s)")
                       .arg(static_cast<int>(wires.size())).arg(static_cast<int>(feeds.size())).arg(static_cast<int>(planes.size())).arg(static_cast<int>(dielectrics.size()));
    if(selectedNode)detail.prepend(QStringLiteral("The selected node is topological: all connected wires/feeds will be removed.\n\n"));
    detail+=QStringLiteral("\n\nConstraints referring to deleted objects will also be removed.");
    if(QMessageBox::question(this,QStringLiteral("Delete geometry"),detail,QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
    pushGeometryHistory();if(m_geometry3D)m_geometry3D->clearSelectionFromController();
    auto eraseIndices=[](auto &container,const std::set<int>&indices){for(auto it=indices.rbegin();it!=indices.rend();++it)if(*it>=0&&*it<static_cast<int>(container.size()))container.erase(container.begin()+*it);};
    eraseIndices(m_wires,wires);eraseIndices(m_feeds,feeds);eraseIndices(m_planes,planes);eraseIndices(m_dielectrics,dielectrics);
    pruneInvalidGeometryConstraints();rebuildScene(false);
}

void AntennaDesignerWidget::moveNode3D(const GeometryPoint &oldPosition,const GeometryPoint &newPositionIn)
{
    constexpr double sameTol=1e-8;auto newPosition=newPositionIn;
    if(m_magneticSnapEnabled)newPosition=snapPointToWireGeometry3D(newPositionIn,1e-6,false);
    if(distance3D(oldPosition.xyM,oldPosition.zM,newPosition.xyM,newPosition.zM)<=sameTol)return;pushGeometryHistory();bool changed=false;
    auto move=[&](QPointF&xy,double&z){if(distance3D(xy,z,oldPosition.xyM,oldPosition.zM)<=sameTol){xy=newPosition.xyM;z=newPosition.zM;changed=true;}};for(auto&w:m_wires){move(w.aM,w.azM);move(w.bM,w.bzM);}for(auto&f:m_feeds)move(f.positionM,f.zM);if(!changed){m_undoHistory.pop_back();return;}m_wires.erase(std::remove_if(m_wires.begin(),m_wires.end(),[](const WireElement&w){return segmentLength3D(w.aM,w.azM,w.bM,w.bzM)<1e-6;}),m_wires.end());rebuildScene(false);
}

void AntennaDesignerWidget::moveFeed3D(int feedIndex,const GeometryPoint &newPosition)
{
    if(feedIndex<0||feedIndex>=static_cast<int>(m_feeds.size()))return;pushGeometryHistory();GeometryPoint p=newPosition;if(m_magneticSnapEnabled)p=snapFeedPoint3D(newPosition,1e-6,true);m_feeds[static_cast<std::size_t>(feedIndex)].positionM=p.xyM;m_feeds[static_cast<std::size_t>(feedIndex)].zM=p.zM;rebuildScene(false);
}

void AntennaDesignerWidget::translateWire3D(int wireIndex,const GeometryPoint &delta)
{
    if(wireIndex<0||wireIndex>=static_cast<int>(m_wires.size()))return;
    if(std::hypot(delta.xyM.x(),delta.xyM.y())<1e-12&&std::abs(delta.zM)<1e-12)return;
    pushGeometryHistory();
    const WireElement old=m_wires[static_cast<std::size_t>(wireIndex)];
    auto &w=m_wires[static_cast<std::size_t>(wireIndex)];
    w.aM+=delta.xyM;w.azM+=delta.zM;w.bM+=delta.xyM;w.bzM+=delta.zM;
    // Feeds physically attached to this span follow the rigid translation. Other wires are left
    // untouched deliberately: moving one explicit WireElement may disconnect/reposition it.
    const NumericalEM::Vec3 a{old.aM.x(),old.aM.y(),old.azM},b{old.bM.x(),old.bM.y(),old.bzM},ab=b-a;
    const double ab2=NumericalEM::dot(ab,ab);const double tol=std::max(1e-7,m_gridMm->value()/MmPerM*0.20);
    for(auto &f:m_feeds)
    {
        const NumericalEM::Vec3 p{f.positionM.x(),f.positionM.y(),f.zM};double dist=NumericalEM::norm(p-a);
        if(ab2>1e-30){const double t=std::clamp(NumericalEM::dot(p-a,ab)/ab2,0.0,1.0);dist=NumericalEM::norm(p-(a+ab*t));}
        if(dist<=tol){f.positionM+=delta.xyM;f.zM+=delta.zM;}
    }
    splitWiresAtSelectedEndpoints(std::set<int>{wireIndex});
    rebuildScene(false);
}

void AntennaDesignerWidget::translatePlane3D(int planeIndex,const GeometryPoint &delta)
{
    if(planeIndex<0||planeIndex>=static_cast<int>(m_planes.size()))return;
    if(std::hypot(delta.xyM.x(),delta.xyM.y())<1e-12&&std::abs(delta.zM)<1e-12)return;
    pushGeometryHistory();auto &pl=m_planes[static_cast<std::size_t>(planeIndex)];pl.centerM+=delta.xyM;pl.zM+=delta.zM;rebuildScene(false);
}

void AntennaDesignerWidget::translateDielectric3D(int dielectricIndex,const GeometryPoint &delta)
{
    if(dielectricIndex<0||dielectricIndex>=static_cast<int>(m_dielectrics.size()))return;
    if(std::hypot(delta.xyM.x(),delta.xyM.y())<1e-12&&std::abs(delta.zM)<1e-12)return;
    pushGeometryHistory();auto &d=m_dielectrics[static_cast<std::size_t>(dielectricIndex)];d.centerM+=delta.xyM;d.zM+=delta.zM;rebuildScene(false);
}

void AntennaDesignerWidget::translateSelection3D(const std::vector<int> &wireIndices,const std::vector<int> &feedIndices,
                                                  const std::vector<int> &planeIndices,const std::vector<int> &dielectricIndices,
                                                  const GeometryPoint &delta)
{
    if(std::hypot(delta.xyM.x(),delta.xyM.y())<1e-12&&std::abs(delta.zM)<1e-12)return;
    std::set<int>wset,fset,pset,dset;
    for(int i:wireIndices)if(i>=0&&i<static_cast<int>(m_wires.size()))wset.insert(i);
    for(int i:feedIndices)if(i>=0&&i<static_cast<int>(m_feeds.size()))fset.insert(i);
    for(int i:planeIndices)if(i>=0&&i<static_cast<int>(m_planes.size()))pset.insert(i);
    for(int i:dielectricIndices)if(i>=0&&i<static_cast<int>(m_dielectrics.size()))dset.insert(i);
    expandSelectionByGroups(wset,fset,pset,dset);
    if(wset.empty()&&fset.empty()&&pset.empty()&&dset.empty())return;
    pushGeometryHistory();

    std::vector<WireElement> oldSelectedWires;oldSelectedWires.reserve(wset.size());
    for(int i:wset)oldSelectedWires.push_back(m_wires[static_cast<std::size_t>(i)]);
    for(int i:wset){auto&w=m_wires[static_cast<std::size_t>(i)];w.aM+=delta.xyM;w.bM+=delta.xyM;w.azM+=delta.zM;w.bzM+=delta.zM;}

    // A feed attached to a selected rigid wire follows it even when the feed was not explicitly
    // Ctrl-selected. Explicitly selected free feeds also translate. Each feed is moved only once.
    const double tol=std::max(1e-7,m_gridMm->value()/MmPerM*0.20);
    for(int fi=0;fi<static_cast<int>(m_feeds.size());++fi)
    {
        bool move=fset.count(fi)>0;const auto&f=m_feeds[static_cast<std::size_t>(fi)];const NumericalEM::Vec3 p{f.positionM.x(),f.positionM.y(),f.zM};
        if(!move)for(const auto&w:oldSelectedWires){const NumericalEM::Vec3 a{w.aM.x(),w.aM.y(),w.azM},b{w.bM.x(),w.bM.y(),w.bzM},ab=b-a;const double d2=NumericalEM::dot(ab,ab);double dist=NumericalEM::norm(p-a);if(d2>1e-30){const double t=std::clamp(NumericalEM::dot(p-a,ab)/d2,0.0,1.0);dist=NumericalEM::norm(p-(a+ab*t));}if(dist<=tol){move=true;break;}}
        if(move){auto&mf=m_feeds[static_cast<std::size_t>(fi)];mf.positionM+=delta.xyM;mf.zM+=delta.zM;}
    }
    for(int i:pset){auto&pl=m_planes[static_cast<std::size_t>(i)];pl.centerM+=delta.xyM;pl.zM+=delta.zM;}
    for(int i:dset){auto&d=m_dielectrics[static_cast<std::size_t>(i)];d.centerM+=delta.xyM;d.zM+=delta.zM;}
    splitWiresAtSelectedEndpoints(wset);
    rebuildScene(false);
}

void AntennaDesignerWidget::duplicate3DSelection()
{
    if(!m_geometry3D||m_geometry3D->selectedObjectCount()<=0)return;
    const auto wires=m_geometry3D->selectedWires(),feeds=m_geometry3D->selectedFeeds(),planes=m_geometry3D->selectedPlanes(),dielectrics=m_geometry3D->selectedDielectrics();
    pushGeometryHistory();
    const double step=std::max(1e-6,m_gridMm?m_gridMm->value()/MmPerM:0.05);GeometryPoint offset{};const int plane=m_editPlane?m_editPlane->currentIndex():0;if(plane==2)offset.xyM.setY(step);else offset.xyM.setX(step);
    auto uniqueName=[](const QString&base,auto&&container){QString candidate=base+QStringLiteral("_copy");int n=2;auto exists=[&](const QString&name){for(const auto&v:container)if(v.name==name)return true;return false;};while(exists(candidate))candidate=base+QStringLiteral("_copy%1").arg(n++);return candidate;};
    std::map<QString,QString> copiedIds;std::vector<int> nw,nf,np,nd;
    for(int i:wires)if(i>=0&&i<static_cast<int>(m_wires.size())){const QString sourceId=m_wires[static_cast<std::size_t>(i)].id;auto copy=m_wires[static_cast<std::size_t>(i)];copy.id=QUuid::createUuid().toString(QUuid::WithoutBraces);copy.name=uniqueName(copy.name,m_wires);copy.aM+=offset.xyM;copy.bM+=offset.xyM;copy.azM+=offset.zM;copy.bzM+=offset.zM;copiedIds[sourceId]=copy.id;nw.push_back(static_cast<int>(m_wires.size()));m_wires.push_back(copy);}
    for(int i:feeds)if(i>=0&&i<static_cast<int>(m_feeds.size())){const QString sourceId=m_feeds[static_cast<std::size_t>(i)].id;auto copy=m_feeds[static_cast<std::size_t>(i)];copy.id=QUuid::createUuid().toString(QUuid::WithoutBraces);copy.name=uniqueName(copy.name,m_feeds);copy.positionM+=offset.xyM;copy.zM+=offset.zM;copiedIds[sourceId]=copy.id;nf.push_back(static_cast<int>(m_feeds.size()));m_feeds.push_back(copy);}
    for(int i:planes)if(i>=0&&i<static_cast<int>(m_planes.size())){const QString sourceId=m_planes[static_cast<std::size_t>(i)].id;auto copy=m_planes[static_cast<std::size_t>(i)];copy.id=QUuid::createUuid().toString(QUuid::WithoutBraces);copy.name=uniqueName(copy.name,m_planes);copy.centerM+=offset.xyM;copy.zM+=offset.zM;copiedIds[sourceId]=copy.id;np.push_back(static_cast<int>(m_planes.size()));m_planes.push_back(copy);}
    for(int i:dielectrics)if(i>=0&&i<static_cast<int>(m_dielectrics.size())){const QString sourceId=m_dielectrics[static_cast<std::size_t>(i)].id;auto copy=m_dielectrics[static_cast<std::size_t>(i)];copy.id=QUuid::createUuid().toString(QUuid::WithoutBraces);copy.name=uniqueName(copy.name,m_dielectrics);copy.centerM+=offset.xyM;copy.zM+=offset.zM;copiedIds[sourceId]=copy.id;nd.push_back(static_cast<int>(m_dielectrics.size()));m_dielectrics.push_back(copy);}

    // Duplicating a rigid group produces a new independent rigid group instead of four unrelated
    // copied members. Ungrouped objects remain ungrouped.
    const auto groupsBefore=m_groups;
    auto uniqueGroupName=[this](const QString&base){QString candidate=base+QStringLiteral("_copy");int n=2;auto exists=[&](const QString&name){for(const auto&g:m_groups)if(g.name==name)return true;return false;};while(exists(candidate))candidate=base+QStringLiteral("_copy%1").arg(n++);return candidate;};
    for(const auto&sourceGroup:groupsBefore)
    {
        GeometryGroup copyGroup;copyGroup.id=QUuid::createUuid().toString(QUuid::WithoutBraces);copyGroup.name=uniqueGroupName(sourceGroup.name);
        for(const auto&id:sourceGroup.objectIds)if(const auto it=copiedIds.find(id);it!=copiedIds.end())copyGroup.objectIds.push_back(it->second);
        if(copyGroup.objectIds.size()>=2)m_groups.push_back(std::move(copyGroup));
    }
    rebuildScene(false);m_geometry3D->setObjectSelection(nw,nf,np,nd);
}

void AntennaDesignerWidget::translate3DSelectionNumerically(const GeometryPoint &delta)
{
    if(!m_geometry3D||m_geometry3D->selectedObjectCount()<=0)return;
    translateSelection3D(m_geometry3D->selectedWires(),m_geometry3D->selectedFeeds(),m_geometry3D->selectedPlanes(),m_geometry3D->selectedDielectrics(),delta);
}

void AntennaDesignerWidget::rotate3DSelection(int axis,double angleDeg,const GeometryPoint &pivotPoint)
{
    if(!m_geometry3D||m_geometry3D->selectedObjectCount()<=0||std::abs(angleDeg)<1e-10)return;axis=std::clamp(axis,0,2);
    const auto wires=m_geometry3D->selectedWires(),feeds=m_geometry3D->selectedFeeds(),planes=m_geometry3D->selectedPlanes(),dielectrics=m_geometry3D->selectedDielectrics();
    std::set<int>wset, fset, pset, dset;for(int i:wires)if(i>=0&&i<static_cast<int>(m_wires.size()))wset.insert(i);for(int i:feeds)if(i>=0&&i<static_cast<int>(m_feeds.size()))fset.insert(i);for(int i:planes)if(i>=0&&i<static_cast<int>(m_planes.size()))pset.insert(i);for(int i:dielectrics)if(i>=0&&i<static_cast<int>(m_dielectrics.size()))dset.insert(i);
    if(wset.empty()&&fset.empty()&&pset.empty()&&dset.empty())return;pushGeometryHistory();
    const NumericalEM::Vec3 pivot{pivotPoint.xyM.x(),pivotPoint.xyM.y(),pivotPoint.zM};std::vector<WireElement> oldSelectedWires;for(int i:wset)oldSelectedWires.push_back(m_wires[static_cast<std::size_t>(i)]);
    auto rotateStored=[&](QPointF &xy,double &z){const auto q=rotateAroundAxis({xy.x(),xy.y(),z},pivot,axis,angleDeg);xy={q.x,q.y};z=q.z;};
    for(int i:wset){auto&w=m_wires[static_cast<std::size_t>(i)];rotateStored(w.aM,w.azM);rotateStored(w.bM,w.bzM);}
    const double tol=std::max(1e-7,m_gridMm->value()/MmPerM*0.20);
    for(int fi=0;fi<static_cast<int>(m_feeds.size());++fi)
    {
        bool move=fset.count(fi)>0;const auto&f=m_feeds[static_cast<std::size_t>(fi)];const NumericalEM::Vec3 fp{f.positionM.x(),f.positionM.y(),f.zM};
        if(!move)for(const auto&w:oldSelectedWires){const NumericalEM::Vec3 a{w.aM.x(),w.aM.y(),w.azM},b{w.bM.x(),w.bM.y(),w.bzM},ab=b-a;const double d2=NumericalEM::dot(ab,ab);double dist=NumericalEM::norm(fp-a);if(d2>1e-30){const double t=std::clamp(NumericalEM::dot(fp-a,ab)/d2,0.0,1.0);dist=NumericalEM::norm(fp-(a+ab*t));}if(dist<=tol){move=true;break;}}
        if(move){auto&mf=m_feeds[static_cast<std::size_t>(fi)];rotateStored(mf.positionM,mf.zM);}
    }
    for(int i:pset){auto&pl=m_planes[static_cast<std::size_t>(i)];rotateStored(pl.centerM,pl.zM);rotateEulerWorldAxis(pl.yawDeg,pl.pitchDeg,pl.rollDeg,axis,angleDeg);}
    for(int i:dset){auto&d=m_dielectrics[static_cast<std::size_t>(i)];rotateStored(d.centerM,d.zM);rotateEulerWorldAxis(d.yawDeg,d.pitchDeg,d.rollDeg,axis,angleDeg);}
    rebuildScene(false);m_geometry3D->setObjectSelection(wires,feeds,planes,dielectrics);
}

void AntennaDesignerWidget::rotate3DSelectionAroundVector(const GeometryPoint &axisPoint,double angleDeg,const GeometryPoint &pivotPoint)
{
    if(!m_geometry3D||m_geometry3D->selectedObjectCount()<=0||std::abs(angleDeg)<1e-10)return;
    NumericalEM::Vec3 axis{axisPoint.xyM.x(),axisPoint.xyM.y(),axisPoint.zM};const double an=NumericalEM::norm(axis);if(an<=1e-12)return;axis=axis*(1.0/an);
    const auto wires=m_geometry3D->selectedWires(),feeds=m_geometry3D->selectedFeeds(),planes=m_geometry3D->selectedPlanes(),dielectrics=m_geometry3D->selectedDielectrics();
    std::set<int>wset, fset, pset, dset;for(int i:wires)if(i>=0&&i<static_cast<int>(m_wires.size()))wset.insert(i);for(int i:feeds)if(i>=0&&i<static_cast<int>(m_feeds.size()))fset.insert(i);for(int i:planes)if(i>=0&&i<static_cast<int>(m_planes.size()))pset.insert(i);for(int i:dielectrics)if(i>=0&&i<static_cast<int>(m_dielectrics.size()))dset.insert(i);
    if(wset.empty()&&fset.empty()&&pset.empty()&&dset.empty())return;pushGeometryHistory();
    const NumericalEM::Vec3 pivot{pivotPoint.xyM.x(),pivotPoint.xyM.y(),pivotPoint.zM};const Matrix3 rotation=vectorRotationMatrix(axis,angleDeg);std::vector<WireElement> oldSelectedWires;for(int i:wset)oldSelectedWires.push_back(m_wires[static_cast<std::size_t>(i)]);
    auto rotateStored=[&](QPointF &xy,double &z){const auto q=rotateAroundVector({xy.x(),xy.y(),z},pivot,axis,angleDeg);xy={q.x,q.y};z=q.z;};
    for(int i:wset){auto&w=m_wires[static_cast<std::size_t>(i)];rotateStored(w.aM,w.azM);rotateStored(w.bM,w.bzM);}
    const double tol=std::max(1e-7,m_gridMm->value()/MmPerM*0.20);
    for(int fi=0;fi<static_cast<int>(m_feeds.size());++fi)
    {
        bool move=fset.count(fi)>0;const auto&f=m_feeds[static_cast<std::size_t>(fi)];const NumericalEM::Vec3 fp{f.positionM.x(),f.positionM.y(),f.zM};
        if(!move)for(const auto&w:oldSelectedWires){const NumericalEM::Vec3 a{w.aM.x(),w.aM.y(),w.azM},b{w.bM.x(),w.bM.y(),w.bzM},ab=b-a;const double d2=NumericalEM::dot(ab,ab);double dist=NumericalEM::norm(fp-a);if(d2>1e-30){const double t=std::clamp(NumericalEM::dot(fp-a,ab)/d2,0.0,1.0);dist=NumericalEM::norm(fp-(a+ab*t));}if(dist<=tol){move=true;break;}}
        if(move){auto&mf=m_feeds[static_cast<std::size_t>(fi)];rotateStored(mf.positionM,mf.zM);}
    }
    for(int i:pset){auto&pl=m_planes[static_cast<std::size_t>(i)];rotateStored(pl.centerM,pl.zM);const Matrix3 combined=matrixMultiply(rotation,eulerMatrix(pl.yawDeg,pl.pitchDeg,pl.rollDeg));matrixToEuler(combined,pl.yawDeg,pl.pitchDeg,pl.rollDeg);}
    for(int i:dset){auto&d=m_dielectrics[static_cast<std::size_t>(i)];rotateStored(d.centerM,d.zM);const Matrix3 combined=matrixMultiply(rotation,eulerMatrix(d.yawDeg,d.pitchDeg,d.rollDeg));matrixToEuler(combined,d.yawDeg,d.pitchDeg,d.rollDeg);}
    rebuildScene(false);m_geometry3D->setObjectSelection(wires,feeds,planes,dielectrics);
}

void AntennaDesignerWidget::mirrorCopy3DSelection(int axis,const GeometryPoint &pivotPoint)
{
    if(!m_geometry3D||m_geometry3D->selectedObjectCount()<=0)return;axis=std::clamp(axis,0,2);const auto wires=m_geometry3D->selectedWires(),feeds=m_geometry3D->selectedFeeds(),planes=m_geometry3D->selectedPlanes(),dielectrics=m_geometry3D->selectedDielectrics();
    const NumericalEM::Vec3 pivot{pivotPoint.xyM.x(),pivotPoint.xyM.y(),pivotPoint.zM};pushGeometryHistory();
    auto mirrorStored=[&](QPointF &xy,double &z){if(axis==0)xy.setX(2.0*pivot.x-xy.x());else if(axis==1)xy.setY(2.0*pivot.y-xy.y());else z=2.0*pivot.z-z;};
    auto uniqueName=[](const QString&base,auto&&container){QString candidate=base+QStringLiteral("_mirror");int n=2;auto exists=[&](const QString&name){for(const auto&v:container)if(v.name==name)return true;return false;};while(exists(candidate))candidate=base+QStringLiteral("_mirror%1").arg(n++);return candidate;};
    std::map<QString,QString> copiedIds;std::vector<int>nw,nf,np,nd;
    for(int i:wires)if(i>=0&&i<static_cast<int>(m_wires.size())){const QString sourceId=m_wires[static_cast<std::size_t>(i)].id;auto copy=m_wires[static_cast<std::size_t>(i)];copy.id=QUuid::createUuid().toString(QUuid::WithoutBraces);copy.name=uniqueName(copy.name,m_wires);mirrorStored(copy.aM,copy.azM);mirrorStored(copy.bM,copy.bzM);copiedIds[sourceId]=copy.id;nw.push_back(static_cast<int>(m_wires.size()));m_wires.push_back(copy);}
    for(int i:feeds)if(i>=0&&i<static_cast<int>(m_feeds.size())){const QString sourceId=m_feeds[static_cast<std::size_t>(i)].id;auto copy=m_feeds[static_cast<std::size_t>(i)];copy.id=QUuid::createUuid().toString(QUuid::WithoutBraces);copy.name=uniqueName(copy.name,m_feeds);mirrorStored(copy.positionM,copy.zM);copiedIds[sourceId]=copy.id;nf.push_back(static_cast<int>(m_feeds.size()));m_feeds.push_back(copy);}
    for(int i:planes)if(i>=0&&i<static_cast<int>(m_planes.size())){const QString sourceId=m_planes[static_cast<std::size_t>(i)].id;auto copy=m_planes[static_cast<std::size_t>(i)];copy.id=QUuid::createUuid().toString(QUuid::WithoutBraces);copy.name=uniqueName(copy.name,m_planes);mirrorStored(copy.centerM,copy.zM);mirrorSurfaceEuler(copy.orientation,copy.yawDeg,copy.pitchDeg,copy.rollDeg,axis,copy.yawDeg,copy.pitchDeg,copy.rollDeg);copiedIds[sourceId]=copy.id;np.push_back(static_cast<int>(m_planes.size()));m_planes.push_back(copy);}
    for(int i:dielectrics)if(i>=0&&i<static_cast<int>(m_dielectrics.size())){const QString sourceId=m_dielectrics[static_cast<std::size_t>(i)].id;auto copy=m_dielectrics[static_cast<std::size_t>(i)];copy.id=QUuid::createUuid().toString(QUuid::WithoutBraces);copy.name=uniqueName(copy.name,m_dielectrics);mirrorStored(copy.centerM,copy.zM);mirrorSurfaceEuler(copy.orientation,copy.yawDeg,copy.pitchDeg,copy.rollDeg,axis,copy.yawDeg,copy.pitchDeg,copy.rollDeg);copiedIds[sourceId]=copy.id;nd.push_back(static_cast<int>(m_dielectrics.size()));m_dielectrics.push_back(copy);}
    const auto groupsBefore=m_groups;
    auto uniqueGroupName=[this](const QString&base){QString candidate=base+QStringLiteral("_mirror");int n=2;auto exists=[&](const QString&name){for(const auto&g:m_groups)if(g.name==name)return true;return false;};while(exists(candidate))candidate=base+QStringLiteral("_mirror%1").arg(n++);return candidate;};
    for(const auto&sourceGroup:groupsBefore)
    {
        GeometryGroup copyGroup;copyGroup.id=QUuid::createUuid().toString(QUuid::WithoutBraces);copyGroup.name=uniqueGroupName(sourceGroup.name);
        for(const auto&id:sourceGroup.objectIds)if(const auto it=copiedIds.find(id);it!=copiedIds.end())copyGroup.objectIds.push_back(it->second);
        if(copyGroup.objectIds.size()>=2)m_groups.push_back(std::move(copyGroup));
    }
    rebuildScene(false);m_geometry3D->setObjectSelection(nw,nf,np,nd);
}

void AntennaDesignerWidget::align3DSelection(int axis)
{
    if(!m_geometry3D||m_geometry3D->selectedObjectCount()<2)return;axis=std::clamp(axis,0,2);
    const auto wires=m_geometry3D->selectedWires(),feeds=m_geometry3D->selectedFeeds(),planes=m_geometry3D->selectedPlanes(),dielectrics=m_geometry3D->selectedDielectrics();
    auto coord=[axis](const NumericalEM::Vec3&p){return axis==0?p.x:axis==1?p.y:p.z;};double sum=0.0;int count=0;
    for(int i:wires)if(i>=0&&i<static_cast<int>(m_wires.size())){const auto&w=m_wires[static_cast<std::size_t>(i)];sum+=coord({0.5*(w.aM.x()+w.bM.x()),0.5*(w.aM.y()+w.bM.y()),0.5*(w.azM+w.bzM)});++count;}
    for(int i:feeds)if(i>=0&&i<static_cast<int>(m_feeds.size())){const auto&f=m_feeds[static_cast<std::size_t>(i)];sum+=coord({f.positionM.x(),f.positionM.y(),f.zM});++count;}
    for(int i:planes)if(i>=0&&i<static_cast<int>(m_planes.size())){const auto&pl=m_planes[static_cast<std::size_t>(i)];sum+=coord({pl.centerM.x(),pl.centerM.y(),pl.zM});++count;}
    for(int i:dielectrics)if(i>=0&&i<static_cast<int>(m_dielectrics.size())){const auto&d=m_dielectrics[static_cast<std::size_t>(i)];sum+=coord({d.centerM.x(),d.centerM.y(),d.zM});++count;}
    if(count<2)return;const double target=sum/count;pushGeometryHistory();std::set<int> explicitFeeds(feeds.begin(),feeds.end()),movedFeeds;
    auto shiftPoint=[axis](QPointF&xy,double&z,double d){if(axis==0)xy.rx()+=d;else if(axis==1)xy.ry()+=d;else z+=d;};
    const double tol=std::max(1e-7,m_gridMm->value()/MmPerM*0.20);
    for(int i:wires)if(i>=0&&i<static_cast<int>(m_wires.size())){auto&w=m_wires[static_cast<std::size_t>(i)];const WireElement old=w;const NumericalEM::Vec3 center{0.5*(w.aM.x()+w.bM.x()),0.5*(w.aM.y()+w.bM.y()),0.5*(w.azM+w.bzM)};const double d=target-coord(center);shiftPoint(w.aM,w.azM,d);shiftPoint(w.bM,w.bzM,d);const NumericalEM::Vec3 a{old.aM.x(),old.aM.y(),old.azM},b{old.bM.x(),old.bM.y(),old.bzM},ab=b-a;const double ab2=NumericalEM::dot(ab,ab);for(int fi=0;fi<static_cast<int>(m_feeds.size());++fi){if(explicitFeeds.count(fi)||movedFeeds.count(fi))continue;const auto&f=m_feeds[static_cast<std::size_t>(fi)];const NumericalEM::Vec3 fp{f.positionM.x(),f.positionM.y(),f.zM};double dist=NumericalEM::norm(fp-a);if(ab2>1e-30){const double t=std::clamp(NumericalEM::dot(fp-a,ab)/ab2,0.0,1.0);dist=NumericalEM::norm(fp-(a+ab*t));}if(dist<=tol){auto&mf=m_feeds[static_cast<std::size_t>(fi)];shiftPoint(mf.positionM,mf.zM,d);movedFeeds.insert(fi);}}}
    for(int i:feeds)if(i>=0&&i<static_cast<int>(m_feeds.size())){auto&f=m_feeds[static_cast<std::size_t>(i)];const double d=target-coord({f.positionM.x(),f.positionM.y(),f.zM});shiftPoint(f.positionM,f.zM,d);}
    for(int i:planes)if(i>=0&&i<static_cast<int>(m_planes.size())){auto&pl=m_planes[static_cast<std::size_t>(i)];const double d=target-coord({pl.centerM.x(),pl.centerM.y(),pl.zM});shiftPoint(pl.centerM,pl.zM,d);}
    for(int i:dielectrics)if(i>=0&&i<static_cast<int>(m_dielectrics.size())){auto&de=m_dielectrics[static_cast<std::size_t>(i)];const double d=target-coord({de.centerM.x(),de.centerM.y(),de.zM});shiftPoint(de.centerM,de.zM,d);}
    rebuildScene(false);m_geometry3D->setObjectSelection(wires,feeds,planes,dielectrics);
}

void AntennaDesignerWidget::rebuildScene(bool fitView)
{
    ensureGeometryIds();
    pruneInvalidGeometryGroups();
    solveGeometryConstraints(false);
    auto *scene=m_canvas->drawingScene();scene->clear();
    const int plane=m_editPlane?m_editPlane->currentIndex():0;
    const double active=m_activePlaneCoordinateM?m_activePlaneCoordinateM->value():0.0;
    const QPalette pal=palette();QColor baseWire=pal.color(QPalette::Highlight);QColor feedColor(255,191,0);if(pal.color(QPalette::Base).lightness()>160)feedColor=QColor(190,110,0);const QColor textColor=pal.color(QPalette::Text);
    const double depthScale=std::max(0.001,m_gridMm->value()/MmPerM*4.0);
    for(std::size_t i=0;i<m_wires.size();++i)
    {
        const auto&w=m_wires[i];const QPointF a=projectEditorPoint(w.aM,w.azM,plane),b=projectEditorPoint(w.bM,w.bzM,plane);
        const double hidden=0.5*(hiddenEditorCoordinate(w.aM,w.azM,plane)+hiddenEditorCoordinate(w.bM,w.bzM,plane));
        QColor wc=baseWire;wc.setAlpha(70+static_cast<int>(185.0/(1.0+std::abs(hidden-active)/depthScale)));
        const QLineF wireLine(toSceneMm(a),toSceneMm(b));
        QGraphicsLineItem *line=nullptr;
        if(m_showPhysicalWireRadius)
        {
            QPen centerPen(wc,0.0,Qt::SolidLine,Qt::RoundCap);line=scene->addLine(wireLine,centerPen);
            QColor body=wc;body.setAlpha(std::max(35,wc.alpha()-45));
            QPen bodyPen(body,std::max(0.02,2.0*w.radiusM*MmPerM),Qt::SolidLine,Qt::RoundCap);
            auto *envelope=new QGraphicsLineItem(wireLine,line);envelope->setPen(bodyPen);envelope->setAcceptedMouseButtons(Qt::NoButton);envelope->setZValue(-1.0);
        }
        else line=scene->addLine(wireLine,QPen(wc,3.0,Qt::SolidLine,Qt::RoundCap));
        line->setFlag(QGraphicsItem::ItemIsSelectable,true);line->setData(0,QStringLiteral("wire"));line->setData(1,static_cast<int>(i));
        line->setToolTip(QStringLiteral("%1 — radius %2 mm, diameter %3 mm").arg(w.name).arg(w.radiusM*MmPerM,0,'g',6).arg(2.0*w.radiusM*MmPerM,0,'g',6));
        auto*label=scene->addSimpleText(w.name);label->setBrush(textColor);label->setFlag(QGraphicsItem::ItemIgnoresTransformations,true);label->setAcceptedMouseButtons(Qt::NoButton);label->setPos(toSceneMm((a+b)*0.5)+QPointF(5,-5));label->setParentItem(line);
    }
    for(std::size_t i=0;i<m_planes.size();++i)
    {
        const auto &pl=m_planes[i];
        const auto mesh=AntennaSurface::triangulate(pl.toSurfaceSpec(),1800);
        QPainterPath path; double hiddenMean=0.0; std::size_t hiddenCount=0;
        for(const auto &tri:mesh)
        {
            QPolygonF poly;
            for(const auto &q3:{tri.a,tri.b,tri.c})
            {
                const QPointF q=projectEditorPoint(QPointF(q3.x,q3.y),q3.z,plane);
                hiddenMean+=hiddenEditorCoordinate(QPointF(q3.x,q3.y),q3.z,plane);++hiddenCount;
                const QPointF sm=toSceneMm(q);poly<<sm;
            }
            path.addPolygon(poly);path.closeSubpath();
        }
        if(hiddenCount)hiddenMean/=double(hiddenCount);else hiddenMean=hiddenEditorCoordinate(pl.centerM,pl.zM,plane);
        QColor pc=pal.color(QPalette::Midlight);pc.setAlpha(35+static_cast<int>(95.0/(1.0+std::abs(hiddenMean-active)/depthScale)));
        QColor pe=pal.color(QPalette::PlaceholderText);pe.setAlpha(130);
        auto *item=scene->addPath(path,QPen(pe,0.75),QBrush(pc));
        item->setData(0,QStringLiteral("plane"));item->setData(1,static_cast<int>(i));item->setFlag(QGraphicsItem::ItemIsSelectable,true);item->setZValue(-20);
        const auto center3=AntennaSurface::pointFromLocal(pl.toSurfaceSpec(),0,0,0);
        const QPointF centerProjected=toSceneMm(projectEditorPoint(QPointF(center3.x,center3.y),center3.z,plane));
        auto *label=scene->addSimpleText(pl.name+QStringLiteral(" [")+surfaceKindText(pl.surfaceType)+QStringLiteral("]"));label->setBrush(pe);label->setFlag(QGraphicsItem::ItemIgnoresTransformations,true);label->setAcceptedMouseButtons(Qt::NoButton);label->setPos(centerProjected+QPointF(5,-5));label->setZValue(1.0);label->setParentItem(item);
    }

    for(std::size_t i=0;i<m_dielectrics.size();++i)
    {
        const auto &d=m_dielectrics[i];AntennaSurface::SurfaceSpec spec;spec.center={d.centerM.x(),d.centerM.y(),d.zM};spec.baseOrientation=d.orientation;spec.yawDeg=d.yawDeg;spec.pitchDeg=d.pitchDeg;spec.rollDeg=d.rollDeg;
        std::array<NumericalEM::Vec3,8> q{};int qi=0;for(double n:{-0.5*d.thicknessM,0.5*d.thicknessM})for(double v:{-0.5*d.heightM,0.5*d.heightM})for(double u:{-0.5*d.widthM,0.5*d.widthM})q[qi++]=AntennaSurface::pointFromLocal(spec,u,v,n);
        const std::array<std::array<int,4>,6> faces{{{{0,1,3,2}},{{4,6,7,5}},{{0,4,5,1}},{{2,3,7,6}},{{0,2,6,4}},{{1,5,7,3}}}};QPainterPath path;
        for(const auto&face:faces){QPolygonF poly;for(int idx:face){const auto&q3=q[static_cast<std::size_t>(idx)];poly<<toSceneMm(projectEditorPoint(QPointF(q3.x,q3.y),q3.z,plane));}path.addPolygon(poly);path.closeSubpath();}
        QColor dc=pal.color(QPalette::Highlight);dc.setAlpha(28);QColor de=pal.color(QPalette::Highlight);de.setAlpha(85);auto *item=scene->addPath(path,QPen(de,0.7),QBrush(dc));item->setData(0,QStringLiteral("dielectric"));item->setData(1,static_cast<int>(i));item->setFlag(QGraphicsItem::ItemIsSelectable,true);item->setZValue(-30);
        const QPointF cp=toSceneMm(projectEditorPoint(d.centerM,d.zM,plane));auto *label=scene->addSimpleText(d.name+QStringLiteral(" [εr=")+QString::number(d.relativePermittivity,'g',4)+QStringLiteral("]"));label->setBrush(de);label->setFlag(QGraphicsItem::ItemIgnoresTransformations,true);label->setAcceptedMouseButtons(Qt::NoButton);label->setPos(cp+QPointF(5,10));label->setZValue(1.0);label->setParentItem(item);
    }

    struct Node{QPointF xy;double z;};std::vector<Node>nodes;
    auto addNode=[&](const QPointF&xy,double z){for(const auto&n:nodes)if(distance3D(n.xy,n.z,xy,z)<1e-8)return;nodes.push_back({xy,z});};
    for(const auto&w:m_wires){addNode(w.aM,w.azM);addNode(w.bM,w.bzM);}QColor nodeColor=pal.color(QPalette::Highlight);nodeColor.setAlpha(210);
    for(const auto&n:nodes)
    {
        const QPointF q=projectEditorPoint(n.xy,n.z,plane);const double hidden=hiddenEditorCoordinate(n.xy,n.z,plane);
        QColor nc=nodeColor;if(std::abs(hidden-active)>depthScale)nc.setAlpha(90);
        auto*node=scene->addEllipse(QRectF(-4.5,-4.5,9,9),QPen(nc,1.5),QBrush(pal.color(QPalette::Base)));node->setPos(toSceneMm(q));node->setFlag(QGraphicsItem::ItemIgnoresTransformations,true);node->setZValue(100);node->setData(0,QStringLiteral("node"));node->setData(2,q);node->setData(3,hidden);node->setToolTip(QStringLiteral("Drag XYZ node %1 in %2 projection").arg(coordText3D(n.xy,n.z),m_editPlane?m_editPlane->currentText():QStringLiteral("XY")));
    }
    for(std::size_t i=0;i<m_feeds.size();++i)
    {
        const auto&f=m_feeds[i];const QPointF q=projectEditorPoint(f.positionM,f.zM,plane);const QPointF ps=toSceneMm(q);auto*outer=scene->addEllipse(QRectF(ps.x()-8,ps.y()-8,16,16),QPen(feedColor,3.0));outer->setFlag(QGraphicsItem::ItemIsSelectable,true);outer->setData(0,QStringLiteral("feed"));outer->setData(1,static_cast<int>(i));auto*crossH=scene->addLine(QLineF(ps+QPointF(-12,0),ps+QPointF(12,0)),QPen(feedColor,2.0));crossH->setAcceptedMouseButtons(Qt::NoButton);crossH->setParentItem(outer);auto*crossV=scene->addLine(QLineF(ps+QPointF(0,-12),ps+QPointF(0,12)),QPen(feedColor,2.0));crossV->setAcceptedMouseButtons(Qt::NoButton);crossV->setParentItem(outer);auto*label=scene->addSimpleText(f.name);label->setBrush(feedColor);label->setFlag(QGraphicsItem::ItemIgnoresTransformations,true);label->setAcceptedMouseButtons(Qt::NoButton);label->setPos(ps+QPointF(10,-10));label->setParentItem(outer);
    }
    if(m_geometry3D)
    {
        m_geometry3D->setGeometry(m_wires,m_feeds,m_planes,m_dielectrics);
        m_geometry3D->setGroups(m_groups);
        std::vector<Geometry3DView::ConstraintOverlay> overlays;std::set<QString> problemObjectIds;
        const std::array<QString,14> shortNames{QStringLiteral("Coincident"),QStringLiteral("Parallel"),QStringLiteral("Perpendicular"),QStringLiteral("Equal L"),QStringLiteral("Distance"),QStringLiteral("Equal W/H"),QStringLiteral("Angle"),QStringLiteral("Wire L"),QStringLiteral("Concentric"),QStringLiteral("Wire ∥ surface"),QStringLiteral("Wire ⟂ surface"),QStringLiteral("Point ↔ surface"),QStringLiteral("Tangent disk"),QStringLiteral("Fix")};
        auto maskText=[](int mask){QString t;if(mask&1)t+=QStringLiteral("X");if(mask&2)t+=QStringLiteral("Y");if(mask&4)t+=QStringLiteral("Z");return t;};
        for(int constraintIndex=0;constraintIndex<static_cast<int>(m_constraints.size());++constraintIndex)
        {
            const auto&c=m_constraints[static_cast<std::size_t>(constraintIndex)];if(!c.enabled)continue;
            auto ap=constraintReferencePoint(c.a),bp=c.type==13?c.fixedPoint:constraintReferencePoint(c.b);if(c.type==7&&c.b.kind==0)for(const auto&w:m_wires)if(w.id==c.b.id){ap={w.aM,w.azM};bp={w.bM,w.bzM};break;}const double residual=geometryConstraintResidual(c);const bool ok=std::isfinite(residual)&&residual<=((c.type==1||c.type==2)?1e-5:(c.type==6?1e-4:1e-7));
            QString label=shortNames[static_cast<std::size_t>(std::clamp(c.type,0,13))];if(c.type==4||c.type==7||c.type==11)label+=QStringLiteral(" %1 mm").arg(c.valueM*MmPerM,0,'g',5);else if(c.type==6)label+=QStringLiteral(" %1 deg").arg(c.valueDeg,0,'g',5);else if(c.type==13)label+=QStringLiteral(" %1").arg(maskText(c.fixedMask));if(c.solveMode==1&&c.type!=13)label+=QStringLiteral(" ⇄");
            const QString diag=geometryConstraintDiagnostic(constraintIndex);if(diag.startsWith(QStringLiteral("conflict")))label+=QStringLiteral(" !");else if(diag.startsWith(QStringLiteral("warning")))label+=QStringLiteral(" ↺");if(diag!=QStringLiteral("OK")&&diag!=QStringLiteral("disabled")){problemObjectIds.insert(c.a.id);if(c.type!=13)problemObjectIds.insert(c.b.id);}
            overlays.push_back({{ap.xyM.x(),ap.xyM.y(),ap.zM},{bp.xyM.x(),bp.xyM.y(),bp.zM},label,ok&&!diag.startsWith(QStringLiteral("conflict")),constraintIndex});
        }
        m_geometry3D->setConstraintOverlays(overlays);m_geometry3D->setConstraintProblemObjectIds(problemObjectIds);
    }
    m_canvas->ensureNavigationBounds();
    refreshTables();refreshGroupTable();refreshConstraintEditor();refreshSummary();clearMomResults();clearSweepResults();clearOptimizationResults();clearMultiOptimizationResults();refreshYagiIndividualVariables();if(fitView)m_canvas->fitGeometry();
}

void AntennaDesignerWidget::refreshTables()
{
    const QSignalBlocker wireBlocker(m_wireTable),feedBlocker(m_feedTable),planeBlocker(m_planeTable),dielectricBlocker(m_dielectricTable);m_wireTable->setRowCount(static_cast<int>(m_wires.size()));
    for(int r=0;r<static_cast<int>(m_wires.size());++r)
    {
        const auto&w=m_wires[static_cast<std::size_t>(r)];const double len=segmentLength3D(w.aM,w.azM,w.bM,w.bzM);
        const QStringList values={w.name,QString::number(w.aM.x(),'g',7),QString::number(w.aM.y(),'g',7),QString::number(w.azM,'g',7),QString::number(w.bM.x(),'g',7),QString::number(w.bM.y(),'g',7),QString::number(w.bzM,'g',7),QString::number(len,'g',7),QString::number(w.radiusM*MmPerM,'g',7)};
        for(int c=0;c<values.size();++c){auto*cell=new QTableWidgetItem(values[c]);if(c==7){cell->setFlags(cell->flags()&~Qt::ItemIsEditable);cell->setToolTip(QStringLiteral("Calculated 3D endpoint distance."));}else if(c>=1&&c<=6)cell->setToolTip(QStringLiteral("Exact XYZ coordinate in metres. Editing a shared endpoint propagates the full 3D node to connected wires and feeds."));else if(c==8)cell->setToolTip(QStringLiteral("Physical wire radius in millimetres."));m_wireTable->setItem(r,c,cell);}
    }
    m_feedTable->setRowCount(static_cast<int>(m_feeds.size()));
    for(int r=0;r<static_cast<int>(m_feeds.size());++r)
    {
        const auto&f=m_feeds[static_cast<std::size_t>(r)];const QStringList values={f.name,QString::number(f.positionM.x(),'g',7),QString::number(f.positionM.y(),'g',7),QString::number(f.zM,'g',7),QString::number(f.voltageV,'g',7),QString::number(f.phaseDeg,'g',7),QString::number(f.sourceOhm,'g',7)};
        for(int c=0;c<values.size();++c){auto*cell=new QTableWidgetItem(values[c]);if(c>=1&&c<=3)cell->setToolTip(QStringLiteral("Exact XYZ feed coordinate in metres. Feed should lie on a conductor for a valid MoM excitation."));else if(c==4||c==5)cell->setToolTip(QStringLiteral("Per-feed complex source excitation. All feed phasors are applied simultaneously by the generalized wire MoM."));else if(c==6)cell->setToolTip(QStringLiteral("S-parameter reference impedance for this feed. It affects Γ/S11/VSWR reporting, not the EFIE excitation matrix."));m_feedTable->setItem(r,c,cell);}
    }
    if(m_feedSimulationTable)
    {
        const bool wasBlocked=m_feedSimulationTable->blockSignals(true);
        m_feedSimulationTable->setRowCount(static_cast<int>(m_feeds.size()));
        for(int r=0;r<static_cast<int>(m_feeds.size());++r)
        {
            const auto&f=m_feeds[static_cast<std::size_t>(r)];
            const double ph=f.phaseDeg*NumericalEM::Pi/180.0;
            const std::complex<double> v=std::polar(f.voltageV,ph);
            const QString sign=v.imag()>=0.0?QStringLiteral("+"):QStringLiteral("−");
            const QString phasor=QStringLiteral("%1 %2 j%3 V").arg(v.real(),0,'g',7).arg(sign).arg(std::abs(v.imag()),0,'g',7);
            const QStringList values={f.name,QString::number(f.voltageV,'g',7),QString::number(f.phaseDeg,'g',7),QString::number(f.sourceOhm,'g',7),phasor};
            for(int c=0;c<values.size();++c)
            {
                auto*cell=new QTableWidgetItem(values[c]);
                if(c==0||c==4)cell->setFlags(cell->flags()&~Qt::ItemIsEditable);
                if(c==1||c==2)cell->setToolTip(QStringLiteral("Complex feed voltage = |V|∠phase. Multiple feeds are superposed in the same MoM solve."));
                else if(c==3)cell->setToolTip(QStringLiteral("Reference impedance used for Γ/S11/return loss/VSWR at this feed; it is not a physical series resistance."));
                else if(c==4)cell->setToolTip(QStringLiteral("Read-only rectangular form of the excitation phasor."));
                m_feedSimulationTable->setItem(r,c,cell);
            }
        }
        m_feedSimulationTable->blockSignals(wasBlocked);
    }
    m_planeTable->setRowCount(static_cast<int>(m_planes.size()));
    for(int r=0;r<static_cast<int>(m_planes.size());++r)
    {
        const auto&p=m_planes[static_cast<std::size_t>(r)];
        const auto stats=AntennaSurface::meshStats(AntennaSurface::triangulate(p.toSurfaceSpec(),6000));
        const QStringList values={p.name,surfaceKindText(p.surfaceType),QString::number(p.centerM.x(),'g',7),QString::number(p.centerM.y(),'g',7),QString::number(p.zM,'g',7),planeOrientationText(p.orientation),QString::number(p.yawDeg,'g',7),QString::number(p.pitchDeg,'g',7),QString::number(p.rollDeg,'g',7),QString::number(p.radiusM,'g',7),QString::number(p.innerRadiusM,'g',7),QString::number(p.widthM,'g',7),QString::number(p.heightM,'g',7),QString::number(p.focalLengthM,'g',7),QString::number(p.meshHintM,'g',7),QString::number(p.feedWidthM,'g',7),QString::number(p.feedLengthM,'g',7),QString::number(p.insetDepthM,'g',7),QString::number(p.notchGapM,'g',7),QString::number(static_cast<qulonglong>(stats.triangleCount)),QString::number(stats.areaM2,'g',7)};
        for(int c=0;c<values.size();++c){auto*cell=new QTableWidgetItem(values[c]);if(c==1)cell->setToolTip(QStringLiteral("Surface type: Rectangular sheet, Disk / annulus, Cylindrical shell, Conical shell, Parabolic reflector, or Inset patch + microstrip."));else if(c==5)cell->setToolTip(QStringLiteral("Base frame must be XY, XZ or YZ; Euler yaw/pitch/roll are then applied."));else if(c>=2&&c<=18)cell->setToolTip(QStringLiteral("Surface geometry / orientation parameter. Feed W/L, inset and notch gap are used by the Inset patch + microstrip surface kind. Dimensions are metres and angles are degrees."));if(c==19||c==20)cell->setFlags(cell->flags()&~Qt::ItemIsEditable);m_planeTable->setItem(r,c,cell);}
    }
    m_dielectricTable->setRowCount(static_cast<int>(m_dielectrics.size()));
    for(int r=0;r<static_cast<int>(m_dielectrics.size());++r)
    {
        const auto&d=m_dielectrics[static_cast<std::size_t>(r)];
        const QString lossFill=QStringLiteral("%1 / %2").arg(d.lossTangent,0,'g',7).arg(d.fieldFillFactor,0,'g',5);
        const QStringList values={d.name,QString::number(d.centerM.x(),'g',7),QString::number(d.centerM.y(),'g',7),QString::number(d.zM,'g',7),planeOrientationText(d.orientation),QString::number(d.yawDeg,'g',7),QString::number(d.pitchDeg,'g',7),QString::number(d.rollDeg,'g',7),QString::number(d.widthM,'g',7),QString::number(d.heightM,'g',7),QString::number(d.thicknessM,'g',7),QString::number(d.relativePermittivity,'g',7),lossFill};
        for(int c=0;c<values.size();++c){auto*cell=new QTableWidgetItem(values[c]);if(c==12)cell->setToolTip(QStringLiteral("Enter dielectric loss tangent and effective field-fill factor as tanδ / fill, for example 0.02 / 0.65."));m_dielectricTable->setItem(r,c,cell);}
    }
    auto refillFeedCombo=[&](QComboBox*combo){if(!combo)return;const QString previous=combo->currentText();const QSignalBlocker blocker(combo);combo->clear();for(const auto&f:m_feeds)combo->addItem(f.name);int idx=combo->findText(previous);if(idx<0&&!m_feeds.empty())idx=0;if(idx>=0)combo->setCurrentIndex(idx);};
    refillFeedCombo(m_reportedFeed);refillFeedCombo(m_sweepFeed);refillFeedCombo(m_optFeed);refillFeedCombo(m_multiFeed);refillFeedCombo(m_surfacePortFeed);refillFeedCombo(m_hybridPortFeed);
    if(m_hybridJunctionFeed){const QString previous=m_hybridJunctionFeed->currentText();const QSignalBlocker blocker(m_hybridJunctionFeed);m_hybridJunctionFeed->clear();m_hybridJunctionFeed->addItem(QStringLiteral("None"));for(const auto&f:m_feeds)m_hybridJunctionFeed->addItem(f.name);int idx=m_hybridJunctionFeed->findText(previous);if(idx<0)idx=0;m_hybridJunctionFeed->setCurrentIndex(idx);}
    auto refillSurfaceCombo=[&](QComboBox*combo){if(!combo)return;const QString previous=combo->currentText();const QSignalBlocker blocker(combo);combo->clear();combo->addItem(QStringLiteral("Auto / nearest"),-1);for(int i=0;i<static_cast<int>(m_planes.size());++i)combo->addItem(m_planes[static_cast<std::size_t>(i)].name,i);int idx=combo->findText(previous);if(idx<0)idx=0;combo->setCurrentIndex(idx);};
    refillSurfaceCombo(m_hybridPortSurface);refillSurfaceCombo(m_hybridJunctionSurface);
}

void AntennaDesignerWidget::refreshSummary()
{
    double totalLength=0.0;for(const auto&w:m_wires)totalLength+=segmentLength3D(w.aM,w.azM,w.bM,w.bzM);const double fHz=std::max(1.0,m_frequencyMHz->value()*1e6),lambda=C0/fHz;
    double zmin=0,zmax=0;bool haveZ=false;
    auto includeZ=[&](double z){if(!haveZ){zmin=zmax=z;haveZ=true;}else{zmin=std::min(zmin,z);zmax=std::max(zmax,z);}};
    for(const auto&w:m_wires){includeZ(w.azM);includeZ(w.bzM);}
    std::size_t surfaceTriangles=0; double surfaceArea=0.0; double maxSurfaceEdge=0.0;
    for(const auto&p:m_planes){const auto mesh=AntennaSurface::triangulate(p.toSurfaceSpec(),6000);const auto stats=AntennaSurface::meshStats(mesh);surfaceTriangles+=stats.triangleCount;surfaceArea+=stats.areaM2;maxSurfaceEdge=std::max(maxSurfaceEdge,stats.maxEdgeM);for(const auto&t:mesh)for(const auto&q:{t.a,t.b,t.c})includeZ(q.z);}
    for(const auto&d:m_dielectrics){AntennaSurface::SurfaceSpec spec;spec.center={d.centerM.x(),d.centerM.y(),d.zM};spec.baseOrientation=d.orientation;spec.yawDeg=d.yawDeg;spec.pitchDeg=d.pitchDeg;spec.rollDeg=d.rollDeg;for(double n:{-0.5*d.thicknessM,0.5*d.thicknessM})for(double u:{-0.5*d.widthM,0.5*d.widthM})for(double v:{-0.5*d.heightM,0.5*d.heightM})includeZ(AntennaSurface::pointFromLocal(spec,u,v,n).z);}
    m_summary->setText(QStringLiteral("%1 wire span(s), %2 PEC surface(s), %3 dielectric region(s), %4 feed/junction marker(s) — full XYZ geometry\nTotal conductor length: %5 m\nλ @ %6 MHz: %7 m\nTotal length / λ: %8\nZ extent: %9 … %10 m%11%12")
                           .arg(m_wires.size()).arg(m_planes.size()).arg(m_dielectrics.size()).arg(m_feeds.size()).arg(totalLength,0,'g',7).arg(m_frequencyMHz->value(),0,'g',7).arg(lambda,0,'g',7).arg(totalLength/lambda,0,'g',7).arg(zmin,0,'g',7).arg(zmax,0,'g',7)
                           .arg(m_planes.empty()?QString():QStringLiteral("\nPEC surface mesh preview: %1 triangle(s), area ≈ %2 m², max mesh edge ≈ %3 m. Thin-wire, standalone PEC RWG and coupled hybrid wire+PEC block-MoM solves are available.").arg(static_cast<qulonglong>(surfaceTriangles)).arg(surfaceArea,0,'g',7).arg(maxSurfaceEdge,0,'g',7))
                           .arg(m_dielectrics.empty()?QString():QStringLiteral("\nDielectric regions: hybrid solver can apply εr/tanδ through a localized effective-medium Green approximation. This is intended for patch/microstrip prototyping, not as an exact layered-medium solution.")));
}

void AntennaDesignerWidget::clearMomResults()
{
    if (m_momStatus) m_momStatus->setText(QStringLiteral("Not solved — geometry or frequency changed."));
    if (m_momFeedResults) m_momFeedResults->clear();
    if (m_momMetrics) m_momMetrics->clear();
    if (m_momConvergenceStatus) m_momConvergenceStatus->setText(QStringLiteral("Not checked — geometry, frequency or mesh settings changed."));
    if (m_momConvergenceTable) m_momConvergenceTable->clearContents();
    if (m_currentMagnitudePlot) m_currentMagnitudePlot->clearData();
    if (m_currentPhasePlot) m_currentPhasePlot->clearData();
    if (m_chargeMagnitudePlot) m_chargeMagnitudePlot->clearData();
    if (m_azimuthPlot) m_azimuthPlot->clearData();
    if (m_elevationPlot) m_elevationPlot->clearData();
    if (m_azimuthPolarPlot) m_azimuthPolarPlot->clearPattern();
    if (m_elevationPolarPlot) m_elevationPolarPlot->clearPattern();
    if (m_radiation3D) m_radiation3D->clearPattern();
    if (m_surfaceMomStatus) m_surfaceMomStatus->setText(QStringLiteral("Not solved — surface geometry, excitation or frequency changed."));
    if (m_surfaceMomMetrics) m_surfaceMomMetrics->clear();
    if (m_hybridMomStatus) m_hybridMomStatus->setText(QStringLiteral("Not solved — wire/surface geometry or frequency changed."));
    if (m_hybridMomMetrics) m_hybridMomMetrics->clear();
    if (m_surfaceCurrentPlot) m_surfaceCurrentPlot->clearData();
    if (m_surfaceRcsPlot) m_surfaceRcsPlot->clearData();
    if (m_geometry3D) m_geometry3D->clearSurfaceCurrentSamples();
}

void AntennaDesignerWidget::clearSweepResults()
{
    if (m_sweepStatus) m_sweepStatus->setText(QStringLiteral("No current sweep — geometry or sweep reference changed."));
    if (m_sweepSummary) m_sweepSummary->clear();
    if (m_sweepZPlot) { m_sweepZPlot->clearData(); m_sweepZPlot->clearMarkers(); }
    if (m_sweepS11Plot) { m_sweepS11Plot->clearData(); m_sweepS11Plot->clearMarkers(); }
    if (m_sweepVswrPlot) { m_sweepVswrPlot->clearData(); m_sweepVswrPlot->clearMarkers(); }
    if (m_sweepSmith) m_sweepSmith->clearSweep();
}

void AntennaDesignerWidget::clearOptimizationResults()
{
    m_optimizedWires.clear();
    m_optimizedFeeds.clear();
    if (m_optStatus) m_optStatus->setText(QStringLiteral("No current optimization — geometry or optimizer settings changed."));
    if (m_optSummary) m_optSummary->clear();
    if (m_applyOptimized) m_applyOptimized->setEnabled(false);
    if (m_optimizationPlot) { m_optimizationPlot->clearData(); m_optimizationPlot->clearMarkers(); }
}

void AntennaDesignerWidget::clearMultiOptimizationResults()
{
    m_multiOptimizedWires.clear();
    m_multiOptimizedFeeds.clear();
    m_individualOptimizedWires.clear();
    m_individualOptimizedFeeds.clear();
    if (m_multiStatus) m_multiStatus->setText(QStringLiteral("No current multi-parameter optimization — geometry or optimizer settings changed."));
    if (m_multiSummary) m_multiSummary->clear();
    if (m_applyMultiOptimized) m_applyMultiOptimized->setEnabled(false);
    if (m_multiOptimizationPlot) { m_multiOptimizationPlot->clearData(); m_multiOptimizationPlot->clearMarkers(); }
    if (m_individualStatus) m_individualStatus->setText(QStringLiteral("No current individual-director optimization — geometry or optimizer settings changed."));
    if (m_individualSummary) m_individualSummary->clear();
    if (m_applyIndividualOptimized) m_applyIndividualOptimized->setEnabled(false);
    if (m_individualOptimizationPlot) { m_individualOptimizationPlot->clearData(); m_individualOptimizationPlot->clearMarkers(); }
}

void AntennaDesignerWidget::solveGeneralizedMom()
{
    if (m_wires.empty() || m_feeds.empty())
    {
        m_momStatus->setText(QStringLiteral("ERROR: add at least one conductor and one feed."));
        return;
    }

    NumericalEM::WireNetworkMomInput in;
    in.frequencyHz = m_frequencyMHz->value() * 1e6;
    in.segmentsPerWavelength = m_segmentsPerWavelength->value();
    in.maxUnknowns = m_maxMomUnknowns->value();
    in.nodeMergeToleranceM = 1e-6;
    in.maxSegmentLengthRadiusFactor = m_radiusMeshFactor ? m_radiusMeshFactor->value() : 3.5;
    in.junctionLocalSubdivisions = m_junctionLocalSubdivisions ? m_junctionLocalSubdivisions->value() : 3;
    in.junctionTreatment = (m_junctionTreatment && m_junctionTreatment->currentIndex()==1) ? NumericalEM::WireJunctionTreatment::LagrangeKcl : NumericalEM::WireJunctionTreatment::ReducedBasis;
    in.currentBasisTreatment = (m_currentBasisTreatment && m_currentBasisTreatment->currentIndex()==1) ? NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge : NumericalEM::WireCurrentBasisTreatment::PulsePointMatching;
    in.wires.reserve(m_wires.size());
    for (const auto &w : m_wires)
        in.wires.push_back(NumericalEM::WireSpan3D{w.name.toStdString(),
                                                   {w.aM.x(), w.aM.y(), w.azM},
                                                   {w.bM.x(), w.bM.y(), w.bzM},
                                                   w.radiusM});
    in.feeds.reserve(m_feeds.size());
    for (const auto &f : m_feeds)
    {
        const double phase = f.phaseDeg * NumericalEM::Pi / 180.0;
        const std::complex<double> v = std::polar(f.voltageV, phase);
        in.feeds.push_back(NumericalEM::WireFeed3D{f.name.toStdString(),
                                                   {f.positionM.x(), f.positionM.y(), f.zM},
                                                   v, f.sourceOhm});
    }

    m_momStatus->setText(QStringLiteral("Solving dense generalized MoM system…"));
    m_momFeedResults->clear();
    m_momMetrics->clear();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();
    QElapsedTimer timer;
    timer.start();
    const auto r = NumericalEM::solveWireNetworkMom(in);
    const qint64 elapsedMs = timer.elapsed();
    QApplication::restoreOverrideCursor();

    if (!r.valid)
    {
        m_momStatus->setText(QStringLiteral("ERROR: %1").arg(QString::fromStdString(r.error)));
        m_currentMagnitudePlot->clearData();
        m_currentPhasePlot->clearData();
        if (m_chargeMagnitudePlot) m_chargeMagnitudePlot->clearData();
        m_azimuthPlot->clearData();
        m_elevationPlot->clearData();
        if (m_azimuthPolarPlot) m_azimuthPolarPlot->clearPattern();
        if (m_elevationPolarPlot) m_elevationPolarPlot->clearPattern();
        if (m_radiation3D) m_radiation3D->clearPattern();
        return;
    }

    const QString junctionSolveSummary = r.linearRooftopBasisUsed
        ? QStringLiteral("%1 mesh span(s) → %2 linear rooftop current DOF(s); %3 open-end + %4 degree-2 + %5 branch continuity condition(s)")
              .arg(r.wireUnknownCount).arg(r.solvedWireDofCount).arg(r.openEndConstraintCount).arg(r.ordinaryContinuityConstraintCount).arg(r.junctionConstraintCount)
        : (r.reducedJunctionBasisUsed
            ? QStringLiteral("%1 physical wire pulse(s) → %2 current DOF(s) with reduced junction basis (%3 independent KCL relation(s), %4 redundant)")
                  .arg(r.wireUnknownCount).arg(r.solvedWireDofCount).arg(r.junctionConstraintCount).arg(r.redundantJunctionConstraintCount)
            : QStringLiteral("%1 wire pulse(s) + %2 independent branch KCL multiplier(s) (%3 redundant relation(s) removed)")
                  .arg(r.wireUnknownCount).arg(r.junctionConstraintCount).arg(r.redundantJunctionConstraintCount));
    m_momStatus->setText(QStringLiteral("Solved in %1 ms — %2 = %3 algebraic unknown(s), %4 connected component(s)%5")
                             .arg(elapsedMs).arg(junctionSolveSummary).arg(r.unknownCount).arg(r.componentCount)
                             .arg(m_planes.empty()?QString():QStringLiteral(" — %1 surface element(s) preview-only / ignored by thin-wire MoM").arg(m_planes.size())));
    m_momStatus->setToolTip(QString::fromStdString(r.note));

    auto complexOhm = [](const std::complex<double> &z) {
        return QStringLiteral("%1 %2 j%3 Ω")
            .arg(z.real(), 0, 'g', 7)
            .arg(z.imag() >= 0.0 ? QStringLiteral("+") : QStringLiteral("−"))
            .arg(std::abs(z.imag()), 0, 'g', 7);
    };
    QStringList feedLines;
    for (std::size_t fi=0;fi<r.feeds.size();++fi)
    {
        const auto &f=r.feeds[fi];
        const double gm = std::abs(f.reflectionCoefficient);
        const QString vswr = std::isfinite(f.vswr) ? QString::number(f.vswr, 'g', 6) : QStringLiteral("∞");
        const QString excitation=fi<m_feeds.size()?QStringLiteral("%1 V ∠ %2° | Zref %3 Ω | ").arg(m_feeds[fi].voltageV,0,'g',6).arg(m_feeds[fi].phaseDeg,0,'g',6).arg(m_feeds[fi].sourceOhm,0,'g',6):QString();
        feedLines << QStringLiteral("%1 — %2active Zin = %3 | |Γ| = %4 | RL = %5 dB | VSWR = %6")
                         .arg(QString::fromStdString(f.name)).arg(excitation).arg(complexOhm(f.activeImpedanceOhm))
                         .arg(gm, 0, 'g', 6).arg(f.returnLossDb, 0, 'g', 6).arg(vswr);
    }
    m_momFeedResults->setText(feedLines.join(QStringLiteral("\n")));
    if (!r.feeds.empty())
    {
        int reported=m_reportedFeed?m_reportedFeed->currentIndex():0;
        if(reported<0||reported>=static_cast<int>(r.feeds.size()))reported=0;
        const auto &selected = r.feeds[static_cast<std::size_t>(reported)];
        const double z0 = reported<static_cast<int>(m_feeds.size()) ? m_feeds[static_cast<std::size_t>(reported)].sourceOhm : 50.0;
        emit antennaImpedanceAvailable(in.frequencyHz, selected.activeImpedanceOhm.real(),
                                       selected.activeImpedanceOhm.imag(), z0,
                                       QString::fromStdString(selected.name));
    }

    const double balance = std::abs(r.acceptedPowerW) > 1e-18 ? r.radiatedPowerW / r.acceptedPowerW : 0.0;
    const double losslessGainDbi = r.directivityDbi;
    double realizedGainDbi = std::numeric_limits<double>::quiet_NaN();
    if (r.feeds.size() == 1)
    {
        const double gm = std::abs(r.feeds.front().reflectionCoefficient);
        if (gm < 1.0)
            realizedGainDbi = losslessGainDbi + 10.0*std::log10(std::max(1e-15,1.0-gm*gm));
    }
    const QString realizedGainText = std::isfinite(realizedGainDbi)
        ? QStringLiteral("%1 dBi").arg(realizedGainDbi,0,'g',6)
        : QStringLiteral("n/a (multi-feed or |Γ|≥1)");
    const QString basisName=r.linearRooftopBasisUsed?QStringLiteral("linear rooftop + charge (experimental)"):(r.reducedJunctionBasisUsed?QStringLiteral("pulse + reduced T/Y/X basis"):QStringLiteral("pulse + legacy Lagrange KCL"));
    m_momMetrics->setText(QStringLiteral("Current basis = %1 | algebraic DOF = %2 | current constraints = %3\nBranch KCL = %4 independent / %5 redundant | branch nodes = %6 (%7 arms)\nOpen-end Imax/Ipeak = %8 % | max |λ| = %9 C/m | |ΣQ| = %10 C\nDmax = %11 linear / %12 dBi | Gmax = %24 dBi (lossless wire model) | Greal,max = %25\nresidual = %13 | max local continuity residual = %14 %\nmax branch KCL = %15 A (%16 % of Ipeak)\nMesh Δl = %17…%18 mm | max Δl/r = %19 | junction extra pulses = %20\nPrad = %21 W | Paccepted = %22 W | Prad/Paccepted = %23")
                              .arg(basisName).arg(r.solvedWireDofCount).arg(r.currentConstraintCount)
                              .arg(r.junctionConstraintCount).arg(r.redundantJunctionConstraintCount).arg(r.branchedJunctionCount).arg(r.branchedArmCount)
                              .arg(100.0*r.maxOpenEndCurrentRelative,0,'g',5).arg(r.maxLineChargeDensityCpm,0,'g',5).arg(std::abs(r.netContinuityChargeC),0,'g',5)
                              .arg(r.directivityLinear,0,'g',6).arg(r.directivityDbi,0,'g',6).arg(r.residualRelative,0,'g',5).arg(100.0*r.maxJunctionCurrentDiscontinuity,0,'g',5)
                              .arg(r.maxBranchKclResidualA,0,'g',5).arg(100.0*r.maxBranchKclRelative,0,'g',5)
                              .arg(1000.0*r.minMeshSegmentM,0,'g',5).arg(1000.0*r.maxMeshSegmentM,0,'g',5).arg(r.maxMeshSegmentToRadius,0,'g',5).arg(r.junctionRefinedExtraPulseCount)
                              .arg(r.radiatedPowerW,0,'g',6).arg(r.acceptedPowerW,0,'g',6).arg(balance,0,'g',6)
                              .arg(losslessGainDbi,0,'g',6).arg(realizedGainText));
    m_momMetrics->setToolTip(QString::fromStdString(r.note));

    // 6.0.1 topology/electrical-length sanity diagnostics.  The generalized wire
    // feed is a delta-gap excitation on an interior degree-2 conductor node.  Two
    // disconnected collinear arms carrying two independent feeds are therefore a
    // two-source array, not the canonical one-port centre-fed dipole.
    {
        const double fHz = std::max(1.0, in.frequencyHz);
        const double lambda = C0 / fHz;
        double totalMetalLength = 0.0;
        for (const auto &w : m_wires) totalMetalLength += segmentLength3D(w.aM,w.azM,w.bM,w.bzM);
        int activeFeedCount = 0;
        for (const auto &f : m_feeds) if (std::abs(f.voltageV) > 1e-12) ++activeFeedCount;

        bool splitDipoleLike = false;
        double centreGapM = std::numeric_limits<double>::quiet_NaN();
        if (r.componentCount == 2 && m_wires.size() == 2 && activeFeedCount >= 2)
        {
            const auto &w0=m_wires[0], &w1=m_wires[1];
            const NumericalEM::Vec3 a0{w0.aM.x(),w0.aM.y(),w0.azM}, b0{w0.bM.x(),w0.bM.y(),w0.bzM};
            const NumericalEM::Vec3 a1{w1.aM.x(),w1.aM.y(),w1.azM}, b1{w1.bM.x(),w1.bM.y(),w1.bzM};
            const auto d0=b0-a0, d1=b1-a1;
            const double n0=NumericalEM::norm(d0), n1=NumericalEM::norm(d1);
            const double alignment=(n0>1e-15&&n1>1e-15)?std::abs(NumericalEM::dot(d0,d1)/(n0*n1)):0.0;
            centreGapM=std::min({NumericalEM::norm(a0-a1),NumericalEM::norm(a0-b1),NumericalEM::norm(b0-a1),NumericalEM::norm(b0-b1)});
            splitDipoleLike=alignment>0.995 && std::isfinite(centreGapM) && centreGapM < 0.10*std::max(totalMetalLength,1e-12);
        }

        QString topologyNote;
        if (splitDipoleLike)
        {
            const double geometricHalfWaveMHz = totalMetalLength>1e-12 ? C0/(2.0*totalMetalLength)/1e6 : 0.0;
            const double shortenedResonantMHz = totalMetalLength>1e-12 ? 0.475*C0/totalMetalLength/1e6 : 0.0;
            const double exactHalfWaveTotalM = 0.5*lambda;
            const double resonantPresetTotalM = 0.475*lambda;
            topologyNote = QStringLiteral(
                "\n\n⚠ Topology check: %1 disconnected collinear conductor components are driven by %2 independent feed(s); centre gap ≈ %3 mm. "
                "This is a simultaneous two-source array, not the validated one-port centre-fed dipole. "
                "For a conventional dipole, make the two spans share one centre node and place ONE feed on that node (or use Presets → Wire — λ/2 dipole)."
                "\nElectrical-length check: metal length = %4 m = %5 λ at %6 MHz. A geometric 0.5 λ dipole of the same metal length corresponds to ≈ %7 MHz; "
                "the practical 0.475 λ resonant-length estimate corresponds to ≈ %8 MHz. At the current frequency, 0.5 λ = %9 m total (%10 m/arm), while the 0.475 λ preset is %11 m total (%12 m/arm).")
                .arg(r.componentCount).arg(activeFeedCount).arg(centreGapM*1e3,0,'g',6)
                .arg(totalMetalLength,0,'g',7).arg(totalMetalLength/lambda,0,'g',6).arg(fHz/1e6,0,'g',7)
                .arg(geometricHalfWaveMHz,0,'g',7).arg(shortenedResonantMHz,0,'g',7)
                .arg(exactHalfWaveTotalM,0,'g',7).arg(0.5*exactHalfWaveTotalM,0,'g',7)
                .arg(resonantPresetTotalM,0,'g',7).arg(0.5*resonantPresetTotalM,0,'g',7);
        }
        else if (r.componentCount > 1 || activeFeedCount > 1)
        {
            topologyNote = QStringLiteral(
                "\n\nTopology note: %1 connected conductor component(s), %2 active impressed feed(s). Multi-feed active impedances/Γ depend on the simultaneous amplitude/phase drive state; they are not an N-port S-parameter matrix.")
                .arg(r.componentCount).arg(activeFeedCount);
        }
        if (!topologyNote.isEmpty())
        {
            m_momMetrics->setText(m_momMetrics->text()+topologyNote);
            if (splitDipoleLike && m_momStatus)
                m_momStatus->setText(m_momStatus->text()+QStringLiteral(" — WARNING: split two-source topology; not a centre-fed dipole"));
        }
    }

    using WireSeriesKey = std::pair<int,int>; // component, source wire (-1 means whole degree-2 component)
    std::map<WireSeriesKey, QVector<double>> xByComponent, magByComponent, phaseByComponent;
    std::map<WireSeriesKey, double> previousPhase;
    for (const auto &seg : r.meshSegments)
    {
        const WireSeriesKey key{seg.componentIndex, r.branchedJunctionCount>0 ? seg.sourceWireIndex : -1};
        xByComponent[key].push_back(seg.pathCenterM);
        magByComponent[key].push_back(std::abs(seg.currentA));
        double ph = std::arg(seg.currentA) * 180.0 / NumericalEM::Pi;
        auto it = previousPhase.find(key);
        if (it != previousPhase.end())
        {
            while (ph - it->second > 180.0) ph -= 360.0;
            while (ph - it->second < -180.0) ph += 360.0;
        }
        previousPhase[key] = ph;
        phaseByComponent[key].push_back(ph);
    }
    QVector<FieldProfileSeries> magSeries, phaseSeries;
    for (const auto &[key, x] : xByComponent)
    {
        const QString stem = key.second>=0 ? QStringLiteral("C%1 / W%2").arg(key.first+1).arg(key.second+1)
                                           : QStringLiteral("C%1").arg(key.first+1);
        magSeries.push_back(FieldProfileSeries{x, magByComponent[key], stem+QStringLiteral(" |I|"), QStringLiteral("A"), false});
        phaseSeries.push_back(FieldProfileSeries{x, phaseByComponent[key], stem+QStringLiteral(" phase"), QStringLiteral("deg"), false});
    }
    const QString currentModel=r.linearRooftopBasisUsed?QStringLiteral("piecewise-linear rooftop current"):QStringLiteral("pulse current");
    m_currentMagnitudePlot->setSeries(magSeries, r.branchedJunctionCount>0
        ? QStringLiteral("Solved %1 distribution — branched components are separated by source wire to avoid false paths through a junction").arg(currentModel)
        : QStringLiteral("Solved %1 distribution — each connected conductor is a separate series").arg(currentModel));
    m_currentPhasePlot->setSeries(phaseSeries, r.branchedJunctionCount>0
        ? QStringLiteral("Solved current phase — each branch/source wire is unwrapped independently")
        : QStringLiteral("Solved current phase — unwrapped independently on each conductor component"));

    std::map<int,std::vector<std::pair<double,double>>> chargeSamples;
    for(const auto &seg:r.meshSegments)
        chargeSamples[seg.sourceWireIndex].push_back({seg.sourceWirePathCenterM,std::abs(seg.lineChargeDensityCpm)});
    QVector<FieldProfileSeries> chargeSeries;
    for(auto &[wireIndex,samples]:chargeSamples)
    {
        std::sort(samples.begin(),samples.end(),[](const auto &a,const auto &b){return a.first<b.first;});
        QVector<double> x,y;x.reserve(static_cast<qsizetype>(samples.size()));y.reserve(static_cast<qsizetype>(samples.size()));
        for(const auto &[position,lambdaMagnitude]:samples){x.push_back(position);y.push_back(lambdaMagnitude);}
        chargeSeries.push_back(FieldProfileSeries{x,y,QStringLiteral("W%1 |λ|").arg(wireIndex+1),QStringLiteral("C/m"),false});
    }
    if(m_chargeMagnitudePlot)m_chargeMagnitudePlot->setSeries(chargeSeries,r.linearRooftopBasisUsed
        ? QStringLiteral("Line charge from the solved linear current slope: λ = −(1/jω) dI/ds. Open-end current is zero by construction.")
        : QStringLiteral("Finite-difference line-charge diagnostic reconstructed from pulse-current variation. Use the rooftop basis for a continuous piecewise-linear charge model."));

    QVector<double> az, azDb, el, elDb;
    for (std::size_t i=0;i<r.azimuthDeg.size();++i)
    {
        az.push_back(r.azimuthDeg[i]);
        const double a = r.azimuthNormalizedFarField[i];
        azDb.push_back(a > 1e-12 ? std::max(-40.0,20.0*std::log10(a)) : -40.0);
    }
    for (std::size_t i=0;i<r.elevationDeg.size();++i)
    {
        el.push_back(r.elevationDeg[i]);
        const double a = r.elevationNormalizedFarField[i];
        elDb.push_back(a > 1e-12 ? std::max(-40.0,20.0*std::log10(a)) : -40.0);
    }
    m_azimuthPlot->setSeries({FieldProfileSeries{az,azDb,QStringLiteral("Global normalized far field"),QStringLiteral("dB"),false}},
                             QStringLiteral("Far-field azimuth cut — XY plane (θ = 90°)"));
    m_elevationPlot->setSeries({FieldProfileSeries{el,elDb,QStringLiteral("Global normalized far field"),QStringLiteral("dB"),false}},
                               QStringLiteral("Far-field elevation cut — φ = 0°"));
    if (m_azimuthPolarPlot)
        m_azimuthPolarPlot->setPattern(r.azimuthDeg, r.azimuthNormalizedFarField,
                                      QStringLiteral("Azimuth radiation pattern — XY plane (θ = 90°), global normalization"), false);
    if (m_elevationPolarPlot)
        m_elevationPolarPlot->setPattern(r.elevationDeg, r.elevationNormalizedFarField,
                                        QStringLiteral("Elevation radiation pattern — φ = 0°, global normalization"), true);
    if (m_radiation3D)
    {
        const double gainDbi = r.directivityDbi; // standalone wire model has no conductor/dielectric loss term
        double realizedGainDbi = std::numeric_limits<double>::quiet_NaN();
        if (r.feeds.size() == 1)
        {
            const double gm = std::abs(r.feeds.front().reflectionCoefficient);
            if (gm < 1.0)
                realizedGainDbi = gainDbi + 10.0*std::log10(std::max(1e-15, 1.0-gm*gm));
        }
        m_radiation3D->setPattern(r.farFieldThetaDeg, r.farFieldPhiDeg, r.farFieldNormalized,
                                  r.directivityDbi, gainDbi, realizedGainDbi, 1.0,
                                  r.maxRadiationThetaDeg, r.maxRadiationPhiDeg);
    }
    if (m_resultTabs) m_resultTabs->setCurrentWidget(m_currentMagnitudePlot);
}



void AntennaDesignerWidget::checkMomConvergence()
{
    if (!m_momConvergenceTable || !m_momConvergenceStatus) return;
    m_momConvergenceTable->clearContents();
    if (m_wires.empty() || m_feeds.empty())
    {
        m_momConvergenceStatus->setText(QStringLiteral("ERROR: add at least one conductor and one feed before checking convergence."));
        return;
    }

    NumericalEM::WireNetworkMomInput base;
    base.frequencyHz = m_frequencyMHz->value() * 1e6;
    base.segmentsPerWavelength = m_segmentsPerWavelength ? m_segmentsPerWavelength->value() : 80;
    base.maxUnknowns = m_maxMomUnknowns ? m_maxMomUnknowns->value() : 450;
    base.nodeMergeToleranceM = 1e-6;
    base.maxSegmentLengthRadiusFactor = m_radiusMeshFactor ? m_radiusMeshFactor->value() : 3.5;
    base.junctionLocalSubdivisions = m_junctionLocalSubdivisions ? m_junctionLocalSubdivisions->value() : 3;
    base.junctionTreatment = (m_junctionTreatment && m_junctionTreatment->currentIndex()==1) ? NumericalEM::WireJunctionTreatment::LagrangeKcl : NumericalEM::WireJunctionTreatment::ReducedBasis;
    base.currentBasisTreatment = (m_currentBasisTreatment && m_currentBasisTreatment->currentIndex()==1) ? NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge : NumericalEM::WireCurrentBasisTreatment::PulsePointMatching;
    base.computeFarField = false;
    base.wires.reserve(m_wires.size());
    for (const auto &w : m_wires)
        base.wires.push_back(NumericalEM::WireSpan3D{w.name.toStdString(),
                                                     {w.aM.x(), w.aM.y(), w.azM},
                                                     {w.bM.x(), w.bM.y(), w.bzM},
                                                     w.radiusM});
    base.feeds.reserve(m_feeds.size());
    for (const auto &f : m_feeds)
    {
        const double phase = f.phaseDeg * NumericalEM::Pi / 180.0;
        base.feeds.push_back(NumericalEM::WireFeed3D{f.name.toStdString(),
                                                     {f.positionM.x(), f.positionM.y(), f.zM},
                                                     std::polar(f.voltageV, phase), f.sourceOhm});
    }

    struct Level
    {
        QString name;
        int segmentsPerLambda = 80;
        double radiusFactor = 3.5;
        int junctionRefine = 3;
        NumericalEM::WireNetworkMomResult result;
    };
    std::array<Level,3> levels{{
        {QStringLiteral("Coarse"), std::max(10, int(std::lround(base.segmentsPerWavelength*0.75))), std::min(50.0, base.maxSegmentLengthRadiusFactor*1.35), std::max(1, base.junctionLocalSubdivisions-1), {}},
        {QStringLiteral("Current"), base.segmentsPerWavelength, base.maxSegmentLengthRadiusFactor, base.junctionLocalSubdivisions, {}},
        {QStringLiteral("Fine"), std::min(240, int(std::lround(base.segmentsPerWavelength*1.25))), std::max(2.0, base.maxSegmentLengthRadiusFactor*0.75), std::min(8, base.junctionLocalSubdivisions+1), {}}
    }};

    m_momConvergenceStatus->setText(QStringLiteral("Running three dense MoM meshes without far-field post-processing…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();
    QStringList failures;
    for (auto &level : levels)
    {
        auto in = base;
        in.segmentsPerWavelength = level.segmentsPerLambda;
        in.maxSegmentLengthRadiusFactor = level.radiusFactor;
        in.junctionLocalSubdivisions = level.junctionRefine;
        level.result = NumericalEM::solveWireNetworkMom(in);
        if (!level.result.valid)
            failures << QStringLiteral("%1: %2").arg(level.name).arg(QString::fromStdString(level.result.error));
        QApplication::processEvents();
    }
    QApplication::restoreOverrideCursor();

    int reported = m_reportedFeed ? m_reportedFeed->currentIndex() : 0;
    if (reported < 0) reported = 0;
    auto feedZ = [&](const Level &level, std::complex<double> &z) -> bool {
        if (!level.result.valid || reported >= static_cast<int>(level.result.feeds.size())) return false;
        z = level.result.feeds[static_cast<std::size_t>(reported)].activeImpedanceOhm;
        return std::isfinite(z.real()) && std::isfinite(z.imag());
    };
    auto zText = [](const std::complex<double> &z) {
        return QStringLiteral("%1 %2 j%3 Ω").arg(z.real(),0,'g',6)
            .arg(z.imag()>=0.0?QStringLiteral("+"):QStringLiteral("−"))
            .arg(std::abs(z.imag()),0,'g',6);
    };
    auto percentDelta = [](const std::complex<double> &a, const std::complex<double> &b) {
        return 100.0*std::abs(a-b)/std::max(std::abs(b),1e-12);
    };

    for (int i=0;i<3;++i)
    {
        const auto &level=levels[static_cast<std::size_t>(i)];
        const auto &r=level.result;
        const QStringList values{
            level.name,
            QString::number(level.segmentsPerLambda),
            QString::number(level.radiusFactor,'g',4),
            QString::number(level.junctionRefine),
            r.valid?QString::number(r.unknownCount):QStringLiteral("—"),
            QString(), QString(),
            r.valid?QStringLiteral("%1 %").arg(100.0*r.maxJunctionCurrentDiscontinuity,0,'g',4):QStringLiteral("—"),
            r.valid?QStringLiteral("%1 %").arg(100.0*r.maxBranchKclRelative,0,'g',4):QStringLiteral("—")
        };
        for(int c=0;c<values.size();++c) m_momConvergenceTable->setItem(i,c,new QTableWidgetItem(values[c]));
        std::complex<double> z{};
        if(feedZ(level,z)) m_momConvergenceTable->item(i,5)->setText(zText(z));
        else if(!r.valid) m_momConvergenceTable->item(i,5)->setText(QStringLiteral("ERROR"));
        if(i<2)
        {
            std::complex<double> z0{},z1{};
            if(feedZ(levels[static_cast<std::size_t>(i)],z0) && feedZ(levels[static_cast<std::size_t>(i+1)],z1))
                m_momConvergenceTable->item(i,6)->setText(QStringLiteral("%1 %").arg(percentDelta(z0,z1),0,'g',4));
            else m_momConvergenceTable->item(i,6)->setText(QStringLiteral("—"));
        }
        else m_momConvergenceTable->item(i,6)->setText(QStringLiteral("reference"));
    }

    if (!failures.isEmpty())
    {
        m_momConvergenceStatus->setText(QStringLiteral("Convergence study incomplete. %1\nIf the fine mesh exceeds Max unknowns, raise the limit gradually or reduce global refinement before retrying.").arg(failures.join(QStringLiteral(" | "))));
        return;
    }

    std::complex<double> zCurrent{},zFine{},zCoarse{};
    if(!feedZ(levels[0],zCoarse)||!feedZ(levels[1],zCurrent)||!feedZ(levels[2],zFine))
    {
        m_momConvergenceStatus->setText(QStringLiteral("Convergence study finished, but the reported feed impedance could not be extracted from every level."));
        return;
    }
    const double coarseToCurrent=percentDelta(zCoarse,zCurrent);
    const double currentToFine=percentDelta(zCurrent,zFine);
    QString verdict;
    if(currentToFine<1.0) verdict=QStringLiteral("excellent");
    else if(currentToFine<3.0) verdict=QStringLiteral("good");
    else if(currentToFine<10.0) verdict=QStringLiteral("marginal");
    else verdict=QStringLiteral("poor");
    const auto &fine=levels[2].result;
    QString convergenceText=QStringLiteral("Heuristic convergence: %1. |ΔZ| coarse→current = %2 %, current→fine = %3 %. Fine mesh: %4 physical span(s), Δl = %5…%6 mm, max Δl/r = %7, branch local extra spans = %8.\nUse the fine/current impedance stability as the primary check; KCL at T/Y/X nodes is constrained algebraically and can be tiny even when the mesh is not yet converged.")
        .arg(verdict).arg(coarseToCurrent,0,'g',4).arg(currentToFine,0,'g',4)
        .arg(fine.wireUnknownCount).arg(1000.0*fine.minMeshSegmentM,0,'g',5).arg(1000.0*fine.maxMeshSegmentM,0,'g',5)
        .arg(fine.maxMeshSegmentToRadius,0,'g',4).arg(fine.junctionRefinedExtraPulseCount);
    if(fine.linearRooftopBasisUsed)
        convergenceText += QStringLiteral("\nRooftop consistency: max free-end |I|/Ipeak = %1 %, |ΣQ| = %2 C, max |λ| = %3 C/m. These are conservation/boundary-condition checks, not an absolute validation of Zin.")
            .arg(100.0*fine.maxOpenEndCurrentRelative,0,'g',4).arg(std::abs(fine.netContinuityChargeC),0,'g',5).arg(fine.maxLineChargeDensityCpm,0,'g',5);
    m_momConvergenceStatus->setText(convergenceText);
    m_momConvergenceStatus->setToolTip(QStringLiteral("Thresholds are intentionally heuristic: <1% excellent, <3% good, <10% marginal, otherwise poor for the reported active feed impedance. Critical designs should still be checked against an independent NEC/FEM/full-wave solver."));
}

void AntennaDesignerWidget::runPhysicalValidation(bool allCases)
{
    if(!m_validationTable || !m_validationStatus || !m_validationCase) return;
    m_validationTable->setRowCount(0);
    if(m_validationPatternPlot){m_validationPatternPlot->clearData();m_validationPatternPlot->clearMarkers();}

    const double frequencyHz=std::max(1.0,m_frequencyMHz?m_frequencyMHz->value()*1e6:300e6);
    std::vector<AntennaValidation::CaseKind> cases;
    if(allCases)
        cases={AntennaValidation::CaseKind::HalfWaveDipole,
               AntennaValidation::CaseKind::SmallCircularLoop,
               AntennaValidation::CaseKind::QuarterWaveMonopole,
               AntennaValidation::CaseKind::RectangularPatch};
    else
        cases={static_cast<AntennaValidation::CaseKind>(std::clamp(m_validationCase->currentIndex(),0,3))};

    QProgressDialog progress(QStringLiteral("Running canonical antenna validation…"),QStringLiteral("Cancel"),0,static_cast<int>(cases.size()),this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(allCases?200:500);
    progress.setValue(0);
    QApplication::setOverrideCursor(Qt::WaitCursor);

    int pass=0,warning=0,fail=0,error=0,info=0;
    std::vector<AntennaValidation::CaseReport> reports;
    for(std::size_t ci=0;ci<cases.size();++ci)
    {
        if(progress.wasCanceled())break;
        progress.setLabelText(QStringLiteral("Validating %1…").arg(QString::fromLatin1(AntennaValidation::caseName(cases[ci]))));
        QApplication::processEvents();
        auto report=AntennaValidation::runCase(cases[ci],frequencyHz);
        for(const auto &row:report.rows)
        {
            const int rr=m_validationTable->rowCount();m_validationTable->insertRow(rr);
            auto set=[&](int col,const QString &text){auto *item=new QTableWidgetItem(text);m_validationTable->setItem(rr,col,item);return item;};
            set(0,QString::fromStdString(row.caseName));
            set(1,QString::fromStdString(row.solverName));
            set(2,row.valid?QStringLiteral("%1 → %2").arg(row.currentUnknowns).arg(row.fineUnknowns):QStringLiteral("—"));
            auto complexText=[](const std::complex<double>&z){return QStringLiteral("%1 %2 j%3 Ω").arg(z.real(),0,'g',6).arg(z.imag()>=0.0?QStringLiteral("+"):QStringLiteral("−")).arg(std::abs(z.imag()),0,'g',6);};
            set(3,row.valid?complexText(row.inputImpedanceOhm):QStringLiteral("ERROR"));
            set(4,row.valid?QStringLiteral("%1 %").arg(row.meshDeltaPercent,0,'g',4):QStringLiteral("—"));
            set(5,row.valid&&row.hasReferenceDirectivity?QStringLiteral("%1 dBi").arg(row.directivityDbi,0,'g',5):QStringLiteral("—"));
            set(6,row.valid&&row.hasReferenceDirectivity?QStringLiteral("%1 dB").arg(row.directivityErrorDb,0,'g',4):QStringLiteral("—"));
            set(7,row.valid&&std::isfinite(row.patternRmsPercent)?QStringLiteral("%1 %").arg(row.patternRmsPercent,0,'g',4):QStringLiteral("—"));
            QString primary;
            if(row.valid)
            {
                if(row.primaryMetricName=="Rrad")primary=QStringLiteral("Rrad %1 Ω / ref %2 Ω (Δ %3 %)").arg(row.primaryMetricValue,0,'g',5).arg(row.primaryMetricReference,0,'g',5).arg(row.primaryMetricErrorPercent,0,'g',4);
                else if(row.primaryMetricName=="f|minX|/f0")primary=QStringLiteral("min |X| @ %1 f0 (offset %2 %)").arg(row.primaryMetricValue,0,'g',4).arg(row.primaryMetricErrorPercent,0,'g',4);
                else if(row.primaryMetricName=="fJ2peak/f0")primary=QStringLiteral("modal ∫|Js|²dS peak @ %1 f0 (offset %2 %)").arg(row.primaryMetricValue,0,'g',4).arg(row.primaryMetricErrorPercent,0,'g',4);
                else if(row.hasReferenceImpedance)primary=QStringLiteral("ΔZref %1 %").arg(row.impedanceReferenceErrorPercent,0,'g',4);
            }
            set(8,primary.isEmpty()?QStringLiteral("—"):primary);
            const QString verdict=QString::fromLatin1(AntennaValidation::verdictName(row.verdict));
            auto *verdictItem=set(9,verdict);
            QString detail=QString::fromStdString(row.referenceSummary);
            if(!row.verdictReason.empty())detail+=QStringLiteral("\n\n%1").arg(QString::fromStdString(row.verdictReason));
            if(!row.error.empty())detail+=QStringLiteral("\n\nERROR: %1").arg(QString::fromStdString(row.error));
            if(row.reciprocityRelative>0.0)detail+=QStringLiteral("\nSolved mutual reciprocity relative error: %1").arg(row.reciprocityRelative,0,'g',5);
            if(row.reciprocityPreSymmetryRelative>0.0)detail+=QStringLiteral("\nPre-symmetry mutual quadrature mismatch: %1").arg(row.reciprocityPreSymmetryRelative,0,'g',5);
            detail+=QStringLiteral("\nSimulation/Designer 1.0 gate role: %1").arg(row.productionPath?QStringLiteral("PRODUCTION"):QStringLiteral("diagnostic / non-blocking"));
            if(row.patternMeshDeltaRmsPercent>0.0)detail+=QStringLiteral("\nCoarse/fine normalized-pattern RMS: %1 %").arg(row.patternMeshDeltaRmsPercent,0,'g',5);
            if(row.boundaryResidualRelative>0.0)detail+=QStringLiteral("\nBoundary/current-continuity relative residual: %1").arg(row.boundaryResidualRelative,0,'g',5);
            for(int c=0;c<m_validationTable->columnCount();++c)if(auto *item=m_validationTable->item(rr,c))item->setToolTip(detail);
            verdictItem->setToolTip(detail);
            switch(row.verdict){case AntennaValidation::Verdict::Pass:++pass;break;case AntennaValidation::Verdict::Warning:++warning;break;case AntennaValidation::Verdict::Fail:++fail;break;case AntennaValidation::Verdict::Error:++error;break;case AntennaValidation::Verdict::Informational:++info;break;}
        }
        reports.push_back(std::move(report));
        progress.setValue(static_cast<int>(ci+1));
        QApplication::processEvents();
    }
    QApplication::restoreOverrideCursor();
    progress.setValue(static_cast<int>(cases.size()));

    // Plot the first report that has an analytical normalized pattern reference.
    for(const auto &report:reports)
    {
        QVector<FieldProfileSeries> series;
        bool referenceAdded=false;
        for(const auto &row:report.rows)
        {
            if(!row.valid||row.patternAngleDeg.empty()||row.patternCalculated.size()!=row.patternAngleDeg.size())continue;
            QVector<double> x,y;x.reserve(static_cast<qsizetype>(row.patternAngleDeg.size()));y.reserve(static_cast<qsizetype>(row.patternCalculated.size()));
            for(double v:row.patternAngleDeg)x.push_back(v);for(double v:row.patternCalculated)y.push_back(v);
            series.push_back(FieldProfileSeries{x,y,QString::fromStdString(row.solverName),QString(),false});
            if(!referenceAdded && row.patternReference.size()==row.patternAngleDeg.size())
            {
                QVector<double> yr;yr.reserve(static_cast<qsizetype>(row.patternReference.size()));for(double v:row.patternReference)yr.push_back(v);
                series.push_back(FieldProfileSeries{x,yr,QStringLiteral("Analytical reference"),QString(),true});referenceAdded=true;
            }
        }
        if(!series.isEmpty()&&m_validationPatternPlot)
        {
            m_validationPatternPlot->setSeries(series,QStringLiteral("Canonical normalized pattern — %1").arg(QString::fromStdString(report.name)));
            break;
        }
    }

    int productionPass=0,productionWarning=0,productionFail=0,productionError=0,productionTotal=0;
    for(const auto &report:reports) for(const auto &row:report.rows) if(row.productionPath)
    {
        ++productionTotal;
        switch(row.verdict)
        {
            case AntennaValidation::Verdict::Pass:++productionPass;break;
            case AntennaValidation::Verdict::Warning:++productionWarning;break;
            case AntennaValidation::Verdict::Fail:++productionFail;break;
            case AntennaValidation::Verdict::Error:++productionError;break;
            case AntennaValidation::Verdict::Informational:break;
        }
    }
    const bool productionClosed=productionTotal>0 && productionPass==productionTotal;
    const bool canceled=progress.wasCanceled();
    m_validationStatus->setText(QStringLiteral("%1 at %2 MHz — PASS %3 | WARNING %4 | FAIL %5 | ERROR %6 | INFO %7.%8\n"
                                               "Simulation/Designer 1.0 production gates: %9/%10 PASS, warning %11, fail %12, error %13 — %14.\n"
                                               "The benchmark is non-destructive. Diagnostic/experimental rows remain visible but do not block the frozen 1.0 gate. The current FDTD workspace is 2D TMz/TEz and is intentionally not compared numerically with these 3D antenna cases.")
                                    .arg(allCases?QStringLiteral("Full canonical campaign"):QStringLiteral("Selected canonical benchmark"))
                                    .arg(frequencyHz/1e6,0,'g',8).arg(pass).arg(warning).arg(fail).arg(error).arg(info)
                                    .arg(canceled?QStringLiteral(" Campaign canceled before completion."):QString())
                                    .arg(productionPass).arg(productionTotal).arg(productionWarning).arg(productionFail).arg(productionError)
                                    .arg(productionClosed?QStringLiteral("CLOSED for the executed cases"):QStringLiteral("OPEN")));
    m_validationStatus->setToolTip(QStringLiteral("Frozen 1.0 production gates: pulse half-wave dipole; pulse electrically-small closed loop; rooftop+RWG finite-ground monopole; differential-RWG patch with modal ∫|Js|²dS resonance estimate. Legacy/experimental rows stay visible as diagnostics. Critical results still require an independent 3D solver or measurement."));
    m_validationTable->resizeRowsToContents();
    if(m_resultTabs && m_validationPatternPlot && !m_validationPatternPlot->isHidden() && !reports.empty())m_resultTabs->setCurrentWidget(m_validationPatternPlot);
}


void AntennaDesignerWidget::solveSurfaceMom()
{
    if (m_planes.empty())
    {
        if (m_surfaceMomStatus) m_surfaceMomStatus->setText(QStringLiteral("ERROR: add at least one PEC surface primitive."));
        return;
    }

    PecSurfaceMom::Input in;
    in.frequencyHz = m_frequencyMHz->value() * 1e6;
    in.maxUnknowns = m_surfaceMaxUnknowns ? m_surfaceMaxUnknowns->value() : 350;
    in.selfRegularizationFactor = m_surfaceSelfRegularization ? m_surfaceSelfRegularization->value() : 0.22;
    in.computeRcsCuts = true;
    in.rcsCutStepDeg = 2.0;
    const bool portMode = m_surfaceExcitationMode && m_surfaceExcitationMode->currentIndex()==1;
    in.excitationKind = portMode ? PecSurfaceMom::ExcitationKind::LumpedEdgePort : PecSurfaceMom::ExcitationKind::PlaneWave;

    const double az = (m_surfaceIncidenceAzDeg ? m_surfaceIncidenceAzDeg->value() : 0.0) * NumericalEM::Pi / 180.0;
    const double el = (m_surfaceIncidenceElDeg ? m_surfaceIncidenceElDeg->value() : -90.0) * NumericalEM::Pi / 180.0;
    const NumericalEM::Vec3 khat{std::cos(el)*std::cos(az), std::cos(el)*std::sin(az), std::sin(el)};
    const NumericalEM::Vec3 reference = std::abs(khat.x) < 0.9 ? NumericalEM::Vec3{1.0,0.0,0.0} : NumericalEM::Vec3{0.0,1.0,0.0};
    const auto proj = reference - khat * NumericalEM::dot(reference, khat);
    const auto u = NumericalEM::normalized(proj);
    const NumericalEM::Vec3 v{ khat.y*u.z-khat.z*u.y,
                               khat.z*u.x-khat.x*u.z,
                               khat.x*u.y-khat.y*u.x };
    const double pol = (m_surfacePolarizationDeg ? m_surfacePolarizationDeg->value() : 0.0) * NumericalEM::Pi / 180.0;
    const auto ehat = u*std::cos(pol) + v*std::sin(pol);
    in.excitation.propagationDirection = khat;
    in.excitation.electricFieldDirection = ehat;
    in.excitation.electricFieldAmplitudeVpm = {m_surfaceFieldVpm ? m_surfaceFieldVpm->value() : 1.0, 0.0};
    if (portMode)
    {
        const int fi = m_surfacePortFeed ? m_surfacePortFeed->currentIndex() : -1;
        if (fi < 0 || fi >= static_cast<int>(m_feeds.size()))
        {
            if (m_surfaceMomStatus) m_surfaceMomStatus->setText(QStringLiteral("ERROR: select a valid geometry feed for the RWG edge port."));
            return;
        }
        const auto &f=m_feeds[static_cast<std::size_t>(fi)];
        const double phase=f.phaseDeg*NumericalEM::Pi/180.0;
        in.port.positionM={f.positionM.x(),f.positionM.y(),f.zM};
        in.port.voltageV=std::polar(f.voltageV,phase);
        in.port.referenceOhm=f.sourceOhm;
    }

    const int totalBudget = m_surfaceMaxTriangles ? m_surfaceMaxTriangles->value() : 500;
    const std::size_t perSurfaceBudget = std::max<std::size_t>(2, static_cast<std::size_t>(totalBudget) / std::max<std::size_t>(1, m_planes.size()));
    for (std::size_t si=0; si<m_planes.size(); ++si)
    {
        const auto mesh = AntennaSurface::triangulate(m_planes[si].toSurfaceSpec(), perSurfaceBudget);
        for (const auto &t : mesh)
            in.triangles.push_back(PecSurfaceMom::Triangle3D{t.a,t.b,t.c,static_cast<int>(si)});
    }

    if (in.triangles.empty())
    {
        if (m_surfaceMomStatus) m_surfaceMomStatus->setText(QStringLiteral("ERROR: surface triangulation generated no valid triangles."));
        return;
    }

    if (m_surfaceMomStatus) m_surfaceMomStatus->setText(QStringLiteral("Solving dense PEC surface EFIE/RWG system…"));
    if (m_surfaceMomMetrics) m_surfaceMomMetrics->clear();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();
    QElapsedTimer timer; timer.start();
    const auto r = PecSurfaceMom::solve(in);
    const qint64 elapsedMs = timer.elapsed();
    QApplication::restoreOverrideCursor();

    if (!r.valid)
    {
        if (m_surfaceMomStatus) m_surfaceMomStatus->setText(QStringLiteral("ERROR: %1").arg(QString::fromStdString(r.error)));
        if (m_surfaceCurrentPlot) m_surfaceCurrentPlot->clearData();
        if (m_surfaceRcsPlot) m_surfaceRcsPlot->clearData();
        return;
    }

    if (m_surfaceMomStatus)
    {
        m_surfaceMomStatus->setText(QStringLiteral("Solved in %1 ms — %2 triangles, %3 RWG unknowns, %4 boundary edges%5")
                                    .arg(elapsedMs).arg(r.triangleCount).arg(r.rwgUnknownCount).arg(r.boundaryEdgeCount)
                                    .arg(r.drivenPort?QStringLiteral(" — driven edge port"):QStringLiteral(" — plane-wave scattering")));
        m_surfaceMomStatus->setToolTip(QString::fromStdString(r.note));
    }
    if (m_surfaceMomMetrics)
    {
        if (r.drivenPort)
        {
            auto complexOhm=[](const std::complex<double>&z){return QStringLiteral("%1 %2 j%3 Ω").arg(z.real(),0,'g',7).arg(z.imag()>=0?QStringLiteral("+"):QStringLiteral("−")).arg(std::abs(z.imag()),0,'g',7);};
            const double gm=std::abs(r.reflectionCoefficient);
            const QString vswr=std::isfinite(r.vswr)?QString::number(r.vswr,'g',6):QStringLiteral("∞");
            const double balance=std::abs(r.acceptedPowerW)>1e-18?r.radiatedPowerW/r.acceptedPowerW:0.0;
            m_surfaceMomMetrics->setText(QStringLiteral("Edge-port Zin = %1 | |Γ| = %2 | RL = %3 dB | VSWR = %4\nPort-to-edge distance = %5 m | basis #%6\nPeak |J| = %7 A/m | residual = %8\nDmax ≈ %9 dBi | Prad = %10 W | Paccepted = %11 W | Prad/Paccepted = %12")
                                         .arg(complexOhm(r.inputImpedanceOhm)).arg(gm,0,'g',6).arg(r.returnLossDb,0,'g',6).arg(vswr)
                                         .arg(r.drivenPortDistanceM,0,'g',6).arg(r.drivenPortBasisIndex+1)
                                         .arg(r.peakSurfaceCurrentApm,0,'g',7).arg(r.residualRelative,0,'g',5)
                                         .arg(r.directivityDbi,0,'g',6).arg(r.radiatedPowerW,0,'g',6).arg(r.acceptedPowerW,0,'g',6).arg(balance,0,'g',6));
            emit antennaImpedanceAvailable(in.frequencyHz,r.inputImpedanceOhm.real(),r.inputImpedanceOhm.imag(),in.port.referenceOhm,
                                           m_surfacePortFeed?m_surfacePortFeed->currentText():QStringLiteral("PEC edge port"));
        }
        else
        {
            const double rcsDbsm = 10.0*std::log10(std::max(1e-18,r.maxRcsM2));
            m_surfaceMomMetrics->setText(QStringLiteral("Peak |J| = %1 A/m\nResidual = %2 | pivots %3 … %4\nMax azimuth-cut RCS = %5 m² / %6 dBsm at %7°\nλ = %8 m | vertices = %9")
                                         .arg(r.peakSurfaceCurrentApm,0,'g',7)
                                         .arg(r.residualRelative,0,'g',5)
                                         .arg(r.minPivotAbs,0,'g',5).arg(r.maxPivotAbs,0,'g',5)
                                         .arg(r.maxRcsM2,0,'g',7).arg(rcsDbsm,0,'g',6).arg(r.maxRcsAzimuthDeg,0,'g',6)
                                         .arg(r.wavelengthM,0,'g',7).arg(r.uniqueVertexCount));
        }
        m_surfaceMomMetrics->setToolTip(QString::fromStdString(r.note));
    }

    QVector<double> triIndex, jMag;
    triIndex.reserve(static_cast<int>(r.triangleCurrents.size()));
    jMag.reserve(static_cast<int>(r.triangleCurrents.size()));
    for (int i=0; i<static_cast<int>(r.triangleCurrents.size()); ++i)
    {
        triIndex.push_back(i+1);
        jMag.push_back(r.triangleCurrents[static_cast<std::size_t>(i)].magnitudeApm);
    }
    if (m_surfaceCurrentPlot)
        m_surfaceCurrentPlot->setSeries({FieldProfileSeries{triIndex,jMag,QStringLiteral("|J| at triangle centroid"),QStringLiteral("A/m"),false}},
                                        QStringLiteral("PEC surface current density — low-order RWG centroid reconstruction"));
    if (m_geometry3D)
    {
        std::vector<NumericalEM::Vec3> points; std::vector<double> magnitude;
        points.reserve(r.triangleCurrents.size()); magnitude.reserve(r.triangleCurrents.size());
        for (const auto &tc : r.triangleCurrents) { points.push_back(tc.centroid); magnitude.push_back(tc.magnitudeApm); }
        m_geometry3D->setSurfaceCurrentSamples(points,magnitude);
    }

    QVector<double> azDeg, azValue, elDeg, elValue;
    if (r.drivenPort)
    {
        for (std::size_t i=0;i<r.azimuthDeg.size()&&i<r.azimuthNormalizedFarField.size();++i)
        {azDeg.push_back(r.azimuthDeg[i]);const double a=r.azimuthNormalizedFarField[i];azValue.push_back(a>1e-5?20.0*std::log10(a):-100.0);}
        for (std::size_t i=0;i<r.elevationDeg.size()&&i<r.elevationNormalizedFarField.size();++i)
        {elDeg.push_back(r.elevationDeg[i]);const double a=r.elevationNormalizedFarField[i];elValue.push_back(a>1e-5?20.0*std::log10(a):-100.0);}
        if (m_surfaceRcsPlot)
            m_surfaceRcsPlot->setSeries({
                FieldProfileSeries{azDeg,azValue,QStringLiteral("Driven azimuth XY cut"),QStringLiteral("dB"),false},
                FieldProfileSeries{elDeg,elValue,QStringLiteral("Driven elevation φ=0 cut"),QStringLiteral("dB"),true}},
                QStringLiteral("Driven PEC edge-port normalized radiation pattern"));
    }
    else
    {
        for (std::size_t i=0;i<r.azimuthDeg.size();++i)
        {azDeg.push_back(r.azimuthDeg[i]);azValue.push_back(10.0*std::log10(std::max(1e-18,r.azimuthRcsM2[i])));}
        for (std::size_t i=0;i<r.elevationDeg.size();++i)
        {elDeg.push_back(r.elevationDeg[i]);elValue.push_back(10.0*std::log10(std::max(1e-18,r.elevationRcsM2[i])));}
        if (m_surfaceRcsPlot)
            m_surfaceRcsPlot->setSeries({
                FieldProfileSeries{azDeg,azValue,QStringLiteral("Azimuth XY cut"),QStringLiteral("dBsm"),false},
                FieldProfileSeries{elDeg,elValue,QStringLiteral("Elevation φ=0 cut"),QStringLiteral("dBsm"),true}},
                QStringLiteral("PEC scattered-field radar cross section — far-field cuts"));
    }
    if (m_resultTabs && m_surfaceCurrentPlot) m_resultTabs->setCurrentWidget(m_surfaceCurrentPlot);
}



bool AntennaDesignerWidget::buildHybridInput(double frequencyHz, bool computeFarField,
                                             HybridWireSurfaceMom::Input &in, QString &error) const
{
    in = HybridWireSurfaceMom::Input{};
    error.clear();
    const int portMode = m_hybridPortMode ? m_hybridPortMode->currentIndex() : 0;
    const bool differentialSurfacePort = portMode == 2;
    if (m_feeds.empty() || m_planes.empty() || (!differentialSurfacePort && m_wires.empty()))
    {
        error = differentialSurfacePort
            ? QStringLiteral("differential surface-port solve requires at least two feed markers and two PEC surfaces.")
            : QStringLiteral("hybrid solve requires at least one wire, one wire feed and one PEC surface.");
        return false;
    }

    in.wire.frequencyHz = frequencyHz;
    in.wire.segmentsPerWavelength = m_segmentsPerWavelength->value();
    in.wire.maxUnknowns = m_maxMomUnknowns->value();
    in.wire.nodeMergeToleranceM = 1e-6;
    in.wire.maxSegmentLengthRadiusFactor = m_radiusMeshFactor ? m_radiusMeshFactor->value() : 3.5;
    in.wire.junctionLocalSubdivisions = m_junctionLocalSubdivisions ? m_junctionLocalSubdivisions->value() : 3;
    in.wire.junctionTreatment = (m_junctionTreatment && m_junctionTreatment->currentIndex()==1)
        ? NumericalEM::WireJunctionTreatment::LagrangeKcl : NumericalEM::WireJunctionTreatment::ReducedBasis;
    in.wire.currentBasisTreatment = (m_currentBasisTreatment && m_currentBasisTreatment->currentIndex()==1)
        ? NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge : NumericalEM::WireCurrentBasisTreatment::PulsePointMatching;
    in.wire.computeFarField = false;
    if (!differentialSurfacePort)
    {
        in.wire.wires.reserve(m_wires.size());
        for (const auto &w : m_wires)
            in.wire.wires.push_back(NumericalEM::WireSpan3D{w.name.toStdString(),
                                                            {w.aM.x(),w.aM.y(),w.azM},
                                                            {w.bM.x(),w.bM.y(),w.bzM},w.radiusM});
    }

    const int endpointFeedIndex = ((portMode == 1 || portMode == 2) && m_hybridPortFeed)
        ? m_hybridPortFeed->currentIndex() : -1;
    const int junctionFeedIndex = (m_hybridJunctionFeed && m_hybridJunctionFeed->currentIndex() > 0)
        ? m_hybridJunctionFeed->currentIndex() - 1 : -1;
    if (endpointFeedIndex >= static_cast<int>(m_feeds.size()))
    {
        error = QStringLiteral("select a valid endpoint feed marker.");
        return false;
    }
    if (junctionFeedIndex >= static_cast<int>(m_feeds.size()))
    {
        error = QStringLiteral("select a valid galvanic-junction marker.");
        return false;
    }
    if (endpointFeedIndex >= 0 && endpointFeedIndex == junctionFeedIndex)
    {
        error = QStringLiteral("the same feed marker cannot be both the driven PEC-referenced endpoint port and an extra passive junction.");
        return false;
    }

    const double mappingToleranceM = (m_hybridMappingToleranceMm ? m_hybridMappingToleranceMm->value() : 10.0) * 1e-3;
    if (differentialSurfacePort)
    {
        if (endpointFeedIndex<0 || junctionFeedIndex<0 || endpointFeedIndex>=static_cast<int>(m_feeds.size()) || junctionFeedIndex>=static_cast<int>(m_feeds.size()))
        {
            error = QStringLiteral("differential surface mode needs two distinct feed markers: negative/return terminal and positive terminal.");
            return false;
        }
        const auto &neg=m_feeds[static_cast<std::size_t>(endpointFeedIndex)];
        const auto &pos=m_feeds[static_cast<std::size_t>(junctionFeedIndex)];
        const double phase=neg.phaseDeg*NumericalEM::Pi/180.0;
        HybridWireSurfaceMom::DifferentialSurfaceFeed sf;
        sf.name=neg.name.toStdString()+std::string(" ↔ ")+pos.name.toStdString();
        sf.negativePositionM={neg.positionM.x(),neg.positionM.y(),neg.zM};
        sf.positivePositionM={pos.positionM.x(),pos.positionM.y(),pos.zM};
        sf.voltageV=std::polar(neg.voltageV,phase); sf.referenceOhm=neg.sourceOhm;
        sf.negativeSurfaceIndex=m_hybridPortSurface?m_hybridPortSurface->currentData().toInt():-1;
        sf.positiveSurfaceIndex=m_hybridJunctionSurface?m_hybridJunctionSurface->currentData().toInt():-1;
        sf.mappingToleranceM=mappingToleranceM;
        sf.footprintRadiusM=(m_hybridDifferentialPortRadiusMm?m_hybridDifferentialPortRadiusMm->value():0.0)*1e-3;
        in.differentialSurfaceFeeds.push_back(sf);
    }
    else
    {
        for (int fi = 0; fi < static_cast<int>(m_feeds.size()); ++fi)
        {
            const auto &f = m_feeds[static_cast<std::size_t>(fi)];
            const double phase=f.phaseDeg*NumericalEM::Pi/180.0;
            const std::complex<double> voltage = std::polar(f.voltageV,phase);
            if (fi == endpointFeedIndex)
            {
                HybridWireSurfaceMom::SurfaceReferencedFeed hf;
                hf.name=f.name.toStdString(); hf.positionM={f.positionM.x(),f.positionM.y(),f.zM};
                hf.voltageV=voltage; hf.referenceOhm=f.sourceOhm;
                hf.surfaceIndex=m_hybridPortSurface ? m_hybridPortSurface->currentData().toInt() : -1;
                hf.mappingToleranceM=mappingToleranceM;
                hf.referenceModel = (m_hybridPortReferenceModel && m_hybridPortReferenceModel->currentIndex() == 1)
                    ? HybridWireSurfaceMom::PortReferenceModel::CoaxialReferencePlane
                    : HybridWireSurfaceMom::PortReferenceModel::AntennaPlane;
                hf.coaxInnerRadiusM = (m_hybridCoaxInnerRadiusMm ? m_hybridCoaxInnerRadiusMm->value() : 0.50) * 1e-3;
                hf.coaxOuterRadiusM = (m_hybridCoaxOuterRadiusMm ? m_hybridCoaxOuterRadiusMm->value() : 1.70) * 1e-3;
                hf.coaxRelativePermittivity = m_hybridCoaxEr ? m_hybridCoaxEr->value() : 2.10;
                hf.coaxLossTangent = m_hybridCoaxTanD ? m_hybridCoaxTanD->value() : 0.0;
                hf.coaxLengthM = (m_hybridCoaxLengthMm ? m_hybridCoaxLengthMm->value() : 0.0) * 1e-3;
                if (hf.referenceModel == HybridWireSurfaceMom::PortReferenceModel::CoaxialReferencePlane &&
                    (!(hf.coaxInnerRadiusM > 0.0) || !(hf.coaxOuterRadiusM > hf.coaxInnerRadiusM) ||
                     !(hf.coaxRelativePermittivity > 0.0) || hf.coaxLengthM < 0.0))
                {
                    error = QStringLiteral("coax reference plane requires b > a > 0, εr > 0 and length ≥ 0.");
                    return false;
                }
                in.surfaceReferencedFeeds.push_back(hf);
            }
            else if (fi == junctionFeedIndex)
            {
                HybridWireSurfaceMom::GalvanicJunction j;
                j.name=f.name.toStdString(); j.positionM={f.positionM.x(),f.positionM.y(),f.zM};
                j.surfaceIndex=m_hybridJunctionSurface ? m_hybridJunctionSurface->currentData().toInt() : -1;
                j.mappingToleranceM=mappingToleranceM;
                in.galvanicJunctions.push_back(j);
            }
            else
            {
                in.wire.feeds.push_back(NumericalEM::WireFeed3D{f.name.toStdString(),
                                                                {f.positionM.x(),f.positionM.y(),f.zM},
                                                                voltage,f.sourceOhm});
            }
        }
    }

    in.useEffectiveDielectricRegions = m_hybridUseDielectric ? m_hybridUseDielectric->isChecked() : true;
    const int dielectricKernelIndex=m_hybridDielectricKernel?m_hybridDielectricKernel->currentIndex():1;
    if(dielectricKernelIndex==0) in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::MidpointFill;
    else if(dielectricKernelIndex==2) in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabQuasiStatic;
    else if(dielectricKernelIndex==3) in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabSommerfeldScalar;
    else if(dielectricKernelIndex==4) in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabSommerfeldTeVector;
    else if(dielectricKernelIndex==5) in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabSommerfeldCrossFace;
    else if(dielectricKernelIndex==6) in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed;
    else if(dielectricKernelIndex==7) in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight;
    else if(dielectricKernelIndex==8) in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabSommerfeldInternalLayer;
    else if(dielectricKernelIndex==9) in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabSommerfeldVedNormal;
    else if(dielectricKernelIndex==10) in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal;
    else if(dielectricKernelIndex==11) in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabSommerfeldComplexPowerGuard;
    else if(dielectricKernelIndex==12) in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabSommerfeldPropagatingFarField;
    else if(dielectricKernelIndex==13) in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabSommerfeldSurfaceWavePoleAudit;
    else if(dielectricKernelIndex==14) in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabSommerfeldGroundedPecModeAudit;
    else in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::SegmentOverlapWeighted;

    in.enableTerminalHalfRwg = m_hybridTerminalHalfRwg ? m_hybridTerminalHalfRwg->isChecked() : true;
    in.useFiniteSurfaceConductivity = m_hybridFiniteConductivity ? m_hybridFiniteConductivity->isChecked() : false;
    in.surfaceConductivitySPerM = (m_hybridSurfaceConductivityMSm ? m_hybridSurfaceConductivityMSm->value() : 58.0) * 1e6;
    in.surfaceThicknessM = (m_hybridSurfaceThicknessUm ? m_hybridSurfaceThicknessUm->value() : 35.0) * 1e-6;
    if (in.useEffectiveDielectricRegions)
    {
        for (const auto &d : m_dielectrics)
        {
            AntennaSurface::SurfaceSpec frameSpec;
            frameSpec.center={d.centerM.x(),d.centerM.y(),d.zM};
            frameSpec.baseOrientation=d.orientation; frameSpec.yawDeg=d.yawDeg; frameSpec.pitchDeg=d.pitchDeg; frameSpec.rollDeg=d.rollDeg;
            const auto axes=AntennaSurface::frameAxes(frameSpec);
            HybridWireSurfaceMom::DielectricRegion rd;
            rd.name=d.name.toStdString(); rd.centerM=frameSpec.center; rd.uAxis=axes.u; rd.vAxis=axes.v; rd.normalAxis=axes.normal;
            rd.widthM=d.widthM; rd.heightM=d.heightM; rd.thicknessM=d.thicknessM;
            rd.relativePermittivity=d.relativePermittivity; rd.lossTangent=d.lossTangent; rd.fieldFillFactor=d.fieldFillFactor;
            in.dielectrics.push_back(rd);
        }
    }

    in.maxTotalUnknowns = m_hybridMaxUnknowns ? m_hybridMaxUnknowns->value() : 450;
    in.maxSurfaceUnknowns = m_surfaceMaxUnknowns ? m_surfaceMaxUnknowns->value() : 350;
    in.surfaceSelfRegularizationFactor = m_surfaceSelfRegularization ? m_surfaceSelfRegularization->value() : 0.22;
    in.mutualRegularizationFactor = m_hybridMutualRegularization ? m_hybridMutualRegularization->value() : 0.08;
    in.computeFarField = computeFarField;
    in.farFieldCutStepDeg = 2.0;
    in.farFieldIntegrationStepDeg = 5.0;

    const int totalBudget = m_surfaceMaxTriangles ? m_surfaceMaxTriangles->value() : 500;
    const std::size_t perSurfaceBudget = std::max<std::size_t>(2, static_cast<std::size_t>(totalBudget) / std::max<std::size_t>(1,m_planes.size()));
    for (std::size_t si=0; si<m_planes.size(); ++si)
    {
        const auto mesh=AntennaSurface::triangulate(m_planes[si].toSurfaceSpec(),perSurfaceBudget);
        for(const auto&t:mesh)in.triangles.push_back(PecSurfaceMom::Triangle3D{t.a,t.b,t.c,static_cast<int>(si)});
    }
    if(in.triangles.empty())
    {
        error = QStringLiteral("surface triangulation generated no valid triangles.");
        return false;
    }
    return true;
}

int AntennaDesignerWidget::hybridResultFeedIndex(const HybridWireSurfaceMom::Result &result, int requestedFeedIndex) const
{
    if (result.feeds.empty()) return -1;
    const int portMode = m_hybridPortMode ? m_hybridPortMode->currentIndex() : 0;
    if (portMode == 2)
        return 0; // one differential surface port is represented by two geometry markers.
    if (requestedFeedIndex >= 0 && requestedFeedIndex < static_cast<int>(m_feeds.size()))
    {
        const std::string target = m_feeds[static_cast<std::size_t>(requestedFeedIndex)].name.toStdString();
        for (int i=0;i<static_cast<int>(result.feeds.size());++i)
            if (result.feeds[static_cast<std::size_t>(i)].name == target) return i;
    }
    return requestedFeedIndex >= 0 && requestedFeedIndex < static_cast<int>(result.feeds.size()) ? requestedFeedIndex : 0;
}

void AntennaDesignerWidget::solveHybridMom()
{
    HybridWireSurfaceMom::Input in;
    QString inputError;
    if (!buildHybridInput(m_frequencyMHz->value() * 1e6, true, in, inputError))
    {
        if (m_hybridMomStatus) m_hybridMomStatus->setText(QStringLiteral("ERROR: %1").arg(inputError));
        return;
    }

    if(m_hybridMomStatus)m_hybridMomStatus->setText(QStringLiteral("Solving coupled dense wire + RWG block system…"));
    if(m_hybridMomMetrics)m_hybridMomMetrics->clear();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();
    QElapsedTimer timer;timer.start();
    const auto r=HybridWireSurfaceMom::solve(in);
    const qint64 elapsedMs=timer.elapsed();
    QApplication::restoreOverrideCursor();

    if(!r.valid)
    {
        if(m_hybridMomStatus)m_hybridMomStatus->setText(QStringLiteral("ERROR: %1").arg(QString::fromStdString(r.error)));
        return;
    }

    if(m_chargeMagnitudePlot)m_chargeMagnitudePlot->clearData();

    if(m_hybridMomStatus)
    {
        const QString wirePart=r.differentialSurfacePortUsed
            ? QStringLiteral("ideal differential PEC surface port (%1 port(s), no explicit wire unknowns)").arg(r.differentialSurfacePortCount)
            : (r.wireLinearRooftopBasisUsed
            ? QStringLiteral("%1 physical wire spans → %2 rooftop DOFs (%3 free open-end constraints, %4 continuity nodes, %5 PEC terminal node(s))")
                  .arg(r.wireUnknownCount).arg(r.wireSolvedDofCount).arg(r.wireOpenEndConstraintCount).arg(r.wireOrdinaryContinuityConstraintCount).arg(r.wireTerminalNodeCount)
            : (r.reducedWireJunctionBasisUsed
                ? QStringLiteral("%1 physical wire pulses → %2 current DOFs (reduced junction basis, rank %3, %4 redundant)").arg(r.wireUnknownCount).arg(r.wireSolvedDofCount).arg(r.wireBranchConstraintCount).arg(r.redundantWireBranchConstraintCount)
                : QStringLiteral("%1 wire pulses + %2 branch KCL multipliers (%3 redundant removed)").arg(r.wireUnknownCount).arg(r.wireBranchConstraintCount).arg(r.redundantWireBranchConstraintCount)));
        m_hybridMomStatus->setText(QStringLiteral("Solved in %1 ms — %2 + %3 surface (%4 terminal half-RWG) + %5 wire/PEC constraint(s) = %6 total unknowns, %7 triangles, %8 dielectric region(s)")
                                   .arg(elapsedMs).arg(wirePart).arg(r.surfaceUnknownCount).arg(r.halfRwgUnknownCount).arg(r.junctionConstraintCount).arg(r.totalUnknownCount).arg(r.triangleCount).arg(r.dielectricRegionCount));
        m_hybridMomStatus->setToolTip(QString::fromStdString(r.note));
    }
    auto complexOhm=[](const std::complex<double>&z){return QStringLiteral("%1 %2 j%3 Ω").arg(z.real(),0,'g',7).arg(z.imag()>=0?QStringLiteral("+"):QStringLiteral("−")).arg(std::abs(z.imag()),0,'g',7);};
    QStringList feedLines;
    for(const auto&f:r.feeds)
    {
        const QString vswr=std::isfinite(f.vswr)?QString::number(f.vswr,'g',6):QStringLiteral("∞");
        if (f.surfaceReferenced)
        {
            QString portLine = (r.differentialSurfacePortUsed
                ? QStringLiteral("%1 — differential PEC surface lumped port: Zin = %2\n|Γ| = %3 | RL = %4 dB | VSWR = %5 | positive surface = %6 | representative RWG = %7 | mapping distance = %8 mm")
                : QStringLiteral("%1 — PEC-referenced endpoint port: Zin = %2\n|Γ| = %3 | RL = %4 dB | VSWR = %5 | mapped surface = %6 | RWG basis = %7 | mapping distance = %8 mm"))
                               .arg(QString::fromStdString(f.name),complexOhm(f.inputImpedanceOhm))
                               .arg(std::abs(f.reflectionCoefficient),0,'g',6).arg(f.returnLossDb,0,'g',6).arg(vswr)
                               .arg(f.mappedSurfaceIndex).arg(f.mappedSurfaceBasisIndex).arg(f.mappingDistanceM*1e3,0,'g',6);
            if (f.referencePlaneCorrected)
            {
                portLine += QStringLiteral("\nReference-plane model: antenna-terminal Zin = %1 | coax Z0 ≈ %2 Ω | electrical length ≈ %3°")
                                .arg(complexOhm(f.antennaPlaneInputImpedanceOhm))
                                .arg(f.feedLineZ0Ohm,0,'g',6).arg(f.feedLineElectricalLengthDeg,0,'g',6);
            }
            feedLines << portLine;
        }
        else
        {
            const auto dz=f.inputImpedanceOhm-f.uncoupledInputImpedanceOhm;
            feedLines << QStringLiteral("%1 — hybrid Zin = %2 | uncoupled wire = %3 | ΔZsurface = %4\n|Γ| = %5 | RL = %6 dB | VSWR = %7")
                         .arg(QString::fromStdString(f.name),complexOhm(f.inputImpedanceOhm),complexOhm(f.uncoupledInputImpedanceOhm),complexOhm(dz))
                         .arg(std::abs(f.reflectionCoefficient),0,'g',6).arg(f.returnLossDb,0,'g',6).arg(vswr);
        }
    }
    QStringList junctionLines;
    for (const auto &j : r.junctions)
    {
        junctionLines << QStringLiteral("%1 — wire I = %2 %3 j%4 A | RWG edge I = %5 %6 j%7 A | mismatch = %8 A | surface %9 / basis %10 | map = %11 mm")
                         .arg(QString::fromStdString(j.name))
                         .arg(j.wireCurrentSumA.real(),0,'g',6).arg(j.wireCurrentSumA.imag()>=0?QStringLiteral("+"):QStringLiteral("−")).arg(std::abs(j.wireCurrentSumA.imag()),0,'g',6)
                         .arg(j.surfaceEdgeCurrentA.real(),0,'g',6).arg(j.surfaceEdgeCurrentA.imag()>=0?QStringLiteral("+"):QStringLiteral("−")).arg(std::abs(j.surfaceEdgeCurrentA.imag()),0,'g',6)
                         .arg(j.currentMismatchA,0,'g',5).arg(j.mappedSurfaceIndex).arg(j.mappedSurfaceBasisIndex).arg(j.surfaceMappingDistanceM*1e3,0,'g',6);
    }
    if(m_hybridMomMetrics)
    {
        QString metrics=feedLines.join(QStringLiteral("\n"));
        if(!junctionLines.isEmpty()) metrics += QStringLiteral("\n\nWire ↔ PEC current-continuity constraints:\n") + junctionLines.join(QStringLiteral("\n"));
        metrics += QStringLiteral("\n\nPeak |Iwire| = %1 A | Peak |Jsurface| = %2 A/m\nResidual = %3 | normalized mutual-block RMS = %4 | solved reciprocity Δmutual = %5 | max wire/PEC mismatch = %6 A\nInternal branch KCL constraints = %7 | max branch residual = %8 A")
            .arg(r.peakWireCurrentA,0,'g',7).arg(r.peakSurfaceCurrentApm,0,'g',7)
            .arg(r.residualRelative,0,'g',5).arg(r.normalizedMutualCouplingRms,0,'g',5).arg(r.mutualReciprocityRelative,0,'g',5).arg(r.maxJunctionCurrentMismatchA,0,'g',5)
            .arg(r.wireBranchConstraintCount).arg(r.maxWireBranchKclResidualA,0,'g',5);
        if(r.mutualReciprocityPreSymmetryRelative>0.0)
            metrics += QStringLiteral("\nPre-symmetry rooftop/RWG mutual mismatch = %1").arg(r.mutualReciprocityPreSymmetryRelative,0,'g',5);
        if(r.differentialSurfacePortUsed)
        {
            metrics += QStringLiteral("\nDifferential surface-port current imbalance = %1 A | Gaussian σ = %2…%3 mm | symmetric ±V/2 duality = %4")
                .arg(r.maxDifferentialPortCurrentImbalanceA,0,'g',5)
                .arg(r.minDifferentialPortFootprintRadiusM*1e3,0,'g',5)
                .arg(r.maxDifferentialPortFootprintRadiusM*1e3,0,'g',5)
                .arg(r.differentialPortSymmetricVoltageSplitUsed?QStringLiteral("yes"):QStringLiteral("no"));
        }
        if(r.layeredSlabQuasiStaticUsed)
            metrics += QStringLiteral("\nLayered dielectric correction: %1 slab region(s), up to %2 image term(s); quasi-static scalar correction on top of the dynamic effective-medium kernel")
                .arg(r.layeredSlabRegionCount).arg(r.layeredImageSeriesTerms);
        if(r.layeredSlabSommerfeldScalarUsed)
        {
            metrics += QStringLiteral("\nSommerfeld scalar spectrum: %1 slab region(s) | radial table = %2 samples | spectral quadrature = %3-point | limiting tanδ = %4 | scalar reactive projection = %5")
                .arg(r.layeredSlabRegionCount).arg(r.layeredSommerfeldRadialSamples).arg(r.layeredSommerfeldQuadratureOrder)
                .arg(r.layeredSommerfeldLimitingLossTangent,0,'g',4)
                .arg(r.layeredSommerfeldReactiveProjectionUsed?QStringLiteral("yes"):QStringLiteral("no"));
            const auto cacheLookups=r.layeredSommerfeldCacheHits+r.layeredSommerfeldCacheMisses;
            const double cacheHitRate=cacheLookups>0?100.0*double(r.layeredSommerfeldCacheHits)/double(cacheLookups):0.0;
            metrics += QStringLiteral("\n5.42-C Sommerfeld cache: %1 hit / %2 miss (%3 %) | table builds = %4 in %5 ms | LRU evictions = %6 | resident face/exterior/internal = %7/%8/%9")
                .arg(qulonglong(r.layeredSommerfeldCacheHits)).arg(qulonglong(r.layeredSommerfeldCacheMisses)).arg(cacheHitRate,0,'f',1)
                .arg(qulonglong(r.layeredSommerfeldCacheTableBuilds)).arg(r.layeredSommerfeldCacheBuildTimeMs,0,'f',2)
                .arg(qulonglong(r.layeredSommerfeldCacheEvictions)).arg(r.layeredSommerfeldFaceCacheEntries)
                .arg(r.layeredSommerfeldExteriorCacheEntries).arg(r.layeredSommerfeldInternalCacheEntries);
            if(r.layeredSlabSommerfeldTeVectorUsed)
            {
                metrics += QStringLiteral("\n5.29/5.30 HED transition: same-face TE vector potential = %1 | TE/TM HED scalar diagnostic = %2 | full-complex scalar injection = %3")
                    .arg(r.layeredSommerfeldVectorPotentialUsed?QStringLiteral("active"):QStringLiteral("off"))
                    .arg(r.layeredSommerfeldHedScalarDiagnosticEvaluated?QStringLiteral("evaluated"):QStringLiteral("off"))
                    .arg(r.layeredSommerfeldFullComplexScalarUsed?QStringLiteral("active"):QStringLiteral("guarded/off"));
                if(r.layeredSlabSommerfeldCrossFaceUsed)
                    metrics += QStringLiteral("\n5.30 transmitted dyadic: cross-face TE/TM vector residual = %1 | cross-face all-pair assembly = %2 | RWG reciprocity pre/post = %3 / %4")
                        .arg(r.layeredSommerfeldCrossFaceVectorUsed?QStringLiteral("reactive-active"):QStringLiteral("off"))
                        .arg(r.layeredSommerfeldCrossFaceAllPairAssemblyUsed?QStringLiteral("active"):QStringLiteral("off"))
                        .arg(r.surfaceReciprocityPreSymmetryRelative,0,'g',5)
                        .arg(r.surfaceReciprocityRelative,0,'g',5);
                if(r.layeredSlabSommerfeldLongitudinalHedUsed)
                    metrics += QStringLiteral("\n5.31 HED longitudinal scalar: coupled TE/TM same-face = %1 | coupled TE/TM cross-face = %2 | full-complex scalar = %3")
                        .arg(r.layeredSommerfeldLongitudinalHedScalarUsed?QStringLiteral("reactive-active"):QStringLiteral("off"))
                        .arg(r.layeredSommerfeldCrossFaceHedScalarUsed?QStringLiteral("reactive-active"):QStringLiteral("off"))
                        .arg(r.layeredSommerfeldFullComplexScalarUsed?QStringLiteral("active"):QStringLiteral("guarded/off"));
                if(r.layeredSlabSommerfeldExteriorHeightUsed)
                    metrics += QStringLiteral("\n5.32 exterior-height transition: wire↔RWG vector residual = %1 | exterior HED scalar = %2")
                        .arg(r.layeredSommerfeldExteriorWireSurfaceVectorUsed?QStringLiteral("reactive-active"):QStringLiteral("no exterior wire/surface pair"))
                        .arg(r.layeredSommerfeldExteriorHedScalarDiagnosticEvaluated?QStringLiteral("evaluated"):QStringLiteral("not exercised"));
                if(r.layeredSlabSommerfeldInternalLayerUsed)
                    metrics += QStringLiteral("\n5.33 internal-layer transition: internal tangential wire↔RWG vector residual = %1 | tangential HED scalar-gradient = %2 | legacy normal-current guard = %3")
                        .arg(r.layeredSommerfeldInternalWireSurfaceVectorUsed?QStringLiteral("reactive-active"):QStringLiteral("not exercised"))
                        .arg(r.layeredSommerfeldWireSurfaceScalarGradientUsed?QStringLiteral("reactive-active"):QStringLiteral("not exercised"))
                        .arg(r.layeredSommerfeldInternalNormalCurrentGuarded?QStringLiteral("guarded/off"):QStringLiteral("not required"));
                if(r.layeredSlabSommerfeldVedNormalUsed)
                    metrics += QStringLiteral("\n5.34 VED/via transition: normal TM vector residual = %1 | VED scalar-gradient = %2 | reciprocal HED full gradient = %3 | off-diagonal VED = %4")
                        .arg(r.layeredSommerfeldVedNormalVectorUsed?QStringLiteral("reactive-active"):QStringLiteral("not exercised"))
                        .arg(r.layeredSommerfeldVedScalarGradientUsed?QStringLiteral("reactive-active"):QStringLiteral("not exercised"))
                        .arg(r.layeredSommerfeldHedFullGradientUsed?QStringLiteral("reactive-active"):QStringLiteral("not exercised"))
                        .arg(r.layeredSommerfeldVedOffDiagonalVectorUsed?QStringLiteral("5.35 reactive-active"):(r.layeredSommerfeldVedOffDiagonalGuarded?QStringLiteral("5.34 guarded/off"):QStringLiteral("not required")));
                if(r.layeredSommerfeldVedOffDiagonalVectorUsed)
                    metrics += QStringLiteral("\n5.35 mixed TM vector dyadic: G_rho,z + G_z,rho = reactive-active | signed cavity derivatives + J1 transform | mutual reciprocity guard = %1")
                        .arg(r.layeredSommerfeldVedOffDiagonalReciprocityGuardUsed?QStringLiteral("active"):QStringLiteral("not exercised"));
                if(r.layeredSommerfeldComplexTransitionAttempted)
                {
                    const auto zc=r.layeredSommerfeldComplexCandidateInputImpedanceOhm;
                    metrics += QStringLiteral("\n5.36 complex wire↔RWG transition: candidate Zin = %1 %2 j%3 Ω | candidate Paccepted = %4 W | result = %5")
                        .arg(zc.real(),0,'g',6).arg(zc.imag()>=0?QStringLiteral("+"):QStringLiteral("−")).arg(std::abs(zc.imag()),0,'g',6)
                        .arg(r.layeredSommerfeldComplexCandidateAcceptedPowerW,0,'g',6)
                        .arg(r.layeredSommerfeldComplexTransitionUsed?QStringLiteral("full-complex active"):QStringLiteral("rejected → 5.35 fallback"));
                }
                if(r.layeredSommerfeldPowerAuditUsed)
                    metrics += QStringLiteral("\n5.36 power audit: Paccepted − (Prad,free-space + Pcond) = %1 W (%2 rel.) | layered far-field = %3")
                        .arg(r.powerClosureResidualW,0,'g',6).arg(r.powerClosureRelative,0,'g',6)
                        .arg(r.layeredSommerfeldPowerAuditFarFieldIncomplete?QStringLiteral("incomplete/advisory"):QStringLiteral("available"));
                if(r.layeredSommerfeldPropagatingFarFieldUsed)
                    metrics += QStringLiteral("\n5.37 propagating layered far field: Pupper = %1 W | Plower = %2 W | Pprop = %3 W | Dprop,max = %4 dBi | source side = %5 | Paccepted − (Pprop + Pcond) = %6 W (%7 rel.) | power guard = %8 | unresolved = surface-wave poles + dielectric absorption")
                        .arg(r.layeredRadiatedPowerUpperW,0,'g',6).arg(r.layeredRadiatedPowerLowerW,0,'g',6).arg(r.layeredRadiatedPowerTotalW,0,'g',6)
                        .arg(r.layeredDirectivityDbi,0,'g',6).arg(r.layeredSommerfeldFarFieldSourceSide>0?QStringLiteral("+normal"):QStringLiteral("−normal"))
                        .arg(r.layeredPowerClosureResidualW,0,'g',6).arg(r.layeredPowerClosureRelative,0,'g',6)
                        .arg(r.layeredSommerfeldPropagatingFarFieldGuardRejected?QStringLiteral("rejected → 5.35 fallback"):(r.layeredSommerfeldPropagatingFarFieldPowerConsistent?QStringLiteral("pass"):QStringLiteral("diagnostic only")));
                if(r.layeredSommerfeldSurfaceWavePoleAuditUsed)
                {
                    metrics += QStringLiteral("\n5.38 guided-pole audit: %1 TE/TM pole candidate(s) | Psurface-wave = %2 W | power = %3")
                        .arg(r.layeredSommerfeldSurfaceWavePoleCount).arg(r.layeredSurfaceWavePowerW,0,'g',6)
                        .arg(r.layeredSommerfeldSurfaceWavePowerResolved?QStringLiteral("resolved"):QStringLiteral("guarded — PEC-ground modal normalization pending"));
                    for(const auto &pole:r.layeredSommerfeldSurfaceWavePoles)
                        metrics += QStringLiteral("\n  %1: β/k0 = %2 | α = %3 Np/m | |D(β)| = %4 | |Res R| = %5 1/m")
                            .arg(QString::fromStdString(pole.polarization)).arg(pole.betaOverK0,0,'g',7).arg(pole.attenuationNpPerM,0,'g',6)
                            .arg(pole.denominatorMagnitude,0,'g',5).arg(pole.residueMagnitudePerM,0,'g',6);
                }
                if(r.layeredSommerfeldGroundedPecModeAuditUsed)
                {
                    metrics += QStringLiteral("\n5.39 grounded-PEC modal audit: ground coverage = %1 % | boundary = %2 | modes = %3 | Psurface-wave = guarded")
                        .arg(100.0*r.layeredSommerfeldGroundCoverageFraction,0,'f',1)
                        .arg(r.layeredSommerfeldGroundedPecBoundaryDetected?QStringLiteral("detected"):QStringLiteral("insufficient RWG backing"))
                        .arg(r.layeredSommerfeldGroundedPecModeCount);
                    for(const auto &mode:r.layeredSommerfeldGroundedPecModes)
                        metrics += QStringLiteral("\n  %1 grounded: β/k0 = %2 | α = %3 Np/m | Pnorm = %4 W/m | dielectric power fraction = %5 % | |D| = %6")
                            .arg(QString::fromStdString(mode.polarization)).arg(mode.betaOverK0,0,'g',7).arg(mode.attenuationNpPerM,0,'g',6)
                            .arg(mode.modalPowerNormalizationWPerM,0,'g',6).arg(100.0*mode.dielectricPowerFraction,0,'g',5).arg(mode.denominatorMagnitude,0,'g',5);
                }
            }
        }
        if(r.pecTerminalChargeRegularizationUsed)
            metrics += QStringLiteral("\nPEC terminal charge regularization: %1 component(s), max transition scale = %2 mm")
                .arg(r.pecTerminalRegularizedComponentCount).arg(r.maxPecTerminalTransitionScaleM*1e3,0,'g',5);
        if(r.wireLinearRooftopBasisUsed)
            metrics += QStringLiteral("\nRooftop diagnostics: max free-end |I| = %1 A | max |λ| = %2 C/m | |ΣQwire| = %3 C")
                .arg(r.maxWireOpenEndCurrentA,0,'g',5).arg(r.maxWireLineChargeCpm,0,'g',5).arg(r.netWireChargeMagnitudeC,0,'g',5);
        const double etaRadForGain=std::clamp(r.radiationEfficiency,0.0,1.0);
        const double hybridGainDbi=etaRadForGain>1e-15?r.directivityDbi+10.0*std::log10(etaRadForGain):-300.0;
        double hybridRealizedGainDbi=std::numeric_limits<double>::quiet_NaN();
        if(r.feeds.size()==1)
        {
            const double gm=std::abs(r.feeds.front().reflectionCoefficient);
            if(gm<1.0)hybridRealizedGainDbi=hybridGainDbi+10.0*std::log10(std::max(1e-15,1.0-gm*gm));
        }
        const QString hybridRealizedText=std::isfinite(hybridRealizedGainDbi)
            ?QStringLiteral("%1 dBi").arg(hybridRealizedGainDbi,0,'g',6):QStringLiteral("n/a");
        metrics += QStringLiteral("\nDmax ≈ %1 dBi | Gmax ≈ %10 dBi | Greal,max ≈ %11 | Prad = %2 W | Paccepted = %3 W | Prad/Paccepted = %4\nConductor loss ≈ %5 W | ηrad,cond ≈ %6 %% | Zs ≈ %7 %8 j%9 Ω/sq")
            .arg(r.directivityDbi,0,'g',6).arg(r.radiatedPowerW,0,'g',6).arg(r.acceptedPowerW,0,'g',6).arg(r.powerBalanceRatio,0,'g',6)
            .arg(r.conductorLossW,0,'g',6).arg(100.0*r.radiationEfficiency,0,'g',5)
            .arg(r.surfaceImpedanceOhmPerSquare.real(),0,'g',6).arg(r.surfaceImpedanceOhmPerSquare.imag()>=0?QStringLiteral("+"):QStringLiteral("−")).arg(std::abs(r.surfaceImpedanceOhmPerSquare.imag()),0,'g',6)
            .arg(hybridGainDbi,0,'g',6).arg(hybridRealizedText);
        m_hybridMomMetrics->setText(metrics);
        m_hybridMomMetrics->setToolTip(QString::fromStdString(r.note));
    }
    if(!r.feeds.empty())
    {
        const auto&f=r.feeds.front();
        emit antennaImpedanceAvailable(in.wire.frequencyHz,f.inputImpedanceOhm.real(),f.inputImpedanceOhm.imag(),f.referenceOhm,
                                       QStringLiteral("Hybrid %1").arg(QString::fromStdString(f.name)));
    }

    using HybridWireSeriesKey=std::pair<int,int>;
    std::map<HybridWireSeriesKey,QVector<double>>xByComponent,magByComponent,phaseByComponent;
    std::map<HybridWireSeriesKey,double>previousPhase;
    for(const auto&seg:r.wireSegments)
    {
        const HybridWireSeriesKey key{seg.componentIndex,r.wireBranchConstraintCount>0?seg.sourceWireIndex:-1};
        xByComponent[key].push_back(seg.pathCenterM);magByComponent[key].push_back(std::abs(seg.currentA));
        double ph=std::arg(seg.currentA)*180.0/NumericalEM::Pi;auto it=previousPhase.find(key);if(it!=previousPhase.end()){while(ph-it->second>180.0)ph-=360.0;while(ph-it->second<-180.0)ph+=360.0;}previousPhase[key]=ph;phaseByComponent[key].push_back(ph);
    }
    QVector<FieldProfileSeries>magSeries,phaseSeries;
    for(const auto&kv:xByComponent){const auto key=kv.first;const QString stem=key.second>=0?QStringLiteral("C%1 / W%2").arg(key.first+1).arg(key.second+1):QStringLiteral("C%1").arg(key.first+1);magSeries.push_back(FieldProfileSeries{kv.second,magByComponent[key],stem+QStringLiteral(" |I| hybrid"),QStringLiteral("A"),false});phaseSeries.push_back(FieldProfileSeries{kv.second,phaseByComponent[key],stem+QStringLiteral(" phase hybrid"),QStringLiteral("deg"),false});}
    if(m_currentMagnitudePlot)m_currentMagnitudePlot->setSeries(magSeries,r.wireBranchConstraintCount>0?QStringLiteral("Hybrid wire-current distribution — branch/source wires separated, PEC surface back-reaction included"):QStringLiteral("Hybrid wire-current distribution — PEC surface back-reaction included"));
    if(m_currentPhasePlot)m_currentPhasePlot->setSeries(phaseSeries,r.wireBranchConstraintCount>0?QStringLiteral("Hybrid wire-current phase — branch/source wires unwrapped independently"):QStringLiteral("Hybrid wire-current phase"));

    if(m_chargeMagnitudePlot && r.wireLinearRooftopBasisUsed)
    {
        std::map<int,std::vector<std::pair<double,double>>> chargeSamples;
        for(const auto &seg:r.wireSegments)chargeSamples[seg.sourceWireIndex].push_back({seg.sourceWirePathCenterM,std::abs(seg.lineChargeDensityCpm)});
        QVector<FieldProfileSeries> chargeSeries;
        for(auto &[wireIndex,samples]:chargeSamples)
        {
            std::sort(samples.begin(),samples.end(),[](const auto&a,const auto&b){return a.first<b.first;});
            QVector<double>x,y;for(const auto &[position,value]:samples){x.push_back(position);y.push_back(value);}
            chargeSeries.push_back(FieldProfileSeries{x,y,QStringLiteral("W%1 |λ| hybrid").arg(wireIndex+1),QStringLiteral("C/m"),false});
        }
        m_chargeMagnitudePlot->setSeries(chargeSeries,QStringLiteral("Hybrid rooftop line charge: λ = −(1/jω)dI/ds. Free wire ends enforce I=0; PEC terminals use half-rooftop leakage modes balanced by RWG current."));
    }

    QVector<double>triIndex,jMag;std::vector<NumericalEM::Vec3>points;std::vector<double>magnitudes;
    for(int i=0;i<static_cast<int>(r.triangleCurrents.size());++i){const auto&tc=r.triangleCurrents[static_cast<std::size_t>(i)];triIndex.push_back(i+1);jMag.push_back(tc.magnitudeApm);points.push_back(tc.centroid);magnitudes.push_back(tc.magnitudeApm);}
    if(m_surfaceCurrentPlot)m_surfaceCurrentPlot->setSeries({FieldProfileSeries{triIndex,jMag,QStringLiteral("|J| hybrid"),QStringLiteral("A/m"),false}},QStringLiteral("PEC surface current induced by the driven wire antenna"));
    if(m_geometry3D)m_geometry3D->setSurfaceCurrentSamples(points,magnitudes);

    const bool useLayeredPattern=r.layeredSommerfeldPropagatingFarFieldUsed && r.layeredSommerfeldPropagatingFarFieldPowerConsistent &&
        r.layeredAzimuthNormalizedFarField.size()==r.azimuthDeg.size() && r.layeredElevationNormalizedFarField.size()==r.elevationDeg.size();
    const auto &azNorm=useLayeredPattern?r.layeredAzimuthNormalizedFarField:r.azimuthNormalizedFarField;
    const auto &elNorm=useLayeredPattern?r.layeredElevationNormalizedFarField:r.elevationNormalizedFarField;
    QVector<double>az,azDb,el,elDb;
    for(std::size_t i=0;i<r.azimuthDeg.size()&&i<azNorm.size();++i){az.push_back(r.azimuthDeg[i]);const double a=azNorm[i];azDb.push_back(a>1e-12?std::max(-40.0,20.0*std::log10(a)):-40.0);}
    for(std::size_t i=0;i<r.elevationDeg.size()&&i<elNorm.size();++i){el.push_back(r.elevationDeg[i]);const double a=elNorm[i];elDb.push_back(a>1e-12?std::max(-40.0,20.0*std::log10(a)):-40.0);}
    const QString farFieldLabel=useLayeredPattern?QStringLiteral("Layered propagating far field (5.37/5.38)"):QStringLiteral("Hybrid global normalized far field");
    if(m_azimuthPlot)m_azimuthPlot->setSeries({FieldProfileSeries{az,azDb,farFieldLabel,QStringLiteral("dB"),false}},useLayeredPattern?QStringLiteral("Layered propagating far-field azimuth cut — TE/TM slab preview"):QStringLiteral("Hybrid far-field azimuth cut — wire + PEC currents"));
    if(m_elevationPlot)m_elevationPlot->setSeries({FieldProfileSeries{el,elDb,farFieldLabel,QStringLiteral("dB"),false}},useLayeredPattern?QStringLiteral("Layered propagating far-field elevation cut — TE/TM slab preview"):QStringLiteral("Hybrid far-field elevation cut — wire + PEC currents"));
    if(m_surfaceRcsPlot)m_surfaceRcsPlot->setSeries({FieldProfileSeries{az,azDb,QStringLiteral("Hybrid azimuth"),QStringLiteral("dB"),false},FieldProfileSeries{el,elDb,QStringLiteral("Hybrid elevation"),QStringLiteral("dB"),true}},QStringLiteral("Hybrid normalized radiation — coherent wire + surface contribution"));
    if(m_azimuthPolarPlot)m_azimuthPolarPlot->setPattern(r.azimuthDeg,azNorm,useLayeredPattern?QStringLiteral("Layered propagating azimuth (5.37/5.38) — global normalization"):QStringLiteral("Hybrid azimuth pattern — global normalization"),false);
    if(m_elevationPolarPlot)m_elevationPolarPlot->setPattern(r.elevationDeg,elNorm,useLayeredPattern?QStringLiteral("Layered propagating elevation (5.37/5.38) — θ convention"):QStringLiteral("Hybrid elevation pattern — θ convention, global normalization"),true);
    if(m_radiation3D)
    {
        const double etaRad = std::clamp(r.radiationEfficiency, 0.0, 1.0);
        const double gainDbi = etaRad > 1e-15 ? r.directivityDbi + 10.0*std::log10(etaRad) : -300.0;
        double realizedGainDbi = std::numeric_limits<double>::quiet_NaN();
        if (r.feeds.size() == 1)
        {
            const double gm = std::abs(r.feeds.front().reflectionCoefficient);
            if (gm < 1.0)
                realizedGainDbi = gainDbi + 10.0*std::log10(std::max(1e-15,1.0-gm*gm));
        }
        const auto &spherePattern=(useLayeredPattern && r.layeredFarFieldNormalized.size()==r.farFieldNormalized.size())?r.layeredFarFieldNormalized:r.farFieldNormalized;
        const double displayDbi=useLayeredPattern?r.layeredDirectivityDbi:r.directivityDbi;
        const double displayTheta=useLayeredPattern?r.layeredMaxRadiationThetaDeg:r.maxRadiationThetaDeg;
        const double displayPhi=useLayeredPattern?r.layeredMaxRadiationPhiDeg:r.maxRadiationPhiDeg;
        const double propEta=(useLayeredPattern&&r.acceptedPowerW>1e-18)?std::clamp(r.layeredRadiatedPowerTotalW/r.acceptedPowerW,0.0,1.0):etaRad;
        const double displayGainDbi=propEta>1e-15?displayDbi+10.0*std::log10(propEta):-300.0;
        double displayRealizedGainDbi=std::numeric_limits<double>::quiet_NaN();
        if(r.feeds.size()==1){const double gm=std::abs(r.feeds.front().reflectionCoefficient);if(gm<1.0)displayRealizedGainDbi=displayGainDbi+10.0*std::log10(std::max(1e-15,1.0-gm*gm));}
        m_radiation3D->setPattern(r.farFieldThetaDeg,r.farFieldPhiDeg,spherePattern,
                                  displayDbi,displayGainDbi,displayRealizedGainDbi,propEta,
                                  displayTheta,displayPhi);
    }
    if(m_resultTabs&&m_currentMagnitudePlot)m_resultTabs->setCurrentWidget(m_currentMagnitudePlot);
}


void AntennaDesignerWidget::solveFrequencySweep()
{
    if (m_feeds.empty())
    {
        if (m_sweepStatus) m_sweepStatus->setText(QStringLiteral("ERROR: add at least one feed marker."));
        return;
    }
    const int observedFeed = m_sweepFeed ? m_sweepFeed->currentIndex() : 0;
    if (observedFeed < 0 || observedFeed >= static_cast<int>(m_feeds.size()))
    {
        if (m_sweepStatus) m_sweepStatus->setText(QStringLiteral("ERROR: select a valid feed to observe."));
        return;
    }

    const int excitationMode = m_sweepExcitationMode ? m_sweepExcitationMode->currentIndex() : 0; // 0 all configured, 1 observed only
    const bool observedFeedOnly = excitationMode == 1;
    if (observedFeedOnly && std::abs(m_feeds[static_cast<std::size_t>(observedFeed)].voltageV) <= 1e-18)
    {
        if (m_sweepStatus) m_sweepStatus->setText(QStringLiteral("ERROR: observed-feed-only sweep requires a non-zero configured voltage on the reported feed."));
        return;
    }

    const int solverChoice = m_sweepSolver ? m_sweepSolver->currentIndex() : 0; // 0 auto, 1 hybrid, 2 thin-wire
    const bool autoHybrid = !m_planes.empty();
    const bool useHybrid = solverChoice == 1 || (solverChoice == 0 && autoHybrid);
    if (!useHybrid && m_wires.empty())
    {
        if (m_sweepStatus) m_sweepStatus->setText(QStringLiteral("ERROR: thin-wire sweep requires at least one wire. Use Hybrid for a differential PEC surface port."));
        return;
    }

    HybridWireSurfaceMom::Input hybridTemplate;
    QString hybridInputError;
    if (useHybrid && !buildHybridInput(m_frequencyMHz->value()*1e6, false, hybridTemplate, hybridInputError))
    {
        if (m_sweepStatus)
            m_sweepStatus->setText(QStringLiteral("ERROR: Hybrid broadband sweep cannot start: %1").arg(hybridInputError));
        return;
    }
    const std::string observedFeedNameStd = m_feeds[static_cast<std::size_t>(observedFeed)].name.toStdString();
    if (useHybrid && observedFeedOnly && (!m_hybridPortMode || m_hybridPortMode->currentIndex()!=2))
    {
        bool driven=false;
        for(const auto &f:hybridTemplate.wire.feeds) if(f.name==observedFeedNameStd) driven=true;
        for(const auto &f:hybridTemplate.surfaceReferencedFeeds) if(f.name==observedFeedNameStd) driven=true;
        if(!driven)
        {
            if (m_sweepStatus) m_sweepStatus->setText(QStringLiteral("ERROR: the reported marker is not a driven Hybrid source in the current port configuration. Select the actual driven feed or use all-configured-feeds mode."));
            return;
        }
    }

    const double startMHz = std::min(m_sweepStartMHz->value(), m_sweepStopMHz->value());
    const double stopMHz = std::max(m_sweepStartMHz->value(), m_sweepStopMHz->value());
    const int requestedPoints = m_sweepPoints->value();
    if (!(stopMHz > startMHz) || requestedPoints < 3)
    {
        if (m_sweepStatus) m_sweepStatus->setText(QStringLiteral("ERROR: sweep stop must be greater than start and at least 3 points are required."));
        return;
    }

    const QString solverLabel = useHybrid ? QStringLiteral("Hybrid broadband") : QStringLiteral("Thin-wire legacy");
    int configuredActiveFeeds=0;
    QStringList configuredExcitations;
    for(const auto &f:m_feeds)
    {
        if(std::abs(f.voltageV)>1e-12)++configuredActiveFeeds;
        configuredExcitations << QStringLiteral("%1 %2 V∠%3°").arg(f.name).arg(f.voltageV,0,'g',5).arg(f.phaseDeg,0,'g',5);
    }
    const bool differentialHybridPort = useHybrid && m_hybridPortMode && m_hybridPortMode->currentIndex()==2;
    const bool multiFeedTopology = m_feeds.size()>1 && !differentialHybridPort;
    const bool simultaneousMultiSource = !observedFeedOnly && configuredActiveFeeds>1 && multiFeedTopology;
    const QString excitationDescription = observedFeedOnly
        ? QStringLiteral("reported feed only (%1); all other impressed feed voltages forced to 0 V").arg(m_feeds[static_cast<std::size_t>(observedFeed)].name)
        : (differentialHybridPort ? QStringLiteral("configured differential Hybrid port")
                                  : QStringLiteral("all configured feed phasors active: %1").arg(configuredExcitations.join(QStringLiteral(", "))));
    QProgressDialog progress(QStringLiteral("Solving %1 antenna sweep…").arg(solverLabel), QStringLiteral("Cancel"), 0, requestedPoints, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    QVector<double> fMHz, rOhm, xOhm, s11Db, vswrPlot, vswrRaw;
    std::vector<double> fStd;
    std::vector<std::complex<double>> gamma;
    std::vector<std::complex<double>> impedance;
    QStringList errors;
    int maxUnknownsSeen = 0;
    int complexFallbackPoints = 0;
    quint64 sommerfeldCacheHits = 0;
    quint64 sommerfeldCacheMisses = 0;
    quint64 sommerfeldCacheBuilds = 0;
    quint64 sommerfeldCacheEvictions = 0;
    double sommerfeldCacheBuildMs = 0.0;
    bool canceled = false;
    QString observedResultFeedName = m_feeds[static_cast<std::size_t>(observedFeed)].name;
    double observedReferenceOhm = m_feeds[static_cast<std::size_t>(observedFeed)].sourceOhm;
    if (m_sweepStatus) m_sweepStatus->setText(QStringLiteral("%1 sweep running…").arg(solverLabel));
    if (m_sweepSummary) m_sweepSummary->clear();
    QElapsedTimer timer;
    timer.start();

    for (int i = 0; i < requestedPoints; ++i)
    {
        if (progress.wasCanceled()) { canceled = true; break; }
        const double alpha = requestedPoints > 1 ? double(i) / double(requestedPoints - 1) : 0.0;
        const double fm = startMHz + alpha * (stopMHz - startMHz);
        progress.setLabelText(QStringLiteral("%1 sweep %2 / %3 — %4 MHz").arg(useHybrid?QStringLiteral("Hybrid"):QStringLiteral("MoM")).arg(i + 1).arg(requestedPoints).arg(fm, 0, 'g', 7));
        QApplication::processEvents();

        std::complex<double> z{0.0,0.0}, g{0.0,0.0};
        double swr = std::numeric_limits<double>::infinity();
        int unknownCount = 0;
        QString pointFeedName;
        double pointReferenceOhm = observedReferenceOhm;

        if (useHybrid)
        {
            auto in = hybridTemplate;
            in.wire.frequencyHz = fm * 1e6;
            in.computeFarField = false;
            in.wire.computeFarField = false;
            if (observedFeedOnly && (!m_hybridPortMode || m_hybridPortMode->currentIndex()!=2))
            {
                for (auto &f : in.wire.feeds) if (f.name != observedFeedNameStd) f.voltageV = {0.0,0.0};
                for (auto &f : in.surfaceReferencedFeeds) if (f.name != observedFeedNameStd) f.voltageV = {0.0,0.0};
            }
            const auto result = HybridWireSurfaceMom::solve(in);
            if (!result.valid)
            {
                errors << QStringLiteral("%1 MHz: %2").arg(fm,0,'g',7)
                              .arg(result.error.empty()?QStringLiteral("invalid hybrid solution"):QString::fromStdString(result.error));
                progress.setValue(i + 1);
                continue;
            }
            const int feedIndex = hybridResultFeedIndex(result, observedFeed);
            if (feedIndex < 0 || feedIndex >= static_cast<int>(result.feeds.size()))
            {
                errors << QStringLiteral("%1 MHz: observed Hybrid feed was not returned by the solver").arg(fm,0,'g',7);
                progress.setValue(i + 1);
                continue;
            }
            const auto &feedResult = result.feeds[static_cast<std::size_t>(feedIndex)];
            z = feedResult.inputImpedanceOhm;
            g = feedResult.reflectionCoefficient;
            swr = feedResult.vswr;
            unknownCount = result.totalUnknownCount;
            pointFeedName = QString::fromStdString(feedResult.name);
            pointReferenceOhm = feedResult.referenceOhm;
            if (result.layeredSommerfeldComplexTransitionFallbackUsed) ++complexFallbackPoints;
            sommerfeldCacheHits += result.layeredSommerfeldCacheHits;
            sommerfeldCacheMisses += result.layeredSommerfeldCacheMisses;
            sommerfeldCacheBuilds += result.layeredSommerfeldCacheTableBuilds;
            sommerfeldCacheEvictions += result.layeredSommerfeldCacheEvictions;
            sommerfeldCacheBuildMs += result.layeredSommerfeldCacheBuildTimeMs;
        }
        else
        {
            NumericalEM::WireNetworkMomInput in;
            in.frequencyHz = fm * 1e6;
            in.segmentsPerWavelength = m_segmentsPerWavelength->value();
            in.maxUnknowns = m_maxMomUnknowns->value();
            in.nodeMergeToleranceM = 1e-6;
            in.maxSegmentLengthRadiusFactor = m_radiusMeshFactor ? m_radiusMeshFactor->value() : 3.5;
            in.junctionLocalSubdivisions = m_junctionLocalSubdivisions ? m_junctionLocalSubdivisions->value() : 3;
            in.junctionTreatment = (m_junctionTreatment && m_junctionTreatment->currentIndex()==1)
                ? NumericalEM::WireJunctionTreatment::LagrangeKcl : NumericalEM::WireJunctionTreatment::ReducedBasis;
            in.currentBasisTreatment = (m_currentBasisTreatment && m_currentBasisTreatment->currentIndex()==1)
                ? NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge : NumericalEM::WireCurrentBasisTreatment::PulsePointMatching;
            in.computeFarField = false;
            in.wires.reserve(m_wires.size());
            for (const auto &w : m_wires)
                in.wires.push_back(NumericalEM::WireSpan3D{w.name.toStdString(),
                                                           {w.aM.x(), w.aM.y(), w.azM},
                                                           {w.bM.x(), w.bM.y(), w.bzM},
                                                           w.radiusM});
            in.feeds.reserve(m_feeds.size());
            for (int fi=0; fi<static_cast<int>(m_feeds.size()); ++fi)
            {
                const auto &f=m_feeds[static_cast<std::size_t>(fi)];
                const double phase = f.phaseDeg * NumericalEM::Pi / 180.0;
                const double amplitude = (observedFeedOnly && fi != observedFeed) ? 0.0 : f.voltageV;
                in.feeds.push_back(NumericalEM::WireFeed3D{f.name.toStdString(),
                                                           {f.positionM.x(), f.positionM.y(), f.zM},
                                                           std::polar(amplitude, phase), f.sourceOhm});
            }
            const auto result = NumericalEM::solveWireNetworkMom(in);
            if (!result.valid || observedFeed >= static_cast<int>(result.feeds.size()))
            {
                errors << QStringLiteral("%1 MHz: %2").arg(fm, 0, 'g', 7)
                              .arg(result.error.empty() ? QStringLiteral("invalid thin-wire solution") : QString::fromStdString(result.error));
                progress.setValue(i + 1);
                continue;
            }
            const auto &feedResult = result.feeds[static_cast<std::size_t>(observedFeed)];
            z = feedResult.activeImpedanceOhm;
            g = feedResult.reflectionCoefficient;
            swr = feedResult.vswr;
            unknownCount = result.unknownCount;
            pointFeedName = QString::fromStdString(feedResult.name);
            pointReferenceOhm = m_feeds[static_cast<std::size_t>(observedFeed)].sourceOhm;
        }

        if (!std::isfinite(z.real()) || !std::isfinite(z.imag()) || !std::isfinite(std::abs(g)))
        {
            errors << QStringLiteral("%1 MHz: non-finite impedance/reflection coefficient").arg(fm,0,'g',7);
            progress.setValue(i + 1);
            continue;
        }
        if (fStd.empty())
        {
            observedResultFeedName = pointFeedName.isEmpty()?observedResultFeedName:pointFeedName;
            observedReferenceOhm = pointReferenceOhm;
        }

        const double gm = std::abs(g);
        const double sdb = 20.0 * std::log10(std::max(1e-15, gm));
        fMHz.push_back(fm);
        rOhm.push_back(z.real());
        xOhm.push_back(z.imag());
        s11Db.push_back(sdb);
        vswrPlot.push_back(std::isfinite(swr) ? std::min(swr, 20.0) : 20.0);
        vswrRaw.push_back(swr);
        fStd.push_back(fm);
        gamma.push_back(g);
        impedance.push_back(z);
        maxUnknownsSeen = std::max(maxUnknownsSeen, unknownCount);
        progress.setValue(i + 1);
    }

    const qint64 elapsedMs = timer.elapsed();
    progress.setValue(requestedPoints);
    if (fStd.size() < 3)
    {
        if (m_sweepStatus)
            m_sweepStatus->setText(canceled
                ? QStringLiteral("%1 sweep canceled after %2 valid point(s); at least 3 are needed to plot a partial sweep.").arg(solverLabel).arg(fStd.size())
                : QStringLiteral("%1 sweep failed: only %2 valid point(s). %3").arg(solverLabel).arg(fStd.size()).arg(errors.isEmpty() ? QString() : errors.first()));
        return;
    }

    int best = 0, resonance = 0;
    for (int i = 1; i < static_cast<int>(fStd.size()); ++i)
    {
        if (std::abs(gamma[static_cast<std::size_t>(i)]) < std::abs(gamma[static_cast<std::size_t>(best)])) best = i;
        if (std::abs(impedance[static_cast<std::size_t>(i)].imag()) < std::abs(impedance[static_cast<std::size_t>(resonance)].imag())) resonance = i;
    }

    int bandLo = -1, bandHi = -1;
    auto swrAt = [&](int i) {
        const double gm = std::abs(gamma[static_cast<std::size_t>(i)]);
        return gm < 1.0 ? (1.0 + gm) / std::max(1.0 - gm, 1e-15) : std::numeric_limits<double>::infinity();
    };
    if (swrAt(best) <= 2.0)
    {
        bandLo = best; bandHi = best;
        while (bandLo > 0 && swrAt(bandLo - 1) <= 2.0) --bandLo;
        while (bandHi + 1 < static_cast<int>(fStd.size()) && swrAt(bandHi + 1) <= 2.0) ++bandHi;
    }

    QVector<FieldPlotMarker> markers;
    markers.push_back(FieldPlotMarker{QStringLiteral("Best match"), fStd[static_cast<std::size_t>(best)], true, true, false});
    if (resonance != best)
        markers.push_back(FieldPlotMarker{QStringLiteral("min |X|"), fStd[static_cast<std::size_t>(resonance)], true, false, false});

    const QString plotFeedName = observedResultFeedName.isEmpty()?m_feeds[static_cast<std::size_t>(observedFeed)].name:observedResultFeedName;
    m_sweepZPlot->setSeries({FieldProfileSeries{fMHz, rOhm, QStringLiteral("Rin"), QStringLiteral("Ω"), false},
                             FieldProfileSeries{fMHz, xOhm, QStringLiteral("Xin"), QStringLiteral("Ω"), true}},
                            QStringLiteral("%1 input impedance versus frequency — %2").arg(solverLabel,plotFeedName));
    m_sweepZPlot->setMarkers(markers);
    const QString reflectionSeriesName = simultaneousMultiSource ? QStringLiteral("Active Γ")
        : (multiFeedTopology ? QStringLiteral("Driving Γ") : QStringLiteral("S11"));
    const QString reflectionTitle = simultaneousMultiSource
        ? QStringLiteral("%1 active reflection magnitude — %2 under simultaneous multi-feed drive — Zref = %3 Ω").arg(solverLabel,plotFeedName).arg(observedReferenceOhm,0,'g',6)
        : (multiFeedTopology
            ? QStringLiteral("%1 driving-point reflection — %2 with other impressed sources off — Zref = %3 Ω").arg(solverLabel,plotFeedName).arg(observedReferenceOhm,0,'g',6)
            : QStringLiteral("%1 S11 magnitude — %2 — Z0 = %3 Ω").arg(solverLabel,plotFeedName).arg(observedReferenceOhm,0,'g',6));
    m_sweepS11Plot->setSeries({FieldProfileSeries{fMHz, s11Db, reflectionSeriesName, QStringLiteral("dB"), false}}, reflectionTitle);
    m_sweepS11Plot->setMarkers(markers);
    FieldProfileSeries vswrSeries{fMHz, vswrPlot, QStringLiteral("VSWR (display clipped at 20)"), QString(), false};
    vswrSeries.hoverValues = vswrRaw;
    m_sweepVswrPlot->setSeries({vswrSeries},
                               QStringLiteral("%1 VSWR — display clipped at 20; hover reports un-clipped value").arg(solverLabel));
    m_sweepVswrPlot->setMarkers(markers);
    m_sweepSmith->setSweep(fStd, gamma, best, resonance);

    const auto bestZ = impedance[static_cast<std::size_t>(best)];
    const double bestGamma = std::abs(gamma[static_cast<std::size_t>(best)]);
    const double bestS11 = 20.0 * std::log10(std::max(1e-15, bestGamma));
    const double bestVswr = swrAt(best);
    const auto resZ = impedance[static_cast<std::size_t>(resonance)];
    QString bandText = QStringLiteral("No sampled VSWR ≤ 2 band");
    if (bandLo >= 0)
    {
        const double low = fStd[static_cast<std::size_t>(bandLo)], high = fStd[static_cast<std::size_t>(bandHi)];
        bandText = QStringLiteral("VSWR≤2 sampled band: %1…%2 MHz (BW ≈ %3 MHz)")
                       .arg(low, 0, 'g', 7).arg(high, 0, 'g', 7).arg(high - low, 0, 'g', 7);
    }

    QString statusExtra;
    if (!errors.isEmpty()) statusExtra += QStringLiteral(" %1 point(s) failed.").arg(errors.size());
    statusExtra += QStringLiteral(" Excitation: %1.").arg(excitationDescription);
    if (simultaneousMultiSource)
        statusExtra += QStringLiteral(" Multi-feed result is ACTIVE impedance/reflection for the reported feed under simultaneous drive; it is not an N-port S11 matrix entry.");
    else if (multiFeedTopology)
        statusExtra += QStringLiteral(" Other impressed feed voltages are off, but no matched terminations are inserted at those markers; the reported quantity is a driving-point Γ, not a formal terminated N-port S11 matrix entry.");
    const double sampledStepMHz=(stopMHz-startMHz)/std::max(1,requestedPoints-1);
    if (sampledStepMHz > 0.10*std::max(1e-12,m_frequencyMHz->value()))
        statusExtra += QStringLiteral(" WARNING: coarse sweep step Δf=%1 MHz (%2% of design f); refine around any candidate resonance/match.")
            .arg(sampledStepMHz,0,'g',6).arg(100.0*sampledStepMHz/std::max(1e-12,m_frequencyMHz->value()),0,'g',4);
    if (resonance==0 || resonance+1==static_cast<int>(fStd.size()))
        statusExtra += QStringLiteral(" WARNING: minimum |X| lies on the sweep boundary; resonance is not bracketed by this sweep.");
    if (useHybrid)
    {
        statusExtra += QStringLiteral(" Same Hybrid input builder/ports/mesh/material settings as the single-frequency solve; far field disabled for speed.");
        if (complexFallbackPoints > 0)
            statusExtra += QStringLiteral(" 5.36+ passive guard fell back to the reactive path at %1 sampled point(s).").arg(complexFallbackPoints);
        const quint64 cacheLookups=sommerfeldCacheHits+sommerfeldCacheMisses;
        if(cacheLookups>0)
        {
            const double hitRate=100.0*double(sommerfeldCacheHits)/double(cacheLookups);
            statusExtra += QStringLiteral(" Sommerfeld cache: %1% hit, %2 table build(s), %3 eviction(s), %4 ms build time.")
                .arg(hitRate,0,'f',1).arg(sommerfeldCacheBuilds).arg(sommerfeldCacheEvictions).arg(sommerfeldCacheBuildMs,0,'f',1);
        }
    }
    else if (!m_planes.empty() || !m_dielectrics.empty())
    {
        if (solverChoice == 2)
            statusExtra += QStringLiteral(" WARNING: explicit legacy thin-wire mode ignored %1 PEC surface(s) and %2 dielectric region(s).").arg(m_planes.size()).arg(m_dielectrics.size());
        else
            statusExtra += QStringLiteral(" WARNING: Auto used thin-wire because no PEC surface is available to instantiate the current Hybrid backend; %1 dielectric region(s) were not included.").arg(m_dielectrics.size());
    }

    if (m_sweepStatus)
        m_sweepStatus->setText(QStringLiteral("%1%2 — %3/%4 valid points in %5 s, max %6 algebraic unknowns.%7")
                                   .arg(canceled ? QStringLiteral("Partial ") : QString(), solverLabel)
                                   .arg(fStd.size()).arg(requestedPoints).arg(elapsedMs / 1000.0, 0, 'f', 2).arg(maxUnknownsSeen).arg(statusExtra));

    QString modelLine = useHybrid
        ? QStringLiteral("Solver: Hybrid wire + PEC/RWG + dielectric | kernel: %1 | far-field during sweep: OFF")
              .arg(m_hybridDielectricKernel ? m_hybridDielectricKernel->currentText() : QStringLiteral("current Hybrid kernel"))
        : QStringLiteral("Solver: generalized thin-wire MoM (legacy/fast) | PEC/dielectric: not assembled");
    modelLine += QStringLiteral("\nExcitation: %1 | reported feed: %2 | reflection quantity: %3")
        .arg(excitationDescription,plotFeedName,simultaneousMultiSource?QStringLiteral("active Γ under simultaneous drive")
             :(multiFeedTopology?QStringLiteral("driving-point Γ; other impressed sources off, no port terminations"):QStringLiteral("one-port Γ/S11 from reported Zin")));
    if (m_sweepSummary)
    {
        m_sweepSummary->setText(QStringLiteral("%1\nBest sampled match: %2 MHz | Zin = %3 %4 j%5 Ω | %6 = %7 dB | VSWR = %8\n"
                                               "Minimum sampled |X|: %9 MHz | Zin = %10 %11 j%12 Ω\n%13")
                                    .arg(modelLine)
                                    .arg(fStd[static_cast<std::size_t>(best)], 0, 'g', 8)
                                    .arg(bestZ.real(), 0, 'g', 7).arg(bestZ.imag() >= 0.0 ? QStringLiteral("+") : QStringLiteral("−"))
                                    .arg(std::abs(bestZ.imag()), 0, 'g', 7).arg(reflectionSeriesName).arg(bestS11, 0, 'g', 6).arg(bestVswr, 0, 'g', 6)
                                    .arg(fStd[static_cast<std::size_t>(resonance)], 0, 'g', 8)
                                    .arg(resZ.real(), 0, 'g', 7).arg(resZ.imag() >= 0.0 ? QStringLiteral("+") : QStringLiteral("−"))
                                    .arg(std::abs(resZ.imag()), 0, 'g', 7).arg(bandText));
        m_sweepSummary->setToolTip(QStringLiteral("The reported resonance is the sampled frequency with minimum |Im(Zin)|. With several simultaneously active feeds, Zin and Γ are active drive-state quantities rather than an N-port S-parameter matrix. The Hybrid broadband path reassembles and solves the complete coupled system at every frequency; refine the sweep around a candidate resonance before accepting a final design."));
    }

    QVector<double> fHzOut, rOut, xOut;
    fHzOut.reserve(static_cast<int>(fStd.size())); rOut.reserve(static_cast<int>(impedance.size())); xOut.reserve(static_cast<int>(impedance.size()));
    for (std::size_t i=0;i<fStd.size();++i)
    {
        fHzOut.push_back(fStd[i]*1e6);
        rOut.push_back(impedance[i].real());
        xOut.push_back(impedance[i].imag());
    }
    emit antennaSweepAvailable(fHzOut,rOut,xOut,observedReferenceOhm,plotFeedName);
    if (m_resultTabs) m_resultTabs->setCurrentWidget(m_sweepS11Plot);
}

void AntennaDesignerWidget::optimizeGeometry()
{
    if (m_wires.empty() || m_feeds.empty())
    {
        if (m_optStatus) m_optStatus->setText(QStringLiteral("ERROR: add at least one conductor and one feed."));
        return;
    }
    const int observedFeed = m_optFeed ? m_optFeed->currentIndex() : 0;
    if (observedFeed < 0 || observedFeed >= static_cast<int>(m_feeds.size()))
    {
        m_optStatus->setText(QStringLiteral("ERROR: select a valid feed to observe."));
        return;
    }

    const double targetMHz = m_optTargetMHz ? m_optTargetMHz->value() : m_frequencyMHz->value();
    const double factorMin = std::min(m_optMinFactor->value(), m_optMaxFactor->value());
    const double factorMax = std::max(m_optMinFactor->value(), m_optMaxFactor->value());
    if (!(targetMHz > 0.0) || !(factorMax > factorMin + 1e-9))
    {
        m_optStatus->setText(QStringLiteral("ERROR: target frequency must be positive and the factor interval must have non-zero width."));
        return;
    }

    const auto baseWires = m_wires;
    const auto baseFeeds = m_feeds;
    const QPointF pivot = baseFeeds[static_cast<std::size_t>(observedFeed)].positionM;
    const double topologyTol = 1e-6;
    const int wireCount = static_cast<int>(baseWires.size());

    auto samePoint = [&](const QPointF &a, const QPointF &b) {
        return std::hypot(a.x() - b.x(), a.y() - b.y()) <= topologyTol;
    };
    auto wiresTouch = [&](int ia, int ib) {
        const auto &a = baseWires[static_cast<std::size_t>(ia)];
        const auto &b = baseWires[static_cast<std::size_t>(ib)];
        return samePoint(a.aM,b.aM) || samePoint(a.aM,b.bM) || samePoint(a.bM,b.aM) || samePoint(a.bM,b.bM);
    };

    std::vector<int> component(static_cast<std::size_t>(wireCount), -1);
    int componentCount = 0;
    for (int seed = 0; seed < wireCount; ++seed)
    {
        if (component[static_cast<std::size_t>(seed)] >= 0) continue;
        std::vector<int> stack{seed};
        component[static_cast<std::size_t>(seed)] = componentCount;
        while (!stack.empty())
        {
            const int a = stack.back(); stack.pop_back();
            for (int b = 0; b < wireCount; ++b)
            {
                if (component[static_cast<std::size_t>(b)] >= 0 || !wiresTouch(a,b)) continue;
                component[static_cast<std::size_t>(b)] = componentCount;
                stack.push_back(b);
            }
        }
        ++componentCount;
    }

    int drivenComponent = -1;
    for (int i = 0; i < wireCount; ++i)
    {
        if (pointSegmentDistance(pivot, baseWires[static_cast<std::size_t>(i)].aM,
                                baseWires[static_cast<std::size_t>(i)].bM) <= topologyTol)
        {
            drivenComponent = component[static_cast<std::size_t>(i)];
            break;
        }
    }
    const int variable = m_optVariable ? m_optVariable->currentIndex() : 0;
    if (variable != 0 && drivenComponent < 0)
    {
        m_optStatus->setText(QStringLiteral("ERROR: the observed feed is not located on a conductor component, so this optimization variable cannot be defined."));
        return;
    }
    if (variable == 2 && componentCount < 2)
    {
        m_optStatus->setText(QStringLiteral("ERROR: parasitic-spacing optimization requires at least one disconnected conductor component in addition to the driven component."));
        return;
    }

    auto pointOnComponent = [&](const QPointF &p, int comp) {
        for (int i = 0; i < wireCount; ++i)
            if (component[static_cast<std::size_t>(i)] == comp &&
                pointSegmentDistance(p, baseWires[static_cast<std::size_t>(i)].aM, baseWires[static_cast<std::size_t>(i)].bM) <= topologyTol)
                return true;
        return false;
    };

    auto transformedGeometry = [&](double factor) {
        std::pair<std::vector<WireElement>,std::vector<FeedPoint>> out{baseWires,baseFeeds};
        auto scalePoint = [&](const QPointF &p) { return pivot + (p - pivot) * factor; };
        if (variable == 0)
        {
            for (auto &w : out.first) { w.aM = scalePoint(w.aM); w.bM = scalePoint(w.bM); }
            for (auto &f : out.second) f.positionM = scalePoint(f.positionM);
        }
        else if (variable == 1)
        {
            for (int i = 0; i < wireCount; ++i)
            {
                if (component[static_cast<std::size_t>(i)] != drivenComponent) continue;
                auto &w = out.first[static_cast<std::size_t>(i)];
                w.aM = scalePoint(w.aM); w.bM = scalePoint(w.bM);
            }
            for (std::size_t fi = 0; fi < out.second.size(); ++fi)
                if (pointOnComponent(baseFeeds[fi].positionM, drivenComponent))
                    out.second[fi].positionM = scalePoint(baseFeeds[fi].positionM);
        }
        else
        {
            for (int comp = 0; comp < componentCount; ++comp)
            {
                if (comp == drivenComponent) continue;
                std::vector<QPointF> uniqueNodes;
                auto addUnique = [&](const QPointF &q) {
                    for (const auto &n : uniqueNodes) if (samePoint(n,q)) return;
                    uniqueNodes.push_back(q);
                };
                for (int i = 0; i < wireCount; ++i)
                    if (component[static_cast<std::size_t>(i)] == comp)
                    { addUnique(baseWires[static_cast<std::size_t>(i)].aM); addUnique(baseWires[static_cast<std::size_t>(i)].bM); }
                if (uniqueNodes.empty()) continue;
                QPointF center(0,0);
                for (const auto &q : uniqueNodes) center += q;
                center /= double(uniqueNodes.size());
                const QPointF translatedCenter = pivot + (center - pivot) * factor;
                const QPointF delta = translatedCenter - center;
                for (int i = 0; i < wireCount; ++i)
                {
                    if (component[static_cast<std::size_t>(i)] != comp) continue;
                    auto &w = out.first[static_cast<std::size_t>(i)];
                    w.aM += delta; w.bM += delta;
                }
                for (std::size_t fi = 0; fi < out.second.size(); ++fi)
                    if (pointOnComponent(baseFeeds[fi].positionM, comp)) out.second[fi].positionM += delta;
            }
        }
        return out;
    };

    struct Evaluation
    {
        double factor = 1.0;
        double objective = std::numeric_limits<double>::infinity();
        std::complex<double> z{0,0};
        std::complex<double> gamma{0,0};
        double vswr = std::numeric_limits<double>::infinity();
        int unknowns = 0;
        bool valid = false;
        std::vector<WireElement> wires;
        std::vector<FeedPoint> feeds;
        QString error;
    };

    const int coarseSamples = std::max(5, m_optCoarseSamples ? m_optCoarseSamples->value() : 7);
    const int refineIterations = std::max(0, m_optRefineIterations ? m_optRefineIterations->value() : 6);
    const int estimatedEvaluations = coarseSamples + 3 + refineIterations;
    QProgressDialog progress(QStringLiteral("Optimizing antenna geometry with repeated MoM solves…"), QStringLiteral("Cancel"), 0, estimatedEvaluations, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    int evaluationCount = 0;
    bool canceled = false;
    std::vector<Evaluation> history;
    history.reserve(static_cast<std::size_t>(estimatedEvaluations + 2));

    auto evaluate = [&](double factor) -> Evaluation {
        Evaluation e; e.factor = factor;
        if (progress.wasCanceled()) { canceled = true; e.error = QStringLiteral("Canceled"); return e; }
        progress.setLabelText(QStringLiteral("MoM candidate %1 — factor %2").arg(evaluationCount + 1).arg(factor,0,'g',8));
        QApplication::processEvents();
        auto geometry = transformedGeometry(factor);
        e.wires = std::move(geometry.first); e.feeds = std::move(geometry.second);

        NumericalEM::WireNetworkMomInput in;
        in.frequencyHz = targetMHz * 1e6;
        in.segmentsPerWavelength = m_segmentsPerWavelength->value();
        in.maxUnknowns = m_maxMomUnknowns->value();
        in.nodeMergeToleranceM = 1e-6;
        in.maxSegmentLengthRadiusFactor = m_radiusMeshFactor ? m_radiusMeshFactor->value() : 3.5;
        in.junctionLocalSubdivisions = m_junctionLocalSubdivisions ? m_junctionLocalSubdivisions->value() : 3;
    in.junctionTreatment = (m_junctionTreatment && m_junctionTreatment->currentIndex()==1) ? NumericalEM::WireJunctionTreatment::LagrangeKcl : NumericalEM::WireJunctionTreatment::ReducedBasis;
    in.currentBasisTreatment = (m_currentBasisTreatment && m_currentBasisTreatment->currentIndex()==1) ? NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge : NumericalEM::WireCurrentBasisTreatment::PulsePointMatching;
        in.computeFarField = false;
        in.wires.reserve(e.wires.size());
        for (const auto &w : e.wires)
            in.wires.push_back(NumericalEM::WireSpan3D{w.name.toStdString(), {w.aM.x(),w.aM.y(),w.azM}, {w.bM.x(),w.bM.y(),w.bzM}, w.radiusM});
        in.feeds.reserve(e.feeds.size());
        for (const auto &f : e.feeds)
        {
            const double phase = f.phaseDeg * NumericalEM::Pi / 180.0;
            in.feeds.push_back(NumericalEM::WireFeed3D{f.name.toStdString(), {f.positionM.x(),f.positionM.y(),f.zM}, std::polar(f.voltageV,phase), f.sourceOhm});
        }
        const auto r = NumericalEM::solveWireNetworkMom(in);
        ++evaluationCount;
        progress.setValue(std::min(evaluationCount, estimatedEvaluations));
        if (!r.valid || observedFeed >= static_cast<int>(r.feeds.size()))
        {
            e.error = r.error.empty() ? QStringLiteral("Invalid MoM result") : QString::fromStdString(r.error);
            history.push_back(e);
            return e;
        }
        const auto &fr = r.feeds[static_cast<std::size_t>(observedFeed)];
        e.z = fr.activeImpedanceOhm;
        e.gamma = fr.reflectionCoefficient;
        e.vswr = fr.vswr;
        e.unknowns = r.unknownCount;
        const double zref = std::max(e.feeds[static_cast<std::size_t>(observedFeed)].sourceOhm,1e-9);
        e.objective = (m_optObjective && m_optObjective->currentIndex() == 1)
            ? std::abs(e.z.imag()) / zref
            : std::abs(e.gamma);
        e.valid = std::isfinite(e.objective);
        history.push_back(e);
        return e;
    };

    m_optStatus->setText(QStringLiteral("Optimization running…"));
    m_optSummary->clear();
    m_applyOptimized->setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QElapsedTimer timer; timer.start();

    std::vector<Evaluation> coarse;
    coarse.reserve(static_cast<std::size_t>(coarseSamples));
    for (int i=0;i<coarseSamples && !canceled;++i)
    {
        const double t = coarseSamples > 1 ? double(i)/double(coarseSamples-1) : 0.0;
        coarse.push_back(evaluate(factorMin + t*(factorMax-factorMin)));
    }
    if (!canceled && factorMin <= 1.0 && 1.0 <= factorMax)
    {
        bool already = false;
        for (const auto &e : coarse) if (std::abs(e.factor-1.0) < 1e-10) { already = true; break; }
        if (!already) evaluate(1.0);
    }

    int bestCoarse = -1;
    for (int i=0;i<static_cast<int>(coarse.size());++i)
        if (coarse[static_cast<std::size_t>(i)].valid && (bestCoarse<0 || coarse[static_cast<std::size_t>(i)].objective < coarse[static_cast<std::size_t>(bestCoarse)].objective)) bestCoarse=i;

    if (!canceled && bestCoarse >= 0 && refineIterations > 0 && coarse.size() >= 2)
    {
        const int leftIndex = std::max(0, bestCoarse-1);
        const int rightIndex = std::min(static_cast<int>(coarse.size())-1, bestCoarse+1);
        double lo = coarse[static_cast<std::size_t>(leftIndex)].factor;
        double hi = coarse[static_cast<std::size_t>(rightIndex)].factor;
        if (hi > lo + 1e-10)
        {
            constexpr double gr = 0.6180339887498948482;
            double c = hi - gr*(hi-lo), d = lo + gr*(hi-lo);
            Evaluation ec = evaluate(c), ed = canceled ? Evaluation{} : evaluate(d);
            for (int iter=0; iter<refineIterations && !canceled; ++iter)
            {
                const double oc = ec.valid ? ec.objective : std::numeric_limits<double>::infinity();
                const double od = ed.valid ? ed.objective : std::numeric_limits<double>::infinity();
                if (oc <= od)
                {
                    hi=d; d=c; ed=ec; c=hi-gr*(hi-lo); ec=evaluate(c);
                }
                else
                {
                    lo=c; c=d; ec=ed; d=lo+gr*(hi-lo); ed=evaluate(d);
                }
            }
        }
    }
    QApplication::restoreOverrideCursor();
    progress.setValue(estimatedEvaluations);
    const qint64 elapsedMs = timer.elapsed();

    int bestIndex = -1;
    int baselineIndex = -1;
    for (int i=0;i<static_cast<int>(history.size());++i)
    {
        if (history[static_cast<std::size_t>(i)].valid && (bestIndex<0 || history[static_cast<std::size_t>(i)].objective < history[static_cast<std::size_t>(bestIndex)].objective)) bestIndex=i;
        if (history[static_cast<std::size_t>(i)].valid && std::abs(history[static_cast<std::size_t>(i)].factor-1.0) < 1e-9) baselineIndex=i;
    }
    if (bestIndex < 0)
    {
        m_optStatus->setText(canceled ? QStringLiteral("Optimization canceled before a valid candidate was solved.")
                                     : QStringLiteral("Optimization failed: no valid MoM candidate in the requested interval."));
        if (!history.empty() && !history.front().error.isEmpty()) m_optSummary->setText(history.front().error);
        return;
    }

    std::vector<int> order(history.size());
    for (int i=0;i<static_cast<int>(order.size());++i) order[static_cast<std::size_t>(i)] = i;
    std::sort(order.begin(),order.end(),[&](int a,int b){return history[static_cast<std::size_t>(a)].factor < history[static_cast<std::size_t>(b)].factor;});
    QVector<double> xFactor, yObjective;
    for (int idx : order)
    {
        const auto &e=history[static_cast<std::size_t>(idx)];
        if (!e.valid) continue;
        xFactor.push_back(e.factor); yObjective.push_back(e.objective);
    }
    const QString objectiveName = (m_optObjective && m_optObjective->currentIndex()==1) ? QStringLiteral("|Xin| / Z0") : QStringLiteral("|Γ|");
    m_optimizationPlot->setSeries({FieldProfileSeries{xFactor,yObjective,objectiveName,QString(),false}},
                                  QStringLiteral("MoM geometry optimization objective at %1 MHz").arg(targetMHz,0,'g',8));
    m_optimizationPlot->setMarkers({FieldPlotMarker{QStringLiteral("Best"),history[static_cast<std::size_t>(bestIndex)].factor,true,true,false}});

    const auto &best=history[static_cast<std::size_t>(bestIndex)];
    m_optimizedWires=best.wires; m_optimizedFeeds=best.feeds;
    m_applyOptimized->setEnabled(true);
    const double gm=std::abs(best.gamma);
    const double s11db=20.0*std::log10(std::max(gm,1e-15));
    const QString variableName = m_optVariable ? m_optVariable->currentText() : QStringLiteral("Geometry scale");
    QString improvement;
    if (baselineIndex>=0)
    {
        const auto &base=history[static_cast<std::size_t>(baselineIndex)];
        if (base.objective>1e-15)
            improvement=QStringLiteral(" | objective change vs factor 1: %1 %").arg(100.0*(best.objective/base.objective-1.0),0,'g',5);
    }
    m_optStatus->setText(QStringLiteral("%1 — %2 valid/attempted candidate(s) in %3 s. Best geometry is staged; use Apply best geometry to modify the design.")
                             .arg(canceled ? QStringLiteral("Optimization canceled; partial result retained") : QStringLiteral("Optimization complete"))
                             .arg(xFactor.size()).arg(history.size()).arg(elapsedMs/1000.0,0,'f',2));
    m_optSummary->setText(QStringLiteral("Variable: %1\nBest factor = %2 | objective %3 = %4%5\nZin = %6 %7 j%8 Ω | S11 = %9 dB | VSWR = %10 | unknowns = %11")
                              .arg(variableName).arg(best.factor,0,'g',9).arg(objectiveName).arg(best.objective,0,'g',7).arg(improvement)
                              .arg(best.z.real(),0,'g',8).arg(best.z.imag()>=0.0?QStringLiteral("+"):QStringLiteral("−")).arg(std::abs(best.z.imag()),0,'g',8)
                              .arg(s11db,0,'g',7).arg(std::isfinite(best.vswr)?QString::number(best.vswr,'g',7):QStringLiteral("∞")).arg(best.unknowns));
    m_optSummary->setToolTip(QStringLiteral("This is a one-dimensional numerical search, not a proof of a global optimum. Repeat with a wider interval or different mesh settings and verify convergence before using a design result."));
    if (m_resultTabs) m_resultTabs->setCurrentWidget(m_optimizationPlot);
}

void AntennaDesignerWidget::refreshYagiIndividualVariables()
{
    if (!m_individualDirectorTable) return;

    struct RowState
    {
        bool lengthFree = true;
        bool positionFree = false;
        double lengthMin = 0.90;
        double lengthMax = 1.10;
        double positionMin = 0.90;
        double positionMax = 1.10;
    };
    std::vector<RowState> oldStates;
    oldStates.reserve(static_cast<std::size_t>(m_individualDirectorTable->rowCount()));
    for (int r = 0; r < m_individualDirectorTable->rowCount(); ++r)
    {
        RowState st;
        if (auto *it = m_individualDirectorTable->item(r, 1)) st.lengthFree = it->checkState() == Qt::Checked;
        if (auto *it = m_individualDirectorTable->item(r, 4)) st.positionFree = it->checkState() == Qt::Checked;
        if (auto *w = qobject_cast<QDoubleSpinBox *>(m_individualDirectorTable->cellWidget(r, 2))) st.lengthMin = w->value();
        if (auto *w = qobject_cast<QDoubleSpinBox *>(m_individualDirectorTable->cellWidget(r, 3))) st.lengthMax = w->value();
        if (auto *w = qobject_cast<QDoubleSpinBox *>(m_individualDirectorTable->cellWidget(r, 5))) st.positionMin = w->value();
        if (auto *w = qobject_cast<QDoubleSpinBox *>(m_individualDirectorTable->cellWidget(r, 6))) st.positionMax = w->value();
        oldStates.push_back(st);
    }

    m_individualDirectorTable->blockSignals(true);
    m_individualDirectorTable->setRowCount(0);
    if (m_wires.empty() || m_feeds.empty())
    {
        m_individualDirectorTable->blockSignals(false);
        if (m_individualStatus) m_individualStatus->setText(QStringLiteral("No Yagi-like geometry detected."));
        return;
    }

    const int observedFeed = (m_multiFeed && m_multiFeed->currentIndex() >= 0) ? m_multiFeed->currentIndex() : 0;
    if (observedFeed >= static_cast<int>(m_feeds.size()))
    {
        m_individualDirectorTable->blockSignals(false);
        if (m_individualStatus) m_individualStatus->setText(QStringLiteral("Select a valid feed before detecting directors."));
        return;
    }

    const double tol = 1e-6;
    const int wireCount = static_cast<int>(m_wires.size());
    auto samePoint = [&](const QPointF &a, const QPointF &b) {
        return std::hypot(a.x()-b.x(), a.y()-b.y()) <= tol;
    };
    auto wiresTouch = [&](int ia, int ib) {
        const auto &a=m_wires[static_cast<std::size_t>(ia)];
        const auto &b=m_wires[static_cast<std::size_t>(ib)];
        return samePoint(a.aM,b.aM)||samePoint(a.aM,b.bM)||samePoint(a.bM,b.aM)||samePoint(a.bM,b.bM);
    };
    std::vector<int> component(static_cast<std::size_t>(wireCount),-1);
    int componentCount=0;
    for (int seed=0;seed<wireCount;++seed)
    {
        if (component[static_cast<std::size_t>(seed)]>=0) continue;
        std::vector<int> stack{seed}; component[static_cast<std::size_t>(seed)]=componentCount;
        while(!stack.empty())
        {
            const int a=stack.back(); stack.pop_back();
            for(int b=0;b<wireCount;++b)
            {
                if(component[static_cast<std::size_t>(b)]>=0||!wiresTouch(a,b)) continue;
                component[static_cast<std::size_t>(b)]=componentCount; stack.push_back(b);
            }
        }
        ++componentCount;
    }
    auto locateComponent=[&](const QPointF &p) {
        for(int i=0;i<wireCount;++i)
            if(pointSegmentDistance(p,m_wires[static_cast<std::size_t>(i)].aM,m_wires[static_cast<std::size_t>(i)].bM)<=tol)
                return component[static_cast<std::size_t>(i)];
        return -1;
    };
    const int driven=locateComponent(m_feeds[static_cast<std::size_t>(observedFeed)].positionM);
    if(driven<0||componentCount<3)
    {
        m_individualDirectorTable->blockSignals(false);
        if (m_individualStatus) m_individualStatus->setText(QStringLiteral("A driven element, one reflector and at least one disconnected director are required."));
        return;
    }
    std::vector<double> lengths(static_cast<std::size_t>(componentCount),0.0);
    std::vector<QPointF> centers(static_cast<std::size_t>(componentCount),QPointF());
    for(int i=0;i<wireCount;++i)
    {
        const auto &w=m_wires[static_cast<std::size_t>(i)];
        const double len=std::hypot(w.bM.x()-w.aM.x(),w.bM.y()-w.aM.y());
        const int c=component[static_cast<std::size_t>(i)];
        lengths[static_cast<std::size_t>(c)]+=len;
        centers[static_cast<std::size_t>(c)]+=((w.aM+w.bM)*0.5)*len;
    }
    for(int c=0;c<componentCount;++c) if(lengths[static_cast<std::size_t>(c)]>1e-15) centers[static_cast<std::size_t>(c)]/=lengths[static_cast<std::size_t>(c)];
    std::vector<int> parasitic; for(int c=0;c<componentCount;++c) if(c!=driven) parasitic.push_back(c);
    if(parasitic.size()<2)
    {
        m_individualDirectorTable->blockSignals(false);
        if (m_individualStatus) m_individualStatus->setText(QStringLiteral("At least two parasitic components are required."));
        return;
    }
    int reflector=parasitic.front();
    for(int c:parasitic) if(lengths[static_cast<std::size_t>(c)]>lengths[static_cast<std::size_t>(reflector)]) reflector=c;
    const QPointF drivenCenter=centers[static_cast<std::size_t>(driven)];
    QPointF forward=drivenCenter-centers[static_cast<std::size_t>(reflector)];
    const double fn=std::hypot(forward.x(),forward.y());
    if(fn<=1e-9)
    {
        m_individualDirectorTable->blockSignals(false);
        if (m_individualStatus) m_individualStatus->setText(QStringLiteral("Reflector and driven-element centers are not spatially separated."));
        return;
    }
    forward/=fn;
    std::vector<int> directors;
    for(int c:parasitic)
    {
        if(c==reflector) continue;
        if(QPointF::dotProduct(centers[static_cast<std::size_t>(c)]-drivenCenter,forward)>tol) directors.push_back(c);
    }
    std::sort(directors.begin(),directors.end(),[&](int a,int b){
        return QPointF::dotProduct(centers[static_cast<std::size_t>(a)]-drivenCenter,forward)<QPointF::dotProduct(centers[static_cast<std::size_t>(b)]-drivenCenter,forward);
    });
    if(directors.empty())
    {
        m_individualDirectorTable->blockSignals(false);
        if (m_individualStatus) m_individualStatus->setText(QStringLiteral("No director was found in the forward boom direction."));
        return;
    }

    const double commonMin=m_multiMinFactor?m_multiMinFactor->value():0.80;
    const double commonMax=m_multiMaxFactor?m_multiMaxFactor->value():1.20;
    m_individualDirectorTable->setRowCount(static_cast<int>(directors.size()));
    for(int r=0;r<static_cast<int>(directors.size());++r)
    {
        const RowState st = r<static_cast<int>(oldStates.size()) ? oldStates[static_cast<std::size_t>(r)] : RowState{true,false,std::max(commonMin,0.90),std::min(commonMax,1.10),std::max(commonMin,0.90),std::min(commonMax,1.10)};
        auto *name=new QTableWidgetItem(QStringLiteral("D%1 (C%2)").arg(r+1).arg(directors[static_cast<std::size_t>(r)]+1));
        name->setFlags(Qt::ItemIsEnabled); m_individualDirectorTable->setItem(r,0,name);
        auto *lf=new QTableWidgetItem(); lf->setFlags(Qt::ItemIsEnabled|Qt::ItemIsUserCheckable); lf->setCheckState(st.lengthFree?Qt::Checked:Qt::Unchecked); m_individualDirectorTable->setItem(r,1,lf);
        auto *pf=new QTableWidgetItem(); pf->setFlags(Qt::ItemIsEnabled|Qt::ItemIsUserCheckable); pf->setCheckState(st.positionFree?Qt::Checked:Qt::Unchecked); m_individualDirectorTable->setItem(r,4,pf);
        auto makeFactor=[&](double value) {
            auto *box=numberBox(m_individualDirectorTable,value,0.20,2.50,4);
            QObject::connect(box,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double){clearMultiOptimizationResults();});
            return box;
        };
        m_individualDirectorTable->setCellWidget(r,2,makeFactor(st.lengthMin));
        m_individualDirectorTable->setCellWidget(r,3,makeFactor(st.lengthMax));
        m_individualDirectorTable->setCellWidget(r,5,makeFactor(st.positionMin));
        m_individualDirectorTable->setCellWidget(r,6,makeFactor(st.positionMax));
    }
    m_individualDirectorTable->blockSignals(false);
    if (m_individualStatus) m_individualStatus->setText(QStringLiteral("Detected %1 director(s). Length and boom-position factors are relative to the current geometry.").arg(directors.size()));
}

void AntennaDesignerWidget::optimizeYagiMulti()
{
    if (m_wires.empty() || m_feeds.empty())
    {
        if (m_multiStatus) m_multiStatus->setText(QStringLiteral("ERROR: add a Yagi-like geometry and at least one feed."));
        return;
    }
    const int observedFeed = m_multiFeed ? m_multiFeed->currentIndex() : 0;
    if (observedFeed < 0 || observedFeed >= static_cast<int>(m_feeds.size()))
    {
        m_multiStatus->setText(QStringLiteral("ERROR: select a valid driven feed."));
        return;
    }

    const double targetMHz = m_multiTargetMHz ? m_multiTargetMHz->value() : m_frequencyMHz->value();
    const double factorMin = std::min(m_multiMinFactor->value(), m_multiMaxFactor->value());
    const double factorMax = std::max(m_multiMinFactor->value(), m_multiMaxFactor->value());
    if (!(targetMHz > 0.0) || !(factorMax > factorMin + 1e-9))
    {
        m_multiStatus->setText(QStringLiteral("ERROR: target frequency must be positive and the factor interval must have non-zero width."));
        return;
    }

    std::vector<int> activeVariables;
    if (m_multiDrivenLength && m_multiDrivenLength->isChecked()) activeVariables.push_back(0);
    if (m_multiReflectorLength && m_multiReflectorLength->isChecked()) activeVariables.push_back(1);
    if (m_multiDirectorLength && m_multiDirectorLength->isChecked()) activeVariables.push_back(2);
    if (m_multiReflectorSpacing && m_multiReflectorSpacing->isChecked()) activeVariables.push_back(3);
    if (m_multiDirectorSpacing && m_multiDirectorSpacing->isChecked()) activeVariables.push_back(4);
    if (activeVariables.empty())
    {
        m_multiStatus->setText(QStringLiteral("ERROR: enable at least one geometry variable."));
        return;
    }

    const double wMatch = m_multiWeightMatch ? m_multiWeightMatch->value() : 1.0;
    const double wDirectivity = m_multiWeightDirectivity ? m_multiWeightDirectivity->value() : 0.0;
    const double wFrontBack = m_multiWeightFrontBack ? m_multiWeightFrontBack->value() : 0.0;
    const double wBandwidth = m_multiWeightBandwidth ? m_multiWeightBandwidth->value() : 0.0;
    const double weightSum = wMatch + wDirectivity + wFrontBack + wBandwidth;
    if (!(weightSum > 0.0))
    {
        m_multiStatus->setText(QStringLiteral("ERROR: at least one objective weight must be greater than zero."));
        return;
    }

    const auto baseWires = m_wires;
    const auto baseFeeds = m_feeds;
    const double topologyTol = 1e-6;
    const int wireCount = static_cast<int>(baseWires.size());
    auto samePoint = [&](const QPointF &a, const QPointF &b) {
        return std::hypot(a.x() - b.x(), a.y() - b.y()) <= topologyTol;
    };
    auto wiresTouch = [&](int ia, int ib) {
        const auto &a = baseWires[static_cast<std::size_t>(ia)];
        const auto &b = baseWires[static_cast<std::size_t>(ib)];
        return samePoint(a.aM,b.aM) || samePoint(a.aM,b.bM) || samePoint(a.bM,b.aM) || samePoint(a.bM,b.bM);
    };

    std::vector<int> component(static_cast<std::size_t>(wireCount), -1);
    int componentCount = 0;
    for (int seed = 0; seed < wireCount; ++seed)
    {
        if (component[static_cast<std::size_t>(seed)] >= 0) continue;
        std::vector<int> stack{seed};
        component[static_cast<std::size_t>(seed)] = componentCount;
        while (!stack.empty())
        {
            const int a = stack.back(); stack.pop_back();
            for (int b = 0; b < wireCount; ++b)
            {
                if (component[static_cast<std::size_t>(b)] >= 0 || !wiresTouch(a,b)) continue;
                component[static_cast<std::size_t>(b)] = componentCount;
                stack.push_back(b);
            }
        }
        ++componentCount;
    }
    if (componentCount < 3)
    {
        m_multiStatus->setText(QStringLiteral("ERROR: the Yagi-array optimizer requires at least three disconnected conductor components: one reflector, one driven element and at least one director. Detected: %1.").arg(componentCount));
        return;
    }

    auto locateComponent = [&](const QPointF &p) {
        for (int i = 0; i < wireCount; ++i)
            if (pointSegmentDistance(p, baseWires[static_cast<std::size_t>(i)].aM,
                                    baseWires[static_cast<std::size_t>(i)].bM) <= topologyTol)
                return component[static_cast<std::size_t>(i)];
        return -1;
    };
    const int drivenComponent = locateComponent(baseFeeds[static_cast<std::size_t>(observedFeed)].positionM);
    if (drivenComponent < 0)
    {
        m_multiStatus->setText(QStringLiteral("ERROR: the observed feed is not located on a conductor component."));
        return;
    }

    std::vector<double> componentLength(static_cast<std::size_t>(componentCount), 0.0);
    std::vector<QPointF> componentCenter(static_cast<std::size_t>(componentCount), QPointF());
    for (int i = 0; i < wireCount; ++i)
    {
        const auto &w = baseWires[static_cast<std::size_t>(i)];
        const double len = std::hypot(w.bM.x()-w.aM.x(), w.bM.y()-w.aM.y());
        const int c = component[static_cast<std::size_t>(i)];
        componentLength[static_cast<std::size_t>(c)] += len;
        componentCenter[static_cast<std::size_t>(c)] += ((w.aM+w.bM)*0.5) * len;
    }
    for (int c=0;c<componentCount;++c)
        if (componentLength[static_cast<std::size_t>(c)] > 1e-15)
            componentCenter[static_cast<std::size_t>(c)] /= componentLength[static_cast<std::size_t>(c)];

    std::vector<int> parasitic;
    for (int c=0;c<componentCount;++c) if (c != drivenComponent) parasitic.push_back(c);
    if (parasitic.size() < 2)
    {
        m_multiStatus->setText(QStringLiteral("ERROR: at least two parasitic components are required."));
        return;
    }

    int reflectorComponent = parasitic.front();
    for (int c : parasitic)
        if (componentLength[static_cast<std::size_t>(c)] > componentLength[static_cast<std::size_t>(reflectorComponent)] + 1e-12)
            reflectorComponent = c;

    const QPointF drivenCenter = componentCenter[static_cast<std::size_t>(drivenComponent)];
    const QPointF reflectorCenter = componentCenter[static_cast<std::size_t>(reflectorComponent)];
    QPointF forward = drivenCenter - reflectorCenter;
    const double forwardNorm = std::hypot(forward.x(), forward.y());
    if (!(forwardNorm > 1e-9))
    {
        m_multiStatus->setText(QStringLiteral("ERROR: reflector and driven-element centroids must be spatially separated."));
        return;
    }
    forward /= forwardNorm;

    std::vector<int> directorComponents;
    for (int c : parasitic)
    {
        if (c == reflectorComponent) continue;
        const QPointF delta = componentCenter[static_cast<std::size_t>(c)] - drivenCenter;
        const double projection = QPointF::dotProduct(delta, forward);
        if (!(projection > topologyTol))
        {
            m_multiStatus->setText(QStringLiteral("ERROR: all parasitic components other than the inferred reflector must lie in the forward boom half-plane. Component C%1 has projection %2 m. Re-orient the geometry or use the Yagi-array preset.")
                                       .arg(c+1).arg(projection,0,'g',6));
            return;
        }
        directorComponents.push_back(c);
    }
    if (directorComponents.empty())
    {
        m_multiStatus->setText(QStringLiteral("ERROR: no director component was found in the forward boom direction."));
        return;
    }
    std::sort(directorComponents.begin(), directorComponents.end(), [&](int a, int b) {
        return QPointF::dotProduct(componentCenter[static_cast<std::size_t>(a)] - drivenCenter, forward) <
               QPointF::dotProduct(componentCenter[static_cast<std::size_t>(b)] - drivenCenter, forward);
    });

    struct Factors { std::array<double,5> v{{1.0,1.0,1.0,1.0,1.0}}; };
    auto isDirector = [&](int comp) {
        return std::find(directorComponents.begin(), directorComponents.end(), comp) != directorComponents.end();
    };
    auto transformedGeometry = [&](const Factors &factors) {
        std::pair<std::vector<WireElement>,std::vector<FeedPoint>> out{baseWires,baseFeeds};
        auto transformPoint = [&](const QPointF &p, int comp) {
            if (comp == drivenComponent)
                return drivenCenter + (p-drivenCenter) * factors.v[0];
            if (comp == reflectorComponent)
            {
                const QPointF newCenter = drivenCenter + (reflectorCenter-drivenCenter) * factors.v[3];
                return newCenter + (p-reflectorCenter) * factors.v[1];
            }
            if (isDirector(comp))
            {
                const QPointF oldCenter = componentCenter[static_cast<std::size_t>(comp)];
                const QPointF newCenter = drivenCenter + (oldCenter-drivenCenter) * factors.v[4];
                return newCenter + (p-oldCenter) * factors.v[2];
            }
            return p;
        };
        for (int i=0;i<wireCount;++i)
        {
            const int comp=component[static_cast<std::size_t>(i)];
            out.first[static_cast<std::size_t>(i)].aM=transformPoint(baseWires[static_cast<std::size_t>(i)].aM,comp);
            out.first[static_cast<std::size_t>(i)].bM=transformPoint(baseWires[static_cast<std::size_t>(i)].bM,comp);
        }
        for (std::size_t fi=0;fi<out.second.size();++fi)
        {
            const int comp=locateComponent(baseFeeds[fi].positionM);
            if (comp>=0) out.second[fi].positionM=transformPoint(baseFeeds[fi].positionM,comp);
        }
        return out;
    };

    auto componentCentroidFor = [&](const std::vector<WireElement> &wires, int comp) {
        QPointF center(0,0); double length=0.0;
        for (int i=0;i<wireCount;++i)
        {
            if (component[static_cast<std::size_t>(i)] != comp) continue;
            const auto &w=wires[static_cast<std::size_t>(i)];
            const double len=std::hypot(w.bM.x()-w.aM.x(),w.bM.y()-w.aM.y());
            center += ((w.aM+w.bM)*0.5)*len; length += len;
        }
        return length>1e-15 ? center/length : QPointF();
    };

    const double targetLambda = C0 / (targetMHz * 1e6);
    const double minSpacingLambda = m_multiMinElementSpacingLambda ? m_multiMinElementSpacingLambda->value() : 0.0;
    const double maxBoomLambda = m_multiMaxBoomLengthLambda ? m_multiMaxBoomLengthLambda->value() : 0.0;
    auto constraintMetrics = [&](const std::vector<WireElement> &wires) {
        std::vector<double> projection;
        projection.reserve(static_cast<std::size_t>(componentCount));
        for (int c=0;c<componentCount;++c)
        {
            const QPointF center=componentCentroidFor(wires,c);
            projection.push_back(QPointF::dotProduct(center-drivenCenter,forward));
        }
        std::sort(projection.begin(),projection.end());
        double minGap=std::numeric_limits<double>::infinity();
        for (std::size_t i=1;i<projection.size();++i)
            minGap=std::min(minGap,projection[i]-projection[i-1]);
        const double boom=projection.empty()?0.0:projection.back()-projection.front();
        return std::pair<double,double>{minGap/targetLambda,boom/targetLambda};
    };

    auto solveAt = [&](const std::vector<WireElement> &wires, const std::vector<FeedPoint> &feeds,
                       double frequencyMHz, bool computeFarField) {
        NumericalEM::WireNetworkMomInput in;
        in.frequencyHz = frequencyMHz * 1e6;
        in.segmentsPerWavelength = m_segmentsPerWavelength->value();
        in.maxUnknowns = m_maxMomUnknowns->value();
        in.nodeMergeToleranceM = 1e-6;
        in.maxSegmentLengthRadiusFactor = m_radiusMeshFactor ? m_radiusMeshFactor->value() : 3.5;
        in.junctionLocalSubdivisions = m_junctionLocalSubdivisions ? m_junctionLocalSubdivisions->value() : 3;
    in.junctionTreatment = (m_junctionTreatment && m_junctionTreatment->currentIndex()==1) ? NumericalEM::WireJunctionTreatment::LagrangeKcl : NumericalEM::WireJunctionTreatment::ReducedBasis;
    in.currentBasisTreatment = (m_currentBasisTreatment && m_currentBasisTreatment->currentIndex()==1) ? NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge : NumericalEM::WireCurrentBasisTreatment::PulsePointMatching;
        in.computeFarField = computeFarField;
        if (computeFarField)
        {
            in.farFieldCutStepDeg = 5.0;
            in.farFieldIntegrationStepDeg = 15.0;
        }
        in.wires.reserve(wires.size());
        for (const auto &w : wires)
            in.wires.push_back(NumericalEM::WireSpan3D{w.name.toStdString(), {w.aM.x(),w.aM.y(),w.azM}, {w.bM.x(),w.bM.y(),w.bzM}, w.radiusM});
        in.feeds.reserve(feeds.size());
        for (const auto &feed : feeds)
        {
            const double phase=feed.phaseDeg*NumericalEM::Pi/180.0;
            in.feeds.push_back(NumericalEM::WireFeed3D{feed.name.toStdString(), {feed.positionM.x(),feed.positionM.y(),feed.zM}, std::polar(feed.voltageV,phase), feed.sourceOhm});
        }
        return NumericalEM::solveWireNetworkMom(in);
    };

    struct Evaluation
    {
        Factors factors;
        double objective=std::numeric_limits<double>::infinity();
        double matchPenalty=std::numeric_limits<double>::infinity();
        double directivityPenalty=0.0;
        double frontBackPenalty=0.0;
        double bandwidthPenalty=0.0;
        double bandwidthMean=0.0;
        double bandwidthWorst=0.0;
        double directivityDbi=0.0;
        double frontBackDb=0.0;
        double minSpacingLambda=0.0;
        double boomLengthLambda=0.0;
        std::complex<double> z{0,0};
        std::complex<double> gamma{0,0};
        double vswr=std::numeric_limits<double>::infinity();
        int unknowns=0;
        bool valid=false;
        std::vector<WireElement> wires;
        std::vector<FeedPoint> feeds;
        QString error;
    };

    const int samples=std::max(3,m_multiSamplesPerVariable?m_multiSamplesPerVariable->value():5);
    const int passes=std::max(1,m_multiPasses?m_multiPasses->value():2);
    const bool needFarField=(wDirectivity>0.0 || wFrontBack>0.0);
    const bool needBandwidth=(wBandwidth>0.0);
    const double bwHalfPct=m_multiBandwidthHalfSpanPct?m_multiBandwidthHalfSpanPct->value():5.0;
    const int bandSamples=m_multiBandwidthSamples?m_multiBandwidthSamples->value():5;
    const bool bandWorstCase=m_multiBandwidthAggregation && m_multiBandwidthAggregation->currentIndex()==1;
    const int estimatedCandidates=1+passes*static_cast<int>(activeVariables.size())*samples;
    QProgressDialog progress(QStringLiteral("Multi-parameter Yagi-array optimization with repeated MoM solves…"),QStringLiteral("Cancel"),0,estimatedCandidates,this);
    progress.setWindowModality(Qt::WindowModal); progress.setMinimumDuration(0);
    int attempted=0; bool canceled=false; int constraintRejected=0;
    std::vector<Evaluation> history; history.reserve(static_cast<std::size_t>(estimatedCandidates));
    std::map<std::string,std::size_t> cache;

    auto keyFor=[&](const Factors &f) {
        return QStringLiteral("%1|%2|%3|%4|%5")
            .arg(f.v[0],0,'f',9).arg(f.v[1],0,'f',9).arg(f.v[2],0,'f',9).arg(f.v[3],0,'f',9).arg(f.v[4],0,'f',9).toStdString();
    };
    auto evaluate=[&](const Factors &factors) -> Evaluation {
        ++attempted;
        progress.setValue(std::min(attempted,estimatedCandidates));
        if (progress.wasCanceled()) { canceled=true; Evaluation e; e.error=QStringLiteral("Canceled"); return e; }
        const std::string key=keyFor(factors);
        const auto cached=cache.find(key);
        if (cached!=cache.end()) return history[cached->second];
        progress.setLabelText(QStringLiteral("Candidate %1/%2 — factors [%3, %4, %5, %6, %7]%8")
                                  .arg(attempted).arg(estimatedCandidates)
                                  .arg(factors.v[0],0,'g',5).arg(factors.v[1],0,'g',5).arg(factors.v[2],0,'g',5)
                                  .arg(factors.v[3],0,'g',5).arg(factors.v[4],0,'g',5)
                                  .arg(needBandwidth?QStringLiteral(" — %1-point band").arg(bandSamples):QString()));
        QApplication::processEvents();
        Evaluation e; e.factors=factors;
        auto geometry=transformedGeometry(factors); e.wires=std::move(geometry.first); e.feeds=std::move(geometry.second);
        const auto metrics=constraintMetrics(e.wires);
        e.minSpacingLambda=metrics.first; e.boomLengthLambda=metrics.second;
        if ((minSpacingLambda>0.0 && e.minSpacingLambda+1e-12<minSpacingLambda) ||
            (maxBoomLambda>0.0 && e.boomLengthLambda-1e-12>maxBoomLambda))
        {
            ++constraintRejected;
            e.error=QStringLiteral("Geometric constraint rejected candidate: min spacing %1 λ, boom %2 λ.")
                        .arg(e.minSpacingLambda,0,'g',5).arg(e.boomLengthLambda,0,'g',5);
            cache[key]=history.size(); history.push_back(e); return e;
        }

        const auto centerResult=solveAt(e.wires,e.feeds,targetMHz,needFarField);
        if (!centerResult.valid || observedFeed>=static_cast<int>(centerResult.feeds.size()))
        {
            e.error=centerResult.error.empty()?QStringLiteral("Invalid center-frequency MoM result"):QString::fromStdString(centerResult.error);
            cache[key]=history.size(); history.push_back(e); return e;
        }
        const auto &fr=centerResult.feeds[static_cast<std::size_t>(observedFeed)];
        e.z=fr.activeImpedanceOhm; e.gamma=fr.reflectionCoefficient; e.vswr=fr.vswr; e.unknowns=centerResult.unknownCount;
        e.matchPenalty=std::abs(e.gamma);
        if (wDirectivity>0.0)
        {
            const double d=std::max(centerResult.directivityLinear,1e-9);
            e.directivityPenalty=1.0/d; e.directivityDbi=centerResult.directivityDbi;
        }
        if (wFrontBack>0.0)
        {
            if (centerResult.azimuthDeg.empty() || centerResult.azimuthNormalizedFarField.empty())
            {
                e.error=QStringLiteral("Far-field samples are unavailable for front/back evaluation.");
                cache[key]=history.size(); history.push_back(e); return e;
            }
            double frontDeg=std::atan2(forward.y(),forward.x())*180.0/NumericalEM::Pi;
            if (frontDeg<0.0) frontDeg+=360.0;
            const double backDeg=std::fmod(frontDeg+180.0,360.0);
            auto amplitudeAt=[&](double targetDeg) {
                double bestDelta=1e9, amp=0.0;
                const std::size_t n=std::min(centerResult.azimuthDeg.size(),centerResult.azimuthNormalizedFarField.size());
                for (std::size_t i=0;i<n;++i)
                {
                    double d=std::abs(centerResult.azimuthDeg[i]-targetDeg); d=std::min(d,360.0-d);
                    if (d<bestDelta) { bestDelta=d; amp=centerResult.azimuthNormalizedFarField[i]; }
                }
                return std::max(amp,1e-9);
            };
            const double front=amplitudeAt(frontDeg), back=amplitudeAt(backDeg);
            const double fbrPower=std::max((front/back)*(front/back),1e-9);
            e.frontBackDb=10.0*std::log10(fbrPower);
            e.frontBackPenalty=1.0/fbrPower;
        }
        if (needBandwidth)
        {
            double sumGamma=0.0, worstGamma=0.0;
            for (int bi=0;bi<bandSamples;++bi)
            {
                if (progress.wasCanceled()) { canceled=true; e.error=QStringLiteral("Canceled"); return e; }
                const double t=bandSamples>1?double(bi)/double(bandSamples-1):0.5;
                const double offsetPct=-bwHalfPct+2.0*bwHalfPct*t;
                const double fMHz=targetMHz*std::max(0.001,1.0+offsetPct/100.0);
                double gm=0.0;
                if (std::abs(fMHz-targetMHz)<=std::max(1e-12,targetMHz*1e-12))
                    gm=e.matchPenalty;
                else
                {
                    QApplication::processEvents();
                    const auto bandResult=solveAt(e.wires,e.feeds,fMHz,false);
                    if (!bandResult.valid || observedFeed>=static_cast<int>(bandResult.feeds.size()))
                    {
                        e.error=QStringLiteral("Multi-frequency band MoM solve failed at %1 MHz.").arg(fMHz,0,'g',8);
                        cache[key]=history.size(); history.push_back(e); return e;
                    }
                    gm=std::abs(bandResult.feeds[static_cast<std::size_t>(observedFeed)].reflectionCoefficient);
                }
                sumGamma+=gm; worstGamma=std::max(worstGamma,gm);
            }
            e.bandwidthMean=sumGamma/static_cast<double>(bandSamples);
            e.bandwidthWorst=worstGamma;
            e.bandwidthPenalty=bandWorstCase?e.bandwidthWorst:e.bandwidthMean;
        }
        const double weighted=wMatch*e.matchPenalty+wDirectivity*e.directivityPenalty+wFrontBack*e.frontBackPenalty+wBandwidth*e.bandwidthPenalty;
        e.objective=weighted/weightSum;
        e.valid=std::isfinite(e.objective);
        cache[key]=history.size(); history.push_back(e); return e;
    };

    m_multiStatus->setText(QStringLiteral("Yagi-array optimization running…"));
    m_multiSummary->clear(); m_applyMultiOptimized->setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QElapsedTimer timer; timer.start();

    Factors current;
    (void)evaluate(current);
    std::array<double,5> lo{{factorMin,factorMin,factorMin,factorMin,factorMin}};
    std::array<double,5> hi{{factorMax,factorMax,factorMax,factorMax,factorMax}};
    for (int pass=0;pass<passes && !canceled;++pass)
    {
        for (int var:activeVariables)
        {
            if (canceled) break;
            std::vector<double> values(static_cast<std::size_t>(samples));
            for (int i=0;i<samples;++i)
            {
                const double t=samples>1?double(i)/double(samples-1):0.0;
                values[static_cast<std::size_t>(i)]=lo[static_cast<std::size_t>(var)]+t*(hi[static_cast<std::size_t>(var)]-lo[static_cast<std::size_t>(var)]);
            }
            int bestSample=-1; Evaluation bestEval;
            for (int i=0;i<samples && !canceled;++i)
            {
                Factors trial=current; trial.v[static_cast<std::size_t>(var)]=values[static_cast<std::size_t>(i)];
                const Evaluation e=evaluate(trial);
                if (e.valid && (bestSample<0 || e.objective<bestEval.objective)) { bestSample=i; bestEval=e; }
            }
            if (bestSample>=0)
            {
                current=bestEval.factors;
                const double newLo=values[static_cast<std::size_t>(std::max(0,bestSample-1))];
                const double newHi=values[static_cast<std::size_t>(std::min(samples-1,bestSample+1))];
                lo[static_cast<std::size_t>(var)]=newLo; hi[static_cast<std::size_t>(var)]=newHi;
            }
        }
    }
    QApplication::restoreOverrideCursor(); progress.setValue(estimatedCandidates);
    const qint64 elapsedMs=timer.elapsed();

    int bestIndex=-1, baselineIndex=-1;
    for (int i=0;i<static_cast<int>(history.size());++i)
    {
        if (history[static_cast<std::size_t>(i)].valid && (bestIndex<0 || history[static_cast<std::size_t>(i)].objective<history[static_cast<std::size_t>(bestIndex)].objective)) bestIndex=i;
        bool isBaseline=true; for (double v:history[static_cast<std::size_t>(i)].factors.v) if (std::abs(v-1.0)>1e-9) {isBaseline=false;break;}
        if (history[static_cast<std::size_t>(i)].valid && isBaseline) baselineIndex=i;
    }
    if (bestIndex<0)
    {
        m_multiStatus->setText(canceled?QStringLiteral("Optimization canceled before a valid candidate was solved."):QStringLiteral("Optimization failed: no valid candidate. Check the geometric constraints and factor bounds."));
        if (!history.empty()&&!history.front().error.isEmpty()) m_multiSummary->setText(history.front().error);
        return;
    }

    QVector<double> xEval,yObjective,yMatch,yDirectivity,yFrontBack,yBandwidth;
    for (int i=0;i<static_cast<int>(history.size());++i)
    {
        const auto &e=history[static_cast<std::size_t>(i)]; if(!e.valid) continue;
        xEval.push_back(i+1.0); yObjective.push_back(e.objective); yMatch.push_back(e.matchPenalty);
        if (wDirectivity>0.0) yDirectivity.push_back(e.directivityPenalty);
        if (wFrontBack>0.0) yFrontBack.push_back(e.frontBackPenalty);
        if (wBandwidth>0.0) yBandwidth.push_back(e.bandwidthPenalty);
    }
    QVector<FieldProfileSeries> series;
    series.push_back(FieldProfileSeries{xEval,yObjective,QStringLiteral("Weighted objective"),QString(),false});
    series.push_back(FieldProfileSeries{xEval,yMatch,QStringLiteral("Center |Γ|"),QString(),false});
    if (wDirectivity>0.0) series.push_back(FieldProfileSeries{xEval,yDirectivity,QStringLiteral("1 / D"),QString(),false});
    if (wFrontBack>0.0) series.push_back(FieldProfileSeries{xEval,yFrontBack,QStringLiteral("1 / FBR"),QString(),false});
    if (wBandwidth>0.0) series.push_back(FieldProfileSeries{xEval,yBandwidth,bandWorstCase?QStringLiteral("Band worst |Γ|"):QStringLiteral("Band mean |Γ|"),QString(),false});
    m_multiOptimizationPlot->setSeries(series,QStringLiteral("Yagi-array multi-parameter MoM optimization at %1 MHz").arg(targetMHz,0,'g',8));
    m_multiOptimizationPlot->setMarkers({FieldPlotMarker{QStringLiteral("Best"),bestIndex+1.0,true,true,false}});

    const auto &best=history[static_cast<std::size_t>(bestIndex)];
    m_multiOptimizedWires=best.wires; m_multiOptimizedFeeds=best.feeds; m_applyMultiOptimized->setEnabled(true);
    const double s11Db=20.0*std::log10(std::max(std::abs(best.gamma),1e-15));
    QString improvement;
    if (baselineIndex>=0 && history[static_cast<std::size_t>(baselineIndex)].objective>1e-15)
        improvement=QStringLiteral(" | objective change vs original: %1 %").arg(100.0*(best.objective/history[static_cast<std::size_t>(baselineIndex)].objective-1.0),0,'g',5);
    const QString directionText=wDirectivity>0.0?QStringLiteral(" | D = %1 dBi").arg(best.directivityDbi,0,'g',6):QString();
    const QString frontBackText=wFrontBack>0.0?QStringLiteral(" | F/B = %1 dB").arg(best.frontBackDb,0,'g',6):QString();
    const QString bwText=wBandwidth>0.0?QStringLiteral(" | band mean/worst |Γ| = %1 / %2").arg(best.bandwidthMean,0,'g',6).arg(best.bandwidthWorst,0,'g',6):QString();
    QString directorList;
    for (std::size_t i=0;i<directorComponents.size();++i)
    {
        if (i) directorList += QStringLiteral(", ");
        directorList += QStringLiteral("C%1").arg(directorComponents[i]+1);
    }
    m_multiStatus->setText(QStringLiteral("%1 — %2 unique valid candidate(s), %3 candidate requests, %4 constraint rejection(s) in %5 s. Best geometry is staged.")
                               .arg(canceled?QStringLiteral("Optimization canceled; partial result retained"):QStringLiteral("Optimization complete"))
                               .arg(xEval.size()).arg(attempted).arg(constraintRejected).arg(elapsedMs/1000.0,0,'f',2));
    m_multiSummary->setText(QStringLiteral("Component classification: driven C%1 | reflector C%2 (longest parasitic) | %3 director(s): %4\n"
                                             "Best factors [Ld, Lr, Ldirs, Sr, Sdirs] = [%5, %6, %7, %8, %9]\n"
                                             "Objective = %10%11\nZin = %12 %13 j%14 Ω | S11 = %15 dB | VSWR = %16%17%18%19\n"
                                             "Geometry: min adjacent spacing = %20 λ | boom = %21 λ | unknowns = %22")
                                .arg(drivenComponent+1).arg(reflectorComponent+1).arg(directorComponents.size()).arg(directorList)
                                .arg(best.factors.v[0],0,'g',7).arg(best.factors.v[1],0,'g',7).arg(best.factors.v[2],0,'g',7)
                                .arg(best.factors.v[3],0,'g',7).arg(best.factors.v[4],0,'g',7)
                                .arg(best.objective,0,'g',7).arg(improvement)
                                .arg(best.z.real(),0,'g',8).arg(best.z.imag()>=0.0?QStringLiteral("+"):QStringLiteral("−")).arg(std::abs(best.z.imag()),0,'g',8)
                                .arg(s11Db,0,'g',7).arg(std::isfinite(best.vswr)?QString::number(best.vswr,'g',7):QStringLiteral("∞"))
                                .arg(directionText).arg(frontBackText).arg(bwText)
                                .arg(best.minSpacingLambda,0,'g',6).arg(best.boomLengthLambda,0,'g',6).arg(best.unknowns));
    m_multiSummary->setToolTip(QStringLiteral("Coordinate search is a local derivative-free optimization, not a proof of the global optimum. The longest parasitic component is classified as reflector and all remaining parasitics must be in the forward boom direction. The director variables scale all directors together, preserving their relative taper and spacing. Band matching is evaluated by repeated full MoM solves at several frequencies. Radiation metrics use a coarser optimization angular grid (5° cuts, 15° solid-angle integration); re-run the applied geometry with the normal full-resolution MoM solve, mesh refinement and a final dense frequency sweep before accepting a design."));
    if (m_resultTabs) m_resultTabs->setCurrentWidget(m_multiOptimizationPlot);
}

void AntennaDesignerWidget::optimizeYagiIndividual()
{
    if (m_wires.empty() || m_feeds.empty() || !m_individualDirectorTable)
    {
        if (m_individualStatus) m_individualStatus->setText(QStringLiteral("ERROR: add a Yagi-like geometry and at least one feed."));
        return;
    }
    const int observedFeed=m_multiFeed?m_multiFeed->currentIndex():0;
    if(observedFeed<0||observedFeed>=static_cast<int>(m_feeds.size()))
    {
        m_individualStatus->setText(QStringLiteral("ERROR: select a valid driven feed."));
        return;
    }
    const double targetMHz=m_multiTargetMHz?m_multiTargetMHz->value():m_frequencyMHz->value();
    if(!(targetMHz>0.0))
    {
        m_individualStatus->setText(QStringLiteral("ERROR: target frequency must be positive."));
        return;
    }

    const auto baseWires=m_wires;
    const auto baseFeeds=m_feeds;
    const int wireCount=static_cast<int>(baseWires.size());
    const double tol=1e-6;
    auto samePoint=[&](const QPointF&a,const QPointF&b){return std::hypot(a.x()-b.x(),a.y()-b.y())<=tol;};
    auto wiresTouch=[&](int ia,int ib){
        const auto&a=baseWires[static_cast<std::size_t>(ia)]; const auto&b=baseWires[static_cast<std::size_t>(ib)];
        return samePoint(a.aM,b.aM)||samePoint(a.aM,b.bM)||samePoint(a.bM,b.aM)||samePoint(a.bM,b.bM);
    };
    std::vector<int> component(static_cast<std::size_t>(wireCount),-1); int componentCount=0;
    for(int seed=0;seed<wireCount;++seed)
    {
        if(component[static_cast<std::size_t>(seed)]>=0) continue;
        std::vector<int> stack{seed}; component[static_cast<std::size_t>(seed)]=componentCount;
        while(!stack.empty())
        {
            const int a=stack.back(); stack.pop_back();
            for(int b=0;b<wireCount;++b)
            {
                if(component[static_cast<std::size_t>(b)]>=0||!wiresTouch(a,b)) continue;
                component[static_cast<std::size_t>(b)]=componentCount; stack.push_back(b);
            }
        }
        ++componentCount;
    }
    auto locateComponent=[&](const QPointF&p){
        for(int i=0;i<wireCount;++i)
            if(pointSegmentDistance(p,baseWires[static_cast<std::size_t>(i)].aM,baseWires[static_cast<std::size_t>(i)].bM)<=tol)
                return component[static_cast<std::size_t>(i)];
        return -1;
    };
    const int drivenComponent=locateComponent(baseFeeds[static_cast<std::size_t>(observedFeed)].positionM);
    if(drivenComponent<0||componentCount<3)
    {
        m_individualStatus->setText(QStringLiteral("ERROR: a driven component, one reflector and at least one director are required."));
        return;
    }
    std::vector<double> componentLength(static_cast<std::size_t>(componentCount),0.0);
    std::vector<QPointF> componentCenter(static_cast<std::size_t>(componentCount),QPointF());
    for(int i=0;i<wireCount;++i)
    {
        const auto&w=baseWires[static_cast<std::size_t>(i)];
        const double len=std::hypot(w.bM.x()-w.aM.x(),w.bM.y()-w.aM.y());
        const int c=component[static_cast<std::size_t>(i)];
        componentLength[static_cast<std::size_t>(c)]+=len;
        componentCenter[static_cast<std::size_t>(c)]+=((w.aM+w.bM)*0.5)*len;
    }
    for(int c=0;c<componentCount;++c) if(componentLength[static_cast<std::size_t>(c)]>1e-15) componentCenter[static_cast<std::size_t>(c)]/=componentLength[static_cast<std::size_t>(c)];
    std::vector<int> parasitic; for(int c=0;c<componentCount;++c) if(c!=drivenComponent) parasitic.push_back(c);
    if(parasitic.size()<2){m_individualStatus->setText(QStringLiteral("ERROR: at least two parasitic components are required."));return;}
    int reflectorComponent=parasitic.front();
    for(int c:parasitic) if(componentLength[static_cast<std::size_t>(c)]>componentLength[static_cast<std::size_t>(reflectorComponent)]) reflectorComponent=c;
    const QPointF drivenCenter=componentCenter[static_cast<std::size_t>(drivenComponent)];
    const QPointF reflectorCenter=componentCenter[static_cast<std::size_t>(reflectorComponent)];
    QPointF forward=drivenCenter-reflectorCenter; const double fn=std::hypot(forward.x(),forward.y());
    if(fn<=1e-9){m_individualStatus->setText(QStringLiteral("ERROR: reflector and driven centers must be separated."));return;} forward/=fn;
    std::vector<int> directorComponents;
    for(int c:parasitic)
    {
        if(c==reflectorComponent) continue;
        const double proj=QPointF::dotProduct(componentCenter[static_cast<std::size_t>(c)]-drivenCenter,forward);
        if(proj<=tol){m_individualStatus->setText(QStringLiteral("ERROR: all non-reflector parasitic elements must lie in the forward boom half-plane."));return;}
        directorComponents.push_back(c);
    }
    std::sort(directorComponents.begin(),directorComponents.end(),[&](int a,int b){
        return QPointF::dotProduct(componentCenter[static_cast<std::size_t>(a)]-drivenCenter,forward)<QPointF::dotProduct(componentCenter[static_cast<std::size_t>(b)]-drivenCenter,forward);
    });
    if(directorComponents.empty()){m_individualStatus->setText(QStringLiteral("ERROR: no directors detected."));return;}
    if(m_individualDirectorTable->rowCount()!=static_cast<int>(directorComponents.size())) refreshYagiIndividualVariables();
    if(m_individualDirectorTable->rowCount()!=static_cast<int>(directorComponents.size()))
    {
        m_individualStatus->setText(QStringLiteral("ERROR: director-variable table does not match the current geometry. Use Detect / refresh directors."));
        return;
    }

    const double commonMin=std::min(m_multiMinFactor->value(),m_multiMaxFactor->value());
    const double commonMax=std::max(m_multiMinFactor->value(),m_multiMaxFactor->value());
    const int directorCount=static_cast<int>(directorComponents.size());
    const int parameterCount=3+2*directorCount;
    struct Parameter { QString name; bool active=false; double lo=1.0; double hi=1.0; };
    std::vector<Parameter> params(static_cast<std::size_t>(parameterCount));
    params[0]={QStringLiteral("Driven length"),m_multiDrivenLength&&m_multiDrivenLength->isChecked(),commonMin,commonMax};
    params[1]={QStringLiteral("Reflector length"),m_multiReflectorLength&&m_multiReflectorLength->isChecked(),commonMin,commonMax};
    params[2]={QStringLiteral("Reflector spacing"),m_multiReflectorSpacing&&m_multiReflectorSpacing->isChecked(),commonMin,commonMax};
    for(int i=0;i<directorCount;++i)
    {
        const int li=3+2*i, pi=li+1;
        auto *lf=m_individualDirectorTable->item(i,1); auto *pf=m_individualDirectorTable->item(i,4);
        auto *lmin=qobject_cast<QDoubleSpinBox*>(m_individualDirectorTable->cellWidget(i,2));
        auto *lmax=qobject_cast<QDoubleSpinBox*>(m_individualDirectorTable->cellWidget(i,3));
        auto *pmin=qobject_cast<QDoubleSpinBox*>(m_individualDirectorTable->cellWidget(i,5));
        auto *pmax=qobject_cast<QDoubleSpinBox*>(m_individualDirectorTable->cellWidget(i,6));
        const double ll=lmin?std::min(lmin->value(),lmax?lmax->value():lmin->value()):commonMin;
        const double lh=lmax?std::max(lmin?lmin->value():lmax->value(),lmax->value()):commonMax;
        const double pl=pmin?std::min(pmin->value(),pmax?pmax->value():pmin->value()):commonMin;
        const double ph=pmax?std::max(pmin?pmin->value():pmax->value(),pmax->value()):commonMax;
        params[static_cast<std::size_t>(li)]={QStringLiteral("D%1 length").arg(i+1),lf&&lf->checkState()==Qt::Checked,ll,lh};
        params[static_cast<std::size_t>(pi)]={QStringLiteral("D%1 position").arg(i+1),pf&&pf->checkState()==Qt::Checked,pl,ph};
    }
    std::vector<int> active;
    for(int i=0;i<parameterCount;++i) if(params[static_cast<std::size_t>(i)].active&&params[static_cast<std::size_t>(i)].hi>params[static_cast<std::size_t>(i)].lo+1e-9) active.push_back(i);
    if(active.empty()){m_individualStatus->setText(QStringLiteral("ERROR: enable at least one individual geometry variable with a non-zero interval."));return;}

    const double wMatch=m_multiWeightMatch?m_multiWeightMatch->value():1.0;
    const double wDirectivity=m_multiWeightDirectivity?m_multiWeightDirectivity->value():0.0;
    const double wFrontBack=m_multiWeightFrontBack?m_multiWeightFrontBack->value():0.0;
    const double wBandwidth=m_multiWeightBandwidth?m_multiWeightBandwidth->value():0.0;
    const double weightSum=wMatch+wDirectivity+wFrontBack+wBandwidth;
    if(!(weightSum>0.0)){m_individualStatus->setText(QStringLiteral("ERROR: at least one objective weight must be positive."));return;}

    std::map<int,int> directorIndex;
    for(int i=0;i<directorCount;++i) directorIndex[directorComponents[static_cast<std::size_t>(i)]]=i;
    struct Factors { std::vector<double> v; };
    auto transformedGeometry=[&](const Factors&f){
        std::pair<std::vector<WireElement>,std::vector<FeedPoint>> out{baseWires,baseFeeds};
        auto transformPoint=[&](const QPointF&p,int comp){
            if(comp==drivenComponent) return drivenCenter+(p-drivenCenter)*f.v[0];
            if(comp==reflectorComponent)
            {
                const QPointF newCenter=drivenCenter+(reflectorCenter-drivenCenter)*f.v[2];
                return newCenter+(p-reflectorCenter)*f.v[1];
            }
            const auto it=directorIndex.find(comp);
            if(it!=directorIndex.end())
            {
                const int di=it->second; const QPointF oldCenter=componentCenter[static_cast<std::size_t>(comp)];
                const double lf=f.v[static_cast<std::size_t>(3+2*di)];
                const double pf=f.v[static_cast<std::size_t>(4+2*di)];
                const QPointF newCenter=drivenCenter+(oldCenter-drivenCenter)*pf;
                return newCenter+(p-oldCenter)*lf;
            }
            return p;
        };
        for(int i=0;i<wireCount;++i)
        {
            const int comp=component[static_cast<std::size_t>(i)];
            out.first[static_cast<std::size_t>(i)].aM=transformPoint(baseWires[static_cast<std::size_t>(i)].aM,comp);
            out.first[static_cast<std::size_t>(i)].bM=transformPoint(baseWires[static_cast<std::size_t>(i)].bM,comp);
        }
        for(std::size_t fi=0;fi<out.second.size();++fi)
        {
            const int comp=locateComponent(baseFeeds[fi].positionM); if(comp>=0) out.second[fi].positionM=transformPoint(baseFeeds[fi].positionM,comp);
        }
        return out;
    };
    auto centroidFor=[&](const std::vector<WireElement>&wires,int comp){
        QPointF center; double total=0.0;
        for(int i=0;i<wireCount;++i) if(component[static_cast<std::size_t>(i)]==comp)
        {
            const auto&w=wires[static_cast<std::size_t>(i)]; const double len=std::hypot(w.bM.x()-w.aM.x(),w.bM.y()-w.aM.y());
            center+=((w.aM+w.bM)*0.5)*len; total+=len;
        }
        return total>1e-15?center/total:QPointF();
    };
    struct ConstraintResult { double minSpacingLambda=0.0; double boomLambda=0.0; bool ordered=true; };
    const double lambda=C0/(targetMHz*1e6);
    const double minSpacing=m_multiMinElementSpacingLambda?m_multiMinElementSpacingLambda->value():0.0;
    const double maxBoom=m_multiMaxBoomLengthLambda?m_multiMaxBoomLengthLambda->value():0.0;
    auto constraints=[&](const std::vector<WireElement>&wires){
        ConstraintResult c; std::vector<double> orderedProj; orderedProj.reserve(static_cast<std::size_t>(directorCount+2));
        const double rProj=QPointF::dotProduct(centroidFor(wires,reflectorComponent)-drivenCenter,forward);
        orderedProj.push_back(rProj); orderedProj.push_back(0.0);
        double previous=0.0;
        if(!(rProj<0.0)) c.ordered=false;
        for(int dc:directorComponents)
        {
            const double p=QPointF::dotProduct(centroidFor(wires,dc)-drivenCenter,forward);
            if(!(p>previous+1e-12)) c.ordered=false;
            previous=p; orderedProj.push_back(p);
        }
        c.minSpacingLambda=std::numeric_limits<double>::infinity();
        for(std::size_t i=1;i<orderedProj.size();++i) c.minSpacingLambda=std::min(c.minSpacingLambda,(orderedProj[i]-orderedProj[i-1])/lambda);
        c.boomLambda=(orderedProj.back()-orderedProj.front())/lambda;
        return c;
    };
    auto solveAt=[&](const std::vector<WireElement>&wires,const std::vector<FeedPoint>&feeds,double frequencyMHz,bool far){
        NumericalEM::WireNetworkMomInput in; in.frequencyHz=frequencyMHz*1e6; in.segmentsPerWavelength=m_segmentsPerWavelength->value(); in.maxUnknowns=m_maxMomUnknowns->value(); in.nodeMergeToleranceM=1e-6; in.maxSegmentLengthRadiusFactor=m_radiusMeshFactor?m_radiusMeshFactor->value():3.5; in.junctionLocalSubdivisions=m_junctionLocalSubdivisions?m_junctionLocalSubdivisions->value():3; in.junctionTreatment=(m_junctionTreatment&&m_junctionTreatment->currentIndex()==1)?NumericalEM::WireJunctionTreatment::LagrangeKcl:NumericalEM::WireJunctionTreatment::ReducedBasis; in.currentBasisTreatment=(m_currentBasisTreatment&&m_currentBasisTreatment->currentIndex()==1)?NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge:NumericalEM::WireCurrentBasisTreatment::PulsePointMatching; in.computeFarField=far;
        if(far){in.farFieldCutStepDeg=5.0;in.farFieldIntegrationStepDeg=15.0;}
        for(const auto&w:wires) in.wires.push_back(NumericalEM::WireSpan3D{w.name.toStdString(),{w.aM.x(),w.aM.y(),w.azM},{w.bM.x(),w.bM.y(),w.bzM},w.radiusM});
        for(const auto&feed:feeds){const double ph=feed.phaseDeg*NumericalEM::Pi/180.0;in.feeds.push_back(NumericalEM::WireFeed3D{feed.name.toStdString(),{feed.positionM.x(),feed.positionM.y(),feed.zM},std::polar(feed.voltageV,ph),feed.sourceOhm});}
        return NumericalEM::solveWireNetworkMom(in);
    };
    struct Eval
    {
        Factors factors; double objective=std::numeric_limits<double>::infinity(),match=0.0,dirPenalty=0.0,fbPenalty=0.0,bwPenalty=0.0,bwMean=0.0,bwWorst=0.0,dDbi=0.0,fbDb=0.0,minSpacingLambda=0.0,boomLambda=0.0; std::complex<double> z{0,0},gamma{0,0}; double vswr=std::numeric_limits<double>::infinity(); int unknowns=0; bool valid=false; QString error; std::vector<WireElement>wires; std::vector<FeedPoint>feeds;
    };
    const int samples=std::max(3,m_multiSamplesPerVariable?m_multiSamplesPerVariable->value():5);
    const int passes=std::max(1,m_multiPasses?m_multiPasses->value():2);
    const bool needFar=wDirectivity>0.0||wFrontBack>0.0, needBw=wBandwidth>0.0;
    const int bandSamples=m_multiBandwidthSamples?m_multiBandwidthSamples->value():5;
    const double bwHalf=m_multiBandwidthHalfSpanPct?m_multiBandwidthHalfSpanPct->value():5.0;
    const bool worst=m_multiBandwidthAggregation&&m_multiBandwidthAggregation->currentIndex()==1;
    const int estimated=1+passes*static_cast<int>(active.size())*samples;
    QProgressDialog progress(QStringLiteral("Individual-director Yagi optimization with repeated MoM solves…"),QStringLiteral("Cancel"),0,estimated,this); progress.setWindowModality(Qt::WindowModal); progress.setMinimumDuration(0);
    int attempted=0,rejected=0; bool canceled=false; std::vector<Eval>history; history.reserve(static_cast<std::size_t>(estimated)); std::map<std::string,std::size_t>cache;
    auto keyFor=[&](const Factors&f){QStringList parts; for(double v:f.v) parts<<QString::number(v,'f',8); return parts.join('|').toStdString();};
    auto evaluate=[&](const Factors&f)->Eval{
        ++attempted; progress.setValue(std::min(attempted,estimated)); if(progress.wasCanceled()){canceled=true;Eval e;e.error=QStringLiteral("Canceled");return e;}
        const std::string key=keyFor(f); if(auto it=cache.find(key);it!=cache.end()) return history[it->second];
        progress.setLabelText(QStringLiteral("Candidate %1/%2 — %3 active variables%4").arg(attempted).arg(estimated).arg(active.size()).arg(needBw?QStringLiteral(" — %1-point band").arg(bandSamples):QString())); QApplication::processEvents();
        Eval e; e.factors=f; auto g=transformedGeometry(f); e.wires=std::move(g.first);e.feeds=std::move(g.second); const auto cm=constraints(e.wires);e.minSpacingLambda=cm.minSpacingLambda;e.boomLambda=cm.boomLambda;
        if(!cm.ordered||(minSpacing>0.0&&cm.minSpacingLambda+1e-12<minSpacing)||(maxBoom>0.0&&cm.boomLambda-1e-12>maxBoom)){++rejected;e.error=QStringLiteral("Geometric constraint/order rejection");cache[key]=history.size();history.push_back(e);return e;}
        const auto center=solveAt(e.wires,e.feeds,targetMHz,needFar); if(!center.valid||observedFeed>=static_cast<int>(center.feeds.size())){e.error=center.error.empty()?QStringLiteral("Invalid MoM result"):QString::fromStdString(center.error);cache[key]=history.size();history.push_back(e);return e;}
        const auto&fr=center.feeds[static_cast<std::size_t>(observedFeed)];e.z=fr.activeImpedanceOhm;e.gamma=fr.reflectionCoefficient;e.vswr=fr.vswr;e.unknowns=center.unknownCount;e.match=std::abs(e.gamma);
        if(wDirectivity>0.0){const double d=std::max(center.directivityLinear,1e-9);e.dirPenalty=1.0/d;e.dDbi=center.directivityDbi;}
        if(wFrontBack>0.0)
        {
            if(center.azimuthDeg.empty()||center.azimuthNormalizedFarField.empty()){e.error=QStringLiteral("Far-field samples unavailable");cache[key]=history.size();history.push_back(e);return e;}
            double fd=std::atan2(forward.y(),forward.x())*180.0/NumericalEM::Pi;if(fd<0)fd+=360.0;const double bd=std::fmod(fd+180.0,360.0);
            auto ampAt=[&](double td){double dd=1e9,a=0.0;const std::size_t n=std::min(center.azimuthDeg.size(),center.azimuthNormalizedFarField.size());for(std::size_t i=0;i<n;++i){double d=std::abs(center.azimuthDeg[i]-td);d=std::min(d,360.0-d);if(d<dd){dd=d;a=center.azimuthNormalizedFarField[i];}}return std::max(a,1e-9);};
            const double fa=ampAt(fd),ba=ampAt(bd),ratio=std::max((fa/ba)*(fa/ba),1e-9);e.fbDb=10.0*std::log10(ratio);e.fbPenalty=1.0/ratio;
        }
        if(needBw)
        {
            double sum=0.0,worstG=0.0;for(int bi=0;bi<bandSamples;++bi){if(progress.wasCanceled()){canceled=true;e.error=QStringLiteral("Canceled");return e;}const double t=bandSamples>1?double(bi)/double(bandSamples-1):0.5;const double fm=targetMHz*std::max(0.001,1.0+(-bwHalf+2.0*bwHalf*t)/100.0);double gm=e.match;if(std::abs(fm-targetMHz)>std::max(1e-12,targetMHz*1e-12)){QApplication::processEvents();const auto br=solveAt(e.wires,e.feeds,fm,false);if(!br.valid||observedFeed>=static_cast<int>(br.feeds.size())){e.error=QStringLiteral("Band MoM failed at %1 MHz").arg(fm,0,'g',8);cache[key]=history.size();history.push_back(e);return e;}gm=std::abs(br.feeds[static_cast<std::size_t>(observedFeed)].reflectionCoefficient);}sum+=gm;worstG=std::max(worstG,gm);}e.bwMean=sum/double(bandSamples);e.bwWorst=worstG;e.bwPenalty=worst?e.bwWorst:e.bwMean;
        }
        e.objective=(wMatch*e.match+wDirectivity*e.dirPenalty+wFrontBack*e.fbPenalty+wBandwidth*e.bwPenalty)/weightSum;e.valid=std::isfinite(e.objective);cache[key]=history.size();history.push_back(e);return e;
    };

    Factors current; current.v.assign(static_cast<std::size_t>(parameterCount),1.0);
    std::vector<double>lo(static_cast<std::size_t>(parameterCount),1.0),hi(static_cast<std::size_t>(parameterCount),1.0);
    for(int idx:active){lo[static_cast<std::size_t>(idx)]=params[static_cast<std::size_t>(idx)].lo;hi[static_cast<std::size_t>(idx)]=params[static_cast<std::size_t>(idx)].hi;}
    m_individualStatus->setText(QStringLiteral("Individual-director optimization running…"));m_individualSummary->clear();m_applyIndividualOptimized->setEnabled(false);QApplication::setOverrideCursor(Qt::WaitCursor);QElapsedTimer timer;timer.start();(void)evaluate(current);
    for(int pass=0;pass<passes&&!canceled;++pass)
    {
        for(int var:active)
        {
            std::vector<double>values(static_cast<std::size_t>(samples));for(int i=0;i<samples;++i){const double t=samples>1?double(i)/double(samples-1):0.0;values[static_cast<std::size_t>(i)]=lo[static_cast<std::size_t>(var)]+t*(hi[static_cast<std::size_t>(var)]-lo[static_cast<std::size_t>(var)]);}
            int bestSample=-1;Eval bestEval;for(int i=0;i<samples&&!canceled;++i){Factors trial=current;trial.v[static_cast<std::size_t>(var)]=values[static_cast<std::size_t>(i)];const Eval e=evaluate(trial);if(e.valid&&(bestSample<0||e.objective<bestEval.objective)){bestSample=i;bestEval=e;}}
            if(bestSample>=0){current=bestEval.factors;lo[static_cast<std::size_t>(var)]=values[static_cast<std::size_t>(std::max(0,bestSample-1))];hi[static_cast<std::size_t>(var)]=values[static_cast<std::size_t>(std::min(samples-1,bestSample+1))];}
            if(canceled) break;
        }
    }
    QApplication::restoreOverrideCursor();progress.setValue(estimated);const qint64 elapsed=timer.elapsed();
    int bestIndex=-1,baseIndex=-1;for(int i=0;i<static_cast<int>(history.size());++i){const auto&e=history[static_cast<std::size_t>(i)];if(e.valid&&(bestIndex<0||e.objective<history[static_cast<std::size_t>(bestIndex)].objective))bestIndex=i;bool base=e.valid;for(double v:e.factors.v)if(std::abs(v-1.0)>1e-9){base=false;break;}if(base)baseIndex=i;}
    if(bestIndex<0){m_individualStatus->setText(canceled?QStringLiteral("Optimization canceled before a valid candidate was solved."):QStringLiteral("Optimization failed: no valid candidate."));return;}
    QVector<double>x,yObj,yMatch,yDir,yFb,yBw;for(int i=0;i<static_cast<int>(history.size());++i){const auto&e=history[static_cast<std::size_t>(i)];if(!e.valid)continue;x.push_back(i+1.0);yObj.push_back(e.objective);yMatch.push_back(e.match);if(wDirectivity>0)yDir.push_back(e.dirPenalty);if(wFrontBack>0)yFb.push_back(e.fbPenalty);if(wBandwidth>0)yBw.push_back(e.bwPenalty);}
    QVector<FieldProfileSeries>series{{x,yObj,QStringLiteral("Weighted objective"),QString(),false},{x,yMatch,QStringLiteral("Center |Γ|"),QString(),false}};if(wDirectivity>0)series.push_back(FieldProfileSeries{x,yDir,QStringLiteral("1 / D"),QString(),false});if(wFrontBack>0)series.push_back(FieldProfileSeries{x,yFb,QStringLiteral("1 / FBR"),QString(),false});if(wBandwidth>0)series.push_back(FieldProfileSeries{x,yBw,worst?QStringLiteral("Band worst |Γ|"):QStringLiteral("Band mean |Γ|"),QString(),false});
    m_individualOptimizationPlot->setSeries(series,QStringLiteral("Individual-director Yagi optimization at %1 MHz").arg(targetMHz,0,'g',8));m_individualOptimizationPlot->setMarkers({FieldPlotMarker{QStringLiteral("Best"),bestIndex+1.0,true,true,false}});
    const auto&best=history[static_cast<std::size_t>(bestIndex)];m_individualOptimizedWires=best.wires;m_individualOptimizedFeeds=best.feeds;m_applyIndividualOptimized->setEnabled(true);const double s11=20.0*std::log10(std::max(std::abs(best.gamma),1e-15));
    QString factorText=QStringLiteral("Driven L=%1 | Reflector L=%2 | Reflector pos=%3").arg(best.factors.v[0],0,'g',6).arg(best.factors.v[1],0,'g',6).arg(best.factors.v[2],0,'g',6);for(int i=0;i<directorCount;++i)factorText+=QStringLiteral("\nD%1: L=%2 | pos=%3").arg(i+1).arg(best.factors.v[static_cast<std::size_t>(3+2*i)],0,'g',6).arg(best.factors.v[static_cast<std::size_t>(4+2*i)],0,'g',6);
    QString improvement;if(baseIndex>=0&&history[static_cast<std::size_t>(baseIndex)].objective>1e-15)improvement=QStringLiteral(" | Δ objective vs original = %1 %").arg(100.0*(best.objective/history[static_cast<std::size_t>(baseIndex)].objective-1.0),0,'g',5);
    m_individualStatus->setText(QStringLiteral("%1 — %2 active variables, %3 valid candidates, %4 requests, %5 rejected in %6 s. Best geometry is staged.").arg(canceled?QStringLiteral("Canceled; partial result retained"):QStringLiteral("Optimization complete")).arg(active.size()).arg(x.size()).arg(attempted).arg(rejected).arg(elapsed/1000.0,0,'f',2));
    m_individualSummary->setText(QStringLiteral("%1\nObjective=%2%3\nZin=%4 %5 j%6 Ω | S11=%7 dB | VSWR=%8 | D=%9 dBi | F/B=%10 dB\nBand mean/worst |Γ|=%11 / %12 | min spacing=%13 λ | boom=%14 λ | unknowns=%15").arg(factorText).arg(best.objective,0,'g',7).arg(improvement).arg(best.z.real(),0,'g',8).arg(best.z.imag()>=0?QStringLiteral("+"):QStringLiteral("−")).arg(std::abs(best.z.imag()),0,'g',8).arg(s11,0,'g',7).arg(std::isfinite(best.vswr)?QString::number(best.vswr,'g',7):QStringLiteral("∞")).arg(best.dDbi,0,'g',6).arg(best.fbDb,0,'g',6).arg(best.bwMean,0,'g',6).arg(best.bwWorst,0,'g',6).arg(best.minSpacingLambda,0,'g',6).arg(best.boomLambda,0,'g',6).arg(best.unknowns));
    m_individualSummary->setToolTip(QStringLiteral("Each enabled director length and position is optimized independently by bounded coordinate search. This remains a local derivative-free search. Validate the staged design with a full-resolution MoM solve, mesh-convergence study and dense frequency sweep."));if(m_resultTabs)m_resultTabs->setCurrentWidget(m_individualOptimizationPlot);
}

void AntennaDesignerWidget::applyMultiOptimizedGeometry()
{
    if (m_multiOptimizedWires.empty() || m_multiOptimizedFeeds.empty()) return;
    pushGeometryHistory();
    auto wires=m_multiOptimizedWires; auto feeds=m_multiOptimizedFeeds;
    m_wires=std::move(wires); m_feeds=std::move(feeds);
    rebuildScene(false);
    if (m_multiStatus) m_multiStatus->setText(QStringLiteral("Multi-parameter optimized geometry applied. RF results were cleared; run a full MoM solve and frequency sweep to validate the design."));
}

void AntennaDesignerWidget::applyOptimizedGeometry()
{
    if (m_optimizedWires.empty() || m_optimizedFeeds.empty()) return;
    pushGeometryHistory();
    auto wires=m_optimizedWires;
    auto feeds=m_optimizedFeeds;
    m_wires=std::move(wires); m_feeds=std::move(feeds);
    rebuildScene(false);
    if (m_optStatus) m_optStatus->setText(QStringLiteral("Optimized geometry applied. RF results were cleared because the geometry changed; solve or sweep again to validate the applied design."));
}

void AntennaDesignerWidget::deleteSelectedRows()
{
    std::vector<int> wireRows;
    for (const auto &idx : m_wireTable->selectionModel()->selectedRows()) wireRows.push_back(idx.row());
    std::sort(wireRows.rbegin(), wireRows.rend());
    wireRows.erase(std::unique(wireRows.begin(), wireRows.end()), wireRows.end());

    std::vector<int> feedRows;
    for (const auto &idx : m_feedTable->selectionModel()->selectedRows()) feedRows.push_back(idx.row());
    std::sort(feedRows.rbegin(), feedRows.rend());
    feedRows.erase(std::unique(feedRows.begin(), feedRows.end()), feedRows.end());

    std::vector<int> planeRows;
    for (const auto &idx : m_planeTable->selectionModel()->selectedRows()) planeRows.push_back(idx.row());
    std::sort(planeRows.rbegin(), planeRows.rend());
    planeRows.erase(std::unique(planeRows.begin(), planeRows.end()), planeRows.end());

    std::vector<int> dielectricRows;
    for (const auto &idx : m_dielectricTable->selectionModel()->selectedRows()) dielectricRows.push_back(idx.row());
    std::sort(dielectricRows.rbegin(), dielectricRows.rend());
    dielectricRows.erase(std::unique(dielectricRows.begin(), dielectricRows.end()), dielectricRows.end());

    if (wireRows.empty() && feedRows.empty() && planeRows.empty() && dielectricRows.empty()) return;
    pushGeometryHistory();

    for (int r : wireRows) if (r >= 0 && r < static_cast<int>(m_wires.size())) m_wires.erase(m_wires.begin() + r);
    for (int r : feedRows) if (r >= 0 && r < static_cast<int>(m_feeds.size())) m_feeds.erase(m_feeds.begin() + r);
    for (int r : planeRows) if (r >= 0 && r < static_cast<int>(m_planes.size())) m_planes.erase(m_planes.begin() + r);
    for (int r : dielectricRows) if (r >= 0 && r < static_cast<int>(m_dielectrics.size())) m_dielectrics.erase(m_dielectrics.begin() + r);
    rebuildScene(false);
}

void AntennaDesignerWidget::clearGeometry()
{
    if (m_wires.empty() && m_feeds.empty() && m_planes.empty() && m_dielectrics.empty()) return;
    pushGeometryHistory();
    m_wires.clear();
    m_feeds.clear();
    m_planes.clear();
    m_dielectrics.clear();
    m_constraints.clear();
    rebuildScene(true);
}

bool AntennaDesignerWidget::createFromCalculator(EmCalculator::AntennaKind kind, const EmCalculator::AntennaInputs &inputs, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error) *error = message;
        return false;
    };
    EmCalculator::AntennaResult calculated;
    std::string calculationError;
    if (!EmCalculator::antennaFromFrequency(kind, inputs, calculated, calculationError))
        return fail(QString::fromStdString(calculationError));
    if (!std::isfinite(calculated.frequencyHz) || calculated.frequencyHz <= 0.0)
        return fail(QStringLiteral("Calculator returned an invalid design frequency."));

    pushGeometryHistory();
    m_groups.clear();
    if (m_frequencyMHz) m_frequencyMHz->setValue(calculated.frequencyHz / 1e6);
    const double lambda0 = C0 / calculated.frequencyHz;
    const double lambdaWire = lambda0 * std::clamp(inputs.velocityFactor, 0.01, 1.0);
    const double wireRadius = std::max(1e-6, (m_wireRadiusMm ? m_wireRadiusMm->value() : 1.0) / MmPerM);
    const double driveV = m_feedVoltageV ? m_feedVoltageV->value() : 1.0;
    const double phaseDeg = m_feedPhaseDeg ? m_feedPhaseDeg->value() : 0.0;
    const double sourceOhm = m_feedSourceOhm ? m_feedSourceOhm->value() : 50.0;

    auto reset = [&] {
        m_wires.clear();
        m_feeds.clear();
        m_planes.clear();
        m_dielectrics.clear();
        m_constraints.clear();
        m_groups.clear();
    };
    auto appendWire = [&](const QString &name, double ax, double ay, double az, double bx, double by, double bz) {
        WireElement w;
        w.name = name;
        w.aM = QPointF(ax, ay); w.azM = az;
        w.bM = QPointF(bx, by); w.bzM = bz;
        w.radiusM = wireRadius;
        m_wires.push_back(w);
    };
    auto appendFeed = [&](const QString &name, double x, double y, double z) {
        FeedPoint f;
        f.name = name; f.positionM = QPointF(x, y); f.zM = z;
        f.voltageV = driveV; f.phaseDeg = phaseDeg; f.sourceOhm = sourceOhm;
        m_feeds.push_back(f);
    };

    QString starterNote;
    switch (kind)
    {
    case EmCalculator::AntennaKind::ExactHalfWaveDipole:
        if (m_presetDrivenLambda) m_presetDrivenLambda->setValue(0.50 * inputs.velocityFactor);
        if (m_presetCombo) m_presetCombo->setCurrentIndex(0);
        generateHalfWaveDipole();
        break;
    case EmCalculator::AntennaKind::ResonantDipole0475:
        if (m_presetDrivenLambda) m_presetDrivenLambda->setValue(0.475 * inputs.velocityFactor);
        if (m_presetCombo) m_presetCombo->setCurrentIndex(0);
        generateHalfWaveDipole();
        break;
    case EmCalculator::AntennaKind::QuarterWaveMonopole:
    {
        reset();
        const double l = 0.25 * lambdaWire;
        appendWire(QStringLiteral("MONOPOLE"), 0, 0, 0, 0, 0, l);
        appendFeed(QStringLiteral("F1"), 0, 0, 0);
        if (m_editPlane) m_editPlane->setCurrentIndex(1);
        rebuildScene(true);
        starterNote = QStringLiteral("Quarter-wave wire created without an explicit ground reference. Add/choose the intended ground plane before treating the feed impedance as physical.");
        break;
    }
    case EmCalculator::AntennaKind::GroundPlaneQuarterWave:
    {
        reset();
        const double l = 0.25 * lambdaWire;
        appendWire(QStringLiteral("RADIATOR"), 0, 0, 0, 0, 0, l);
        appendWire(QStringLiteral("RADIAL_XP"), 0, 0, 0, l, 0, 0);
        appendWire(QStringLiteral("RADIAL_XM"), 0, 0, 0, -l, 0, 0);
        appendWire(QStringLiteral("RADIAL_YP"), 0, 0, 0, 0, l, 0);
        appendWire(QStringLiteral("RADIAL_YM"), 0, 0, 0, 0, -l, 0);
        appendFeed(QStringLiteral("F1"), 0, 0, 0);
        rebuildScene(true);
        starterNote = QStringLiteral("Four horizontal quarter-wave radials generated. Radial droop angle and feed-region geometry should be tuned for the required impedance.");
        break;
    }
    case EmCalculator::AntennaKind::InvertedVee:
        if (m_presetDrivenLambda) m_presetDrivenLambda->setValue(0.475 * inputs.velocityFactor);
        if (m_presetCombo) m_presetCombo->setCurrentIndex(1);
        generateVDipole();
        starterNote = QStringLiteral("The current preset opening angle is retained; apex angle and installation height shift resonance and impedance.");
        break;
    case EmCalculator::AntennaKind::FoldedDipole:
        if (m_presetDrivenLambda) m_presetDrivenLambda->setValue(0.475 * inputs.velocityFactor);
        if (m_presetCombo) m_presetCombo->setCurrentIndex(2);
        generateFoldedDipole();
        break;
    case EmCalculator::AntennaKind::FiveEighthMonopole:
    {
        reset();
        const double l = 0.625 * lambdaWire;
        appendWire(QStringLiteral("FIVE_EIGHTH_RADIATOR"), 0, 0, 0, 0, 0, l);
        appendFeed(QStringLiteral("F1"), 0, 0, 0);
        if (m_editPlane) m_editPlane->setCurrentIndex(1);
        rebuildScene(true);
        starterNote = QStringLiteral("5/8-wave radiator created. A suitable ground/reference and matching/loading network are normally required.");
        break;
    }
    case EmCalculator::AntennaKind::FullWaveLoop:
        if (m_presetLoopPerimeterLambda) m_presetLoopPerimeterLambda->setValue(inputs.velocityFactor);
        if (m_presetCombo) m_presetCombo->setCurrentIndex(3);
        generateSquareLoop();
        break;
    case EmCalculator::AntennaKind::Jpole:
    {
        reset();
        const double longL = 0.75 * lambdaWire;
        const double shortL = 0.25 * lambdaWire;
        const double spacing = 0.02 * lambdaWire;
        appendWire(QStringLiteral("J_LONG"), 0, 0, 0, 0, 0, longL);
        appendWire(QStringLiteral("J_BOTTOM"), 0, 0, 0, spacing, 0, 0);
        appendWire(QStringLiteral("J_SHORT"), spacing, 0, 0, spacing, 0, shortL);
        appendFeed(QStringLiteral("F_TAP_START"), 0, 0, 0.04 * lambdaWire);
        if (m_editPlane) m_editPlane->setCurrentIndex(1);
        rebuildScene(true);
        starterNote = QStringLiteral("Canonical 3/4λ + 1/4λ J geometry created with a starter feed tap. The real balanced tap/return, spacing and match must be tuned before interpreting Zin.");
        break;
    }
    case EmCalculator::AntennaKind::SlimJim:
    {
        reset();
        const double longL = 0.75 * lambdaWire;
        const double halfL = 0.50 * lambdaWire;
        const double quarterL = 0.25 * lambdaWire;
        const double spacing = 0.02 * lambdaWire;
        const double gap = 0.008 * lambdaWire;
        appendWire(QStringLiteral("SJ_LONG"), 0, 0, 0, 0, 0, longL);
        appendWire(QStringLiteral("SJ_TOP"), 0, 0, longL, spacing, 0, longL);
        appendWire(QStringLiteral("SJ_UPPER_RETURN"), spacing, 0, longL, spacing, 0, longL - halfL + 0.5 * gap);
        appendWire(QStringLiteral("SJ_BOTTOM"), 0, 0, 0, spacing, 0, 0);
        appendWire(QStringLiteral("SJ_MATCH_STUB"), spacing, 0, 0, spacing, 0, quarterL - 0.5 * gap);
        appendFeed(QStringLiteral("F_TAP_START"), 0, 0, 0.04 * lambdaWire);
        if (m_editPlane) m_editPlane->setCurrentIndex(1);
        rebuildScene(true);
        starterNote = QStringLiteral("Slim-Jim starter with a small right-side gap created. Feed tap, line spacing and gap are tuning variables; the single-point starter feed is not a complete balanced feed model.");
        break;
    }
    case EmCalculator::AntennaKind::AxialModeHelix:
        if (m_presetHelixRadiusLambda) m_presetHelixRadiusLambda->setValue(inputs.velocityFactor / (2.0 * NumericalEM::Pi));
        if (m_presetHelixPitchLambda) m_presetHelixPitchLambda->setValue(0.23 * inputs.velocityFactor);
        if (m_presetCombo) m_presetCombo->setCurrentIndex(5);
        generateHelicalAntenna();
        starterNote = QStringLiteral("Helix circumference and pitch come from the calculator; the existing Designer turn count is retained.");
        break;
    case EmCalculator::AntennaKind::Yagi3Element:
        if (m_presetDrivenLambda) m_presetDrivenLambda->setValue(0.475 * inputs.velocityFactor);
        if (m_presetYagiReflectorLambda) m_presetYagiReflectorLambda->setValue(0.50 * inputs.velocityFactor);
        if (m_presetYagiDirectorLambda) m_presetYagiDirectorLambda->setValue(0.45 * inputs.velocityFactor);
        if (m_presetYagiReflectorSpacingLambda) m_presetYagiReflectorSpacingLambda->setValue(0.20 * inputs.velocityFactor);
        if (m_presetYagiDirectorSpacingLambda) m_presetYagiDirectorSpacingLambda->setValue(0.15 * inputs.velocityFactor);
        if (m_presetYagiDirectorCount) m_presetYagiDirectorCount->setValue(1);
        if (m_presetCombo) m_presetCombo->setCurrentIndex(4);
        generateYagiArray();
        starterNote = QStringLiteral("Three-element Yagi start created. Use the Yagi optimizers / convergence checks for final element lengths and spacing.");
        break;
    case EmCalculator::AntennaKind::RectangularPatch:
        if (m_presetPatchEr) m_presetPatchEr->setValue(inputs.relativePermittivity);
        if (m_presetPatchHeightMm) m_presetPatchHeightMm->setValue(inputs.substrateHeightM * MmPerM);
        if (m_presetCombo) m_presetCombo->setCurrentIndex(7);
        generateInsetMicrostripPatch();
        starterNote = QStringLiteral("Inset-fed patch generated using the same frequency/substrate inputs. Feed inset and line geometry remain matching variables.");
        break;
    case EmCalculator::AntennaKind::ParabolicDish:
    {
        reset();
        PlaneElement dish;
        dish.name = QStringLiteral("PARABOLIC_REFLECTOR");
        dish.surfaceType = 4;
        dish.orientation = 0;
        dish.centerM = QPointF(0.0, 0.0);
        dish.zM = 0.0;
        dish.radiusM = 0.5 * inputs.dishDiameterM;
        dish.focalLengthM = inputs.focalRatio * inputs.dishDiameterM;
        dish.meshHintM = std::max(1e-4, std::min(lambda0 / 12.0, inputs.dishDiameterM / 24.0));
        m_planes.push_back(dish);
        rebuildScene(true);
        starterNote = QStringLiteral("Parabolic PEC surface generated. No feed is inserted automatically: place/model the actual horn/dipole/feed near the focus z=f before a coupled simulation.");
        break;
    }
    }

    if (m_sweepStartMHz && m_sweepStopMHz)
    {
        const double fMHz = calculated.frequencyHz / 1e6;
        m_sweepStartMHz->setValue(0.70 * fMHz);
        m_sweepStopMHz->setValue(1.30 * fMHz);
        if (m_optTargetMHz) m_optTargetMHz->setValue(fMHz);
        if (m_multiTargetMHz) m_multiTargetMHz->setValue(fMHz);
    }
    if (m_workspaceTabs) m_workspaceTabs->setCurrentIndex(0);
    if (!starterNote.isEmpty() && m_momStatus) m_momStatus->setText(QStringLiteral("Calculator geometry: %1").arg(starterNote));
    if (error) error->clear();
    return true;
}

void AntennaDesignerWidget::generateHalfWaveDipole()
{
    m_wires.clear();
    m_feeds.clear();
    m_planes.clear();
    m_dielectrics.clear();
    m_constraints.clear();
    const double lambda = C0 / std::max(1.0, m_frequencyMHz->value() * 1e6);
    const double totalLength = (m_presetDrivenLambda ? m_presetDrivenLambda->value() : 0.475) * lambda;
    WireElement left{QStringLiteral("W1"), QPointF(-totalLength/2.0, 0.0), QPointF(0.0, 0.0), m_wireRadiusMm->value()/MmPerM};
    WireElement right{QStringLiteral("W2"), QPointF(0.0, 0.0), QPointF(totalLength/2.0, 0.0), m_wireRadiusMm->value()/MmPerM};
    m_wires = {left, right};
    FeedPoint f{QStringLiteral("F1"), QPointF(0.0,0.0), m_feedVoltageV->value(), m_feedPhaseDeg->value(), m_feedSourceOhm->value()};
    m_feeds.push_back(f);
    rebuildScene(true);
}

void AntennaDesignerWidget::generateVDipole()
{
    m_wires.clear();
    m_feeds.clear();
    m_planes.clear();
    m_dielectrics.clear();
    m_constraints.clear();
    const double lambda = C0 / std::max(1.0, m_frequencyMHz->value() * 1e6);
    const double totalLength = (m_presetDrivenLambda ? m_presetDrivenLambda->value() : 0.475) * lambda;
    const double arm = 0.5 * totalLength;
    const double openingDeg = m_presetVOpeningDeg ? m_presetVOpeningDeg->value() : 120.0;
    const double halfOpening = 0.5 * (180.0 - openingDeg) * NumericalEM::Pi / 180.0;
    const QPointF left(-arm * std::cos(halfOpening), arm * std::sin(halfOpening));
    const QPointF right(arm * std::cos(halfOpening), arm * std::sin(halfOpening));
    const double a = m_wireRadiusMm->value() / MmPerM;
    m_wires = {WireElement{QStringLiteral("W1"), left, QPointF(0,0), a},
               WireElement{QStringLiteral("W2"), QPointF(0,0), right, a}};
    m_feeds.push_back(FeedPoint{QStringLiteral("F1"), QPointF(0,0), m_feedVoltageV->value(), m_feedPhaseDeg->value(), m_feedSourceOhm->value()});
    rebuildScene(true);
}

void AntennaDesignerWidget::generateFoldedDipole()
{
    m_wires.clear();
    m_feeds.clear();
    m_planes.clear();
    m_dielectrics.clear();
    m_constraints.clear();
    const double lambda = C0 / std::max(1.0, m_frequencyMHz->value() * 1e6);
    const double L = (m_presetDrivenLambda ? m_presetDrivenLambda->value() : 0.475) * lambda;
    const double sep = (m_presetFoldedSpacingLambda ? m_presetFoldedSpacingLambda->value() : 0.02) * lambda;
    const double y0 = -0.5 * sep, y1 = 0.5 * sep, x0 = -0.5 * L, x1 = 0.5 * L;
    const double a = m_wireRadiusMm->value() / MmPerM;
    m_wires = {WireElement{QStringLiteral("W1"), QPointF(x0,y0), QPointF(0,y0), a},
               WireElement{QStringLiteral("W2"), QPointF(0,y0), QPointF(x1,y0), a},
               WireElement{QStringLiteral("W3"), QPointF(x1,y0), QPointF(x1,y1), a},
               WireElement{QStringLiteral("W4"), QPointF(x1,y1), QPointF(x0,y1), a},
               WireElement{QStringLiteral("W5"), QPointF(x0,y1), QPointF(x0,y0), a}};
    m_feeds.push_back(FeedPoint{QStringLiteral("F1"), QPointF(0,y0), m_feedVoltageV->value(), m_feedPhaseDeg->value(), m_feedSourceOhm->value()});
    rebuildScene(true);
}

void AntennaDesignerWidget::generateSquareLoop()
{
    m_wires.clear();
    m_feeds.clear();
    m_planes.clear();
    m_dielectrics.clear();
    m_constraints.clear();
    const double lambda = C0 / std::max(1.0, m_frequencyMHz->value() * 1e6);
    const double side = 0.25 * (m_presetLoopPerimeterLambda ? m_presetLoopPerimeterLambda->value() : 1.0) * lambda;
    const double h = 0.5 * side;
    const double a = m_wireRadiusMm->value() / MmPerM;
    const QPointF feedPoint(0.0, -h);
    m_wires = {WireElement{QStringLiteral("W1"), feedPoint, QPointF(h,-h), a},
               WireElement{QStringLiteral("W2"), QPointF(h,-h), QPointF(h,h), a},
               WireElement{QStringLiteral("W3"), QPointF(h,h), QPointF(-h,h), a},
               WireElement{QStringLiteral("W4"), QPointF(-h,h), QPointF(-h,-h), a},
               WireElement{QStringLiteral("W5"), QPointF(-h,-h), feedPoint, a}};
    m_feeds.push_back(FeedPoint{QStringLiteral("F1"), feedPoint, m_feedVoltageV->value(), m_feedPhaseDeg->value(), m_feedSourceOhm->value()});
    rebuildScene(true);
}

void AntennaDesignerWidget::generateYagiArray()
{
    m_wires.clear();
    m_feeds.clear();
    m_planes.clear();
    m_dielectrics.clear();
    m_constraints.clear();
    const double lambda = C0 / std::max(1.0, m_frequencyMHz->value() * 1e6);
    const double reflectorL = (m_presetYagiReflectorLambda ? m_presetYagiReflectorLambda->value() : 0.50) * lambda;
    const double drivenL = (m_presetDrivenLambda ? m_presetDrivenLambda->value() : 0.475) * lambda;
    const double firstDirectorLambda = m_presetYagiDirectorLambda ? m_presetYagiDirectorLambda->value() : 0.44;
    const double reflectorX = -(m_presetYagiReflectorSpacingLambda ? m_presetYagiReflectorSpacingLambda->value() : 0.20) * lambda;
    const double firstDirectorSpacingLambda = m_presetYagiDirectorSpacingLambda ? m_presetYagiDirectorSpacingLambda->value() : 0.15;
    const double directorPitchLambda = m_presetYagiDirectorPitchLambda ? m_presetYagiDirectorPitchLambda->value() : firstDirectorSpacingLambda;
    const double directorTaperLambda = m_presetYagiDirectorTaperLambda ? m_presetYagiDirectorTaperLambda->value() : 0.008;
    const int directorCount = std::max(1, m_presetYagiDirectorCount ? m_presetYagiDirectorCount->value() : 1);
    const double a = m_wireRadiusMm->value() / MmPerM;

    m_wires.push_back(WireElement{QStringLiteral("REF"), QPointF(reflectorX,-reflectorL/2), QPointF(reflectorX,reflectorL/2), a});
    m_wires.push_back(WireElement{QStringLiteral("DRV1"), QPointF(0,-drivenL/2), QPointF(0,0), a});
    m_wires.push_back(WireElement{QStringLiteral("DRV2"), QPointF(0,0), QPointF(0,drivenL/2), a});

    for (int i = 0; i < directorCount; ++i)
    {
        const double xLambda = firstDirectorSpacingLambda + static_cast<double>(i) * directorPitchLambda;
        const double lengthLambda = std::max(0.05, firstDirectorLambda - static_cast<double>(i) * directorTaperLambda);
        const double x = xLambda * lambda;
        const double length = lengthLambda * lambda;
        m_wires.push_back(WireElement{QStringLiteral("DIR%1").arg(i + 1), QPointF(x,-length/2), QPointF(x,length/2), a});
    }

    m_feeds.push_back(FeedPoint{QStringLiteral("F1"), QPointF(0,0), m_feedVoltageV->value(), m_feedPhaseDeg->value(), m_feedSourceOhm->value()});
    rebuildScene(true);
}

void AntennaDesignerWidget::generateHelicalAntenna()
{
    m_wires.clear();
    m_feeds.clear();
    m_planes.clear();
    m_dielectrics.clear();
    m_constraints.clear();
    const double lambda=C0/std::max(1.0,m_frequencyMHz->value()*1e6);
    const double radius=(m_presetHelixRadiusLambda?m_presetHelixRadiusLambda->value():0.16)*lambda;
    const double pitch=(m_presetHelixPitchLambda?m_presetHelixPitchLambda->value():0.22)*lambda;
    const double turns=m_presetHelixTurns?m_presetHelixTurns->value():5.0;
    const int segments=std::clamp(static_cast<int>(std::ceil(turns*24.0)),24,720);
    const double wireRadius=m_wireRadiusMm->value()/MmPerM;
    auto point=[&](int i){const double t=turns*2.0*NumericalEM::Pi*double(i)/double(segments);const double z=pitch*t/(2.0*NumericalEM::Pi);return std::array<double,3>{radius*std::cos(t),radius*std::sin(t),z};};
    for(int i=0;i<segments;++i)
    {
        const auto a=point(i),b=point(i+1);WireElement w;w.name=QStringLiteral("H%1").arg(i+1);w.aM=QPointF(a[0],a[1]);w.azM=a[2];w.bM=QPointF(b[0],b[1]);w.bzM=b[2];w.radiusM=wireRadius;m_wires.push_back(w);
    }
    const auto p0=point(0);FeedPoint f;f.name=QStringLiteral("F1");f.positionM=QPointF(p0[0],p0[1]);f.zM=p0[2];f.voltageV=m_feedVoltageV->value();f.phaseDeg=m_feedPhaseDeg->value();f.sourceOhm=m_feedSourceOhm->value();m_feeds.push_back(f);
    if(m_editPlane)m_editPlane->setCurrentIndex(1); // XZ is often the clearest orthographic start view.
    if(m_activePlaneCoordinateM)m_activePlaneCoordinateM->setValue(0.0);
    rebuildScene(true);
}

void AntennaDesignerWidget::generateProbeFedPatch()
{
    m_wires.clear();
    m_feeds.clear();
    m_planes.clear();
    m_dielectrics.clear();
    m_constraints.clear();

    const double fHz=std::max(1.0,m_frequencyMHz->value()*1e6);
    const double lambda=C0/fHz;
    const double er=4.2;
    const double h=std::min(0.02*lambda,0.0016); // FR-4-like starter, deliberately editable afterwards.
    const double W=0.5*lambda*std::sqrt(2.0/(er+1.0));
    const double epsEff=0.5*(er+1.0)+0.5*(er-1.0)/std::sqrt(1.0+12.0*h/std::max(W,1e-12));
    const double wh=W/std::max(h,1e-12);
    const double dL=h*0.412*((epsEff+0.3)*(wh+0.264))/std::max(1e-12,(epsEff-0.258)*(wh+0.8));
    const double Leff=0.5*lambda/std::sqrt(std::max(epsEff,1.0));
    const double L=std::max(0.05*lambda,Leff-2.0*dL);
    const double groundW=std::max(1.35*W,W+12.0*h);
    const double groundL=std::max(1.35*L,L+12.0*h);
    const double feedX=0.0;
    const double feedY=-0.22*L;
    const double meshGround=std::max(1e-4,std::min(lambda/20.0,std::min(groundW,groundL)/10.0));
    const double meshPatch=std::max(1e-4,std::min(lambda/30.0,std::min(W,L)/12.0));

    PlaneElement ground; ground.name=QStringLiteral("GND"); ground.surfaceType=0; ground.centerM=QPointF(0,0); ground.zM=0.0;
    ground.widthM=groundW; ground.heightM=groundL; ground.meshHintM=meshGround;
    PlaneElement patch; patch.name=QStringLiteral("PATCH"); patch.surfaceType=0; patch.centerM=QPointF(0,0); patch.zM=h;
    patch.widthM=W; patch.heightM=L; patch.meshHintM=meshPatch;
    m_planes={ground,patch};

    DielectricElement sub; sub.name=QStringLiteral("SUB_FR4_STARTER"); sub.centerM=QPointF(0,0); sub.zM=0.5*h;
    sub.widthM=groundW; sub.heightM=groundL; sub.thicknessM=h; sub.relativePermittivity=er; sub.lossTangent=0.02;
    // Match the localized effective-medium kernel to the same Hammerstad εeff used to
    // size the starter patch. This is substantially more physical for a wide patch over
    // a thin substrate than the generic 0.65 open-microstrip seed.
    sub.fieldFillFactor=er>1.0?std::clamp((epsEff-1.0)/(er-1.0),0.0,1.0):0.0;
    m_dielectrics.push_back(sub);

    WireElement probe; probe.name=QStringLiteral("PROBE"); probe.aM=QPointF(feedX,feedY); probe.azM=0.0; probe.bM=QPointF(feedX,feedY); probe.bzM=h;
    probe.radiusM=std::max(1e-5,std::min(0.00040,0.05*h)); m_wires.push_back(probe);

    FeedPoint port; port.name=QStringLiteral("FPORT_GND"); port.positionM=QPointF(feedX,feedY); port.zM=0.0;
    port.voltageV=m_feedVoltageV->value(); port.phaseDeg=m_feedPhaseDeg->value(); port.sourceOhm=m_feedSourceOhm->value();
    FeedPoint junction=port; junction.name=QStringLiteral("FJ_PATCH"); junction.zM=h; junction.voltageV=0.0;
    m_feeds={port,junction};

    if(m_editPlane)m_editPlane->setCurrentIndex(1); // XZ shows the probe/substrate stack immediately.
    if(m_activePlaneCoordinateM)m_activePlaneCoordinateM->setValue(feedY);
    rebuildScene(true);

    if(m_hybridPortMode)m_hybridPortMode->setCurrentIndex(2);
    if(m_hybridPortFeed){const int i=m_hybridPortFeed->findText(port.name);if(i>=0)m_hybridPortFeed->setCurrentIndex(i);}
    if(m_hybridJunctionFeed){const int i=m_hybridJunctionFeed->findText(junction.name);if(i>=0)m_hybridJunctionFeed->setCurrentIndex(i);}
    auto selectSurface=[&](QComboBox*combo,int surfaceIndex){if(!combo)return;for(int i=0;i<combo->count();++i)if(combo->itemData(i).toInt()==surfaceIndex){combo->setCurrentIndex(i);return;}};
    selectSurface(m_hybridPortSurface,0);
    selectSurface(m_hybridJunctionSurface,1);
    if(m_hybridUseDielectric)m_hybridUseDielectric->setChecked(true);
    if(m_hybridDielectricKernel)m_hybridDielectricKernel->setCurrentIndex(14);
    if(m_hybridMappingToleranceMm)m_hybridMappingToleranceMm->setValue(std::max(10.0,750.0*std::max(meshGround,meshPatch)));
    if(m_hybridMomStatus)m_hybridMomStatus->setText(QStringLiteral("Patch starter generated. Differential PEC surface lumped port and the 5.39 grounded-PEC modal audit are preselected; the vertical probe remains as a geometry/legacy reference and is excluded from that port solve. Same-face RWG terms keep the 5.29 TE vector correction, opposite slab faces retain the 5.30 residual transmitted TE/TM tangential dyadic, and the scalar block uses the coupled HED TE/TM longitudinal reflection/transmission spectrum and exterior wire↔RWG mutual coupling receives the height-propagated TE/TM vector residual. The reciprocal blocks are symmetrized before solution. Tangential internal/exterior wire↔RWG vector and scalar-gradient residuals remain available; predominantly normal internal via/probe currents use the TM VED vector/scalar-gradient transition and 5.35 adds the residual mixed rho-z / z-rho vector terms. Full-complex layered-radiation increments remain guarded until they pass the power checks. Mesh and dielectric-kernel convergence remain mandatory."));
}

void AntennaDesignerWidget::generateInsetMicrostripPatch()
{
    m_wires.clear();
    m_feeds.clear();
    m_planes.clear();
    m_dielectrics.clear();
    m_constraints.clear();

    const double fHz=std::max(1.0,m_frequencyMHz->value()*1e6);
    const double lambda=C0/fHz;
    const double er=m_presetPatchEr?m_presetPatchEr->value():4.2;
    const double h=std::max(1e-6,(m_presetPatchHeightMm?m_presetPatchHeightMm->value():1.6)*1e-3);
    const double tanD=std::max(0.0,m_presetPatchTanD?m_presetPatchTanD->value():0.02);
    const double targetZ=std::max(1.0,m_presetPatchLineZ0?m_presetPatchLineZ0->value():50.0);
    const auto patchEstimate=MicrostripModels::rectangularPatch(fHz,h,er);
    if(!patchEstimate.valid) return;
    const double W=patchEstimate.widthM,L=patchEstimate.physicalLengthM;
    const double lineW=std::clamp(MicrostripModels::widthForImpedance(targetZ,h,er),0.02*W,0.45*W);
    const auto lineInfo=MicrostripModels::analyzeLine(fHz,lineW,h,er,tanD);
    const auto insetInfo=MicrostripModels::insetForResistance(L,m_presetPatchEdgeResistance?m_presetPatchEdgeResistance->value():300.0,targetZ);
    const double inset=insetInfo.valid?insetInfo.insetDepthM:0.0;
    const double gap=std::max(0.0,(m_presetPatchNotchGapMm?m_presetPatchNotchGapMm->value():0.40)*1e-3);
    const double lineOutside=std::max({6.0*h,0.12*lambda,lineInfo.valid?0.12*lineInfo.guidedWavelengthM:0.0});
    const double margin=std::max(6.0*h,0.08*lambda);
    const double boardW=std::max(W+2.0*margin,1.30*W);
    const double boardBottom=-0.5*L-lineOutside-margin;
    const double boardTop=0.5*L+margin;
    const double boardL=boardTop-boardBottom;
    const double boardCy=0.5*(boardTop+boardBottom);
    const double meshGround=std::max(1e-4,std::min(lambda/18.0,std::min(boardW,boardL)/10.0));
    const double meshTop=std::max(5e-5,std::min({lambda/35.0,std::min(W,L)/12.0,std::max(lineW,1e-4)}));

    PlaneElement ground;ground.name=QStringLiteral("GND");ground.surfaceType=0;ground.centerM=QPointF(0,boardCy);ground.zM=0.0;
    ground.widthM=boardW;ground.heightM=boardL;ground.meshHintM=meshGround;
    PlaneElement top;top.name=QStringLiteral("PATCH_INSET_FEED");top.surfaceType=5;top.centerM=QPointF(0,0);top.zM=h;
    top.widthM=W;top.heightM=L;top.feedWidthM=lineW;top.feedLengthM=lineOutside;top.insetDepthM=inset;top.notchGapM=gap;top.meshHintM=meshTop;
    m_planes={ground,top};

    DielectricElement sub;sub.name=QStringLiteral("SUBSTRATE");sub.centerM=QPointF(0,boardCy);sub.zM=0.5*h;
    sub.widthM=boardW;sub.heightM=boardL;sub.thicknessM=h;sub.relativePermittivity=er;sub.lossTangent=tanD;
    // For the localized kernel approximation, seed fill from the quasi-TEM line epsilon_eff.
    const double ee=lineInfo.valid?lineInfo.effectivePermittivity:MicrostripModels::effectivePermittivity(lineW,h,er);
    sub.fieldFillFactor=er>1.0?std::clamp((ee-1.0)/(er-1.0),0.0,1.0):0.0;
    m_dielectrics.push_back(sub);

    const double feedStartY=-0.5*L-lineOutside;
    const double probeY=feedStartY+std::min(0.20*lineOutside,std::max(1.5*h,0.5*meshTop));
    WireElement probe;probe.name=QStringLiteral("MICROSTRIP_TRANSITION");probe.aM=QPointF(0,probeY);probe.azM=0.0;probe.bM=QPointF(0,probeY);probe.bzM=h;
    probe.radiusM=std::max(1e-5,std::min(0.0005,0.08*h));m_wires.push_back(probe);

    FeedPoint port;port.name=QStringLiteral("FPORT_GND");port.positionM=QPointF(0,probeY);port.zM=0.0;
    port.voltageV=m_feedVoltageV->value();port.phaseDeg=m_feedPhaseDeg->value();port.sourceOhm=m_feedSourceOhm->value();
    FeedPoint junction=port;junction.name=QStringLiteral("FJ_MICROSTRIP");junction.zM=h;junction.voltageV=0.0;
    m_feeds={port,junction};

    if(m_editPlane)m_editPlane->setCurrentIndex(1);
    if(m_activePlaneCoordinateM)m_activePlaneCoordinateM->setValue(probeY);
    rebuildScene(true);
    if(m_hybridPortMode)m_hybridPortMode->setCurrentIndex(2);
    if(m_hybridPortFeed){const int i=m_hybridPortFeed->findText(port.name);if(i>=0)m_hybridPortFeed->setCurrentIndex(i);}
    if(m_hybridJunctionFeed){const int i=m_hybridJunctionFeed->findText(junction.name);if(i>=0)m_hybridJunctionFeed->setCurrentIndex(i);}
    auto selectSurface=[&](QComboBox*combo,int surfaceIndex){if(!combo)return;for(int i=0;i<combo->count();++i)if(combo->itemData(i).toInt()==surfaceIndex){combo->setCurrentIndex(i);return;}};
    selectSurface(m_hybridPortSurface,0);selectSurface(m_hybridJunctionSurface,1);
    if(m_hybridUseDielectric)m_hybridUseDielectric->setChecked(true);
    if(m_hybridDielectricKernel)m_hybridDielectricKernel->setCurrentIndex(14);
    if(m_hybridMappingToleranceMm)m_hybridMappingToleranceMm->setValue(std::max(2.0,900.0*meshTop));
    if(m_hybridMomStatus)m_hybridMomStatus->setText(QStringLiteral("Inset-fed microstrip patch generated. Differential PEC surface lumped port and the 5.39 grounded-PEC modal audit are preselected; the top conductor remains one continuous RWG mesh. The vertical transition is retained as a geometry/legacy reference and excluded from the differential-port solve. Opposite slab faces retain the residual transmitted TE/TM tangential vector correction, the scalar block uses the coupled HED TE/TM longitudinal reflection/transmission spectrum, exterior wire↔RWG pairs use the height-propagated guarded vector residual, and reciprocal symmetrization remains active. Tangential internal-layer sources retain the guarded 5.33 cavity residual and scalar-gradient correction; predominantly normal internal vias/probes now use the 5.34 TM VED scalar-gradient transition with reciprocal HED full-gradient coupling, while 5.35 activates the residual mixed rho-z / z-rho TM vector terms. Full-complex radiative MPIE completion remains a later stage."));
}

void AntennaDesignerWidget::buildPrimitive()
{
    if (!m_primitiveType || !m_primitiveOrientation) return;
    pushGeometryHistory();
    if (m_primitiveClearFirst && m_primitiveClearFirst->isChecked())
    {
        m_wires.clear();
        m_feeds.clear();
        m_planes.clear();
    m_dielectrics.clear();
    m_constraints.clear();
    }

    const int type = m_primitiveType->currentIndex();
    const int orientation = m_primitiveOrientation->currentIndex();
    const QVector3D center(m_primitiveOriginX ? m_primitiveOriginX->value() : 0.0,
                           m_primitiveOriginY ? m_primitiveOriginY->value() : 0.0,
                           m_primitiveOriginZ ? m_primitiveOriginZ->value() : 0.0);
    const double yawDeg = m_primitiveYawDeg ? m_primitiveYawDeg->value() : 0.0;
    const double pitchDeg = m_primitivePitchDeg ? m_primitivePitchDeg->value() : 0.0;
    const double rollDeg = m_primitiveRollDeg ? m_primitiveRollDeg->value() : 0.0;
    const double wireRadius = m_wireRadiusMm ? m_wireRadiusMm->value()/MmPerM : 0.001;
    const int baseWire = static_cast<int>(m_wires.size());
    std::vector<QVector3D> generatedNodes;

    auto world = [&](double u, double v, double normal = 0.0) {
        return orientedPointEuler(center.x(), center.y(), center.z(), orientation, yawDeg, pitchDeg, rollDeg, u, v, normal);
    };
    auto appendWire = [&](const QVector3D &a, const QVector3D &b, const QString &prefix) {
        if ((a-b).length() < 1e-9f) return;
        WireElement w;
        w.name = QStringLiteral("%1%2").arg(prefix).arg(static_cast<int>(m_wires.size()) - baseWire + 1);
        w.aM = QPointF(a.x(), a.y()); w.azM = a.z();
        w.bM = QPointF(b.x(), b.y()); w.bzM = b.z();
        w.radiusM = wireRadius;
        m_wires.push_back(w);
    };
    auto appendPath = [&](const std::vector<QVector3D> &points, const QString &prefix, bool closed) {
        if (points.size() < 2) return;
        generatedNodes = points;
        for (std::size_t i=0;i+1<points.size();++i) appendWire(points[i],points[i+1],prefix);
        if (closed && (points.front()-points.back()).length()>1e-8f) appendWire(points.back(),points.front(),prefix);
    };

    const double radius = std::max(1e-6, m_primitiveRadiusM ? m_primitiveRadiusM->value() : 0.20);
    const double innerRadius = std::clamp(m_primitiveInnerRadiusM ? m_primitiveInnerRadiusM->value() : 0.03, 0.0, radius);
    const double width = std::max(1e-6, m_primitiveWidthM ? m_primitiveWidthM->value() : 0.50);
    const double height = std::max(1e-6, m_primitiveHeightM ? m_primitiveHeightM->value() : 0.30);
    const double focalLength = std::max(1e-6, m_primitiveFocalLengthM ? m_primitiveFocalLengthM->value() : 0.25);
    const double pitch = std::max(0.0, m_primitivePitchM ? m_primitivePitchM->value() : 0.08);
    const double turns = std::max(0.05, m_primitiveTurns ? m_primitiveTurns->value() : 4.0);
    const double startRad = (m_primitiveStartDeg ? m_primitiveStartDeg->value() : 0.0) * NumericalEM::Pi / 180.0;
    const double sweepRad = (m_primitiveSweepDeg ? m_primitiveSweepDeg->value() : 360.0) * NumericalEM::Pi / 180.0;
    const int curveSegments = std::max(4, m_primitiveSegments ? m_primitiveSegments->value() : 32);

    bool generatedWireGeometry = false;
    bool closedPath = false;
    switch (type)
    {
        case 0: // circular loop
        {
            const int n=curveSegments;
            std::vector<QVector3D> pts; pts.reserve(static_cast<std::size_t>(n+1));
            for(int i=0;i<=n;++i){const double t=startRad+2.0*NumericalEM::Pi*double(i)/double(n);pts.push_back(world(radius*std::cos(t),radius*std::sin(t)));}
            appendPath(pts,QStringLiteral("LOOP"),false); closedPath=true; generatedWireGeometry=true; break;
        }
        case 1: // circular arc
        {
            const int n=curveSegments;
            std::vector<QVector3D> pts; pts.reserve(static_cast<std::size_t>(n+1));
            for(int i=0;i<=n;++i){const double t=startRad+sweepRad*double(i)/double(n);pts.push_back(world(radius*std::cos(t),radius*std::sin(t)));}
            appendPath(pts,QStringLiteral("ARC"),false); generatedWireGeometry=true; break;
        }
        case 2: // rectangular loop
        {
            const double hx=0.5*width, hy=0.5*height;
            std::vector<QVector3D> pts={world(0,-hy),world(hx,-hy),world(hx,hy),world(-hx,hy),world(-hx,-hy),world(0,-hy)};
            appendPath(pts,QStringLiteral("RECT"),false); closedPath=true; generatedWireGeometry=true; break;
        }
        case 3: // helix / solenoid wire
        {
            const int n=std::clamp(static_cast<int>(std::ceil(turns*curveSegments)),8,1440);
            std::vector<QVector3D> pts; pts.reserve(static_cast<std::size_t>(n+1));
            for(int i=0;i<=n;++i)
            {
                const double frac=double(i)/double(n),t=startRad+turns*2.0*NumericalEM::Pi*frac;
                const double normal=pitch*(turns*frac-0.5*turns);
                pts.push_back(world(radius*std::cos(t),radius*std::sin(t),normal));
            }
            appendPath(pts,QStringLiteral("HELIX"),false); generatedWireGeometry=true; break;
        }
        case 4: // planar Archimedean spiral
        {
            const int n=std::clamp(static_cast<int>(std::ceil(turns*curveSegments)),8,1440);
            std::vector<QVector3D> pts; pts.reserve(static_cast<std::size_t>(n+1));
            for(int i=0;i<=n;++i)
            {
                const double frac=double(i)/double(n),t=startRad+turns*2.0*NumericalEM::Pi*frac;
                const double r=innerRadius+(radius-innerRadius)*frac;
                pts.push_back(world(r*std::cos(t),r*std::sin(t)));
            }
            appendPath(pts,QStringLiteral("SPIRAL"),false); generatedWireGeometry=true; break;
        }
        case 5: // meander
        {
            const int folds=std::clamp(static_cast<int>(std::lround(turns)),2,80);
            std::vector<QVector3D> pts;
            const double y0=-0.5*height,dy=height/double(folds);
            double x=-0.5*width;
            pts.push_back(world(x,y0));
            for(int row=0;row<=folds;++row)
            {
                const double y=y0+dy*row;
                if(row>0) pts.push_back(world(x,y));
                x = (x<0.0 ? 0.5*width : -0.5*width);
                pts.push_back(world(x,y));
            }
            appendPath(pts,QStringLiteral("MEANDER"),false); generatedWireGeometry=true; break;
        }
        case 6: // rectangular sheet
        case 7: // disk / annulus
        case 8: // cylindrical shell
        case 9: // conical shell
        case 10: // parabolic reflector
        {
            PlaneElement pl;
            pl.surfaceType=type-6;
            const QString prefix = pl.surfaceType==0 ? QStringLiteral("SHEET") : pl.surfaceType==1 ? QStringLiteral("DISK") : pl.surfaceType==2 ? QStringLiteral("CYL") : pl.surfaceType==3 ? QStringLiteral("CONE") : QStringLiteral("DISH");
            pl.name=QStringLiteral("%1%2").arg(prefix).arg(m_planes.size()+1);
            pl.centerM=QPointF(center.x(),center.y());pl.zM=center.z();pl.orientation=orientation;
            pl.yawDeg=yawDeg;pl.pitchDeg=pitchDeg;pl.rollDeg=rollDeg;
            pl.radiusM=radius;pl.innerRadiusM=innerRadius;pl.widthM=width;pl.heightM=height;pl.focalLengthM=focalLength;
            pl.meshHintM=std::max(1e-6,m_primitiveGridSpacingM?m_primitiveGridSpacingM->value():0.05);
            m_planes.push_back(pl);
            break;
        }
        case 11: // finite dielectric substrate / slab
        {
            DielectricElement d;d.name=QStringLiteral("SUB%1").arg(m_dielectrics.size()+1);d.centerM=QPointF(center.x(),center.y());d.zM=center.z();d.orientation=orientation;d.yawDeg=yawDeg;d.pitchDeg=pitchDeg;d.rollDeg=rollDeg;d.widthM=width;d.heightM=height;d.thicknessM=std::max(1e-9,m_primitiveThicknessM?m_primitiveThicknessM->value():0.0016);d.relativePermittivity=std::max(1.0,m_primitiveDielectricEr?m_primitiveDielectricEr->value():4.2);d.lossTangent=std::max(0.0,m_primitiveDielectricTanD?m_primitiveDielectricTanD->value():0.02);d.fieldFillFactor=std::clamp(m_primitiveDielectricFill?m_primitiveDielectricFill->value():0.65,0.0,1.0);m_dielectrics.push_back(d);break;
        }
        default: break;
    }

    if (generatedWireGeometry && m_primitiveAddFeed && m_primitiveAddFeed->isChecked() && !generatedNodes.empty())
    {
        const std::size_t index = closedPath ? 0 : generatedNodes.size()/2;
        const QVector3D q=generatedNodes[std::min(index,generatedNodes.size()-1)];
        FeedPoint f;
        f.name=QStringLiteral("F%1").arg(m_feeds.size()+1);f.positionM=QPointF(q.x(),q.y());f.zM=q.z();
        f.voltageV=m_feedVoltageV?m_feedVoltageV->value():1.0;f.phaseDeg=m_feedPhaseDeg?m_feedPhaseDeg->value():0.0;f.sourceOhm=m_feedSourceOhm?m_feedSourceOhm->value():50.0;
        m_feeds.push_back(f);
    }
    rebuildScene(true);
}

void AntennaDesignerWidget::generateQuarterWaveGroundPlane()
{
    m_wires.clear();
    m_feeds.clear();
    m_planes.clear();
    m_dielectrics.clear();
    m_constraints.clear();

    const double fHz=std::max(1.0,m_frequencyMHz->value()*1e6);
    const double lambda=C0/fHz;
    const double monopoleL=0.24*lambda;
    const double groundR=0.30*lambda;
    const double wireRadius=std::max(1e-6,m_wireRadiusMm->value()/MmPerM);

    PlaneElement ground;
    ground.name=QStringLiteral("GND_DISK");
    ground.surfaceType=1; // disk
    ground.centerM=QPointF(0,0); ground.zM=0.0;
    ground.radiusM=groundR;
    ground.meshHintM=std::max(1e-4,std::min(lambda/22.0,groundR/9.0));
    m_planes.push_back(ground);

    WireElement radiator;
    radiator.name=QStringLiteral("MONOPOLE");
    radiator.aM=QPointF(0,0); radiator.azM=0.0;
    radiator.bM=QPointF(0,0); radiator.bzM=monopoleL;
    radiator.radiusM=wireRadius;
    m_wires.push_back(radiator);

    FeedPoint port;
    port.name=QStringLiteral("F_GND"); port.positionM=QPointF(0,0); port.zM=0.0;
    port.voltageV=m_feedVoltageV->value(); port.phaseDeg=m_feedPhaseDeg->value(); port.sourceOhm=m_feedSourceOhm->value();
    m_feeds.push_back(port);

    if(m_editPlane)m_editPlane->setCurrentIndex(1); // XZ
    if(m_activePlaneCoordinateM)m_activePlaneCoordinateM->setValue(0.0);
    rebuildScene(true);

    if(m_hybridPortMode)m_hybridPortMode->setCurrentIndex(1);
    if(m_hybridPortFeed){const int i=m_hybridPortFeed->findText(port.name);if(i>=0)m_hybridPortFeed->setCurrentIndex(i);}
    if(m_hybridJunctionFeed)m_hybridJunctionFeed->setCurrentIndex(0);
    if(m_hybridPortSurface){for(int i=0;i<m_hybridPortSurface->count();++i)if(m_hybridPortSurface->itemData(i).toInt()==0){m_hybridPortSurface->setCurrentIndex(i);break;}}
    if(m_hybridMappingToleranceMm)m_hybridMappingToleranceMm->setValue(std::max(5.0,0.015*lambda*MmPerM));
    if(m_hybridMomStatus)m_hybridMomStatus->setText(QStringLiteral("λ/4 monopole over PEC ground disk generated. Use the Hybrid solver and verify ground-radius / mesh / mapping convergence."));
}

void AntennaDesignerWidget::generateInvertedFAntenna()
{
    m_wires.clear();
    m_feeds.clear();
    m_planes.clear();
    m_dielectrics.clear();
    m_constraints.clear();

    const double fHz=std::max(1.0,m_frequencyMHz->value()*1e6);
    const double lambda=C0/fHz;
    const double height=0.05*lambda;
    const double topLength=0.22*lambda;
    const double feedX=0.035*lambda;
    const double groundW=0.50*lambda;
    const double groundH=0.24*lambda;
    const double wireRadius=std::max(1e-6,m_wireRadiusMm->value()/MmPerM);

    PlaneElement ground;
    ground.name=QStringLiteral("GND"); ground.surfaceType=0;
    ground.centerM=QPointF(0.5*topLength,0.0); ground.zM=0.0;
    ground.widthM=groundW; ground.heightM=groundH;
    ground.meshHintM=std::max(1e-4,std::min(lambda/22.0,std::min(groundW,groundH)/10.0));
    m_planes.push_back(ground);

    auto addWire3=[&](const QString&name,double ax,double az,double bx,double bz){
        WireElement w;w.name=name;w.aM=QPointF(ax,0.0);w.azM=az;w.bM=QPointF(bx,0.0);w.bzM=bz;w.radiusM=wireRadius;m_wires.push_back(w);
    };
    addWire3(QStringLiteral("SHORT"),0.0,0.0,0.0,height);
    addWire3(QStringLiteral("TOP1"),0.0,height,feedX,height);
    addWire3(QStringLiteral("TOP2"),feedX,height,topLength,height);
    addWire3(QStringLiteral("FEED_LEG"),feedX,0.0,feedX,height);

    FeedPoint driven;driven.name=QStringLiteral("F_DRIVE");driven.positionM=QPointF(feedX,0.0);driven.zM=0.0;
    driven.voltageV=m_feedVoltageV->value();driven.phaseDeg=m_feedPhaseDeg->value();driven.sourceOhm=m_feedSourceOhm->value();
    FeedPoint shortJ=driven;shortJ.name=QStringLiteral("F_SHORT_GND");shortJ.positionM=QPointF(0.0,0.0);shortJ.voltageV=0.0;
    m_feeds={driven,shortJ};

    if(m_editPlane)m_editPlane->setCurrentIndex(1); // XZ
    if(m_activePlaneCoordinateM)m_activePlaneCoordinateM->setValue(0.0);
    rebuildScene(true);

    if(m_hybridPortMode)m_hybridPortMode->setCurrentIndex(1);
    if(m_hybridPortFeed){const int i=m_hybridPortFeed->findText(driven.name);if(i>=0)m_hybridPortFeed->setCurrentIndex(i);}
    if(m_hybridJunctionFeed){const int i=m_hybridJunctionFeed->findText(shortJ.name);if(i>=0)m_hybridJunctionFeed->setCurrentIndex(i);}
    auto selectGround=[&](QComboBox*combo){if(!combo)return;for(int i=0;i<combo->count();++i)if(combo->itemData(i).toInt()==0){combo->setCurrentIndex(i);return;}};
    selectGround(m_hybridPortSurface);selectGround(m_hybridJunctionSurface);
    if(m_hybridMappingToleranceMm)m_hybridMappingToleranceMm->setValue(std::max(5.0,0.012*lambda*MmPerM));
    if(m_hybridMomStatus)m_hybridMomStatus->setText(QStringLiteral("Inverted-F starter generated: driven leg + shorting leg over a PEC ground plane. Tune height, top length and feed offset in Geometry after generation, then validate with the Hybrid solver."));
}

void AntennaDesignerWidget::generatePreset(int index)
{
    pushGeometryHistory();
    switch (index)
    {
        case 1: generateVDipole(); break;
        case 2: generateFoldedDipole(); break;
        case 3: generateSquareLoop(); break;
        case 4: generateYagiArray(); break;
        case 5: generateHelicalAntenna(); break;
        case 6: generateProbeFedPatch(); break;
        case 7: generateInsetMicrostripPatch(); break;
        case 8: generateQuarterWaveGroundPlane(); break;
        case 9: generateInvertedFAntenna(); break;
        default: generateHalfWaveDipole(); break;
    }
    if (m_sweepStartMHz && m_sweepStopMHz)
    {
        const double f = std::max(0.001, m_frequencyMHz->value());
        m_sweepStartMHz->setValue(0.70 * f);
        m_sweepStopMHz->setValue(1.30 * f);
        if (m_optTargetMHz) m_optTargetMHz->setValue(f);
        if (m_multiTargetMHz) m_multiTargetMHz->setValue(f);
    }
}

void AntennaDesignerWidget::saveGeometry()
{
    ensureGeometryIds();
    pruneInvalidGeometryConstraints();
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save antenna geometry"), QStringLiteral("antenna.qta"), QStringLiteral("QTsignalApp antenna (*.qta);;JSON (*.json)"));
    if (path.isEmpty()) return;

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("QTsignalApp antenna geometry"));
    root.insert(QStringLiteral("version"), AntennaProjectSchemaVersion);
    root.insert(QStringLiteral("units"), QStringLiteral("m"));
    root.insert(QStringLiteral("designFrequencyHz"), m_frequencyMHz->value() * 1e6);
    root.insert(QStringLiteral("editorPlane"), m_editPlane ? m_editPlane->currentIndex() : 0);
    root.insert(QStringLiteral("editorPlaneCoordinateM"), m_activePlaneCoordinateM ? m_activePlaneCoordinateM->value() : 0.0);
    root.insert(QStringLiteral("constructionPlaneRotXDeg"), m_constructionPlaneRotXDeg);
    root.insert(QStringLiteral("constructionPlaneRotYDeg"), m_constructionPlaneRotYDeg);
    root.insert(QStringLiteral("constructionPlaneRotZDeg"), m_constructionPlaneRotZDeg);
    root.insert(QStringLiteral("momSegmentsPerWavelength"), m_segmentsPerWavelength ? m_segmentsPerWavelength->value() : 80);
    root.insert(QStringLiteral("momMaxUnknowns"), m_maxMomUnknowns ? m_maxMomUnknowns->value() : 450);
    root.insert(QStringLiteral("momRadiusMeshFactor"), m_radiusMeshFactor ? m_radiusMeshFactor->value() : 3.5);
    root.insert(QStringLiteral("momJunctionLocalSubdivisions"), m_junctionLocalSubdivisions ? m_junctionLocalSubdivisions->value() : 3);
    root.insert(QStringLiteral("momJunctionTreatment"), m_junctionTreatment ? m_junctionTreatment->currentIndex() : 0);
    root.insert(QStringLiteral("momCurrentBasisTreatment"), m_currentBasisTreatment ? m_currentBasisTreatment->currentIndex() : 0);
    root.insert(QStringLiteral("surfaceMaxTriangles"), m_surfaceMaxTriangles ? m_surfaceMaxTriangles->value() : 500);
    root.insert(QStringLiteral("surfaceMaxUnknowns"), m_surfaceMaxUnknowns ? m_surfaceMaxUnknowns->value() : 350);
    root.insert(QStringLiteral("surfaceSelfRegularization"), m_surfaceSelfRegularization ? m_surfaceSelfRegularization->value() : 0.22);
    root.insert(QStringLiteral("hybridMaxUnknowns"), m_hybridMaxUnknowns ? m_hybridMaxUnknowns->value() : 450);
    root.insert(QStringLiteral("hybridMutualRegularization"), m_hybridMutualRegularization ? m_hybridMutualRegularization->value() : 0.08);
    root.insert(QStringLiteral("hybridPortMode"), m_hybridPortMode ? m_hybridPortMode->currentIndex() : 0);
    root.insert(QStringLiteral("hybridPortFeed"), m_hybridPortFeed ? m_hybridPortFeed->currentText() : QString());
    root.insert(QStringLiteral("hybridPortSurface"), m_hybridPortSurface ? m_hybridPortSurface->currentData().toInt() : -1);
    root.insert(QStringLiteral("hybridJunctionFeed"), (m_hybridJunctionFeed && m_hybridJunctionFeed->currentIndex()>0) ? m_hybridJunctionFeed->currentText() : QString());
    root.insert(QStringLiteral("hybridJunctionSurface"), m_hybridJunctionSurface ? m_hybridJunctionSurface->currentData().toInt() : -1);
    root.insert(QStringLiteral("hybridMappingToleranceMm"), m_hybridMappingToleranceMm ? m_hybridMappingToleranceMm->value() : 10.0);
    root.insert(QStringLiteral("hybridDifferentialPortRadiusMm"), m_hybridDifferentialPortRadiusMm ? m_hybridDifferentialPortRadiusMm->value() : 0.0);
    root.insert(QStringLiteral("hybridUseDielectric"), m_hybridUseDielectric ? m_hybridUseDielectric->isChecked() : true);
    root.insert(QStringLiteral("hybridDielectricKernel"), m_hybridDielectricKernel ? m_hybridDielectricKernel->currentIndex() : 1);
    root.insert(QStringLiteral("hybridTerminalHalfRwg"), m_hybridTerminalHalfRwg ? m_hybridTerminalHalfRwg->isChecked() : true);
    root.insert(QStringLiteral("hybridFiniteConductivity"), m_hybridFiniteConductivity ? m_hybridFiniteConductivity->isChecked() : false);
    root.insert(QStringLiteral("hybridSurfaceConductivityMSm"), m_hybridSurfaceConductivityMSm ? m_hybridSurfaceConductivityMSm->value() : 58.0);
    root.insert(QStringLiteral("hybridSurfaceThicknessUm"), m_hybridSurfaceThicknessUm ? m_hybridSurfaceThicknessUm->value() : 35.0);
    root.insert(QStringLiteral("hybridPortReferenceModel"), m_hybridPortReferenceModel ? m_hybridPortReferenceModel->currentIndex() : 0);
    root.insert(QStringLiteral("hybridCoaxInnerRadiusMm"), m_hybridCoaxInnerRadiusMm ? m_hybridCoaxInnerRadiusMm->value() : 0.50);
    root.insert(QStringLiteral("hybridCoaxOuterRadiusMm"), m_hybridCoaxOuterRadiusMm ? m_hybridCoaxOuterRadiusMm->value() : 1.70);
    root.insert(QStringLiteral("hybridCoaxEr"), m_hybridCoaxEr ? m_hybridCoaxEr->value() : 2.10);
    root.insert(QStringLiteral("hybridCoaxTanD"), m_hybridCoaxTanD ? m_hybridCoaxTanD->value() : 0.0);
    root.insert(QStringLiteral("hybridCoaxLengthMm"), m_hybridCoaxLengthMm ? m_hybridCoaxLengthMm->value() : 0.0);
    root.insert(QStringLiteral("sweepStartMHz"), m_sweepStartMHz ? m_sweepStartMHz->value() : 70.0);
    root.insert(QStringLiteral("sweepStopMHz"), m_sweepStopMHz ? m_sweepStopMHz->value() : 130.0);
    root.insert(QStringLiteral("sweepPoints"), m_sweepPoints ? m_sweepPoints->value() : 11);
    root.insert(QStringLiteral("sweepFeedIndex"), m_sweepFeed ? m_sweepFeed->currentIndex() : 0);
    root.insert(QStringLiteral("sweepExcitationMode"), m_sweepExcitationMode ? m_sweepExcitationMode->currentIndex() : 0);
    root.insert(QStringLiteral("sweepSolverIndex"), m_sweepSolver ? m_sweepSolver->currentIndex() : 0);
    root.insert(QStringLiteral("presetIndex"), m_presetCombo ? m_presetCombo->currentIndex() : 0);
    root.insert(QStringLiteral("presetDrivenLambda"), m_presetDrivenLambda ? m_presetDrivenLambda->value() : 0.475);
    root.insert(QStringLiteral("presetVOpeningDeg"), m_presetVOpeningDeg ? m_presetVOpeningDeg->value() : 120.0);
    root.insert(QStringLiteral("presetFoldedSpacingLambda"), m_presetFoldedSpacingLambda ? m_presetFoldedSpacingLambda->value() : 0.02);
    root.insert(QStringLiteral("presetLoopPerimeterLambda"), m_presetLoopPerimeterLambda ? m_presetLoopPerimeterLambda->value() : 1.0);
    root.insert(QStringLiteral("presetYagiReflectorLambda"), m_presetYagiReflectorLambda ? m_presetYagiReflectorLambda->value() : 0.50);
    root.insert(QStringLiteral("presetYagiDirectorLambda"), m_presetYagiDirectorLambda ? m_presetYagiDirectorLambda->value() : 0.44);
    root.insert(QStringLiteral("presetYagiReflectorSpacingLambda"), m_presetYagiReflectorSpacingLambda ? m_presetYagiReflectorSpacingLambda->value() : 0.20);
    root.insert(QStringLiteral("presetYagiDirectorSpacingLambda"), m_presetYagiDirectorSpacingLambda ? m_presetYagiDirectorSpacingLambda->value() : 0.15);
    root.insert(QStringLiteral("presetYagiDirectorCount"), m_presetYagiDirectorCount ? m_presetYagiDirectorCount->value() : 1);
    root.insert(QStringLiteral("presetYagiDirectorPitchLambda"), m_presetYagiDirectorPitchLambda ? m_presetYagiDirectorPitchLambda->value() : 0.15);
    root.insert(QStringLiteral("presetYagiDirectorTaperLambda"), m_presetYagiDirectorTaperLambda ? m_presetYagiDirectorTaperLambda->value() : 0.008);
    root.insert(QStringLiteral("presetHelixRadiusLambda"), m_presetHelixRadiusLambda ? m_presetHelixRadiusLambda->value() : 0.16);
    root.insert(QStringLiteral("presetHelixPitchLambda"), m_presetHelixPitchLambda ? m_presetHelixPitchLambda->value() : 0.22);
    root.insert(QStringLiteral("presetHelixTurns"), m_presetHelixTurns ? m_presetHelixTurns->value() : 5.0);
    root.insert(QStringLiteral("presetPatchEr"), m_presetPatchEr ? m_presetPatchEr->value() : 4.2);
    root.insert(QStringLiteral("presetPatchHeightMm"), m_presetPatchHeightMm ? m_presetPatchHeightMm->value() : 1.6);
    root.insert(QStringLiteral("presetPatchTanD"), m_presetPatchTanD ? m_presetPatchTanD->value() : 0.02);
    root.insert(QStringLiteral("presetPatchLineZ0"), m_presetPatchLineZ0 ? m_presetPatchLineZ0->value() : 50.0);
    root.insert(QStringLiteral("presetPatchEdgeResistance"), m_presetPatchEdgeResistance ? m_presetPatchEdgeResistance->value() : 300.0);
    root.insert(QStringLiteral("presetPatchNotchGapMm"), m_presetPatchNotchGapMm ? m_presetPatchNotchGapMm->value() : 0.40);
    root.insert(QStringLiteral("primitiveType"), m_primitiveType ? m_primitiveType->currentIndex() : 0);
    root.insert(QStringLiteral("primitiveOrientation"), m_primitiveOrientation ? m_primitiveOrientation->currentIndex() : 0);
    root.insert(QStringLiteral("primitiveOriginX"), m_primitiveOriginX ? m_primitiveOriginX->value() : 0.0);
    root.insert(QStringLiteral("primitiveOriginY"), m_primitiveOriginY ? m_primitiveOriginY->value() : 0.0);
    root.insert(QStringLiteral("primitiveOriginZ"), m_primitiveOriginZ ? m_primitiveOriginZ->value() : 0.0);
    root.insert(QStringLiteral("primitiveYawDeg"), m_primitiveYawDeg ? m_primitiveYawDeg->value() : 0.0);
    root.insert(QStringLiteral("primitivePitchDeg"), m_primitivePitchDeg ? m_primitivePitchDeg->value() : 0.0);
    root.insert(QStringLiteral("primitiveRollDeg"), m_primitiveRollDeg ? m_primitiveRollDeg->value() : 0.0);
    root.insert(QStringLiteral("primitiveRadiusM"), m_primitiveRadiusM ? m_primitiveRadiusM->value() : 0.20);
    root.insert(QStringLiteral("primitiveInnerRadiusM"), m_primitiveInnerRadiusM ? m_primitiveInnerRadiusM->value() : 0.03);
    root.insert(QStringLiteral("primitiveWidthM"), m_primitiveWidthM ? m_primitiveWidthM->value() : 0.50);
    root.insert(QStringLiteral("primitiveHeightM"), m_primitiveHeightM ? m_primitiveHeightM->value() : 0.30);
    root.insert(QStringLiteral("primitiveFocalLengthM"), m_primitiveFocalLengthM ? m_primitiveFocalLengthM->value() : 0.25);
    root.insert(QStringLiteral("primitivePitchM"), m_primitivePitchM ? m_primitivePitchM->value() : 0.08);
    root.insert(QStringLiteral("primitiveTurns"), m_primitiveTurns ? m_primitiveTurns->value() : 4.0);
    root.insert(QStringLiteral("primitiveStartDeg"), m_primitiveStartDeg ? m_primitiveStartDeg->value() : 0.0);
    root.insert(QStringLiteral("primitiveSweepDeg"), m_primitiveSweepDeg ? m_primitiveSweepDeg->value() : 360.0);
    root.insert(QStringLiteral("primitiveSegments"), m_primitiveSegments ? m_primitiveSegments->value() : 32);
    root.insert(QStringLiteral("primitiveGridSpacingM"), m_primitiveGridSpacingM ? m_primitiveGridSpacingM->value() : 0.05);
    root.insert(QStringLiteral("primitiveThicknessM"), m_primitiveThicknessM ? m_primitiveThicknessM->value() : 0.0016);
    root.insert(QStringLiteral("primitiveDielectricEr"), m_primitiveDielectricEr ? m_primitiveDielectricEr->value() : 4.2);
    root.insert(QStringLiteral("primitiveDielectricTanD"), m_primitiveDielectricTanD ? m_primitiveDielectricTanD->value() : 0.02);
    root.insert(QStringLiteral("primitiveDielectricFill"), m_primitiveDielectricFill ? m_primitiveDielectricFill->value() : 0.65);
    root.insert(QStringLiteral("primitiveAddFeed"), m_primitiveAddFeed ? m_primitiveAddFeed->isChecked() : false);
    root.insert(QStringLiteral("primitiveClearFirst"), m_primitiveClearFirst ? m_primitiveClearFirst->isChecked() : false);
    root.insert(QStringLiteral("optimizationTargetMHz"), m_optTargetMHz ? m_optTargetMHz->value() : m_frequencyMHz->value());
    root.insert(QStringLiteral("optimizationFeedIndex"), m_optFeed ? m_optFeed->currentIndex() : 0);
    root.insert(QStringLiteral("optimizationVariable"), m_optVariable ? m_optVariable->currentIndex() : 0);
    root.insert(QStringLiteral("optimizationObjective"), m_optObjective ? m_optObjective->currentIndex() : 0);
    root.insert(QStringLiteral("optimizationMinFactor"), m_optMinFactor ? m_optMinFactor->value() : 0.75);
    root.insert(QStringLiteral("optimizationMaxFactor"), m_optMaxFactor ? m_optMaxFactor->value() : 1.25);
    root.insert(QStringLiteral("optimizationCoarseSamples"), m_optCoarseSamples ? m_optCoarseSamples->value() : 7);
    root.insert(QStringLiteral("optimizationRefineIterations"), m_optRefineIterations ? m_optRefineIterations->value() : 6);
    root.insert(QStringLiteral("multiOptimizationTargetMHz"), m_multiTargetMHz ? m_multiTargetMHz->value() : m_frequencyMHz->value());
    root.insert(QStringLiteral("multiOptimizationFeedIndex"), m_multiFeed ? m_multiFeed->currentIndex() : 0);
    root.insert(QStringLiteral("multiOptimizationDrivenLength"), m_multiDrivenLength ? m_multiDrivenLength->isChecked() : true);
    root.insert(QStringLiteral("multiOptimizationReflectorLength"), m_multiReflectorLength ? m_multiReflectorLength->isChecked() : true);
    root.insert(QStringLiteral("multiOptimizationDirectorLength"), m_multiDirectorLength ? m_multiDirectorLength->isChecked() : true);
    root.insert(QStringLiteral("multiOptimizationReflectorSpacing"), m_multiReflectorSpacing ? m_multiReflectorSpacing->isChecked() : true);
    root.insert(QStringLiteral("multiOptimizationDirectorSpacing"), m_multiDirectorSpacing ? m_multiDirectorSpacing->isChecked() : true);
    root.insert(QStringLiteral("multiOptimizationMinFactor"), m_multiMinFactor ? m_multiMinFactor->value() : 0.80);
    root.insert(QStringLiteral("multiOptimizationMaxFactor"), m_multiMaxFactor ? m_multiMaxFactor->value() : 1.20);
    root.insert(QStringLiteral("multiOptimizationSamplesPerVariable"), m_multiSamplesPerVariable ? m_multiSamplesPerVariable->value() : 5);
    root.insert(QStringLiteral("multiOptimizationPasses"), m_multiPasses ? m_multiPasses->value() : 2);
    root.insert(QStringLiteral("multiOptimizationWeightMatch"), m_multiWeightMatch ? m_multiWeightMatch->value() : 1.0);
    root.insert(QStringLiteral("multiOptimizationWeightDirectivity"), m_multiWeightDirectivity ? m_multiWeightDirectivity->value() : 0.20);
    root.insert(QStringLiteral("multiOptimizationWeightFrontBack"), m_multiWeightFrontBack ? m_multiWeightFrontBack->value() : 0.20);
    root.insert(QStringLiteral("multiOptimizationWeightBandwidth"), m_multiWeightBandwidth ? m_multiWeightBandwidth->value() : 0.0);
    root.insert(QStringLiteral("multiOptimizationBandwidthHalfSpanPct"), m_multiBandwidthHalfSpanPct ? m_multiBandwidthHalfSpanPct->value() : 5.0);
    root.insert(QStringLiteral("multiOptimizationBandwidthSamples"), m_multiBandwidthSamples ? m_multiBandwidthSamples->value() : 5);
    root.insert(QStringLiteral("multiOptimizationBandwidthAggregation"), m_multiBandwidthAggregation ? m_multiBandwidthAggregation->currentIndex() : 0);
    root.insert(QStringLiteral("multiOptimizationMinElementSpacingLambda"), m_multiMinElementSpacingLambda ? m_multiMinElementSpacingLambda->value() : 0.05);
    root.insert(QStringLiteral("multiOptimizationMaxBoomLengthLambda"), m_multiMaxBoomLengthLambda ? m_multiMaxBoomLengthLambda->value() : 4.0);
    QJsonArray individualDirectorVariables;
    if (m_individualDirectorTable)
    {
        for (int r = 0; r < m_individualDirectorTable->rowCount(); ++r)
        {
            QJsonObject o;
            o.insert(QStringLiteral("label"), m_individualDirectorTable->item(r,0) ? m_individualDirectorTable->item(r,0)->text() : QStringLiteral("D%1").arg(r+1));
            o.insert(QStringLiteral("lengthFree"), m_individualDirectorTable->item(r,1) && m_individualDirectorTable->item(r,1)->checkState() == Qt::Checked);
            o.insert(QStringLiteral("positionFree"), m_individualDirectorTable->item(r,4) && m_individualDirectorTable->item(r,4)->checkState() == Qt::Checked);
            if (auto *w = qobject_cast<QDoubleSpinBox *>(m_individualDirectorTable->cellWidget(r,2))) o.insert(QStringLiteral("lengthMin"), w->value());
            if (auto *w = qobject_cast<QDoubleSpinBox *>(m_individualDirectorTable->cellWidget(r,3))) o.insert(QStringLiteral("lengthMax"), w->value());
            if (auto *w = qobject_cast<QDoubleSpinBox *>(m_individualDirectorTable->cellWidget(r,5))) o.insert(QStringLiteral("positionMin"), w->value());
            if (auto *w = qobject_cast<QDoubleSpinBox *>(m_individualDirectorTable->cellWidget(r,6))) o.insert(QStringLiteral("positionMax"), w->value());
            individualDirectorVariables.append(o);
        }
    }
    root.insert(QStringLiteral("individualDirectorVariables"), individualDirectorVariables);

    QJsonArray wires;
    for (const auto &w : m_wires)
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), w.id);
        o.insert(QStringLiteral("name"), w.name);
        o.insert(QStringLiteral("x1"), w.aM.x()); o.insert(QStringLiteral("y1"), w.aM.y()); o.insert(QStringLiteral("z1"), w.azM);
        o.insert(QStringLiteral("x2"), w.bM.x()); o.insert(QStringLiteral("y2"), w.bM.y()); o.insert(QStringLiteral("z2"), w.bzM);
        o.insert(QStringLiteral("radiusM"), w.radiusM);
        wires.append(o);
    }
    root.insert(QStringLiteral("wires"), wires);

    QJsonArray surfaces;
    for (const auto &p : m_planes)
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), p.id);
        o.insert(QStringLiteral("name"), p.name);
        o.insert(QStringLiteral("surfaceType"), p.surfaceType);
        o.insert(QStringLiteral("cx"), p.centerM.x()); o.insert(QStringLiteral("cy"), p.centerM.y()); o.insert(QStringLiteral("cz"), p.zM);
        o.insert(QStringLiteral("orientation"), p.orientation);
        o.insert(QStringLiteral("yawDeg"), p.yawDeg); o.insert(QStringLiteral("pitchDeg"), p.pitchDeg); o.insert(QStringLiteral("rollDeg"), p.rollDeg);
        o.insert(QStringLiteral("radiusM"), p.radiusM); o.insert(QStringLiteral("innerRadiusM"), p.innerRadiusM);
        o.insert(QStringLiteral("widthM"), p.widthM); o.insert(QStringLiteral("heightM"), p.heightM);
        o.insert(QStringLiteral("focalLengthM"), p.focalLengthM);
        o.insert(QStringLiteral("feedWidthM"), p.feedWidthM); o.insert(QStringLiteral("feedLengthM"), p.feedLengthM);
        o.insert(QStringLiteral("insetDepthM"), p.insetDepthM); o.insert(QStringLiteral("notchGapM"), p.notchGapM);
        o.insert(QStringLiteral("meshHintM"), p.meshHintM);
        surfaces.append(o);
    }
    root.insert(QStringLiteral("surfaces"), surfaces);

    QJsonArray dielectrics;
    for (const auto &d : m_dielectrics)
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), d.id);
        o.insert(QStringLiteral("name"), d.name);
        o.insert(QStringLiteral("cx"), d.centerM.x()); o.insert(QStringLiteral("cy"), d.centerM.y()); o.insert(QStringLiteral("cz"), d.zM);
        o.insert(QStringLiteral("orientation"), d.orientation);
        o.insert(QStringLiteral("yawDeg"), d.yawDeg); o.insert(QStringLiteral("pitchDeg"), d.pitchDeg); o.insert(QStringLiteral("rollDeg"), d.rollDeg);
        o.insert(QStringLiteral("widthM"), d.widthM); o.insert(QStringLiteral("heightM"), d.heightM); o.insert(QStringLiteral("thicknessM"), d.thicknessM);
        o.insert(QStringLiteral("relativePermittivity"), d.relativePermittivity); o.insert(QStringLiteral("lossTangent"), d.lossTangent); o.insert(QStringLiteral("fieldFillFactor"), d.fieldFillFactor);
        dielectrics.append(o);
    }
    root.insert(QStringLiteral("dielectrics"), dielectrics);

    QJsonArray feeds;
    for (const auto &f : m_feeds)
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), f.id);
        o.insert(QStringLiteral("name"), f.name);
        o.insert(QStringLiteral("x"), f.positionM.x()); o.insert(QStringLiteral("y"), f.positionM.y()); o.insert(QStringLiteral("z"), f.zM);
        o.insert(QStringLiteral("voltageV"), f.voltageV); o.insert(QStringLiteral("phaseDeg"), f.phaseDeg);
        o.insert(QStringLiteral("referenceOhm"), f.sourceOhm);
        o.insert(QStringLiteral("sourceOhm"), f.sourceOhm); // backward compatibility with .qta v1
        feeds.append(o);
    }
    root.insert(QStringLiteral("feeds"), feeds);

    QJsonArray constraints;
    for(const auto &c:m_constraints)
    {
        QJsonObject o;o.insert(QStringLiteral("id"),c.id);o.insert(QStringLiteral("name"),c.name);o.insert(QStringLiteral("type"),c.type);o.insert(QStringLiteral("aKind"),c.a.kind);o.insert(QStringLiteral("aId"),c.a.id);o.insert(QStringLiteral("aAnchor"),c.a.anchor);o.insert(QStringLiteral("bKind"),c.b.kind);o.insert(QStringLiteral("bId"),c.b.id);o.insert(QStringLiteral("bAnchor"),c.b.anchor);o.insert(QStringLiteral("valueM"),c.valueM);o.insert(QStringLiteral("valueDeg"),c.valueDeg);o.insert(QStringLiteral("enabled"),c.enabled);o.insert(QStringLiteral("solveMode"),c.solveMode);o.insert(QStringLiteral("fixedMask"),c.fixedMask);o.insert(QStringLiteral("fixedX"),c.fixedPoint.xyM.x());o.insert(QStringLiteral("fixedY"),c.fixedPoint.xyM.y());o.insert(QStringLiteral("fixedZ"),c.fixedPoint.zM);constraints.append(o);
    }
    root.insert(QStringLiteral("constraints"),constraints);

    QJsonArray groups;
    for(const auto &g:m_groups)
    {
        QJsonObject o;o.insert(QStringLiteral("id"),g.id);o.insert(QStringLiteral("name"),g.name);
        QJsonArray members;for(const auto&id:g.objectIds)members.append(id);o.insert(QStringLiteral("members"),members);groups.append(o);
    }
    root.insert(QStringLiteral("groups"),groups);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::warning(this, QStringLiteral("Save antenna"), QStringLiteral("Cannot open the selected file for atomic writing."));
        return;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size() || !file.commit())
    {
        QMessageBox::warning(this, QStringLiteral("Save antenna"),
                             QStringLiteral("The antenna project could not be committed safely. The previous file was left unchanged."));
        return;
    }
}

void AntennaDesignerWidget::loadGeometry()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load antenna geometry"), QString(), QStringLiteral("QTsignalApp antenna (*.qta *.json);;All files (*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, QStringLiteral("Load antenna"), QStringLiteral("Cannot open the selected file."));
        return;
    }
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
    {
        QMessageBox::warning(this, QStringLiteral("Load antenna"), QStringLiteral("Invalid JSON antenna file: %1").arg(err.errorString()));
        return;
    }
    const QJsonObject rawRoot = doc.object();
    if (rawRoot.value(QStringLiteral("format")).toString() != QStringLiteral("QTsignalApp antenna geometry"))
    {
        QMessageBox::warning(this, QStringLiteral("Load antenna"), QStringLiteral("This file is not a QTsignalApp antenna geometry."));
        return;
    }

    int sourceSchemaVersion = AntennaProjectMinimumSchemaVersion;
    QString migrationNotice;
    QString validationError;
    if (!checkAntennaProjectSchema(rawRoot, sourceSchemaVersion, migrationNotice, validationError))
    {
        QMessageBox::warning(this, QStringLiteral("Load antenna — incompatible project"), validationError);
        return;
    }
    QJsonObject root = migrateAntennaProjectToCurrent(rawRoot, sourceSchemaVersion);
    if (!validateAntennaProjectDocument(root, validationError))
    {
        QMessageBox::warning(this, QStringLiteral("Load antenna — invalid project"),
                             QStringLiteral("The project was rejected before modifying the current geometry:\n\n%1").arg(validationError));
        return;
    }

    std::vector<WireElement> wires;
    for (const auto &v : root.value(QStringLiteral("wires")).toArray())
    {
        const auto o = v.toObject();
        WireElement w;
        w.id = o.value(QStringLiteral("id")).toString();
        w.name = o.value(QStringLiteral("name")).toString(QStringLiteral("W%1").arg(wires.size()+1));
        w.aM = QPointF(o.value(QStringLiteral("x1")).toDouble(), o.value(QStringLiteral("y1")).toDouble());
        w.azM = o.value(QStringLiteral("z1")).toDouble(0.0);
        w.bM = QPointF(o.value(QStringLiteral("x2")).toDouble(), o.value(QStringLiteral("y2")).toDouble());
        w.bzM = o.value(QStringLiteral("z2")).toDouble(0.0);
        w.radiusM = std::max(1e-9, o.value(QStringLiteral("radiusM")).toDouble(0.001));
        wires.push_back(w);
    }
    std::vector<PlaneElement> planes;
    const QJsonArray surfaceArray = root.contains(QStringLiteral("surfaces")) ? root.value(QStringLiteral("surfaces")).toArray() : root.value(QStringLiteral("planes")).toArray();
    for (const auto &v : surfaceArray)
    {
        const auto o=v.toObject(); PlaneElement p;
        p.id=o.value(QStringLiteral("id")).toString();
        p.name=o.value(QStringLiteral("name")).toString(QStringLiteral("SURFACE%1").arg(planes.size()+1));
        p.surfaceType=std::clamp(o.value(QStringLiteral("surfaceType")).toInt(0),0,5);
        p.centerM=QPointF(o.value(QStringLiteral("cx")).toDouble(),o.value(QStringLiteral("cy")).toDouble());p.zM=o.value(QStringLiteral("cz")).toDouble(0.0);
        p.orientation=std::clamp(o.value(QStringLiteral("orientation")).toInt(0),0,2);
        p.yawDeg=o.value(QStringLiteral("yawDeg")).toDouble(0.0);p.pitchDeg=o.value(QStringLiteral("pitchDeg")).toDouble(0.0);p.rollDeg=o.value(QStringLiteral("rollDeg")).toDouble(0.0);
        p.radiusM=std::max(1e-9,o.value(QStringLiteral("radiusM")).toDouble(0.25));p.innerRadiusM=std::max(0.0,o.value(QStringLiteral("innerRadiusM")).toDouble(0.0));
        p.widthM=std::max(1e-9,o.value(QStringLiteral("widthM")).toDouble(1.0));p.heightM=std::max(1e-9,o.value(QStringLiteral("heightM")).toDouble(1.0));
        p.focalLengthM=std::max(1e-9,o.value(QStringLiteral("focalLengthM")).toDouble(0.25));
        p.feedWidthM=std::max(1e-9,o.value(QStringLiteral("feedWidthM")).toDouble(0.003));p.feedLengthM=std::max(0.0,o.value(QStringLiteral("feedLengthM")).toDouble(0.020));
        p.insetDepthM=std::max(0.0,o.value(QStringLiteral("insetDepthM")).toDouble(0.0));p.notchGapM=std::max(0.0,o.value(QStringLiteral("notchGapM")).toDouble(0.0005));
        p.meshHintM=std::max(1e-9,o.value(QStringLiteral("meshHintM")).toDouble(0.05));
        planes.push_back(p);
    }
    std::vector<DielectricElement> dielectrics;
    for (const auto &v : root.value(QStringLiteral("dielectrics")).toArray())
    {
        const auto o=v.toObject(); DielectricElement d;
        d.id=o.value(QStringLiteral("id")).toString();
        d.name=o.value(QStringLiteral("name")).toString(QStringLiteral("SUB%1").arg(dielectrics.size()+1));
        d.centerM=QPointF(o.value(QStringLiteral("cx")).toDouble(),o.value(QStringLiteral("cy")).toDouble()); d.zM=o.value(QStringLiteral("cz")).toDouble(0.0);
        d.orientation=std::clamp(o.value(QStringLiteral("orientation")).toInt(0),0,2);
        d.yawDeg=o.value(QStringLiteral("yawDeg")).toDouble(0.0); d.pitchDeg=o.value(QStringLiteral("pitchDeg")).toDouble(0.0); d.rollDeg=o.value(QStringLiteral("rollDeg")).toDouble(0.0);
        d.widthM=std::max(1e-9,o.value(QStringLiteral("widthM")).toDouble(0.10)); d.heightM=std::max(1e-9,o.value(QStringLiteral("heightM")).toDouble(0.10));
        d.thicknessM=std::max(1e-9,o.value(QStringLiteral("thicknessM")).toDouble(0.0016));
        d.relativePermittivity=std::max(1.0,o.value(QStringLiteral("relativePermittivity")).toDouble(4.2));
        d.lossTangent=std::max(0.0,o.value(QStringLiteral("lossTangent")).toDouble(0.02));
        d.fieldFillFactor=std::clamp(o.value(QStringLiteral("fieldFillFactor")).toDouble(0.65),0.0,1.0);
        dielectrics.push_back(d);
    }
    std::vector<FeedPoint> feeds;
    for (const auto &v : root.value(QStringLiteral("feeds")).toArray())
    {
        const auto o = v.toObject();
        FeedPoint f;
        f.id = o.value(QStringLiteral("id")).toString();
        f.name = o.value(QStringLiteral("name")).toString(QStringLiteral("F%1").arg(feeds.size()+1));
        f.positionM = QPointF(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble());
        f.zM = o.value(QStringLiteral("z")).toDouble(0.0);
        f.voltageV = o.value(QStringLiteral("voltageV")).toDouble(1.0);
        f.phaseDeg = o.value(QStringLiteral("phaseDeg")).toDouble(0.0);
        const double zref = o.contains(QStringLiteral("referenceOhm"))
            ? o.value(QStringLiteral("referenceOhm")).toDouble(50.0)
            : o.value(QStringLiteral("sourceOhm")).toDouble(50.0);
        f.sourceOhm = std::max(1e-9, zref);
        feeds.push_back(f);
    }
    std::vector<GeometryConstraint> constraints;
    for(const auto &v:root.value(QStringLiteral("constraints")).toArray())
    {
        const auto o=v.toObject();GeometryConstraint c;c.id=o.value(QStringLiteral("id")).toString();c.name=o.value(QStringLiteral("name")).toString(QStringLiteral("C%1").arg(constraints.size()+1));c.type=std::clamp(o.value(QStringLiteral("type")).toInt(0),0,13);c.a.kind=std::clamp(o.value(QStringLiteral("aKind")).toInt(0),0,3);c.a.id=o.value(QStringLiteral("aId")).toString();c.a.anchor=c.a.kind==0?std::clamp(o.value(QStringLiteral("aAnchor")).toInt(0),0,2):0;c.b.kind=std::clamp(o.value(QStringLiteral("bKind")).toInt(c.a.kind),0,3);c.b.id=o.value(QStringLiteral("bId")).toString(c.a.id);c.b.anchor=c.b.kind==0?std::clamp(o.value(QStringLiteral("bAnchor")).toInt(c.a.anchor),0,2):0;c.valueM=std::max(0.0,o.value(QStringLiteral("valueM")).toDouble(0.0));c.valueDeg=std::clamp(o.value(QStringLiteral("valueDeg")).toDouble(0.0),0.0,180.0);c.enabled=o.value(QStringLiteral("enabled")).toBool(true);c.solveMode=std::clamp(o.value(QStringLiteral("solveMode")).toInt(0),0,1);c.fixedMask=std::clamp(o.value(QStringLiteral("fixedMask")).toInt(7),1,7);c.fixedPoint={{o.value(QStringLiteral("fixedX")).toDouble(0.0),o.value(QStringLiteral("fixedY")).toDouble(0.0)},o.value(QStringLiteral("fixedZ")).toDouble(0.0)};constraints.push_back(c);
    }
    std::vector<GeometryGroup> groups;
    for(const auto &v:root.value(QStringLiteral("groups")).toArray())
    {
        const auto o=v.toObject();GeometryGroup g;g.id=o.value(QStringLiteral("id")).toString();g.name=o.value(QStringLiteral("name")).toString(QStringLiteral("Group %1").arg(groups.size()+1));
        for(const auto&m:o.value(QStringLiteral("members")).toArray()){const QString id=m.toString();if(!id.isEmpty())g.objectIds.push_back(id);}
        if(g.objectIds.size()>=2)groups.push_back(std::move(g));
    }
    m_wires = std::move(wires);
    m_feeds = std::move(feeds);
    m_planes = std::move(planes);
    m_dielectrics = std::move(dielectrics);
    m_constraints = std::move(constraints);
    m_groups = std::move(groups);
    // Backward-load sequence retained and extended with group pruning:
    // ensureGeometryIds();pruneInvalidGeometryConstraints();solveGeometryConstraints(false);
    ensureGeometryIds();pruneInvalidGeometryConstraints();pruneInvalidGeometryGroups();solveGeometryConstraints(false);
    const double fHz = root.value(QStringLiteral("designFrequencyHz")).toDouble(m_frequencyMHz->value()*1e6);
    if (std::isfinite(fHz) && fHz > 0.0) m_frequencyMHz->setValue(fHz/1e6);
    if (m_segmentsPerWavelength && root.contains(QStringLiteral("momSegmentsPerWavelength")))
        m_segmentsPerWavelength->setValue(root.value(QStringLiteral("momSegmentsPerWavelength")).toInt(m_segmentsPerWavelength->value()));
    if (m_maxMomUnknowns && root.contains(QStringLiteral("momMaxUnknowns")))
        m_maxMomUnknowns->setValue(root.value(QStringLiteral("momMaxUnknowns")).toInt(m_maxMomUnknowns->value()));
    if (m_radiusMeshFactor && root.contains(QStringLiteral("momRadiusMeshFactor")))
        m_radiusMeshFactor->setValue(root.value(QStringLiteral("momRadiusMeshFactor")).toDouble(m_radiusMeshFactor->value()));
    if (m_junctionLocalSubdivisions && root.contains(QStringLiteral("momJunctionLocalSubdivisions")))
        m_junctionLocalSubdivisions->setValue(root.value(QStringLiteral("momJunctionLocalSubdivisions")).toInt(m_junctionLocalSubdivisions->value()));
    if (m_junctionTreatment && root.contains(QStringLiteral("momJunctionTreatment")))
        m_junctionTreatment->setCurrentIndex(std::clamp(root.value(QStringLiteral("momJunctionTreatment")).toInt(0),0,1));
    if (m_currentBasisTreatment && root.contains(QStringLiteral("momCurrentBasisTreatment")))
        m_currentBasisTreatment->setCurrentIndex(std::clamp(root.value(QStringLiteral("momCurrentBasisTreatment")).toInt(0),0,1));
    if (m_surfaceMaxTriangles && root.contains(QStringLiteral("surfaceMaxTriangles")))
        m_surfaceMaxTriangles->setValue(root.value(QStringLiteral("surfaceMaxTriangles")).toInt(m_surfaceMaxTriangles->value()));
    if (m_surfaceMaxUnknowns && root.contains(QStringLiteral("surfaceMaxUnknowns")))
        m_surfaceMaxUnknowns->setValue(root.value(QStringLiteral("surfaceMaxUnknowns")).toInt(m_surfaceMaxUnknowns->value()));
    if (m_surfaceSelfRegularization && root.contains(QStringLiteral("surfaceSelfRegularization")))
        m_surfaceSelfRegularization->setValue(root.value(QStringLiteral("surfaceSelfRegularization")).toDouble(m_surfaceSelfRegularization->value()));
    if (m_hybridMaxUnknowns && root.contains(QStringLiteral("hybridMaxUnknowns")))
        m_hybridMaxUnknowns->setValue(root.value(QStringLiteral("hybridMaxUnknowns")).toInt(m_hybridMaxUnknowns->value()));
    if (m_hybridMutualRegularization && root.contains(QStringLiteral("hybridMutualRegularization")))
        m_hybridMutualRegularization->setValue(root.value(QStringLiteral("hybridMutualRegularization")).toDouble(m_hybridMutualRegularization->value()));
    if (m_hybridPortMode && root.contains(QStringLiteral("hybridPortMode")))
        m_hybridPortMode->setCurrentIndex(std::clamp(root.value(QStringLiteral("hybridPortMode")).toInt(0),0,m_hybridPortMode->count()-1));
    if (m_hybridMappingToleranceMm && root.contains(QStringLiteral("hybridMappingToleranceMm")))
        m_hybridMappingToleranceMm->setValue(root.value(QStringLiteral("hybridMappingToleranceMm")).toDouble(m_hybridMappingToleranceMm->value()));
    if (m_hybridDifferentialPortRadiusMm && root.contains(QStringLiteral("hybridDifferentialPortRadiusMm")))
        m_hybridDifferentialPortRadiusMm->setValue(root.value(QStringLiteral("hybridDifferentialPortRadiusMm")).toDouble(m_hybridDifferentialPortRadiusMm->value()));
    if (m_hybridUseDielectric && root.contains(QStringLiteral("hybridUseDielectric")))
        m_hybridUseDielectric->setChecked(root.value(QStringLiteral("hybridUseDielectric")).toBool(true));
    if (m_hybridDielectricKernel && root.contains(QStringLiteral("hybridDielectricKernel")))
        m_hybridDielectricKernel->setCurrentIndex(std::clamp(root.value(QStringLiteral("hybridDielectricKernel")).toInt(1),0,m_hybridDielectricKernel->count()-1));
    if (m_hybridTerminalHalfRwg && root.contains(QStringLiteral("hybridTerminalHalfRwg")))
        m_hybridTerminalHalfRwg->setChecked(root.value(QStringLiteral("hybridTerminalHalfRwg")).toBool(true));
    if (m_hybridFiniteConductivity && root.contains(QStringLiteral("hybridFiniteConductivity")))
        m_hybridFiniteConductivity->setChecked(root.value(QStringLiteral("hybridFiniteConductivity")).toBool(false));
    if (m_hybridSurfaceConductivityMSm && root.contains(QStringLiteral("hybridSurfaceConductivityMSm")))
        m_hybridSurfaceConductivityMSm->setValue(root.value(QStringLiteral("hybridSurfaceConductivityMSm")).toDouble(58.0));
    if (m_hybridSurfaceThicknessUm && root.contains(QStringLiteral("hybridSurfaceThicknessUm")))
        m_hybridSurfaceThicknessUm->setValue(root.value(QStringLiteral("hybridSurfaceThicknessUm")).toDouble(35.0));
    if (m_hybridPortReferenceModel && root.contains(QStringLiteral("hybridPortReferenceModel")))
        m_hybridPortReferenceModel->setCurrentIndex(std::clamp(root.value(QStringLiteral("hybridPortReferenceModel")).toInt(0),0,m_hybridPortReferenceModel->count()-1));
    if (m_hybridCoaxInnerRadiusMm && root.contains(QStringLiteral("hybridCoaxInnerRadiusMm")))
        m_hybridCoaxInnerRadiusMm->setValue(root.value(QStringLiteral("hybridCoaxInnerRadiusMm")).toDouble(m_hybridCoaxInnerRadiusMm->value()));
    if (m_hybridCoaxOuterRadiusMm && root.contains(QStringLiteral("hybridCoaxOuterRadiusMm")))
        m_hybridCoaxOuterRadiusMm->setValue(root.value(QStringLiteral("hybridCoaxOuterRadiusMm")).toDouble(m_hybridCoaxOuterRadiusMm->value()));
    if (m_hybridCoaxEr && root.contains(QStringLiteral("hybridCoaxEr")))
        m_hybridCoaxEr->setValue(root.value(QStringLiteral("hybridCoaxEr")).toDouble(m_hybridCoaxEr->value()));
    if (m_hybridCoaxTanD && root.contains(QStringLiteral("hybridCoaxTanD")))
        m_hybridCoaxTanD->setValue(root.value(QStringLiteral("hybridCoaxTanD")).toDouble(m_hybridCoaxTanD->value()));
    if (m_hybridCoaxLengthMm && root.contains(QStringLiteral("hybridCoaxLengthMm")))
        m_hybridCoaxLengthMm->setValue(root.value(QStringLiteral("hybridCoaxLengthMm")).toDouble(m_hybridCoaxLengthMm->value()));
    if (m_sweepStartMHz && root.contains(QStringLiteral("sweepStartMHz")))
        m_sweepStartMHz->setValue(root.value(QStringLiteral("sweepStartMHz")).toDouble(m_sweepStartMHz->value()));
    if (m_sweepStopMHz && root.contains(QStringLiteral("sweepStopMHz")))
        m_sweepStopMHz->setValue(root.value(QStringLiteral("sweepStopMHz")).toDouble(m_sweepStopMHz->value()));
    if (m_sweepPoints && root.contains(QStringLiteral("sweepPoints")))
        m_sweepPoints->setValue(root.value(QStringLiteral("sweepPoints")).toInt(m_sweepPoints->value()));
    if (m_presetDrivenLambda && root.contains(QStringLiteral("presetDrivenLambda"))) m_presetDrivenLambda->setValue(root.value(QStringLiteral("presetDrivenLambda")).toDouble(m_presetDrivenLambda->value()));
    if (m_presetVOpeningDeg && root.contains(QStringLiteral("presetVOpeningDeg"))) m_presetVOpeningDeg->setValue(root.value(QStringLiteral("presetVOpeningDeg")).toDouble(m_presetVOpeningDeg->value()));
    if (m_presetFoldedSpacingLambda && root.contains(QStringLiteral("presetFoldedSpacingLambda"))) m_presetFoldedSpacingLambda->setValue(root.value(QStringLiteral("presetFoldedSpacingLambda")).toDouble(m_presetFoldedSpacingLambda->value()));
    if (m_presetLoopPerimeterLambda && root.contains(QStringLiteral("presetLoopPerimeterLambda"))) m_presetLoopPerimeterLambda->setValue(root.value(QStringLiteral("presetLoopPerimeterLambda")).toDouble(m_presetLoopPerimeterLambda->value()));
    if (m_presetYagiReflectorLambda && root.contains(QStringLiteral("presetYagiReflectorLambda"))) m_presetYagiReflectorLambda->setValue(root.value(QStringLiteral("presetYagiReflectorLambda")).toDouble(m_presetYagiReflectorLambda->value()));
    if (m_presetYagiDirectorLambda && root.contains(QStringLiteral("presetYagiDirectorLambda"))) m_presetYagiDirectorLambda->setValue(root.value(QStringLiteral("presetYagiDirectorLambda")).toDouble(m_presetYagiDirectorLambda->value()));
    if (m_presetYagiReflectorSpacingLambda && root.contains(QStringLiteral("presetYagiReflectorSpacingLambda"))) m_presetYagiReflectorSpacingLambda->setValue(root.value(QStringLiteral("presetYagiReflectorSpacingLambda")).toDouble(m_presetYagiReflectorSpacingLambda->value()));
    if (m_presetYagiDirectorSpacingLambda && root.contains(QStringLiteral("presetYagiDirectorSpacingLambda"))) m_presetYagiDirectorSpacingLambda->setValue(root.value(QStringLiteral("presetYagiDirectorSpacingLambda")).toDouble(m_presetYagiDirectorSpacingLambda->value()));
    if (m_presetYagiDirectorCount && root.contains(QStringLiteral("presetYagiDirectorCount"))) m_presetYagiDirectorCount->setValue(root.value(QStringLiteral("presetYagiDirectorCount")).toInt(m_presetYagiDirectorCount->value()));
    if (m_presetYagiDirectorPitchLambda && root.contains(QStringLiteral("presetYagiDirectorPitchLambda"))) m_presetYagiDirectorPitchLambda->setValue(root.value(QStringLiteral("presetYagiDirectorPitchLambda")).toDouble(m_presetYagiDirectorPitchLambda->value()));
    if (m_presetYagiDirectorTaperLambda && root.contains(QStringLiteral("presetYagiDirectorTaperLambda"))) m_presetYagiDirectorTaperLambda->setValue(root.value(QStringLiteral("presetYagiDirectorTaperLambda")).toDouble(m_presetYagiDirectorTaperLambda->value()));
    if (m_presetHelixRadiusLambda && root.contains(QStringLiteral("presetHelixRadiusLambda"))) m_presetHelixRadiusLambda->setValue(root.value(QStringLiteral("presetHelixRadiusLambda")).toDouble(m_presetHelixRadiusLambda->value()));
    if (m_presetHelixPitchLambda && root.contains(QStringLiteral("presetHelixPitchLambda"))) m_presetHelixPitchLambda->setValue(root.value(QStringLiteral("presetHelixPitchLambda")).toDouble(m_presetHelixPitchLambda->value()));
    if (m_presetHelixTurns && root.contains(QStringLiteral("presetHelixTurns"))) m_presetHelixTurns->setValue(root.value(QStringLiteral("presetHelixTurns")).toDouble(m_presetHelixTurns->value()));
    if (m_presetPatchEr && root.contains(QStringLiteral("presetPatchEr"))) m_presetPatchEr->setValue(root.value(QStringLiteral("presetPatchEr")).toDouble(m_presetPatchEr->value()));
    if (m_presetPatchHeightMm && root.contains(QStringLiteral("presetPatchHeightMm"))) m_presetPatchHeightMm->setValue(root.value(QStringLiteral("presetPatchHeightMm")).toDouble(m_presetPatchHeightMm->value()));
    if (m_presetPatchTanD && root.contains(QStringLiteral("presetPatchTanD"))) m_presetPatchTanD->setValue(root.value(QStringLiteral("presetPatchTanD")).toDouble(m_presetPatchTanD->value()));
    if (m_presetPatchLineZ0 && root.contains(QStringLiteral("presetPatchLineZ0"))) m_presetPatchLineZ0->setValue(root.value(QStringLiteral("presetPatchLineZ0")).toDouble(m_presetPatchLineZ0->value()));
    if (m_presetPatchEdgeResistance && root.contains(QStringLiteral("presetPatchEdgeResistance"))) m_presetPatchEdgeResistance->setValue(root.value(QStringLiteral("presetPatchEdgeResistance")).toDouble(m_presetPatchEdgeResistance->value()));
    if (m_presetPatchNotchGapMm && root.contains(QStringLiteral("presetPatchNotchGapMm"))) m_presetPatchNotchGapMm->setValue(root.value(QStringLiteral("presetPatchNotchGapMm")).toDouble(m_presetPatchNotchGapMm->value()));
    if (m_primitiveType && root.contains(QStringLiteral("primitiveType"))) m_primitiveType->setCurrentIndex(std::clamp(root.value(QStringLiteral("primitiveType")).toInt(0),0,m_primitiveType->count()-1));
    if (m_primitiveOrientation && root.contains(QStringLiteral("primitiveOrientation"))) m_primitiveOrientation->setCurrentIndex(std::clamp(root.value(QStringLiteral("primitiveOrientation")).toInt(0),0,m_primitiveOrientation->count()-1));
    if (m_primitiveOriginX && root.contains(QStringLiteral("primitiveOriginX"))) m_primitiveOriginX->setValue(root.value(QStringLiteral("primitiveOriginX")).toDouble(m_primitiveOriginX->value()));
    if (m_primitiveOriginY && root.contains(QStringLiteral("primitiveOriginY"))) m_primitiveOriginY->setValue(root.value(QStringLiteral("primitiveOriginY")).toDouble(m_primitiveOriginY->value()));
    if (m_primitiveOriginZ && root.contains(QStringLiteral("primitiveOriginZ"))) m_primitiveOriginZ->setValue(root.value(QStringLiteral("primitiveOriginZ")).toDouble(m_primitiveOriginZ->value()));
    if (m_primitiveYawDeg && root.contains(QStringLiteral("primitiveYawDeg"))) m_primitiveYawDeg->setValue(root.value(QStringLiteral("primitiveYawDeg")).toDouble(m_primitiveYawDeg->value()));
    if (m_primitivePitchDeg && root.contains(QStringLiteral("primitivePitchDeg"))) m_primitivePitchDeg->setValue(root.value(QStringLiteral("primitivePitchDeg")).toDouble(m_primitivePitchDeg->value()));
    if (m_primitiveRollDeg && root.contains(QStringLiteral("primitiveRollDeg"))) m_primitiveRollDeg->setValue(root.value(QStringLiteral("primitiveRollDeg")).toDouble(m_primitiveRollDeg->value()));
    if (m_primitiveRadiusM && root.contains(QStringLiteral("primitiveRadiusM"))) m_primitiveRadiusM->setValue(root.value(QStringLiteral("primitiveRadiusM")).toDouble(m_primitiveRadiusM->value()));
    if (m_primitiveInnerRadiusM && root.contains(QStringLiteral("primitiveInnerRadiusM"))) m_primitiveInnerRadiusM->setValue(root.value(QStringLiteral("primitiveInnerRadiusM")).toDouble(m_primitiveInnerRadiusM->value()));
    if (m_primitiveWidthM && root.contains(QStringLiteral("primitiveWidthM"))) m_primitiveWidthM->setValue(root.value(QStringLiteral("primitiveWidthM")).toDouble(m_primitiveWidthM->value()));
    if (m_primitiveHeightM && root.contains(QStringLiteral("primitiveHeightM"))) m_primitiveHeightM->setValue(root.value(QStringLiteral("primitiveHeightM")).toDouble(m_primitiveHeightM->value()));
    if (m_primitiveFocalLengthM && root.contains(QStringLiteral("primitiveFocalLengthM"))) m_primitiveFocalLengthM->setValue(root.value(QStringLiteral("primitiveFocalLengthM")).toDouble(m_primitiveFocalLengthM->value()));
    if (m_primitivePitchM && root.contains(QStringLiteral("primitivePitchM"))) m_primitivePitchM->setValue(root.value(QStringLiteral("primitivePitchM")).toDouble(m_primitivePitchM->value()));
    if (m_primitiveTurns && root.contains(QStringLiteral("primitiveTurns"))) m_primitiveTurns->setValue(root.value(QStringLiteral("primitiveTurns")).toDouble(m_primitiveTurns->value()));
    if (m_primitiveStartDeg && root.contains(QStringLiteral("primitiveStartDeg"))) m_primitiveStartDeg->setValue(root.value(QStringLiteral("primitiveStartDeg")).toDouble(m_primitiveStartDeg->value()));
    if (m_primitiveSweepDeg && root.contains(QStringLiteral("primitiveSweepDeg"))) m_primitiveSweepDeg->setValue(root.value(QStringLiteral("primitiveSweepDeg")).toDouble(m_primitiveSweepDeg->value()));
    if (m_primitiveSegments && root.contains(QStringLiteral("primitiveSegments"))) m_primitiveSegments->setValue(root.value(QStringLiteral("primitiveSegments")).toInt(m_primitiveSegments->value()));
    if (m_primitiveGridSpacingM && root.contains(QStringLiteral("primitiveGridSpacingM"))) m_primitiveGridSpacingM->setValue(root.value(QStringLiteral("primitiveGridSpacingM")).toDouble(m_primitiveGridSpacingM->value()));
    if (m_primitiveThicknessM && root.contains(QStringLiteral("primitiveThicknessM"))) m_primitiveThicknessM->setValue(root.value(QStringLiteral("primitiveThicknessM")).toDouble(m_primitiveThicknessM->value()));
    if (m_primitiveDielectricEr && root.contains(QStringLiteral("primitiveDielectricEr"))) m_primitiveDielectricEr->setValue(root.value(QStringLiteral("primitiveDielectricEr")).toDouble(m_primitiveDielectricEr->value()));
    if (m_primitiveDielectricTanD && root.contains(QStringLiteral("primitiveDielectricTanD"))) m_primitiveDielectricTanD->setValue(root.value(QStringLiteral("primitiveDielectricTanD")).toDouble(m_primitiveDielectricTanD->value()));
    if (m_primitiveDielectricFill && root.contains(QStringLiteral("primitiveDielectricFill"))) m_primitiveDielectricFill->setValue(root.value(QStringLiteral("primitiveDielectricFill")).toDouble(m_primitiveDielectricFill->value()));
    if (m_primitiveAddFeed && root.contains(QStringLiteral("primitiveAddFeed"))) m_primitiveAddFeed->setChecked(root.value(QStringLiteral("primitiveAddFeed")).toBool(false));
    if (m_primitiveClearFirst && root.contains(QStringLiteral("primitiveClearFirst"))) m_primitiveClearFirst->setChecked(root.value(QStringLiteral("primitiveClearFirst")).toBool(false));
    if (m_editPlane && root.contains(QStringLiteral("editorPlane"))) m_editPlane->setCurrentIndex(std::clamp(root.value(QStringLiteral("editorPlane")).toInt(0),0,2));
    if (m_activePlaneCoordinateM && root.contains(QStringLiteral("editorPlaneCoordinateM"))) m_activePlaneCoordinateM->setValue(root.value(QStringLiteral("editorPlaneCoordinateM")).toDouble(0.0));
    m_constructionPlaneRotXDeg=root.value(QStringLiteral("constructionPlaneRotXDeg")).toDouble(0.0);
    m_constructionPlaneRotYDeg=root.value(QStringLiteral("constructionPlaneRotYDeg")).toDouble(0.0);
    m_constructionPlaneRotZDeg=root.value(QStringLiteral("constructionPlaneRotZDeg")).toDouble(0.0);
    if(m_geometry3D)m_geometry3D->setConstructionPlaneOrientation(m_constructionPlaneRotXDeg,m_constructionPlaneRotYDeg,m_constructionPlaneRotZDeg);
    if (m_optTargetMHz && root.contains(QStringLiteral("optimizationTargetMHz"))) m_optTargetMHz->setValue(root.value(QStringLiteral("optimizationTargetMHz")).toDouble(m_optTargetMHz->value()));
    if (m_optVariable && root.contains(QStringLiteral("optimizationVariable"))) m_optVariable->setCurrentIndex(std::clamp(root.value(QStringLiteral("optimizationVariable")).toInt(0),0,m_optVariable->count()-1));
    if (m_optObjective && root.contains(QStringLiteral("optimizationObjective"))) m_optObjective->setCurrentIndex(std::clamp(root.value(QStringLiteral("optimizationObjective")).toInt(0),0,m_optObjective->count()-1));
    if (m_optMinFactor && root.contains(QStringLiteral("optimizationMinFactor"))) m_optMinFactor->setValue(root.value(QStringLiteral("optimizationMinFactor")).toDouble(m_optMinFactor->value()));
    if (m_optMaxFactor && root.contains(QStringLiteral("optimizationMaxFactor"))) m_optMaxFactor->setValue(root.value(QStringLiteral("optimizationMaxFactor")).toDouble(m_optMaxFactor->value()));
    if (m_optCoarseSamples && root.contains(QStringLiteral("optimizationCoarseSamples"))) m_optCoarseSamples->setValue(root.value(QStringLiteral("optimizationCoarseSamples")).toInt(m_optCoarseSamples->value()));
    if (m_optRefineIterations && root.contains(QStringLiteral("optimizationRefineIterations"))) m_optRefineIterations->setValue(root.value(QStringLiteral("optimizationRefineIterations")).toInt(m_optRefineIterations->value()));
    if (m_multiTargetMHz && root.contains(QStringLiteral("multiOptimizationTargetMHz"))) m_multiTargetMHz->setValue(root.value(QStringLiteral("multiOptimizationTargetMHz")).toDouble(m_multiTargetMHz->value()));
    if (m_multiDrivenLength && root.contains(QStringLiteral("multiOptimizationDrivenLength"))) m_multiDrivenLength->setChecked(root.value(QStringLiteral("multiOptimizationDrivenLength")).toBool(true));
    if (m_multiReflectorLength && root.contains(QStringLiteral("multiOptimizationReflectorLength"))) m_multiReflectorLength->setChecked(root.value(QStringLiteral("multiOptimizationReflectorLength")).toBool(true));
    if (m_multiDirectorLength && root.contains(QStringLiteral("multiOptimizationDirectorLength"))) m_multiDirectorLength->setChecked(root.value(QStringLiteral("multiOptimizationDirectorLength")).toBool(true));
    if (m_multiReflectorSpacing && root.contains(QStringLiteral("multiOptimizationReflectorSpacing"))) m_multiReflectorSpacing->setChecked(root.value(QStringLiteral("multiOptimizationReflectorSpacing")).toBool(true));
    if (m_multiDirectorSpacing && root.contains(QStringLiteral("multiOptimizationDirectorSpacing"))) m_multiDirectorSpacing->setChecked(root.value(QStringLiteral("multiOptimizationDirectorSpacing")).toBool(true));
    if (m_multiMinFactor && root.contains(QStringLiteral("multiOptimizationMinFactor"))) m_multiMinFactor->setValue(root.value(QStringLiteral("multiOptimizationMinFactor")).toDouble(m_multiMinFactor->value()));
    if (m_multiMaxFactor && root.contains(QStringLiteral("multiOptimizationMaxFactor"))) m_multiMaxFactor->setValue(root.value(QStringLiteral("multiOptimizationMaxFactor")).toDouble(m_multiMaxFactor->value()));
    if (m_multiSamplesPerVariable && root.contains(QStringLiteral("multiOptimizationSamplesPerVariable"))) m_multiSamplesPerVariable->setValue(root.value(QStringLiteral("multiOptimizationSamplesPerVariable")).toInt(m_multiSamplesPerVariable->value()));
    if (m_multiPasses && root.contains(QStringLiteral("multiOptimizationPasses"))) m_multiPasses->setValue(root.value(QStringLiteral("multiOptimizationPasses")).toInt(m_multiPasses->value()));
    if (m_multiWeightMatch && root.contains(QStringLiteral("multiOptimizationWeightMatch"))) m_multiWeightMatch->setValue(root.value(QStringLiteral("multiOptimizationWeightMatch")).toDouble(m_multiWeightMatch->value()));
    if (m_multiWeightDirectivity && root.contains(QStringLiteral("multiOptimizationWeightDirectivity"))) m_multiWeightDirectivity->setValue(root.value(QStringLiteral("multiOptimizationWeightDirectivity")).toDouble(m_multiWeightDirectivity->value()));
    if (m_multiWeightFrontBack && root.contains(QStringLiteral("multiOptimizationWeightFrontBack"))) m_multiWeightFrontBack->setValue(root.value(QStringLiteral("multiOptimizationWeightFrontBack")).toDouble(m_multiWeightFrontBack->value()));
    if (m_multiWeightBandwidth && root.contains(QStringLiteral("multiOptimizationWeightBandwidth"))) m_multiWeightBandwidth->setValue(root.value(QStringLiteral("multiOptimizationWeightBandwidth")).toDouble(m_multiWeightBandwidth->value()));
    if (m_multiBandwidthHalfSpanPct && root.contains(QStringLiteral("multiOptimizationBandwidthHalfSpanPct"))) m_multiBandwidthHalfSpanPct->setValue(root.value(QStringLiteral("multiOptimizationBandwidthHalfSpanPct")).toDouble(m_multiBandwidthHalfSpanPct->value()));
    if (m_multiBandwidthSamples && root.contains(QStringLiteral("multiOptimizationBandwidthSamples"))) m_multiBandwidthSamples->setValue(root.value(QStringLiteral("multiOptimizationBandwidthSamples")).toInt(m_multiBandwidthSamples->value()));
    if (m_multiBandwidthAggregation && root.contains(QStringLiteral("multiOptimizationBandwidthAggregation"))) m_multiBandwidthAggregation->setCurrentIndex(std::clamp(root.value(QStringLiteral("multiOptimizationBandwidthAggregation")).toInt(0),0,m_multiBandwidthAggregation->count()-1));
    if (m_multiMinElementSpacingLambda && root.contains(QStringLiteral("multiOptimizationMinElementSpacingLambda"))) m_multiMinElementSpacingLambda->setValue(root.value(QStringLiteral("multiOptimizationMinElementSpacingLambda")).toDouble(m_multiMinElementSpacingLambda->value()));
    if (m_multiMaxBoomLengthLambda && root.contains(QStringLiteral("multiOptimizationMaxBoomLengthLambda"))) m_multiMaxBoomLengthLambda->setValue(root.value(QStringLiteral("multiOptimizationMaxBoomLengthLambda")).toDouble(m_multiMaxBoomLengthLambda->value()));
    if (m_presetCombo && root.contains(QStringLiteral("presetIndex"))) m_presetCombo->setCurrentIndex(std::clamp(root.value(QStringLiteral("presetIndex")).toInt(0),0,m_presetCombo->count()-1));
    rebuildScene(true);
    // Loading replaces the complete geometry, so refresh the explicit 3D camera frame once.
    // Ordinary edits and drags deliberately keep the current frame stable.
    if (m_geometry3D) m_geometry3D->fitGeometry();
    if (m_hybridPortFeed && root.contains(QStringLiteral("hybridPortFeed")))
    { const int idx=m_hybridPortFeed->findText(root.value(QStringLiteral("hybridPortFeed")).toString()); if(idx>=0)m_hybridPortFeed->setCurrentIndex(idx); }
    if (m_hybridJunctionFeed && root.contains(QStringLiteral("hybridJunctionFeed")))
    { const QString name=root.value(QStringLiteral("hybridJunctionFeed")).toString(); const int idx=name.isEmpty()?0:m_hybridJunctionFeed->findText(name); if(idx>=0)m_hybridJunctionFeed->setCurrentIndex(idx); }
    auto restoreSurfaceData=[](QComboBox *combo,const QJsonObject &obj,const QString &key){if(!combo||!obj.contains(key))return;const int wanted=obj.value(key).toInt(-1);for(int i=0;i<combo->count();++i)if(combo->itemData(i).toInt()==wanted){combo->setCurrentIndex(i);break;}};
    restoreSurfaceData(m_hybridPortSurface,root,QStringLiteral("hybridPortSurface"));
    restoreSurfaceData(m_hybridJunctionSurface,root,QStringLiteral("hybridJunctionSurface"));
    if (m_sweepFeed && root.contains(QStringLiteral("sweepFeedIndex")) && m_sweepFeed->count() > 0)
        m_sweepFeed->setCurrentIndex(std::clamp(root.value(QStringLiteral("sweepFeedIndex")).toInt(0), 0, m_sweepFeed->count() - 1));
    if (m_sweepExcitationMode && root.contains(QStringLiteral("sweepExcitationMode")))
        m_sweepExcitationMode->setCurrentIndex(std::clamp(root.value(QStringLiteral("sweepExcitationMode")).toInt(0), 0, m_sweepExcitationMode->count() - 1));
    if (m_sweepSolver && root.contains(QStringLiteral("sweepSolverIndex")) && m_sweepSolver->count() > 0)
        m_sweepSolver->setCurrentIndex(std::clamp(root.value(QStringLiteral("sweepSolverIndex")).toInt(0), 0, m_sweepSolver->count() - 1));
    if (m_optFeed && root.contains(QStringLiteral("optimizationFeedIndex")) && m_optFeed->count() > 0)
        m_optFeed->setCurrentIndex(std::clamp(root.value(QStringLiteral("optimizationFeedIndex")).toInt(0), 0, m_optFeed->count() - 1));
    if (m_multiFeed && root.contains(QStringLiteral("multiOptimizationFeedIndex")) && m_multiFeed->count() > 0)
        m_multiFeed->setCurrentIndex(std::clamp(root.value(QStringLiteral("multiOptimizationFeedIndex")).toInt(0), 0, m_multiFeed->count() - 1));
    refreshYagiIndividualVariables();
    const QJsonArray individualVariables = root.value(QStringLiteral("individualDirectorVariables")).toArray();
    if (m_individualDirectorTable && !individualVariables.isEmpty())
    {
        const int rows = std::min(m_individualDirectorTable->rowCount(), static_cast<int>(individualVariables.size()));
        for (int r = 0; r < rows; ++r)
        {
            const QJsonObject o = individualVariables.at(r).toObject();
            if (auto *it = m_individualDirectorTable->item(r,1)) it->setCheckState(o.value(QStringLiteral("lengthFree")).toBool(true) ? Qt::Checked : Qt::Unchecked);
            if (auto *it = m_individualDirectorTable->item(r,4)) it->setCheckState(o.value(QStringLiteral("positionFree")).toBool(false) ? Qt::Checked : Qt::Unchecked);
            if (auto *w = qobject_cast<QDoubleSpinBox *>(m_individualDirectorTable->cellWidget(r,2))) w->setValue(o.value(QStringLiteral("lengthMin")).toDouble(w->value()));
            if (auto *w = qobject_cast<QDoubleSpinBox *>(m_individualDirectorTable->cellWidget(r,3))) w->setValue(o.value(QStringLiteral("lengthMax")).toDouble(w->value()));
            if (auto *w = qobject_cast<QDoubleSpinBox *>(m_individualDirectorTable->cellWidget(r,5))) w->setValue(o.value(QStringLiteral("positionMin")).toDouble(w->value()));
            if (auto *w = qobject_cast<QDoubleSpinBox *>(m_individualDirectorTable->cellWidget(r,6))) w->setValue(o.value(QStringLiteral("positionMax")).toDouble(w->value()));
        }
    }
    if (!migrationNotice.isEmpty())
        QMessageBox::information(this, QStringLiteral("Antenna project migrated"), migrationNotice);
}

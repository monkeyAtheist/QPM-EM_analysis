#include "widgets/electrostatic_canvas.h"
#include "field_visualization_utils.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QFontMetrics>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace
{
QColor chargeColor(double q)
{
    if (q > 0.0) return QColor(220, 70, 65);
    if (q < 0.0) return QColor(65, 115, 225);
    return QColor(140, 140, 140);
}
}

ElectrostaticCanvas::ElectrostaticCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(650, 500);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void ElectrostaticCanvas::setModel(ElectrostaticModel *model)
{
    if (m_model == model) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model)
    {
        connect(m_model, &ElectrostaticModel::changed, this, [this] {
            invalidateVisualization();
            update();
        });
    }
    invalidateVisualization();
    update();
}

void ElectrostaticCanvas::setSelectedSource(int index)
{
    const int maxIndex = m_model ? m_model->sourceCount() - 1 : -1;
    index = std::clamp(index, -1, maxIndex);
    if (m_selectedSource == index) return;
    m_selectedSource = index;
    emit selectedSourceChanged(index);
    update();
}

void ElectrostaticCanvas::setMeasurementPoint(const ElectrostaticVec3 &point)
{
    m_measurementPoint = point;
    invalidateVisualization();
    update();
}

void ElectrostaticCanvas::setViewPlane(FieldViewPlane plane)
{
    if (m_viewPlane == plane) return;
    m_viewPlane = plane;
    invalidateVisualization();
    centerView();
}

void ElectrostaticCanvas::setPixelsPerMeter(double value)
{
    m_pixelsPerMeter = std::clamp(value, 15.0, 1200.0);
    invalidateVisualization();
    update();
}

void ElectrostaticCanvas::centerView()
{
    m_viewCenterWorld = {0.0, 0.0};
    m_pixelsPerMeter = 90.0;
    invalidateVisualization();
    update();
}

void ElectrostaticCanvas::setShowFieldVectors(bool enabled)
{
    if (m_showFieldVectors == enabled) return;
    m_showFieldVectors = enabled;
    update();
}

void ElectrostaticCanvas::setScalarMap(ScalarMap mode)
{
    if (m_scalarMap == mode) return;
    m_scalarMap = mode;
    invalidateVisualization();
    update();
}

void ElectrostaticCanvas::setShowContours(bool enabled)
{
    if (m_showContours == enabled) return;
    m_showContours = enabled;
    invalidateVisualization();
    update();
}

void ElectrostaticCanvas::setShowFieldLines(bool enabled)
{
    if (m_showFieldLines == enabled) return;
    m_showFieldLines = enabled;
    invalidateVisualization();
    update();
}

void ElectrostaticCanvas::setProbeSegment(const ElectrostaticVec3 &pointA, const ElectrostaticVec3 &pointB, bool enabled)
{
    m_probeA = pointA;
    m_probeB = pointB;
    m_probeVisible = enabled;
    update();
}

void ElectrostaticCanvas::setProbeSegmentVisible(bool enabled)
{
    if (m_probeVisible == enabled)
        return;
    m_probeVisible = enabled;
    update();
}

void ElectrostaticCanvas::setPointProbes(const QVector<ElectrostaticVec3> &positions, const QStringList &names, int selectedIndex)
{
    m_pointProbes = positions;
    m_pointProbeNames = names;
    while (m_pointProbeNames.size() < m_pointProbes.size())
        m_pointProbeNames.push_back(QStringLiteral("P%1").arg(m_pointProbeNames.size() + 1));
    if (m_pointProbeNames.size() > m_pointProbes.size())
        m_pointProbeNames = m_pointProbeNames.mid(0, m_pointProbes.size());
    const int maxProbeIndex = int(m_pointProbes.size()) - 1;
    m_selectedPointProbe = m_pointProbes.isEmpty() ? -1 : std::clamp(selectedIndex, -1, maxProbeIndex);
    update();
}

void ElectrostaticCanvas::setPointProbesVisible(bool enabled)
{
    if (m_pointProbesVisible == enabled)
        return;
    m_pointProbesVisible = enabled;
    update();
}

QPointF ElectrostaticCanvas::projectPoint(const ElectrostaticVec3 &p) const
{
    switch (m_viewPlane)
    {
    case FieldViewPlane::XY: return {p.x, p.y};
    case FieldViewPlane::XZ: return {p.x, p.z};
    case FieldViewPlane::YZ: return {p.y, p.z};
    }
    return {p.x, p.y};
}

QPointF ElectrostaticCanvas::projectVector(const ElectrostaticVec3 &v) const
{
    return projectPoint(v);
}

ElectrostaticVec3 ElectrostaticCanvas::unprojectPoint(const QPointF &p, const ElectrostaticVec3 &base) const
{
    ElectrostaticVec3 out = base;
    switch (m_viewPlane)
    {
    case FieldViewPlane::XY: out.x = p.x(); out.y = p.y(); break;
    case FieldViewPlane::XZ: out.x = p.x(); out.z = p.y(); break;
    case FieldViewPlane::YZ: out.y = p.x(); out.z = p.y(); break;
    }
    return out;
}

ElectrostaticVec3 ElectrostaticCanvas::samplePointForPlane(const QPointF &p) const
{
    return unprojectPoint(p, m_measurementPoint);
}

QPointF ElectrostaticCanvas::worldToScreen(const QPointF &p) const
{
    return {width() * 0.5 + (p.x() - m_viewCenterWorld.x()) * m_pixelsPerMeter,
            height() * 0.5 - (p.y() - m_viewCenterWorld.y()) * m_pixelsPerMeter};
}

QPointF ElectrostaticCanvas::screenToWorld(const QPointF &p) const
{
    return {m_viewCenterWorld.x() + (p.x() - width() * 0.5) / m_pixelsPerMeter,
            m_viewCenterWorld.y() - (p.y() - height() * 0.5) / m_pixelsPerMeter};
}

QString ElectrostaticCanvas::horizontalAxisName() const
{
    return m_viewPlane == FieldViewPlane::YZ ? QStringLiteral("y") : QStringLiteral("x");
}

QString ElectrostaticCanvas::verticalAxisName() const
{
    return m_viewPlane == FieldViewPlane::XY ? QStringLiteral("y") : QStringLiteral("z");
}

void ElectrostaticCanvas::paintEvent(QPaintEvent *)
{
    if (m_cacheSize != size())
        invalidateVisualization();

    if (m_visualizationDirty && m_dragTarget != DragTarget::Source && !m_panning)
    {
        rebuildScalarCache();
        rebuildFieldLineCache();
        m_visualizationDirty = false;
        m_cacheSize = size();
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(248, 249, 251));
    if (!m_visualizationDirty) drawScalarHeatmap(painter);
    drawGrid(painter);
    if (!m_visualizationDirty)
    {
        drawContours(painter);
        drawFieldLines(painter);
    }
    if (m_showFieldVectors) drawFieldVectors(painter);
    drawSources(painter);
    drawProbeSegment(painter);
    drawPointProbes(painter);
    drawMeasurement(painter);
    if (!m_visualizationDirty) drawScalarLegend(painter);
    painter.setPen(QColor(80, 80, 80));
    painter.drawText(QRect(10, 8, width() - 20, 24), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("%1/%2 plane | left: M/select | drag source/A/B/Pn | middle: pan | wheel: zoom")
                         .arg(horizontalAxisName(), verticalAxisName()));
}

void ElectrostaticCanvas::invalidateVisualization()
{
    m_visualizationDirty = true;
}

QRectF ElectrostaticCanvas::visualizationRect() const
{
    return QRectF(0.0, 34.0, std::max(1, width()), std::max(1, height() - 56));
}

double ElectrostaticCanvas::scalarValueAt(const ElectrostaticVec3 &point, bool *valid) const
{
    if (valid) *valid = false;
    if (!m_model || m_scalarMap == ScalarMap::Off)
        return 0.0;
    const auto field = m_model->fieldAt(point);
    if (field.singular)
        return 0.0;

    if (m_scalarMap == ScalarMap::ElectricFieldMagnitude)
    {
        const double value = field.electricField.norm();
        if (valid) *valid = std::isfinite(value);
        return value;
    }

    if (!field.potentialDefined || !std::isfinite(field.potential))
        return 0.0;
    if (valid) *valid = true;
    return field.potential;
}

QString ElectrostaticCanvas::scalarUnit() const
{
    switch (m_scalarMap)
    {
    case ScalarMap::ElectricFieldMagnitude: return QStringLiteral("V/m");
    case ScalarMap::Potential: return QStringLiteral("V");
    case ScalarMap::Off: break;
    }
    return {};
}

QString ElectrostaticCanvas::scalarTitle() const
{
    switch (m_scalarMap)
    {
    case ScalarMap::ElectricFieldMagnitude: return QStringLiteral("|E|");
    case ScalarMap::Potential: return QStringLiteral("V");
    case ScalarMap::Off: break;
    }
    return {};
}

QColor ElectrostaticCanvas::sequentialColor(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    struct Stop { double p; QColor c; };
    const Stop stops[] = {
        {0.00, QColor(24, 42, 78)},
        {0.25, QColor(35, 103, 170)},
        {0.50, QColor(45, 177, 170)},
        {0.75, QColor(238, 205, 75)},
        {1.00, QColor(208, 62, 52)}
    };
    for (int i = 0; i < 4; ++i)
    {
        if (t <= stops[i + 1].p)
        {
            const double u = (t - stops[i].p) / (stops[i + 1].p - stops[i].p);
            return QColor(int(stops[i].c.red()   + (stops[i+1].c.red()   - stops[i].c.red())   * u),
                          int(stops[i].c.green() + (stops[i+1].c.green() - stops[i].c.green()) * u),
                          int(stops[i].c.blue()  + (stops[i+1].c.blue()  - stops[i].c.blue())  * u),
                          178);
        }
    }
    QColor c = stops[4].c; c.setAlpha(178); return c;
}

QColor ElectrostaticCanvas::divergingColor(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    const QColor low(45, 92, 180);
    const QColor mid(247, 247, 242);
    const QColor high(195, 55, 48);
    const QColor a = t < 0.5 ? low : mid;
    const QColor b = t < 0.5 ? mid : high;
    const double u = t < 0.5 ? t * 2.0 : (t - 0.5) * 2.0;
    return QColor(int(a.red() + (b.red() - a.red()) * u),
                  int(a.green() + (b.green() - a.green()) * u),
                  int(a.blue() + (b.blue() - a.blue()) * u), 178);
}

QString ElectrostaticCanvas::formatEngineering(double value, const QString &unit)
{
    if (!std::isfinite(value)) return QStringLiteral("n/a");
    if (std::abs(value) < 1e-30) return QStringLiteral("0 %1").arg(unit);
    const double a = std::abs(value);
    struct Prefix { double scale; const char *symbol; };
    const Prefix prefixes[] = {
        {1e12,"T"},{1e9,"G"},{1e6,"M"},{1e3,"k"},{1.0,""},
        {1e-3,"m"},{1e-6,"u"},{1e-9,"n"},{1e-12,"p"}
    };
    for (const auto &p : prefixes)
        if (a >= p.scale * 0.999 || p.scale == 1e-12)
            return QStringLiteral("%1 %2%3").arg(value / p.scale, 0, 'g', 4).arg(QString::fromLatin1(p.symbol), unit);
    return QStringLiteral("%1 %2").arg(value, 0, 'g', 4).arg(unit);
}

void ElectrostaticCanvas::rebuildScalarCache()
{
    m_scalarImage = {};
    m_contourLines.clear();
    m_scalarHasData = false;
    if (!m_model || m_model->sourceCount() == 0 || m_scalarMap == ScalarMap::Off)
        return;

    const QRectF area = visualizationRect();
    const int columns = std::clamp(int(area.width() / 18.0) + 1, 28, 64);
    const int rows = std::clamp(int(area.height() / 18.0) + 1, 20, 48);
    FieldVisualizationGrid grid;
    grid.columns = columns;
    grid.rows = rows;
    grid.screenRect = area;
    grid.values.resize(columns * rows);
    grid.valid.fill(0, columns * rows);

    QVector<double> transformed;
    QVector<double> rawAbsolute;
    transformed.reserve(columns * rows);
    rawAbsolute.reserve(columns * rows);
    m_scalarUsesLog = m_scalarMap == ScalarMap::ElectricFieldMagnitude;

    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            const QPointF screen = grid.screenPoint(column, row);
            const QPointF plane = screenToWorld(screen);
            bool valid = false;
            const double raw = scalarValueAt(samplePointForPlane(plane), &valid);
            const int index = grid.index(column, row);
            if (!valid || !std::isfinite(raw))
                continue;
            const double mapped = m_scalarUsesLog ? std::log10(std::max(std::abs(raw), 1e-30)) : raw;
            grid.values[index] = mapped;
            grid.valid[index] = 1;
            transformed.push_back(mapped);
            rawAbsolute.push_back(std::abs(raw));
        }
    }

    if (transformed.size() < 4)
        return;

    if (m_scalarMap == ScalarMap::Potential)
    {
        double limit = fieldVisualizationPercentile(rawAbsolute, 0.94);
        if (!std::isfinite(limit) || limit < 1e-18)
            limit = std::max(1e-18, fieldVisualizationPercentile(rawAbsolute, 1.0));
        m_scalarDisplayMin = -limit;
        m_scalarDisplayMax = limit;
    }
    else
    {
        m_scalarDisplayMin = fieldVisualizationPercentile(transformed, 0.05);
        m_scalarDisplayMax = fieldVisualizationPercentile(transformed, 0.95);
        if (m_scalarDisplayMax - m_scalarDisplayMin < 1e-9)
            m_scalarDisplayMax = m_scalarDisplayMin + 1.0;
    }

    m_scalarImage = QImage(columns, rows, QImage::Format_ARGB32_Premultiplied);
    m_scalarImage.fill(Qt::transparent);
    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            if (!grid.isValid(column, row))
                continue;
            const double v = grid.value(column, row);
            const double t = std::clamp((v - m_scalarDisplayMin) / (m_scalarDisplayMax - m_scalarDisplayMin), 0.0, 1.0);
            m_scalarImage.setPixelColor(column, row,
                m_scalarMap == ScalarMap::Potential ? divergingColor(t) : sequentialColor(t));
        }
    }

    if (m_showContours)
    {
        QVector<double> levels;
        const int count = 9;
        for (int i = 1; i <= count; ++i)
        {
            const double f = double(i) / double(count + 1);
            levels.push_back(m_scalarDisplayMin + (m_scalarDisplayMax - m_scalarDisplayMin) * f);
        }
        if (m_scalarMap == ScalarMap::Potential && m_scalarDisplayMin < 0.0 && m_scalarDisplayMax > 0.0)
            levels.push_back(0.0);
        m_contourLines = fieldVisualizationContours(grid, levels);
    }
    m_scalarHasData = true;
}

void ElectrostaticCanvas::rebuildFieldLineCache()
{
    m_fieldLinePaths.clear();
    if (!m_showFieldLines || !m_model || m_model->sourceCount() == 0)
        return;

    const QRectF area = visualizationRect();
    const QPointF a = screenToWorld(area.topLeft());
    const QPointF b = screenToWorld(area.bottomRight());
    const QRectF worldBounds(QPointF(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                             QPointF(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
    const double step = std::clamp(9.0 / m_pixelsPerMeter, 0.002, 1.0);
    const int totalSources = std::max(1, m_model->sourceCount());
    const int seedsPerSource = std::clamp(18 / totalSources, 4, 8);

    QVector<QPointF> seeds;
    for (int i = 0; i < m_model->sourceCount(); ++i)
    {
        const auto *source = m_model->source(i);
        if (!source) continue;
        const QPointF center = projectPoint(source->position);
        double sourceRadius = 18.0 / m_pixelsPerMeter;
        if (source->type == ElectrostaticSource::Type::SolidSphere ||
            source->type == ElectrostaticSource::Type::SphericalShell ||
            source->type == ElectrostaticSource::Type::ThickSphericalShell ||
            source->type == ElectrostaticSource::Type::InfiniteCylinderZ ||
            source->type == ElectrostaticSource::Type::InfiniteHollowCylinderZ ||
            source->type == ElectrostaticSource::Type::CircularPlate ||
            source->type == ElectrostaticSource::Type::AnnularPlate)
            sourceRadius = std::max(sourceRadius, source->radius * 1.08);
        else if (source->type == ElectrostaticSource::Type::FiniteLine)
            sourceRadius = std::max(sourceRadius, source->length * 0.18);
        else if (source->type == ElectrostaticSource::Type::RectangularPlate ||
                 source->type == ElectrostaticSource::Type::RectangularVolume ||
                 source->type == ElectrostaticSource::Type::TriangularPlate)
            sourceRadius = std::max(sourceRadius, std::max(source->width, source->height) * 0.18);
        for (int k = 0; k < seedsPerSource; ++k)
        {
            const double angle = 2.0 * 3.14159265358979323846 * double(k) / double(seedsPerSource);
            seeds.push_back(center + QPointF(std::cos(angle), std::sin(angle)) * sourceRadius);
        }
    }

    const auto trace = [this, worldBounds, step](const QPointF &seed, double sign) {
        QPainterPath path;
        QPointF p = seed;
        path.moveTo(worldToScreen(p));
        for (int iteration = 0; iteration < 130; ++iteration)
        {
            if (!worldBounds.adjusted(-step, -step, step, step).contains(p)) break;
            const auto field = m_model->fieldAt(samplePointForPlane(p));
            if (field.singular) break;
            QPointF v = projectVector(field.electricField);
            double n = std::hypot(v.x(), v.y());
            if (!std::isfinite(n) || n < 1e-30) break;
            v /= n;
            const QPointF mid = p + v * (0.5 * step * sign);
            const auto midField = m_model->fieldAt(samplePointForPlane(mid));
            if (midField.singular) break;
            QPointF vm = projectVector(midField.electricField);
            const double nm = std::hypot(vm.x(), vm.y());
            if (!std::isfinite(nm) || nm < 1e-30) break;
            vm /= nm;
            const QPointF next = p + vm * (step * sign);
            if (!worldBounds.adjusted(-step, -step, step, step).contains(next)) break;
            path.lineTo(worldToScreen(next));
            if (iteration > 24 && QLineF(next, seed).length() < step * 1.4) break;
            p = next;
        }
        return path;
    };

    for (const QPointF &seed : seeds)
    {
        const QPainterPath forward = trace(seed, +1.0);
        const QPainterPath backward = trace(seed, -1.0);
        if (forward.elementCount() > 2) m_fieldLinePaths.push_back(forward);
        if (backward.elementCount() > 2) m_fieldLinePaths.push_back(backward);
    }
}

void ElectrostaticCanvas::drawScalarHeatmap(QPainter &painter)
{
    if (!m_scalarHasData || m_scalarImage.isNull()) return;
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(visualizationRect(), m_scalarImage);
    painter.restore();
}

void ElectrostaticCanvas::drawContours(QPainter &painter)
{
    if (!m_showContours || m_contourLines.isEmpty()) return;
    painter.save();
    painter.setPen(QPen(QColor(32, 36, 42, 175), 1.0));
    painter.drawLines(m_contourLines);
    painter.restore();
}

void ElectrostaticCanvas::drawFieldLines(QPainter &painter)
{
    if (!m_showFieldLines || m_fieldLinePaths.isEmpty()) return;
    painter.save();
    painter.setPen(QPen(QColor(38, 115, 72, 190), 1.45));
    painter.setBrush(Qt::NoBrush);
    for (const auto &path : m_fieldLinePaths) painter.drawPath(path);
    painter.restore();
}

void ElectrostaticCanvas::drawScalarLegend(QPainter &painter)
{
    if (m_scalarMap == ScalarMap::Off) return;
    if (!m_scalarHasData)
    {
        const QRectF messageBox(width() - 328.0, 38.0, 316.0, 42.0);
        painter.save();
        painter.setPen(QPen(QColor(135, 92, 20, 180), 1.0));
        painter.setBrush(QColor(255, 250, 232, 225));
        painter.drawRoundedRect(messageBox, 4.0, 4.0);
        painter.setPen(QColor(105, 72, 18));
        painter.drawText(messageBox.adjusted(8, 4, -8, -4), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                         m_scalarMap == ScalarMap::Potential
                             ? QStringLiteral("Potential map unavailable: at least one source has no absolute-potential reference in this model.")
                             : QStringLiteral("Scalar map unavailable for the current slice."));
        painter.restore();
        return;
    }
    const QRectF box(width() - 228.0, 38.0, 216.0, 54.0);
    painter.save();
    painter.setPen(QPen(QColor(70, 73, 78, 150), 1.0));
    painter.setBrush(QColor(255,255,255,218));
    painter.drawRoundedRect(box, 4.0, 4.0);
    painter.setPen(QColor(45,45,48));
    const QString scaleNote = m_scalarUsesLog ? QStringLiteral(" (log scale)") : QString();
    painter.drawText(QRectF(box.left()+8, box.top()+3, box.width()-16, 16),
                     Qt::AlignLeft | Qt::AlignVCenter, scalarTitle() + scaleNote);
    const QRectF bar(box.left()+8, box.top()+21, box.width()-16, 10);
    const int steps = 96;
    for (int i=0;i<steps;++i)
    {
        const double t0=double(i)/double(steps);
        const QRectF r(bar.left()+bar.width()*t0, bar.top(), bar.width()/steps+1.0, bar.height());
        painter.fillRect(r, m_scalarMap == ScalarMap::Potential ? divergingColor(t0) : sequentialColor(t0));
    }
    const double rawMin = m_scalarUsesLog ? std::pow(10.0, m_scalarDisplayMin) : m_scalarDisplayMin;
    const double rawMax = m_scalarUsesLog ? std::pow(10.0, m_scalarDisplayMax) : m_scalarDisplayMax;
    painter.drawText(QRectF(box.left()+8,box.top()+33,box.width()/2-8,17), Qt::AlignLeft|Qt::AlignVCenter,
                     formatEngineering(rawMin,scalarUnit()));
    painter.drawText(QRectF(box.center().x(),box.top()+33,box.width()/2-8,17), Qt::AlignRight|Qt::AlignVCenter,
                     formatEngineering(rawMax,scalarUnit()));
    painter.restore();
}

void ElectrostaticCanvas::drawGrid(QPainter &painter)
{
    const double targetPixels = 70.0;
    const double rawStep = targetPixels / m_pixelsPerMeter;
    const double decade = std::pow(10.0, std::floor(std::log10(std::max(rawStep, 1e-12))));
    const double normalized = rawStep / decade;
    double nice = 1.0;
    if (normalized > 5.0) nice = 10.0;
    else if (normalized > 2.0) nice = 5.0;
    else if (normalized > 1.0) nice = 2.0;
    const double step = nice * decade;

    const QPointF tl = screenToWorld({0.0, 0.0});
    const QPointF br = screenToWorld({double(width()), double(height())});
    const double minX = std::min(tl.x(), br.x());
    const double maxX = std::max(tl.x(), br.x());
    const double minY = std::min(tl.y(), br.y());
    const double maxY = std::max(tl.y(), br.y());

    painter.setPen(QPen(QColor(222,225,230), 1.0));
    for (double x = std::floor(minX / step) * step; x <= maxX + step * 0.5; x += step)
    {
        const double sx = worldToScreen({x,0.0}).x();
        painter.drawLine(QPointF(sx,0), QPointF(sx,height()));
    }
    for (double y = std::floor(minY / step) * step; y <= maxY + step * 0.5; y += step)
    {
        const double sy = worldToScreen({0.0,y}).y();
        painter.drawLine(QPointF(0,sy), QPointF(width(),sy));
    }

    const QPointF origin = worldToScreen({0.0,0.0});
    painter.setPen(QPen(QColor(125,130,138),1.4));
    painter.drawLine(QPointF(0,origin.y()), QPointF(width(),origin.y()));
    painter.drawLine(QPointF(origin.x(),0), QPointF(origin.x(),height()));
    painter.setPen(QColor(90,90,95));
    painter.drawText(QPointF(width()-24, origin.y()-5), horizontalAxisName());
    painter.drawText(QPointF(origin.x()+6,42), verticalAxisName());
    painter.drawText(QPointF(12,height()-10), QStringLiteral("grid: %1 m").arg(step,0,'g',3));
}

void ElectrostaticCanvas::drawFieldVectors(QPainter &painter)
{
    if (!m_model || m_model->sourceCount() == 0) return;
    const int spacing = 72;
    painter.setPen(QPen(QColor(60,145,90,165),1.1));
    for (int sy = 65; sy < height()-25; sy += spacing)
    {
        for (int sx = 35; sx < width()-25; sx += spacing)
        {
            const QPointF plane = screenToWorld({double(sx),double(sy)});
            const auto field = m_model->fieldAt(samplePointForPlane(plane));
            const QPointF e = projectVector(field.electricField);
            const double mag = std::hypot(e.x(), e.y());
            if (!std::isfinite(mag) || mag <= 1e-30 || field.singular) continue;
            const double len = 14.0 + std::clamp(std::log10(mag + 1.0) * 2.2, 0.0, 12.0);
            const QPointF dir(e.x()/mag, -e.y()/mag);
            drawArrow(painter, {double(sx),double(sy)}, QPointF(double(sx),double(sy)) + dir*len);
        }
    }
}

void ElectrostaticCanvas::drawSources(QPainter &painter)
{
    if (!m_model) return;
    using Type = ElectrostaticSource::Type;
    for (int i=0; i<m_model->sourceCount(); ++i)
    {
        const auto *source = m_model->source(i);
        if (!source) continue;
        const QPointF center = worldToScreen(projectPoint(source->position));
        const QColor color = chargeColor(source->strength);
        painter.setPen(QPen(color.darker(135), i == m_selectedSource ? 3.0 : 1.7));
        painter.setBrush(QColor(color.red(),color.green(),color.blue(),75));

        if (source->type == Type::PointCharge)
        {
            painter.setBrush(color);
            painter.drawEllipse(center,10,10);
            painter.setPen(Qt::white);
            painter.drawText(QRectF(center.x()-9,center.y()-10,18,20),Qt::AlignCenter,
                             source->strength >= 0.0 ? QStringLiteral("+") : QStringLiteral("−"));
        }
        else if (source->type == Type::SolidSphere || source->type == Type::SphericalShell ||
                 source->type == Type::ThickSphericalShell)
        {
            const double r = std::max(10.0, source->radius*m_pixelsPerMeter);
            if (source->type == Type::SphericalShell) painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(center,r,r);
            if (source->type == Type::SolidSphere)
                painter.drawEllipse(center,r*0.30,r*0.30);
            else if (source->type == Type::ThickSphericalShell)
            {
                const double ri = std::clamp(source->innerRadius/source->radius,0.0,0.999)*r;
                painter.setBrush(palette().brush(QPalette::Base));
                painter.drawEllipse(center,ri,ri);
            }
        }
        else if (source->type == Type::InfiniteCylinderZ || source->type == Type::InfiniteHollowCylinderZ)
        {
            const double r = std::max(10.0, source->radius*m_pixelsPerMeter);
            if (m_viewPlane == FieldViewPlane::XY)
            {
                painter.drawEllipse(center,r,r);
                if (source->type == Type::InfiniteHollowCylinderZ)
                {
                    const double ri = std::clamp(source->innerRadius/source->radius,0.0,0.999)*r;
                    painter.setBrush(palette().brush(QPalette::Base));
                    painter.drawEllipse(center,ri,ri);
                }
                painter.drawLine(center+QPointF(-r*.7,-r*.7), center+QPointF(r*.7,r*.7));
                painter.drawLine(center+QPointF(-r*.7,r*.7), center+QPointF(r*.7,-r*.7));
            }
            else
            {
                painter.drawRect(QRectF(center.x()-r, 34.0, 2*r, height()-56.0));
                if (source->type == Type::InfiniteHollowCylinderZ)
                {
                    const double ri = std::clamp(source->innerRadius/source->radius,0.0,0.999)*r;
                    painter.setBrush(palette().brush(QPalette::Base));
                    painter.drawRect(QRectF(center.x()-ri, 34.0, 2*ri, height()-56.0));
                }
            }
        }
        else if (source->type == Type::InfiniteLine)
        {
            if (m_viewPlane == FieldViewPlane::XY)
            {
                painter.setBrush(color);
                painter.drawEllipse(center, 6.0, 6.0);
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(center, 11.0, 11.0);
            }
            else
            {
                painter.setPen(QPen(color, i==m_selectedSource ? 5.0 : 3.0));
                painter.drawLine(QPointF(center.x(),34.0), QPointF(center.x(),height()-22.0));
            }
        }
        else if (source->type == Type::FiniteLine)
        {
            if (m_viewPlane == FieldViewPlane::XY)
            {
                const double angle = qDegreesToRadians(source->angleDeg);
                const QPointF d(std::cos(angle)*source->length*m_pixelsPerMeter*.5,
                                -std::sin(angle)*source->length*m_pixelsPerMeter*.5);
                painter.setPen(QPen(color, i==m_selectedSource ? 6.0 : 4.0, Qt::SolidLine, Qt::RoundCap));
                painter.drawLine(center-d,center+d);
            }
            else
            {
                painter.setPen(QPen(color, i==m_selectedSource ? 5.0 : 3.0));
                painter.drawEllipse(center,6,6);
            }
        }
        else if (source->type == Type::RectangularPlate || source->type == Type::RectangularVolume || source->type == Type::TriangularPlate)
        {
            if (m_viewPlane == FieldViewPlane::XY)
            {
                painter.save(); painter.translate(center); painter.rotate(-source->angleDeg);
                if (source->type == Type::RectangularPlate || source->type == Type::RectangularVolume)
                {
                    if (source->type == Type::RectangularVolume)
                        painter.setBrush(QColor(color.red(),color.green(),color.blue(),105));
                    painter.drawRect(QRectF(-source->width*m_pixelsPerMeter*.5,
                                            -source->height*m_pixelsPerMeter*.5,
                                            source->width*m_pixelsPerMeter,
                                            source->height*m_pixelsPerMeter));
                }
                else
                {
                    const double w=source->width*m_pixelsPerMeter, h=source->height*m_pixelsPerMeter;
                    QPolygonF tri; tri << QPointF(-0.5*w,h/3.0) << QPointF(0.5*w,h/3.0) << QPointF(0.0,-2.0*h/3.0);
                    painter.drawPolygon(tri);
                }
                painter.restore();
            }
            else if (source->type == Type::RectangularVolume)
            {
                const double angle = qDegreesToRadians(source->angleDeg);
                const double halfHorizontal = 0.5 * m_pixelsPerMeter *
                    (m_viewPlane == FieldViewPlane::XZ
                        ? std::abs(source->width*std::cos(angle)) + std::abs(source->height*std::sin(angle))
                        : std::abs(source->width*std::sin(angle)) + std::abs(source->height*std::cos(angle)));
                const double halfThickness = 0.5 * source->thickness * m_pixelsPerMeter;
                painter.setBrush(QColor(color.red(),color.green(),color.blue(),105));
                painter.drawRect(QRectF(center.x()-halfHorizontal, center.y()-halfThickness,
                                        2.0*halfHorizontal, 2.0*halfThickness));
            }
            else
            {
                const double half = source->width*m_pixelsPerMeter*.5;
                painter.drawLine(center-QPointF(half,0), center+QPointF(half,0));
            }
        }
        else if (source->type == Type::CircularPlate || source->type == Type::AnnularPlate)
        {
            if (m_viewPlane == FieldViewPlane::XY)
            {
                const double r=std::max(8.0,source->radius*m_pixelsPerMeter);
                painter.drawEllipse(center,r,r);
                if(source->type==Type::AnnularPlate)
                {
                    const double ri=std::clamp(source->innerRadius/source->radius,0.0,0.999)*r;
                    painter.setBrush(palette().brush(QPalette::Base));
                    painter.drawEllipse(center,ri,ri);
                }
            }
            else
            {
                const double half=source->radius*m_pixelsPerMeter;
                painter.drawLine(center-QPointF(half,0),center+QPointF(half,0));
            }
        }
        else if (source->type == Type::InfinitePlane)
        {
            painter.save();
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(color, i==m_selectedSource?3.0:1.6, Qt::DashLine));
            if(m_viewPlane==FieldViewPlane::XY)
                painter.drawRect(rect().adjusted(12,42,-12,-28));
            else
                painter.drawLine(QPointF(8,center.y()),QPointF(width()-8,center.y()));
            painter.restore();
        }
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(center+QPointF(13,-13),source->name);
    }
}

void ElectrostaticCanvas::drawProbeSegment(QPainter &painter)
{
    if (!m_probeVisible)
        return;

    const QPointF a = worldToScreen(projectPoint(m_probeA));
    const QPointF b = worldToScreen(projectPoint(m_probeB));
    const QColor accent(245, 170, 55);

    painter.save();
    QPen pen(accent, 2.0, Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(a, b);

    painter.setBrush(accent);
    painter.setPen(QPen(QColor(80, 55, 20), 1.0));
    painter.drawEllipse(a, 6.0, 6.0);
    painter.drawEllipse(b, 6.0, 6.0);

    painter.setPen(accent);
    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(a + QPointF(9.0, -9.0), QStringLiteral("A"));
    painter.drawText(b + QPointF(9.0, -9.0), QStringLiteral("B"));
    painter.restore();
}

void ElectrostaticCanvas::drawPointProbes(QPainter &painter)
{
    if (!m_pointProbesVisible || m_pointProbes.isEmpty())
        return;

    painter.save();
    QFont labelFont = painter.font();
    labelFont.setBold(true);
    painter.setFont(labelFont);
    for (int i = 0; i < m_pointProbes.size(); ++i)
    {
        const QPointF p = worldToScreen(projectPoint(m_pointProbes[i]));
        const bool selected = i == m_selectedPointProbe;
        const QColor color = selected ? QColor(255, 205, 65) : QColor(40, 185, 205);
        painter.setPen(QPen(selected ? QColor(85, 65, 15) : QColor(20, 85, 95), selected ? 2.2 : 1.4));
        painter.setBrush(color);
        painter.drawEllipse(p, selected ? 7.0 : 5.5, selected ? 7.0 : 5.5);
        painter.drawLine(p + QPointF(-9.0, 0.0), p + QPointF(9.0, 0.0));
        painter.drawLine(p + QPointF(0.0, -9.0), p + QPointF(0.0, 9.0));
        painter.setPen(color);
        const QString name = i < m_pointProbeNames.size() && !m_pointProbeNames[i].isEmpty()
                                 ? m_pointProbeNames[i]
                                 : QStringLiteral("P%1").arg(i + 1);
        painter.drawText(p + QPointF(10.0, -9.0), name);
    }
    painter.restore();
}

int ElectrostaticCanvas::hitTestProbeEndpoint(const QPointF &screenPoint) const
{
    if (!m_probeVisible)
        return -1;
    const QPointF a = worldToScreen(projectPoint(m_probeA));
    const QPointF b = worldToScreen(projectPoint(m_probeB));
    if (std::hypot(screenPoint.x() - a.x(), screenPoint.y() - a.y()) <= 13.0) return 0;
    if (std::hypot(screenPoint.x() - b.x(), screenPoint.y() - b.y()) <= 13.0) return 1;
    return -1;
}

int ElectrostaticCanvas::hitTestPointProbe(const QPointF &screenPoint) const
{
    if (!m_pointProbesVisible)
        return -1;
    int best = -1;
    double bestDistance = 14.0;
    for (int i = 0; i < m_pointProbes.size(); ++i)
    {
        const QPointF p = worldToScreen(projectPoint(m_pointProbes[i]));
        const double d = std::hypot(screenPoint.x() - p.x(), screenPoint.y() - p.y());
        if (d < bestDistance)
        {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

void ElectrostaticCanvas::drawMeasurement(QPainter &painter)
{
    const QPointF p = worldToScreen(projectPoint(m_measurementPoint));
    painter.setPen(QPen(QColor(125,45,160),2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(p,7,7);
    painter.drawLine(p+QPointF(-12,0),p+QPointF(12,0));
    painter.drawLine(p+QPointF(0,-12),p+QPointF(0,12));
    painter.drawText(p+QPointF(10,20),QStringLiteral("M"));
    if (!m_model || m_model->sourceCount()==0) return;
    const auto field = m_model->fieldAt(m_measurementPoint);
    const QPointF e = projectVector(field.electricField);
    const double mag = std::hypot(e.x(),e.y());
    if (!std::isfinite(mag) || mag<=1e-30 || field.singular) return;
    const QPointF dir(e.x()/mag,-e.y()/mag);
    painter.setPen(QPen(QColor(125,45,160),2.4));
    drawArrow(painter,p,p+dir*48.0);
}

void ElectrostaticCanvas::drawArrow(QPainter &painter,const QPointF &start,const QPointF &end) const
{
    painter.drawLine(start,end);
    const QPointF v=end-start;
    const double len=std::hypot(v.x(),v.y());
    if (len<1.0) return;
    const QPointF u(v.x()/len,v.y()/len), n(-u.y(),u.x());
    const double head=5.0;
    painter.drawLine(end,end-u*head+n*head*.55);
    painter.drawLine(end,end-u*head-n*head*.55);
}

int ElectrostaticCanvas::hitTestSource(const QPointF &screenPoint) const
{
    if (!m_model) return -1;
    int best=-1; double bestDistance=26.0;
    for (int i=0;i<m_model->sourceCount();++i)
    {
        const auto *source=m_model->source(i); if(!source) continue;
        const QPointF center=worldToScreen(projectPoint(source->position));
        const double d=std::hypot(screenPoint.x()-center.x(),screenPoint.y()-center.y());
        if(d<bestDistance){best=i;bestDistance=d;}
    }
    return best;
}

void ElectrostaticCanvas::mousePressEvent(QMouseEvent *event)
{
    m_lastMousePos = event->position().toPoint();
    if (event->button() == Qt::MiddleButton)
    {
        m_panning = true;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    const int endpoint = hitTestProbeEndpoint(event->position());
    if (endpoint >= 0)
    {
        m_dragTarget = endpoint == 0 ? DragTarget::ProbeA : DragTarget::ProbeB;
        setCursor(Qt::SizeAllCursor);
        return;
    }

    const int pointProbe = hitTestPointProbe(event->position());
    if (pointProbe >= 0)
    {
        m_selectedPointProbe = pointProbe;
        m_draggedPointProbe = pointProbe;
        m_dragTarget = DragTarget::PointProbe;
        emit pointProbeSelected(pointProbe);
        setCursor(Qt::SizeAllCursor);
        update();
        return;
    }

    const int hit = hitTestSource(event->position());
    if (hit >= 0)
    {
        setSelectedSource(hit);
        m_dragTarget = DragTarget::Source;
        setCursor(Qt::SizeAllCursor);
        return;
    }

    m_measurementPoint = unprojectPoint(screenToWorld(event->position()), m_measurementPoint);
    emit measurementPointChanged(m_measurementPoint.x, m_measurementPoint.y, m_measurementPoint.z);
    update();
}

void ElectrostaticCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning)
    {
        const QPoint delta = event->position().toPoint() - m_lastMousePos;
        m_viewCenterWorld.rx() -= double(delta.x()) / m_pixelsPerMeter;
        m_viewCenterWorld.ry() += double(delta.y()) / m_pixelsPerMeter;
        invalidateVisualization();
        m_lastMousePos = event->position().toPoint();
        update();
        return;
    }

    if (m_dragTarget == DragTarget::Source && m_model && m_selectedSource >= 0)
    {
        const auto *source = m_model->source(m_selectedSource);
        if (!source) return;
        const ElectrostaticVec3 p = unprojectPoint(screenToWorld(event->position()), source->position);
        emit sourcePositionEdited(m_selectedSource, p.x, p.y, p.z);
        return;
    }

    if (m_dragTarget == DragTarget::ProbeA)
    {
        m_probeA = unprojectPoint(screenToWorld(event->position()), m_probeA);
        emit probeSegmentEdited(m_probeA.x, m_probeA.y, m_probeA.z, m_probeB.x, m_probeB.y, m_probeB.z);
        update();
        return;
    }
    if (m_dragTarget == DragTarget::ProbeB)
    {
        m_probeB = unprojectPoint(screenToWorld(event->position()), m_probeB);
        emit probeSegmentEdited(m_probeA.x, m_probeA.y, m_probeA.z, m_probeB.x, m_probeB.y, m_probeB.z);
        update();
        return;
    }
    if (m_dragTarget == DragTarget::PointProbe && m_draggedPointProbe >= 0 && m_draggedPointProbe < m_pointProbes.size())
    {
        auto &probe = m_pointProbes[m_draggedPointProbe];
        probe = unprojectPoint(screenToWorld(event->position()), probe);
        emit pointProbePositionEdited(m_draggedPointProbe, probe.x, probe.y, probe.z);
        update();
    }
}

void ElectrostaticCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton)
    {
        m_panning = false;
        invalidateVisualization();
        unsetCursor();
        update();
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    const DragTarget finishedTarget = m_dragTarget;
    const int finishedPointProbe = m_draggedPointProbe;
    m_dragTarget = DragTarget::None;
    m_draggedPointProbe = -1;
    if (finishedTarget == DragTarget::Source)
        invalidateVisualization();
    if (finishedTarget == DragTarget::ProbeA || finishedTarget == DragTarget::ProbeB)
        emit probeSegmentEditFinished();
    if (finishedTarget == DragTarget::PointProbe && finishedPointProbe >= 0)
        emit pointProbeEditFinished(finishedPointProbe);
    unsetCursor();
    update();
}

void ElectrostaticCanvas::wheelEvent(QWheelEvent *event)
{
    const QPointF before=screenToWorld(event->position());
    const double factor=event->angleDelta().y()>0?1.16:1.0/1.16;
    setPixelsPerMeter(m_pixelsPerMeter*factor);
    const QPointF after=screenToWorld(event->position());
    m_viewCenterWorld.rx()+=before.x()-after.x();
    m_viewCenterWorld.ry()+=before.y()-after.y();
    update();
}

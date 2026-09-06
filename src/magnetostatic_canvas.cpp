#include "widgets/magnetostatic_canvas.h"
#include "field_visualization_utils.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double Pi = 3.1415926535897932384626433832795;
QColor magneticColor(MagnetostaticSource::Type type)
{
    using Type = MagnetostaticSource::Type;
    switch (type)
    {
    case Type::InfiniteWireZ: return QColor(205, 95, 50);
    case Type::FiniteWire: return QColor(205, 130, 35);
    case Type::CircularLoop: return QColor(45, 125, 195);
    case Type::Solenoid: return QColor(85, 90, 190);
    case Type::MagneticDipole: return QColor(150, 65, 175);
    case Type::SolidCurrentCylinderZ: return QColor(190, 105, 45);
    case Type::HollowCurrentCylinderZ: return QColor(185, 120, 55);
    case Type::RectangularLoop: return QColor(45, 145, 180);
    case Type::TriangularLoop: return QColor(55, 155, 150);
    case Type::HelmholtzPair: return QColor(75, 105, 205);
    case Type::InfiniteCurrentSheet: return QColor(145, 105, 175);
    }
    return QColor(90, 90, 90);
}
}

MagnetostaticCanvas::MagnetostaticCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(650, 500);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void MagnetostaticCanvas::setModel(MagnetostaticModel *model)
{
    if (m_model == model)
        return;
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model)
    {
        connect(m_model, &MagnetostaticModel::changed, this, [this] {
            invalidateVisualization();
            update();
        });
    }
    invalidateVisualization();
    update();
}

void MagnetostaticCanvas::setSelectedSource(int index)
{
    const int maxIndex = m_model ? m_model->sourceCount() - 1 : -1;
    index = std::clamp(index, -1, maxIndex);
    if (m_selectedSource == index)
        return;
    m_selectedSource = index;
    emit selectedSourceChanged(index);
    update();
}

void MagnetostaticCanvas::setMeasurementPoint(const MagnetostaticVec3 &point)
{
    m_measurementPoint = point;
    invalidateVisualization();
    update();
}

void MagnetostaticCanvas::setViewPlane(FieldViewPlane plane)
{
    if (m_viewPlane == plane)
        return;
    m_viewPlane = plane;
    invalidateVisualization();
    centerView();
}

void MagnetostaticCanvas::centerView()
{
    m_viewCenterWorld = {0.0, 0.0};
    m_pixelsPerMeter = 90.0;
    invalidateVisualization();
    update();
}

void MagnetostaticCanvas::setShowFieldVectors(bool enabled)
{
    m_showFieldVectors = enabled;
    update();
}

void MagnetostaticCanvas::setScalarMap(ScalarMap mode)
{
    if (m_scalarMap == mode) return;
    m_scalarMap = mode;
    invalidateVisualization();
    update();
}

void MagnetostaticCanvas::setShowContours(bool enabled)
{
    if (m_showContours == enabled) return;
    m_showContours = enabled;
    invalidateVisualization();
    update();
}

void MagnetostaticCanvas::setShowFieldLines(bool enabled)
{
    if (m_showFieldLines == enabled) return;
    m_showFieldLines = enabled;
    invalidateVisualization();
    update();
}

void MagnetostaticCanvas::setProbeSegment(const MagnetostaticVec3 &pointA, const MagnetostaticVec3 &pointB, bool enabled)
{
    m_probeA = pointA;
    m_probeB = pointB;
    m_probeVisible = enabled;
    update();
}

void MagnetostaticCanvas::setProbeSegmentVisible(bool enabled)
{
    if (m_probeVisible == enabled)
        return;
    m_probeVisible = enabled;
    update();
}

void MagnetostaticCanvas::setPointProbes(const QVector<MagnetostaticVec3> &positions, const QStringList &names, int selectedIndex)
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

void MagnetostaticCanvas::setPointProbesVisible(bool enabled)
{
    if (m_pointProbesVisible == enabled)
        return;
    m_pointProbesVisible = enabled;
    update();
}

QPointF MagnetostaticCanvas::projectPoint(const MagnetostaticVec3 &p) const
{
    switch (m_viewPlane)
    {
    case FieldViewPlane::XY: return {p.x, p.y};
    case FieldViewPlane::XZ: return {p.x, p.z};
    case FieldViewPlane::YZ: return {p.y, p.z};
    }
    return {p.x, p.y};
}

QPointF MagnetostaticCanvas::projectVector(const MagnetostaticVec3 &v) const
{
    return projectPoint(v);
}

MagnetostaticVec3 MagnetostaticCanvas::unprojectPoint(const QPointF &p, const MagnetostaticVec3 &base) const
{
    MagnetostaticVec3 out = base;
    switch (m_viewPlane)
    {
    case FieldViewPlane::XY: out.x = p.x(); out.y = p.y(); break;
    case FieldViewPlane::XZ: out.x = p.x(); out.z = p.y(); break;
    case FieldViewPlane::YZ: out.y = p.x(); out.z = p.y(); break;
    }
    return out;
}

MagnetostaticVec3 MagnetostaticCanvas::samplePointForPlane(const QPointF &p) const
{
    return unprojectPoint(p, m_measurementPoint);
}

QPointF MagnetostaticCanvas::worldToScreen(const QPointF &p) const
{
    return {width() * 0.5 + (p.x() - m_viewCenterWorld.x()) * m_pixelsPerMeter,
            height() * 0.5 - (p.y() - m_viewCenterWorld.y()) * m_pixelsPerMeter};
}

QPointF MagnetostaticCanvas::screenToWorld(const QPointF &p) const
{
    return {m_viewCenterWorld.x() + (p.x() - width() * 0.5) / m_pixelsPerMeter,
            m_viewCenterWorld.y() - (p.y() - height() * 0.5) / m_pixelsPerMeter};
}

QString MagnetostaticCanvas::horizontalAxisName() const
{
    return m_viewPlane == FieldViewPlane::YZ ? QStringLiteral("y") : QStringLiteral("x");
}

QString MagnetostaticCanvas::verticalAxisName() const
{
    return m_viewPlane == FieldViewPlane::XY ? QStringLiteral("y") : QStringLiteral("z");
}

void MagnetostaticCanvas::paintEvent(QPaintEvent *)
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

void MagnetostaticCanvas::invalidateVisualization()
{
    m_visualizationDirty = true;
}

QRectF MagnetostaticCanvas::visualizationRect() const
{
    return QRectF(0.0, 34.0, std::max(1, width()), std::max(1, height() - 56));
}

double MagnetostaticCanvas::scalarValueAt(const MagnetostaticVec3 &point, bool *valid) const
{
    if (valid) *valid = false;
    if (!m_model || m_scalarMap == ScalarMap::Off)
        return 0.0;
    const auto field = m_model->fieldAt(point);
    if (field.singular)
        return 0.0;
    const double value = field.magneticFluxDensity.norm();
    if (valid) *valid = std::isfinite(value);
    return value;
}

QColor MagnetostaticCanvas::sequentialColor(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    struct Stop { double p; QColor c; };
    const Stop stops[] = {
        {0.00, QColor(30, 36, 74)},
        {0.25, QColor(57, 83, 164)},
        {0.50, QColor(55, 168, 184)},
        {0.75, QColor(232, 199, 72)},
        {1.00, QColor(188, 48, 92)}
    };
    for (int i=0;i<4;++i)
    {
        if (t <= stops[i+1].p)
        {
            const double u=(t-stops[i].p)/(stops[i+1].p-stops[i].p);
            return QColor(int(stops[i].c.red()+(stops[i+1].c.red()-stops[i].c.red())*u),
                          int(stops[i].c.green()+(stops[i+1].c.green()-stops[i].c.green())*u),
                          int(stops[i].c.blue()+(stops[i+1].c.blue()-stops[i].c.blue())*u),178);
        }
    }
    QColor c=stops[4].c;c.setAlpha(178);return c;
}

QString MagnetostaticCanvas::formatEngineering(double value, const QString &unit)
{
    if (!std::isfinite(value)) return QStringLiteral("n/a");
    if (std::abs(value)<1e-30) return QStringLiteral("0 %1").arg(unit);
    const double a=std::abs(value);
    struct Prefix{double scale;const char*symbol;};
    const Prefix prefixes[]={{1e12,"T"},{1e9,"G"},{1e6,"M"},{1e3,"k"},{1.0,""},{1e-3,"m"},{1e-6,"u"},{1e-9,"n"},{1e-12,"p"}};
    for(const auto&p:prefixes)
        if(a>=p.scale*0.999||p.scale==1e-12)
            return QStringLiteral("%1 %2%3").arg(value/p.scale,0,'g',4).arg(QString::fromLatin1(p.symbol),unit);
    return QStringLiteral("%1 %2").arg(value,0,'g',4).arg(unit);
}

void MagnetostaticCanvas::rebuildScalarCache()
{
    m_scalarImage={};
    m_contourLines.clear();
    m_scalarHasData=false;
    if(!m_model||m_model->sourceCount()==0||m_scalarMap==ScalarMap::Off) return;
    const QRectF area=visualizationRect();
    const int columns=std::clamp(int(area.width()/20.0)+1,26,58);
    const int rows=std::clamp(int(area.height()/20.0)+1,18,42);
    FieldVisualizationGrid grid;
    grid.columns=columns;grid.rows=rows;grid.screenRect=area;
    grid.values.resize(columns*rows);grid.valid.fill(0,columns*rows);
    QVector<double> values;values.reserve(columns*rows);
    for(int row=0;row<rows;++row)
    {
        for(int column=0;column<columns;++column)
        {
            const QPointF plane=screenToWorld(grid.screenPoint(column,row));
            bool valid=false;
            const double raw=scalarValueAt(samplePointForPlane(plane),&valid);
            if(!valid||!std::isfinite(raw)) continue;
            const double mapped=std::log10(std::max(raw,1e-30));
            const int index=grid.index(column,row);
            grid.values[index]=mapped;grid.valid[index]=1;values.push_back(mapped);
        }
    }
    if(values.size()<4) return;
    m_scalarDisplayMin=fieldVisualizationPercentile(values,0.05);
    m_scalarDisplayMax=fieldVisualizationPercentile(values,0.95);
    if(m_scalarDisplayMax-m_scalarDisplayMin<1e-9)m_scalarDisplayMax=m_scalarDisplayMin+1.0;
    m_scalarImage=QImage(columns,rows,QImage::Format_ARGB32_Premultiplied);m_scalarImage.fill(Qt::transparent);
    for(int row=0;row<rows;++row)
        for(int column=0;column<columns;++column)
        {
            if(!grid.isValid(column,row))continue;
            const double t=std::clamp((grid.value(column,row)-m_scalarDisplayMin)/(m_scalarDisplayMax-m_scalarDisplayMin),0.0,1.0);
            m_scalarImage.setPixelColor(column,row,sequentialColor(t));
        }
    if(m_showContours)
    {
        QVector<double> levels;
        for(int i=1;i<=8;++i){const double f=double(i)/9.0;levels.push_back(m_scalarDisplayMin+(m_scalarDisplayMax-m_scalarDisplayMin)*f);}
        m_contourLines=fieldVisualizationContours(grid,levels);
    }
    m_scalarHasData=true;
}

void MagnetostaticCanvas::rebuildFieldLineCache()
{
    m_fieldLinePaths.clear();
    if(!m_showFieldLines||!m_model||m_model->sourceCount()==0)return;
    const QRectF area=visualizationRect();
    const QPointF a=screenToWorld(area.topLeft()),b=screenToWorld(area.bottomRight());
    const QRectF bounds(QPointF(std::min(a.x(),b.x()),std::min(a.y(),b.y())),QPointF(std::max(a.x(),b.x()),std::max(a.y(),b.y())));
    const double step=std::clamp(10.0/m_pixelsPerMeter,0.002,1.2);
    const int nsrc=std::max(1,m_model->sourceCount());
    const int seedsPerSource=std::clamp(14/nsrc,3,6);
    QVector<QPointF> seeds;
    for(int i=0;i<m_model->sourceCount();++i)
    {
        const auto*source=m_model->source(i);if(!source)continue;
        const QPointF center=projectPoint(source->position);
        double radius=20.0/m_pixelsPerMeter;
        if(source->type==MagnetostaticSource::Type::CircularLoop||source->type==MagnetostaticSource::Type::Solenoid||
           source->type==MagnetostaticSource::Type::HelmholtzPair||
           source->type==MagnetostaticSource::Type::SolidCurrentCylinderZ||
           source->type==MagnetostaticSource::Type::HollowCurrentCylinderZ)
            radius=std::max(radius,source->radius*1.12);
        else if(source->type==MagnetostaticSource::Type::FiniteWire)
            radius=std::max(radius,source->length*0.16);
        else if(source->type==MagnetostaticSource::Type::RectangularLoop||source->type==MagnetostaticSource::Type::TriangularLoop)
            radius=std::max(radius,std::max(source->width,source->height)*0.20);
        for(int k=0;k<seedsPerSource;++k)
        {
            const double angle=2.0*3.14159265358979323846*double(k)/double(seedsPerSource);
            seeds.push_back(center+QPointF(std::cos(angle),std::sin(angle))*radius);
        }
    }
    const auto trace=[this,bounds,step](const QPointF&seed,double sign){
        QPainterPath path;QPointF p=seed;path.moveTo(worldToScreen(p));
        for(int iteration=0;iteration<105;++iteration)
        {
            if(!bounds.adjusted(-step,-step,step,step).contains(p))break;
            const auto field=m_model->fieldAt(samplePointForPlane(p));if(field.singular)break;
            QPointF v=projectVector(field.magneticFluxDensity);double n=std::hypot(v.x(),v.y());
            if(!std::isfinite(n)||n<1e-30)break;v/=n;
            const QPointF mid=p+v*(0.5*step*sign);
            const auto mf=m_model->fieldAt(samplePointForPlane(mid));if(mf.singular)break;
            QPointF vm=projectVector(mf.magneticFluxDensity);const double nm=std::hypot(vm.x(),vm.y());
            if(!std::isfinite(nm)||nm<1e-30)break;vm/=nm;
            const QPointF next=p+vm*(step*sign);if(!bounds.adjusted(-step,-step,step,step).contains(next))break;
            path.lineTo(worldToScreen(next));
            if(iteration>20&&QLineF(next,seed).length()<step*1.5){path.lineTo(worldToScreen(seed));break;}
            p=next;
        }
        return path;
    };
    for(const auto&seed:seeds)
    {
        const auto f=trace(seed,+1.0),bwd=trace(seed,-1.0);
        if(f.elementCount()>2)m_fieldLinePaths.push_back(f);
        if(bwd.elementCount()>2)m_fieldLinePaths.push_back(bwd);
    }
}

void MagnetostaticCanvas::drawScalarHeatmap(QPainter&painter)
{
    if(!m_scalarHasData||m_scalarImage.isNull())return;
    painter.save();painter.setRenderHint(QPainter::SmoothPixmapTransform,true);painter.drawImage(visualizationRect(),m_scalarImage);painter.restore();
}

void MagnetostaticCanvas::drawContours(QPainter&painter)
{
    if(!m_showContours||m_contourLines.isEmpty())return;
    painter.save();painter.setPen(QPen(QColor(34,38,46,175),1.0));painter.drawLines(m_contourLines);painter.restore();
}

void MagnetostaticCanvas::drawFieldLines(QPainter&painter)
{
    if(!m_showFieldLines||m_fieldLinePaths.isEmpty())return;
    painter.save();painter.setPen(QPen(QColor(38,88,165,195),1.5));painter.setBrush(Qt::NoBrush);
    for(const auto&path:m_fieldLinePaths)painter.drawPath(path);painter.restore();
}

void MagnetostaticCanvas::drawScalarLegend(QPainter&painter)
{
    if(!m_scalarHasData||m_scalarMap==ScalarMap::Off)return;
    const QRectF box(width()-228.0,38.0,216.0,54.0);
    painter.save();painter.setPen(QPen(QColor(70,73,78,150),1.0));painter.setBrush(QColor(255,255,255,218));painter.drawRoundedRect(box,4,4);
    painter.setPen(QColor(45,45,48));painter.drawText(QRectF(box.left()+8,box.top()+3,box.width()-16,16),Qt::AlignLeft|Qt::AlignVCenter,QStringLiteral("|B| (log scale)"));
    const QRectF bar(box.left()+8,box.top()+21,box.width()-16,10);const int steps=96;
    for(int i=0;i<steps;++i){const double t=double(i)/double(steps);painter.fillRect(QRectF(bar.left()+bar.width()*t,bar.top(),bar.width()/steps+1,bar.height()),sequentialColor(t));}
    painter.drawText(QRectF(box.left()+8,box.top()+33,box.width()/2-8,17),Qt::AlignLeft|Qt::AlignVCenter,formatEngineering(std::pow(10.0,m_scalarDisplayMin),QStringLiteral("T")));
    painter.drawText(QRectF(box.center().x(),box.top()+33,box.width()/2-8,17),Qt::AlignRight|Qt::AlignVCenter,formatEngineering(std::pow(10.0,m_scalarDisplayMax),QStringLiteral("T")));
    painter.restore();
}

void MagnetostaticCanvas::drawGrid(QPainter &painter)
{
    const double targetPixels = 70.0;
    const double rawStep = targetPixels / m_pixelsPerMeter;
    const double decade = std::pow(10.0, std::floor(std::log10(std::max(rawStep, 1e-12))));
    const double normalizedStep = rawStep / decade;
    double nice = 1.0;
    if (normalizedStep > 5.0) nice = 10.0;
    else if (normalizedStep > 2.0) nice = 5.0;
    else if (normalizedStep > 1.0) nice = 2.0;
    const double step = nice * decade;

    const QPointF tl = screenToWorld({0.0, 0.0});
    const QPointF br = screenToWorld({double(width()), double(height())});
    const double minX = std::min(tl.x(), br.x());
    const double maxX = std::max(tl.x(), br.x());
    const double minY = std::min(tl.y(), br.y());
    const double maxY = std::max(tl.y(), br.y());

    painter.setPen(QPen(QColor(220, 224, 232), 1.0));
    for (double x = std::floor(minX / step) * step; x <= maxX + step * 0.5; x += step)
    {
        const double sx = worldToScreen({x, 0.0}).x();
        painter.drawLine(QPointF(sx, 0.0), QPointF(sx, height()));
    }
    for (double y = std::floor(minY / step) * step; y <= maxY + step * 0.5; y += step)
    {
        const double sy = worldToScreen({0.0, y}).y();
        painter.drawLine(QPointF(0.0, sy), QPointF(width(), sy));
    }

    const QPointF origin = worldToScreen({0.0, 0.0});
    painter.setPen(QPen(QColor(120, 126, 136), 1.4));
    painter.drawLine(QPointF(0, origin.y()), QPointF(width(), origin.y()));
    painter.drawLine(QPointF(origin.x(), 0), QPointF(origin.x(), height()));
    painter.setPen(QColor(70, 72, 78));
    painter.drawText(QPointF(width() - 24, origin.y() - 6), horizontalAxisName());
    painter.drawText(QPointF(origin.x() + 6, 42), verticalAxisName());
    painter.drawText(QPointF(12, height() - 10), QStringLiteral("grid: %1 m").arg(step, 0, 'g', 3));
}

void MagnetostaticCanvas::drawFieldVectors(QPainter &painter)
{
    if (!m_model || m_model->sourceCount() == 0)
        return;
    const int spacing = 76;
    painter.setPen(QPen(QColor(45, 115, 190, 170), 1.15));
    for (int sy = 66; sy < height() - 24; sy += spacing)
    {
        for (int sx = 38; sx < width() - 24; sx += spacing)
        {
            const QPointF plane = screenToWorld({double(sx), double(sy)});
            const auto field = m_model->fieldAt(samplePointForPlane(plane));
            const QPointF b = projectVector(field.magneticFluxDensity);
            const double mag = std::hypot(b.x(), b.y());
            if (!std::isfinite(mag) || mag < 1e-30 || field.singular)
                continue;
            const double len = 13.0 + std::clamp((std::log10(mag + 1e-18) + 18.0) * 1.7, 0.0, 13.0);
            const QPointF dir(b.x() / mag, -b.y() / mag);
            drawArrow(painter, {double(sx), double(sy)}, QPointF(double(sx), double(sy)) + dir * len);
        }
    }
}

void MagnetostaticCanvas::drawProjectedLoop(QPainter &painter,
                                             const MagnetostaticSource &source,
                                             const QColor &color,
                                             double width)
{
    const MagnetostaticVec3 axis = MagnetostaticModel::axisUnit(source);
    const MagnetostaticVec3 ref = std::abs(axis.z) < 0.85
        ? MagnetostaticVec3{0.0, 0.0, 1.0}
        : MagnetostaticVec3{1.0, 0.0, 0.0};
    const MagnetostaticVec3 u = normalized(cross(axis, ref));
    const MagnetostaticVec3 v = normalized(cross(axis, u));
    QPainterPath path;
    const int n = 80;
    for (int i = 0; i <= n; ++i)
    {
        const double a = 2.0 * Pi * double(i) / double(n);
        const MagnetostaticVec3 p = source.position + (u * std::cos(a) + v * std::sin(a)) * source.radius;
        const QPointF s = worldToScreen(projectPoint(p));
        if (i == 0) path.moveTo(s); else path.lineTo(s);
    }
    painter.setPen(QPen(color, width));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

void MagnetostaticCanvas::drawSources(QPainter &painter)
{
    if (!m_model) return;
    using Type = MagnetostaticSource::Type;
    for (int i = 0; i < m_model->sourceCount(); ++i)
    {
        const auto *source = m_model->source(i);
        if (!source) continue;
        const QPointF center = worldToScreen(projectPoint(source->position));
        const QColor color = magneticColor(source->type);
        const double selectedWidth = i == m_selectedSource ? 4.0 : 2.5;
        painter.setPen(QPen(color.darker(125), selectedWidth));
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), 58));

        if (source->type == Type::InfiniteWireZ)
        {
            if (m_viewPlane == FieldViewPlane::XY)
            {
                painter.drawEllipse(center, 11.0, 11.0);
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(center, 4.0, 4.0);
                if (source->strength < 0.0)
                {
                    painter.drawLine(center + QPointF(-7,-7), center + QPointF(7,7));
                    painter.drawLine(center + QPointF(-7,7), center + QPointF(7,-7));
                }
            }
            else
            {
                painter.setPen(QPen(color, selectedWidth + 1.0));
                painter.drawLine(QPointF(center.x(), 34.0), QPointF(center.x(), height() - 22.0));
            }
        }
        else if (source->type == Type::SolidCurrentCylinderZ || source->type == Type::HollowCurrentCylinderZ)
        {
            const double r = std::max(10.0, source->radius*m_pixelsPerMeter);
            if(m_viewPlane==FieldViewPlane::XY)
            {
                painter.drawEllipse(center,r,r);
                if(source->type==Type::HollowCurrentCylinderZ)
                {
                    const double ri=std::clamp(source->innerRadius/source->radius,0.0,0.999)*r;
                    painter.setBrush(palette().brush(QPalette::Base));
                    painter.drawEllipse(center,ri,ri);
                }
            }
            else
            {
                painter.drawRect(QRectF(center.x()-r,34.0,2*r,height()-56.0));
                if(source->type==Type::HollowCurrentCylinderZ)
                {
                    const double ri=std::clamp(source->innerRadius/source->radius,0.0,0.999)*r;
                    painter.setBrush(palette().brush(QPalette::Base));
                    painter.drawRect(QRectF(center.x()-ri,34.0,2*ri,height()-56.0));
                }
            }
        }
        else if (source->type == Type::FiniteWire)
        {
            const MagnetostaticVec3 axis = MagnetostaticModel::axisUnit(*source);
            const QPointF p0 = worldToScreen(projectPoint(source->position - axis * (source->length * 0.5)));
            const QPointF p1 = worldToScreen(projectPoint(source->position + axis * (source->length * 0.5)));
            painter.setPen(QPen(color, selectedWidth + 2.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(p0, p1);
        }
        else if (source->type == Type::CircularLoop)
        {
            drawProjectedLoop(painter, *source, color, selectedWidth + 1.0);
        }
        else if (source->type == Type::RectangularLoop || source->type == Type::TriangularLoop)
        {
            const MagnetostaticVec3 axis=MagnetostaticModel::axisUnit(*source);
            const MagnetostaticVec3 reference=std::abs(axis.z)<0.85?MagnetostaticVec3{0,0,1}:MagnetostaticVec3{1,0,0};
            const MagnetostaticVec3 u=normalized(cross(axis,reference));
            const MagnetostaticVec3 v=normalized(cross(axis,u));
            QPolygonF poly;
            if(source->type==Type::RectangularLoop)
            {
                const auto p0=source->position-u*(source->width*.5)-v*(source->height*.5);
                const auto p1=source->position+u*(source->width*.5)-v*(source->height*.5);
                const auto p2=source->position+u*(source->width*.5)+v*(source->height*.5);
                const auto p3=source->position-u*(source->width*.5)+v*(source->height*.5);
                poly<<worldToScreen(projectPoint(p0))<<worldToScreen(projectPoint(p1))<<worldToScreen(projectPoint(p2))<<worldToScreen(projectPoint(p3))<<worldToScreen(projectPoint(p0));
            }
            else
            {
                const auto p0=source->position-u*(source->width*.5)-v*(source->height/3.0);
                const auto p1=source->position+u*(source->width*.5)-v*(source->height/3.0);
                const auto p2=source->position+v*(2.0*source->height/3.0);
                poly<<worldToScreen(projectPoint(p0))<<worldToScreen(projectPoint(p1))<<worldToScreen(projectPoint(p2))<<worldToScreen(projectPoint(p0));
            }
            painter.setBrush(Qt::NoBrush); painter.drawPolyline(poly);
        }
        else if (source->type == Type::Solenoid)
        {
            MagnetostaticSource loop = *source;
            const MagnetostaticVec3 axis = MagnetostaticModel::axisUnit(*source);
            const int visibleTurns = std::clamp(source->turns, 3, 18);
            for (int turn = 0; turn < visibleTurns; ++turn)
            {
                const double t = visibleTurns == 1 ? 0.5 : double(turn) / double(visibleTurns - 1);
                loop.position = source->position + axis * ((t - 0.5) * source->length);
                drawProjectedLoop(painter, loop, color, turn == 0 || turn == visibleTurns-1 ? selectedWidth : std::max(1.0, selectedWidth*0.58));
            }
            painter.setPen(QPen(color, 1.2, Qt::DashLine));
            painter.drawLine(worldToScreen(projectPoint(source->position - axis * (source->length * 0.5))),
                             worldToScreen(projectPoint(source->position + axis * (source->length * 0.5))));
        }
        else if(source->type==Type::HelmholtzPair)
        {
            const MagnetostaticVec3 axis=MagnetostaticModel::axisUnit(*source);
            MagnetostaticSource loop=*source;
            loop.type=Type::CircularLoop;
            loop.position=source->position-axis*(source->length*.5); drawProjectedLoop(painter,loop,color,selectedWidth+1.0);
            loop.position=source->position+axis*(source->length*.5); drawProjectedLoop(painter,loop,color,selectedWidth+1.0);
            painter.setPen(QPen(color,1.1,Qt::DashLine));
            painter.drawLine(worldToScreen(projectPoint(source->position-axis*(source->length*.5))),worldToScreen(projectPoint(source->position+axis*(source->length*.5))));
        }
        else if(source->type==Type::InfiniteCurrentSheet)
        {
            painter.setBrush(Qt::NoBrush); painter.setPen(QPen(color,selectedWidth,Qt::DashLine));
            if(m_viewPlane==FieldViewPlane::XY) painter.drawRect(rect().adjusted(12,42,-12,-28));
            else painter.drawLine(QPointF(8,center.y()),QPointF(width()-8,center.y()));
            const double az=qDegreesToRadians(source->azimuthDeg);
            const QPointF d(std::cos(az)*42.0,-std::sin(az)*42.0);
            drawArrow(painter,center-d*.5,center+d*.5);
        }
        else
        {
            const MagnetostaticVec3 axis = MagnetostaticModel::axisUnit(*source);
            const QPointF projected = projectVector(axis);
            const double mag = std::hypot(projected.x(), projected.y());
            if (mag > 1e-12)
            {
                const QPointF dir(projected.x() / mag, -projected.y() / mag);
                painter.setPen(QPen(color, selectedWidth + 1.0));
                drawArrow(painter, center - dir * 24.0, center + dir * 24.0);
            }
            painter.drawEllipse(center, 6.0, 6.0);
        }

        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(center + QPointF(13.0, -12.0), source->name);
    }
}

void MagnetostaticCanvas::drawProbeSegment(QPainter &painter)
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

void MagnetostaticCanvas::drawPointProbes(QPainter &painter)
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

int MagnetostaticCanvas::hitTestProbeEndpoint(const QPointF &screenPoint) const
{
    if (!m_probeVisible)
        return -1;
    const QPointF a = worldToScreen(projectPoint(m_probeA));
    const QPointF b = worldToScreen(projectPoint(m_probeB));
    if (std::hypot(screenPoint.x() - a.x(), screenPoint.y() - a.y()) <= 13.0) return 0;
    if (std::hypot(screenPoint.x() - b.x(), screenPoint.y() - b.y()) <= 13.0) return 1;
    return -1;
}

int MagnetostaticCanvas::hitTestPointProbe(const QPointF &screenPoint) const
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

void MagnetostaticCanvas::drawMeasurement(QPainter &painter)
{
    const QPointF p = worldToScreen(projectPoint(m_measurementPoint));
    painter.setPen(QPen(QColor(25, 145, 125), 2.1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(p, 7.0, 7.0);
    painter.drawLine(p + QPointF(-12,0), p + QPointF(12,0));
    painter.drawLine(p + QPointF(0,-12), p + QPointF(0,12));
    painter.drawText(p + QPointF(10, 20), QStringLiteral("M"));

    if (!m_model || m_model->sourceCount() == 0)
        return;
    const auto field = m_model->fieldAt(m_measurementPoint);
    const QPointF b = projectVector(field.magneticFluxDensity);
    const double mag = std::hypot(b.x(), b.y());
    if (!std::isfinite(mag) || mag < 1e-30 || field.singular)
        return;
    const QPointF dir(b.x() / mag, -b.y() / mag);
    painter.setPen(QPen(QColor(25, 145, 125), 2.5));
    drawArrow(painter, p, p + dir * 52.0);
}

void MagnetostaticCanvas::drawArrow(QPainter &painter, const QPointF &start, const QPointF &end) const
{
    painter.drawLine(start, end);
    const QPointF d = end - start;
    const double len = std::hypot(d.x(), d.y());
    if (len < 1.0)
        return;
    const QPointF u(d.x()/len, d.y()/len);
    const QPointF n(-u.y(), u.x());
    const double h = 5.5;
    painter.drawLine(end, end - u*h + n*h*0.55);
    painter.drawLine(end, end - u*h - n*h*0.55);
}

int MagnetostaticCanvas::hitTestSource(const QPointF &screenPoint) const
{
    if (!m_model)
        return -1;
    int best = -1;
    double bestDistance = 28.0;
    for (int i = 0; i < m_model->sourceCount(); ++i)
    {
        const auto *source = m_model->source(i);
        if (!source) continue;
        const QPointF center = worldToScreen(projectPoint(source->position));
        const double d = std::hypot(center.x()-screenPoint.x(), center.y()-screenPoint.y());
        if (d < bestDistance)
        {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

void MagnetostaticCanvas::mousePressEvent(QMouseEvent *event)
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

void MagnetostaticCanvas::mouseMoveEvent(QMouseEvent *event)
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
        const MagnetostaticVec3 p = unprojectPoint(screenToWorld(event->position()), source->position);
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

void MagnetostaticCanvas::mouseReleaseEvent(QMouseEvent *event)
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

void MagnetostaticCanvas::wheelEvent(QWheelEvent *event)
{
    const QPointF before = screenToWorld(event->position());
    const double factor = event->angleDelta().y() > 0 ? 1.16 : 1.0 / 1.16;
    m_pixelsPerMeter = std::clamp(m_pixelsPerMeter * factor, 15.0, 1200.0);
    invalidateVisualization();
    const QPointF after = screenToWorld(event->position());
    m_viewCenterWorld.rx() += before.x() - after.x();
    m_viewCenterWorld.ry() += before.y() - after.y();
    update();
}

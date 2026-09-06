#include "widgets/field_3d_views.h"

#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>
#include <utility>

namespace
{
constexpr double Pi = 3.14159265358979323846;

template <typename V>
double norm3(const V &v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

template <typename V>
V add3(const V &a, const V &b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

template <typename V>
V mul3(const V &a, double s)
{
    return {a.x * s, a.y * s, a.z * s};
}

template <typename V>
V axisFromAngles(double azimuthDeg, double elevationDeg)
{
    const double az = azimuthDeg * Pi / 180.0;
    const double el = elevationDeg * Pi / 180.0;
    return {std::cos(el) * std::cos(az), std::cos(el) * std::sin(az), std::sin(el)};
}

QColor positiveColor(const QPalette &pal)
{
    QColor c = pal.color(QPalette::Highlight);
    if (c.lightness() < 105) c = c.lighter(170);
    return c;
}

QColor negativeColor(const QPalette &pal)
{
    QColor c = pal.color(QPalette::LinkVisited);
    if (!c.isValid()) c = pal.color(QPalette::Mid);
    if (c.lightness() < 105) c = c.lighter(180);
    return c;
}

QColor fieldColor(const QPalette &pal)
{
    QColor c = pal.color(QPalette::Link);
    if (!c.isValid()) c = pal.color(QPalette::Highlight);
    if (c.lightness() < 100) c = c.lighter(170);
    return c;
}

void drawArrow2D(QPainter &p, const QPointF &a, const QPointF &b, const QColor &color, double width = 1.2)
{
    const QPointF d = b - a;
    const double n = std::hypot(d.x(), d.y());
    if (n < 1.0) return;
    p.save();
    p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(a, b);
    const QPointF u = d / n;
    const QPointF q(-u.y(), u.x());
    const double head = std::clamp(n * 0.28, 4.0, 8.0);
    p.drawLine(b, b - u * head + q * head * 0.45);
    p.drawLine(b, b - u * head - q * head * 0.45);
    p.restore();
}

void drawCross(QPainter &p, const QPointF &pt, const QColor &color, double r)
{
    p.save();
    p.setPen(QPen(color, 2.0));
    p.drawLine(QPointF(pt.x() - r, pt.y()), QPointF(pt.x() + r, pt.y()));
    p.drawLine(QPointF(pt.x(), pt.y() - r), QPointF(pt.x(), pt.y() + r));
    p.restore();
}

template <typename Project>
void drawAxes(QPainter &p, Project project, double radius, const QPalette &pal)
{
    const QPointF o = project(0.0, 0.0, 0.0);
    const std::array<std::array<double,3>,3> axes{{{{radius,0,0}},{{0,radius,0}},{{0,0,radius}}}};
    const std::array<QString,3> names{QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("Z")};
    p.save();
    p.setPen(QPen(pal.color(QPalette::Mid), 1.0));
    for (std::size_t i=0;i<axes.size();++i)
    {
        const auto &a = axes[i];
        const QPointF q = project(a[0], a[1], a[2]);
        p.drawLine(o, q);
        p.drawText(q + QPointF(4, -3), names[i]);
    }
    p.restore();
}

void drawHint(QPainter &p, const QRect &rect, const QPalette &pal, const QString &fieldName)
{
    p.save();
    p.setPen(pal.color(QPalette::Mid));
    p.drawText(rect.adjusted(10, 8, -10, -8), Qt::AlignLeft | Qt::AlignTop,
               QStringLiteral("3D %1 view  |  drag source: move  |  right-click: create/delete source  |  Delete: remove selected  |  drag empty: rotate  |  Shift/right-drag: pan  |  wheel: zoom")
                   .arg(fieldName));
    p.restore();
}

} // namespace

ElectrostaticField3DView::ElectrostaticField3DView(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(650, 500);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void ElectrostaticField3DView::setModel(const ElectrostaticModel *model) { m_model = model; update(); }
void ElectrostaticField3DView::setSelectedSource(int index) { m_selectedSource = index; update(); }
void ElectrostaticField3DView::setMeasurementPoint(const ElectrostaticVec3 &point) { m_measurementPoint = point; update(); }
void ElectrostaticField3DView::setShowFieldVectors(bool show) { m_showFieldVectors = show; update(); }
void ElectrostaticField3DView::setEditPlane(int plane) { m_editPlane = std::clamp(plane, 0, 2); update(); }
void ElectrostaticField3DView::centerView() { m_yawDeg = -38.0; m_pitchDeg = 27.0; m_zoom = 1.0; m_panPx = {}; update(); }

ElectrostaticField3DView::Vec3 ElectrostaticField3DView::rotate(const Vec3 &p) const
{
    const double yaw = m_yawDeg * Pi / 180.0;
    const double pitch = m_pitchDeg * Pi / 180.0;
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double x1 = cy * p.x - sy * p.y;
    const double y1 = sy * p.x + cy * p.y;
    return {x1, cp * y1 - sp * p.z, sp * y1 + cp * p.z};
}

QPointF ElectrostaticField3DView::project(const Vec3 &p) const
{
    const Vec3 r = rotate(p);
    const double radius = std::max(0.1, sceneRadius());
    const double scale = 0.36 * std::min(width(), height()) * m_zoom / radius;
    return QPointF(width() * 0.5 + m_panPx.x() + r.x * scale,
                   height() * 0.52 + m_panPx.y() - r.z * scale);
}

double ElectrostaticField3DView::sceneRadius() const
{
    double r = 1.6;
    if (m_model)
    {
        for (const auto &s : m_model->sources())
        {
            r = std::max(r, norm3(s.position));
            r = std::max(r, norm3(s.position) + std::max({0.2, s.radius, 0.5 * s.length, 0.5 * s.width, 0.5 * s.height}));
        }
    }
    r = std::max(r, norm3(m_measurementPoint));
    return 1.20 * r;
}


int ElectrostaticField3DView::hitSource(const QPointF &screen) const
{
    if (!m_model) return -1;
    int best = -1;
    double bestDistance = 14.0;
    for (int i = 0; i < m_model->sourceCount(); ++i)
    {
        const auto *source = m_model->source(i);
        if (!source) continue;
        const QPointF q = project({source->position.x, source->position.y, source->position.z});
        const double d = std::hypot(screen.x() - q.x(), screen.y() - q.y());
        if (d < bestDistance) { bestDistance = d; best = i; }
    }
    return best;
}

bool ElectrostaticField3DView::screenToSourcePlane(const QPointF &screen, const ElectrostaticVec3 &origin, ElectrostaticVec3 &out) const
{
    const double radius = std::max(0.1, sceneRadius());
    const double scale = 0.36 * std::min(width(), height()) * m_zoom / radius;
    if (!(scale > 1e-12)) return false;
    const double x1 = (screen.x() - (width() * 0.5 + m_panPx.x())) / scale;
    const double z2 = -(screen.y() - (height() * 0.52 + m_panPx.y())) / scale;
    const double yaw = m_yawDeg * Pi / 180.0, pitch = m_pitchDeg * Pi / 180.0;
    const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const double y10 = sp * z2;
    ElectrostaticVec3 p0{cy * x1 + sy * y10, -sy * x1 + cy * y10, cp * z2};
    const ElectrostaticVec3 dir{sy * cp, cy * cp, -sp};
    double denominator = 0.0, numerator = 0.0;
    if (m_editPlane == 0) { denominator = dir.z; numerator = origin.z - p0.z; }
    else if (m_editPlane == 1) { denominator = dir.y; numerator = origin.y - p0.y; }
    else { denominator = dir.x; numerator = origin.x - p0.x; }
    if (std::abs(denominator) < 1e-10) return false;
    const double t = numerator / denominator;
    out = {p0.x + dir.x * t, p0.y + dir.y * t, p0.z + dir.z * t};
    if (m_editPlane == 0) out.z = origin.z;
    else if (m_editPlane == 1) out.y = origin.y;
    else out.x = origin.x;
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

bool ElectrostaticField3DView::screenToCreationPlane(const QPointF &screen, ElectrostaticVec3 &out) const
{
    return screenToSourcePlane(screen, m_measurementPoint, out);
}

void ElectrostaticField3DView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), palette().color(QPalette::Base));
    const QPalette pal = palette();
    const double r = sceneRadius();
    drawAxes(p, [this](double x, double y, double z) { return project({x, y, z}); }, r, pal);

    // A sparse spatial lattice makes depth and camera orientation readable without replacing the 2D grid.
    p.save();
    QColor grid = pal.color(QPalette::Mid); grid.setAlpha(55);
    p.setPen(QPen(grid, 1.0));
    for (int i = -4; i <= 4; ++i)
    {
        const double a = r * i / 4.0;
        p.drawLine(project({-r, a, 0}), project({r, a, 0}));
        p.drawLine(project({a, -r, 0}), project({a, r, 0}));
    }
    p.restore();

    struct Sample { Vec3 pos; Vec3 vec; double mag; };
    std::vector<Sample> samples;
    double maxLog = -std::numeric_limits<double>::infinity();
    double minLog = std::numeric_limits<double>::infinity();
    if (m_model && m_showFieldVectors)
    {
        constexpr int N = 5;
        samples.reserve(N*N*N);
        for (int iz = 0; iz < N; ++iz)
            for (int iy = 0; iy < N; ++iy)
                for (int ix = 0; ix < N; ++ix)
                {
                    const double x = -0.82*r + 1.64*r*ix/(N-1);
                    const double y = -0.82*r + 1.64*r*iy/(N-1);
                    const double z = -0.62*r + 1.24*r*iz/(N-1);
                    const auto f = m_model->fieldAt({x,y,z});
                    const double mag = f.electricField.norm();
                    if (f.singular || !std::isfinite(mag) || mag <= 1e-30) continue;
                    const double lm = std::log10(mag);
                    minLog = std::min(minLog, lm); maxLog = std::max(maxLog, lm);
                    samples.push_back({{x,y,z}, {f.electricField.x,f.electricField.y,f.electricField.z}, mag});
                }
    }
    const QColor fcolor = fieldColor(pal);
    for (const auto &s : samples)
    {
        const double n = std::max(1e-30, s.mag);
        const Vec3 u{s.vec.x/n, s.vec.y/n, s.vec.z/n};
        const double lm = std::log10(n);
        const double t = (maxLog > minLog + 1e-12) ? (lm-minLog)/(maxLog-minLog) : 0.6;
        const double len = r * (0.055 + 0.11 * std::clamp(t,0.0,1.0));
        const Vec3 b{s.pos.x + u.x*len, s.pos.y + u.y*len, s.pos.z + u.z*len};
        QColor c = fcolor; c.setAlphaF(0.28 + 0.62*std::clamp(t,0.0,1.0));
        drawArrow2D(p, project(s.pos), project(b), c, 1.0 + t);
    }

    if (m_model)
    {
        int index = 0;
        for (const auto &s : m_model->sources())
        {
            const ElectrostaticVec3 shown = (m_draggingSource && index == m_dragSourceIndex) ? m_dragSourcePreview : s.position;
            const Vec3 c{shown.x,shown.y,shown.z};
            const QPointF pc = project(c);
            QColor sc = s.strength >= 0.0 ? positiveColor(pal) : negativeColor(pal);
            const bool selected = index == m_selectedSource;
            p.save();
            p.setPen(QPen(selected ? pal.color(QPalette::Text) : sc, selected ? 2.7 : 1.8));
            p.setBrush(QBrush(sc, Qt::SolidPattern));
            switch (s.type)
            {
            case ElectrostaticSource::Type::PointCharge:
                p.drawEllipse(pc, selected ? 8.0 : 6.0, selected ? 8.0 : 6.0);
                break;
            case ElectrostaticSource::Type::SolidSphere:
            case ElectrostaticSource::Type::SphericalShell:
            case ElectrostaticSource::Type::ThickSphericalShell:
            {
                const QPointF pr = project({c.x+s.radius,c.y,c.z});
                const double rr = std::max(5.0, std::hypot(pr.x()-pc.x(),pr.y()-pc.y()));
                p.setBrush(s.type == ElectrostaticSource::Type::SolidSphere ? QBrush(sc) : QBrush(QColor(sc.red(),sc.green(),sc.blue(),55)));
                p.drawEllipse(pc, rr, rr);
                if(s.type==ElectrostaticSource::Type::ThickSphericalShell && s.radius>0.0)
                {
                    const double ri=rr*std::clamp(s.innerRadius/s.radius,0.0,0.999);
                    p.setBrush(pal.brush(QPalette::Base));p.drawEllipse(pc,ri,ri);
                }
                break;
            }
            case ElectrostaticSource::Type::InfiniteCylinderZ:
            case ElectrostaticSource::Type::InfiniteHollowCylinderZ:
            case ElectrostaticSource::Type::InfiniteLine:
            {
                const double h = 1.5*r;
                p.drawLine(project({c.x,c.y,-h}), project({c.x,c.y,h}));
                const QPointF pr=project({c.x+s.radius,c.y,c.z});
                const double rr=(s.type==ElectrostaticSource::Type::InfiniteLine)?5.0:std::max(4.0,std::hypot(pr.x()-pc.x(),pr.y()-pc.y()));
                p.drawEllipse(pc,rr,rr);
                if(s.type==ElectrostaticSource::Type::InfiniteHollowCylinderZ && s.radius>0.0)
                {
                    const double ri=rr*std::clamp(s.innerRadius/s.radius,0.0,0.999);p.drawEllipse(pc,ri,ri);
                }
                break;
            }
            case ElectrostaticSource::Type::FiniteLine:
            {
                const double a = s.angleDeg*Pi/180.0;
                const Vec3 d{0.5*s.length*std::cos(a),0.5*s.length*std::sin(a),0};
                p.drawLine(project({c.x-d.x,c.y-d.y,c.z}),project({c.x+d.x,c.y+d.y,c.z}));
                break;
            }
            case ElectrostaticSource::Type::RectangularVolume:
            {
                const double a=s.angleDeg*Pi/180.0, ca=std::cos(a), sa=std::sin(a);
                const Vec3 u{0.5*s.width*ca,0.5*s.width*sa,0};
                const Vec3 v{-0.5*s.height*sa,0.5*s.height*ca,0};
                const double hz=0.5*std::max(s.thickness,1e-9);
                std::array<Vec3,8> q{{
                    {c.x-u.x-v.x,c.y-u.y-v.y,c.z-hz},{c.x+u.x-v.x,c.y+u.y-v.y,c.z-hz},
                    {c.x+u.x+v.x,c.y+u.y+v.y,c.z-hz},{c.x-u.x+v.x,c.y-u.y+v.y,c.z-hz},
                    {c.x-u.x-v.x,c.y-u.y-v.y,c.z+hz},{c.x+u.x-v.x,c.y+u.y-v.y,c.z+hz},
                    {c.x+u.x+v.x,c.y+u.y+v.y,c.z+hz},{c.x-u.x+v.x,c.y-u.y+v.y,c.z+hz}
                }};
                QColor fill=sc;fill.setAlpha(48);p.setBrush(fill);
                QPolygonF back,front;for(int k=0;k<4;++k)back<<project(q[static_cast<std::size_t>(k)]);for(int k=4;k<8;++k)front<<project(q[static_cast<std::size_t>(k)]);
                p.drawPolygon(back);p.drawPolygon(front);for(int k=0;k<4;++k)p.drawLine(project(q[static_cast<std::size_t>(k)]),project(q[static_cast<std::size_t>(k+4)]));
                break;
            }
            case ElectrostaticSource::Type::RectangularPlate:
            case ElectrostaticSource::Type::TriangularPlate:
            {
                const double a=s.angleDeg*Pi/180.0, ca=std::cos(a), sa=std::sin(a);
                const Vec3 u{0.5*s.width*ca,0.5*s.width*sa,0};
                const Vec3 v{-0.5*s.height*sa,0.5*s.height*ca,0};
                QPolygonF poly;
                if(s.type==ElectrostaticSource::Type::RectangularPlate)
                {
                    poly << project({c.x-u.x-v.x,c.y-u.y-v.y,c.z})
                         << project({c.x+u.x-v.x,c.y+u.y-v.y,c.z})
                         << project({c.x+u.x+v.x,c.y+u.y+v.y,c.z})
                         << project({c.x-u.x+v.x,c.y-u.y+v.y,c.z});
                }
                else
                {
                    const Vec3 ub{u.x,u.y,u.z};const Vec3 vb{v.x*(2.0/3.0),v.y*(2.0/3.0),0};
                    const Vec3 va{v.x*(4.0/3.0),v.y*(4.0/3.0),0};
                    poly << project({c.x-ub.x+vb.x,c.y-ub.y+vb.y,c.z})
                         << project({c.x+ub.x+vb.x,c.y+ub.y+vb.y,c.z})
                         << project({c.x-va.x,c.y-va.y,c.z});
                }
                QColor fill=sc; fill.setAlpha(55); p.setBrush(fill); p.drawPolygon(poly);
                break;
            }
            case ElectrostaticSource::Type::CircularPlate:
            case ElectrostaticSource::Type::AnnularPlate:
            {
                QPolygonF outer,inner;const double a0=s.angleDeg*Pi/180.0;
                for(int k=0;k<=64;++k){const double a=a0+2.0*Pi*k/64.0;outer<<project({c.x+s.radius*std::cos(a),c.y+s.radius*std::sin(a),c.z});}
                QColor fill=sc;fill.setAlpha(55);p.setBrush(fill);p.drawPolygon(outer);
                if(s.type==ElectrostaticSource::Type::AnnularPlate){for(int k=0;k<=64;++k){const double a=a0+2.0*Pi*k/64.0;inner<<project({c.x+s.innerRadius*std::cos(a),c.y+s.innerRadius*std::sin(a),c.z});}p.setBrush(pal.brush(QPalette::Base));p.drawPolygon(inner);}
                break;
            }
            case ElectrostaticSource::Type::InfinitePlane:
            {
                const double L=1.1*r;QPolygonF poly;poly<<project({c.x-L,c.y-L,c.z})<<project({c.x+L,c.y-L,c.z})<<project({c.x+L,c.y+L,c.z})<<project({c.x-L,c.y+L,c.z});
                QColor fill=sc;fill.setAlpha(28);p.setBrush(fill);p.drawPolygon(poly);break;
            }
            }
            p.setBrush(Qt::NoBrush);
            p.drawText(pc + QPointF(8,-8), s.name);
            p.restore();
            ++index;
        }
    }

    drawCross(p, project({m_measurementPoint.x,m_measurementPoint.y,m_measurementPoint.z}), pal.color(QPalette::Text), 7.0);
    drawHint(p, rect(), pal, QStringLiteral("electric-field"));
}

void ElectrostaticField3DView::mousePressEvent(QMouseEvent *e)
{
    m_lastMouse=e->pos();
    if(e->button()==Qt::RightButton){m_rightPressPos=e->pos();m_rightDragged=false;}
    m_panning=(e->button()==Qt::RightButton)||((e->button()==Qt::LeftButton)&&(e->modifiers()&Qt::ShiftModifier));
    if(e->button()==Qt::LeftButton && !m_panning)
    {
        const int hit=hitSource(e->position());
        if(hit>=0 && m_model)
        {
            m_selectedSource=hit;
            if(sourceSelected) sourceSelected(hit);
            if(const auto *source=m_model->source(hit))
            {
                m_draggingSource=true; m_dragSourceIndex=hit; m_dragSourceStart=source->position; m_dragSourcePreview=source->position;
                m_rotating=false; update(); e->accept(); return;
            }
        }
    }
    m_rotating=(e->button()==Qt::LeftButton)&&!m_panning;
    e->accept();
}
void ElectrostaticField3DView::mouseMoveEvent(QMouseEvent *e)
{
    if(m_draggingSource && (e->buttons()&Qt::LeftButton))
    {
        ElectrostaticVec3 q{}; if(screenToSourcePlane(e->position(),m_dragSourceStart,q)) m_dragSourcePreview=q; update(); e->accept(); return;
    }
    const QPoint d=e->pos()-m_lastMouse; m_lastMouse=e->pos();
    if((e->buttons()&Qt::RightButton)){const QPoint rd=e->pos()-m_rightPressPos;if(std::hypot(double(rd.x()),double(rd.y()))>4.0)m_rightDragged=true;}
    if(m_rotating){m_yawDeg+=d.x()*0.45;m_pitchDeg=std::clamp(m_pitchDeg-d.y()*0.45,-89.0,89.0);update();}
    else if(m_panning){m_panPx+=QPointF(d);update();}
    e->accept();
}
void ElectrostaticField3DView::mouseReleaseEvent(QMouseEvent *e)
{
    if(e->button()==Qt::LeftButton && m_draggingSource)
    {
        m_draggingSource=false; const int index=m_dragSourceIndex; m_dragSourceIndex=-1;
        if(sourceMoveRequested && index>=0) sourceMoveRequested(index,m_dragSourcePreview);
        update(); e->accept(); return;
    }
    m_rotating=false;m_panning=false;e->accept();
}
void ElectrostaticField3DView::wheelEvent(QWheelEvent *e){m_zoom=std::clamp(m_zoom*std::pow(1.0015,e->angleDelta().y()),0.18,8.0);update();e->accept();}
void ElectrostaticField3DView::mouseDoubleClickEvent(QMouseEvent *e){centerView();e->accept();}

void ElectrostaticField3DView::contextMenuEvent(QContextMenuEvent *e)
{
    if(m_rightDragged){m_rightDragged=false;e->ignore();return;}
    const int hit=hitSource(e->pos());if(hit>=0){m_selectedSource=hit;if(sourceSelected)sourceSelected(hit);}
    ElectrostaticVec3 position{};const bool canCreate=screenToCreationPlane(e->pos(),position);QMenu menu(this);
    std::vector<std::pair<QAction*,int>> add;
    if(canCreate){
        QMenu *create=menu.addMenu(QStringLiteral("Add electrostatic source here"));
        const std::array<ElectrostaticSource::Type,14> types{{ElectrostaticSource::Type::PointCharge,ElectrostaticSource::Type::SolidSphere,ElectrostaticSource::Type::SphericalShell,ElectrostaticSource::Type::ThickSphericalShell,ElectrostaticSource::Type::InfiniteCylinderZ,ElectrostaticSource::Type::InfiniteHollowCylinderZ,ElectrostaticSource::Type::InfiniteLine,ElectrostaticSource::Type::FiniteLine,ElectrostaticSource::Type::RectangularPlate,ElectrostaticSource::Type::RectangularVolume,ElectrostaticSource::Type::CircularPlate,ElectrostaticSource::Type::AnnularPlate,ElectrostaticSource::Type::TriangularPlate,ElectrostaticSource::Type::InfinitePlane}};
        for(const auto type:types)add.push_back({create->addAction(ElectrostaticModel::typeName(type)),static_cast<int>(type)});menu.addSeparator();}
    QAction *remove=nullptr;if(m_selectedSource>=0){QString name=QStringLiteral("selected source");if(m_model)if(const auto*s=m_model->source(m_selectedSource))name=s->name;remove=menu.addAction(QStringLiteral("Delete %1").arg(name));}
    QAction *chosen=menu.exec(e->globalPos());if(!chosen){e->accept();return;}
    for(const auto &entry:add)if(chosen==entry.first){if(sourceCreateRequested)sourceCreateRequested(entry.second,position);e->accept();return;}
    if(chosen==remove&&sourceDeleteRequested&&m_selectedSource>=0)sourceDeleteRequested(m_selectedSource);e->accept();
}

void ElectrostaticField3DView::keyPressEvent(QKeyEvent *e)
{
    if((e->key()==Qt::Key_Delete||e->key()==Qt::Key_Backspace)&&m_selectedSource>=0&&sourceDeleteRequested){sourceDeleteRequested(m_selectedSource);e->accept();return;}QWidget::keyPressEvent(e);
}

MagnetostaticField3DView::MagnetostaticField3DView(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(650,500); setMouseTracking(true); setFocusPolicy(Qt::StrongFocus);
}
void MagnetostaticField3DView::setModel(const MagnetostaticModel *model){m_model=model;update();}
void MagnetostaticField3DView::setSelectedSource(int index){m_selectedSource=index;update();}
void MagnetostaticField3DView::setMeasurementPoint(const MagnetostaticVec3 &point){m_measurementPoint=point;update();}
void MagnetostaticField3DView::setShowFieldVectors(bool show){m_showFieldVectors=show;update();}
void MagnetostaticField3DView::setEditPlane(int plane){m_editPlane=std::clamp(plane,0,2);update();}
void MagnetostaticField3DView::centerView(){m_yawDeg=-38;m_pitchDeg=27;m_zoom=1;m_panPx={};update();}

MagnetostaticField3DView::Vec3 MagnetostaticField3DView::rotate(const Vec3 &p) const
{
    const double yaw=m_yawDeg*Pi/180.0,pitch=m_pitchDeg*Pi/180.0;
    const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
    const double x1=cy*p.x-sy*p.y,y1=sy*p.x+cy*p.y;
    return {x1,cp*y1-sp*p.z,sp*y1+cp*p.z};
}
QPointF MagnetostaticField3DView::project(const Vec3 &p) const
{
    const Vec3 r=rotate(p); const double radius=std::max(0.1,sceneRadius());
    const double scale=0.36*std::min(width(),height())*m_zoom/radius;
    return {width()*0.5+m_panPx.x()+r.x*scale,height()*0.52+m_panPx.y()-r.z*scale};
}
double MagnetostaticField3DView::sceneRadius() const
{
    double r=1.6;
    if(m_model) for(const auto &s:m_model->sources()) r=std::max(r,norm3(s.position)+std::max({0.2,s.radius,0.5*s.length,0.5*s.width,0.5*s.height}));
    r=std::max(r,norm3(m_measurementPoint)); return 1.20*r;
}


int MagnetostaticField3DView::hitSource(const QPointF &screen) const
{
    if(!m_model)return -1; int best=-1; double bestDistance=14.0;
    for(int i=0;i<m_model->sourceCount();++i){const auto *source=m_model->source(i);if(!source)continue;const QPointF q=project({source->position.x,source->position.y,source->position.z});const double d=std::hypot(screen.x()-q.x(),screen.y()-q.y());if(d<bestDistance){bestDistance=d;best=i;}}
    return best;
}

bool MagnetostaticField3DView::screenToSourcePlane(const QPointF &screen,const MagnetostaticVec3 &origin,MagnetostaticVec3 &out) const
{
    const double radius=std::max(0.1,sceneRadius());const double scale=0.36*std::min(width(),height())*m_zoom/radius;if(!(scale>1e-12))return false;
    const double x1=(screen.x()-(width()*0.5+m_panPx.x()))/scale;const double z2=-(screen.y()-(height()*0.52+m_panPx.y()))/scale;
    const double yaw=m_yawDeg*Pi/180.0,pitch=m_pitchDeg*Pi/180.0,cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);const double y10=sp*z2;
    MagnetostaticVec3 p0{cy*x1+sy*y10,-sy*x1+cy*y10,cp*z2};const MagnetostaticVec3 dir{sy*cp,cy*cp,-sp};double denominator=0.0,numerator=0.0;
    if(m_editPlane==0){denominator=dir.z;numerator=origin.z-p0.z;}else if(m_editPlane==1){denominator=dir.y;numerator=origin.y-p0.y;}else{denominator=dir.x;numerator=origin.x-p0.x;}
    if(std::abs(denominator)<1e-10)return false;const double t=numerator/denominator;out={p0.x+dir.x*t,p0.y+dir.y*t,p0.z+dir.z*t};if(m_editPlane==0)out.z=origin.z;else if(m_editPlane==1)out.y=origin.y;else out.x=origin.x;return std::isfinite(out.x)&&std::isfinite(out.y)&&std::isfinite(out.z);
}

bool MagnetostaticField3DView::screenToCreationPlane(const QPointF &screen,MagnetostaticVec3 &out) const
{
    return screenToSourcePlane(screen,m_measurementPoint,out);
}

void MagnetostaticField3DView::paintEvent(QPaintEvent *)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing,true); p.fillRect(rect(),palette().color(QPalette::Base));
    const QPalette pal=palette(); const double r=sceneRadius();
    drawAxes(p,[this](double x,double y,double z){return project({x,y,z});},r,pal);
    p.save(); QColor grid=pal.color(QPalette::Mid);grid.setAlpha(55);p.setPen(QPen(grid,1));
    for(int i=-4;i<=4;++i){const double a=r*i/4.0;p.drawLine(project({-r,a,0}),project({r,a,0}));p.drawLine(project({a,-r,0}),project({a,r,0}));} p.restore();

    struct Sample{Vec3 pos,vec;double mag;}; std::vector<Sample> samples;
    double minLog=std::numeric_limits<double>::infinity(),maxLog=-std::numeric_limits<double>::infinity();
    if(m_model&&m_showFieldVectors){constexpr int N=5;samples.reserve(N*N*N);
        for(int iz=0;iz<N;++iz)for(int iy=0;iy<N;++iy)for(int ix=0;ix<N;++ix){
            const double x=-0.82*r+1.64*r*ix/(N-1),y=-0.82*r+1.64*r*iy/(N-1),z=-0.62*r+1.24*r*iz/(N-1);
            const auto f=m_model->fieldAt({x,y,z});const double mag=f.magneticFluxDensity.norm();
            if(f.singular||!std::isfinite(mag)||mag<=1e-30)continue;const double lm=std::log10(mag);minLog=std::min(minLog,lm);maxLog=std::max(maxLog,lm);
            samples.push_back({{x,y,z},{f.magneticFluxDensity.x,f.magneticFluxDensity.y,f.magneticFluxDensity.z},mag});}}
    const QColor fc=fieldColor(pal);
    for(const auto&s:samples){const double n=std::max(1e-30,s.mag);const Vec3 u{s.vec.x/n,s.vec.y/n,s.vec.z/n};const double lm=std::log10(n);
        const double t=(maxLog>minLog+1e-12)?(lm-minLog)/(maxLog-minLog):0.6;const double len=r*(0.055+0.11*std::clamp(t,0.0,1.0));
        const Vec3 b{s.pos.x+u.x*len,s.pos.y+u.y*len,s.pos.z+u.z*len};QColor c=fc;c.setAlphaF(0.28+0.62*std::clamp(t,0.0,1.0));drawArrow2D(p,project(s.pos),project(b),c,1.0+t);}

    if(m_model){int index=0;for(const auto&s:m_model->sources()){
        const MagnetostaticVec3 shown=(m_draggingSource&&index==m_dragSourceIndex)?m_dragSourcePreview:s.position; const Vec3 c{shown.x,shown.y,shown.z};const QPointF pc=project(c);const bool selected=index==m_selectedSource;
        QColor sc=positiveColor(pal);p.save();p.setPen(QPen(selected?pal.color(QPalette::Text):sc,selected?2.7:1.8));p.setBrush(Qt::NoBrush);
        const MagnetostaticVec3 ax0=MagnetostaticModel::axisUnit(s);const Vec3 ax{ax0.x,ax0.y,ax0.z};
        switch(s.type){
        case MagnetostaticSource::Type::InfiniteWireZ:
            p.drawLine(project({c.x,c.y,-1.5*r}),project({c.x,c.y,1.5*r}));break;
        case MagnetostaticSource::Type::SolidCurrentCylinderZ:
        case MagnetostaticSource::Type::HollowCurrentCylinderZ:{
            p.drawLine(project({c.x,c.y,-1.5*r}),project({c.x,c.y,1.5*r}));const QPointF pr=project({c.x+s.radius,c.y,c.z});const double rr=std::max(4.0,std::hypot(pr.x()-pc.x(),pr.y()-pc.y()));p.drawEllipse(pc,rr,rr);if(s.type==MagnetostaticSource::Type::HollowCurrentCylinderZ&&s.radius>0.0)p.drawEllipse(pc,rr*std::clamp(s.innerRadius/s.radius,0.0,0.999),rr*std::clamp(s.innerRadius/s.radius,0.0,0.999));break;}
        case MagnetostaticSource::Type::FiniteWire:{const Vec3 d{ax.x*s.length*0.5,ax.y*s.length*0.5,ax.z*s.length*0.5};p.drawLine(project({c.x-d.x,c.y-d.y,c.z-d.z}),project({c.x+d.x,c.y+d.y,c.z+d.z}));break;}
        case MagnetostaticSource::Type::CircularLoop:
        case MagnetostaticSource::Type::Solenoid:
        case MagnetostaticSource::Type::HelmholtzPair:{
            Vec3 ref=std::abs(ax.z)<0.85?Vec3{0,0,1}:Vec3{1,0,0};Vec3 u{ax.y*ref.z-ax.z*ref.y,ax.z*ref.x-ax.x*ref.z,ax.x*ref.y-ax.y*ref.x};
            double un=std::sqrt(u.x*u.x+u.y*u.y+u.z*u.z);u={u.x/un,u.y/un,u.z/un};Vec3 v{ax.y*u.z-ax.z*u.y,ax.z*u.x-ax.x*u.z,ax.x*u.y-ax.y*u.x};
            int rings=1;double spacing=0.0;if(s.type==MagnetostaticSource::Type::Solenoid){rings=5;spacing=s.length;}else if(s.type==MagnetostaticSource::Type::HelmholtzPair){rings=2;spacing=s.length;}
            for(int ring=0;ring<rings;++ring){double off=rings==1?0.0:spacing*(-0.5+double(ring)/(rings-1));QPolygonF poly;for(int k=0;k<=48;++k){double a=2*Pi*k/48.0;Vec3 q{c.x+ax.x*off+s.radius*(u.x*std::cos(a)+v.x*std::sin(a)),c.y+ax.y*off+s.radius*(u.y*std::cos(a)+v.y*std::sin(a)),c.z+ax.z*off+s.radius*(u.z*std::cos(a)+v.z*std::sin(a))};poly<<project(q);}p.drawPolyline(poly);}break;}
        case MagnetostaticSource::Type::RectangularLoop:
        case MagnetostaticSource::Type::TriangularLoop:{
            Vec3 ref=std::abs(ax.z)<0.85?Vec3{0,0,1}:Vec3{1,0,0};
            Vec3 u{ax.y*ref.z-ax.z*ref.y,ax.z*ref.x-ax.x*ref.z,ax.x*ref.y-ax.y*ref.x};
            double un=std::sqrt(u.x*u.x+u.y*u.y+u.z*u.z);u={u.x/un,u.y/un,u.z/un};
            Vec3 v{ax.y*u.z-ax.z*u.y,ax.z*u.x-ax.x*u.z,ax.x*u.y-ax.y*u.x};QPolygonF poly;
            if(s.type==MagnetostaticSource::Type::RectangularLoop){
                const Vec3 q0{c.x-u.x*s.width*.5-v.x*s.height*.5,c.y-u.y*s.width*.5-v.y*s.height*.5,c.z-u.z*s.width*.5-v.z*s.height*.5};
                const Vec3 q1{c.x+u.x*s.width*.5-v.x*s.height*.5,c.y+u.y*s.width*.5-v.y*s.height*.5,c.z+u.z*s.width*.5-v.z*s.height*.5};
                const Vec3 q2{c.x+u.x*s.width*.5+v.x*s.height*.5,c.y+u.y*s.width*.5+v.y*s.height*.5,c.z+u.z*s.width*.5+v.z*s.height*.5};
                const Vec3 q3{c.x-u.x*s.width*.5+v.x*s.height*.5,c.y-u.y*s.width*.5+v.y*s.height*.5,c.z-u.z*s.width*.5+v.z*s.height*.5};
                poly<<project(q0)<<project(q1)<<project(q2)<<project(q3)<<project(q0);
            }else{
                const Vec3 q0{c.x-u.x*s.width*.5-v.x*s.height/3.0,c.y-u.y*s.width*.5-v.y*s.height/3.0,c.z-u.z*s.width*.5-v.z*s.height/3.0};
                const Vec3 q1{c.x+u.x*s.width*.5-v.x*s.height/3.0,c.y+u.y*s.width*.5-v.y*s.height/3.0,c.z+u.z*s.width*.5-v.z*s.height/3.0};
                const Vec3 q2{c.x+v.x*2*s.height/3.0,c.y+v.y*2*s.height/3.0,c.z+v.z*2*s.height/3.0};
                poly<<project(q0)<<project(q1)<<project(q2)<<project(q0);
            }p.drawPolyline(poly);break;}
        case MagnetostaticSource::Type::InfiniteCurrentSheet:{const double L=1.1*r;QPolygonF poly;poly<<project({c.x-L,c.y-L,c.z})<<project({c.x+L,c.y-L,c.z})<<project({c.x+L,c.y+L,c.z})<<project({c.x-L,c.y+L,c.z});p.drawPolygon(poly);break;}
        case MagnetostaticSource::Type::MagneticDipole:{const double L=0.32*r;drawArrow2D(p,project({c.x-ax.x*L*0.5,c.y-ax.y*L*0.5,c.z-ax.z*L*0.5}),project({c.x+ax.x*L*0.5,c.y+ax.y*L*0.5,c.z+ax.z*L*0.5}),sc,2.2);break;}
        }
        p.drawEllipse(pc,4.0,4.0);p.drawText(pc+QPointF(8,-8),s.name);p.restore();++index;}}
    drawCross(p,project({m_measurementPoint.x,m_measurementPoint.y,m_measurementPoint.z}),pal.color(QPalette::Text),7.0);
    drawHint(p,rect(),pal,QStringLiteral("magnetic-field"));
}

void MagnetostaticField3DView::mousePressEvent(QMouseEvent*e){m_lastMouse=e->pos();if(e->button()==Qt::RightButton){m_rightPressPos=e->pos();m_rightDragged=false;}m_panning=(e->button()==Qt::RightButton)||((e->button()==Qt::LeftButton)&&(e->modifiers()&Qt::ShiftModifier));if(e->button()==Qt::LeftButton&&!m_panning){const int hit=hitSource(e->position());if(hit>=0&&m_model){m_selectedSource=hit;if(sourceSelected)sourceSelected(hit);if(const auto*source=m_model->source(hit)){m_draggingSource=true;m_dragSourceIndex=hit;m_dragSourceStart=source->position;m_dragSourcePreview=source->position;m_rotating=false;update();e->accept();return;}}}m_rotating=(e->button()==Qt::LeftButton)&&!m_panning;e->accept();}
void MagnetostaticField3DView::mouseMoveEvent(QMouseEvent*e){if(m_draggingSource&&(e->buttons()&Qt::LeftButton)){MagnetostaticVec3 q{};if(screenToSourcePlane(e->position(),m_dragSourceStart,q))m_dragSourcePreview=q;update();e->accept();return;}const QPoint d=e->pos()-m_lastMouse;m_lastMouse=e->pos();if((e->buttons()&Qt::RightButton)){const QPoint rd=e->pos()-m_rightPressPos;if(std::hypot(double(rd.x()),double(rd.y()))>4.0)m_rightDragged=true;}if(m_rotating){m_yawDeg+=d.x()*0.45;m_pitchDeg=std::clamp(m_pitchDeg-d.y()*0.45,-89.0,89.0);update();}else if(m_panning){m_panPx+=QPointF(d);update();}e->accept();}
void MagnetostaticField3DView::mouseReleaseEvent(QMouseEvent*e){if(e->button()==Qt::LeftButton&&m_draggingSource){m_draggingSource=false;const int index=m_dragSourceIndex;m_dragSourceIndex=-1;if(sourceMoveRequested&&index>=0)sourceMoveRequested(index,m_dragSourcePreview);update();e->accept();return;}m_rotating=false;m_panning=false;e->accept();}
void MagnetostaticField3DView::wheelEvent(QWheelEvent*e){m_zoom=std::clamp(m_zoom*std::pow(1.0015,e->angleDelta().y()),0.18,8.0);update();e->accept();}
void MagnetostaticField3DView::mouseDoubleClickEvent(QMouseEvent*e){centerView();e->accept();}

void MagnetostaticField3DView::contextMenuEvent(QContextMenuEvent *e)
{
    if(m_rightDragged){m_rightDragged=false;e->ignore();return;}const int hit=hitSource(e->pos());if(hit>=0){m_selectedSource=hit;if(sourceSelected)sourceSelected(hit);}
    MagnetostaticVec3 position{};const bool canCreate=screenToCreationPlane(e->pos(),position);QMenu menu(this);std::vector<std::pair<QAction*,int>>add;
    if(canCreate){QMenu *create=menu.addMenu(QStringLiteral("Add magnetostatic source here"));const std::array<MagnetostaticSource::Type,11>types{{MagnetostaticSource::Type::InfiniteWireZ,MagnetostaticSource::Type::FiniteWire,MagnetostaticSource::Type::SolidCurrentCylinderZ,MagnetostaticSource::Type::HollowCurrentCylinderZ,MagnetostaticSource::Type::CircularLoop,MagnetostaticSource::Type::RectangularLoop,MagnetostaticSource::Type::TriangularLoop,MagnetostaticSource::Type::Solenoid,MagnetostaticSource::Type::HelmholtzPair,MagnetostaticSource::Type::InfiniteCurrentSheet,MagnetostaticSource::Type::MagneticDipole}};for(const auto type:types)add.push_back({create->addAction(MagnetostaticModel::typeName(type)),static_cast<int>(type)});menu.addSeparator();}
    QAction*remove=nullptr;if(m_selectedSource>=0){QString name=QStringLiteral("selected source");if(m_model)if(const auto*s=m_model->source(m_selectedSource))name=s->name;remove=menu.addAction(QStringLiteral("Delete %1").arg(name));}
    QAction*chosen=menu.exec(e->globalPos());if(!chosen){e->accept();return;}for(const auto &entry:add)if(chosen==entry.first){if(sourceCreateRequested)sourceCreateRequested(entry.second,position);e->accept();return;}if(chosen==remove&&sourceDeleteRequested&&m_selectedSource>=0)sourceDeleteRequested(m_selectedSource);e->accept();
}

void MagnetostaticField3DView::keyPressEvent(QKeyEvent *e)
{
    if((e->key()==Qt::Key_Delete||e->key()==Qt::Key_Backspace)&&m_selectedSource>=0&&sourceDeleteRequested){sourceDeleteRequested(m_selectedSource);e->accept();return;}QWidget::keyPressEvent(e);
}

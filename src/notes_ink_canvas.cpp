#include "widgets/notes_ink_canvas.h"

#include <QEvent>
#include <QBuffer>
#include <QResizeEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygon>
#include <QStringList>
#include <QPen>
#include <QPointingDevice>
#include <QTabletEvent>

#include <algorithm>
#include <cmath>
#include <utility>
#include <limits>

namespace
{
double pointToSegmentDistance(QPointF p, QPointF a, QPointF b)
{
    const QPointF ab = b - a;
    const double ll = ab.x() * ab.x() + ab.y() * ab.y();
    if (ll <= 1.0e-12) return std::hypot((p - a).x(), (p - a).y());
    const double t = std::clamp(((p - a).x() * ab.x() + (p - a).y() * ab.y()) / ll, 0.0, 1.0);
    const QPointF near = a + ab * t;
    return std::hypot((p - near).x(), (p - near).y());
}
}

NotesInkCanvas::NotesInkCanvas(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("notesInkCanvas"));
    setMinimumSize(450, 330);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_AcceptTouchEvents, true);
}

bool NotesInkCanvas::event(QEvent *event)
{
    if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate ||
        event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel)
    {
        // Single-finger touches (including palm contacts) never draw ink.
        // Two-finger gestures are reserved for view navigation when the pen is inactive.
        auto *touch = static_cast<QTouchEvent *>(event);
        if ((event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate) &&
            !m_tabletStroke && touch->points().size() == 2)
        {
            const QPointF a = touch->points()[0].position();
            const QPointF b = touch->points()[1].position();
            const QPointF center = (a + b) / 2.0;
            const double dist = std::hypot(a.x()-b.x(), a.y()-b.y());
            if (!m_touchPinching)
            {
                m_touchPinching = true;
                m_touchPinchDistance = std::max(1.0, dist);
                m_touchStartZoom = m_zoom;
                m_touchPreviousCenter = center;
            }
            else
            {
                setZoomFactor(m_touchStartZoom * dist / m_touchPinchDistance, center);
                m_viewPan += center - m_touchPreviousCenter;
                clampPan();
                m_touchPreviousCenter = center;
                update();
            }
        }
        else if (event->type() != QEvent::TouchUpdate || touch->points().size() != 2)
            m_touchPinching = false;
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

void NotesInkCanvas::setTool(Tool tool)
{
    if (m_drawing || m_shapeDrawing || m_wireDrawing || m_marqueeSelecting ||
        m_movingSelection || m_resizingImage || m_panning) endStroke();
    m_tool = tool;
    if (tool != Tool::Select) resetSelection();
    setCursor(tool == Tool::Eraser || tool==Tool::Wire || isShapeTool(tool) ? Qt::CrossCursor :
              tool == Tool::Text ? Qt::IBeamCursor :
              tool == Tool::Select || tool == Tool::Hand ? Qt::OpenHandCursor :
              tool == Tool::Component ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}
void NotesInkCanvas::setPenColor(const QColor &color) { if (color.isValid()) m_color = color; }
void NotesInkCanvas::setPenWidth(double width) { if (std::isfinite(width)) m_width = std::clamp(width, 1.0, 24.0); }
void NotesInkCanvas::setTextSize(int size) { m_textSize = std::clamp(size, 12, 96); }
void NotesInkCanvas::setGridVisible(bool visible) { m_grid = visible; update(); }
bool NotesInkCanvas::canUndo() const noexcept { return !page().undo.isEmpty(); }
bool NotesInkCanvas::canRedo() const noexcept { return !page().redo.isEmpty(); }
int NotesInkCanvas::strokeCount() const noexcept { return page().strokes.size(); }
int NotesInkCanvas::textCount() const noexcept { return page().texts.size(); }
int NotesInkCanvas::shapeCount() const noexcept { return page().shapes.size(); }
int NotesInkCanvas::imageCount() const noexcept { return page().images.size(); }
int NotesInkCanvas::symbolCount() const noexcept { return page().symbols.size(); }
int NotesInkCanvas::wireCount() const noexcept
{
    int count = 0;
    for (const Shape &s : page().shapes) if (s.type == Tool::Wire) ++count;
    return count;
}
int NotesInkCanvas::selectedObjectCount() const noexcept { return m_groupSelection.size(); }

NotesInkCanvas::ContentState NotesInkCanvas::snapshot() const
{
    return {page().strokes, page().texts, page().shapes, page().images, page().symbols};
}
void NotesInkCanvas::applySnapshot(const ContentState &state)
{
    page().strokes=state.strokes; page().texts=state.texts; page().shapes=state.shapes;
    page().images=state.images; page().symbols=state.symbols;
}
void NotesInkCanvas::selectAllObjects()
{
    if (m_tool != Tool::Select) setTool(Tool::Select);
    resetSelection();
    for (int i=0; i<page().symbols.size(); ++i) m_groupSelection.append({SelectionKind::Symbol, i});
    for (int i=0; i<page().shapes.size(); ++i) m_groupSelection.append({SelectionKind::Shape, i});
    for (int i=0; i<page().texts.size(); ++i) m_groupSelection.append({SelectionKind::Text, i});
    for (int i=0; i<page().strokes.size(); ++i) m_groupSelection.append({SelectionKind::Stroke, i});
    for (int i=0; i<page().images.size(); ++i) m_groupSelection.append({SelectionKind::Image, i});
    if (!m_groupSelection.isEmpty())
    {
        m_selection=m_groupSelection.first().kind;
        m_selectionIndex=m_groupSelection.first().index;
    }
    update();
}



namespace
{
const QStringList &symbolIds()
{
    static const QStringList ids = {
        QStringLiteral("resistor"), QStringLiteral("resistor_us"),
        QStringLiteral("potentiometer"), QStringLiteral("capacitor"),
        QStringLiteral("capacitor_polar"), QStringLiteral("inductor"),
        QStringLiteral("transformer"), QStringLiteral("fuse"),
        QStringLiteral("bjt_npn"), QStringLiteral("bjt_pnp"),
        QStringLiteral("mos_n"), QStringLiteral("mos_p"),
        QStringLiteral("jfet_n"), QStringLiteral("jfet_p"),
        QStringLiteral("opamp"), QStringLiteral("diode"),
        QStringLiteral("zener"), QStringLiteral("led"),
        QStringLiteral("voltage_dc"), QStringLiteral("voltage_ac"),
        QStringLiteral("current_source"), QStringLiteral("battery"),
        QStringLiteral("ground"), QStringLiteral("switch"),
        QStringLiteral("junction"), QStringLiteral("crystal"),
        QStringLiteral("antenna")
    };
    return ids;
}
}

bool NotesInkCanvas::knownSymbol(const QString &id) { return symbolIds().contains(id); }
QString NotesInkCanvas::symbolDesignator(const QString &id)
{
    if (id.startsWith(QStringLiteral("resistor")) || id == QStringLiteral("potentiometer")) return QStringLiteral("R");
    if (id.startsWith(QStringLiteral("capacitor"))) return QStringLiteral("C");
    if (id == QStringLiteral("inductor")) return QStringLiteral("L");
    if (id == QStringLiteral("transformer")) return QStringLiteral("T");
    if (id.startsWith(QStringLiteral("bjt")) || id.startsWith(QStringLiteral("mos")) || id.startsWith(QStringLiteral("jfet"))) return QStringLiteral("Q");
    if (id == QStringLiteral("opamp")) return QStringLiteral("U");
    if (id == QStringLiteral("diode") || id == QStringLiteral("zener") || id == QStringLiteral("led")) return QStringLiteral("D");
    if (id == QStringLiteral("fuse")) return QStringLiteral("F");
    if (id == QStringLiteral("switch")) return QStringLiteral("S");
    if (id == QStringLiteral("crystal")) return QStringLiteral("Y");
    if (id == QStringLiteral("antenna")) return QStringLiteral("ANT");
    if (id == QStringLiteral("voltage_dc") || id == QStringLiteral("voltage_ac") || id == QStringLiteral("battery")) return QStringLiteral("V");
    if (id == QStringLiteral("current_source")) return QStringLiteral("I");
    return {};
}
QPointF NotesInkCanvas::snapSymbolPoint(QPointF pos) const
{
    if (m_symbolSnap)
    {
        pos.setX(std::round(pos.x()/20.0)*20.0);
        pos.setY(std::round(pos.y()/20.0)*20.0);
    }
    return pos;
}
void NotesInkCanvas::setSymbolType(const QString &id)
{
    if (knownSymbol(id)) m_symbolType = id;
}
void NotesInkCanvas::setSymbolScale(double scale)
{
    if (std::isfinite(scale)) m_symbolScale = std::clamp(scale, 0.65, 2.0);
}
QRectF NotesInkCanvas::symbolBounds(const SymbolItem &symbol) const
{
    // Conservative hit and selection region, including rotated three-terminal symbols.
    const double radius = 90.0 * symbol.scale;
    return QRectF(symbol.center-QPointF(radius,radius), QSizeF(2*radius, 2*radius));
}
int NotesInkCanvas::symbolAt(const QPointF &pos) const
{
    for (int i=page().symbols.size()-1; i>=0; --i)
    {
        const SymbolItem &item=page().symbols[i];
        if (!symbolBounds(item).contains(pos)) continue;
        // Prefer the body and connection leads rather than a huge square hitbox:
        // neighboring components in an electrical schematic can be close.
        const QPointF d=(pos-item.center)/item.scale;
        const double theta=-item.rotation*(std::acos(-1.0)/180.0);
        const double x=d.x()*std::cos(theta)-d.y()*std::sin(theta);
        const double y=d.x()*std::sin(theta)+d.y()*std::cos(theta);
        const bool body=std::abs(x)<=49.0 && std::abs(y)<=49.0;
        const bool horizontalLead=std::abs(x)<=77.0 && std::abs(y)<=10.0;
        const bool verticalLead=std::abs(x)<=12.0 && std::abs(y)<=77.0;
        if (body || horizontalLead || verticalLead) return i;
    }
    return -1;
}
QVector<QPointF> NotesInkCanvas::localPins(const QString &id) const
{
    if (id==QStringLiteral("transformer"))
        return {{-33,-70},{-33,70},{33,-70},{33,70}};
    if (id.startsWith(QStringLiteral("bjt_")))
        return {{-70,0},{23,-70},{23,70}};
    if (id.startsWith(QStringLiteral("mos_")) || id.startsWith(QStringLiteral("jfet_")))
        return {{-70,0},{10,-70},{10,70}};
    if (id==QStringLiteral("opamp"))
        return {{-70,-25},{-70,25},{70,0}};
    if (id==QStringLiteral("voltage_dc") || id==QStringLiteral("voltage_ac") ||
        id==QStringLiteral("current_source"))
        return {{0,-70},{0,70}};
    if (id==QStringLiteral("ground")) return {{0,-70}};
    if (id==QStringLiteral("antenna")) return {{0,70}};
    if (id==QStringLiteral("junction")) return {{-70,0},{70,0},{0,-70},{0,70}};
    if (id==QStringLiteral("potentiometer")) return {{-70,0},{70,0},{-7,-50}};
    return {{-70,0},{70,0}};
}
QPointF NotesInkCanvas::pinLocation(const SymbolItem &symbol, int pin) const
{
    const QVector<QPointF> pins=localPins(symbol.id);
    if (pin<0 || pin>=pins.size()) return symbol.center;
    const QPointF local=pins[pin]*symbol.scale;
    const double angle=symbol.rotation*(std::acos(-1.0)/180.0);
    return symbol.center+QPointF(local.x()*std::cos(angle)-local.y()*std::sin(angle),
                                 local.x()*std::sin(angle)+local.y()*std::cos(angle));
}
QPointF NotesInkCanvas::anchoredPoint(quint64 uid, int pin, const QPointF &fallback) const
{
    if (!uid) return fallback;
    for (const SymbolItem &item : page().symbols)
        if (item.uid==uid && pin>=0 && pin<localPins(item.id).size())
            return pinLocation(item,pin);
    return fallback; // orphaned connection is visibly retained as a free endpoint
}
NotesInkCanvas::Shape NotesInkCanvas::anchoredWire(const Shape &s) const
{
    if (s.type!=Tool::Wire) return s;
    Shape result=s;
    result.a=anchoredPoint(s.aSymbol,s.aPin,s.a);
    result.b=anchoredPoint(s.bSymbol,s.bPin,s.b);
    return result;
}
bool NotesInkCanvas::snapToTerminal(const QPointF &point, quint64 &uid, int &pin, QPointF &snapped) const
{
    double best=22.0; // page pixels; independence from screen DPI/zoom
    uid=0; pin=-1; snapped=point;
    for (const SymbolItem &symbol : page().symbols)
    {
        const QVector<QPointF> pins=localPins(symbol.id);
        for (int i=0;i<pins.size();++i)
        {
            const QPointF candidate=pinLocation(symbol,i);
            const double distance=std::hypot(point.x()-candidate.x(),point.y()-candidate.y());
            if (distance<best) { best=distance; uid=symbol.uid; pin=i; snapped=candidate; }
        }
    }
    return uid!=0;
}
bool NotesInkCanvas::addWire(const QPointF &from, const QPointF &to)
{
    if (page().shapes.size()>=500 || !std::isfinite(from.x()) || !std::isfinite(from.y()) ||
        !std::isfinite(to.x()) || !std::isfinite(to.y())) return false;
    Shape wire{Tool::Wire,from,to,m_color,m_width};
    snapToTerminal(from,wire.aSymbol,wire.aPin,wire.a);
    snapToTerminal(to,wire.bSymbol,wire.bPin,wire.b);
    wire.a.setX(std::clamp(wire.a.x(),0.0,PageW));
    wire.a.setY(std::clamp(wire.a.y(),0.0,PageH));
    wire.b.setX(std::clamp(wire.b.x(),0.0,PageW));
    wire.b.setY(std::clamp(wire.b.y(),0.0,PageH));
    if (std::hypot(wire.a.x()-wire.b.x(),wire.a.y()-wire.b.y())<3.0 ||
        (wire.aSymbol && wire.aSymbol==wire.bSymbol && wire.aPin==wire.bPin)) return false;
    Edit edit;
    edit.type=Edit::Type::AddShape;
    edit.index=page().shapes.size();
    edit.shape=wire;
    page().shapes.append(wire);
    commitEdit(std::move(edit));
    return true;
}

bool NotesInkCanvas::addSymbol(const QString &id, const QPointF &centre, double scale,
                               int rotationDegrees, const QString &reference)
{
    if (!knownSymbol(id) || page().symbols.size()>=400 || !std::isfinite(centre.x()) ||
        !std::isfinite(centre.y()) || !std::isfinite(scale) || scale<0.65 || scale>2.0 ||
        rotationDegrees%90!=0 || reference.size()>36 || reference.contains(QLatin1Char('\n'))) return false;
    const double radius=90.0*scale;
    QPointF p=snapSymbolPoint(centre);
    p.setX(std::clamp(p.x(),radius,PageW-radius));
    p.setY(std::clamp(p.y(),radius,PageH-radius));
    SymbolItem item{id,p,scale,((rotationDegrees%360)+360)%360,m_color,m_width,reference};
    item.uid=m_nextSymbolUid++;
    if (item.reference.isEmpty())
    {
        const QString prefix=symbolDesignator(id);
        if (!prefix.isEmpty())
        {
            int n=1;
            while (n<10000)
            {
                const QString candidate=prefix+QString::number(n);
                bool used=false;
                for (const SymbolItem &existing:page().symbols)
                    if (existing.reference==candidate) { used=true; break; }
                if (!used) { item.reference=candidate; break; }
                ++n;
            }
        }
    }
    Edit edit;
    edit.type=Edit::Type::AddSymbol;
    edit.index=page().symbols.size();
    edit.symbol=item;
    page().symbols.append(item);
    commitEdit(std::move(edit));
    return true;
}
bool NotesInkCanvas::hasSelectedSymbol() const noexcept
{
    return m_selection==SelectionKind::Symbol && m_selectionIndex>=0 && m_selectionIndex<page().symbols.size();
}
QString NotesInkCanvas::selectedSymbolReference() const
{
    return hasSelectedSymbol()?page().symbols[m_selectionIndex].reference:QString();
}
bool NotesInkCanvas::rotateSelectedSymbol(int degrees)
{
    if (!hasSelectedSymbol() || degrees%90!=0) return false;
    Edit edit;
    edit.type=Edit::Type::ChangeSymbol;
    edit.index=m_selectionIndex;
    edit.beforeSymbol=page().symbols[m_selectionIndex];
    edit.afterSymbol=edit.beforeSymbol;
    edit.afterSymbol.rotation=((edit.afterSymbol.rotation+degrees)%360+360)%360;
    page().symbols[m_selectionIndex]=edit.afterSymbol;
    commitEdit(std::move(edit));
    return true;
}
bool NotesInkCanvas::resizeSelectedSymbol(double scale)
{
    if (!hasSelectedSymbol() || !std::isfinite(scale) || scale<0.65 || scale>2.0) return false;
    Edit edit;
    edit.type=Edit::Type::ChangeSymbol;
    edit.index=m_selectionIndex;
    edit.beforeSymbol=page().symbols[m_selectionIndex];
    edit.afterSymbol=edit.beforeSymbol;
    edit.afterSymbol.scale=scale;
    const double radius=90.0*scale;
    edit.afterSymbol.center.setX(std::clamp(edit.afterSymbol.center.x(),radius,PageW-radius));
    edit.afterSymbol.center.setY(std::clamp(edit.afterSymbol.center.y(),radius,PageH-radius));
    page().symbols[m_selectionIndex]=edit.afterSymbol;
    commitEdit(std::move(edit));
    return true;
}
bool NotesInkCanvas::renameSelectedSymbol(const QString &reference)
{
    if (!hasSelectedSymbol() || reference.size()>36 || reference.contains(QLatin1Char('\n'))) return false;
    Edit edit;
    edit.type=Edit::Type::ChangeSymbol;
    edit.index=m_selectionIndex;
    edit.beforeSymbol=page().symbols[m_selectionIndex];
    edit.afterSymbol=edit.beforeSymbol;
    edit.afterSymbol.reference=reference.trimmed();
    page().symbols[m_selectionIndex]=edit.afterSymbol;
    commitEdit(std::move(edit));
    return true;
}
bool NotesInkCanvas::removeSymbolAt(int index)
{
    if (index<0 || index>=page().symbols.size()) return false;
    Edit edit;
    edit.type=Edit::Type::RemoveSymbol;
    edit.index=index;
    edit.symbol=page().symbols[index];
    edit.beforeShapes=page().shapes;
    // Detach connected wire endpoints as visible free ends before removing a symbol.
    // This avoids dangling IDs that would make a saved v6 notebook invalid.
    for (Shape &wire:page().shapes)
    {
        if (wire.type!=Tool::Wire) continue;
        const Shape visible=anchoredWire(wire);
        if (wire.aSymbol==edit.symbol.uid)
        { wire.a=visible.a;wire.aSymbol=0;wire.aPin=-1; }
        if (wire.bSymbol==edit.symbol.uid)
        { wire.b=visible.b;wire.bSymbol=0;wire.bPin=-1; }
    }
    edit.afterShapes=page().shapes;
    page().symbols.removeAt(index);
    resetSelection();
    commitEdit(std::move(edit));
    return true;
}
bool NotesInkCanvas::deleteSelectedSymbol()
{
    return hasSelectedSymbol() && removeSymbolAt(m_selectionIndex);
}

void NotesInkCanvas::clampPan()
{
    const double fit = std::min(double(width())/PageW, double(height())/PageH);
    const double displayedW = PageW * fit * m_zoom;
    const double displayedH = PageH * fit * m_zoom;
    const double xLimit = std::max(0.0, (displayedW - width()) * 0.5);
    const double yLimit = std::max(0.0, (displayedH - height()) * 0.5);
    m_viewPan.setX(std::clamp(m_viewPan.x(), -xLimit, xLimit));
    m_viewPan.setY(std::clamp(m_viewPan.y(), -yLimit, yLimit));
}

void NotesInkCanvas::setZoomFactor(double factor, const QPointF &anchor)
{
    if (!std::isfinite(factor)) return;
    const QPointF at = (anchor.x() >= 0.0 && anchor.y() >= 0.0)
        ? anchor : QPointF(width()/2.0, height()/2.0);
    const QPointF onPage = mapToPage(at);
    m_zoom = std::clamp(factor, 1.0, 6.0);
    const double fit = std::min(double(width())/PageW, double(height())/PageH);
    const QPointF center(width()/2.0, height()/2.0);
    m_viewPan = at - center - QPointF((onPage.x()-PageW/2.0)*fit*m_zoom,
                                      (onPage.y()-PageH/2.0)*fit*m_zoom);
    clampPan();
    if (m_onViewChanged) m_onViewChanged(m_zoom);
    update();
}

void NotesInkCanvas::zoomIn() { setZoomFactor(m_zoom*1.25); }
void NotesInkCanvas::zoomOut() { setZoomFactor(m_zoom/1.25); }
void NotesInkCanvas::fitPage() { m_zoom=1.0; m_viewPan={}; if (m_onViewChanged) m_onViewChanged(m_zoom); update(); }

void NotesInkCanvas::beginPan(const QPointF &widgetPos)
{
    m_panning=true;
    m_panOrigin=widgetPos;
    m_panStart=m_viewPan;
    setCursor(Qt::ClosedHandCursor);
}
void NotesInkCanvas::movePan(const QPointF &widgetPos)
{
    if (!m_panning) return;
    m_viewPan=m_panStart+widgetPos-m_panOrigin;
    clampPan();
    update();
}
void NotesInkCanvas::endPan()
{
    m_panning=false;
    setCursor(m_tool==Tool::Hand || m_tool==Tool::Select ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void NotesInkCanvas::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        const double steps = event->angleDelta().y() / 120.0;
        if (steps != 0.0) setZoomFactor(m_zoom*std::pow(1.15, steps), event->position());
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void NotesInkCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    clampPan();
}

bool NotesInkCanvas::addImage(const QImage &source, const QPointF &position)
{
    if (source.isNull() || page().images.size()>=24) return false;
    QImage scaled=source;
    if (scaled.width()>1400 || scaled.height()>900)
        scaled=scaled.scaled(1400,900,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    QByteArray png;
    for (int attempt=0; attempt<4; ++attempt)
    {
        png.clear();
        QBuffer buffer(&png);
        if (!buffer.open(QIODevice::WriteOnly) || !scaled.save(&buffer,"PNG")) return false;
        if (png.size()<=1500000) break;
        scaled=scaled.scaled(std::max(1,scaled.width()*3/4),std::max(1,scaled.height()*3/4),
                             Qt::KeepAspectRatio,Qt::SmoothTransformation);
    }
    if (png.size()>1500000 || serialize().size()+png.size()*2>23*1024*1024) return false;
    const double scale=std::min({1.0,PageW*0.80/scaled.width(),PageH*0.80/scaled.height()});
    const QSizeF size(scaled.width()*scale,scaled.height()*scale);
    const QPointF defaultPos((PageW-size.width())/2.0,(PageH-size.height())/2.0);
    QPointF pos = (position.x()<0 || position.y()<0) ? defaultPos : position;
    pos.setX(std::clamp(pos.x(),0.0,PageW-size.width()));
    pos.setY(std::clamp(pos.y(),0.0,PageH-size.height()));
    ImageItem img{pos,size,png,scaled};
    Edit edit;
    edit.type=Edit::Type::AddImage;
    edit.index=page().images.size();
    edit.image=img;
    page().images.append(std::move(img));
    commitEdit(std::move(edit));
    return true;
}

bool NotesInkCanvas::isShapeTool(Tool tool)
{
    return tool == Tool::Line || tool == Tool::Arrow ||
           tool == Tool::Rectangle || tool == Tool::Ellipse;
}

void NotesInkCanvas::resetSelection()
{
    m_selection = SelectionKind::None;
    m_selectionIndex = -1;
    m_movingSelection = false;
    m_resizingImage = false;
    m_groupSelection.clear();
    m_marqueeSelecting=false;
}

QRectF NotesInkCanvas::strokeBounds(const Stroke &stroke)
{
    if (stroke.samples.isEmpty()) return {};
    double xmin = stroke.samples.first().pos.x(), xmax = xmin;
    double ymin = stroke.samples.first().pos.y(), ymax = ymin;
    for (const Sample &sample : stroke.samples)
    {
        xmin = std::min(xmin, sample.pos.x()); xmax = std::max(xmax, sample.pos.x());
        ymin = std::min(ymin, sample.pos.y()); ymax = std::max(ymax, sample.pos.y());
    }
    return QRectF(QPointF(xmin, ymin), QPointF(xmax, ymax));
}

void NotesInkCanvas::addShape(Tool type, const QPointF &start, const QPointF &end)
{
    if (!isShapeTool(type) || page().shapes.size() >= 500 ||
        !std::isfinite(start.x()) || !std::isfinite(start.y()) ||
        !std::isfinite(end.x()) || !std::isfinite(end.y())) return;
    Shape shape{type, QPointF(std::clamp(start.x(), 0.0, PageW), std::clamp(start.y(), 0.0, PageH)),
                 QPointF(std::clamp(end.x(), 0.0, PageW), std::clamp(end.y(), 0.0, PageH)),
                 m_color, m_width};
    const double dx = shape.a.x() - shape.b.x(), dy = shape.a.y() - shape.b.y();
    if (std::hypot(dx, dy) < 3.0) return;
    if ((type == Tool::Rectangle || type == Tool::Ellipse) &&
        (std::abs(dx) < 3.0 || std::abs(dy) < 3.0)) return;
    Edit edit;
    edit.type = Edit::Type::AddShape;
    edit.index = page().shapes.size();
    edit.shape = shape;
    page().shapes.append(shape);
    commitEdit(std::move(edit));
}

bool NotesInkCanvas::setCurrentPage(int index)
{
    if (index < 0 || index >= m_pages.size()) return false;
    if (m_drawing || m_shapeDrawing || m_wireDrawing || m_marqueeSelecting ||
        m_movingSelection || m_resizingImage || m_panning) endStroke();
    if (index == m_currentPage) return true;
    resetSelection();
    m_currentPage = index;
    fitPage();
    modified(); // persist active page as part of notebook state
    return true;
}

bool NotesInkCanvas::addPage()
{
    if (m_pages.size() >= MaxPages) return false;
    if (m_drawing || m_shapeDrawing || m_wireDrawing || m_marqueeSelecting ||
        m_movingSelection || m_resizingImage || m_panning) endStroke();
    resetSelection();
    m_pages.insert(m_currentPage + 1, Page{});
    ++m_currentPage;
    fitPage();
    modified();
    return true;
}

bool NotesInkCanvas::removeCurrentPage()
{
    if (m_pages.size() <= 1) return false;
    if (m_drawing || m_shapeDrawing || m_wireDrawing || m_marqueeSelecting ||
        m_movingSelection || m_resizingImage || m_panning) endStroke();
    resetSelection();
    m_pages.removeAt(m_currentPage);
    m_currentPage = std::min(m_currentPage, int(m_pages.size()) - 1);
    fitPage();
    modified();
    return true;
}

QRectF NotesInkCanvas::pageRect() const
{
    const double scale = std::min(double(width()) / PageW, double(height()) / PageH) * m_zoom;
    const QSizeF size(PageW * scale, PageH * scale);
    return QRectF((width() - size.width()) * 0.5 + m_viewPan.x(), (height() - size.height()) * 0.5 + m_viewPan.y(), size.width(), size.height());
}

QPointF NotesInkCanvas::mapToPage(const QPointF &pos) const
{
    const QRectF paper = pageRect();
    if (paper.width() < 1.0 || paper.height() < 1.0) return {};
    return {(pos.x() - paper.left()) * PageW / paper.width(),
            (pos.y() - paper.top()) * PageH / paper.height()};
}

QPointF NotesInkCanvas::pageToWidget(const QPointF &pos) const
{
    const QRectF paper=pageRect();
    return QPointF(paper.left()+pos.x()*paper.width()/PageW,
                   paper.top()+pos.y()*paper.height()/PageH);
}

QRectF NotesInkCanvas::textBounds(const TextItem &item) const
{
    QFont font;
    font.setPixelSize(item.size);
    const QFontMetricsF metrics(font);
    const QRectF textRect = metrics.boundingRect(QRectF(0, 0, std::max(50.0, PageW - item.pos.x()), PageH),
                                                  Qt::TextWordWrap | Qt::AlignLeft, item.text);
    return QRectF(item.pos, textRect.size() + QSizeF(12, 8));
}

int NotesInkCanvas::textAt(const QPointF &pos) const
{
    for (int i = page().texts.size() - 1; i >= 0; --i)
        if (textBounds(page().texts[i]).contains(pos)) return i;
    return -1;
}

int NotesInkCanvas::imageAt(const QPointF &pos) const
{
    for (int i=page().images.size()-1; i>=0; --i)
        if (QRectF(page().images[i].pos,page().images[i].size).contains(pos)) return i;
    return -1;
}

int NotesInkCanvas::strokeAt(const QPointF &pos) const
{
    for (int i = page().strokes.size() - 1; i >= 0; --i)
    {
        const Stroke &s = page().strokes[i];
        const double radius = 12.0 + s.width * 0.5;
        for (int j = 0; j < s.samples.size(); ++j)
        {
            const QPointF a = s.samples[j].pos;
            const QPointF b = s.samples[std::max(0, j-1)].pos;
            if (pointToSegmentDistance(pos, a, b) <= radius) return i;
        }
    }
    return -1;
}

int NotesInkCanvas::shapeAt(const QPointF &pos) const
{
    for (int i = page().shapes.size() - 1; i >= 0; --i)
    {
        const Shape &s = page().shapes[i];
        const double tolerance = 12.0 + s.width * 0.5;
        double distance = std::numeric_limits<double>::infinity();
        if (s.type == Tool::Wire)
        {
            const Shape wire=anchoredWire(s);
            const QPointF mid((wire.a.x()+wire.b.x())/2.0,wire.a.y());
            const QPointF mid2((wire.a.x()+wire.b.x())/2.0,wire.b.y());
            distance=std::min({pointToSegmentDistance(pos,wire.a,mid),
                               pointToSegmentDistance(pos,mid,mid2),
                               pointToSegmentDistance(pos,mid2,wire.b)});
        }
        else if (s.type == Tool::Line || s.type == Tool::Arrow)
            distance = pointToSegmentDistance(pos, s.a, s.b);
        else if (s.type == Tool::Rectangle)
        {
            const QRectF r(s.a, s.b);
            const QRectF bounds = r.normalized();
            const QPointF tl = bounds.topLeft(), tr = bounds.topRight();
            const QPointF bl = bounds.bottomLeft(), br = bounds.bottomRight();
            distance = std::min({pointToSegmentDistance(pos, tl, tr), pointToSegmentDistance(pos, tr, br),
                                 pointToSegmentDistance(pos, br, bl), pointToSegmentDistance(pos, bl, tl)});
        }
        else if (s.type == Tool::Ellipse)
        {
            const QRectF r = QRectF(s.a, s.b).normalized();
            if (r.width() >= 1.0 && r.height() >= 1.0)
            {
                const QPointF center = r.center();
                const double rx = r.width()/2.0, ry = r.height()/2.0;
                constexpr int Steps = 64;
                const double pi = std::acos(-1.0);
                QPointF last(center.x() + rx, center.y());
                for (int j = 1; j <= Steps; ++j)
                {
                    const double angle = 2.0 * pi * j / Steps;
                    const QPointF next(center.x() + rx*std::cos(angle), center.y() + ry*std::sin(angle));
                    distance = std::min(distance, pointToSegmentDistance(pos, last, next));
                    last = next;
                }
            }
        }
        if (distance <= tolerance) return i;
    }
    return -1;
}

QRectF NotesInkCanvas::selectionBounds() const
{
    if (m_selectionIndex < 0) return {};
    switch (m_selection)
    {
    case SelectionKind::Text:
        if (m_selectionIndex < page().texts.size()) return textBounds(page().texts[m_selectionIndex]);
        break;
    case SelectionKind::Shape:
        if (m_selectionIndex < page().shapes.size())
        {
            const Shape &s = page().shapes[m_selectionIndex];
            const Shape rendered=anchoredWire(s);
            return QRectF(rendered.a, rendered.b).normalized();
        }
        break;
    case SelectionKind::Stroke:
        if (m_selectionIndex < page().strokes.size()) return strokeBounds(page().strokes[m_selectionIndex]);
        break;
    case SelectionKind::Image:
        if (m_selectionIndex < page().images.size())
            return QRectF(page().images[m_selectionIndex].pos,page().images[m_selectionIndex].size);
        break;
    case SelectionKind::Symbol:
        if (m_selectionIndex < page().symbols.size()) return symbolBounds(page().symbols[m_selectionIndex]);
        break;
    case SelectionKind::None: break;
    }
    return {};
}

QRectF NotesInkCanvas::boundsFor(const SelectionRef &ref) const
{
    if (ref.index<0) return {};
    switch (ref.kind)
    {
    case SelectionKind::Symbol:
        if (ref.index<page().symbols.size()) return symbolBounds(page().symbols[ref.index]);
        break;
    case SelectionKind::Image:
        if (ref.index<page().images.size())
            return QRectF(page().images[ref.index].pos,page().images[ref.index].size);
        break;
    case SelectionKind::Text:
        if (ref.index<page().texts.size()) return textBounds(page().texts[ref.index]);
        break;
    case SelectionKind::Stroke:
        if (ref.index<page().strokes.size()) return strokeBounds(page().strokes[ref.index]);
        break;
    case SelectionKind::Shape:
        if (ref.index<page().shapes.size())
        {
            const Shape s=anchoredWire(page().shapes[ref.index]);
            return QRectF(s.a,s.b).normalized();
        }
        break;
    case SelectionKind::None: break;
    }
    return {};
}
bool NotesInkCanvas::selectedRef(const SelectionRef &ref) const
{
    for (const SelectionRef &item:m_groupSelection)
        if (item.kind==ref.kind && item.index==ref.index) return true;
    return false;
}
void NotesInkCanvas::collectMarquee(const QRectF &area)
{
    if (area.width()<5.0 || area.height()<5.0) { resetSelection(); return; }
    resetSelection();
    const auto add=[&](SelectionKind kind,int count) {
        for (int i=0;i<count;++i)
        {
            const SelectionRef ref{kind,i};
            if (area.intersects(boundsFor(ref))) m_groupSelection.append(ref);
        }
    };
    add(SelectionKind::Symbol,page().symbols.size());
    add(SelectionKind::Shape,page().shapes.size());
    add(SelectionKind::Text,page().texts.size());
    add(SelectionKind::Image,page().images.size());
    add(SelectionKind::Stroke,page().strokes.size());
    if (!m_groupSelection.isEmpty())
    {
        m_selection=m_groupSelection.first().kind;
        m_selectionIndex=m_groupSelection.first().index;
    }
    update();
}
void NotesInkCanvas::beginSelection(const QPointF &pos, bool additive)
{
    SelectionRef hit;
    const int component=symbolAt(pos);
    if (component>=0) hit={SelectionKind::Symbol,component};
    else if (const int text=textAt(pos); text>=0) hit={SelectionKind::Text,text};
    else if (const int shape=shapeAt(pos); shape>=0) hit={SelectionKind::Shape,shape};
    else if (const int stroke=strokeAt(pos); stroke>=0) hit={SelectionKind::Stroke,stroke};
    else if (const int image=imageAt(pos); image>=0) hit={SelectionKind::Image,image};

    if (hit.kind==SelectionKind::None)
    {
        if (!additive) resetSelection();
        m_marqueeSelecting=true;
        m_marquee=QRectF(pos,pos);
        update();
        return;
    }
    if (additive)
    {
        if (selectedRef(hit))
        {
            for (int i=0;i<m_groupSelection.size();++i)
                if (m_groupSelection[i].kind==hit.kind && m_groupSelection[i].index==hit.index)
                { m_groupSelection.removeAt(i); break; }
        }
        else m_groupSelection.append(hit);
        m_selection=m_groupSelection.isEmpty()?SelectionKind::None:m_groupSelection.first().kind;
        m_selectionIndex=m_groupSelection.isEmpty()?-1:m_groupSelection.first().index;
        m_movingSelection=false;
        update();
        return;
    }
    if (!selectedRef(hit) || m_groupSelection.isEmpty())
    {
        resetSelection();
        m_groupSelection.append(hit);
    }
    m_selection=hit.kind;
    m_selectionIndex=hit.index;
    m_originalText={}; m_originalShape={}; m_originalStroke={};
    m_originalImage={}; m_originalSymbol={};
    if (hit.kind==SelectionKind::Text) m_originalText=page().texts[hit.index];
    else if (hit.kind==SelectionKind::Shape) m_originalShape=page().shapes[hit.index];
    else if (hit.kind==SelectionKind::Stroke) m_originalStroke=page().strokes[hit.index];
    else if (hit.kind==SelectionKind::Image) m_originalImage=page().images[hit.index];
    else if (hit.kind==SelectionKind::Symbol) m_originalSymbol=page().symbols[hit.index];
    m_groupOrigin=snapshot();
    m_dragOrigin=pos;
    m_movingSelection=true;
    setCursor(Qt::ClosedHandCursor);
    update();
}
void NotesInkCanvas::moveSelection(const QPointF &pos)
{
    if (!m_movingSelection || m_groupSelection.isEmpty()) return;
    QPointF delta=pos-m_dragOrigin;
    QRectF all;
    bool first=true;
    for (const SelectionRef &ref:m_groupSelection)
    {
        const QRectF bounds=boundsFor(ref);
        if (first) {all=bounds;first=false;} else all=all.united(bounds);
    }
    if (first) return;
    // The reference bounds must not change while dragging.
    // The offset is computed against the saved initial state, not live geometry.
    applySnapshot(m_groupOrigin);
    first=true;
    for (const SelectionRef &ref:m_groupSelection)
    {
        const QRectF bounds=boundsFor(ref);
        if (first) {all=bounds;first=false;} else all=all.united(bounds);
    }
    delta.setX(std::clamp(delta.x(),-all.left(),PageW-all.right()));
    delta.setY(std::clamp(delta.y(),-all.top(),PageH-all.bottom()));
    for (const SelectionRef &ref:m_groupSelection)
    {
        if (ref.kind==SelectionKind::Text)
            page().texts[ref.index].pos+=delta;
        else if (ref.kind==SelectionKind::Shape)
        {
            Shape &shape=page().shapes[ref.index];
            if (shape.type==Tool::Wire)
            {
                // Anchored endpoints follow symbols automatically; move only free ends.
                if (!shape.aSymbol) shape.a+=delta;
                if (!shape.bSymbol) shape.b+=delta;
            }
            else {shape.a+=delta;shape.b+=delta;}
        }
        else if (ref.kind==SelectionKind::Stroke)
            for (Sample &s:page().strokes[ref.index].samples) s.pos+=delta;
        else if (ref.kind==SelectionKind::Image)
            page().images[ref.index].pos+=delta;
        else if (ref.kind==SelectionKind::Symbol)
            page().symbols[ref.index].center+=delta;
    }
    update();
}

void NotesInkCanvas::resizeSelectedImage(const QPointF &paper, bool freeAspect)
{
    if (!m_resizingImage || m_selection!=SelectionKind::Image ||
        m_selectionIndex<0 || m_selectionIndex>=page().images.size()) return;
    const QSizeF old=m_originalImage.size;
    if (old.width()<1.0 || old.height()<1.0) return;
    const double maxW=std::max(1.0,PageW-m_originalImage.pos.x());
    const double maxH=std::max(1.0,PageH-m_originalImage.pos.y());
    double w=std::clamp(paper.x()-m_originalImage.pos.x(),std::min(32.0,maxW),maxW);
    double h=std::clamp(paper.y()-m_originalImage.pos.y(),std::min(32.0,maxH),maxH);
    if (!freeAspect)
    {
        const double ratio=old.width()/old.height();
        if (w/old.width()>h/old.height()) h=w/ratio;
        else w=h*ratio;
        const double factor=std::min({1.0,(PageW-m_originalImage.pos.x())/w,
                                     (PageH-m_originalImage.pos.y())/h});
        w*=factor; h*=factor;
    }
    page().images[m_selectionIndex].size=QSizeF(w,h);
    update();
}

void NotesInkCanvas::addTextAt(const QPointF &paperPosition, const QString &text)
{
    if (text.trimmed().isEmpty() || text.size() > 4000 || page().texts.size() >= 200 ||
        !std::isfinite(paperPosition.x()) || !std::isfinite(paperPosition.y())) return;
    const QPointF pos(std::clamp(paperPosition.x(), 0.0, PageW - 20.0),
                      std::clamp(paperPosition.y(), 0.0, PageH - 20.0));
    TextItem item{pos, text, m_color, m_textSize};
    Edit edit;
    edit.type = Edit::Type::AddText;
    edit.index = page().texts.size();
    edit.afterText = item;
    page().texts.append(std::move(item));
    commitEdit(std::move(edit));
}

void NotesInkCanvas::editTextAt(const QPointF &paperPos)
{
    const int index = textAt(paperPos);
    const QString previous = index >= 0 ? page().texts[index].text : QString();
    bool accepted = false;
    const QString text = QInputDialog::getMultiLineText(this,
        index >= 0 ? QStringLiteral("Edit text on page") : QStringLiteral("Insert text on page"),
        QStringLiteral("Text (blank removes an existing annotation):"), previous, &accepted);
    if (!accepted || text.size() > 4000) return;
    if (index < 0) { addTextAt(paperPos, text); return; }
    Edit edit;
    edit.index = index;
    edit.beforeText = page().texts[index];
    if (text.trimmed().isEmpty())
    {
        edit.type = Edit::Type::RemoveText;
        page().texts.removeAt(index);
    }
    else
    {
        if (text == previous) return;
        edit.type = Edit::Type::ChangeText;
        edit.afterText = page().texts[index];
        edit.afterText.text = text;
        page().texts[index] = edit.afterText;
    }
    commitEdit(std::move(edit));
}

void NotesInkCanvas::drawStroke(QPainter &p, const Stroke &stroke) const
{
    if (stroke.samples.isEmpty()) return;
    p.setPen(Qt::NoPen);
    const auto radius = [&stroke](double pressure) { return 0.5 * stroke.width * (0.48 + 0.52 * pressure); };
    p.setBrush(stroke.color);
    if (stroke.samples.size() == 1)
    {
        const Sample &s = stroke.samples.first();
        p.drawEllipse(s.pos, radius(s.pressure), radius(s.pressure));
        return;
    }
    for (int i = 1; i < stroke.samples.size(); ++i)
    {
        const Sample &a = stroke.samples[i - 1];
        const Sample &b = stroke.samples[i];
        QPen pen(stroke.color, stroke.width * (0.48 + 0.52 * (a.pressure + b.pressure) * 0.5),
                 Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.drawLine(a.pos, b.pos);
    }
}

void NotesInkCanvas::drawShape(QPainter &p, const Shape &shape) const
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(shape.color, shape.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const QRectF bounds = QRectF(shape.a, shape.b).normalized();
    switch (shape.type)
    {
    case Tool::Wire:
    {
        const Shape wire=anchoredWire(shape);
        const double xmid=(wire.a.x()+wire.b.x())*0.5;
        QPainterPath trace(wire.a);
        trace.lineTo(xmid,wire.a.y());
        trace.lineTo(xmid,wire.b.y());
        trace.lineTo(wire.b);
        p.drawPath(trace);
        break;
    }
    case Tool::Line: p.drawLine(shape.a, shape.b); break;
    case Tool::Arrow:
    {
        p.drawLine(shape.a, shape.b);
        const QPointF v = shape.b - shape.a;
        const double length = std::hypot(v.x(), v.y());
        if (length < 1.0) break;
        const QPointF dir = v / length;
        const QPointF normal(-dir.y(), dir.x());
        const double head = std::min(24.0, length * 0.35);
        p.drawLine(shape.b, shape.b - dir*head + normal*(head*0.40));
        p.drawLine(shape.b, shape.b - dir*head - normal*(head*0.40));
        break;
    }
    case Tool::Rectangle: p.drawRect(bounds); break;
    case Tool::Ellipse: p.drawEllipse(bounds); break;
    default: break;
    }
    p.restore();
}


void NotesInkCanvas::drawSymbol(QPainter &p, const SymbolItem &item) const
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(item.center);
    p.rotate(item.rotation);
    p.scale(item.scale,item.scale);
    p.setPen(QPen(item.color,item.lineWidth,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const auto line=[&p](double x1,double y1,double x2,double y2){p.drawLine(QPointF(x1,y1),QPointF(x2,y2));};
    const auto circle=[&p](double x,double y,double r){p.drawEllipse(QPointF(x,y),r,r);};
    const auto arrow=[&](QPointF from,QPointF tip){
        const QPointF v=tip-from;
        const double n=std::hypot(v.x(),v.y());
        if (n<0.01) return;
        const QPointF u=v/n, perp(-u.y(),u.x());
        p.drawLine(tip,tip-u*12+perp*5);
        p.drawLine(tip,tip-u*12-perp*5);
    };
    const auto leads=[&](){line(-70,0,-39,0);line(39,0,70,0);};
    const QString &id=item.id;
    if (id==QStringLiteral("resistor") || id==QStringLiteral("potentiometer"))
    {
        leads();p.drawRect(QRectF(-39,-14,78,28));
        if (id==QStringLiteral("potentiometer")) {line(-7,-50,1,-16);arrow(QPointF(-7,-50),QPointF(1,-16));}
    }
    else if (id==QStringLiteral("resistor_us"))
    {
        leads();QPainterPath path(QPointF(-39,0));
        for (int i=0;i<8;++i) path.lineTo(-34.0+i*9.0,(i%2==0?-14.0:14.0));
        path.lineTo(39,0);p.drawPath(path);
    }
    else if (id==QStringLiteral("capacitor") || id==QStringLiteral("capacitor_polar"))
    {
        line(-70,0,-13,0);line(13,0,70,0);
        line(-13,-32,-13,32);line(13,-32,13,32);
        if (id==QStringLiteral("capacitor_polar")) {line(-37,-39,-27,-39);line(-32,-44,-32,-34);}
    }
    else if (id==QStringLiteral("inductor"))
    {
        line(-70,0,-44,0);line(44,0,70,0);
        QPainterPath path(QPointF(-44,0));
        for (int i=0;i<4;++i) { double x=-44+i*22; path.cubicTo(x+1,-30,x+21,-30,x+22,0); }
        p.drawPath(path);
    }
    else if (id==QStringLiteral("transformer"))
    {
        for (double x:{-33.0,33.0})
        {
            line(x,-70,x,-42);line(x,42,x,70);
            QPainterPath coil(QPointF(x,-42));
            for(int i=0;i<4;++i)
            {
                const double y=-42+i*21.0;
                coil.cubicTo(x+(x<0?20:-20),y+3,x+(x<0?20:-20),y+18,x,y+21);
            }
            p.drawPath(coil);
        }
        line(-7,-43,-7,43);line(7,-43,7,43);
    }
    else if (id==QStringLiteral("fuse"))
    {
        line(-70,0,-35,0);line(35,0,70,0);p.drawRect(QRectF(-35,-12,70,24));line(-33,0,33,0);
    }
    else if (id==QStringLiteral("bjt_npn") || id==QStringLiteral("bjt_pnp"))
    {
        circle(0,0,38);line(-70,0,-12,0);line(-12,-26,-12,26);
        line(-12,-16,17,-33);line(17,-33,23,-70);
        line(-12,16,17,33);line(17,33,23,70);
        if (id==QStringLiteral("bjt_npn")) arrow(QPointF(-2,21),QPointF(17,33));
        else arrow(QPointF(17,33),QPointF(-2,21));
    }
    else if (id==QStringLiteral("mos_n") || id==QStringLiteral("mos_p") ||
             id==QStringLiteral("jfet_n") || id==QStringLiteral("jfet_p"))
    {
        const bool jfet=id.startsWith(QStringLiteral("jfet"));
        const bool ntype=id.endsWith(QStringLiteral("_n"));
        line(-70,0,-22,0);line(-22,-29,-22,29);
        line(10,-70,10,-27);line(10,27,10,70);
        if (jfet) line(10,-27,10,27);
        else {line(10,-27,10,-6);line(10,6,10,27);}
        // A MOS gate is isolated; a JFET gate meets its conducting channel.
        if (jfet)
        {
            line(-22,0,10,0);
            if (ntype) arrow(QPointF(-18,0),QPointF(4,0));
            else arrow(QPointF(5,0),QPointF(-17,0));
        }
        else
        {
            line(10,0,34,0);  // body contact and channel-type arrow
            if (ntype) arrow(QPointF(33,0),QPointF(13,0));
            else arrow(QPointF(12,0),QPointF(32,0));
        }
    }
    else if (id==QStringLiteral("opamp"))
    {
        QPolygonF tri;tri<<QPointF(-36,-48)<<QPointF(-36,48)<<QPointF(51,0);
        p.drawPolygon(tri);line(-70,-25,-36,-25);line(-70,25,-36,25);line(51,0,70,0);
        line(-23,-27,-13,-27);line(-23,23,-13,23);line(-18,18,-18,28);
    }
    else if (id==QStringLiteral("diode") || id==QStringLiteral("zener") || id==QStringLiteral("led"))
    {
        line(-70,0,-29,0);line(29,0,70,0);
        QPolygonF tri;tri<<QPointF(-29,-23)<<QPointF(-29,23)<<QPointF(19,0);
        p.drawPolygon(tri);line(20,-26,20,26);
        if (id==QStringLiteral("zener")){line(20,-26,30,-32);line(20,26,10,32);}
        if (id==QStringLiteral("led"))
        {
            line(0,-34,21,-55);arrow(QPointF(0,-34),QPointF(21,-55));
            line(22,-36,43,-57);arrow(QPointF(22,-36),QPointF(43,-57));
        }
    }
    else if (id==QStringLiteral("voltage_dc") || id==QStringLiteral("voltage_ac") ||
             id==QStringLiteral("current_source"))
    {
        line(0,-70,0,-33);line(0,33,0,70);circle(0,0,33);
        if (id==QStringLiteral("voltage_dc"))
        {line(-8,-15,8,-15);line(0,-23,0,-7);line(-8,18,8,18);}
        else if(id==QStringLiteral("voltage_ac"))
        {
            QPainterPath sine(QPointF(-23,0));
            sine.cubicTo(-17,-20,-7,-20,0,0);sine.cubicTo(7,20,17,20,23,0);p.drawPath(sine);
        }
        else {line(0,21,0,-21);arrow(QPointF(0,21),QPointF(0,-21));}
    }
    else if (id==QStringLiteral("battery"))
    {
        line(-70,0,-35,0);line(35,0,70,0);
        line(-35,-32,-35,32);line(-18,-15,-18,15);
        line(9,-32,9,32);line(26,-15,26,15);
        line(-53,-41,-43,-41);line(-48,-46,-48,-36);
    }
    else if (id==QStringLiteral("ground"))
    {
        line(0,-70,0,0);line(-38,0,38,0);line(-24,12,24,12);line(-10,24,10,24);
    }
    else if (id==QStringLiteral("switch"))
    {
        line(-70,0,-30,0);line(30,0,70,0);circle(-28,0,3.5);circle(28,0,3.5);line(-26,-4,21,-26);
    }
    else if (id==QStringLiteral("junction"))
    {
        line(-70,0,70,0);line(0,-70,0,70);p.setBrush(item.color);circle(0,0,6);
    }
    else if (id==QStringLiteral("crystal"))
    {
        line(-70,0,-38,0);line(38,0,70,0);
        line(-38,-29,-38,29);line(38,-29,38,29);p.drawRect(QRectF(-28,-20,56,40));
    }
    else if (id==QStringLiteral("antenna"))
    {
        line(0,70,0,-35);line(-26,-60,0,-35);line(26,-60,0,-35);
        p.drawArc(QRectF(-46,-68,92,70),35*16,110*16);
    }
    p.restore();
    if (!item.reference.isEmpty())
    {
        p.save();
        p.setPen(item.color);
        QFont font=p.font();font.setPixelSize(22);p.setFont(font);
        // Label in page coordinates; stays readable after component rotations.
        p.drawText(QRectF(item.center+QPointF(57*item.scale,-53*item.scale),QSizeF(150,35)),item.reference);
        p.restore();
    }
}

void NotesInkCanvas::drawContent(QPainter &p, bool drawPaper) const
{
    if (drawPaper)
    {
        p.fillRect(QRectF(0, 0, PageW, PageH), QColor(255, 255, 251));
        if (m_grid)
        {
            p.setPen(QPen(QColor(226, 232, 237), 1.0));
            for (int x = 0; x <= int(PageW); x += 40) p.drawLine(x, 0, x, int(PageH));
            for (int y = 0; y <= int(PageH); y += 40) p.drawLine(0, y, int(PageW), y);
        }
    }
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const ImageItem &img : page().images)
        if (!img.pixels.isNull()) p.drawImage(QRectF(img.pos,img.size),img.pixels,
                                                 QRectF(QPointF(0.0,0.0),QSizeF(img.pixels.size())));
    for (const Stroke &stroke : page().strokes) drawStroke(p, stroke);
    for (const Shape &shape : page().shapes) drawShape(p, shape);
    for (const SymbolItem &symbol : page().symbols) drawSymbol(p, symbol);
    if (m_drawing) drawStroke(p, m_activeStroke);
    if (m_shapeDrawing) drawShape(p, m_activeShape);
    if (m_wireDrawing) drawShape(p, m_activeWire);
    for (const TextItem &item : page().texts)
    {
        QFont font = p.font();
        font.setPixelSize(item.size);
        p.setFont(font);
        p.setPen(item.color);
        p.drawText(QRectF(item.pos, QSizeF(std::max(50.0, PageW - item.pos.x()),
                                        std::max(40.0, PageH - item.pos.y()))),
                   Qt::TextWordWrap | Qt::AlignLeft, item.text);
    }
}

void NotesInkCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(65, 68, 73));
    const QRectF paper = pageRect();
    p.save();
    p.translate(paper.left(), paper.top());
    p.scale(paper.width() / PageW, paper.height() / PageH);
    drawContent(p, true);
    if (m_tool==Tool::Wire)
    {
        p.setPen(QPen(QColor(25,129,86),2.0));
        p.setBrush(QColor(255,250,225));
        for (const SymbolItem &symbol:page().symbols)
        {
            const auto pins=localPins(symbol.id);
            for (int i=0;i<pins.size();++i) p.drawEllipse(pinLocation(symbol,i),6.0,6.0);
        }
    }
    if (m_tool==Tool::Select && m_marqueeSelecting)
    {
        p.setPen(QPen(QColor(28,123,200),2.0,Qt::DashLine));
        p.setBrush(QColor(28,123,200,28));
        p.drawRect(m_marquee.normalized());
    }
    if (m_tool==Tool::Select && m_groupSelection.size()>1)
    {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(28,123,200),2.0,Qt::DashLine));
        for (const SelectionRef &ref:m_groupSelection)
            p.drawRect(boundsFor(ref).normalized().adjusted(-7,-7,7,7));
    }
    if (m_tool == Tool::Select && m_groupSelection.size()<=1 && m_selection != SelectionKind::None)
    {
        const QRectF selection = selectionBounds().normalized().adjusted(-9, -9, 9, 9);
        p.setPen(QPen(QColor(28, 123, 200), 2.0, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(selection);
        if (m_selection==SelectionKind::Image)
        {
            const QRectF r=selectionBounds();
            p.setPen(QPen(QColor(28,123,200),2.0));
            p.setBrush(QColor(255,255,255));
            p.drawRect(QRectF(r.bottomRight()-QPointF(10,10),QSizeF(20,20)));
        }
    }
    p.restore();
}

void NotesInkCanvas::beginStroke(const QPointF &pos, double pressure, bool additive)
{
    if (m_tool==Tool::Hand && !m_tabletEraser) { beginPan(pos); return; }
    if (!pageRect().contains(pos)) return;
    const QPointF paper = mapToPage(pos);
    if (m_tool == Tool::Select && !m_tabletEraser)
    {
        if (m_selection==SelectionKind::Image && m_selectionIndex>=0 &&
            m_selectionIndex<page().images.size())
        {
            const QRectF bounds=QRectF(page().images[m_selectionIndex].pos,
                                        page().images[m_selectionIndex].size);
            if (QRectF(bounds.bottomRight()-QPointF(20,20),QSizeF(40,40)).contains(paper))
            {
                m_resizingImage=true;
                m_originalImage=page().images[m_selectionIndex];
                setCursor(Qt::SizeFDiagCursor);
                return;
            }
        }
        beginSelection(paper, additive);
        return;
    }
    if (m_tool==Tool::Wire && !m_tabletEraser)
    {
        if (page().shapes.size()>=500) return;
        m_activeWire={Tool::Wire,paper,paper,m_color,m_width};
        snapToTerminal(paper,m_activeWire.aSymbol,m_activeWire.aPin,m_activeWire.a);
        m_wireDrawing=true;
        update();
        return;
    }
    if (m_tool == Tool::Text && !m_tabletEraser)
    {
        // A modal keyboard dialog can intercept TabletRelease. Do not leave
        // the Surface Pen in an active stroke state after text insertion.
        m_tabletStroke = false;
        m_tabletEraser = false;
        editTextAt(paper);
        return;
    }
    if (m_tool == Tool::Component && !m_tabletEraser)
    {
        m_tabletStroke = false;
        addSymbol(m_symbolType,paper,m_symbolScale);
        return;
    }
    if (m_tool == Tool::Eraser || m_tabletEraser)
    {
        m_erasing = true;
        eraseAt(paper);
        return;
    }
    if (isShapeTool(m_tool) && !m_tabletEraser)
    {
        if (page().shapes.size() >= 500) return;
        m_activeShape = {m_tool, paper, paper, m_color, m_width};
        m_shapeDrawing = true;
        update();
        return;
    }
    if (page().strokes.size() >= 2000) return;
    m_activeStroke = {m_color, m_width, {}};
    m_activeStroke.samples.append({paper, std::clamp(pressure, 0.15, 1.0)});
    m_drawing = true;
    update();
}

void NotesInkCanvas::continueStroke(const QPointF &pos, double pressure, bool constrain)
{
    if (m_panning) { movePan(pos); return; }
    if (m_resizingImage) { resizeSelectedImage(mapToPage(pos),constrain); return; }
    if (m_erasing) { eraseAt(mapToPage(pos)); return; }
    if (m_marqueeSelecting)
    {
        m_marquee.setBottomRight(mapToPage(pos));
        update();
        return;
    }
    if (m_wireDrawing)
    {
        m_activeWire.b=mapToPage(pos);
        snapToTerminal(m_activeWire.b,m_activeWire.bSymbol,m_activeWire.bPin,m_activeWire.b);
        update();
        return;
    }
    if (m_movingSelection)
    {
        moveSelection(mapToPage(pos));
        return;
    }
    if (m_shapeDrawing)
    {
        const QPointF pt = mapToPage(pos);
        QPointF endpoint(std::clamp(pt.x(), 0.0, PageW), std::clamp(pt.y(), 0.0, PageH));
        if (constrain && (m_activeShape.type == Tool::Ellipse || m_activeShape.type == Tool::Rectangle))
        {
            const QPointF d = endpoint - m_activeShape.a;
            double side = std::max(std::abs(d.x()), std::abs(d.y()));
            const double maxX = d.x() < 0.0 ? m_activeShape.a.x() : PageW - m_activeShape.a.x();
            const double maxY = d.y() < 0.0 ? m_activeShape.a.y() : PageH - m_activeShape.a.y();
            side = std::min({side, maxX, maxY});
            endpoint = m_activeShape.a + QPointF(d.x() < 0.0 ? -side : side,
                                                  d.y() < 0.0 ? -side : side);
        }
        m_activeShape.b = endpoint;
        update();
        return;
    }
    if (!m_drawing || !pageRect().contains(pos)) return;
    const QPointF paper = mapToPage(pos);
    const auto &last = m_activeStroke.samples.last().pos;
    if (std::hypot(paper.x() - last.x(), paper.y() - last.y()) < 0.8) return;
    if (m_activeStroke.samples.size() >= 6000) return;
    m_activeStroke.samples.append({paper, std::clamp(pressure, 0.15, 1.0)});
    update();
}

void NotesInkCanvas::endStroke()
{
    if (m_panning) { endPan(); return; }
    if (m_resizingImage)
    {
        m_resizingImage=false;
        setCursor(Qt::OpenHandCursor);
        if (m_selection==SelectionKind::Image && m_selectionIndex>=0 &&
            m_selectionIndex<page().images.size() &&
            page().images[m_selectionIndex].size != m_originalImage.size)
        {
            Edit edit;
            edit.type=Edit::Type::ResizeImage;
            edit.index=m_selectionIndex;
            edit.beforeImage=m_originalImage;
            edit.afterImage=page().images[m_selectionIndex];
            commitEdit(std::move(edit));
        }
        return;
    }
    if (m_marqueeSelecting)
    {
        m_marqueeSelecting=false;
        collectMarquee(m_marquee.normalized());
        update();
        return;
    }
    if (m_wireDrawing)
    {
        m_wireDrawing=false;
        addWire(m_activeWire.a,m_activeWire.b);
        update();
        return;
    }
    if (m_shapeDrawing)
    {
        m_shapeDrawing = false;
        const Shape shape = m_activeShape;
        addShape(shape.type, shape.a, shape.b);
    }
    if (m_movingSelection && m_groupSelection.size()>1)
    {
        const ContentState after=snapshot();
        bool changed=false;
        for (const SelectionRef &ref:m_groupSelection)
        {
            if (ref.kind==SelectionKind::Symbol)
                changed=changed || (m_groupOrigin.symbols[ref.index].center!=page().symbols[ref.index].center);
            else if (ref.kind==SelectionKind::Image)
                changed=changed || (m_groupOrigin.images[ref.index].pos!=page().images[ref.index].pos);
            else if (ref.kind==SelectionKind::Text)
                changed=changed || (m_groupOrigin.texts[ref.index].pos!=page().texts[ref.index].pos);
            else if (ref.kind==SelectionKind::Stroke && !page().strokes[ref.index].samples.isEmpty())
                changed=changed || (m_groupOrigin.strokes[ref.index].samples.first().pos!=page().strokes[ref.index].samples.first().pos);
            else if (ref.kind==SelectionKind::Shape)
                changed=changed || (m_groupOrigin.shapes[ref.index].a!=page().shapes[ref.index].a ||
                                    m_groupOrigin.shapes[ref.index].b!=page().shapes[ref.index].b);
        }
        if (changed)
        {
            Edit edit;
            edit.type=Edit::Type::MoveGroup;
            edit.beforeGroup=m_groupOrigin;
            edit.afterGroup=after;
            commitEdit(std::move(edit));
        }
        m_movingSelection=false;
        setCursor(Qt::OpenHandCursor);
    }
    else if (m_movingSelection && m_selectionIndex >= 0)
    {
        Edit edit;
        edit.index = m_selectionIndex;
        bool changed = false;
        if (m_selection == SelectionKind::Text && edit.index < page().texts.size())
        {
            edit.type = Edit::Type::MoveText;
            edit.beforeText = m_originalText;
            edit.afterText = page().texts[edit.index];
            changed = edit.beforeText.pos != edit.afterText.pos;
        }
        else if (m_selection == SelectionKind::Shape && edit.index < page().shapes.size())
        {
            edit.type = Edit::Type::MoveShape;
            edit.beforeShape = m_originalShape;
            edit.afterShape = page().shapes[edit.index];
            changed = edit.beforeShape.a != edit.afterShape.a || edit.beforeShape.b != edit.afterShape.b;
        }
        else if (m_selection == SelectionKind::Stroke && edit.index < page().strokes.size())
        {
            edit.type = Edit::Type::MoveStroke;
            edit.beforeStroke = m_originalStroke;
            edit.afterStroke = page().strokes[edit.index];
            changed = !edit.beforeStroke.samples.isEmpty() && !edit.afterStroke.samples.isEmpty() &&
                      edit.beforeStroke.samples.first().pos != edit.afterStroke.samples.first().pos;
        }
        else if (m_selection == SelectionKind::Image && edit.index < page().images.size())
        {
            edit.type=Edit::Type::MoveImage;
            edit.beforeImage=m_originalImage;
            edit.afterImage=page().images[edit.index];
            changed=edit.beforeImage.pos!=edit.afterImage.pos;
        }
        else if (m_selection == SelectionKind::Symbol && edit.index < page().symbols.size())
        {
            edit.type=Edit::Type::ChangeSymbol;
            edit.beforeSymbol=m_originalSymbol;
            edit.afterSymbol=page().symbols[edit.index];
            changed=edit.beforeSymbol.center!=edit.afterSymbol.center;
        }
        if (changed) commitEdit(std::move(edit));
        m_movingSelection = false;
        setCursor(Qt::OpenHandCursor);
    }
    if (m_drawing && !m_activeStroke.samples.isEmpty())
    {
        Edit edit;
        edit.type = Edit::Type::AddStroke;
        edit.index = page().strokes.size();
        edit.stroke = std::move(m_activeStroke);
        page().strokes.push_back(edit.stroke);
        commitEdit(std::move(edit));
    }
    m_drawing = false;
    m_erasing = false;
    update();
}

void NotesInkCanvas::eraseAt(const QPointF &paper)
{
    const int component=symbolAt(paper);
    if (component>=0)
    {
        removeSymbolAt(component);
        return;
    }
    const int ti = textAt(paper);
    if (ti >= 0)
    {
        Edit edit;
        edit.type = Edit::Type::RemoveText;
        edit.index = ti;
        edit.beforeText = page().texts.takeAt(ti);
        commitEdit(std::move(edit));
        return;
    }
    const int shapeIndex = shapeAt(paper);
    if (shapeIndex >= 0)
    {
        Edit edit;
        edit.type = Edit::Type::RemoveShape;
        edit.index = shapeIndex;
        edit.shape = page().shapes.takeAt(shapeIndex);
        resetSelection();
        commitEdit(std::move(edit));
        return;
    }
    for (int i = page().strokes.size() - 1; i >= 0; --i)
    {
        const Stroke &s = page().strokes[i];
        const double radius = 19.0 + s.width * 0.5;
        bool hit = false;
        for (int j = 0; j < s.samples.size() && !hit; ++j)
        {
            const QPointF a = s.samples[j].pos;
            const QPointF b = s.samples[std::max(0, j - 1)].pos;
            hit = pointToSegmentDistance(paper, a, b) <= radius;
        }
        if (!hit) continue;
        Edit edit;
        edit.type = Edit::Type::RemoveStroke;
        edit.index = i;
        edit.stroke = page().strokes.takeAt(i);
        commitEdit(std::move(edit));
        return;
    }
    const int ii=imageAt(paper);
    if (ii>=0)
    {
        Edit edit;
        edit.type=Edit::Type::RemoveImage;
        edit.index=ii;
        edit.image=page().images.takeAt(ii);
        resetSelection();
        commitEdit(std::move(edit));
    }
}

void NotesInkCanvas::commitEdit(Edit edit)
{
    page().redo.clear();
    page().undo.append(std::move(edit));
    if (page().undo.size() > 40) page().undo.removeFirst();
    modified();
}

void NotesInkCanvas::modified()
{
    if (m_onModified) m_onModified();
    update();
}

void NotesInkCanvas::undo()
{
    if (m_drawing || m_shapeDrawing || m_wireDrawing || m_marqueeSelecting ||
        m_movingSelection || m_resizingImage || m_panning) endStroke();
    if (page().undo.isEmpty()) return;
    resetSelection();
    Edit edit = page().undo.takeLast();
    switch (edit.type)
    {
    case Edit::Type::AddStroke:
        if (edit.index >= 0 && edit.index < page().strokes.size()) page().strokes.removeAt(edit.index);
        break;
    case Edit::Type::RemoveStroke:
        page().strokes.insert(std::clamp(edit.index, 0, int(page().strokes.size())), edit.stroke);
        break;
    case Edit::Type::AddText:
        if (edit.index >= 0 && edit.index < page().texts.size()) page().texts.removeAt(edit.index);
        break;
    case Edit::Type::RemoveText:
    case Edit::Type::ChangeText:
        if (edit.type == Edit::Type::RemoveText)
            page().texts.insert(std::clamp(edit.index, 0, int(page().texts.size())), edit.beforeText);
        else if (edit.index >= 0 && edit.index < page().texts.size()) page().texts[edit.index] = edit.beforeText;
        break;
    case Edit::Type::AddShape:
        if (edit.index >= 0 && edit.index < page().shapes.size()) page().shapes.removeAt(edit.index);
        break;
    case Edit::Type::RemoveShape:
        page().shapes.insert(std::clamp(edit.index, 0, int(page().shapes.size())), edit.shape);
        break;
    case Edit::Type::MoveText:
        if (edit.index >= 0 && edit.index < page().texts.size()) page().texts[edit.index] = edit.beforeText;
        break;
    case Edit::Type::MoveShape:
        if (edit.index >= 0 && edit.index < page().shapes.size()) page().shapes[edit.index] = edit.beforeShape;
        break;
    case Edit::Type::MoveStroke:
        if (edit.index >= 0 && edit.index < page().strokes.size()) page().strokes[edit.index] = edit.beforeStroke;
        break;
    case Edit::Type::AddImage:
        if (edit.index>=0 && edit.index<page().images.size()) page().images.removeAt(edit.index);
        break;
    case Edit::Type::RemoveImage:
        page().images.insert(std::clamp(edit.index,0,int(page().images.size())),edit.image);
        break;
    case Edit::Type::ResizeImage:
    case Edit::Type::MoveImage:
        if (edit.index>=0 && edit.index<page().images.size()) page().images[edit.index]=edit.beforeImage;
        break;
    case Edit::Type::AddSymbol:
        if (edit.index>=0 && edit.index<page().symbols.size()) page().symbols.removeAt(edit.index);
        break;
    case Edit::Type::RemoveSymbol:
        page().symbols.insert(std::clamp(edit.index,0,int(page().symbols.size())),edit.symbol);
        page().shapes=edit.beforeShapes;
        break;
    case Edit::Type::ChangeSymbol:
        if (edit.index>=0 && edit.index<page().symbols.size()) page().symbols[edit.index]=edit.beforeSymbol;
        break;
    case Edit::Type::MoveGroup:
        applySnapshot(edit.beforeGroup);
        break;
    case Edit::Type::Clear:
        page().images=edit.previousImages;
        page().symbols=edit.previousSymbols;
        page().strokes = edit.previousStrokes;
        page().texts = edit.previousTexts;
        page().shapes = edit.previousShapes;
        break;
    }
    page().redo.append(std::move(edit));
    modified();
}

void NotesInkCanvas::redo()
{
    if (m_drawing || m_shapeDrawing || m_wireDrawing || m_marqueeSelecting ||
        m_movingSelection || m_resizingImage || m_panning) endStroke();
    if (page().redo.isEmpty()) return;
    resetSelection();
    Edit edit = page().redo.takeLast();
    switch (edit.type)
    {
    case Edit::Type::AddStroke:
        page().strokes.insert(std::clamp(edit.index, 0, int(page().strokes.size())), edit.stroke);
        break;
    case Edit::Type::RemoveStroke:
        if (edit.index >= 0 && edit.index < page().strokes.size()) page().strokes.removeAt(edit.index);
        break;
    case Edit::Type::AddText:
        page().texts.insert(std::clamp(edit.index, 0, int(page().texts.size())), edit.afterText);
        break;
    case Edit::Type::RemoveText:
        if (edit.index >= 0 && edit.index < page().texts.size()) page().texts.removeAt(edit.index);
        break;
    case Edit::Type::ChangeText:
        if (edit.index >= 0 && edit.index < page().texts.size()) page().texts[edit.index] = edit.afterText;
        break;
    case Edit::Type::AddShape:
        page().shapes.insert(std::clamp(edit.index, 0, int(page().shapes.size())), edit.shape);
        break;
    case Edit::Type::RemoveShape:
        if (edit.index >= 0 && edit.index < page().shapes.size()) page().shapes.removeAt(edit.index);
        break;
    case Edit::Type::MoveText:
        if (edit.index >= 0 && edit.index < page().texts.size()) page().texts[edit.index] = edit.afterText;
        break;
    case Edit::Type::MoveShape:
        if (edit.index >= 0 && edit.index < page().shapes.size()) page().shapes[edit.index] = edit.afterShape;
        break;
    case Edit::Type::MoveStroke:
        if (edit.index >= 0 && edit.index < page().strokes.size()) page().strokes[edit.index] = edit.afterStroke;
        break;
    case Edit::Type::AddImage:
        page().images.insert(std::clamp(edit.index,0,int(page().images.size())),edit.image);
        break;
    case Edit::Type::RemoveImage:
        if (edit.index>=0 && edit.index<page().images.size()) page().images.removeAt(edit.index);
        break;
    case Edit::Type::ResizeImage:
    case Edit::Type::MoveImage:
        if (edit.index>=0 && edit.index<page().images.size()) page().images[edit.index]=edit.afterImage;
        break;
    case Edit::Type::AddSymbol:
        page().symbols.insert(std::clamp(edit.index,0,int(page().symbols.size())),edit.symbol);
        break;
    case Edit::Type::RemoveSymbol:
        if (edit.index>=0 && edit.index<page().symbols.size()) page().symbols.removeAt(edit.index);
        page().shapes=edit.afterShapes;
        break;
    case Edit::Type::ChangeSymbol:
        if (edit.index>=0 && edit.index<page().symbols.size()) page().symbols[edit.index]=edit.afterSymbol;
        break;
    case Edit::Type::MoveGroup:
        applySnapshot(edit.afterGroup);
        break;
    case Edit::Type::Clear:
        page().strokes.clear(); page().texts.clear(); page().shapes.clear(); page().images.clear(); page().symbols.clear();
        break;
    }
    page().undo.append(std::move(edit));
    modified();
}

void NotesInkCanvas::clearDocument()
{
    if (m_drawing || m_shapeDrawing || m_wireDrawing || m_marqueeSelecting ||
        m_movingSelection || m_resizingImage || m_panning) endStroke();
    if (page().strokes.isEmpty() && page().texts.isEmpty() && page().shapes.isEmpty() && page().images.isEmpty() && page().symbols.isEmpty()) return;
    resetSelection();
    Edit edit;
    edit.type = Edit::Type::Clear;
    edit.previousStrokes = std::move(page().strokes);
    edit.previousTexts = std::move(page().texts);
    edit.previousShapes = std::move(page().shapes);
    edit.previousImages = std::move(page().images);
    edit.previousSymbols = std::move(page().symbols);
    page().strokes.clear();
    page().texts.clear();
    page().shapes.clear();
    page().images.clear();
    page().symbols.clear();
    commitEdit(std::move(edit));
}

void NotesInkCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button()==Qt::MiddleButton)
    {
        beginPan(event->position());
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && event->source() != Qt::MouseEventSynthesizedByQt && !m_tabletStroke)
    {
        beginStroke(event->position(), 1.0, bool(event->modifiers() & Qt::ShiftModifier));
        event->accept();
    }
    else QWidget::mousePressEvent(event);
}
void NotesInkCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning && (event->buttons() & Qt::MiddleButton))
    {
        movePan(event->position());
        event->accept();
        return;
    }
    if ((event->buttons() & Qt::LeftButton) && event->source() != Qt::MouseEventSynthesizedByQt && !m_tabletStroke)
    {
        continueStroke(event->position(), 1.0, bool(event->modifiers() & Qt::ShiftModifier));
        event->accept();
    }
    else QWidget::mouseMoveEvent(event);
}
void NotesInkCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button()==Qt::MiddleButton && m_panning)
    {
        endPan();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && !m_tabletStroke)
    {
        endStroke();
        event->accept();
    }
    else QWidget::mouseReleaseEvent(event);
}

void NotesInkCanvas::tabletEvent(QTabletEvent *event)
{
    const bool eraserTip = event->pointerType() == QPointingDevice::PointerType::Eraser;
    if (event->type() == QEvent::TabletPress)
    {
        m_tabletStroke = true;
        m_tabletEraser = eraserTip;
        beginStroke(event->position(), event->pressure(), bool(event->modifiers() & Qt::ShiftModifier));
    }
    else if (event->type() == QEvent::TabletMove && m_tabletStroke)
        continueStroke(event->position(), event->pressure(), bool(event->modifiers() & Qt::ShiftModifier));
    else if (event->type() == QEvent::TabletRelease)
    {
        endStroke();
        m_tabletStroke = false;
        m_tabletEraser = false;
    }
    event->accept();
}

QByteArray NotesInkCanvas::serialize() const
{
    QJsonArray pages;
    for (const Page &p : m_pages)
    {
        QJsonArray strokes;
        for (const Stroke &stroke : p.strokes)
        {
            QJsonArray samples;
            for (const Sample &s : stroke.samples)
            {
                QJsonArray point;
                point.append(s.pos.x()); point.append(s.pos.y()); point.append(s.pressure);
                samples.append(point);
            }
            QJsonObject entry;
            entry.insert(QStringLiteral("color"), stroke.color.name(QColor::HexRgb));
            entry.insert(QStringLiteral("width"), stroke.width);
            entry.insert(QStringLiteral("points"), samples);
            strokes.append(entry);
        }
        QJsonArray texts;
        for (const TextItem &item : p.texts)
        {
            QJsonObject entry;
            entry.insert(QStringLiteral("x"), item.pos.x());
            entry.insert(QStringLiteral("y"), item.pos.y());
            entry.insert(QStringLiteral("text"), item.text);
            entry.insert(QStringLiteral("color"), item.color.name(QColor::HexRgb));
            entry.insert(QStringLiteral("size"), item.size);
            texts.append(entry);
        }
        QJsonArray shapes;
        for (const Shape &shape : p.shapes)
        {
            QJsonObject entry;
            QString type;
            switch (shape.type)
            {
            case Tool::Line: type = QStringLiteral("line"); break;
            case Tool::Arrow: type = QStringLiteral("arrow"); break;
            case Tool::Rectangle: type = QStringLiteral("rectangle"); break;
            case Tool::Ellipse: type = QStringLiteral("ellipse"); break;
            case Tool::Wire: type = QStringLiteral("wire"); break;
            default: continue;
            }
            entry.insert(QStringLiteral("type"), type);
            if (shape.type==Tool::Wire)
            {
                entry.insert(QStringLiteral("aUid"),double(shape.aSymbol));
                entry.insert(QStringLiteral("aPin"),shape.aPin);
                entry.insert(QStringLiteral("bUid"),double(shape.bSymbol));
                entry.insert(QStringLiteral("bPin"),shape.bPin);
                // Persist every page using that page's own symbols, even if inactive.
                // Otherwise a wire on another page may serialize a stale fallback.
                Shape resolved=shape;
                for (const SymbolItem &symbol:p.symbols)
                {
                    if (symbol.uid==shape.aSymbol) resolved.a=pinLocation(symbol,shape.aPin);
                    if (symbol.uid==shape.bSymbol) resolved.b=pinLocation(symbol,shape.bPin);
                }
                entry.insert(QStringLiteral("x1"), resolved.a.x());
                entry.insert(QStringLiteral("y1"), resolved.a.y());
                entry.insert(QStringLiteral("x2"), resolved.b.x());
                entry.insert(QStringLiteral("y2"), resolved.b.y());
            }
            else
            {
            entry.insert(QStringLiteral("x1"), shape.a.x());
            entry.insert(QStringLiteral("y1"), shape.a.y());
            entry.insert(QStringLiteral("x2"), shape.b.x());
            entry.insert(QStringLiteral("y2"), shape.b.y());
            }
            entry.insert(QStringLiteral("color"), shape.color.name(QColor::HexRgb));
            entry.insert(QStringLiteral("width"), shape.width);
            shapes.append(entry);
        }
        QJsonArray images;
        for (const ImageItem &item : p.images)
        {
            QJsonObject entry;
            entry.insert(QStringLiteral("x"),item.pos.x());
            entry.insert(QStringLiteral("y"),item.pos.y());
            entry.insert(QStringLiteral("w"),item.size.width());
            entry.insert(QStringLiteral("h"),item.size.height());
            entry.insert(QStringLiteral("png"),QString::fromLatin1(item.png.toBase64()));
            images.append(entry);
        }
        QJsonArray symbols;
        for (const SymbolItem &item:p.symbols)
        {
            QJsonObject entry;
            entry.insert(QStringLiteral("id"),item.id);
            entry.insert(QStringLiteral("x"),item.center.x());
            entry.insert(QStringLiteral("y"),item.center.y());
            entry.insert(QStringLiteral("scale"),item.scale);
            entry.insert(QStringLiteral("rotation"),item.rotation);
            entry.insert(QStringLiteral("color"),item.color.name(QColor::HexRgb));
            entry.insert(QStringLiteral("width"),item.lineWidth);
            entry.insert(QStringLiteral("reference"),item.reference);
            entry.insert(QStringLiteral("uid"),double(item.uid));
            symbols.append(entry);
        }
        QJsonObject pageJson;
        pageJson.insert(QStringLiteral("symbols"),symbols);
        pageJson.insert(QStringLiteral("images"),images);
        pageJson.insert(QStringLiteral("strokes"), strokes);
        pageJson.insert(QStringLiteral("texts"), texts);
        pageJson.insert(QStringLiteral("shapes"), shapes);
        pages.append(pageJson);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 6);
    root.insert(QStringLiteral("width"), PageW);
    root.insert(QStringLiteral("height"), PageH);
    root.insert(QStringLiteral("currentPage"), m_currentPage);
    root.insert(QStringLiteral("pages"), pages);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool NotesInkCanvas::loadDocument(const QByteArray &data, QString *error)
{
    if (data.trimmed().isEmpty())
    {
        m_pages = {Page{}};
        m_currentPage = 0;
        m_nextSymbolUid=1;
        m_drawing = m_erasing = m_shapeDrawing = false;
        fitPage();
        resetSelection();
        update();
        return true;
    }
    if (data.size() > 24 * 1024 * 1024)
    {
        if (error) *error = QStringLiteral("Notebook exceeds 24 MiB.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (!doc.isObject())
    {
        if (error) *error = parseError.errorString();
        return false;
    }
    const QJsonObject root = doc.object();
    const int version = root.value(QStringLiteral("version")).toInt(-1);
    if ((version < 1 || version > 6) ||
        root.value(QStringLiteral("width")).toDouble() != PageW ||
        root.value(QStringLiteral("height")).toDouble() != PageH)
    {
        if (error) *error = QStringLiteral("Unknown ink schema or page size.");
        return false;
    }
    const auto invalid = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    QJsonArray entries;
    if (version == 1)
    {
        if (!root.value(QStringLiteral("strokes")).isArray())
            return invalid(QStringLiteral("Invalid legacy stroke list."));
        QJsonObject oldPage;
        oldPage.insert(QStringLiteral("strokes"), root.value(QStringLiteral("strokes")));
        oldPage.insert(QStringLiteral("texts"), QJsonArray{});
        entries.append(oldPage);
    }
    else
    {
        if (!root.value(QStringLiteral("pages")).isArray())
            return invalid(QStringLiteral("Missing notebook pages."));
        entries = root.value(QStringLiteral("pages")).toArray();
    }
    if (entries.isEmpty() || entries.size() > MaxPages)
        return invalid(QStringLiteral("Invalid page count."));
    QVector<Page> candidate;
    candidate.reserve(entries.size());
    qint64 totalEmbeddedPixels=0; // bounded RAM even for highly compressed PNGs
    quint64 nextUid=1;
    QVector<quint64> usedUids;
    for (const QJsonValue &pageValue : entries)
    {
        if (!pageValue.isObject()) return invalid(QStringLiteral("Invalid notebook page."));
        const QJsonObject pageObject = pageValue.toObject();
        if (!pageObject.value(QStringLiteral("strokes")).isArray() ||
            !pageObject.value(QStringLiteral("texts")).isArray())
            return invalid(QStringLiteral("Invalid page contents."));
        const QJsonArray strokeEntries = pageObject.value(QStringLiteral("strokes")).toArray();
        const QJsonArray textEntries = pageObject.value(QStringLiteral("texts")).toArray();
        const QJsonValue shapeValue = pageObject.value(QStringLiteral("shapes"));
        if (version >= 3 && !shapeValue.isArray())
            return invalid(QStringLiteral("Missing shape list in notebook v3."));
        const QJsonArray shapeEntries = version >= 3 ? shapeValue.toArray() : QJsonArray{};
        const QJsonValue imageValue = pageObject.value(QStringLiteral("images"));
        if (version >= 4 && !imageValue.isArray())
            return invalid(QStringLiteral("Missing image list in notebook v4."));
        const QJsonArray imageEntries = version >= 4 ? imageValue.toArray() : QJsonArray{};
        const QJsonValue symbolValue=pageObject.value(QStringLiteral("symbols"));
        if (version>=5 && !symbolValue.isArray())
            return invalid(QStringLiteral("Missing electronic symbol list."));
        const QJsonArray symbolEntries=version>=5?symbolValue.toArray():QJsonArray{};
        if (strokeEntries.size() > 2000 || textEntries.size() > 200 || shapeEntries.size() > 500 || imageEntries.size() > 24 || symbolEntries.size() > 400)
            return invalid(QStringLiteral("Page contains too many objects."));
        Page newPage;
        newPage.strokes.reserve(strokeEntries.size());
        for (const QJsonValue &entry : strokeEntries)
        {
            if (!entry.isObject()) return invalid(QStringLiteral("Invalid stroke."));
            const QJsonObject obj = entry.toObject();
            const QColor color(obj.value(QStringLiteral("color")).toString());
            const QJsonValue width = obj.value(QStringLiteral("width"));
            if (!color.isValid() || !width.isDouble() || !std::isfinite(width.toDouble()) ||
                width.toDouble() < 1.0 || width.toDouble() > 24.0 ||
                !obj.value(QStringLiteral("points")).isArray())
                return invalid(QStringLiteral("Invalid ink properties."));
            const QJsonArray list = obj.value(QStringLiteral("points")).toArray();
            if (list.isEmpty() || list.size() > 6000)
                return invalid(QStringLiteral("Invalid stroke length."));
            Stroke stroke;
            stroke.color = color;
            stroke.width = width.toDouble();
            stroke.samples.reserve(list.size());
            for (const QJsonValue &point : list)
            {
                if (!point.isArray() || point.toArray().size() != 3)
                    return invalid(QStringLiteral("Invalid ink point."));
                const QJsonArray coords = point.toArray();
                if (!coords[0].isDouble() || !coords[1].isDouble() || !coords[2].isDouble())
                    return invalid(QStringLiteral("Invalid ink coordinates."));
                const double x = coords[0].toDouble(), y = coords[1].toDouble(), pressure = coords[2].toDouble();
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(pressure) ||
                    x < 0.0 || y < 0.0 || x > PageW || y > PageH || pressure < 0.0 || pressure > 1.0)
                    return invalid(QStringLiteral("Ink coordinate out of range."));
                stroke.samples.append({QPointF(x, y), pressure});
            }
            newPage.strokes.append(std::move(stroke));
        }
        for (const QJsonValue &entry : textEntries)
        {
            if (!entry.isObject()) return invalid(QStringLiteral("Invalid text annotation."));
            const QJsonObject obj = entry.toObject();
            if (!obj.value(QStringLiteral("x")).isDouble() ||
                !obj.value(QStringLiteral("y")).isDouble() ||
                !obj.value(QStringLiteral("text")).isString() ||
                !obj.value(QStringLiteral("size")).isDouble())
                return invalid(QStringLiteral("Invalid text annotation values."));
            const double x = obj.value(QStringLiteral("x")).toDouble();
            const double y = obj.value(QStringLiteral("y")).toDouble();
            const double size = obj.value(QStringLiteral("size")).toDouble();
            const QColor color(obj.value(QStringLiteral("color")).toString());
            const QString text = obj.value(QStringLiteral("text")).toString();
            if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0 || y < 0.0 ||
                x > PageW || y > PageH || !std::isfinite(size) || size < 12.0 || size > 96.0 ||
                std::floor(size) != size || text.isEmpty() || text.size() > 4000 || !color.isValid())
                return invalid(QStringLiteral("Text annotation out of bounds."));
            newPage.texts.append({QPointF(x, y), text, color, int(size)});
        }
        for (const QJsonValue &entry : shapeEntries)
        {
            if (!entry.isObject()) return invalid(QStringLiteral("Invalid technical shape."));
            const QJsonObject obj = entry.toObject();
            const QString typeName = obj.value(QStringLiteral("type")).toString();
            Tool type;
            if (typeName == QStringLiteral("line")) type = Tool::Line;
            else if (typeName == QStringLiteral("arrow")) type = Tool::Arrow;
            else if (typeName == QStringLiteral("rectangle")) type = Tool::Rectangle;
            else if (typeName == QStringLiteral("ellipse")) type = Tool::Ellipse;
            else if (typeName == QStringLiteral("wire") && version>=6) type=Tool::Wire;
            else return invalid(QStringLiteral("Unknown technical shape type."));
            const auto validCoordinate = [](const QJsonValue &v, double max) {
                return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= 0.0 && v.toDouble() <= max;
            };
            if (!validCoordinate(obj.value(QStringLiteral("x1")), PageW) ||
                !validCoordinate(obj.value(QStringLiteral("x2")), PageW) ||
                !validCoordinate(obj.value(QStringLiteral("y1")), PageH) ||
                !validCoordinate(obj.value(QStringLiteral("y2")), PageH))
                return invalid(QStringLiteral("Technical shape outside the paper."));
            const QJsonValue w = obj.value(QStringLiteral("width"));
            const QColor color(obj.value(QStringLiteral("color")).toString());
            if (!w.isDouble() || !std::isfinite(w.toDouble()) || w.toDouble() < 1.0 ||
                w.toDouble() > 24.0 || !color.isValid())
                return invalid(QStringLiteral("Invalid technical shape style."));
            Shape shape{type, {obj.value(QStringLiteral("x1")).toDouble(), obj.value(QStringLiteral("y1")).toDouble()},
                         {obj.value(QStringLiteral("x2")).toDouble(), obj.value(QStringLiteral("y2")).toDouble()},
                         color, w.toDouble()};
            if (type==Tool::Wire)
            {
                const auto getUid=[&](const QString &key)->quint64 {
                    const QJsonValue val=obj.value(key);
                    return val.isDouble() && std::isfinite(val.toDouble()) && val.toDouble()>=0 &&
                           val.toDouble()<1.0e9 && std::floor(val.toDouble())==val.toDouble()
                           ? quint64(val.toDouble()) : std::numeric_limits<quint64>::max();
                };
                shape.aSymbol=getUid(QStringLiteral("aUid"));
                shape.bSymbol=getUid(QStringLiteral("bUid"));
                const QJsonValue pa=obj.value(QStringLiteral("aPin"));
                const QJsonValue pb=obj.value(QStringLiteral("bPin"));
                if (!pa.isDouble() || !pb.isDouble() || !std::isfinite(pa.toDouble()) ||
                    !std::isfinite(pb.toDouble()) || std::floor(pa.toDouble())!=pa.toDouble() ||
                    std::floor(pb.toDouble())!=pb.toDouble() ||
                    shape.aSymbol==std::numeric_limits<quint64>::max() ||
                    shape.bSymbol==std::numeric_limits<quint64>::max() ||
                    pa.toDouble()<-1 || pa.toDouble()>7 || pb.toDouble()<-1 || pb.toDouble()>7)
                    return invalid(QStringLiteral("Invalid wire terminal references."));
                shape.aPin=int(pa.toDouble()); shape.bPin=int(pb.toDouble());
                if ((!shape.aSymbol && shape.aPin!=-1) || (!shape.bSymbol && shape.bPin!=-1))
                    return invalid(QStringLiteral("Free wire endpoint has a terminal index."));
            }
            if (std::hypot(shape.a.x() - shape.b.x(), shape.a.y() - shape.b.y()) < 3.0)
                return invalid(QStringLiteral("Degenerate technical shape."));
            newPage.shapes.append(shape);
        }
        for (const QJsonValue &entry : imageEntries)
        {
            if (!entry.isObject()) return invalid(QStringLiteral("Invalid page image."));
            const QJsonObject obj=entry.toObject();
            const auto validCoord=[](const QJsonValue &v,double bound) {
                return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble()>=0.0 && v.toDouble()<=bound;
            };
            if (!validCoord(obj.value(QStringLiteral("x")),PageW) ||
                !validCoord(obj.value(QStringLiteral("y")),PageH) ||
                !validCoord(obj.value(QStringLiteral("w")),PageW) ||
                !validCoord(obj.value(QStringLiteral("h")),PageH))
                return invalid(QStringLiteral("Invalid image position or dimensions."));
            const double x=obj.value(QStringLiteral("x")).toDouble();
            const double y=obj.value(QStringLiteral("y")).toDouble();
            const double w=obj.value(QStringLiteral("w")).toDouble();
            const double h=obj.value(QStringLiteral("h")).toDouble();
            if (w<1.0 || h<1.0 || x+w>PageW+1e-6 || y+h>PageH+1e-6)
                return invalid(QStringLiteral("Image lies outside the page."));
            const QJsonValue encoded=obj.value(QStringLiteral("png"));
            if (!encoded.isString() || encoded.toString().size()>2100000)
                return invalid(QStringLiteral("Page image exceeds size limits."));
            const QByteArray base64=encoded.toString().toLatin1();
            const QByteArray bytes=QByteArray::fromBase64(base64);
            if (bytes.size()>1500000 || bytes.isEmpty() || bytes.toBase64()!=base64 ||
                !bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a")))
                return invalid(QStringLiteral("Invalid embedded PNG image."));
            // Check IHDR dimensions before decoding potentially highly-compressed pixels.
            if (bytes.size()<24 || bytes.mid(12,4)!=QByteArrayLiteral("IHDR"))
                return invalid(QStringLiteral("Malformed PNG header."));
            const auto readU32=[&bytes](int offset) -> quint32 {
                return (quint32(quint8(bytes[offset]))<<24) |
                       (quint32(quint8(bytes[offset+1]))<<16) |
                       (quint32(quint8(bytes[offset+2]))<<8) | quint32(quint8(bytes[offset+3]));
            };
            const quint32 pixelW=readU32(16),pixelH=readU32(20);
            if (pixelW<1 || pixelH<1 || pixelW>1400 || pixelH>900 ||
                totalEmbeddedPixels+qint64(pixelW)*pixelH>40000000LL)
                return invalid(QStringLiteral("Embedded image exceeds safe pixel budget."));
            totalEmbeddedPixels+=qint64(pixelW)*pixelH;
            const QImage pixels=QImage::fromData(bytes,"PNG");
            if (pixels.isNull() || pixels.width()!=int(pixelW) || pixels.height()!=int(pixelH))
                return invalid(QStringLiteral("Embedded PNG cannot be decoded safely."));
            newPage.images.append({QPointF(x,y),QSizeF(w,h),bytes,pixels});
        }
        for (const QJsonValue &entry:symbolEntries)
        {
            if (!entry.isObject()) return invalid(QStringLiteral("Invalid electronic symbol."));
            const QJsonObject obj=entry.toObject();
            const QString id=obj.value(QStringLiteral("id")).toString();
            const QColor color(obj.value(QStringLiteral("color")).toString());
            const QJsonValue xv=obj.value(QStringLiteral("x"));
            const QJsonValue yv=obj.value(QStringLiteral("y"));
            const QJsonValue sv=obj.value(QStringLiteral("scale"));
            const QJsonValue rv=obj.value(QStringLiteral("rotation"));
            const QJsonValue wv=obj.value(QStringLiteral("width"));
            const QJsonValue ref=obj.value(QStringLiteral("reference"));
            if (!knownSymbol(id) || !color.isValid() || !xv.isDouble() || !yv.isDouble() ||
                !sv.isDouble() || !rv.isDouble() || !wv.isDouble() || !ref.isString())
                return invalid(QStringLiteral("Invalid electronic symbol properties."));
            const double x=xv.toDouble(),y=yv.toDouble(),scale=sv.toDouble();
            const double rotation=rv.toDouble(),penWidth=wv.toDouble();
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(scale) ||
                !std::isfinite(rotation) || !std::isfinite(penWidth) || scale<0.65 || scale>2.0 ||
                rotation<0.0 || rotation>270.0 || std::fmod(rotation,90.0)!=0.0 ||
                penWidth<1.0 || penWidth>24.0 || ref.toString().size()>36 ||
                ref.toString().contains(QLatin1Char('\n')) ||
                x<90.0*scale || x>PageW-90.0*scale ||
                y<90.0*scale || y>PageH-90.0*scale)
                return invalid(QStringLiteral("Electronic symbol geometry outside paper."));
            SymbolItem item{id,QPointF(x,y),scale,int(rotation),color,penWidth,ref.toString()};
            if (version>=6)
            {
                const QJsonValue rawUid=obj.value(QStringLiteral("uid"));
                if (!rawUid.isDouble() || !std::isfinite(rawUid.toDouble()) ||
                    rawUid.toDouble()<1.0 || rawUid.toDouble()>=1.0e9 ||
                    std::floor(rawUid.toDouble())!=rawUid.toDouble())
                    return invalid(QStringLiteral("Invalid persistent symbol ID."));
                item.uid=quint64(rawUid.toDouble());
                if (usedUids.contains(item.uid)) return invalid(QStringLiteral("Duplicate symbol ID."));
                nextUid=std::max(nextUid,item.uid+1);
            }
            else item.uid=nextUid++;
            usedUids.append(item.uid);
            newPage.symbols.append(item);
        }
        for (const Shape &wire:newPage.shapes)
        {
            if (wire.type!=Tool::Wire) continue;
            const auto pinValid=[&](quint64 uid, int pin)->bool {
                if (!uid) return pin==-1;
                for (const SymbolItem &symbol:newPage.symbols)
                    if (symbol.uid==uid) return pin>=0 && pin<localPins(symbol.id).size();
                return false;
            };
            if (!pinValid(wire.aSymbol,wire.aPin) || !pinValid(wire.bSymbol,wire.bPin))
                return invalid(QStringLiteral("Wire refers to a missing or invalid component terminal."));
        }
        candidate.append(std::move(newPage));
    }
    const int active = version == 1 ? 0 : root.value(QStringLiteral("currentPage")).toInt(-1);
    if (active < 0 || active >= candidate.size())
        return invalid(QStringLiteral("Invalid active page index."));
    m_pages = std::move(candidate); // only replace after every page is validated
    m_nextSymbolUid=nextUid;
    m_currentPage = active;
    m_drawing = m_erasing = m_tabletStroke = m_tabletEraser = false;
    m_shapeDrawing = false;
    fitPage();
    resetSelection();
    update();
    return true;
}

QImage NotesInkCanvas::renderToImage(int width, int height) const
{
    width = std::clamp(width, 32, 4096);
    height = std::clamp(height, 32, 4096);
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.scale(double(width) / PageW, double(height) / PageH);
    drawContent(painter, true);
    return image;
}

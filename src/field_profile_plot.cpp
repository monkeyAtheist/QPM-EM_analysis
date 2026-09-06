#include "widgets/field_profile_plot.h"

#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QFontMetrics>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
QColor seriesColor(const QPalette &pal, int index)
{
    QColor base = pal.color(QPalette::Highlight);
    if (base.lightness() < 85)
        base = base.lighter(190);
    int h = base.hsvHue();
    if (h < 0) h = 205;
    QColor c;
    c.setHsv((h + index * 67) % 360,
             std::clamp(base.hsvSaturation() + 20, 70, 230),
             std::clamp(base.value(), 150, 245));
    return c;
}

struct Range
{
    double min = 0.0;
    double max = 1.0;
    bool valid = false;
};

void addValue(Range &r, double v)
{
    if (!std::isfinite(v)) return;
    if (!r.valid) { r.min = r.max = v; r.valid = true; }
    else { r.min = std::min(r.min, v); r.max = std::max(r.max, v); }
}

void padRange(Range &r)
{
    if (!r.valid) { r.min = 0.0; r.max = 1.0; return; }
    if (std::abs(r.max - r.min) < 1e-15)
    {
        const double span = std::max(1.0, std::abs(r.min) * 0.2);
        r.min -= span; r.max += span;
    }
    else
    {
        const double pad = 0.08 * (r.max-r.min);
        r.min -= pad; r.max += pad;
    }
}
}

FieldProfilePlot::FieldProfilePlot(QWidget *parent)
    : QWidget(parent)
{
    // Keep plots readable without forcing parent workspaces beyond common laptop
    // heights. Pages that need more room can still request a larger minimum locally.
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

void FieldProfilePlot::setData(const QVector<double> &distanceMeters,
                               const QVector<double> &values,
                               const QString &title,
                               const QString &valueUnit)
{
    FieldProfileSeries s;
    s.distanceMeters = distanceMeters;
    s.values = values;
    s.name = title;
    s.unit = valueUnit;
    setSeries({s}, title);
}

void FieldProfilePlot::setSeries(const QVector<FieldProfileSeries> &series,
                                 const QString &title)
{
    m_series = series;
    m_title = title;
    update();
}

void FieldProfilePlot::clearData()
{
    m_series.clear();
    update();
}

void FieldProfilePlot::setXAxis(const QString &label, const QString &unit)
{
    m_xAxisLabel = label;
    m_xAxisUnit = unit;
    update();
}

void FieldProfilePlot::setXAxisLogarithmic(bool enabled)
{
    m_xAxisLogarithmic = enabled;
    update();
}

void FieldProfilePlot::setMarkers(const QVector<FieldPlotMarker> &markers)
{
    m_markers = markers;
    update();
}

void FieldProfilePlot::clearMarkers()
{
    m_markers.clear();
    update();
}

QSize FieldProfilePlot::minimumSizeHint() const
{
    return {640, 340};
}

QString FieldProfilePlot::formatEngineering(double value, const QString &unit)
{
    if (std::isinf(value))
        return value > 0.0 ? QStringLiteral("∞ %1").arg(unit) : QStringLiteral("−∞ %1").arg(unit);
    if (!std::isfinite(value))
        return QStringLiteral("n/a");

    // Logarithmic level units are already scaled quantities. Applying SI prefixes to
    // dB (for example displaying -0.235 dB as -235 mdB) is mathematically legal but
    // highly unconventional and makes RF sweep plots needlessly difficult to read.
    if (unit.startsWith(QStringLiteral("dB"), Qt::CaseSensitive))
        return QStringLiteral("%1 %2").arg(value, 0, 'g', 6).arg(unit);

    if (value == 0.0)
        return QStringLiteral("0 %1").arg(unit);

    struct Prefix { int exponent; const char *symbol; };
    static const Prefix prefixes[] = {
        {-12, "p"}, {-9, "n"}, {-6, "u"}, {-3, "m"},
        {0, ""}, {3, "k"}, {6, "M"}, {9, "G"}, {12, "T"}
    };
    int exponent = int(std::floor(std::log10(std::abs(value)) / 3.0)) * 3;
    exponent = std::clamp(exponent, -12, 12);
    const double scaled = value / std::pow(10.0, exponent);
    const char *symbol = "";
    for (const auto &prefix : prefixes)
        if (prefix.exponent == exponent) { symbol = prefix.symbol; break; }
    return QStringLiteral("%1 %2%3")
        .arg(scaled, 0, 'g', 5)
        .arg(QString::fromLatin1(symbol), unit);
}

void FieldProfilePlot::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPalette pal = palette();
    const QColor background = pal.color(QPalette::Base);
    const QColor text = pal.color(QPalette::Text);
    const QColor grid = pal.color(QPalette::Mid);
    painter.fillRect(rect(), background);

    QStringList units;
    for (const auto &s : m_series)
        if (!s.unit.isEmpty() && !units.contains(s.unit))
            units.push_back(s.unit);
    if (units.size() > 2)
        units = units.mid(0, 2);

    const bool secondAxis = units.size() > 1;
    const double rightMargin = secondAxis ? 88.0 : 28.0;
    const QRectF plotRect(82.0, 72.0,
                          std::max(70.0, width() - 82.0 - rightMargin),
                          std::max(70.0, height() - 126.0));

    painter.setPen(text);
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(QRectF(12, 7, width() - 24, 24), Qt::AlignCenter,
                     m_title.isEmpty() ? QStringLiteral("Field profile") : m_title);
    painter.setFont(font());

    Range xRange;
    Range yRange[2];
    int finiteCount = 0;
    for (const auto &s : m_series)
    {
        const int axis = units.size() > 1 && s.unit == units[1] ? 1 : 0;
        const int count = std::min(s.distanceMeters.size(), s.values.size());
        for (int i = 0; i < count; ++i)
        {
            if (std::isfinite(s.distanceMeters[i]) && std::isfinite(s.values[i]) && (!m_xAxisLogarithmic || s.distanceMeters[i] > 0.0))
            {
                addValue(xRange, m_xAxisLogarithmic ? std::log10(s.distanceMeters[i]) : s.distanceMeters[i]);
                addValue(yRange[axis], s.values[i]);
                ++finiteCount;
            }
        }
    }

    if (finiteCount < 2)
    {
        painter.setPen(pal.color(QPalette::PlaceholderText));
        painter.drawText(plotRect, Qt::AlignCenter,
                         QStringLiteral("No finite profile data to display."));
        return;
    }
    padRange(xRange);
    padRange(yRange[0]);
    if (secondAxis) padRange(yRange[1]);

    auto mapX = [&](double x) {
        const double tx = m_xAxisLogarithmic ? std::log10(std::max(x, 1e-300)) : x;
        return plotRect.left() + (tx-xRange.min)/(xRange.max-xRange.min)*plotRect.width();
    };
    auto mapY = [&](double y, int axis) {
        const auto &r = yRange[axis];
        return plotRect.bottom() - (y-r.min)/(r.max-r.min)*plotRect.height();
    };

    painter.setPen(QPen(grid, 1.0, Qt::DotLine));
    constexpr int divisions = 6;
    for (int i = 0; i <= divisions; ++i)
    {
        const double t = double(i)/divisions;
        const double x = plotRect.left() + t*plotRect.width();
        const double y = plotRect.top() + t*plotRect.height();
        painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
        painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
    }

    if (yRange[0].min < 0.0 && yRange[0].max > 0.0)
    {
        painter.setPen(QPen(text, 1.2));
        painter.drawLine(QPointF(plotRect.left(), mapY(0.0, 0)), QPointF(plotRect.right(), mapY(0.0, 0)));
    }

    painter.setPen(QPen(text, 1.2));
    painter.drawRect(plotRect);
    painter.setPen(text);
    for (int i = 0; i <= divisions; ++i)
    {
        const double t = double(i)/divisions;
        const double xCoord = xRange.min + t*(xRange.max-xRange.min);
        const double xValue = m_xAxisLogarithmic ? std::pow(10.0, xCoord) : xCoord;
        const double y0 = yRange[0].max - t*(yRange[0].max-yRange[0].min);
        painter.drawText(QRectF(plotRect.left()+t*plotRect.width()-46,
                                plotRect.bottom()+8, 92, 20),
                         Qt::AlignHCenter|Qt::AlignTop,
                         formatEngineering(xValue, m_xAxisUnit));
        painter.drawText(QRectF(4, plotRect.top()+t*plotRect.height()-10, 72, 20),
                         Qt::AlignRight|Qt::AlignVCenter,
                         formatEngineering(y0, units.value(0)));
        if (secondAxis)
        {
            const double y1 = yRange[1].max - t*(yRange[1].max-yRange[1].min);
            painter.drawText(QRectF(plotRect.right()+8, plotRect.top()+t*plotRect.height()-10, 78, 20),
                             Qt::AlignLeft|Qt::AlignVCenter,
                             formatEngineering(y1, units[1]));
        }
    }

    // Compact legend above the plot.
    double legendX = plotRect.left();
    const double legendY = 40.0;
    const QFontMetrics fm(painter.font());
    for (int si = 0; si < m_series.size(); ++si)
    {
        const auto &s = m_series[si];
        const QColor c = seriesColor(pal, si);
        QPen pen(c, s.dashed ? 1.6 : 2.4);
        if (s.dashed) pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.drawLine(QPointF(legendX, legendY+6), QPointF(legendX+22, legendY+6));
        painter.setPen(text);
        const QString label = QStringLiteral("%1 [%2]").arg(s.name, s.unit);
        painter.drawText(QPointF(legendX+28, legendY+11), label);
        legendX += 34 + fm.horizontalAdvance(label);
        if (legendX > plotRect.right()-120)
            break;
    }

    for (int si = 0; si < m_series.size(); ++si)
    {
        const auto &s = m_series[si];
        if (!units.contains(s.unit) && !s.unit.isEmpty())
            continue;
        const int axis = secondAxis && s.unit == units[1] ? 1 : 0;
        QPainterPath path;
        bool started = false;
        const int count = std::min(s.distanceMeters.size(), s.values.size());
        for (int i = 0; i < count; ++i)
        {
            if (!std::isfinite(s.distanceMeters[i]) || !std::isfinite(s.values[i]) || (m_xAxisLogarithmic && s.distanceMeters[i] <= 0.0))
            {
                started = false;
                continue;
            }
            const QPointF p(mapX(s.distanceMeters[i]), mapY(s.values[i], axis));
            if (!started) { path.moveTo(p); started = true; }
            else path.lineTo(p);
        }
        QPen pen(seriesColor(pal, si), s.dashed ? 1.6 : 2.3);
        if (s.dashed) pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.drawPath(path);
    }

    // VNA/profile markers are drawn after curves so they remain visible.
    for (int mi = 0; mi < m_markers.size(); ++mi)
    {
        const auto &marker = m_markers[mi];
        const double markerCoord = m_xAxisLogarithmic ? (marker.xValue > 0.0 ? std::log10(marker.xValue) : std::numeric_limits<double>::quiet_NaN()) : marker.xValue;
        if (!marker.enabled || !std::isfinite(markerCoord) ||
            markerCoord < xRange.min || markerCoord > xRange.max)
            continue;
        const double x = mapX(marker.xValue);
        QColor mc = seriesColor(pal, 8 + mi);
        QPen mp(mc, marker.active ? 2.2 : 1.4, marker.active ? Qt::SolidLine : Qt::DashLine);
        painter.setPen(mp);
        painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
        painter.setBrush(mc);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(x, plotRect.top()+7.0), marker.active ? 4.5 : 3.2, marker.active ? 4.5 : 3.2);
        painter.setPen(mc);
        const QString label = QStringLiteral("%1  %2").arg(marker.name, formatEngineering(marker.xValue, m_xAxisUnit));
        const double labelY = plotRect.top() + 10.0 + (mi % 3) * 18.0;
        const QRectF lr(std::clamp(x - 64.0, plotRect.left(), std::max(plotRect.left(), plotRect.right()-128.0)), labelY, 128.0, 18.0);
        painter.drawText(lr, Qt::AlignHCenter|Qt::AlignVCenter, label);
    }

    if (m_hoverActive && plotRect.contains(m_hoverPosition))
    {
        double hoverX = 0.0;
        if (xValueFromPosition(m_hoverPosition, hoverX))
        {
            const double hx = mapX(hoverX);
            QColor hoverColor = pal.color(QPalette::Highlight);
            hoverColor.setAlpha(180);
            painter.setPen(QPen(hoverColor, 1.2, Qt::DashLine));
            painter.drawLine(QPointF(hx, plotRect.top()), QPointF(hx, plotRect.bottom()));

            QStringList lines;
            lines << QStringLiteral("%1: %2").arg(m_xAxisLabel, formatEngineering(hoverX, m_xAxisUnit));
            for (int si = 0; si < m_series.size(); ++si)
            {
                const auto &s = m_series[si];
                const int count = std::min(s.distanceMeters.size(), s.values.size());
                int best = -1;
                double bestDx = std::numeric_limits<double>::infinity();
                for (int i = 0; i < count; ++i)
                {
                    const double sx = s.distanceMeters[i];
                    const double sy = s.values[i];
                    if (!std::isfinite(sx) || !std::isfinite(sy) || (m_xAxisLogarithmic && sx <= 0.0))
                        continue;
                    const double dx = std::abs((m_xAxisLogarithmic ? std::log10(sx) : sx) - (m_xAxisLogarithmic ? std::log10(std::max(hoverX, 1e-300)) : hoverX));
                    if (dx < bestDx) { bestDx = dx; best = i; }
                }
                if (best < 0) continue;
                const double sx = s.distanceMeters[best];
                const double sy = s.values[best];
                const double hoverValue = s.hoverValues.size() == s.values.size() ? s.hoverValues[best] : sy;
                const int axis = secondAxis && s.unit == units.value(1) ? 1 : 0;
                const QPointF hp(mapX(sx), mapY(sy, axis));
                QColor c = seriesColor(pal, si);
                painter.setBrush(c);
                painter.setPen(QPen(pal.color(QPalette::Base), 1.0));
                painter.drawEllipse(hp, 3.8, 3.8);
                lines << QStringLiteral("%1: %2").arg(s.name, formatEngineering(hoverValue, s.unit));
            }

            const QFontMetrics hoverFm(painter.font());
            int boxW = 0;
            for (const QString &line : lines)
                boxW = std::max(boxW, hoverFm.horizontalAdvance(line));
            const double lineH = hoverFm.height() + 2.0;
            const double boxH = 10.0 + lineH * lines.size();
            const double boxWidth = std::min(plotRect.width() - 8.0, double(boxW) + 18.0);
            double boxX = hx + 12.0;
            if (boxX + boxWidth > plotRect.right())
                boxX = hx - 12.0 - boxWidth;
            boxX = std::clamp(boxX, plotRect.left() + 4.0, std::max(plotRect.left() + 4.0, plotRect.right() - boxWidth - 4.0));
            const double boxY = std::clamp(m_hoverPosition.y() - boxH - 8.0, plotRect.top() + 4.0, std::max(plotRect.top() + 4.0, plotRect.bottom() - boxH - 4.0));
            QRectF box(boxX, boxY, boxWidth, boxH);
            QColor boxFill = pal.color(QPalette::Base);
            boxFill.setAlpha(232);
            painter.setBrush(boxFill);
            painter.setPen(QPen(pal.color(QPalette::Mid), 1.0));
            painter.drawRoundedRect(box, 6.0, 6.0);
            painter.setPen(text);
            for (int i = 0; i < lines.size(); ++i)
                painter.drawText(QRectF(box.left() + 8.0, box.top() + 6.0 + i * lineH, box.width() - 12.0, lineH),
                                 Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
        }
    }

    painter.setPen(text);
    painter.drawText(QRectF(plotRect.left(), height()-28, plotRect.width(), 20),
                     Qt::AlignCenter, m_xAxisLabel);
}


int FieldProfilePlot::markerIndexNearPosition(const QPointF &position) const
{
    QStringList units;
    for (const auto &series : m_series)
        if (!series.unit.isEmpty() && !units.contains(series.unit))
            units.push_back(series.unit);
    if (units.size() > 2) units = units.mid(0, 2);
    const bool secondAxis = units.size() > 1;
    const double rightMargin = secondAxis ? 88.0 : 28.0;
    const QRectF plotRect(82.0, 72.0,
                          std::max(70.0, width() - 82.0 - rightMargin),
                          std::max(70.0, height() - 126.0));
    if (!plotRect.contains(position)) return -1;

    Range xRange;
    for (const auto &series : m_series)
    {
        const int count = std::min(series.distanceMeters.size(), series.values.size());
        for (int i = 0; i < count; ++i)
            if (std::isfinite(series.distanceMeters[i]) && std::isfinite(series.values[i]) && (!m_xAxisLogarithmic || series.distanceMeters[i] > 0.0))
                addValue(xRange, m_xAxisLogarithmic ? std::log10(series.distanceMeters[i]) : series.distanceMeters[i]);
    }
    if (!xRange.valid) return -1;
    padRange(xRange);

    int bestIndex = -1;
    double bestPixels = 14.0;
    for (int i = 0; i < m_markers.size(); ++i)
    {
        const auto &marker = m_markers[i];
        const double markerCoord = m_xAxisLogarithmic ? (marker.xValue > 0.0 ? std::log10(marker.xValue) : std::numeric_limits<double>::quiet_NaN()) : marker.xValue;
        if (!marker.enabled || !marker.draggable || !std::isfinite(markerCoord) ||
            markerCoord < xRange.min || markerCoord > xRange.max)
            continue;
        const double markerX = plotRect.left() + (markerCoord - xRange.min) / (xRange.max - xRange.min) * plotRect.width();
        const double distance = std::abs(position.x() - markerX);
        if (distance <= bestPixels)
        {
            bestPixels = distance;
            bestIndex = i;
        }
    }
    return bestIndex;
}

bool FieldProfilePlot::xValueFromPosition(const QPointF &position, double &value) const
{
    QStringList units;
    for (const auto &s : m_series)
        if (!s.unit.isEmpty() && !units.contains(s.unit))
            units.push_back(s.unit);
    if (units.size() > 2) units = units.mid(0, 2);
    const bool secondAxis = units.size() > 1;
    const double rightMargin = secondAxis ? 88.0 : 28.0;
    const QRectF plotRect(82.0, 72.0,
                          std::max(70.0, width() - 82.0 - rightMargin),
                          std::max(70.0, height() - 126.0));
    if (!plotRect.contains(position)) return false;

    Range xRange;
    for (const auto &s : m_series)
    {
        const int count = std::min(s.distanceMeters.size(), s.values.size());
        for (int i = 0; i < count; ++i)
            if (std::isfinite(s.distanceMeters[i]) && std::isfinite(s.values[i]) && (!m_xAxisLogarithmic || s.distanceMeters[i] > 0.0))
                addValue(xRange, m_xAxisLogarithmic ? std::log10(s.distanceMeters[i]) : s.distanceMeters[i]);
    }
    if (!xRange.valid) return false;
    padRange(xRange);
    const double t = std::clamp((position.x()-plotRect.left())/plotRect.width(), 0.0, 1.0);
    const double coord = xRange.min + t*(xRange.max-xRange.min);
    value = m_xAxisLogarithmic ? std::pow(10.0, coord) : coord;
    return std::isfinite(value);
}

void FieldProfilePlot::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && (markerPositionRequested || markerPositionRequestedIndexed))
    {
        double x = 0.0;
        if (xValueFromPosition(event->position(), x))
        {
            if (markerPositionRequestedIndexed)
            {
                m_draggedMarker = markerIndexNearPosition(event->position());
                if (m_draggedMarker < 0)
                {
                    QWidget::mousePressEvent(event);
                    return;
                }
                markerPositionRequestedIndexed(m_draggedMarker, x);
            }
            else
            {
                m_draggedMarker = -1;
                markerPositionRequested(x);
            }
            m_markerDragging = true;
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void FieldProfilePlot::mouseMoveEvent(QMouseEvent *event)
{
    if (m_markerDragging && (event->buttons() & Qt::LeftButton))
    {
        double x = 0.0;
        if (xValueFromPosition(event->position(), x))
        {
            if (markerPositionRequestedIndexed && m_draggedMarker >= 0)
                markerPositionRequestedIndexed(m_draggedMarker, x);
            else if (markerPositionRequested)
                markerPositionRequested(x);
            m_hoverActive = true;
            m_hoverPosition = event->position();
            update();
        }
        event->accept();
        return;
    }

    double x = 0.0;
    const bool hover = xValueFromPosition(event->position(), x);
    if (hover != m_hoverActive || (hover && (event->position() - m_hoverPosition).manhattanLength() >= 1.0))
    {
        m_hoverActive = hover;
        m_hoverPosition = event->position();
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void FieldProfilePlot::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_markerDragging)
    {
        m_markerDragging = false;
        m_draggedMarker = -1;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void FieldProfilePlot::leaveEvent(QEvent *event)
{
    if (m_hoverActive)
    {
        m_hoverActive = false;
        update();
    }
    QWidget::leaveEvent(event);
}

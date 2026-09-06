#pragma once

#include "field_view_plane.h"
#include "magnetostatic_model.h"

#include <QImage>
#include <QLineF>
#include <QPainterPath>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QPainter;

class MagnetostaticCanvas final : public QWidget
{
    Q_OBJECT

public:
    enum class ScalarMap
    {
        Off,
        MagneticFluxDensityMagnitude
    };

    explicit MagnetostaticCanvas(QWidget *parent = nullptr);

    void setModel(MagnetostaticModel *model);
    MagnetostaticModel *model() const noexcept { return m_model; }

    int selectedSource() const noexcept { return m_selectedSource; }
    void setSelectedSource(int index);

    MagnetostaticVec3 measurementPoint() const noexcept { return m_measurementPoint; }
    void setMeasurementPoint(const MagnetostaticVec3 &point);

    FieldViewPlane viewPlane() const noexcept { return m_viewPlane; }
    void setViewPlane(FieldViewPlane plane);

    void centerView();
    void setShowFieldVectors(bool enabled);

    ScalarMap scalarMap() const noexcept { return m_scalarMap; }
    void setScalarMap(ScalarMap mode);
    void setShowContours(bool enabled);
    void setShowFieldLines(bool enabled);

    void setProbeSegment(const MagnetostaticVec3 &pointA, const MagnetostaticVec3 &pointB, bool enabled = true);
    void setProbeSegmentVisible(bool enabled);
    bool probeSegmentVisible() const noexcept { return m_probeVisible; }
    void setPointProbes(const QVector<MagnetostaticVec3> &positions, const QStringList &names, int selectedIndex = -1);
    void setPointProbesVisible(bool enabled);
    bool pointProbesVisible() const noexcept { return m_pointProbesVisible; }
    int selectedPointProbe() const noexcept { return m_selectedPointProbe; }
    MagnetostaticVec3 probePointA() const noexcept { return m_probeA; }
    MagnetostaticVec3 probePointB() const noexcept { return m_probeB; }

signals:
    void selectedSourceChanged(int index);
    void measurementPointChanged(double x, double y, double z);
    void sourcePositionEdited(int index, double x, double y, double z);
    void probeSegmentEdited(double ax, double ay, double az, double bx, double by, double bz);
    void probeSegmentEditFinished();
    void pointProbeSelected(int index);
    void pointProbePositionEdited(int index, double x, double y, double z);
    void pointProbeEditFinished(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QPointF projectPoint(const MagnetostaticVec3 &point) const;
    QPointF projectVector(const MagnetostaticVec3 &vector) const;
    MagnetostaticVec3 unprojectPoint(const QPointF &planePoint, const MagnetostaticVec3 &base) const;
    MagnetostaticVec3 samplePointForPlane(const QPointF &planePoint) const;
    QPointF worldToScreen(const QPointF &point) const;
    QPointF screenToWorld(const QPointF &point) const;
    QString horizontalAxisName() const;
    QString verticalAxisName() const;

    int hitTestSource(const QPointF &screenPoint) const;
    void drawGrid(QPainter &painter);
    void drawFieldVectors(QPainter &painter);
    void drawSources(QPainter &painter);
    void drawMeasurement(QPainter &painter);
    void drawProbeSegment(QPainter &painter);
    void drawPointProbes(QPainter &painter);
    int hitTestProbeEndpoint(const QPointF &screenPoint) const;
    int hitTestPointProbe(const QPointF &screenPoint) const;
    void drawArrow(QPainter &painter, const QPointF &start, const QPointF &end) const;
    void drawProjectedLoop(QPainter &painter, const MagnetostaticSource &source, const QColor &color, double width);

    void invalidateVisualization();
    QRectF visualizationRect() const;
    void rebuildScalarCache();
    void rebuildFieldLineCache();
    void drawScalarHeatmap(QPainter &painter);
    void drawContours(QPainter &painter);
    void drawFieldLines(QPainter &painter);
    void drawScalarLegend(QPainter &painter);
    double scalarValueAt(const MagnetostaticVec3 &point, bool *valid) const;
    static QColor sequentialColor(double t);
    static QString formatEngineering(double value, const QString &unit);

    MagnetostaticModel *m_model = nullptr;
    int m_selectedSource = -1;
    MagnetostaticVec3 m_measurementPoint{};
    FieldViewPlane m_viewPlane = FieldViewPlane::XY;

    QPointF m_viewCenterWorld{0.0, 0.0};
    double m_pixelsPerMeter = 90.0;
    bool m_showFieldVectors = true;
    ScalarMap m_scalarMap = ScalarMap::Off;
    bool m_showContours = false;
    bool m_showFieldLines = false;

    bool m_probeVisible = false;
    MagnetostaticVec3 m_probeA{-1.0, 0.0, 0.0};
    MagnetostaticVec3 m_probeB{1.0, 0.0, 0.0};
    QVector<MagnetostaticVec3> m_pointProbes;
    QStringList m_pointProbeNames;
    bool m_pointProbesVisible = true;
    int m_selectedPointProbe = -1;

    bool m_visualizationDirty = true;
    QSize m_cacheSize;
    QImage m_scalarImage;
    QVector<QLineF> m_contourLines;
    QVector<QPainterPath> m_fieldLinePaths;
    double m_scalarDisplayMin = 0.0;
    double m_scalarDisplayMax = 1.0;
    bool m_scalarHasData = false;

    enum class DragTarget { None, Source, ProbeA, ProbeB, PointProbe };
    DragTarget m_dragTarget = DragTarget::None;
    int m_draggedPointProbe = -1;
    bool m_panning = false;
    QPoint m_lastMousePos;
};

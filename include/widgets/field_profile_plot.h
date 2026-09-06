#pragma once

#include <QVector>
#include <QWidget>
#include <QString>
#include <QMouseEvent>

#include <functional>

struct FieldProfileSeries
{
    QVector<double> distanceMeters;
    QVector<double> values;
    QString name;
    QString unit;
    bool dashed = false;
    // Optional un-clipped values used by the interactive hover readout.  When empty,
    // `values` is used for both rendering and inspection.
    QVector<double> hoverValues;
};

struct FieldPlotMarker
{
    QString name;
    double xValue = 0.0;
    bool enabled = true;
    bool active = false;
    bool draggable = true;
};

class FieldProfilePlot final : public QWidget
{
public:
    explicit FieldProfilePlot(QWidget *parent = nullptr);

    void setData(const QVector<double> &distanceMeters,
                 const QVector<double> &values,
                 const QString &title,
                 const QString &valueUnit);
    void setSeries(const QVector<FieldProfileSeries> &series,
                   const QString &title = QString());
    void clearData();
    void setXAxis(const QString &label, const QString &unit = QStringLiteral("m"));
    void setXAxisLogarithmic(bool enabled);
    bool xAxisLogarithmic() const { return m_xAxisLogarithmic; }
    void setMarkers(const QVector<FieldPlotMarker> &markers);
    void clearMarkers();

    // Called while the user clicks/drags inside the plot. The owner decides
    // which marker is active and snaps the requested X coordinate to data.
    std::function<void(double)> markerPositionRequested;
    // Optional indexed callback for pages with several independently draggable cursors.
    // When set, clicking near a draggable marker locks that marker for the drag.
    std::function<void(int, double)> markerPositionRequestedIndexed;

    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    static QString formatEngineering(double value, const QString &unit);
    bool xValueFromPosition(const QPointF &position, double &value) const;
    int markerIndexNearPosition(const QPointF &position) const;

    QVector<FieldProfileSeries> m_series;
    QVector<FieldPlotMarker> m_markers;
    QString m_title;
    QString m_xAxisLabel = QStringLiteral("Distance along A → B");
    QString m_xAxisUnit = QStringLiteral("m");
    bool m_xAxisLogarithmic = false;
    bool m_markerDragging = false;
    int m_draggedMarker = -1;
    bool m_hoverActive = false;
    QPointF m_hoverPosition;
};

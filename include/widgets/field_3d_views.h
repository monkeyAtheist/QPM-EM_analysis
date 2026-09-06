#pragma once

#include "electrostatic_model.h"
#include "magnetostatic_model.h"

#include <QPoint>
#include <QPointF>
#include <QWidget>

#include <functional>

class QContextMenuEvent;
class QKeyEvent;

class ElectrostaticField3DView final : public QWidget
{
public:
    explicit ElectrostaticField3DView(QWidget *parent = nullptr);

    void setModel(const ElectrostaticModel *model);
    void setSelectedSource(int index);
    void setMeasurementPoint(const ElectrostaticVec3 &point);
    void setShowFieldVectors(bool show);
    void setEditPlane(int plane);
    void centerView();

    std::function<void(int)> sourceSelected;
    std::function<void(int, const ElectrostaticVec3 &)> sourceMoveRequested;
    std::function<void(int, const ElectrostaticVec3 &)> sourceCreateRequested;
    std::function<void(int)> sourceDeleteRequested;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct Vec3 { double x = 0.0; double y = 0.0; double z = 0.0; };
    QPointF project(const Vec3 &p) const;
    Vec3 rotate(const Vec3 &p) const;
    double sceneRadius() const;
    int hitSource(const QPointF &screen) const;
    bool screenToSourcePlane(const QPointF &screen, const ElectrostaticVec3 &origin, ElectrostaticVec3 &out) const;
    bool screenToCreationPlane(const QPointF &screen, ElectrostaticVec3 &out) const;

    const ElectrostaticModel *m_model = nullptr;
    int m_selectedSource = -1;
    ElectrostaticVec3 m_measurementPoint{};
    bool m_showFieldVectors = true;
    double m_yawDeg = -38.0;
    double m_pitchDeg = 27.0;
    double m_zoom = 1.0;
    QPointF m_panPx{};
    QPoint m_lastMouse;
    bool m_rotating = false;
    bool m_panning = false;
    bool m_draggingSource = false;
    int m_editPlane = 0;
    int m_dragSourceIndex = -1;
    ElectrostaticVec3 m_dragSourceStart{};
    ElectrostaticVec3 m_dragSourcePreview{};
    QPoint m_rightPressPos{};
    bool m_rightDragged = false;
};

class MagnetostaticField3DView final : public QWidget
{
public:
    explicit MagnetostaticField3DView(QWidget *parent = nullptr);

    void setModel(const MagnetostaticModel *model);
    void setSelectedSource(int index);
    void setMeasurementPoint(const MagnetostaticVec3 &point);
    void setShowFieldVectors(bool show);
    void setEditPlane(int plane);
    void centerView();

    std::function<void(int)> sourceSelected;
    std::function<void(int, const MagnetostaticVec3 &)> sourceMoveRequested;
    std::function<void(int, const MagnetostaticVec3 &)> sourceCreateRequested;
    std::function<void(int)> sourceDeleteRequested;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct Vec3 { double x = 0.0; double y = 0.0; double z = 0.0; };
    QPointF project(const Vec3 &p) const;
    Vec3 rotate(const Vec3 &p) const;
    double sceneRadius() const;
    int hitSource(const QPointF &screen) const;
    bool screenToSourcePlane(const QPointF &screen, const MagnetostaticVec3 &origin, MagnetostaticVec3 &out) const;
    bool screenToCreationPlane(const QPointF &screen, MagnetostaticVec3 &out) const;

    const MagnetostaticModel *m_model = nullptr;
    int m_selectedSource = -1;
    MagnetostaticVec3 m_measurementPoint{};
    bool m_showFieldVectors = true;
    double m_yawDeg = -38.0;
    double m_pitchDeg = 27.0;
    double m_zoom = 1.0;
    QPointF m_panPx{};
    QPoint m_lastMouse;
    bool m_rotating = false;
    bool m_panning = false;
    bool m_draggingSource = false;
    int m_editPlane = 0;
    int m_dragSourceIndex = -1;
    MagnetostaticVec3 m_dragSourceStart{};
    MagnetostaticVec3 m_dragSourcePreview{};
    QPoint m_rightPressPos{};
    bool m_rightDragged = false;
};

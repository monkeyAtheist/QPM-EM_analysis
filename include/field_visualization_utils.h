#pragma once

#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QVector>

#include <algorithm>
#include <cmath>

struct FieldVisualizationGrid
{
    int columns = 0;
    int rows = 0;
    QRectF screenRect;
    QVector<double> values;
    QVector<unsigned char> valid;

    int index(int column, int row) const noexcept { return row * columns + column; }
    bool isValid(int column, int row) const noexcept
    {
        const int i = index(column, row);
        return i >= 0 && i < valid.size() && valid.at(i) != 0;
    }
    double value(int column, int row) const noexcept { return values.at(index(column, row)); }
    QPointF screenPoint(int column, int row) const noexcept
    {
        const double tx = columns > 1 ? double(column) / double(columns - 1) : 0.0;
        const double ty = rows > 1 ? double(row) / double(rows - 1) : 0.0;
        return {screenRect.left() + tx * screenRect.width(),
                screenRect.top() + ty * screenRect.height()};
    }
};

inline double fieldVisualizationPercentile(QVector<double> values, double fraction)
{
    if (values.isEmpty())
        return 0.0;
    std::sort(values.begin(), values.end());
    fraction = std::clamp(fraction, 0.0, 1.0);
    const double position = fraction * double(values.size() - 1);
    const int lo = int(std::floor(position));
    const int hi = int(std::ceil(position));
    const double t = position - double(lo);
    return values.at(lo) * (1.0 - t) + values.at(hi) * t;
}

inline QPointF fieldVisualizationEdgeIntersection(const QPointF &a,
                                                   const QPointF &b,
                                                   double va,
                                                   double vb,
                                                   double level)
{
    const double denominator = vb - va;
    const double t = std::abs(denominator) > 1e-30
        ? std::clamp((level - va) / denominator, 0.0, 1.0)
        : 0.5;
    return a + (b - a) * t;
}

inline QVector<QLineF> fieldVisualizationContours(const FieldVisualizationGrid &grid,
                                                   const QVector<double> &levels)
{
    QVector<QLineF> lines;
    if (grid.columns < 2 || grid.rows < 2 || levels.isEmpty())
        return lines;

    for (const double level : levels)
    {
        for (int row = 0; row < grid.rows - 1; ++row)
        {
            for (int column = 0; column < grid.columns - 1; ++column)
            {
                if (!grid.isValid(column, row) ||
                    !grid.isValid(column + 1, row) ||
                    !grid.isValid(column + 1, row + 1) ||
                    !grid.isValid(column, row + 1))
                    continue;

                const double v00 = grid.value(column, row);
                const double v10 = grid.value(column + 1, row);
                const double v11 = grid.value(column + 1, row + 1);
                const double v01 = grid.value(column, row + 1);

                const QPointF p00 = grid.screenPoint(column, row);
                const QPointF p10 = grid.screenPoint(column + 1, row);
                const QPointF p11 = grid.screenPoint(column + 1, row + 1);
                const QPointF p01 = grid.screenPoint(column, row + 1);

                QVector<QPointF> hits;
                hits.reserve(4);
                const auto crosses = [level](double a, double b) {
                    return (a < level && b >= level) || (b < level && a >= level);
                };

                if (crosses(v00, v10)) hits.push_back(fieldVisualizationEdgeIntersection(p00, p10, v00, v10, level));
                if (crosses(v10, v11)) hits.push_back(fieldVisualizationEdgeIntersection(p10, p11, v10, v11, level));
                if (crosses(v11, v01)) hits.push_back(fieldVisualizationEdgeIntersection(p11, p01, v11, v01, level));
                if (crosses(v01, v00)) hits.push_back(fieldVisualizationEdgeIntersection(p01, p00, v01, v00, level));

                if (hits.size() == 2)
                {
                    lines.push_back(QLineF(hits.at(0), hits.at(1)));
                }
                else if (hits.size() == 4)
                {
                    // Ambiguous saddle cell. Pair the closest endpoints; this keeps the
                    // visualization stable without requiring topology-specific metadata.
                    const double d01 = QLineF(hits.at(0), hits.at(1)).length() + QLineF(hits.at(2), hits.at(3)).length();
                    const double d03 = QLineF(hits.at(0), hits.at(3)).length() + QLineF(hits.at(1), hits.at(2)).length();
                    if (d01 <= d03)
                    {
                        lines.push_back(QLineF(hits.at(0), hits.at(1)));
                        lines.push_back(QLineF(hits.at(2), hits.at(3)));
                    }
                    else
                    {
                        lines.push_back(QLineF(hits.at(0), hits.at(3)));
                        lines.push_back(QLineF(hits.at(1), hits.at(2)));
                    }
                }
            }
        }
    }
    return lines;
}

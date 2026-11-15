#pragma once

// Numerical *display* integrator: no dependency on Qt and no changes to the
// validated electrostatic field equations. All distances use the view's units.
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace ElectrostaticFieldLines
{
struct Point
{
    double x = 0.0;
    double y = 0.0;
};
inline Point operator+(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
inline Point operator-(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
inline Point operator*(Point a, double s) { return {a.x * s, a.y * s}; }
inline double norm(Point p) { return std::hypot(p.x, p.y); }
inline double distance(Point a, Point b) { return norm(a - b); }

struct SourceStop
{
    Point center;
    double radius = 0.0;
};
struct Bounds
{
    double left = 0.0;
    double bottom = 0.0;
    double right = 0.0;
    double top = 0.0;
    bool contains(Point p) const
    {
        return p.x >= left && p.x <= right && p.y >= bottom && p.y <= top;
    }
};
struct Settings
{
    double maxStep = 0.05;  // approx 6 view pixels
    double minStep = 0.005; // approx 0.6 view pixels
    int maxSteps = 1600;
};

// Expects an un-normalized 2D projected electric field. Returned line follows
// E when direction is +1 and -E when direction is -1. It terminates before
// approaching a charge singularity; no line may spiral through that charge.
inline std::vector<Point> trace(Point seed, double direction,
                                const Bounds &bounds,
                                const std::vector<SourceStop> &stops,
                                const std::function<Point(Point)> &electricField,
                                Settings settings)
{
    std::vector<Point> points;
    if (!bounds.contains(seed) || direction == 0.0 || settings.maxStep <= 0.0)
        return points;
    points.reserve(256);
    points.push_back(seed);
    settings.minStep = std::max(1.0e-9, std::min(settings.minStep, settings.maxStep));
    settings.maxSteps = std::clamp(settings.maxSteps, 1, 3000);

    auto tangent = [&](Point p, Point &result) -> bool {
        if (!bounds.contains(p)) return false;
        const Point v = electricField(p);
        const double n = norm(v);
        if (!std::isfinite(n) || n < 1.0e-35) return false;
        result = v * (direction / n);
        return std::isfinite(result.x) && std::isfinite(result.y);
    };

    for (int iter = 0; iter < settings.maxSteps; ++iter)
    {
        const Point p = points.back();
        double nearest = std::numeric_limits<double>::infinity();
        for (const SourceStop &stop : stops)
            nearest = std::min(nearest, distance(p, stop.center) - stop.radius);
        if (nearest <= 0.0) break;
        double h = std::min(settings.maxStep, std::max(settings.minStep, nearest * 0.24));
        h = std::min(h, nearest * 0.45);
        if (h < settings.minStep * 0.2) break;

        Point k1{}, k2{}, k3{}, k4{}, next{};
        bool good = false;
        for (int retry = 0; retry < 10; ++retry)
        {
            if (!tangent(p, k1) || !tangent(p + k1 * (h * 0.5), k2) ||
                !tangent(p + k2 * (h * 0.5), k3) || !tangent(p + k3 * h, k4))
                break;
            next = p + (k1 + k2 * 2.0 + k3 * 2.0 + k4) * (h / 6.0);
            if (!bounds.contains(next)) break;
            bool entersSource = false;
            for (const SourceStop &stop : stops)
                if (distance(next, stop.center) < stop.radius) entersSource = true;
            // Reduce step in strongly curved zones; prevents near-singularity
            // overshoot and unphysical apparent loops at charge markers.
            const double turn = std::clamp(k1.x * k4.x + k1.y * k4.y, -1.0, 1.0);
            if ((entersSource || turn < 0.94) && h > settings.minStep * 0.22)
            {
                h *= 0.5;
                continue;
            }
            if (entersSource) break;
            good = true;
            break;
        }
        if (!good || distance(p, next) < settings.minStep * 0.05) break;
        points.push_back(next);
        // Guard against numerical cycles around null points. Electrostatic
        // equipotential fields cannot have closed field-line loops.
        if (points.size() > 24)
        {
            bool loop = false;
            for (std::size_t j = 0; j + 18 < points.size(); j += 3)
            {
                if (distance(points[j], next) < settings.minStep * 1.5)
                {
                    loop = true;
                    break;
                }
            }
            if (loop) break;
        }
    }
    return points;
}
}

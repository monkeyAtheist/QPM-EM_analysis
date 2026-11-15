#include "electrostatic_field_line_integrator.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using namespace ElectrostaticFieldLines;

int main()
{
    const std::vector<SourceStop> charges = {{{-0.7, 0.0}, 0.08}, {{0.7, 0.0}, 0.08}};
    const Bounds domain{-3.0, -2.5, 3.0, 2.5};
    const auto electricField = [](Point p) {
        const auto at = [&](Point c, double q) {
            const Point r = p - c;
            const double d = norm(r);
            if (d < 1.0e-10) return Point{0.0, 0.0};
            return r * (q / (d*d*d));
        };
        return at({-0.7, 0.0}, 1.0) + at({0.7, 0.0}, -1.0);
    };
    int sinkEnds = 0;
    int boundsEnds = 0;
    for (int i = 0; i < 24; ++i)
    {
        const double a = 2.0 * 3.14159265358979323846 * double(i) / 24;
        Point seed{-0.7 + 0.13 * std::cos(a), 0.13 * std::sin(a)};
        const auto line = trace(seed, +1, domain, charges, electricField, {0.05, 0.003, 600});
        assert(line.size() > 5);
        assert(line.size() < 601);
        for (auto p : line) {
            assert(std::isfinite(p.x) && std::isfinite(p.y));
            assert(domain.contains(p));
            assert(distance(p, charges[0].center) >= charges[0].radius);
            assert(distance(p, charges[1].center) >= charges[1].radius);
        }
        const double sinkDist = distance(line.back(), charges[1].center);
        if (sinkDist < 0.085) ++sinkEnds;
        else ++boundsEnds;
    }
    assert(sinkEnds > 10);
    std::cout << "Electrostatic 2D fieldlines: " << sinkEnds << " terminate at negative charge, "
              << boundsEnds << " others terminate/exit within bounded domain; PASS\n";
    const auto invalid = trace({0, 0}, +1, domain, {}, [](Point){ return Point{0, 0}; }, {});
    assert(invalid.size() == 1); // null electric field never propagates
    std::cout << "Null-field guard: PASS\n";
}

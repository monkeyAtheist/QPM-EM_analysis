#include "numerical_em_solvers.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace NumericalEM
{
namespace
{
constexpr std::array<double, 8> GlX{
    0.09501250983763744, 0.28160355077925891, 0.45801677765722739, 0.61787624440264375,
    0.75540440835500303, 0.86563120238783174, 0.94457502307323258, 0.98940093499164993};
constexpr std::array<double, 8> GlW{
    0.18945061045506850, 0.18260341504492359, 0.16915651939500254, 0.14959598881657673,
    0.12462897125553387, 0.09515851168249278, 0.06225352393864789, 0.02715245941175409};

inline bool finiteScalar(double v) { return std::isfinite(v); }
inline bool finiteVec3(const Vec3 &v) { return finiteScalar(v.x) && finiteScalar(v.y) && finiteScalar(v.z); }
inline bool finiteComplex(const std::complex<double> &v) { return finiteScalar(v.real()) && finiteScalar(v.imag()); }
inline double sqr(double v) { return v * v; }
inline double length(const Vec2 &v) { return std::sqrt(v.x*v.x + v.y*v.y); }
inline Vec2 sub(const Vec2 &a, const Vec2 &b) { return {a.x-b.x, a.y-b.y}; }
inline Vec2 add(const Vec2 &a, const Vec2 &b) { return {a.x+b.x, a.y+b.y}; }
inline Vec2 mul(const Vec2 &a, double s) { return {a.x*s, a.y*s}; }

struct SolveStats
{
    double minPivot = std::numeric_limits<double>::infinity();
    double maxPivot = 0.0;
};

bool solveReal(std::vector<std::vector<double>> a, std::vector<double> b,
               std::vector<double> &x, SolveStats &stats)
{
    const int n = static_cast<int>(b.size());
    if (static_cast<int>(a.size()) != n) return false;
    for (int k = 0; k < n; ++k)
    {
        int pivot = k;
        double best = std::abs(a[k][k]);
        for (int i = k + 1; i < n; ++i)
        {
            const double candidate = std::abs(a[i][k]);
            if (candidate > best) { best = candidate; pivot = i; }
        }
        if (!(best > 1e-18) || !std::isfinite(best)) return false;
        if (pivot != k) { std::swap(a[pivot], a[k]); std::swap(b[pivot], b[k]); }
        stats.minPivot = std::min(stats.minPivot, best);
        stats.maxPivot = std::max(stats.maxPivot, best);
        const double diag = a[k][k];
        for (int i = k + 1; i < n; ++i)
        {
            const double f = a[i][k] / diag;
            if (f == 0.0) continue;
            a[i][k] = 0.0;
            for (int j = k + 1; j < n; ++j) a[i][j] -= f * a[k][j];
            b[i] -= f * b[k];
        }
    }
    x.assign(n, 0.0);
    for (int i = n - 1; i >= 0; --i)
    {
        double s = b[i];
        for (int j = i + 1; j < n; ++j) s -= a[i][j] * x[j];
        if (std::abs(a[i][i]) < 1e-18) return false;
        x[i] = s / a[i][i];
    }
    return true;
}

bool solveComplex(std::vector<std::vector<std::complex<double>>> a,
                  std::vector<std::complex<double>> b,
                  std::vector<std::complex<double>> &x,
                  SolveStats &stats)
{
    const int n = static_cast<int>(b.size());
    if (static_cast<int>(a.size()) != n) return false;
    for (int k = 0; k < n; ++k)
    {
        int pivot = k;
        double best = std::abs(a[k][k]);
        for (int i = k + 1; i < n; ++i)
        {
            const double candidate = std::abs(a[i][k]);
            if (candidate > best) { best = candidate; pivot = i; }
        }
        if (!(best > 1e-18) || !std::isfinite(best)) return false;
        if (pivot != k) { std::swap(a[pivot], a[k]); std::swap(b[pivot], b[k]); }
        stats.minPivot = std::min(stats.minPivot, best);
        stats.maxPivot = std::max(stats.maxPivot, best);
        const auto diag = a[k][k];
        for (int i = k + 1; i < n; ++i)
        {
            const auto f = a[i][k] / diag;
            if (std::abs(f) == 0.0) continue;
            a[i][k] = {0.0, 0.0};
            for (int j = k + 1; j < n; ++j) a[i][j] -= f * a[k][j];
            b[i] -= f * b[k];
        }
    }
    x.assign(n, {0.0, 0.0});
    for (int i = n - 1; i >= 0; --i)
    {
        auto s = b[i];
        for (int j = i + 1; j < n; ++j) s -= a[i][j] * x[j];
        if (std::abs(a[i][i]) < 1e-18) return false;
        x[i] = s / a[i][i];
    }
    return true;
}

struct NullspaceBasis
{
    int rank = 0;
    std::vector<int> independentRows;
    // Row-major transform T: physicalCurrent = T * reducedCurrent.
    std::vector<std::vector<double>> t;
};

bool buildConstraintNullspace(const std::vector<std::vector<double>> &constraints,
                              int columnCount,
                              NullspaceBasis &out)
{
    out = {};
    if (columnCount <= 0) return false;
    if (constraints.empty())
    {
        out.t.assign(static_cast<std::size_t>(columnCount),
                     std::vector<double>(static_cast<std::size_t>(columnCount), 0.0));
        for (int i=0;i<columnCount;++i) out.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)] = 1.0;
        return true;
    }

    std::vector<std::vector<double>> a = constraints;
    for (auto &row : a) row.resize(static_cast<std::size_t>(columnCount), 0.0);
    std::vector<int> rowIds(a.size());
    std::iota(rowIds.begin(), rowIds.end(), 0);
    std::vector<int> pivotCols;
    int pivotRow = 0;
    constexpr double tol = 1e-12;
    for (int col=0; col<columnCount && pivotRow<static_cast<int>(a.size()); ++col)
    {
        int bestRow = -1;
        double best = tol;
        for (int r=pivotRow; r<static_cast<int>(a.size()); ++r)
        {
            const double v = std::abs(a[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)]);
            if (v > best) { best=v; bestRow=r; }
        }
        if (bestRow < 0) continue;
        if (bestRow != pivotRow)
        {
            std::swap(a[static_cast<std::size_t>(bestRow)], a[static_cast<std::size_t>(pivotRow)]);
            std::swap(rowIds[static_cast<std::size_t>(bestRow)], rowIds[static_cast<std::size_t>(pivotRow)]);
        }
        const double diag = a[static_cast<std::size_t>(pivotRow)][static_cast<std::size_t>(col)];
        for (int j=0;j<columnCount;++j) a[static_cast<std::size_t>(pivotRow)][static_cast<std::size_t>(j)] /= diag;
        for (int r=0;r<static_cast<int>(a.size());++r)
        {
            if (r==pivotRow) continue;
            const double f = a[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)];
            if (std::abs(f)<=tol) continue;
            for (int j=0;j<columnCount;++j)
                a[static_cast<std::size_t>(r)][static_cast<std::size_t>(j)] -= f*a[static_cast<std::size_t>(pivotRow)][static_cast<std::size_t>(j)];
        }
        pivotCols.push_back(col);
        out.independentRows.push_back(rowIds[static_cast<std::size_t>(pivotRow)]);
        ++pivotRow;
    }
    out.rank = pivotRow;
    const int reducedCount = columnCount - out.rank;
    if (reducedCount <= 0) return false;

    std::vector<bool> isPivot(static_cast<std::size_t>(columnCount), false);
    for (int c : pivotCols) isPivot[static_cast<std::size_t>(c)] = true;
    std::vector<int> freeCols;
    freeCols.reserve(static_cast<std::size_t>(reducedCount));
    for (int c=0;c<columnCount;++c) if (!isPivot[static_cast<std::size_t>(c)]) freeCols.push_back(c);

    out.t.assign(static_cast<std::size_t>(columnCount),
                 std::vector<double>(static_cast<std::size_t>(reducedCount), 0.0));
    for (int k=0;k<reducedCount;++k)
    {
        std::vector<double> x(static_cast<std::size_t>(columnCount), 0.0);
        x[static_cast<std::size_t>(freeCols[static_cast<std::size_t>(k)])] = 1.0;
        for (int r=out.rank-1;r>=0;--r)
        {
            const int pc = pivotCols[static_cast<std::size_t>(r)];
            double sum = 0.0;
            for (int c=0;c<columnCount;++c)
                if (c!=pc) sum += a[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)]*x[static_cast<std::size_t>(c)];
            x[static_cast<std::size_t>(pc)] = -sum;
        }
        double n2 = 0.0;
        for (double v : x) n2 += v*v;
        const double inv = 1.0/std::sqrt(std::max(n2,1e-30));
        for (int i=0;i<columnCount;++i) out.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)] = x[static_cast<std::size_t>(i)]*inv;
    }

    // Sanity check: every generated basis vector must satisfy every supplied KCL row.
    for (const auto &row : constraints)
        for (int k=0;k<reducedCount;++k)
        {
            double s = 0.0;
            for (int i=0;i<columnCount && i<static_cast<int>(row.size());++i)
                s += row[static_cast<std::size_t>(i)]*out.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)];
            if (std::abs(s) > 1e-9) return false;
        }
    return true;
}

std::vector<BemPanel2D> panelsForConductor(const BemConductor2D &c, int index, int requested)
{
    std::vector<BemPanel2D> out;
    requested = std::clamp(requested, 8, 240);
    if (c.shape == ConductorShape::Circle)
    {
        const double r = std::max(c.radiusM, 1e-9);
        out.reserve(requested);
        for (int i = 0; i < requested; ++i)
        {
            const double a0 = 2.0 * Pi * i / requested;
            const double a1 = 2.0 * Pi * (i + 1) / requested;
            const Vec2 p0{c.center.x + r*std::cos(a0), c.center.y + r*std::sin(a0)};
            const Vec2 p1{c.center.x + r*std::cos(a1), c.center.y + r*std::sin(a1)};
            const Vec2 mid = mul(add(p0,p1),0.5);
            out.push_back({p0,p1,mid,length(sub(p1,p0)),index,0.0});
        }
        return out;
    }

    const double w = std::max(c.widthM, 1e-9);
    const double h = std::max(c.heightM, 1e-9);
    const double perimeter = 2.0 * (w+h);
    int nx = std::max(2, int(std::round(requested * w / perimeter)));
    int ny = std::max(2, int(std::round(requested * h / perimeter)));
    const Vec2 corners[4] = {
        {c.center.x-w/2,c.center.y-h/2}, {c.center.x+w/2,c.center.y-h/2},
        {c.center.x+w/2,c.center.y+h/2}, {c.center.x-w/2,c.center.y+h/2}};
    auto addEdge=[&](Vec2 a,Vec2 b,int n){
        for(int i=0;i<n;++i){ const double t0=double(i)/n,t1=double(i+1)/n;
            const Vec2 p0{a.x+(b.x-a.x)*t0,a.y+(b.y-a.y)*t0};
            const Vec2 p1{a.x+(b.x-a.x)*t1,a.y+(b.y-a.y)*t1};
            out.push_back({p0,p1,mul(add(p0,p1),0.5),length(sub(p1,p0)),index,0.0}); }};
    addEdge(corners[0],corners[1],nx); addEdge(corners[1],corners[2],ny);
    addEdge(corners[2],corners[3],nx); addEdge(corners[3],corners[0],ny);
    return out;
}

bool conductorsOverlap(const BemConductor2D &a, const BemConductor2D &b)
{
    if (a.shape == ConductorShape::Circle && b.shape == ConductorShape::Circle)
    {
        const double dx=a.center.x-b.center.x,dy=a.center.y-b.center.y;
        return std::sqrt(dx*dx+dy*dy) <= a.radiusM+b.radiusM;
    }
    if (a.shape == ConductorShape::Rectangle && b.shape == ConductorShape::Rectangle)
        return std::abs(a.center.x-b.center.x) <= 0.5*(a.widthM+b.widthM) &&
               std::abs(a.center.y-b.center.y) <= 0.5*(a.heightM+b.heightM);

    const BemConductor2D &circle = a.shape == ConductorShape::Circle ? a : b;
    const BemConductor2D &rect = a.shape == ConductorShape::Rectangle ? a : b;
    const double nearestX=std::clamp(circle.center.x,rect.center.x-rect.widthM/2.0,rect.center.x+rect.widthM/2.0);
    const double nearestY=std::clamp(circle.center.y,rect.center.y-rect.heightM/2.0,rect.center.y+rect.heightM/2.0);
    const double dx=circle.center.x-nearestX,dy=circle.center.y-nearestY;
    return dx*dx+dy*dy <= circle.radiusM*circle.radiusM;
}

double logPanelIntegral(const BemPanel2D &panel, const Vec2 &obs, bool self)
{
    constexpr double rRef = 1.0;
    if (self)
    {
        const double L = std::max(panel.lengthM, 1e-18);
        return L * (std::log(L/(2.0*rRef)) - 1.0);
    }
    const Vec2 d = sub(panel.p1,panel.p0);
    double sum = 0.0;
    for (int g=0;g<8;++g)
    {
        for (int sgn : {-1,1})
        {
            const double xi = sgn * GlX[g];
            const double t = 0.5*(xi+1.0);
            const Vec2 p{panel.p0.x+d.x*t,panel.p0.y+d.y*t};
            const double r = std::max(length(sub(obs,p)),1e-18);
            sum += GlW[g] * std::log(r/rRef);
        }
    }
    return panel.lengthM * 0.5 * sum;
}

std::complex<double> pocklingtonKernel(double zObs, double zSrc, double a, double k,
                                       double omega, double epsilon)
{
    const std::complex<double> j(0.0,1.0);
    const double R = std::sqrt(a*a + sqr(zObs-zSrc));
    const auto expTerm = std::exp(-j*k*R);
    const auto bracket = (1.0 + j*k*R) * (2.0*R*R - 3.0*a*a) + sqr(k*a*R);
    return j/(omega*epsilon) * expTerm * bracket / (4.0*Pi*std::pow(R,5));
}

std::complex<double> integratePocklingtonSegment(double zObs, double z0, double z1,
                                                  double a, double k, double omega, double epsilon)
{
    const double mid=0.5*(z0+z1),half=0.5*(z1-z0);
    std::complex<double> sum{0.0,0.0};
    for(int g=0;g<8;++g)
        for(int sgn:{-1,1})
            sum += GlW[g]*pocklingtonKernel(zObs,mid+half*sgn*GlX[g],a,k,omega,epsilon);
    return half*sum;
}



struct NetworkNode
{
    Vec3 p{};
    std::vector<int> edges;
};

struct NetworkEdge
{
    int n0 = -1;
    int n1 = -1;
    double radiusM = 0.001;
    int wireIndex = -1;
};

struct OrientedNetworkEdge
{
    int edgeIndex = -1;
    int fromNode = -1;
    int toNode = -1;
    Vec3 p0{};
    Vec3 p1{};
    Vec3 tangent{};
    double lengthM = 0.0;
    double radiusM = 0.001;
    int componentIndex = -1;
    double pathStartM = 0.0;
};

struct NetworkComponent
{
    bool closed = false;
    std::vector<int> nodes;
    std::vector<OrientedNetworkEdge> edges;
};


bool vecLexLess(const Vec3 &a, const Vec3 &b)
{
    if (a.x != b.x) return a.x < b.x;
    if (a.y != b.y) return a.y < b.y;
    return a.z < b.z;
}

struct RooftopSupport
{
    int edgeIndex = -1;
    double endpointCoefficient = 0.0; // signed current along the oriented edge at the peak node
    bool peakAtP0 = true;
};

struct RooftopFunction
{
    int nodeIndex = -1;
    std::vector<RooftopSupport> supports;
};

struct RooftopBasisSet
{
    std::vector<RooftopFunction> functions;
    int openEndCount = 0;
    int ordinaryContinuityCount = 0;
    int branchKclCount = 0;
};

RooftopBasisSet buildRooftopBasis(const std::vector<NetworkNode> &nodes,
                                  const std::vector<OrientedNetworkEdge> &orientedEdges,
                                  const std::vector<int> &edgeToOriented)
{
    RooftopBasisSet out;
    for(int ni=0;ni<static_cast<int>(nodes.size());++ni)
    {
        const auto &node=nodes[static_cast<std::size_t>(ni)];
        const int degree=static_cast<int>(node.edges.size());
        if(degree<=0)continue;
        if(degree==1){++out.openEndCount;continue;}
        if(degree==2)++out.ordinaryContinuityCount; else ++out.branchKclCount;

        struct Arm{int edge=-1;double outwardSign=1.0;int otherNode=-1;bool peakAtP0=true;};
        std::vector<Arm> arms;arms.reserve(node.edges.size());
        for(int rawEdge:node.edges)
        {
            const int oi=edgeToOriented[static_cast<std::size_t>(rawEdge)];
            if(oi<0)continue;
            const auto &oe=orientedEdges[static_cast<std::size_t>(oi)];
            const bool atP0=oe.fromNode==ni;
            arms.push_back({oi,atP0?+1.0:-1.0,atP0?oe.toNode:oe.fromNode,atP0});
        }
        std::sort(arms.begin(),arms.end(),[&](const Arm&a,const Arm&b){
            return vecLexLess(nodes[static_cast<std::size_t>(a.otherNode)].p,nodes[static_cast<std::size_t>(b.otherNode)].p);
        });
        if(arms.size()<2)continue;
        // d incident arms require d-1 independent KCL-safe junction modes. The first arm is a
        // deterministic reference; each mode sends one unit outward on arm k and one unit inward
        // on the reference arm. Degree-2 nodes therefore reduce to the usual rooftop current.
        const Arm ref=arms.front();
        for(std::size_t k=1;k<arms.size();++k)
        {
            RooftopFunction f;f.nodeIndex=ni;
            f.supports.push_back({ref.edge,-ref.outwardSign,ref.peakAtP0});
            f.supports.push_back({arms[k].edge,+arms[k].outwardSign,arms[k].peakAtP0});
            out.functions.push_back(std::move(f));
        }
    }
    return out;
}

inline double rooftopShape(bool peakAtP0,double xi)
{
    const double u=0.5*(xi+1.0);
    return peakAtP0?(1.0-u):u;
}

double pointSegmentDistance3(const Vec3 &p, const Vec3 &a, const Vec3 &b, double *tOut = nullptr)
{
    const Vec3 ab = b - a;
    const double d2 = dot(ab, ab);
    double t = d2 > 1e-30 ? dot(p - a, ab) / d2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    if (tOut) *tOut = t;
    return norm(p - (a + ab * t));
}

double segmentSegmentDistance3(const Vec3 &p0, const Vec3 &p1, const Vec3 &q0, const Vec3 &q1,
                               double *sOut = nullptr, double *tOut = nullptr, Vec3 *midOut = nullptr)
{
    // Closest points on two finite 3D segments (Ericson-style clamped solution).  This is
    // used only for CAD/topology contact detection; electromagnetic coupling still uses the
    // full EFIE kernel even when conductors are not galvanically connected.
    const Vec3 d1=p1-p0,d2=q1-q0,r=p0-q0;
    const double a=dot(d1,d1),e=dot(d2,d2),f=dot(d2,r);
    double s=0.0,t=0.0;
    if(a<=1e-30&&e<=1e-30)
    {
        if(sOut) *sOut=0.0;
        if(tOut) *tOut=0.0;
        if(midOut) *midOut=(p0+q0)*0.5;
        return norm(p0-q0);
    }
    if(a<=1e-30)
        t=std::clamp(f/e,0.0,1.0);
    else
    {
        const double c=dot(d1,r);
        if(e<=1e-30)s=std::clamp(-c/a,0.0,1.0);
        else
        {
            const double b=dot(d1,d2),den=a*e-b*b;
            if(std::abs(den)>1e-30)s=std::clamp((b*f-c*e)/den,0.0,1.0);
            t=(b*s+f)/e;
            if(t<0.0){t=0.0;s=std::clamp(-c/a,0.0,1.0);}
            else if(t>1.0){t=1.0;s=std::clamp((b-c)/a,0.0,1.0);}
        }
    }
    const Vec3 cp=p0+d1*s,cq=q0+d2*t;
    if(sOut) *sOut=s;
    if(tOut) *tOut=t;
    if(midOut) *midOut=(cp+cq)*0.5;
    return norm(cp-cq);
}

std::complex<double> projectedEfieKernel(const Vec3 &rObs, const Vec3 &tObs,
                                         const Vec3 &rSrc, const Vec3 &tSrc,
                                         double regularizationRadiusM,
                                         double k, double omega)
{
    const std::complex<double> j(0.0, 1.0);
    const Vec3 d = rObs - rSrc;
    const double a = std::max(regularizationRadiusM, 1e-12);
    const double R = std::sqrt(dot(d, d) + a*a);
    const double kR = std::max(k * R, 1e-15);
    const Vec3 q = d * (1.0 / R); // axial part of R-hat; virtual radial offset regularizes the thin wire.
    const auto G = std::exp(-j * k * R) / (4.0 * Pi * R);
    const auto A = 1.0 - j / kR - 1.0 / (kR*kR);
    const auto B = -1.0 + 3.0*j / kR + 3.0 / (kR*kR);
    const auto projection = A * dot(tObs, tSrc) + B * dot(tObs, q) * dot(tSrc, q);
    return j * omega * Mu0 * G * projection;
}

// Radius-aware composite Gauss integration for a pulse source segment.  The thin-wire
// EFIE self and near-neighbour kernels vary on the radial scale a, while a wavelength-based
// mesh segment can be several radii long.  A single fixed Gauss panel then badly under-resolves
// the sharp reactive near field and makes Zin oscillate with segmentation.  Split only the
// near interactions into smaller source panels; far interactions keep the historical 16-point
// rule.  This changes numerical quadrature, not the underlying Pocklington/dyadic kernel.
std::complex<double> integrateProjectedPulseSegment(const Vec3 &rObs, const Vec3 &tObs,
                                                     const Vec3 &p0, const Vec3 &p1,
                                                     const Vec3 &tSrc, double radiusM,
                                                     double k, double omega)
{
    const double lengthM = norm(p1-p0);
    if (!(lengthM > 1e-18)) return {0.0,0.0};
    double uClosest = 0.0;
    const double distanceM = pointSegmentDistance3(rObs,p0,p1,&uClosest);
    const double a = std::max(radiusM,1e-12);

    int panels = 1;
    // Self/adjacent interactions need resolution on the radial scale.  Limiting to 32 panels
    // keeps the dense O(N^2) solve practical while making the common Δl/a=3...12 range stable.
    if (distanceM < std::max(4.0*a,0.75*lengthM))
        panels = std::clamp(static_cast<int>(std::ceil(lengthM/(1.25*a))),1,32);

    std::complex<double> total{0.0,0.0};
    for (int panel=0; panel<panels; ++panel)
    {
        const double u0=double(panel)/double(panels), u1=double(panel+1)/double(panels);
        const Vec3 a0=p0+(p1-p0)*u0, a1=p0+(p1-p0)*u1;
        const Vec3 mid=(a0+a1)*0.5, half=(a1-a0)*0.5;
        std::complex<double> sum{0.0,0.0};
        for (int g=0; g<8; ++g)
            for (int sgn : {-1,1})
            {
                const Vec3 rs=mid+half*(sgn*GlX[static_cast<std::size_t>(g)]);
                sum += GlW[static_cast<std::size_t>(g)] * projectedEfieKernel(rObs,tObs,rs,tSrc,a,k,omega);
            }
        total += 0.5*norm(a1-a0)*sum;
    }
    return total;
}

// Radius-aware composite Galerkin integration for a pair of piecewise-linear rooftop supports.
// Unlike the historical 4x4 rule, this resolves BOTH the observation and source spans on the
// wire-radius scale when their centerlines are self/near coupled.  The rooftop shape is always
// evaluated in the original span coordinate, so panel subdivision changes quadrature only --
// it does not alter the basis or the EFIE operator.
std::complex<double> integrateProjectedRooftopPair(const Vec3 &obsP0, const Vec3 &obsP1,
                                                    const Vec3 &obsTangent, bool obsPeakAtP0,
                                                    const Vec3 &srcP0, const Vec3 &srcP1,
                                                    const Vec3 &srcTangent, bool srcPeakAtP0,
                                                    double radiusM, double k, double omega)
{
    constexpr std::array<double,4> gx{-0.8611363115940526,-0.3399810435848563,
                                       0.3399810435848563, 0.8611363115940526};
    constexpr std::array<double,4> gw{ 0.3478548451374538, 0.6521451548625461,
                                       0.6521451548625461, 0.3478548451374538};

    const double lo=norm(obsP1-obsP0), ls=norm(srcP1-srcP0);
    if(!(lo>1e-18) || !(ls>1e-18)) return {0.0,0.0};
    const double a=std::max(radiusM,1e-12);
    const double d=segmentSegmentDistance3(obsP0,obsP1,srcP0,srcP1);

    int po=1, ps=1;
    const bool nearPair=d<std::max(5.0*a,0.60*std::max(lo,ls));
    if(nearPair)
    {
        // The near-field rooftop double integral needs several Gauss panels even when Δl/a is only
        // 3..8. Use approximately one composite panel per radius plus two guard panels. The cap
        // keeps very thin-wire educational loops practical even when their Δl/a is much larger.
        po=std::clamp(static_cast<int>(std::ceil(lo/a))+2,4,48);
        ps=std::clamp(static_cast<int>(std::ceil(ls/a))+2,4,48);
    }

    const Vec3 od=obsP1-obsP0, sd=srcP1-srcP0;
    std::complex<double> total{0.0,0.0};
    for(int io=0;io<po;++io)
    {
        const double uo0=double(io)/double(po),uo1=double(io+1)/double(po);
        const double uoc=0.5*(uo0+uo1),uoh=0.5*(uo1-uo0);
        for(int is=0;is<ps;++is)
        {
            const double us0=double(is)/double(ps),us1=double(is+1)/double(ps);
            const double usc=0.5*(us0+us1),ush=0.5*(us1-us0);
            std::complex<double> pair{0.0,0.0};
            for(int go=0;go<4;++go)
            {
                const double uo=uoc+uoh*gx[static_cast<std::size_t>(go)];
                const double xo=2.0*uo-1.0;
                const Vec3 ro=obsP0+od*uo;
                const double fo=rooftopShape(obsPeakAtP0,xo);
                for(int gs=0;gs<4;++gs)
                {
                    const double us=usc+ush*gx[static_cast<std::size_t>(gs)];
                    const double xs=2.0*us-1.0;
                    const Vec3 rs=srcP0+sd*us;
                    const double fs=rooftopShape(srcPeakAtP0,xs);
                    pair+=gw[static_cast<std::size_t>(go)]*gw[static_cast<std::size_t>(gs)]*fo*fs*
                          projectedEfieKernel(ro,obsTangent,rs,srcTangent,a,k,omega);
                }
            }
            total += (lo*uoh)*(ls*ush)*pair;
        }
    }
    return total;
}


Vec3 cross(const Vec3 &a,const Vec3 &b)
{
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}

void loopBasis(const Vec3 &axisIn, Vec3 &u, Vec3 &v)
{
    const Vec3 n=normalized(axisIn);
    const Vec3 ref=std::abs(n.z)<0.85?Vec3{0,0,1}:Vec3{0,1,0};
    u=normalized(cross(ref,n));
    v=cross(n,u);
}

} // namespace

Vec3 operator+(const Vec3 &a,const Vec3 &b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 operator-(const Vec3 &a,const Vec3 &b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 operator*(const Vec3 &a,double s){return {a.x*s,a.y*s,a.z*s};}
double dot(const Vec3&a,const Vec3&b){return a.x*b.x+a.y*b.y+a.z*b.z;}
double norm(const Vec3&a){return std::sqrt(dot(a,a));}
Vec3 normalized(const Vec3&a){const double n=norm(a);return n>1e-18?a*(1.0/n):Vec3{0,0,1};}
Vec3 axisFromAzimuthElevationDeg(double az,double el){const double a=az*Pi/180.0,e=el*Pi/180.0;return normalized({std::cos(e)*std::cos(a),std::cos(e)*std::sin(a),std::sin(e)});}

BemResult2D solveTwoConductorBem2D(const BemInput2D &input)
{
    BemResult2D out;
    if (!(input.epsilonR > 0.0)) { out.error="epsilonR must be positive"; return out; }
    if (std::abs(input.conductorA.potentialV-input.conductorB.potentialV)<1e-12)
    { out.error="Conductor potentials must differ"; return out; }
    if (conductorsOverlap(input.conductorA,input.conductorB))
    { out.error="Conductor bounding boxes overlap; separate the conductors"; return out; }

    auto pa=panelsForConductor(input.conductorA,0,input.panelsPerConductor);
    auto pb=panelsForConductor(input.conductorB,1,input.panelsPerConductor);
    out.panels.reserve(pa.size()+pb.size());
    out.panels.insert(out.panels.end(),pa.begin(),pa.end());
    out.panels.insert(out.panels.end(),pb.begin(),pb.end());
    const int n=static_cast<int>(out.panels.size());
    std::vector<std::vector<double>> A(n+1,std::vector<double>(n+1,0.0));
    std::vector<double> b(n+1,0.0);

    // Unknown u = sigma/epsilon. This scaling keeps the dense system numerically reasonable.
    for(int i=0;i<n;++i)
    {
        b[i]=out.panels[i].conductorIndex==0?input.conductorA.potentialV:input.conductorB.potentialV;
        for(int j=0;j<n;++j)
            A[i][j]=-logPanelIntegral(out.panels[j],out.panels[i].midpoint,i==j)/(2.0*Pi);
        A[i][n]=-1.0; // unknown common potential reference offset
    }
    for(int j=0;j<n;++j) A[n][j]=out.panels[j].lengthM; // enforce net charge = 0

    SolveStats stats;
    std::vector<double> x;
    if(!solveReal(A,b,x,stats)) { out.error="BEM dense system is singular or ill-conditioned"; return out; }

    const double eps=Epsilon0*input.epsilonR;
    out.commonPotentialOffsetV=x[n];
    for(int j=0;j<n;++j)
    {
        out.panels[j].surfaceChargeDensityCpm2=eps*x[j];
        out.totalChargePerLengthCpm[out.panels[j].conductorIndex]+=eps*x[j]*out.panels[j].lengthM;
    }
    out.voltageDifferenceV=input.conductorA.potentialV-input.conductorB.potentialV;
    out.capacitancePerLengthFpm=std::abs(out.totalChargePerLengthCpm[0]/out.voltageDifferenceV);
    out.minPivotAbs=stats.minPivot; out.maxPivotAbs=stats.maxPivot;

    double maxResidual=0.0;
    for(int i=0;i<n;++i)
    {
        double v=-out.commonPotentialOffsetV;
        for(int j=0;j<n;++j)
            v += -logPanelIntegral(out.panels[j],out.panels[i].midpoint,i==j)/(2.0*Pi) * x[j];
        const double target=out.panels[i].conductorIndex==0?input.conductorA.potentialV:input.conductorB.potentialV;
        maxResidual=std::max(maxResidual,std::abs(v-target));
    }
    out.maxBoundaryResidualV=maxResidual;
    out.valid=std::isfinite(out.capacitancePerLengthFpm) && out.capacitancePerLengthFpm>0.0;
    out.note="2D electrostatic boundary-element model of two infinitely long conductors. Constant surface-charge panels, logarithmic Green function, homogeneous dielectric, and zero net line charge are assumed. Result is capacitance per unit length; end/fringing effects are not represented.";
    return out;
}

double bemPotentialV(const BemResult2D &result,double epsilonR,const Vec2 &point)
{
    if(!result.valid || !(epsilonR>0.0)) return std::numeric_limits<double>::quiet_NaN();
    const double eps=Epsilon0*epsilonR;
    double v=-result.commonPotentialOffsetV;
    for(const auto &p:result.panels)
    {
        const double u=p.surfaceChargeDensityCpm2/eps;
        v += -logPanelIntegral(p,point,false)/(2.0*Pi)*u;
    }
    return v;
}

Vec2 bemElectricFieldVpm(const BemResult2D &result,double epsilonR,const Vec2 &point)
{
    Vec2 e{};
    if(!result.valid || !(epsilonR>0.0)) return e;
    const double eps=Epsilon0*epsilonR;
    for(const auto &panel:result.panels)
    {
        const Vec2 d=sub(panel.p1,panel.p0);
        double ix=0.0,iy=0.0;
        for(int g=0;g<8;++g) for(int sgn:{-1,1})
        {
            const double xi=sgn*GlX[g],t=0.5*(xi+1.0);
            const Vec2 p{panel.p0.x+d.x*t,panel.p0.y+d.y*t};
            const Vec2 r=sub(point,p); const double r2=std::max(r.x*r.x+r.y*r.y,1e-24);
            ix += GlW[g]*r.x/r2; iy += GlW[g]*r.y/r2;
        }
        const double factor=panel.surfaceChargeDensityCpm2/eps*panel.lengthM*0.5/(2.0*Pi);
        e.x += factor*ix; e.y += factor*iy;
    }
    return e;
}

double analyticTwoWireCapacitancePerLength(double radiusM,double centerSpacingM,double epsilonR)
{
    if(!(radiusM>0.0) || !(centerSpacingM>2.0*radiusM) || !(epsilonR>0.0)) return 0.0;
    return Pi*Epsilon0*epsilonR/std::acosh(centerSpacingM/(2.0*radiusM));
}

ThinWireMomResult solveThinWireDipolePocklington(const ThinWireMomInput &input)
{
    ThinWireMomResult out;
    if(!finiteScalar(input.frequencyHz) || !finiteScalar(input.lengthM) || !finiteScalar(input.radiusM) || !finiteScalar(input.feedVoltageV))
    { out.error="Thin-wire input contains NaN or Inf"; return out; }
    if(!(input.frequencyHz>0.0) || !(input.lengthM>0.0) || !(input.radiusM>0.0) || !(std::abs(input.feedVoltageV)>1e-18))
    { out.error="Frequency, length, radius and feed voltage must be non-zero positive magnitudes"; return out; }
    int n=std::clamp(input.segments,7,151); if((n%2)==0) ++n;
    if(input.radiusM>=0.1*input.lengthM) { out.error="Thin-wire model requires radius << length"; return out; }
    const double f=input.frequencyHz,omega=2.0*Pi*f,lambda=C0/f,k=2.0*Pi/lambda,eps=Epsilon0;
    const double dz=input.lengthM/n;
    out.wavelengthM=lambda;out.electricalLengthLambda=input.lengthM/lambda;out.segmentLengthM=dz;
    out.zM.resize(n);
    for(int i=0;i<n;++i) out.zM[i]=-input.lengthM/2.0+dz*(i+0.5);

    std::vector<std::vector<std::complex<double>>> Z(n,std::vector<std::complex<double>>(n));
    std::vector<std::complex<double>> rhs(n,{0.0,0.0});
    for(int m=0;m<n;++m)
        for(int q=0;q<n;++q)
        {
            const double zc=out.zM[q];
            Z[m][q]=integratePocklingtonSegment(out.zM[m],zc-dz/2.0,zc+dz/2.0,input.radiusM,k,omega,eps);
        }
    rhs[n/2]=input.feedVoltageV/dz; // delta-gap impressed axial field

    SolveStats stats;
    if(!solveComplex(Z,rhs,out.currentA,stats)) { out.error="Pocklington MoM matrix is singular"; return out; }
    out.minPivotAbs=stats.minPivot;out.maxPivotAbs=stats.maxPivot;
    const auto ifeed=out.currentA[n/2];
    if(std::abs(ifeed)<1e-18) { out.error="Solved feed current is approximately zero"; return out; }
    out.inputImpedanceOhm=input.feedVoltageV/ifeed;
    out.feedCurrentMagnitudeA=std::abs(ifeed);

    double maxR=0.0,maxB=0.0;
    for(int i=0;i<n;++i)
    {
        std::complex<double> s{0,0};for(int j=0;j<n;++j)s+=Z[i][j]*out.currentA[j];
        maxR=std::max(maxR,std::abs(s-rhs[i]));maxB=std::max(maxB,std::abs(rhs[i]));
    }
    out.residualRelative=maxR/std::max(maxB,1e-30);

    out.thetaDeg.reserve(181);out.normalizedFarField.reserve(181);
    std::vector<double> raw;raw.reserve(181);double maxF=0.0;
    for(int deg=0;deg<=180;++deg)
    {
        const double th=deg*Pi/180.0,ct=std::cos(th),st=std::sin(th);
        std::complex<double> sum{0,0};
        for(int i=0;i<n;++i) sum+=out.currentA[i]*std::exp(std::complex<double>(0,k*out.zM[i]*ct))*dz;
        const double mag=std::abs(st*sum);raw.push_back(mag);maxF=std::max(maxF,mag);out.thetaDeg.push_back(deg);
    }
    for(double r:raw)out.normalizedFarField.push_back(maxF>0?r/maxF:0.0);
    double integ=0.0;
    for(int i=0;i<180;++i)
    {
        const double t0=i*Pi/180.0,t1=(i+1)*Pi/180.0;
        const double u0=sqr(raw[i])*std::sin(t0),u1=sqr(raw[i+1])*std::sin(t1);
        integ+=0.5*(u0+u1)*(Pi/180.0);
    }
    if(integ>1e-30 && maxF>0.0)
    {
        out.directivityLinear=2.0*sqr(maxF)/integ;
        out.directivityDbi=10.0*std::log10(out.directivityLinear);
    }
    out.valid=finiteComplex(out.inputImpedanceOhm) && finiteScalar(out.residualRelative) && finiteScalar(out.directivityLinear);
    if(!out.valid && out.error.empty()) out.error="Thin-wire solve produced a non-finite numerical result";
    out.note="Experimental educational full-wave thin-wire solver: Pocklington EFIE, pulse current basis, point matching, perfect conductor in free space and a delta-gap feed. Input impedance is sensitive to wire radius, segmentation and feed model; verify convergence by changing the odd segment count and compare critical designs with NEC/MoM/FEM software.";
    return out;
}

WireNetworkMomResult solveWireNetworkMom(const WireNetworkMomInput &input)
{
    WireNetworkMomResult out;
    if (!finiteScalar(input.frequencyHz) || !finiteScalar(input.nodeMergeToleranceM) ||
        !finiteScalar(input.maxSegmentLengthRadiusFactor) || !finiteScalar(input.farFieldCutStepDeg) ||
        !finiteScalar(input.farFieldIntegrationStepDeg))
    { out.error = "Wire-network input contains NaN or Inf"; return out; }
    if (!(input.frequencyHz > 0.0)) { out.error = "Frequency must be positive"; return out; }
    if (input.wires.empty()) { out.error = "At least one wire span is required"; return out; }
    if (input.feeds.empty()) { out.error = "At least one feed is required"; return out; }
    if (input.maxUnknowns <= 0 || input.segmentsPerWavelength <= 0 || !(input.nodeMergeToleranceM >= 0.0) ||
        !(input.maxSegmentLengthRadiusFactor > 0.0) || !(input.farFieldCutStepDeg > 0.0) || !(input.farFieldIntegrationStepDeg > 0.0))
    { out.error = "Wire-network discretization parameters must be finite positive values"; return out; }
    for (const auto &w : input.wires)
    {
        if (!finiteVec3(w.aM) || !finiteVec3(w.bM) || !finiteScalar(w.radiusM))
        { out.error = "Wire geometry contains NaN or Inf"; return out; }
    }
    for (const auto &feed : input.feeds)
    {
        if (!finiteVec3(feed.positionM) || !finiteComplex(feed.voltageV) || !finiteScalar(feed.referenceOhm))
        { out.error = "Wire feed contains NaN or Inf"; return out; }
        if (!(feed.referenceOhm > 0.0)) { out.error = "Wire feed reference impedance must be positive"; return out; }
    }

    const double f = input.frequencyHz;
    const double omega = 2.0 * Pi * f;
    const double lambda = C0 / f;
    const double k = 2.0 * Pi / lambda;
    const int segmentsPerLambda = std::clamp(input.segmentsPerWavelength, 10, 240);
    const double wavelengthTarget = lambda / double(segmentsPerLambda);
    const double mergeTol = std::max({input.nodeMergeToleranceM, 1e-9, wavelengthTarget * 1e-8});
    const double splitTol = std::max(mergeTol * 4.0, 1e-8);
    out.wavelengthM = lambda;

    for (const auto &w : input.wires)
    {
        const double L = norm(w.bM - w.aM);
        if (!(L > 1e-9) || !(w.radiusM > 0.0)) { out.error = "Every wire must have positive length and radius"; return out; }
        if (w.radiusM >= 0.15 * L) { out.error = "Thin-wire model requires each wire radius to be much smaller than its span length"; return out; }
    }

    std::vector<NetworkNode> nodes;
    std::vector<NetworkEdge> edges;
    auto addNode = [&](const Vec3 &p) {
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
            if (norm(nodes[static_cast<std::size_t>(i)].p - p) <= mergeTol) return i;
        nodes.push_back(NetworkNode{p,{}});
        return static_cast<int>(nodes.size()) - 1;
    };
    auto addEdge = [&](int n0, int n1, double radius, int wireIndex) {
        if (n0 == n1) return;
        const int ei = static_cast<int>(edges.size());
        edges.push_back(NetworkEdge{n0,n1,radius,wireIndex});
        nodes[static_cast<std::size_t>(n0)].edges.push_back(ei);
        nodes[static_cast<std::size_t>(n1)].edges.push_back(ei);
    };

    std::vector<Vec3> splitCandidates;
    std::vector<Vec3> wireContactCandidates;
    splitCandidates.reserve(input.wires.size()*2 + input.feeds.size() + input.wires.size());
    wireContactCandidates.reserve(input.wires.size()*2 + input.wires.size());
    for (const auto &w : input.wires)
    {
        splitCandidates.push_back(w.aM); splitCandidates.push_back(w.bM);
        wireContactCandidates.push_back(w.aM); wireContactCandidates.push_back(w.bM);
    }
    for (const auto &feed : input.feeds) splitCandidates.push_back(feed.positionM);

    // Do not silently miss a true interior/interior wire contact. Endpoint-on-segment contacts
    // are already covered by the endpoint candidates above; this additional pass detects two
    // centerlines that cross/touch away from their endpoints and forces both spans to be split at
    // the common electrical node. A resulting T/Y/X branch is then reported explicitly below
    // so every true centerline contact becomes an explicit graph node for later branch handling.
    for(std::size_t i=0;i<input.wires.size();++i)for(std::size_t j=i+1;j<input.wires.size();++j)
    {
        double si=0.0,tj=0.0;Vec3 contact{};
        const auto &a=input.wires[i],&b=input.wires[j];
        if(segmentSegmentDistance3(a.aM,a.bM,b.aM,b.bM,&si,&tj,&contact)<=mergeTol)
        {
            const bool interiorI=si>1e-9&&si<1.0-1e-9;
            const bool interiorJ=tj>1e-9&&tj<1.0-1e-9;
            if(interiorI&&interiorJ)
            {
                splitCandidates.push_back(contact);
                wireContactCandidates.push_back(contact);
            }
        }
    }

    // Identify the geometric points that will become true degree>2 conductor nodes before
    // meshing. A wire contributes two arms when the candidate lies in its interior and one arm
    // when it lies on an endpoint. This correctly distinguishes a simple end-to-end connection
    // (degree 2) from endpoint-on-span T nodes (degree 3) and interior/interior X nodes (degree 4).
    std::vector<Vec3> uniqueContactCandidates;
    for(const auto &p : wireContactCandidates)
    {
        bool duplicate=false;
        for(const auto &q : uniqueContactCandidates) if(norm(p-q)<=mergeTol){duplicate=true;break;}
        if(!duplicate) uniqueContactCandidates.push_back(p);
    }
    std::vector<Vec3> branchPoints;
    for(const auto &p : uniqueContactCandidates)
    {
        int armCount=0;
        for(const auto &w : input.wires)
        {
            double t=0.0;
            if(pointSegmentDistance3(p,w.aM,w.bM,&t)>splitTol) continue;
            const bool interior=t>1e-8&&t<1.0-1e-8;
            armCount += interior ? 2 : 1;
        }
        if(armCount>2) branchPoints.push_back(p);
    }
    const int junctionRefine=std::clamp(input.junctionLocalSubdivisions,1,8);
    int junctionExtraPulseCount=0;

    auto isBranchPoint=[&](const Vec3 &p){
        for(const auto &q:branchPoints) if(norm(p-q)<=splitTol) return true;
        return false;
    };

    for (int wi = 0; wi < static_cast<int>(input.wires.size()); ++wi)
    {
        const auto &w = input.wires[static_cast<std::size_t>(wi)];
        const Vec3 ab = w.bM - w.aM;
        std::vector<double> cuts{0.0, 1.0};
        for (const auto &p : splitCandidates)
        {
            double t = 0.0;
            if (pointSegmentDistance3(p, w.aM, w.bM, &t) <= splitTol && t > 1e-9 && t < 1.0-1e-9)
                cuts.push_back(t);
        }
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end(), [](double a,double b){return std::abs(a-b)<1e-10;}), cuts.end());
        const double radiusTarget = std::clamp(input.maxSegmentLengthRadiusFactor, 2.0, 50.0) * std::max(w.radiusM, 1e-12);
        const double localTarget = std::min(wavelengthTarget, radiusTarget);
        for (std::size_t ci = 0; ci + 1 < cuts.size(); ++ci)
        {
            const Vec3 a = w.aM + ab * cuts[ci];
            const Vec3 b = w.aM + ab * cuts[ci+1];
            const double spanLength = norm(b-a);
            if (spanLength <= 1e-12) continue;
            const int subdivisions = std::max(1, static_cast<int>(std::ceil(spanLength / localTarget)));
            std::vector<double> localCuts;
            localCuts.reserve(static_cast<std::size_t>(subdivisions + 2*junctionRefine));
            for(int si=0;si<=subdivisions;++si) localCuts.push_back(double(si)/double(subdivisions));
            const bool refineStart=junctionRefine>1&&isBranchPoint(a);
            const bool refineEnd=junctionRefine>1&&isBranchPoint(b);
            if(refineStart)
                for(int j=1;j<junctionRefine;++j) localCuts.push_back((1.0/double(subdivisions))*double(j)/double(junctionRefine));
            if(refineEnd)
                for(int j=1;j<junctionRefine;++j) localCuts.push_back(1.0-(1.0/double(subdivisions))*double(j)/double(junctionRefine));
            std::sort(localCuts.begin(),localCuts.end());
            localCuts.erase(std::unique(localCuts.begin(),localCuts.end(),[](double x,double y){return std::abs(x-y)<1e-12;}),localCuts.end());
            junctionExtraPulseCount += static_cast<int>(localCuts.size()) - (subdivisions + 1);
            int previous = addNode(a);
            for(std::size_t li=1;li<localCuts.size();++li)
            {
                const double u=localCuts[li];
                const int next = addNode(a + (b-a)*u);
                addEdge(previous, next, w.radiusM, wi);
                previous = next;
            }
        }
    }
    if (edges.empty()) { out.error = "Meshing produced no wire segments"; return out; }
    const int maxUnknowns = std::clamp(input.maxUnknowns, 16, 1200);
    if (static_cast<int>(edges.size()) > maxUnknowns)
    {
        out.error = "Adaptive thin-wire mesh exceeds maxUnknowns; increase the limit, use a thicker equivalent wire radius, lower segments/wavelength, or simplify the geometry";
        return out;
    }

    // Build connected components. Degree-2 components keep the historical continuous-path
    // orientation so existing dipole/loop results remain numerically comparable. Branched
    // components are traversed as graphs; their local T/Y/X nodes are handled later by KCL
    // constraints rather than rejected.
    std::vector<int> nodeComponent(nodes.size(), -1);
    std::vector<NetworkComponent> components;
    for (int seed = 0; seed < static_cast<int>(nodes.size()); ++seed)
    {
        if (nodes[static_cast<std::size_t>(seed)].edges.empty() || nodeComponent[static_cast<std::size_t>(seed)] >= 0) continue;
        const int componentIndex = static_cast<int>(components.size());
        std::vector<int> stack{seed}, compNodes;
        nodeComponent[static_cast<std::size_t>(seed)] = componentIndex;
        while (!stack.empty())
        {
            const int node = stack.back(); stack.pop_back(); compNodes.push_back(node);
            for (int ei : nodes[static_cast<std::size_t>(node)].edges)
            {
                const auto &e = edges[static_cast<std::size_t>(ei)];
                const int other = e.n0 == node ? e.n1 : e.n0;
                if (nodeComponent[static_cast<std::size_t>(other)] < 0)
                { nodeComponent[static_cast<std::size_t>(other)] = componentIndex; stack.push_back(other); }
            }
        }

        std::vector<int> endpoints;
        bool branched = false;
        for (int node : compNodes)
        {
            const std::size_t degree = nodes[static_cast<std::size_t>(node)].edges.size();
            if (degree == 1) endpoints.push_back(node);
            if (degree > 2) branched = true;
        }

        NetworkComponent comp;
        comp.nodes = compNodes;
        if (!branched)
        {
            if (!(endpoints.size() == 2 || endpoints.empty()))
            { out.error = "A non-branched wire component is neither an open path nor a closed loop"; return out; }
            comp.closed = endpoints.empty();
            int start = -1;
            if (!comp.closed)
                start = vecLexLess(nodes[static_cast<std::size_t>(endpoints[0])].p, nodes[static_cast<std::size_t>(endpoints[1])].p) ? endpoints[0] : endpoints[1];
            else
            {
                start = compNodes.front();
                for (int node : compNodes) if (vecLexLess(nodes[static_cast<std::size_t>(node)].p, nodes[static_cast<std::size_t>(start)].p)) start = node;
            }

            int current = start, previousEdge = -1;
            double pathS = 0.0;
            std::vector<int> orderedNodes;
            orderedNodes.push_back(current);
            for (std::size_t guard = 0; guard <= edges.size()+1; ++guard)
            {
                int nextEdge = -1;
                const auto &inc = nodes[static_cast<std::size_t>(current)].edges;
                if (previousEdge < 0 && comp.closed && inc.size() == 2)
                {
                    const auto &e0 = edges[static_cast<std::size_t>(inc[0])];
                    const auto &e1 = edges[static_cast<std::size_t>(inc[1])];
                    const int o0 = e0.n0 == current ? e0.n1 : e0.n0;
                    const int o1 = e1.n0 == current ? e1.n1 : e1.n0;
                    nextEdge = vecLexLess(nodes[static_cast<std::size_t>(o0)].p, nodes[static_cast<std::size_t>(o1)].p) ? inc[0] : inc[1];
                }
                else
                    for (int ei : inc) if (ei != previousEdge) { nextEdge = ei; break; }
                if (nextEdge < 0) break;
                const auto &e = edges[static_cast<std::size_t>(nextEdge)];
                const int nextNode = e.n0 == current ? e.n1 : e.n0;
                const Vec3 p0 = nodes[static_cast<std::size_t>(current)].p;
                const Vec3 p1 = nodes[static_cast<std::size_t>(nextNode)].p;
                const double L = norm(p1-p0);
                comp.edges.push_back(OrientedNetworkEdge{nextEdge,current,nextNode,p0,p1,normalized(p1-p0),L,e.radiusM,componentIndex,pathS});
                pathS += L;
                previousEdge = nextEdge;
                current = nextNode;
                if (comp.closed && current == start) break;
                orderedNodes.push_back(current);
            }
            const std::size_t expectedEdges = comp.closed ? orderedNodes.size() : (orderedNodes.size() > 0 ? orderedNodes.size()-1 : 0);
            if (comp.edges.size() != expectedEdges)
            { out.error = "Failed to order a connected degree-2 wire component into a continuous path"; return out; }
        }
        else
        {
            // A branch component cannot be flattened into one path. Traverse every mesh edge
            // exactly once, orienting tree edges away from a deterministic root. pathStartM is
            // then a graph-distance-like display abscissa; it has no role in the EM equations.
            comp.closed = endpoints.empty();
            int root = !endpoints.empty() ? endpoints.front() : compNodes.front();
            for (int node : (!endpoints.empty() ? endpoints : compNodes))
                if (vecLexLess(nodes[static_cast<std::size_t>(node)].p, nodes[static_cast<std::size_t>(root)].p)) root = node;

            std::vector<bool> edgeSeen(edges.size(), false), nodeSeen(nodes.size(), false);
            std::vector<double> nodeDistance(nodes.size(), std::numeric_limits<double>::infinity());
            nodeDistance[static_cast<std::size_t>(root)] = 0.0;
            std::vector<int> walk{root};
            while (!walk.empty())
            {
                const int current = walk.back(); walk.pop_back();
                if (nodeSeen[static_cast<std::size_t>(current)]) continue;
                nodeSeen[static_cast<std::size_t>(current)] = true;
                auto incident = nodes[static_cast<std::size_t>(current)].edges;
                std::sort(incident.begin(), incident.end(), [&](int ea, int eb){
                    const auto &aEdge=edges[static_cast<std::size_t>(ea)], &bEdge=edges[static_cast<std::size_t>(eb)];
                    const int oa=aEdge.n0==current?aEdge.n1:aEdge.n0, ob=bEdge.n0==current?bEdge.n1:bEdge.n0;
                    return vecLexLess(nodes[static_cast<std::size_t>(oa)].p,nodes[static_cast<std::size_t>(ob)].p);
                });
                for (int ei : incident)
                {
                    if (edgeSeen[static_cast<std::size_t>(ei)]) continue;
                    const auto &e = edges[static_cast<std::size_t>(ei)];
                    const int other = e.n0 == current ? e.n1 : e.n0;
                    int from = current, to = other;
                    if (nodeSeen[static_cast<std::size_t>(other)] &&
                        vecLexLess(nodes[static_cast<std::size_t>(other)].p, nodes[static_cast<std::size_t>(current)].p))
                    { from = other; to = current; }
                    const Vec3 p0=nodes[static_cast<std::size_t>(from)].p, p1=nodes[static_cast<std::size_t>(to)].p;
                    const double L=norm(p1-p0);
                    double pathS=nodeDistance[static_cast<std::size_t>(from)];
                    if (!std::isfinite(pathS)) pathS=0.0;
                    comp.edges.push_back(OrientedNetworkEdge{ei,from,to,p0,p1,normalized(p1-p0),L,e.radiusM,componentIndex,pathS});
                    edgeSeen[static_cast<std::size_t>(ei)] = true;
                    if (!std::isfinite(nodeDistance[static_cast<std::size_t>(to)]))
                        nodeDistance[static_cast<std::size_t>(to)] = pathS + L;
                    if (!nodeSeen[static_cast<std::size_t>(other)]) walk.push_back(other);
                }
            }
            std::size_t expectedEdges=0;
            for (const auto &e:edges) if (nodeComponent[static_cast<std::size_t>(e.n0)]==componentIndex) ++expectedEdges;
            if (comp.edges.size()!=expectedEdges)
            { out.error="Failed to traverse every edge of a branched wire component"; return out; }
        }
        components.push_back(std::move(comp));
    }
    if (components.empty()) { out.error = "No connected wire components were found"; return out; }

    std::vector<OrientedNetworkEdge> orientedEdges;
    std::vector<int> edgeToOriented(edges.size(), -1);
    for (const auto &comp : components)
        for (const auto &oe : comp.edges)
        {
            edgeToOriented[static_cast<std::size_t>(oe.edgeIndex)] = static_cast<int>(orientedEdges.size());
            orientedEdges.push_back(oe);
        }
    const int n = static_cast<int>(orientedEdges.size());
    if (n != static_cast<int>(edges.size())) { out.error = "Internal wire graph traversal lost mesh segments"; return out; }
    out.junctionRefinedExtraPulseCount=junctionExtraPulseCount;
    out.minMeshSegmentM=std::numeric_limits<double>::infinity();
    out.maxMeshSegmentM=0.0;
    out.maxMeshSegmentToRadius=0.0;
    for(const auto &oe:orientedEdges)
    {
        out.minMeshSegmentM=std::min(out.minMeshSegmentM,oe.lengthM);
        out.maxMeshSegmentM=std::max(out.maxMeshSegmentM,oe.lengthM);
        out.maxMeshSegmentToRadius=std::max(out.maxMeshSegmentToRadius,oe.lengthM/std::max(oe.radiusM,1e-18));
    }
    if(!std::isfinite(out.minMeshSegmentM))out.minMeshSegmentM=0.0;

    struct BranchBinding
    {
        int node = -1;
        std::vector<std::pair<int,double>> incidence; // +I leaves the junction, -I enters it.
    };
    std::vector<BranchBinding> branchBindings;
    for (int ni=0; ni<static_cast<int>(nodes.size()); ++ni)
    {
        const auto &node=nodes[static_cast<std::size_t>(ni)];
        if (node.edges.size()<=2) continue;
        BranchBinding b; b.node=ni;
        for (int rawEdge:node.edges)
        {
            const int oi=edgeToOriented[static_cast<std::size_t>(rawEdge)];
            if (oi<0) { out.error="Branch-junction edge mapping failed"; return out; }
            const auto &oe=orientedEdges[static_cast<std::size_t>(oi)];
            const double sign=oe.fromNode==ni?+1.0:-1.0;
            b.incidence.push_back({oi,sign});
        }
        branchBindings.push_back(std::move(b));
    }
    out.branchedJunctionCount=static_cast<int>(branchBindings.size());
    out.branchedArmCount=0;
    for(const auto &b:branchBindings)out.branchedArmCount+=static_cast<int>(b.incidence.size());

    std::vector<std::complex<double>> rhs(static_cast<std::size_t>(n), {0.0,0.0});
    struct FeedBinding { int node=-1; int e0=-1; int e1=-1; double s0=1.0; double s1=1.0; };
    std::vector<FeedBinding> feedBindings(input.feeds.size());
    std::vector<bool> nodeHasFeed(nodes.size(), false);
    bool anyExcitation = false;
    for (std::size_t fi = 0; fi < input.feeds.size(); ++fi)
    {
        const auto &feed = input.feeds[fi];
        int bestNode = -1; double bestD = std::numeric_limits<double>::infinity();
        for (int ni = 0; ni < static_cast<int>(nodes.size()); ++ni)
        {
            const double d = norm(nodes[static_cast<std::size_t>(ni)].p-feed.positionM);
            if (d < bestD) { bestD=d; bestNode=ni; }
        }
        if (bestNode < 0 || bestD > std::max(splitTol*4.0, wavelengthTarget*1e-5))
        { out.error = "A feed is not located on a meshed conductor node"; return out; }
        if (nodes[static_cast<std::size_t>(bestNode)].edges.size() != 2)
        { out.error = "A delta-gap feed must lie on an interior degree-2 conductor node. Branched conductor junctions are supported, but placing the voltage gap directly on a T/Y/X node is still ambiguous; move the feed a short distance onto one arm."; return out; }
        if (nodeHasFeed[static_cast<std::size_t>(bestNode)]) { out.error = "Multiple feeds on the same electrical node are not supported"; return out; }
        nodeHasFeed[static_cast<std::size_t>(bestNode)] = true;
        const int e0 = edgeToOriented[static_cast<std::size_t>(nodes[static_cast<std::size_t>(bestNode)].edges[0])];
        const int e1 = edgeToOriented[static_cast<std::size_t>(nodes[static_cast<std::size_t>(bestNode)].edges[1])];
        if (e0 < 0 || e1 < 0) { out.error = "Feed edge mapping failed"; return out; }

        const auto &oe0=orientedEdges[static_cast<std::size_t>(e0)], &oe1=orientedEdges[static_cast<std::size_t>(e1)];
        const int other0=oe0.fromNode==bestNode?oe0.toNode:oe0.fromNode;
        const int other1=oe1.fromNode==bestNode?oe1.toNode:oe1.fromNode;
        auto signForDirection=[&](const OrientedNetworkEdge &oe,int from,int to){
            return (oe.fromNode==from && oe.toNode==to) ? 1.0 : -1.0;
        };
        double s0=1.0,s1=1.0;
        if (vecLexLess(nodes[static_cast<std::size_t>(other0)].p,nodes[static_cast<std::size_t>(other1)].p))
        {
            s0=signForDirection(oe0,other0,bestNode);
            s1=signForDirection(oe1,bestNode,other1);
        }
        else
        {
            s1=signForDirection(oe1,other1,bestNode);
            s0=signForDirection(oe0,bestNode,other0);
        }
        feedBindings[fi] = FeedBinding{bestNode,e0,e1,s0,s1};
        rhs[static_cast<std::size_t>(e0)] += 0.5*s0*feed.voltageV/std::max(oe0.lengthM,1e-18);
        rhs[static_cast<std::size_t>(e1)] += 0.5*s1*feed.voltageV/std::max(oe1.lengthM,1e-18);
        if (std::abs(feed.voltageV) > 1e-18) anyExcitation = true;
    }
    if (!anyExcitation) { out.error = "At least one feed voltage must be non-zero"; return out; }

    std::vector<std::complex<double>> current(static_cast<std::size_t>(n),{0.0,0.0});
    std::vector<std::complex<double>> currentP0(static_cast<std::size_t>(n),{0.0,0.0});
    std::vector<std::complex<double>> currentP1(static_cast<std::size_t>(n),{0.0,0.0});
    SolveStats stats;
    int totalUnknowns=0;
    int reducedWireDofs=n;
    int constraintRank=0;

    if(input.currentBasisTreatment==WireCurrentBasisTreatment::LinearRooftopCharge)
    {
        // True node-centred piecewise-linear current functions. An open endpoint has no
        // associated basis function, so I(end)=0 is satisfied geometrically. A degree-2 node
        // contributes one rooftop function and a degree-d branch contributes d-1 independent
        // KCL-safe functions. The EFIE is tested Galerkin-style over the same local linear
        // functions. Self and near support pairs use radius-aware composite Galerkin quadrature
        // on both source and observation spans; far pairs retain a compact 4x4 Gauss rule.
        const RooftopBasisSet rooftop=buildRooftopBasis(nodes,orientedEdges,edgeToOriented);
        const int nr=static_cast<int>(rooftop.functions.size());
        if(nr<=0){out.error="Linear rooftop current basis produced no current degrees of freedom";return out;}
        if(nr>maxUnknowns){out.error="Linear rooftop current basis exceeds maxUnknowns; coarsen the wire mesh, increase the limit, or use the pulse basis";return out;}
        totalUnknowns=nr;reducedWireDofs=nr;constraintRank=rooftop.branchKclCount;
        out.linearRooftopBasisUsed=true;
        out.reducedJunctionBasisUsed=rooftop.branchKclCount>0;
        out.junctionConstraintCount=rooftop.branchKclCount;
        out.redundantJunctionConstraintCount=0;
        out.currentConstraintCount=rooftop.openEndCount+rooftop.ordinaryContinuityCount+rooftop.branchKclCount;
        out.ordinaryContinuityConstraintCount=rooftop.ordinaryContinuityCount;
        out.openEndConstraintCount=rooftop.openEndCount;

        std::vector<std::vector<std::complex<double>>> A(static_cast<std::size_t>(nr),
            std::vector<std::complex<double>>(static_cast<std::size_t>(nr),{0.0,0.0}));
        std::vector<std::complex<double>> rhsSolve(static_cast<std::size_t>(nr),{0.0,0.0});

        for(int a=0;a<nr;++a)
        {
            const auto &test=rooftop.functions[static_cast<std::size_t>(a)];
            // Delta-gap weak excitation: only basis functions peaking at the feed node are
            // nonzero on the infinitesimal gap. Project their node current onto the deterministic
            // virtual path used by the feed-current extractor.
            for(std::size_t fi=0;fi<input.feeds.size();++fi)
            {
                const auto &bind=feedBindings[fi];
                if(test.nodeIndex!=bind.node)continue;
                double c0=0.0,c1=0.0;
                for(const auto &sp:test.supports)
                {
                    if(sp.edgeIndex==bind.e0)c0+=sp.endpointCoefficient;
                    if(sp.edgeIndex==bind.e1)c1+=sp.endpointCoefficient;
                }
                const double portProjection=0.5*(bind.s0*c0+bind.s1*c1);
                rhsSolve[static_cast<std::size_t>(a)]+=input.feeds[fi].voltageV*portProjection;
            }

            for(int b=0;b<nr;++b)
            {
                const auto &trial=rooftop.functions[static_cast<std::size_t>(b)];
                std::complex<double> zab{0.0,0.0};
                for(const auto &so:test.supports)
                {
                    const auto &obs=orientedEdges[static_cast<std::size_t>(so.edgeIndex)];
                    for(const auto &ss:trial.supports)
                    {
                        const auto &src=orientedEdges[static_cast<std::size_t>(ss.edgeIndex)];
                        const double aEff=0.5*(std::max(obs.radiusM,1e-12)+std::max(src.radiusM,1e-12));
                        const auto pair=integrateProjectedRooftopPair(obs.p0,obs.p1,obs.tangent,so.peakAtP0,
                                                                     src.p0,src.p1,src.tangent,ss.peakAtP0,
                                                                     aEff,k,omega);
                        zab+=so.endpointCoefficient*ss.endpointCoefficient*pair;
                    }
                }
                A[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)]=zab;
            }
        }

        std::vector<std::complex<double>> solution;
        if(!solveComplex(A,rhsSolve,solution,stats))
        {out.error="Generalized thin-wire MoM / linear rooftop-charge matrix is singular or ill-conditioned";return out;}
        for(int a=0;a<nr;++a)
        {
            const auto qa=solution[static_cast<std::size_t>(a)];
            for(const auto &sp:rooftop.functions[static_cast<std::size_t>(a)].supports)
            {
                if(sp.peakAtP0)currentP0[static_cast<std::size_t>(sp.edgeIndex)]+=sp.endpointCoefficient*qa;
                else currentP1[static_cast<std::size_t>(sp.edgeIndex)]+=sp.endpointCoefficient*qa;
            }
        }
        for(int i=0;i<n;++i)current[static_cast<std::size_t>(i)]=0.5*(currentP0[static_cast<std::size_t>(i)]+currentP1[static_cast<std::size_t>(i)]);
        double maxResidual=0.0,maxRhs=0.0;
        for(int i=0;i<nr;++i)
        {
            std::complex<double> ax{0.0,0.0};
            for(int q=0;q<nr;++q)ax+=A[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]*solution[static_cast<std::size_t>(q)];
            maxResidual=std::max(maxResidual,std::abs(ax-rhsSolve[static_cast<std::size_t>(i)]));
            maxRhs=std::max(maxRhs,std::abs(rhsSolve[static_cast<std::size_t>(i)]));
        }
        out.residualRelative=maxResidual/std::max(maxRhs,1e-30);
    }
    else
    {
        std::vector<std::vector<std::complex<double>>> Z(static_cast<std::size_t>(n), std::vector<std::complex<double>>(static_cast<std::size_t>(n), {0.0,0.0}));
        for (int m = 0; m < n; ++m)
        {
            const auto &obs = orientedEdges[static_cast<std::size_t>(m)];
            const Vec3 ro = (obs.p0+obs.p1)*0.5;
            for (int q = 0; q < n; ++q)
            {
                const auto &src = orientedEdges[static_cast<std::size_t>(q)];
                const double aEff = 0.5*(std::max(obs.radiusM,1e-12)+std::max(src.radiusM,1e-12));
                Z[static_cast<std::size_t>(m)][static_cast<std::size_t>(q)] =
                    integrateProjectedPulseSegment(ro,obs.tangent,src.p0,src.p1,src.tangent,aEff,k,omega);
            }
        }

        // Historical pulse-current T/Y/X treatment from 5.14.0.
        const int branchCount=static_cast<int>(branchBindings.size());
        std::vector<std::vector<double>> branchMatrix(static_cast<std::size_t>(branchCount),
                                                       std::vector<double>(static_cast<std::size_t>(n),0.0));
        for(int bi=0;bi<branchCount;++bi)
            for(const auto &[edgeIndex,sign]:branchBindings[static_cast<std::size_t>(bi)].incidence)
                branchMatrix[static_cast<std::size_t>(bi)][static_cast<std::size_t>(edgeIndex)] += sign;

        NullspaceBasis junctionBasis;
        if(!buildConstraintNullspace(branchMatrix,n,junctionBasis))
        {out.error="Failed to build an independent T/Y/X junction-current basis";return out;}
        constraintRank=junctionBasis.rank;
        reducedWireDofs=n-constraintRank;
        out.junctionConstraintCount=constraintRank;
        out.redundantJunctionConstraintCount=branchCount-constraintRank;
        out.reducedJunctionBasisUsed=(input.junctionTreatment==WireJunctionTreatment::ReducedBasis && constraintRank>0);
        out.currentConstraintCount=constraintRank;

        std::vector<std::vector<std::complex<double>>> A;
        std::vector<std::complex<double>> rhsSolve;
        if(out.reducedJunctionBasisUsed)
        {
            totalUnknowns=reducedWireDofs;
            if(totalUnknowns>maxUnknowns){out.error="Reduced junction-basis MoM exceeds maxUnknowns; increase the limit, coarsen the mesh, or simplify the geometry";return out;}
            A.assign(static_cast<std::size_t>(totalUnknowns),std::vector<std::complex<double>>(static_cast<std::size_t>(totalUnknowns),{0.0,0.0}));
            rhsSolve.assign(static_cast<std::size_t>(totalUnknowns),{0.0,0.0});
            for(int a=0;a<totalUnknowns;++a)
            {
                for(int i=0;i<n;++i)rhsSolve[static_cast<std::size_t>(a)]+=junctionBasis.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)]*rhs[static_cast<std::size_t>(i)];
                for(int b=0;b<totalUnknowns;++b)
                {
                    std::complex<double> v{0.0,0.0};
                    for(int i=0;i<n;++i)
                    {
                        const double tia=junctionBasis.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)];if(std::abs(tia)<1e-18)continue;
                        for(int q=0;q<n;++q){const double tqb=junctionBasis.t[static_cast<std::size_t>(q)][static_cast<std::size_t>(b)];if(std::abs(tqb)<1e-18)continue;v+=tia*Z[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]*tqb;}
                    }
                    A[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)]=v;
                }
            }
        }
        else
        {
            totalUnknowns=n+constraintRank;
            if(totalUnknowns>maxUnknowns){out.error="Thin-wire mesh plus independent branch-junction constraints exceeds maxUnknowns; increase the limit, coarsen the mesh, or simplify the branched geometry";return out;}
            A.assign(static_cast<std::size_t>(totalUnknowns),std::vector<std::complex<double>>(static_cast<std::size_t>(totalUnknowns),{0.0,0.0}));
            rhsSolve.assign(static_cast<std::size_t>(totalUnknowns),{0.0,0.0});
            for(int i=0;i<n;++i){rhsSolve[static_cast<std::size_t>(i)]=rhs[static_cast<std::size_t>(i)];for(int q=0;q<n;++q)A[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]=Z[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)];}
            double junctionScale=0.0;for(int i=0;i<n;++i)junctionScale=std::max(junctionScale,std::abs(Z[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)]));if(!(junctionScale>1e-18)||!std::isfinite(junctionScale))junctionScale=1.0;
            for(int ri=0;ri<constraintRank;++ri)
            {
                const int bi=junctionBasis.independentRows[static_cast<std::size_t>(ri)];const int row=n+ri;
                for(const auto &[edgeIndex,sign]:branchBindings[static_cast<std::size_t>(bi)].incidence){const std::complex<double> c=junctionScale*sign;A[static_cast<std::size_t>(row)][static_cast<std::size_t>(edgeIndex)]+=c;A[static_cast<std::size_t>(edgeIndex)][static_cast<std::size_t>(row)]+=c;}
            }
        }

        std::vector<std::complex<double>> solution;
        if(!solveComplex(A,rhsSolve,solution,stats))
        {out.error=out.reducedJunctionBasisUsed?"Generalized thin-wire MoM / reduced T/Y/X junction basis is singular or ill-conditioned":"Generalized thin-wire MoM / branch-junction saddle matrix is singular or ill-conditioned";return out;}
        if(out.reducedJunctionBasisUsed)
            for(int i=0;i<n;++i)for(int a=0;a<totalUnknowns;++a)current[static_cast<std::size_t>(i)]+=junctionBasis.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)]*solution[static_cast<std::size_t>(a)];
        else std::copy(solution.begin(),solution.begin()+n,current.begin());
        currentP0=current;currentP1=current;
        double maxResidual=0.0,maxRhs=0.0;
        for(int i=0;i<totalUnknowns;++i){std::complex<double> ax{0.0,0.0};for(int q=0;q<totalUnknowns;++q)ax+=A[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]*solution[static_cast<std::size_t>(q)];maxResidual=std::max(maxResidual,std::abs(ax-rhsSolve[static_cast<std::size_t>(i)]));maxRhs=std::max(maxRhs,std::abs(rhsSolve[static_cast<std::size_t>(i)]));}
        out.residualRelative=maxResidual/std::max(maxRhs,1e-30);
    }
    out.minPivotAbs=stats.minPivot;out.maxPivotAbs=stats.maxPivot;

    out.maxJunctionCurrentDiscontinuity = 0.0;
    out.maxBranchKclResidualA=0.0;
    out.maxOpenEndCurrentA=0.0;
    double peakCurrent = 0.0;
    for (int i=0;i<n;++i)
    {
        peakCurrent=std::max(peakCurrent,std::abs(current[static_cast<std::size_t>(i)]));
        peakCurrent=std::max(peakCurrent,std::abs(currentP0[static_cast<std::size_t>(i)]));
        peakCurrent=std::max(peakCurrent,std::abs(currentP1[static_cast<std::size_t>(i)]));
    }
    out.chargeNodes.reserve(nodes.size());
    out.netContinuityChargeC={0.0,0.0};
    const std::complex<double> jomega(0.0,omega);
    for (int ni=0;ni<static_cast<int>(nodes.size());++ni)
    {
        const auto &node=nodes[static_cast<std::size_t>(ni)];
        if(node.edges.empty())continue;
        std::complex<double> kcl{0.0,0.0};
        double controlLength=0.0;
        for(int rawEdge:node.edges)
        {
            const int oi=edgeToOriented[static_cast<std::size_t>(rawEdge)];
            if(oi<0)continue;
            const auto &oe=orientedEdges[static_cast<std::size_t>(oi)];
            const bool atP0=oe.fromNode==ni;
            const double sign=atP0?+1.0:-1.0;
            const auto endpointCurrent=atP0?currentP0[static_cast<std::size_t>(oi)]:currentP1[static_cast<std::size_t>(oi)];
            kcl+=sign*endpointCurrent;
            controlLength+=0.5*oe.lengthM;
        }
        if(node.edges.size()==1)out.maxOpenEndCurrentA=std::max(out.maxOpenEndCurrentA,std::abs(kcl));
        if(node.edges.size()>=2)
        {
            const double rel=std::abs(kcl)/std::max(peakCurrent,1e-18);
            out.maxJunctionCurrentDiscontinuity=std::max(out.maxJunctionCurrentDiscontinuity,rel);
            if(node.edges.size()>2)out.maxBranchKclResidualA=std::max(out.maxBranchKclResidualA,std::abs(kcl));
        }
        // Pulse current has distributional charge at current jumps; rooftop current moves that
        // charge onto the span slope and drives this nodal residual to numerical zero.
        const std::complex<double> qNode=std::abs(jomega)>1e-30?-kcl/jomega:std::complex<double>{0.0,0.0};
        const std::complex<double> lambdaEq=controlLength>1e-18?qNode/controlLength:std::complex<double>{0.0,0.0};
        WireNetworkChargeNode cs;cs.positionM=node.p;cs.componentIndex=nodeComponent[static_cast<std::size_t>(ni)];
        cs.degree=static_cast<int>(node.edges.size());cs.openEndpoint=cs.degree==1;cs.branchedJunction=cs.degree>2;
        cs.lumpedContinuityChargeC=qNode;cs.equivalentLineChargeCpm=lambdaEq;out.chargeNodes.push_back(cs);
        if(!out.linearRooftopBasisUsed)out.netContinuityChargeC+=qNode;
    }
    out.maxBranchKclRelative=out.maxBranchKclResidualA/std::max(peakCurrent,1e-18);
    out.maxOpenEndCurrentRelative=out.maxOpenEndCurrentA/std::max(peakCurrent,1e-18);

    out.meshSegments.reserve(orientedEdges.size());
    for (int i=0;i<n;++i)
    {
        const auto &e=orientedEdges[static_cast<std::size_t>(i)];
        const int sourceWire=edges[static_cast<std::size_t>(e.edgeIndex)].wireIndex;
        const auto &w=input.wires[static_cast<std::size_t>(sourceWire)];
        const Vec3 ab=w.bM-w.aM;const double ab2=dot(ab,ab);const double wireLength=norm(ab);
        const Vec3 center=(e.p0+e.p1)*0.5;
        const double param=ab2>1e-30?std::clamp(dot(center-w.aM,ab)/ab2,0.0,1.0):0.0;
        WireNetworkMeshSegment seg;seg.p0M=e.p0;seg.p1M=e.p1;seg.centerM=center;seg.tangent=e.tangent;
        seg.lengthM=e.lengthM;seg.radiusM=e.radiusM;seg.componentIndex=e.componentIndex;seg.sourceWireIndex=sourceWire;
        seg.pathCenterM=e.pathStartM+0.5*e.lengthM;seg.sourceWirePathCenterM=param*wireLength;
        seg.currentA=current[static_cast<std::size_t>(i)];seg.currentAtP0A=currentP0[static_cast<std::size_t>(i)];seg.currentAtP1A=currentP1[static_cast<std::size_t>(i)];
        if(out.linearRooftopBasisUsed && e.lengthM>1e-18 && std::abs(jomega)>1e-30)
            seg.lineChargeDensityCpm=-(seg.currentAtP1A-seg.currentAtP0A)/(jomega*e.lengthM);
        out.meshSegments.push_back(seg);
    }

    if(out.linearRooftopBasisUsed)
    {
        out.netContinuityChargeC={0.0,0.0};
        for(auto &seg:out.meshSegments)
        {
            out.maxLineChargeDensityCpm=std::max(out.maxLineChargeDensityCpm,std::abs(seg.lineChargeDensityCpm));
            out.netContinuityChargeC+=seg.lineChargeDensityCpm*seg.lengthM;
        }
    }
    else
    {
        // For the historical pulse basis, reconstruct a finite-difference distributed charge
        // along each original CAD span. Nodal jump charge above remains available separately.
        for(int wi=0;wi<static_cast<int>(input.wires.size());++wi)
        {
            std::vector<int> ids;for(int i=0;i<n;++i)if(out.meshSegments[static_cast<std::size_t>(i)].sourceWireIndex==wi)ids.push_back(i);
            std::sort(ids.begin(),ids.end(),[&](int a,int b){return out.meshSegments[static_cast<std::size_t>(a)].sourceWirePathCenterM<out.meshSegments[static_cast<std::size_t>(b)].sourceWirePathCenterM;});
            if(ids.size()<2)continue;
            const Vec3 sourceTangent=normalized(input.wires[static_cast<std::size_t>(wi)].bM-input.wires[static_cast<std::size_t>(wi)].aM);
            std::vector<std::complex<double>> aligned(ids.size());std::vector<double> pos(ids.size());
            for(std::size_t ii=0;ii<ids.size();++ii){const auto &seg=out.meshSegments[static_cast<std::size_t>(ids[ii])];aligned[ii]=(dot(seg.tangent,sourceTangent)>=0?1.0:-1.0)*seg.currentA;pos[ii]=seg.sourceWirePathCenterM;}
            for(std::size_t ii=0;ii<ids.size();++ii)
            {
                std::complex<double> dIds{0.0,0.0};
                if(ii==0)dIds=(aligned[1]-aligned[0])/std::max(pos[1]-pos[0],1e-18);
                else if(ii+1==ids.size())dIds=(aligned[ii]-aligned[ii-1])/std::max(pos[ii]-pos[ii-1],1e-18);
                else dIds=(aligned[ii+1]-aligned[ii-1])/std::max(pos[ii+1]-pos[ii-1],1e-18);
                const auto lambda=std::abs(jomega)>1e-30?-dIds/jomega:std::complex<double>{0.0,0.0};
                out.meshSegments[static_cast<std::size_t>(ids[ii])].lineChargeDensityCpm=lambda;
                out.maxLineChargeDensityCpm=std::max(out.maxLineChargeDensityCpm,std::abs(lambda));
            }
        }
    }

    out.feeds.reserve(input.feeds.size());
    for (std::size_t fi=0;fi<input.feeds.size();++fi)
    {
        const auto &feed=input.feeds[fi]; const auto &bind=feedBindings[fi];
        auto currentAtNode=[&](int edgeIndex){const auto &oe=orientedEdges[static_cast<std::size_t>(edgeIndex)];return oe.fromNode==bind.node?currentP0[static_cast<std::size_t>(edgeIndex)]:currentP1[static_cast<std::size_t>(edgeIndex)];};
        const auto ifeed=0.5*(bind.s0*currentAtNode(bind.e0)+bind.s1*currentAtNode(bind.e1));
        WireNetworkFeedResult fr; fr.name=feed.name; fr.positionM=feed.positionM; fr.meshNodeIndex=bind.node;
        fr.componentIndex=nodeComponent[static_cast<std::size_t>(bind.node)]; fr.voltageV=feed.voltageV; fr.currentA=ifeed;
        fr.referenceOhm=std::max(feed.referenceOhm,1e-9);
        if(std::abs(ifeed)>1e-18)fr.activeImpedanceOhm=feed.voltageV/ifeed;
        const std::complex<double> z0(fr.referenceOhm,0.0),den=fr.activeImpedanceOhm+z0;
        if(std::abs(den)>1e-18)fr.reflectionCoefficient=(fr.activeImpedanceOhm-z0)/den;
        const double gm=std::abs(fr.reflectionCoefficient);
        fr.returnLossDb=gm>1e-15?-20.0*std::log10(gm):300.0;
        fr.vswr=gm<1.0?(1.0+gm)/std::max(1.0-gm,1e-15):std::numeric_limits<double>::infinity();
        out.acceptedPowerW += 0.5*std::real(feed.voltageV*std::conj(ifeed));
        out.feeds.push_back(fr);
    }

    if (input.computeFarField)
    {
        auto farFieldSquared=[&](const Vec3 &dirIn){
            const Vec3 dir=normalized(dirIn);
            std::array<std::complex<double>,3> F{{{0,0},{0,0},{0,0}}};
            for(int i=0;i<n;++i)
            {
                const auto &e=orientedEdges[static_cast<std::size_t>(i)];
                const Vec3 tr=e.tangent-dir*dot(dir,e.tangent);
                const Vec3 mid=(e.p0+e.p1)*0.5,half=(e.p1-e.p0)*0.5;
                std::complex<double> phaseIntegral{0,0};
                for(int g=0;g<8;++g)for(int sgn:{-1,1})
                {
                    const double xi=sgn*GlX[static_cast<std::size_t>(g)];
                    const Vec3 r=mid+half*xi;
                    const double u=0.5*(xi+1.0);
                    const auto iLocal=out.linearRooftopBasisUsed?((1.0-u)*currentP0[static_cast<std::size_t>(i)]+u*currentP1[static_cast<std::size_t>(i)]):current[static_cast<std::size_t>(i)];
                    phaseIntegral+=GlW[static_cast<std::size_t>(g)]*iLocal*std::exp(std::complex<double>(0,k*dot(dir,r)));
                }
                phaseIntegral*=0.5*e.lengthM;
                F[0]+=tr.x*phaseIntegral;F[1]+=tr.y*phaseIntegral;F[2]+=tr.z*phaseIntegral;
            }
            return std::norm(F[0])+std::norm(F[1])+std::norm(F[2]);
        };

        std::vector<double> azRaw,elRaw;
        const double requestedCutStep=std::clamp(input.farFieldCutStepDeg,0.25,45.0);
        const int azIntervals=std::max(8,static_cast<int>(std::ceil(360.0/requestedCutStep)));
        const int elIntervals=std::max(4,static_cast<int>(std::ceil(180.0/requestedCutStep)));
        for(int i=0;i<=azIntervals;++i){const double deg=360.0*double(i)/double(azIntervals);const double p=deg*Pi/180.0;out.azimuthDeg.push_back(deg);azRaw.push_back(std::sqrt(std::max(0.0,farFieldSquared({std::cos(p),std::sin(p),0}))));}
        for(int i=0;i<=elIntervals;++i){const double deg=180.0*double(i)/double(elIntervals);const double t=deg*Pi/180.0;out.elevationDeg.push_back(deg);elRaw.push_back(std::sqrt(std::max(0.0,farFieldSquared({std::sin(t),0,std::cos(t)}))));}

        const double stepDeg=std::clamp(input.farFieldIntegrationStepDeg,1.0,30.0);
        const double dth=stepDeg*Pi/180.0,dph=stepDeg*Pi/180.0;
        double integ=0.0,maxF2=0.0;
        std::vector<double> sphereRaw;
        for(double th=.5*dth;th<Pi;th+=dth)
        {
            out.farFieldThetaDeg.push_back(th*180.0/Pi);
            bool firstTheta = out.farFieldPhiDeg.empty();
            for(double ph=.5*dph;ph<2*Pi;ph+=dph)
            {
                if(firstTheta) out.farFieldPhiDeg.push_back(ph*180.0/Pi);
                const double f2=farFieldSquared({std::sin(th)*std::cos(ph),std::sin(th)*std::sin(ph),std::cos(th)});
                sphereRaw.push_back(std::sqrt(std::max(0.0,f2)));
                integ+=f2*std::sin(th)*dth*dph;
                if(f2>maxF2)
                {
                    maxF2=f2;
                    out.maxRadiationThetaDeg=th*180.0/Pi;
                    out.maxRadiationPhiDeg=ph*180.0/Pi;
                }
            }
        }
        // Use one common peak for all cuts and the 3D sphere.  Previous versions
        // normalized azimuth/elevation independently, which made a weak cut appear as
        // 0 dB even when the actual antenna maximum pointed outside that plane.
        auto updatePeakFromCut=[&](const std::vector<double>&raw,const std::vector<double>&angles,bool azimuth){
            if(raw.empty()||raw.size()!=angles.size())return;
            const auto it=std::max_element(raw.begin(),raw.end());
            const std::size_t idx=static_cast<std::size_t>(std::distance(raw.begin(),it));
            const double f2=(*it)*(*it);
            if(f2<=maxF2)return;
            maxF2=f2;
            if(azimuth){out.maxRadiationThetaDeg=90.0;out.maxRadiationPhiDeg=std::fmod(angles[idx]+360.0,360.0);}
            else{out.maxRadiationThetaDeg=angles[idx];out.maxRadiationPhiDeg=0.0;}
        };
        updatePeakFromCut(azRaw,out.azimuthDeg,true);
        updatePeakFromCut(elRaw,out.elevationDeg,false);
        const double globalMax=std::sqrt(std::max(0.0,maxF2));
        for(double v:azRaw)out.azimuthNormalizedFarField.push_back(globalMax>0.0?v/globalMax:0.0);
        for(double v:elRaw)out.elevationNormalizedFarField.push_back(globalMax>0.0?v/globalMax:0.0);
        out.farFieldNormalized.reserve(sphereRaw.size());
        for(double v:sphereRaw) out.farFieldNormalized.push_back(globalMax>0.0?v/globalMax:0.0);
        if(integ>1e-30&&maxF2>0)
        {
            out.directivityLinear=4*Pi*maxF2/integ;
            out.directivityDbi=10*std::log10(out.directivityLinear);
            const double eta0=std::sqrt(Mu0/Epsilon0);
            out.radiatedPowerW=eta0*k*k*integ/(32*Pi*Pi);
        }
    }

    out.meshSegmentCount=n;
    out.wireUnknownCount=n;
    out.solvedWireDofCount=out.linearRooftopBasisUsed?reducedWireDofs:(out.reducedJunctionBasisUsed?reducedWireDofs:n);
    out.unknownCount=totalUnknowns;
    out.componentCount=static_cast<int>(components.size());
    bool finiteFeeds=!out.feeds.empty();
    for(const auto &feed:out.feeds) finiteFeeds=finiteFeeds && finiteComplex(feed.currentA) && finiteComplex(feed.activeImpedanceOhm) &&
        finiteComplex(feed.reflectionCoefficient) && finiteScalar(feed.returnLossDb) && (finiteScalar(feed.vswr) || std::isinf(feed.vswr));
    out.valid=finiteFeeds&&finiteScalar(out.residualRelative)&&finiteScalar(out.directivityLinear)&&finiteScalar(out.radiatedPowerW)&&finiteScalar(out.acceptedPowerW);
    if(!out.valid && out.error.empty()) out.error="Wire-network solve produced a non-finite numerical result";
    out.note="Experimental educational generalized thin-wire MoM. The pulse/point-matching path now uses radius-aware composite Gauss quadrature for self and near-neighbour source segments; this resolves the sharp reactive Pocklington kernel on the wire-radius scale and removes the severe Zin oscillation that occurred when a single fixed Gauss panel covered many radii. Far interactions keep the historical 16-point rule. The 5.14 rank-aware T/Y/X reduced-basis or legacy Lagrange KCL treatment remains available. The experimental linear rooftop + charge formulation builds node-centred piecewise-linear current functions: open endpoints have no nodal basis and therefore satisfy I(end)=0, every ordinary degree-2 node contributes one continuous rooftop mode, and a degree-d T/Y/X node contributes d-1 independent KCL-safe modes. The rooftop impedance matrix uses radius-aware composite Galerkin integration for self/near support pairs and a compact 4x4 rule for far pairs; the basis remains experimental and should still be checked against the canonical validation bench. Delta-gap excitation is weakly projected onto the unique degree-2 rooftop mode at the feed. Solved endpoint currents give a direct low-order line-charge reconstruction lambda=-(1/jw)dI/ds on each mesh span, while pulse mode also exposes finite-difference/nodal continuity-charge diagnostics. Always use the 3-level mesh convergence assistant and compare critical results with NEC/FEM/full-wave software. Local T/Y/X refinement remains available. Feed reference impedance affects Gamma/VSWR only. Finite conductivity, dielectrics and ground planes are not modeled by this standalone wire-only solve; the hybrid wire+PEC solver supports both pulse and rooftop wire bases. Use the canonical validation bench to distinguish numerical convergence from physical agreement.";
    return out;
}

double thinCircularLoopSelfInductance(double radiusM,double wireRadiusM,int turns)
{
    const double r=std::max(radiusM,1e-9);const double a=std::clamp(wireRadiusM,1e-12,0.3*r);const int n=std::max(1,turns);
    return std::max(0.0,Mu0*r*(std::log(8.0*r/a)-2.0))*double(n*n);
}

LoopCouplingResult solveCircularLoopCoupling(const LoopCouplingInput &input)
{
    LoopCouplingResult out;
    const auto &a=input.primary,&b=input.secondary;
    if(!(a.radiusM>0&&b.radiusM>0&&a.wireRadiusM>0&&b.wireRadiusM>0&&a.turns>0&&b.turns>0))
    {out.error="Loop radii, wire radii and turn counts must be positive";return out;}
    const int n=std::clamp(input.integrationSegments,24,720);
    const double muR=std::max(1e-9,input.relativePermeability);
    Vec3 ua,va,ub,vb;loopBasis(a.axis,ua,va);loopBasis(b.axis,ub,vb);
    const double dt=2.0*Pi/n;
    double sum=0.0,minDist=std::numeric_limits<double>::infinity();
    for(int i=0;i<n;++i)
    {
        const double t=(i+0.5)*dt;
        const Vec3 pa=a.center+ua*(a.radiusM*std::cos(t))+va*(a.radiusM*std::sin(t));
        const Vec3 dla=ua*(-a.radiusM*std::sin(t)*dt)+va*(a.radiusM*std::cos(t)*dt);
        for(int j=0;j<n;++j)
        {
            const double s=(j+0.5)*dt;
            const Vec3 pb=b.center+ub*(b.radiusM*std::cos(s))+vb*(b.radiusM*std::sin(s));
            const Vec3 dlb=ub*(-b.radiusM*std::sin(s)*dt)+vb*(b.radiusM*std::cos(s)*dt);
            const double d=norm(pa-pb);minDist=std::min(minDist,d);
            if(d<0.25*(a.wireRadiusM+b.wireRadiusM)) { out.error="Loop conductors intersect or are too close for filamentary Neumann integration";return out; }
            sum+=dot(dla,dlb)/d;
        }
    }
    out.mutualInductanceH=Mu0*muR/(4.0*Pi)*sum*a.turns*b.turns;
    out.primarySelfInductanceH=muR*thinCircularLoopSelfInductance(a.radiusM,a.wireRadiusM,a.turns);
    out.secondarySelfInductanceH=muR*thinCircularLoopSelfInductance(b.radiusM,b.wireRadiusM,b.turns);
    out.rawCouplingCoefficient=out.mutualInductanceH/std::sqrt(std::max(out.primarySelfInductanceH*out.secondarySelfInductanceH,1e-30));
    out.couplingCoefficient=std::clamp(out.rawCouplingCoefficient,-1.0,1.0);
    out.minimumSegmentDistanceM=minDist;out.valid=std::isfinite(out.mutualInductanceH);
    out.note = muR > 1.000001
        ? "Mutual inductance uses the Neumann double line integral with filamentary circular loops and arbitrary 3D orientation, scaled by an effective uniform relative permeability. Self inductances are scaled by the same mu_r, so k is unchanged. This is only a first-order homogeneous/effective-medium core approximation: a localized ferrite core, air gaps, saturation and fringing require a magnetostatic/FEM solution."
        : "Mutual inductance uses the Neumann double line integral with filamentary circular loops and arbitrary 3D orientation. Self inductances use a separate thin-wire loop approximation, so k is an engineering estimate; nearby thick windings, localized magnetic cores and distributed windings require a more detailed model.";
    return out;
}

} // namespace NumericalEM

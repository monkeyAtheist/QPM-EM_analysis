#include "pec_surface_mom.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace PecSurfaceMom
{
namespace
{
using NumericalEM::Vec3;
using C = std::complex<double>;
constexpr C J{0.0, 1.0};

bool finiteScalar(double v){return std::isfinite(v);}
bool finiteVec3(const Vec3 &v){return finiteScalar(v.x)&&finiteScalar(v.y)&&finiteScalar(v.z);}
bool finiteComplex(const C &v){return finiteScalar(v.real())&&finiteScalar(v.imag());}

Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}

double triangleArea(const Triangle3D &t)
{
    return 0.5 * NumericalEM::norm(cross(t.b-t.a, t.c-t.a));
}

Vec3 centroid(const Triangle3D &t)
{
    return {(t.a.x+t.b.x+t.c.x)/3.0,
            (t.a.y+t.b.y+t.c.y)/3.0,
            (t.a.z+t.b.z+t.c.z)/3.0};
}

struct ComplexVec3
{
    C x{0,0}, y{0,0}, z{0,0};
};

ComplexVec3 &operator+=(ComplexVec3 &a, const ComplexVec3 &b)
{
    a.x+=b.x; a.y+=b.y; a.z+=b.z; return a;
}
ComplexVec3 operator*(const Vec3 &a, C s)
{
    return {a.x*s,a.y*s,a.z*s};
}
ComplexVec3 operator*(const ComplexVec3 &a, C s)
{
    return {a.x*s,a.y*s,a.z*s};
}
C dotComplex(const Vec3 &a, const ComplexVec3 &b)
{
    return a.x*b.x+a.y*b.y+a.z*b.z;
}
C dotComplex(const ComplexVec3 &a, const Vec3 &b)
{
    return a.x*b.x+a.y*b.y+a.z*b.z;
}
double normComplex2(const ComplexVec3 &a)
{
    return std::norm(a.x)+std::norm(a.y)+std::norm(a.z);
}
ComplexVec3 transverse(const ComplexVec3 &a, const Vec3 &rhat)
{
    const C p=dotComplex(a,rhat);
    return {a.x-rhat.x*p,a.y-rhat.y*p,a.z-rhat.z*p};
}

struct SolveStats
{
    double minPivot = std::numeric_limits<double>::infinity();
    double maxPivot = 0.0;
};

bool solveDense(std::vector<std::vector<C>> a, std::vector<C> b,
                std::vector<C> &x, SolveStats &stats)
{
    const int n=static_cast<int>(b.size());
    if(static_cast<int>(a.size())!=n) return false;
    for(int k=0;k<n;++k)
    {
        int pivot=k; double best=std::abs(a[k][k]);
        for(int i=k+1;i<n;++i){const double q=std::abs(a[i][k]);if(q>best){best=q;pivot=i;}}
        if(!(best>1e-18)||!std::isfinite(best)) return false;
        if(pivot!=k){std::swap(a[pivot],a[k]);std::swap(b[pivot],b[k]);}
        stats.minPivot=std::min(stats.minPivot,best); stats.maxPivot=std::max(stats.maxPivot,best);
        const C diag=a[k][k];
        for(int i=k+1;i<n;++i)
        {
            const C f=a[i][k]/diag; if(std::abs(f)==0.0) continue;
            a[i][k]={0,0};
            for(int q=k+1;q<n;++q) a[i][q]-=f*a[k][q];
            b[i]-=f*b[k];
        }
    }
    x.assign(n,{0,0});
    for(int i=n-1;i>=0;--i)
    {
        C s=b[i]; for(int q=i+1;q<n;++q) s-=a[i][q]*x[q];
        if(std::abs(a[i][i])<1e-18) return false;
        x[i]=s/a[i][i];
    }
    return true;
}

struct VertexKey
{
    std::int64_t x=0,y=0,z=0;
    bool operator<(const VertexKey&o) const { return std::tie(x,y,z)<std::tie(o.x,o.y,o.z); }
};

VertexKey keyFor(const Vec3&p,double tol)
{
    const double s=1.0/std::max(tol,1e-12);
    return {static_cast<std::int64_t>(std::llround(p.x*s)),
            static_cast<std::int64_t>(std::llround(p.y*s)),
            static_cast<std::int64_t>(std::llround(p.z*s))};
}

struct TriData
{
    Triangle3D tri;
    std::array<int,3> vertex{-1,-1,-1};
    double area=0.0;
    Vec3 centroid{};
};

struct EdgeKey
{
    int a=-1,b=-1;
    bool operator<(const EdgeKey&o) const { return std::tie(a,b)<std::tie(o.a,o.b); }
};

struct EdgeUse
{
    int triangle=-1;
    int freeLocal=-1;
    int edgeLocalA=-1;
    int edgeLocalB=-1;
};

struct BasisSupport
{
    int triangle=-1;
    int freeVertex=-1;
    double sign=1.0;
};

struct BasisData
{
    int edgeV0=-1,edgeV1=-1;
    double edgeLength=0.0;
    BasisSupport plus{},minus{};
};

struct QuadPoint
{
    Vec3 p{};
    double weight=0.0;
};

std::array<QuadPoint,3> triangleQuadrature(const TriData&t)
{
    // Degree-2 symmetric triangle rule, positive weights A/3.
    constexpr std::array<std::array<double,3>,3> b{{
        {{2.0/3.0,1.0/6.0,1.0/6.0}},
        {{1.0/6.0,2.0/3.0,1.0/6.0}},
        {{1.0/6.0,1.0/6.0,2.0/3.0}}
    }};
    std::array<QuadPoint,3> q{};
    for(int i=0;i<3;++i)
    {
        q[i].p={b[i][0]*t.tri.a.x+b[i][1]*t.tri.b.x+b[i][2]*t.tri.c.x,
                b[i][0]*t.tri.a.y+b[i][1]*t.tri.b.y+b[i][2]*t.tri.c.y,
                b[i][0]*t.tri.a.z+b[i][1]*t.tri.b.z+b[i][2]*t.tri.c.z};
        q[i].weight=t.area/3.0;
    }
    return q;
}

std::array<QuadPoint,7> triangleQuadratureNear(const TriData&t)
{
    constexpr double a1=0.059715871789770, b1=0.470142064105115;
    constexpr double a2=0.797426985353087, b2=0.101286507323456;
    constexpr std::array<std::array<double,3>,7>b{{
        {{1.0/3.0,1.0/3.0,1.0/3.0}},
        {{a1,b1,b1}},{{b1,a1,b1}},{{b1,b1,a1}},
        {{a2,b2,b2}},{{b2,a2,b2}},{{b2,b2,a2}}
    }};
    constexpr std::array<double,7>w{{
        0.225000000000000,
        0.132394152788506,0.132394152788506,0.132394152788506,
        0.125939180544827,0.125939180544827,0.125939180544827
    }};
    std::array<QuadPoint,7>q{};
    for(int i=0;i<7;++i)
    {
        q[static_cast<std::size_t>(i)].p={
            b[static_cast<std::size_t>(i)][0]*t.tri.a.x+b[static_cast<std::size_t>(i)][1]*t.tri.b.x+b[static_cast<std::size_t>(i)][2]*t.tri.c.x,
            b[static_cast<std::size_t>(i)][0]*t.tri.a.y+b[static_cast<std::size_t>(i)][1]*t.tri.b.y+b[static_cast<std::size_t>(i)][2]*t.tri.c.y,
            b[static_cast<std::size_t>(i)][0]*t.tri.a.z+b[static_cast<std::size_t>(i)][1]*t.tri.b.z+b[static_cast<std::size_t>(i)][2]*t.tri.c.z};
        q[static_cast<std::size_t>(i)].weight=w[static_cast<std::size_t>(i)]*t.area;
    }
    return q;
}

int sharedVertexCount(const TriData&a,const TriData&b)
{
    int n=0;for(int va:a.vertex)for(int vb:b.vertex)if(va==vb){++n;break;}return n;
}

std::vector<QuadPoint> triangleQuadratureComposite1(const TriData&t)
{
    const Vec3 ab=(t.tri.a+t.tri.b)*0.5,bc=(t.tri.b+t.tri.c)*0.5,ca=(t.tri.c+t.tri.a)*0.5;
    const std::array<Triangle3D,4> child{{
        {t.tri.a,ab,ca,t.tri.surfaceIndex},{ab,t.tri.b,bc,t.tri.surfaceIndex},
        {ca,bc,t.tri.c,t.tri.surfaceIndex},{ab,bc,ca,t.tri.surfaceIndex}}};
    std::vector<QuadPoint>out;out.reserve(12);
    for(const auto&ct:child){TriData d;d.tri=ct;d.area=triangleArea(ct);d.centroid=(ct.a+ct.b+ct.c)*(1.0/3.0);const auto q=triangleQuadrature(d);out.insert(out.end(),q.begin(),q.end());}
    return out;
}

Vec3 freeVertexPosition(const TriData&t,int freeVertexGlobal)
{
    if(t.vertex[0]==freeVertexGlobal) return t.tri.a;
    if(t.vertex[1]==freeVertexGlobal) return t.tri.b;
    return t.tri.c;
}

Vec3 rwgValue(const BasisData&b,const BasisSupport&s,const TriData&t,const Vec3&r)
{
    const Vec3 rf=freeVertexPosition(t,s.freeVertex);
    const double factor=b.edgeLength/(2.0*t.area);
    return s.sign>0.0 ? (r-rf)*factor : (rf-r)*factor;
}

double rwgDivergence(const BasisData&b,const BasisSupport&s,const TriData&t)
{
    return s.sign*b.edgeLength/t.area;
}

C green(const Vec3&r,const Vec3&rp,double k,double regularization)
{
    const Vec3 d=r-rp;
    const double R=std::sqrt(NumericalEM::dot(d,d)+regularization*regularization);
    return std::exp(-J*k*R)/(4.0*NumericalEM::Pi*R);
}

struct SingularSourceIntegral{C vectorGreen{0,0};C scalarGreen{0,0};};

SingularSourceIntegral integrateSameTriangleSourceDuffy(const BasisData&sourceBasis,const BasisSupport&sourceSupport,
                                                         const TriData&sourceTriangle,const Vec3&observationPoint,
                                                         const Vec3&observationBasisValue,double k)
{
    constexpr std::array<double,4> gx{{-0.8611363115940525752,-0.3399810435848562648,0.3399810435848562648,0.8611363115940525752}};
    constexpr std::array<double,4> gw{{0.3478548451374538574,0.6521451548625461426,0.6521451548625461426,0.3478548451374538574}};
    const std::array<Vec3,3>v{{sourceTriangle.tri.a,sourceTriangle.tri.b,sourceTriangle.tri.c}};
    SingularSourceIntegral out;
    for(int side=0;side<3;++side)
    {
        const Vec3 a=v[static_cast<std::size_t>(side)]-observationPoint,b=v[static_cast<std::size_t>((side+1)%3)]-observationPoint;
        const double crossMag=NumericalEM::norm(cross(a,b));if(!(crossMag>1e-30))continue;
        for(int iu=0;iu<4;++iu){const double u=0.5*(gx[static_cast<std::size_t>(iu)]+1.0),wu=0.5*gw[static_cast<std::size_t>(iu)];
            for(int iv=0;iv<4;++iv){const double vv=0.5*(gx[static_cast<std::size_t>(iv)]+1.0),wv=0.5*gw[static_cast<std::size_t>(iv)];const Vec3 radial=a*(1.0-vv)+b*vv;const Vec3 sourcePoint=observationPoint+radial*u;const double weight=wu*wv*crossMag*u;const C G=green(observationPoint,sourcePoint,k,0.0);const Vec3 fn=rwgValue(sourceBasis,sourceSupport,sourceTriangle,sourcePoint);out.vectorGreen+=NumericalEM::dot(observationBasisValue,fn)*G*weight;out.scalarGreen+=G*weight;}}
    }
    return out;
}

Vec3 orthogonalizedPolarization(const Vec3&khat,const Vec3&desired)
{
    Vec3 e=desired-khat*NumericalEM::dot(desired,khat);
    if(NumericalEM::norm(e)<1e-10)
    {
        const Vec3 ref=std::abs(khat.z)<0.85?Vec3{0,0,1}:Vec3{0,1,0};
        e=cross(ref,khat);
    }
    return NumericalEM::normalized(e);
}

ComplexVec3 incidentField(const PlaneWaveExcitation&exc,const Vec3&khat,const Vec3&ehat,
                          const Vec3&r,double k)
{
    const C ph=std::exp(-J*k*NumericalEM::dot(khat,r));
    return ehat*(exc.electricFieldAmplitudeVpm*ph);
}

C rhsForBasis(const BasisData&b,const std::vector<TriData>&tri,
              const PlaneWaveExcitation&exc,const Vec3&khat,const Vec3&ehat,double k)
{
    C rhs{0,0};
    for(const auto&s:{b.plus,b.minus})
    {
        if(s.triangle<0) continue;
        const auto&q=triangleQuadrature(tri[static_cast<std::size_t>(s.triangle)]);
        for(const auto&qp:q)
        {
            const Vec3 f=rwgValue(b,s,tri[static_cast<std::size_t>(s.triangle)],qp.p);
            rhs += dotComplex(f,incidentField(exc,khat,ehat,qp.p,k))*qp.weight;
        }
    }
    return rhs;
}

C matrixEntry(const BasisData&m,const BasisData&n,const std::vector<TriData>&tri,
              double k,double omega,double selfFactor)
{
    (void)selfFactor; // Retained in the API/project format for backward compatibility.
    C vectorTerm{0,0}, scalarTerm{0,0};
    const std::array<BasisSupport,2> ms{m.plus,m.minus},ns{n.plus,n.minus};
    for(const auto&sm:ms) for(const auto&sn:ns)
    {
        if(sm.triangle<0||sn.triangle<0) continue;
        const TriData&tm=tri[static_cast<std::size_t>(sm.triangle)];
        const TriData&tn=tri[static_cast<std::size_t>(sn.triangle)];
        const double divM=rwgDivergence(m,sm,tm),divN=rwgDivergence(n,sn,tn);
        if(sm.triangle==sn.triangle)
        {
            const auto qm=triangleQuadratureNear(tm);
            for(const auto&po:qm){const Vec3 fm=rwgValue(m,sm,tm,po.p);const auto inner=integrateSameTriangleSourceDuffy(n,sn,tn,po.p,fm,k);vectorTerm+=inner.vectorGreen*po.weight;scalarTerm+=divM*divN*inner.scalarGreen*po.weight;}
            continue;
        }
        if(sharedVertexCount(tm,tn)>0)
        {
            const auto qm=triangleQuadratureComposite1(tm),qn=triangleQuadratureComposite1(tn);
            for(const auto&po:qm)for(const auto&ps:qn){const Vec3 fm=rwgValue(m,sm,tm,po.p),fn=rwgValue(n,sn,tn,ps.p);const C G=green(po.p,ps.p,k,0.0);const double w=po.weight*ps.weight;vectorTerm+=NumericalEM::dot(fm,fn)*G*w;scalarTerm+=divM*divN*G*w;}
            continue;
        }
        const auto qm=triangleQuadrature(tm),qn=triangleQuadrature(tn);
        for(const auto&po:qm)for(const auto&ps:qn){const Vec3 fm=rwgValue(m,sm,tm,po.p),fn=rwgValue(n,sn,tn,ps.p);const C G=green(po.p,ps.p,k,0.0);const double w=po.weight*ps.weight;vectorTerm+=NumericalEM::dot(fm,fn)*G*w;scalarTerm+=divM*divN*G*w;}
    }
    const C zA=J*omega*NumericalEM::Mu0*vectorTerm;
    const C zPhi=(1.0/(J*omega*NumericalEM::Epsilon0))*scalarTerm;
    return zA+zPhi;
}

ComplexVec3 triangleCurrentAtCentroid(int triIndex,const std::vector<BasisData>&bases,
                                     const std::vector<TriData>&tri,const std::vector<C>&coeff)
{
    ComplexVec3 out{};
    const TriData&t=tri[static_cast<std::size_t>(triIndex)];
    for(std::size_t bi=0;bi<bases.size();++bi)
    {
        const auto&b=bases[bi];
        for(const auto&s:{b.plus,b.minus}) if(s.triangle==triIndex)
            out += rwgValue(b,s,t,t.centroid)*coeff[bi];
    }
    return out;
}

ComplexVec3 farFieldMomentTransverse(const Vec3&rhat,const std::vector<TriData>&tri,
                                      const std::vector<ComplexVec3>&jTri,double k)
{
    ComplexVec3 f{};
    for(std::size_t i=0;i<tri.size();++i)
    {
        const C ph=std::exp(J*k*NumericalEM::dot(rhat,tri[i].centroid));
        f += jTri[i]*(tri[i].area*ph);
    }
    return transverse(f,rhat);
}

double rcsForDirection(const Vec3&rhat,const std::vector<TriData>&tri,
                       const std::vector<ComplexVec3>&jTri,double k,double omega,double e0)
{
    const ComplexVec3 ft=farFieldMomentTransverse(rhat,tri,jTri,k);
    const double scale=std::pow(omega*NumericalEM::Mu0,2)/(4.0*NumericalEM::Pi*std::max(1e-30,e0*e0));
    return std::max(0.0,scale*normComplex2(ft));
}

double radiationIntensityForDirection(const Vec3&rhat,const std::vector<TriData>&tri,
                                      const std::vector<ComplexVec3>&jTri,double k,double omega)
{
    const ComplexVec3 ft=farFieldMomentTransverse(rhat,tri,jTri,k);
    const double eta0=std::sqrt(NumericalEM::Mu0/NumericalEM::Epsilon0);
    const double erScale=std::pow(omega*NumericalEM::Mu0/(4.0*NumericalEM::Pi),2);
    return std::max(0.0,erScale*normComplex2(ft)/(2.0*eta0));
}

} // namespace

Result solve(const Input &input)
{
    Result out;
    if(!finiteScalar(input.frequencyHz) || !finiteScalar(input.vertexMergeToleranceM) ||
       !finiteScalar(input.selfRegularizationFactor) || !finiteScalar(input.rcsCutStepDeg))
    {out.error="Surface MoM input contains NaN or Inf";return out;}
    if(!(input.frequencyHz>0.0)){out.error="Surface MoM frequency must be positive";return out;}
    if(input.triangles.empty()){out.error="Surface MoM requires at least one triangle";return out;}
    if(input.maxUnknowns<=0 || !(input.vertexMergeToleranceM>=0.0) || !(input.selfRegularizationFactor>0.0) || !(input.rcsCutStepDeg>0.0))
    {out.error="Surface MoM discretization parameters must be finite positive values";return out;}
    for(const auto &triangle:input.triangles)
    {
        if(!finiteVec3(triangle.a)||!finiteVec3(triangle.b)||!finiteVec3(triangle.c))
        {out.error="PEC surface geometry contains NaN or Inf";return out;}
    }
    if(input.excitationKind==ExcitationKind::PlaneWave)
    {
        if(!finiteVec3(input.excitation.propagationDirection)||!finiteVec3(input.excitation.electricFieldDirection)||!finiteComplex(input.excitation.electricFieldAmplitudeVpm))
        {out.error="Plane-wave excitation contains NaN or Inf";return out;}
        if(!(NumericalEM::norm(input.excitation.propagationDirection)>1e-15) || !(NumericalEM::norm(input.excitation.electricFieldDirection)>1e-15))
        {out.error="Plane-wave propagation and polarization directions must be non-zero";return out;}
        if(!(std::abs(input.excitation.electricFieldAmplitudeVpm)>1e-15))
        {out.error="Incident electric-field amplitude must be non-zero";return out;}
    }
    if(input.excitationKind==ExcitationKind::LumpedEdgePort)
    {
        if(!finiteVec3(input.port.positionM)||!finiteComplex(input.port.voltageV)||!finiteScalar(input.port.referenceOhm))
        {out.error="Lumped edge port contains NaN or Inf";return out;}
        if(!(std::abs(input.port.voltageV)>1e-15) || !(input.port.referenceOhm>0.0))
        {out.error="Lumped edge port requires non-zero voltage and positive reference impedance";return out;}
    }

    const double tol=std::max(1e-12,input.vertexMergeToleranceM);
    std::map<VertexKey,int> vertexMap;
    std::vector<Vec3> vertices;
    std::vector<TriData> tri;
    tri.reserve(input.triangles.size());
    auto vertexId=[&](const Vec3&p){
        const VertexKey key=keyFor(p,tol);
        auto it=vertexMap.find(key); if(it!=vertexMap.end()) return it->second;
        const int id=static_cast<int>(vertices.size());vertexMap.emplace(key,id);vertices.push_back(p);return id;};

    for(const auto&t:input.triangles)
    {
        const double A=triangleArea(t);
        if(!(A>1e-18)||!std::isfinite(A)) continue;
        TriData td;td.tri=t;td.area=A;td.centroid=centroid(t);
        td.vertex={vertexId(t.a),vertexId(t.b),vertexId(t.c)};
        if(td.vertex[0]==td.vertex[1]||td.vertex[1]==td.vertex[2]||td.vertex[2]==td.vertex[0]) continue;
        tri.push_back(td);
    }
    if(tri.empty()){out.error="All PEC triangles are degenerate";return out;}

    std::map<EdgeKey,std::vector<EdgeUse>> edgeUses;
    for(int ti=0;ti<static_cast<int>(tri.size());++ti)
    {
        const auto&v=tri[static_cast<std::size_t>(ti)].vertex;
        const std::array<std::array<int,3>,3> e{{{{0,1,2}},{{1,2,0}},{{2,0,1}}}};
        for(const auto&q:e)
        {
            int a=v[q[0]],b=v[q[1]]; if(a>b) std::swap(a,b);
            edgeUses[{a,b}].push_back({ti,q[2],q[0],q[1]});
        }
    }

    std::vector<BasisData>bases;
    int boundary=0;
    for(const auto&[edge,uses]:edgeUses)
    {
        if(uses.size()==1){++boundary;continue;}
        if(uses.size()!=2){out.error="Non-manifold PEC surface: an edge is shared by more than two triangles";return out;}
        const Vec3&a=vertices[static_cast<std::size_t>(edge.a)],&b=vertices[static_cast<std::size_t>(edge.b)];
        const double l=NumericalEM::norm(b-a); if(!(l>1e-15)) continue;
        BasisData rwg;rwg.edgeV0=edge.a;rwg.edgeV1=edge.b;rwg.edgeLength=l;
        rwg.plus={uses[0].triangle,tri[static_cast<std::size_t>(uses[0].triangle)].vertex[uses[0].freeLocal],+1.0};
        rwg.minus={uses[1].triangle,tri[static_cast<std::size_t>(uses[1].triangle)].vertex[uses[1].freeLocal],-1.0};
        bases.push_back(rwg);
    }
    if(bases.empty()){out.error="No RWG interior edges were found. Refine/connect the surface mesh.";return out;}
    if(static_cast<int>(bases.size())>std::max(1,input.maxUnknowns))
    {
        out.error="RWG unknown count exceeds the configured dense-solver limit; increase mesh hint or Max RWG unknowns";
        return out;
    }

    const double omega=2.0*NumericalEM::Pi*input.frequencyHz;
    const double lambda=NumericalEM::C0/input.frequencyHz;
    const double k=2.0*NumericalEM::Pi/lambda;
    const Vec3 khat=NumericalEM::normalized(input.excitation.propagationDirection);
    const Vec3 ehat=orthogonalizedPolarization(khat,input.excitation.electricFieldDirection);
    const int n=static_cast<int>(bases.size());

    int portBasis=-1; double portDistance=std::numeric_limits<double>::infinity();
    if(input.excitationKind==ExcitationKind::LumpedEdgePort)
    {
        for(int bi=0;bi<n;++bi)
        {
            const auto&b=bases[static_cast<std::size_t>(bi)];
            const Vec3 a=vertices[static_cast<std::size_t>(b.edgeV0)],bb=vertices[static_cast<std::size_t>(b.edgeV1)];
            const Vec3 ab=bb-a;const double d2=NumericalEM::dot(ab,ab);
            double t=d2>1e-30?NumericalEM::dot(input.port.positionM-a,ab)/d2:0.0;t=std::clamp(t,0.0,1.0);
            const double d=NumericalEM::norm(input.port.positionM-(a+ab*t));
            if(d<portDistance){portDistance=d;portBasis=bi;}
        }
        if(portBasis<0){out.error="Could not map the surface port to an RWG interior edge";return out;}
    }

    std::vector<std::vector<C>> z(n,std::vector<C>(n,{0,0}));
    std::vector<C> rhs(n,{0,0});
    for(int m=0;m<n;++m)
    {
        if(input.excitationKind==ExcitationKind::PlaneWave)
            rhs[m]=rhsForBasis(bases[static_cast<std::size_t>(m)],tri,input.excitation,khat,ehat,k);
        else if(m==portBasis)
            rhs[m]=input.port.voltageV*bases[static_cast<std::size_t>(m)].edgeLength;
        for(int q=0;q<n;++q)
            z[m][q]=matrixEntry(bases[static_cast<std::size_t>(m)],bases[static_cast<std::size_t>(q)],tri,k,omega,
                                std::clamp(input.selfRegularizationFactor,0.02,1.0));
    }

    SolveStats stats;std::vector<C> coeff;
    if(!solveDense(z,rhs,coeff,stats)){out.error="PEC RWG dense EFIE matrix is singular or ill-conditioned";return out;}

    double r2=0.0,b2=0.0;
    for(int i=0;i<n;++i)
    {
        C ax{0,0};for(int q=0;q<n;++q) ax+=z[i][q]*coeff[q];
        r2+=std::norm(ax-rhs[i]);b2+=std::norm(rhs[i]);
    }

    out.wavelengthM=lambda;out.triangleCount=static_cast<int>(tri.size());out.uniqueVertexCount=static_cast<int>(vertices.size());
    out.boundaryEdgeCount=boundary;out.rwgUnknownCount=n;out.residualRelative=std::sqrt(r2/std::max(1e-30,b2));
    out.minPivotAbs=stats.minPivot;out.maxPivotAbs=stats.maxPivot;
    out.bases.reserve(bases.size());
    for(std::size_t i=0;i<bases.size();++i)
    {
        const auto&b=bases[i];const Vec3 a=vertices[static_cast<std::size_t>(b.edgeV0)],bb=vertices[static_cast<std::size_t>(b.edgeV1)];
        out.bases.push_back({b.plus.triangle,b.minus.triangle,a,bb,(a+bb)*0.5,b.edgeLength,coeff[i],coeff[i]*b.edgeLength});
    }

    std::vector<ComplexVec3> jTri; jTri.reserve(tri.size());out.triangleCurrents.reserve(tri.size());
    for(int ti=0;ti<static_cast<int>(tri.size());++ti)
    {
        const auto jv=triangleCurrentAtCentroid(ti,bases,tri,coeff);jTri.push_back(jv);
        const double mag=std::sqrt(normComplex2(jv));out.peakSurfaceCurrentApm=std::max(out.peakSurfaceCurrentApm,mag);
        out.triangleCurrents.push_back({tri[static_cast<std::size_t>(ti)].centroid,tri[static_cast<std::size_t>(ti)].area,jv.x,jv.y,jv.z,mag});
    }

    if(input.excitationKind==ExcitationKind::LumpedEdgePort)
    {
        out.drivenPort=true;out.drivenPortBasisIndex=portBasis;out.drivenPortDistanceM=portDistance;
        const auto&pb=out.bases[static_cast<std::size_t>(portBasis)];out.drivenPortEdgeCenter=pb.edgeCenter;
        out.drivenPortVoltageV=input.port.voltageV;out.drivenPortCurrentA=pb.integratedEdgeCurrentA;
        if(std::abs(out.drivenPortCurrentA)>1e-18) out.inputImpedanceOhm=out.drivenPortVoltageV/out.drivenPortCurrentA;
        const C z0{input.port.referenceOhm,0.0};
        if(std::abs(out.inputImpedanceOhm+z0)>1e-18) out.reflectionCoefficient=(out.inputImpedanceOhm-z0)/(out.inputImpedanceOhm+z0);
        const double gm=std::abs(out.reflectionCoefficient);
        out.returnLossDb=gm>1e-15?-20.0*std::log10(gm):300.0;
        out.vswr=gm<1.0?(1.0+gm)/std::max(1e-15,1.0-gm):std::numeric_limits<double>::infinity();
        out.acceptedPowerW=0.5*std::real(out.drivenPortVoltageV*std::conj(out.drivenPortCurrentA));
    }

    if(input.computeRcsCuts && input.excitationKind==ExcitationKind::PlaneWave)
    {
        const double step=std::clamp(input.rcsCutStepDeg,0.5,30.0);
        const double e0=std::abs(input.excitation.electricFieldAmplitudeVpm);
        for(double az=0.0;az<360.0-1e-9;az+=step)
        {
            const double a=az*NumericalEM::Pi/180.0;const Vec3 rhat{std::cos(a),std::sin(a),0.0};
            const double sigma=rcsForDirection(rhat,tri,jTri,k,omega,e0);
            out.azimuthDeg.push_back(az);out.azimuthRcsM2.push_back(sigma);
            if(sigma>out.maxRcsM2){out.maxRcsM2=sigma;out.maxRcsAzimuthDeg=az;}
        }
        for(double el=-90.0;el<=90.0+1e-9;el+=step)
        {
            const double a=el*NumericalEM::Pi/180.0;const Vec3 rhat{std::cos(a),0.0,std::sin(a)};
            const double sigma=rcsForDirection(rhat,tri,jTri,k,omega,e0);
            out.elevationDeg.push_back(el);out.elevationRcsM2.push_back(sigma);
        }
    }
    else if(input.excitationKind==ExcitationKind::LumpedEdgePort)
    {
        const double cutStep=std::clamp(input.rcsCutStepDeg,0.5,30.0);
        double umax=0.0;
        std::vector<double> azU,elU;
        for(double az=0.0;az<360.0-1e-9;az+=cutStep)
        {
            const double a=az*NumericalEM::Pi/180.0;const Vec3 rhat{std::cos(a),std::sin(a),0.0};
            const double uRad=radiationIntensityForDirection(rhat,tri,jTri,k,omega);
            out.azimuthDeg.push_back(az);azU.push_back(uRad);umax=std::max(umax,uRad);
        }
        for(double el=-90.0;el<=90.0+1e-9;el+=cutStep)
        {
            const double a=el*NumericalEM::Pi/180.0;const Vec3 rhat{std::cos(a),0.0,std::sin(a)};
            const double uRad=radiationIntensityForDirection(rhat,tri,jTri,k,omega);
            out.elevationDeg.push_back(el);elU.push_back(uRad);umax=std::max(umax,uRad);
        }
        for(double uRad:azU) out.azimuthNormalizedFarField.push_back(umax>0.0?std::sqrt(uRad/umax):0.0);
        for(double uRad:elU) out.elevationNormalizedFarField.push_back(umax>0.0?std::sqrt(uRad/umax):0.0);

        // Coarse full-sphere power integration used only for engineering directivity/power diagnostics.
        const double dth=5.0*NumericalEM::Pi/180.0,dph=5.0*NumericalEM::Pi/180.0;double prad=0.0;umax=0.0;
        for(double th=2.5;th<180.0;th+=5.0)
        {
            const double tr=th*NumericalEM::Pi/180.0;
            for(double ph=0.0;ph<360.0;ph+=5.0)
            {
                const double pr=ph*NumericalEM::Pi/180.0;const Vec3 rhat{std::sin(tr)*std::cos(pr),std::sin(tr)*std::sin(pr),std::cos(tr)};
                const double uRad=radiationIntensityForDirection(rhat,tri,jTri,k,omega);
                prad+=uRad*std::sin(tr)*dth*dph;umax=std::max(umax,uRad);
            }
        }
        out.radiatedPowerW=prad;
        if(prad>1e-30){out.directivityLinear=4.0*NumericalEM::Pi*umax/prad;out.directivityDbi=10.0*std::log10(std::max(1e-30,out.directivityLinear));}
    }

    bool finiteResult=finiteScalar(out.residualRelative)&&finiteScalar(out.peakSurfaceCurrentApm)&&finiteScalar(out.maxRcsM2);
    if(out.drivenPort)
        finiteResult=finiteResult&&finiteComplex(out.drivenPortCurrentA)&&finiteComplex(out.inputImpedanceOhm)&&finiteComplex(out.reflectionCoefficient)&&
                     finiteScalar(out.returnLossDb)&&(finiteScalar(out.vswr)||std::isinf(out.vswr))&&finiteScalar(out.acceptedPowerW)&&finiteScalar(out.radiatedPowerW);
    out.valid=finiteResult;
    if(!out.valid && out.error.empty()) out.error="Surface MoM solve produced a non-finite numerical result";
    out.note=input.excitationKind==ExcitationKind::PlaneWave
        ? "Educational frequency-domain PEC surface EFIE with RWG basis functions on interior triangle edges. Matrix integrals use Duffy singularity extraction for same-triangle RWG terms, a localized composite rule for adjacent triangles, and the compact symmetric rule for far pairs. Open-surface boundary current is forced to zero by omitting half-RWG boundary functions. Plane-wave results are suitable for qualitative current/RCS studies and mesh-convergence exercises, not production antenna certification. Thin-wire/surface mutual coupling is not yet included."
        : "Educational driven PEC surface EFIE with an approximate lumped voltage source mapped to the nearest interior RWG edge. Port current is the RWG coefficient times shared-edge length, so Zin=V/I is a first-order edge-gap estimate. RWG same-triangle singular terms use Duffy extraction, adjacent triangles use localized composite quadrature, and open boundaries omit half-RWG functions. Use mesh/port-position/regularization convergence studies; thin-wire/surface mutual coupling is not yet included.";
    return out;
}

} // namespace PecSurfaceMom

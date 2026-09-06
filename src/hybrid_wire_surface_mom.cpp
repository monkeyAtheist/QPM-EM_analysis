#include "hybrid_wire_surface_mom.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <tuple>
#include <utility>

namespace HybridWireSurfaceMom
{
namespace
{
using NumericalEM::Vec3;
using C = std::complex<double>;
constexpr C J{0.0,1.0};
constexpr std::array<double,8> GlX{
    0.09501250983763744,0.28160355077925891,0.45801677765722739,0.61787624440264375,
    0.75540440835500303,0.86563120238783174,0.94457502307323258,0.98940093499164993};
constexpr std::array<double,8> GlW{
    0.18945061045506850,0.18260341504492359,0.16915651939500254,0.14959598881657673,
    0.12462897125553387,0.09515851168249278,0.06225352393864789,0.02715245941175409};

bool finiteScalar(double v){return std::isfinite(v);}
bool finiteVec3(const Vec3 &v){return finiteScalar(v.x)&&finiteScalar(v.y)&&finiteScalar(v.z);}
bool finiteComplex(const C &v){return finiteScalar(v.real())&&finiteScalar(v.imag());}
bool finiteVector(const std::vector<double> &values){return std::all_of(values.begin(),values.end(),[](double v){return std::isfinite(v);});}

bool validateInputNumerics(const Input &input,std::string &error)
{
    if(!finiteScalar(input.wire.frequencyHz)||!finiteScalar(input.surfaceVertexMergeToleranceM)||
       !finiteScalar(input.surfaceSelfRegularizationFactor)||!finiteScalar(input.mutualRegularizationFactor)||
       !finiteScalar(input.surfaceConductivitySPerM)||!finiteScalar(input.surfaceThicknessM)||
       !finiteScalar(input.surfaceRelativePermeability)||!finiteScalar(input.farFieldCutStepDeg)||
       !finiteScalar(input.farFieldIntegrationStepDeg))
    {error="Hybrid input contains NaN or Inf";return false;}
    if(input.maxTotalUnknowns<=0||input.maxSurfaceUnknowns<=0||!(input.surfaceVertexMergeToleranceM>=0.0)||
       !(input.surfaceSelfRegularizationFactor>0.0)||!(input.mutualRegularizationFactor>0.0)||
       !(input.farFieldCutStepDeg>0.0)||!(input.farFieldIntegrationStepDeg>0.0))
    {error="Hybrid discretization parameters must be finite positive values";return false;}
    if(input.useFiniteSurfaceConductivity && (!(input.surfaceConductivitySPerM>0.0)||!(input.surfaceThicknessM>0.0)||!(input.surfaceRelativePermeability>0.0)))
    {error="Finite-conductivity surface parameters must be positive";return false;}
    for(const auto &t:input.triangles)
        if(!finiteVec3(t.a)||!finiteVec3(t.b)||!finiteVec3(t.c)){error="Hybrid PEC surface geometry contains NaN or Inf";return false;}
    for(const auto &w:input.wire.wires)
    {
        if(!finiteVec3(w.aM)||!finiteVec3(w.bM)||!finiteScalar(w.radiusM)){error="Hybrid wire geometry contains NaN or Inf";return false;}
        if(!(w.radiusM>0.0)){error="Hybrid wire radius must be positive";return false;}
    }
    for(const auto &feed:input.wire.feeds)
    {
        if(!finiteVec3(feed.positionM)||!finiteComplex(feed.voltageV)||!finiteScalar(feed.referenceOhm)){error="Hybrid wire feed contains NaN or Inf";return false;}
        if(!(feed.referenceOhm>0.0)){error="Hybrid wire-feed reference impedance must be positive";return false;}
    }
    for(const auto &d:input.dielectrics)
    {
        if(!finiteVec3(d.centerM)||!finiteVec3(d.uAxis)||!finiteVec3(d.vAxis)||!finiteVec3(d.normalAxis)||
           !finiteScalar(d.widthM)||!finiteScalar(d.heightM)||!finiteScalar(d.thicknessM)||
           !finiteScalar(d.relativePermittivity)||!finiteScalar(d.lossTangent)||!finiteScalar(d.fieldFillFactor))
        {error="Hybrid dielectric region contains NaN or Inf";return false;}
        if(!(d.widthM>0.0)||!(d.heightM>0.0)||!(d.thicknessM>0.0)||!(d.relativePermittivity>0.0)||
           !(d.lossTangent>=0.0)||!(d.fieldFillFactor>=0.0&&d.fieldFillFactor<=1.0)||
           !(NumericalEM::norm(d.uAxis)>1e-12)||!(NumericalEM::norm(d.vAxis)>1e-12)||!(NumericalEM::norm(d.normalAxis)>1e-12))
        {error="Hybrid dielectric dimensions/material/axes are invalid";return false;}
    }
    for(const auto &feed:input.surfaceReferencedFeeds)
    {
        if(!finiteVec3(feed.positionM)||!finiteComplex(feed.voltageV)||!finiteScalar(feed.referenceOhm)||!finiteScalar(feed.mappingToleranceM)||
           !finiteScalar(feed.coaxInnerRadiusM)||!finiteScalar(feed.coaxOuterRadiusM)||!finiteScalar(feed.coaxRelativePermittivity)||
           !finiteScalar(feed.coaxLossTangent)||!finiteScalar(feed.coaxLengthM))
        {error="Hybrid PEC-referenced feed contains NaN or Inf";return false;}
        if(!(feed.referenceOhm>0.0)||!(feed.mappingToleranceM>0.0)){error="Hybrid PEC-referenced feed impedance/tolerance must be positive";return false;}
        if(feed.referenceModel==PortReferenceModel::CoaxialReferencePlane &&
           (!(feed.coaxInnerRadiusM>0.0)||!(feed.coaxOuterRadiusM>feed.coaxInnerRadiusM)||!(feed.coaxRelativePermittivity>0.0)||
            !(feed.coaxLossTangent>=0.0)||!(feed.coaxLengthM>=0.0)))
        {error="Hybrid coax reference-plane parameters are invalid";return false;}
    }
    for(const auto &feed:input.differentialSurfaceFeeds)
    {
        if(!finiteVec3(feed.positivePositionM)||!finiteVec3(feed.negativePositionM)||!finiteComplex(feed.voltageV)||
           !finiteScalar(feed.referenceOhm)||!finiteScalar(feed.mappingToleranceM)||!finiteScalar(feed.footprintRadiusM))
        {error="Hybrid differential surface feed contains NaN or Inf";return false;}
        if(!(feed.referenceOhm>0.0)||!(feed.mappingToleranceM>0.0)||!(feed.footprintRadiusM>=0.0))
        {error="Hybrid differential surface-feed impedance/tolerance/footprint is invalid";return false;}
    }
    for(const auto &j:input.galvanicJunctions)
    {
        if(!finiteVec3(j.positionM)||!finiteScalar(j.mappingToleranceM)||!finiteScalar(j.surfaceCurrentSign))
        {error="Hybrid galvanic junction contains NaN or Inf";return false;}
        if(!(j.mappingToleranceM>0.0)||std::abs(j.surfaceCurrentSign)<1e-15)
        {error="Hybrid galvanic-junction tolerance/sign is invalid";return false;}
    }
    return true;
}

bool finiteResultCore(const Result &out)
{
    if(!finiteScalar(out.residualRelative)||!finiteScalar(out.acceptedPowerW)||!finiteScalar(out.radiatedPowerW)||
       !finiteScalar(out.conductorLossW)||!finiteScalar(out.directivityLinear)||!finiteScalar(out.directivityDbi)) return false;
    for(const auto &feed:out.feeds)
        if(!finiteComplex(feed.voltageV)||!finiteComplex(feed.currentA)||!finiteComplex(feed.inputImpedanceOhm)||
           !finiteComplex(feed.reflectionCoefficient)||!finiteScalar(feed.referenceOhm)||!finiteScalar(feed.returnLossDb)||
           !(finiteScalar(feed.vswr)||std::isinf(feed.vswr))) return false;
    return finiteVector(out.azimuthNormalizedFarField)&&finiteVector(out.elevationNormalizedFarField)&&finiteVector(out.farFieldNormalized)&&
           finiteVector(out.layeredAzimuthNormalizedFarField)&&finiteVector(out.layeredElevationNormalizedFarField)&&finiteVector(out.layeredFarFieldNormalized);
}

struct SommerfeldCacheCounters
{
    std::uint64_t faceHits=0,faceMisses=0,faceBuilds=0,faceEvictions=0;
    std::uint64_t exteriorHits=0,exteriorMisses=0,exteriorBuilds=0,exteriorEvictions=0;
    std::uint64_t internalHits=0,internalMisses=0,internalBuilds=0,internalEvictions=0;
    int faceEntries=0,exteriorEntries=0,internalEntries=0;
    double faceBuildMs=0.0,exteriorBuildMs=0.0,internalBuildMs=0.0;
};

thread_local SommerfeldCacheCounters gSommerfeldCacheCounters;

struct SommerfeldCacheSnapshot
{
    SommerfeldCacheCounters counters;
};

SommerfeldCacheSnapshot sommerfeldCacheSnapshot()
{
    return {gSommerfeldCacheCounters};
}

void applySommerfeldCacheDiagnostics(Result &out,const SommerfeldCacheSnapshot &start)
{
    const auto &a=start.counters;
    const auto &b=gSommerfeldCacheCounters;
    out.layeredSommerfeldFaceCacheHits=b.faceHits-a.faceHits;
    out.layeredSommerfeldExteriorCacheHits=b.exteriorHits-a.exteriorHits;
    out.layeredSommerfeldInternalCacheHits=b.internalHits-a.internalHits;
    out.layeredSommerfeldCacheHits=out.layeredSommerfeldFaceCacheHits+out.layeredSommerfeldExteriorCacheHits+out.layeredSommerfeldInternalCacheHits;
    out.layeredSommerfeldCacheMisses=(b.faceMisses-a.faceMisses)+(b.exteriorMisses-a.exteriorMisses)+(b.internalMisses-a.internalMisses);
    out.layeredSommerfeldCacheTableBuilds=(b.faceBuilds-a.faceBuilds)+(b.exteriorBuilds-a.exteriorBuilds)+(b.internalBuilds-a.internalBuilds);
    out.layeredSommerfeldCacheEvictions=(b.faceEvictions-a.faceEvictions)+(b.exteriorEvictions-a.exteriorEvictions)+(b.internalEvictions-a.internalEvictions);
    out.layeredSommerfeldFaceCacheEntries=b.faceEntries;
    out.layeredSommerfeldExteriorCacheEntries=b.exteriorEntries;
    out.layeredSommerfeldInternalCacheEntries=b.internalEntries;
    out.layeredSommerfeldCacheBuildTimeMs=(b.faceBuildMs-a.faceBuildMs)+(b.exteriorBuildMs-a.exteriorBuildMs)+(b.internalBuildMs-a.internalBuildMs);
}

template<class Key,class Value>
class BoundedLruCache
{
public:
    explicit BoundedLruCache(std::size_t capacity):m_capacity(std::max<std::size_t>(1,capacity)){}

    template<class Builder>
    const Value &get(const Key &key,Builder &&builder,std::uint64_t &hits,std::uint64_t &misses,
                     std::uint64_t &builds,std::uint64_t &evictions,int &entryCount,double &buildMs)
    {
        // Most matrix assemblies repeatedly query one slab/frequency table. Keep a
        // safe last-node fast path so those hot lookups avoid the ordered-map search.
        if(m_lastEntry && key==m_lastKey)
        {
            ++hits;m_lastEntry->age=++m_clock;entryCount=static_cast<int>(m_values.size());return m_lastEntry->value;
        }
        auto it=m_values.find(key);
        if(it!=m_values.end())
        {
            ++hits;it->second.age=++m_clock;m_lastKey=key;m_lastEntry=&it->second;
            entryCount=static_cast<int>(m_values.size());return it->second.value;
        }
        ++misses;
        if(m_values.size()>=m_capacity)
        {
            auto victim=std::min_element(m_values.begin(),m_values.end(),[](const auto &lhs,const auto &rhs){return lhs.second.age<rhs.second.age;});
            if(victim!=m_values.end())
            {
                if(m_lastEntry==&victim->second)m_lastEntry=nullptr;
                m_values.erase(victim);++evictions;
            }
        }
        const auto t0=std::chrono::steady_clock::now();
        Value value=builder();
        const auto t1=std::chrono::steady_clock::now();
        buildMs+=std::chrono::duration<double,std::milli>(t1-t0).count();
        ++builds;
        auto inserted=m_values.emplace(key,Entry{std::move(value),++m_clock}).first;
        m_lastKey=key;m_lastEntry=&inserted->second;
        entryCount=static_cast<int>(m_values.size());
        return inserted->second.value;
    }

private:
    struct Entry{Value value;std::uint64_t age=0;};
    std::size_t m_capacity=1;
    std::uint64_t m_clock=0;
    std::map<Key,Entry> m_values;
    Key m_lastKey{};
    Entry *m_lastEntry=nullptr;
};

Vec3 cross(const Vec3&a,const Vec3&b)
{return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}

double triangleArea(const PecSurfaceMom::Triangle3D&t)
{return 0.5*NumericalEM::norm(cross(t.b-t.a,t.c-t.a));}

Vec3 centroid(const PecSurfaceMom::Triangle3D&t)
{return {(t.a.x+t.b.x+t.c.x)/3.0,(t.a.y+t.b.y+t.c.y)/3.0,(t.a.z+t.b.z+t.c.z)/3.0};}

struct ComplexVec3{C x{0,0},y{0,0},z{0,0};};
ComplexVec3 &operator+=(ComplexVec3&a,const ComplexVec3&b){a.x+=b.x;a.y+=b.y;a.z+=b.z;return a;}
ComplexVec3 operator*(const Vec3&a,C s){return {a.x*s,a.y*s,a.z*s};}
ComplexVec3 operator*(const ComplexVec3&a,C s){return {a.x*s,a.y*s,a.z*s};}
C dotComplex(const Vec3&a,const ComplexVec3&b){return a.x*b.x+a.y*b.y+a.z*b.z;}
C dotComplex(const ComplexVec3&a,const Vec3&b){return a.x*b.x+a.y*b.y+a.z*b.z;}

bool complexLayeredMode(DielectricKernelModel model)
{
    return model==DielectricKernelModel::LayeredSlabSommerfeldComplexPowerGuard ||
           model==DielectricKernelModel::LayeredSlabSommerfeldPropagatingFarField ||
           model==DielectricKernelModel::LayeredSlabSommerfeldSurfaceWavePoleAudit ||
           model==DielectricKernelModel::LayeredSlabSommerfeldGroundedPecModeAudit;
}

bool layeredPropagatingFarFieldMode(DielectricKernelModel model)
{
    return model==DielectricKernelModel::LayeredSlabSommerfeldPropagatingFarField ||
           model==DielectricKernelModel::LayeredSlabSommerfeldSurfaceWavePoleAudit ||
           model==DielectricKernelModel::LayeredSlabSommerfeldGroundedPecModeAudit;
}

bool layeredSurfaceWavePoleAuditMode(DielectricKernelModel model)
{
    return model==DielectricKernelModel::LayeredSlabSommerfeldSurfaceWavePoleAudit ||
           model==DielectricKernelModel::LayeredSlabSommerfeldGroundedPecModeAudit;
}

bool layeredGroundedPecModeAuditMode(DielectricKernelModel model)
{
    return model==DielectricKernelModel::LayeredSlabSommerfeldGroundedPecModeAudit;
}
double normComplex2(const ComplexVec3&a){return std::norm(a.x)+std::norm(a.y)+std::norm(a.z);}
ComplexVec3 transverse(const ComplexVec3&a,const Vec3&rhat)
{const C p=dotComplex(a,rhat);return {a.x-rhat.x*p,a.y-rhat.y*p,a.z-rhat.z*p};}

struct MediumParams
{
    C k{0.0,0.0};
    C epsilon{NumericalEM::Epsilon0,0.0};
    double relativePermittivity = 1.0;
    double lossTangent = 0.0;
};

double axisCoordinate(const Vec3 &d,const Vec3 &axis)
{
    const Vec3 n=NumericalEM::normalized(axis);
    return NumericalEM::dot(d,n);
}

bool contains(const DielectricRegion &r,const Vec3 &p)
{
    const Vec3 d=p-r.centerM;
    const double u=std::abs(axisCoordinate(d,r.uAxis));
    const double v=std::abs(axisCoordinate(d,r.vAxis));
    const double n=std::abs(axisCoordinate(d,r.normalAxis));
    const double tol=1e-9+1e-6*std::max({r.widthM,r.heightM,r.thicknessM,1e-6});
    return u<=0.5*std::max(r.widthM,1e-12)+tol &&
           v<=0.5*std::max(r.heightM,1e-12)+tol &&
           n<=0.5*std::max(r.thicknessM,1e-12)+tol;
}

double segmentOverlapFraction(const DielectricRegion &r,const Vec3 &a,const Vec3 &b)
{
    const Vec3 d=b-a;
    if(NumericalEM::norm(d)<1e-15) return contains(r,a)?1.0:0.0;
    double t0=0.0,t1=1.0;
    const std::array<Vec3,3> axes{NumericalEM::normalized(r.uAxis),NumericalEM::normalized(r.vAxis),NumericalEM::normalized(r.normalAxis)};
    const std::array<double,3> half{0.5*std::max(r.widthM,1e-12),0.5*std::max(r.heightM,1e-12),0.5*std::max(r.thicknessM,1e-12)};
    for(int i=0;i<3;++i)
    {
        const double p0=NumericalEM::dot(a-r.centerM,axes[static_cast<std::size_t>(i)]);
        const double dv=NumericalEM::dot(d,axes[static_cast<std::size_t>(i)]);
        const double h=half[static_cast<std::size_t>(i)];
        if(std::abs(dv)<1e-18){if(std::abs(p0)>h)return 0.0;continue;}
        double ta=(-h-p0)/dv,tb=(h-p0)/dv;if(ta>tb)std::swap(ta,tb);
        t0=std::max(t0,ta);t1=std::min(t1,tb);if(t1<=t0)return 0.0;
    }
    return std::clamp(t1-t0,0.0,1.0);
}

MediumParams mediumForPair(const Input &input,const Vec3 &a,const Vec3 &b,double omega,double k0)
{
    MediumParams m; m.k={k0,0.0};
    if(!input.useEffectiveDielectricRegions || input.dielectrics.empty()) return m;
    const Vec3 mid=(a+b)*0.5;
    double er=1.0,tanD=0.0;
    for(const auto &r:input.dielectrics)
    {
        double geometricWeight=0.0;
        if(input.dielectricKernelModel==DielectricKernelModel::MidpointFill)
            geometricWeight=contains(r,mid)?1.0:0.0;
        else
            geometricWeight=segmentOverlapFraction(r,a,b);
        if(!(geometricWeight>0.0)) continue;
        const double fill=std::clamp(r.fieldFillFactor,0.0,1.0)*geometricWeight;
        const double candidateEr=1.0+fill*(std::max(1.0,r.relativePermittivity)-1.0);
        if(candidateEr>=er)
        {
            er=candidateEr;
            tanD=fill*std::max(0.0,r.lossTangent);
        }
    }
    m.relativePermittivity=er; m.lossTangent=tanD;
    m.epsilon=NumericalEM::Epsilon0*er*C{1.0,-tanD};
    m.k=omega*std::sqrt(C{NumericalEM::Mu0,0.0}*m.epsilon);
    return m;
}

C finiteSheetImpedance(const Input &input,double omega)
{
    if(!input.useFiniteSurfaceConductivity || !(omega>0.0) || !(input.surfaceConductivitySPerM>0.0) || !(input.surfaceThicknessM>0.0)) return {0.0,0.0};
    const double mu=NumericalEM::Mu0*std::max(1e-6,input.surfaceRelativePermeability);
    const double sigma=input.surfaceConductivitySPerM;
    const C gamma=std::sqrt(J*omega*mu*sigma);
    const C intrinsic=gamma/sigma;
    const C gt=gamma*input.surfaceThicknessM;
    if(std::abs(gt)<1e-9) return {1.0/(sigma*input.surfaceThicknessM),0.0};
    const C th=std::tanh(gt);
    if(std::abs(th)<1e-18) return intrinsic;
    return intrinsic/th;
}

struct SolveStats{double minPivot=std::numeric_limits<double>::infinity();double maxPivot=0.0;};
bool solveDense(std::vector<std::vector<C>> a,std::vector<C> b,std::vector<C>&x,SolveStats&stats)
{
    const int n=static_cast<int>(b.size());if(static_cast<int>(a.size())!=n)return false;
    for(int k=0;k<n;++k)
    {
        int pivot=k;double best=std::abs(a[k][k]);
        for(int i=k+1;i<n;++i){const double q=std::abs(a[i][k]);if(q>best){best=q;pivot=i;}}
        if(!(best>1e-18)||!std::isfinite(best))return false;
        if(pivot!=k){std::swap(a[pivot],a[k]);std::swap(b[pivot],b[k]);}
        stats.minPivot=std::min(stats.minPivot,best);stats.maxPivot=std::max(stats.maxPivot,best);
        const C diag=a[k][k];
        for(int i=k+1;i<n;++i)
        {
            const C f=a[i][k]/diag;if(std::abs(f)==0.0)continue;a[i][k]={0,0};
            for(int q=k+1;q<n;++q)a[i][q]-=f*a[k][q];
            b[i]-=f*b[k];
        }
    }
    x.assign(n,{0,0});
    for(int i=n-1;i>=0;--i){C s=b[i];for(int q=i+1;q<n;++q)s-=a[i][q]*x[q];if(std::abs(a[i][i])<1e-18)return false;x[i]=s/a[i][i];}
    return true;
}

struct NullspaceBasis
{
    int rank=0;
    std::vector<int> independentRows;
    std::vector<std::vector<double>> t; // physical wire currents = T * reduced currents
};

bool buildConstraintNullspace(const std::vector<std::vector<double>>&constraints,int n,NullspaceBasis&out)
{
    out={};if(n<=0)return false;
    if(constraints.empty())
    {
        out.t.assign(static_cast<std::size_t>(n),std::vector<double>(static_cast<std::size_t>(n),0.0));
        for(int i=0;i<n;++i)out.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)]=1.0;
        return true;
    }
    auto a=constraints;for(auto&r:a)r.resize(static_cast<std::size_t>(n),0.0);
    std::vector<int>rowIds(a.size());std::iota(rowIds.begin(),rowIds.end(),0);
    std::vector<int>pivotCols;int pr=0;constexpr double tol=1e-12;
    for(int col=0;col<n&&pr<static_cast<int>(a.size());++col)
    {
        int best=-1;double mag=tol;for(int r=pr;r<static_cast<int>(a.size());++r){const double v=std::abs(a[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)]);if(v>mag){mag=v;best=r;}}
        if(best<0)continue;
        if(best!=pr){std::swap(a[static_cast<std::size_t>(best)],a[static_cast<std::size_t>(pr)]);std::swap(rowIds[static_cast<std::size_t>(best)],rowIds[static_cast<std::size_t>(pr)]);}
        const double d=a[static_cast<std::size_t>(pr)][static_cast<std::size_t>(col)];for(int j=0;j<n;++j)a[static_cast<std::size_t>(pr)][static_cast<std::size_t>(j)]/=d;
        for(int r=0;r<static_cast<int>(a.size());++r)if(r!=pr){const double f=a[static_cast<std::size_t>(r)][static_cast<std::size_t>(col)];if(std::abs(f)<=tol)continue;for(int j=0;j<n;++j)a[static_cast<std::size_t>(r)][static_cast<std::size_t>(j)]-=f*a[static_cast<std::size_t>(pr)][static_cast<std::size_t>(j)];}
        pivotCols.push_back(col);out.independentRows.push_back(rowIds[static_cast<std::size_t>(pr)]);++pr;
    }
    out.rank=pr;const int nr=n-pr;if(nr<=0)return false;
    std::vector<bool>pivot(static_cast<std::size_t>(n),false);for(int c:pivotCols)pivot[static_cast<std::size_t>(c)]=true;
    std::vector<int>freeCols;for(int c=0;c<n;++c)if(!pivot[static_cast<std::size_t>(c)])freeCols.push_back(c);
    out.t.assign(static_cast<std::size_t>(n),std::vector<double>(static_cast<std::size_t>(nr),0.0));
    for(int k=0;k<nr;++k)
    {
        std::vector<double>x(static_cast<std::size_t>(n),0.0);x[static_cast<std::size_t>(freeCols[static_cast<std::size_t>(k)])]=1.0;
        for(int r=pr-1;r>=0;--r){const int pc=pivotCols[static_cast<std::size_t>(r)];double s=0.0;for(int c=0;c<n;++c)if(c!=pc)s+=a[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)]*x[static_cast<std::size_t>(c)];x[static_cast<std::size_t>(pc)]=-s;}
        double n2=0.0;for(double v:x)n2+=v*v;const double inv=1.0/std::sqrt(std::max(n2,1e-30));for(int i=0;i<n;++i)out.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)]=x[static_cast<std::size_t>(i)]*inv;
    }
    for(const auto&r:constraints)for(int k=0;k<nr;++k){double s=0.0;for(int i=0;i<n&&i<static_cast<int>(r.size());++i)s+=r[static_cast<std::size_t>(i)]*out.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)];if(std::abs(s)>1e-9)return false;}
    return true;
}

std::complex<double> projectedEfieKernel(const Input &input,const Vec3&rObs,const Vec3&tObs,const Vec3&rSrc,const Vec3&tSrc,
                                         double regularizationRadiusM,double k0,double omega)
{
    const Vec3 d=rObs-rSrc;const double a=std::max(regularizationRadiusM,1e-12);
    const double R=std::sqrt(NumericalEM::dot(d,d)+a*a);
    const auto med=mediumForPair(input,rObs,rSrc,omega,k0);
    C kR=med.k*R;if(std::abs(kR)<1e-15)kR=C{1e-15,0.0};
    const Vec3 q=d*(1.0/R);const C G=std::exp(-J*med.k*R)/(4.0*NumericalEM::Pi*R);
    const C A=1.0-J/kR-1.0/(kR*kR);const C B=-1.0+3.0*J/kR+3.0/(kR*kR);
    const C projection=A*NumericalEM::dot(tObs,tSrc)+B*NumericalEM::dot(tObs,q)*NumericalEM::dot(tSrc,q);
    return J*omega*NumericalEM::Mu0*G*projection;
}

struct VertexKey{std::int64_t x=0,y=0,z=0;bool operator<(const VertexKey&o)const{return std::tie(x,y,z)<std::tie(o.x,o.y,o.z);}};
VertexKey keyFor(const Vec3&p,double tol)
{const double s=1.0/std::max(tol,1e-12);return {static_cast<std::int64_t>(std::llround(p.x*s)),static_cast<std::int64_t>(std::llround(p.y*s)),static_cast<std::int64_t>(std::llround(p.z*s))};}

struct TriData{PecSurfaceMom::Triangle3D tri;std::array<int,3> vertex{-1,-1,-1};double area=0.0;Vec3 centroid{};};
struct EdgeKey{int a=-1,b=-1;bool operator<(const EdgeKey&o)const{return std::tie(a,b)<std::tie(o.a,o.b);}};
struct EdgeUse{int triangle=-1;int freeLocal=-1;};
struct BasisSupport{int triangle=-1;int freeVertex=-1;double sign=1.0;};
struct BasisData{int edgeV0=-1,edgeV1=-1;double edgeLength=0.0;BasisSupport plus{},minus{};};
struct QuadPoint{Vec3 p{};double weight=0.0;};

std::array<QuadPoint,3> triangleQuadrature(const TriData&t)
{
    constexpr std::array<std::array<double,3>,3>b{{{{2.0/3.0,1.0/6.0,1.0/6.0}},{{1.0/6.0,2.0/3.0,1.0/6.0}},{{1.0/6.0,1.0/6.0,2.0/3.0}}}};
    std::array<QuadPoint,3>q{};
    for(int i=0;i<3;++i){q[i].p={b[i][0]*t.tri.a.x+b[i][1]*t.tri.b.x+b[i][2]*t.tri.c.x,b[i][0]*t.tri.a.y+b[i][1]*t.tri.b.y+b[i][2]*t.tri.c.y,b[i][0]*t.tri.a.z+b[i][1]*t.tri.b.z+b[i][2]*t.tri.c.z};q[i].weight=t.area/3.0;}
    return q;
}

std::array<QuadPoint,7> triangleQuadratureNear(const TriData&t)
{
    // Dunavant degree-5 seven-point rule.  We reserve it for self/adjacent
    // triangle pairs, where the Green kernel changes too rapidly for the
    // inexpensive three-point rule but global composite refinement would be
    // unnecessarily expensive.
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
    int n=0;
    for(int va:a.vertex) for(int vb:b.vertex) if(va==vb){++n;break;}
    return n;
}

std::vector<QuadPoint> triangleQuadratureComposite1(const TriData &t)
{
    const Vec3 ab=(t.tri.a+t.tri.b)*0.5,bc=(t.tri.b+t.tri.c)*0.5,ca=(t.tri.c+t.tri.a)*0.5;
    const std::array<PecSurfaceMom::Triangle3D,4> child{{
        {t.tri.a,ab,ca,t.tri.surfaceIndex},{ab,t.tri.b,bc,t.tri.surfaceIndex},
        {ca,bc,t.tri.c,t.tri.surfaceIndex},{ab,bc,ca,t.tri.surfaceIndex}}};
    std::vector<QuadPoint> out;out.reserve(12);
    for(const auto &ct:child)
    {
        TriData d;d.tri=ct;d.area=triangleArea(ct);d.centroid=(ct.a+ct.b+ct.c)*(1.0/3.0);
        const auto q=triangleQuadrature(d);out.insert(out.end(),q.begin(),q.end());
    }
    return out;
}


Vec3 freeVertexPosition(const TriData&t,int global){if(t.vertex[0]==global)return t.tri.a;if(t.vertex[1]==global)return t.tri.b;return t.tri.c;}
Vec3 rwgValue(const BasisData&b,const BasisSupport&s,const TriData&t,const Vec3&r)
{const Vec3 rf=freeVertexPosition(t,s.freeVertex);const double factor=b.edgeLength/(2.0*t.area);return s.sign>0.0?(r-rf)*factor:(rf-r)*factor;}
double rwgDivergence(const BasisData&b,const BasisSupport&s,const TriData&t){return s.sign*b.edgeLength/t.area;}

double surfaceBasisDotIntegral(const BasisData &m,const BasisData &n,const std::vector<TriData> &tri)
{
    double integral=0.0;
    for(const auto &sm:{m.plus,m.minus}) for(const auto &sn:{n.plus,n.minus})
    {
        if(sm.triangle<0 || sn.triangle<0 || sm.triangle!=sn.triangle) continue;
        const auto &t=tri[static_cast<std::size_t>(sm.triangle)];
        for(const auto &qp:triangleQuadrature(t))
            integral += NumericalEM::dot(rwgValue(m,sm,t,qp.p),rwgValue(n,sn,t,qp.p))*qp.weight;
    }
    return integral;
}

C green(const Vec3&r,const Vec3&rp,C k,double regularization)
{const Vec3 d=r-rp;const double R=std::sqrt(NumericalEM::dot(d,d)+regularization*regularization);return std::exp(-J*k*R)/(4.0*NumericalEM::Pi*R);}

// v5.25 layered-substrate electrostatic correction ---------------------------------
//
// The printed-antenna geometry already contains the patch and ground as explicit PEC
// RWG sheets.  A PEC image plane in the dielectric kernel would therefore impose the
// conductor boundary twice.  Instead, model only the finite dielectric slab between two
// outer free-space half-spaces.  In the electrostatic spectral domain, the two interface
// potentials obey
//
//   [sigma_t]     [ eps0 + eps2*coth(kh)   -eps2*csch(kh) ] [phi_t]
//   [sigma_b] = k [ -eps2*csch(kh)         eps0 + eps2*coth(kh) ] [phi_b].
//
// For eps0 on both outer sides this inverse admits a rapidly convergent real-space image
// series.  We derive a residual quasi-static correction from that series while retaining
// the audited dynamic effective-medium baseline.  This captures part of the dominant
// microstrip dielectric-interface/fringing capacitance without pretending to be the full
// frequency-domain Sommerfeld dyadic Green function.
struct LayeredSurfacePair
{
    const DielectricRegion *region=nullptr;
    int observationFace=0; // +1 = +normal face, -1 = -normal face
    int sourceFace=0;
};

bool pointOnDielectricFace(const DielectricRegion &r,const Vec3 &p,int &face,double &faceError)
{
    const Vec3 u=NumericalEM::normalized(r.uAxis),v=NumericalEM::normalized(r.vAxis),n=NumericalEM::normalized(r.normalAxis);
    const Vec3 d=p-r.centerM;
    const double uu=NumericalEM::dot(d,u),vv=NumericalEM::dot(d,v),zz=NumericalEM::dot(d,n);
    const double lateralTol=1e-9+1e-6*std::max({r.widthM,r.heightM,r.thicknessM,1e-6});
    if(std::abs(uu)>0.5*std::max(r.widthM,1e-12)+lateralTol ||
       std::abs(vv)>0.5*std::max(r.heightM,1e-12)+lateralTol) return false;
    const double half=0.5*std::max(r.thicknessM,1e-12);
    const double ep=std::abs(zz-half),em=std::abs(zz+half);
    face=ep<=em?+1:-1;faceError=std::min(ep,em);
    const double normalTol=std::max(2e-8,2e-5*std::max(r.thicknessM,1e-9));
    return faceError<=normalTol;
}

LayeredSurfacePair layeredSurfacePairFor(const Input &input,const Vec3 &rObs,const Vec3 &rSrc)
{
    LayeredSurfacePair best;double bestScore=std::numeric_limits<double>::infinity();
    if(!input.useEffectiveDielectricRegions ||
       (input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabQuasiStatic &&
        input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldScalar &&
        input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldTeVector &&
        input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldCrossFace &&
        input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed &&
        input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight &&
        (input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldInternalLayer && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedNormal && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal && !complexLayeredMode(input.dielectricKernelModel)))) return best;
    for(const auto &r:input.dielectrics)
    {
        if(!(r.thicknessM>0.0) || !(r.widthM>0.0) || !(r.heightM>0.0) || !(r.relativePermittivity>=1.0)) continue;
        int fo=0,fs=0;double eo=0.0,es=0.0;
        if(!pointOnDielectricFace(r,rObs,fo,eo) || !pointOnDielectricFace(r,rSrc,fs,es)) continue;
        const double score=(eo+es)/std::max(r.thicknessM,1e-12);
        if(score<bestScore){bestScore=score;best={&r,fo,fs};}
    }
    return best;
}

// v5.32 exterior-height extension --------------------------------------------------
//
// The face-only tables above are sufficient for patch/ground RWG pairs but do not
// describe a wire, probe or observation point displaced into either exterior air
// half-space.  For a symmetric air/slab/air stack, an exterior source/observer
// pair acquires the standard propagation factor exp[-gamma1(d_obs+d_src)] on top
// of the face reflection/transmission spectrum.  This helper deliberately accepts
// only points on or OUTSIDE the two slab faces. Points inside the dielectric are
// left on the audited effective-medium path until the internal-layer source dyadic
// is implemented.
struct LayeredExteriorPair
{
    const DielectricRegion *region=nullptr;
    int observationSide=0; // +1 above +normal face, -1 below -normal face
    int sourceSide=0;
    double observationDistanceM=0.0;
    double sourceDistanceM=0.0;
};

bool pointInExteriorHalfSpaceOfSlab(const DielectricRegion &r,const Vec3 &p,int &side,double &distanceM)
{
    const Vec3 u=NumericalEM::normalized(r.uAxis),v=NumericalEM::normalized(r.vAxis),n=NumericalEM::normalized(r.normalAxis);
    const Vec3 d=p-r.centerM;
    const double uu=NumericalEM::dot(d,u),vv=NumericalEM::dot(d,v),zz=NumericalEM::dot(d,n);
    const double lateralTol=1e-9+1e-6*std::max({r.widthM,r.heightM,r.thicknessM,1e-6});
    if(std::abs(uu)>0.5*std::max(r.widthM,1e-12)+lateralTol ||
       std::abs(vv)>0.5*std::max(r.heightM,1e-12)+lateralTol) return false;
    const double half=0.5*std::max(r.thicknessM,1e-12);
    const double normalTol=std::max(2e-8,2e-5*std::max(r.thicknessM,1e-9));
    if(zz>=half-normalTol){side=+1;distanceM=std::max(0.0,zz-half);return true;}
    if(zz<=-half+normalTol){side=-1;distanceM=std::max(0.0,-half-zz);return true;}
    return false;
}

LayeredExteriorPair layeredExteriorPairFor(const Input &input,const Vec3 &rObs,const Vec3 &rSrc)
{
    LayeredExteriorPair best;
    if(!input.useEffectiveDielectricRegions ||
       (input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight &&
        (input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldInternalLayer && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedNormal && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal && !complexLayeredMode(input.dielectricKernelModel)))) return best;
    double bestScore=std::numeric_limits<double>::infinity();
    for(const auto &r:input.dielectrics)
    {
        if(!(r.thicknessM>0.0) || !(r.widthM>0.0) || !(r.heightM>0.0) || !(r.relativePermittivity>=1.0)) continue;
        int so=0,ss=0;double dobs=0.0,dsrc=0.0;
        if(!pointInExteriorHalfSpaceOfSlab(r,rObs,so,dobs) ||
           !pointInExteriorHalfSpaceOfSlab(r,rSrc,ss,dsrc)) continue;
        const double score=(dobs+dsrc)/std::max(r.thicknessM,1e-12);
        if(score<bestScore){bestScore=score;best={&r,so,ss,dobs,dsrc};}
    }
    return best;
}

// v5.33 internal-layer transition --------------------------------------------------
//
// The 5.32 exterior Green is exact only when both source and observation are on or
// outside the air/slab interfaces.  For a tangential current located inside the
// dielectric we instead use the multiple-reflection cavity Green in medium 2.  The
// direct homogeneous layer term is already present in the effective-medium baseline;
// therefore the spectral tables below contain only the reflected residual.  A point
// on either slab face is accepted as the limiting z=0 or z=h observation/source, but
// at least one point must be strictly inside so the audited 5.31 face/face operator is
// left byte-for-byte on its existing path.
struct LayeredInternalPair
{
    const DielectricRegion *region=nullptr;
    double observationZM=0.0; // coordinate from -normal face: 0..h
    double sourceZM=0.0;
    bool observationStrictInterior=false;
    bool sourceStrictInterior=false;
};

bool pointInDielectricLayer(const DielectricRegion &r,const Vec3 &p,double &zFromBottomM,bool &strictInterior)
{
    const Vec3 u=NumericalEM::normalized(r.uAxis),v=NumericalEM::normalized(r.vAxis),n=NumericalEM::normalized(r.normalAxis);
    const Vec3 d=p-r.centerM;
    const double uu=NumericalEM::dot(d,u),vv=NumericalEM::dot(d,v),zz=NumericalEM::dot(d,n);
    const double lateralTol=1e-9+1e-6*std::max({r.widthM,r.heightM,r.thicknessM,1e-6});
    if(std::abs(uu)>0.5*std::max(r.widthM,1e-12)+lateralTol ||
       std::abs(vv)>0.5*std::max(r.heightM,1e-12)+lateralTol) return false;
    const double h=std::max(r.thicknessM,1e-12),half=0.5*h;
    const double normalTol=std::max(2e-8,2e-5*std::max(r.thicknessM,1e-9));
    if(zz<-half-normalTol || zz>half+normalTol) return false;
    zFromBottomM=std::clamp(zz+half,0.0,h);
    strictInterior=(zFromBottomM>normalTol && zFromBottomM<h-normalTol);
    return true;
}

LayeredInternalPair layeredInternalPairFor(const Input &input,const Vec3 &rObs,const Vec3 &rSrc)
{
    LayeredInternalPair best;
    if(!input.useEffectiveDielectricRegions ||
       (input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldInternalLayer && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedNormal && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal && !complexLayeredMode(input.dielectricKernelModel))) return best;
    double bestScore=std::numeric_limits<double>::infinity();
    for(const auto &r:input.dielectrics)
    {
        if(!(r.thicknessM>0.0) || !(r.widthM>0.0) || !(r.heightM>0.0) || !(r.relativePermittivity>=1.0)) continue;
        double zo=0.0,zs=0.0;bool io=false,is=false;
        if(!pointInDielectricLayer(r,rObs,zo,io) || !pointInDielectricLayer(r,rSrc,zs,is)) continue;
        if(!io && !is) continue;
        const double h=std::max(r.thicknessM,1e-12);
        const double score=(std::abs(zo-zs)+0.25*(io?0.0:1.0)+0.25*(is?0.0:1.0))/h;
        if(score<bestScore){bestScore=score;best={&r,zo,zs,io,is};}
    }
    return best;
}

C staticGreenFromDistance(double distanceM)
{
    const double R=std::max(distanceM,1e-15);
    return C{1.0/(4.0*NumericalEM::Pi*R),0.0};
}

int layeredImageTermCount(C reflection)
{
    // r^2 is the round-trip factor.  Stop after the omitted tail is comfortably below
    // ordinary RWG quadrature error, while bounding pathological very-high-epsilon cases.
    const double q=std::min(0.999999,std::norm(reflection));
    if(q<=1e-12) return 3;
    // Dense RWG assembly evaluates this kernel many times. Four to six round trips
    // retain the dominant finite-slab/fringing correction for ordinary PCB eps_r
    // while keeping the educational solver interactive.
    const int n=static_cast<int>(std::ceil(std::log(1e-2)/std::log(q)));
    return std::clamp(n,3,6);
}

constexpr int SommerfeldRadialSampleCount=80;
constexpr int SommerfeldQuadratureOrder=16;
constexpr double SommerfeldLimitingLossTangent=1e-5;

void populateLayeredDiagnostics(const Input &input,Result &out)
{
    if(!input.useEffectiveDielectricRegions ||
       (input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabQuasiStatic &&
        input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldScalar &&
        input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldTeVector &&
        input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldCrossFace &&
        input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed &&
        input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight &&
        (input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldInternalLayer && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedNormal && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal && !complexLayeredMode(input.dielectricKernelModel)))) return;
    int regions=0,maxTerms=0;
    bool exteriorMutualGeometry=false;
    bool exteriorTangentialGeometry=false;
    bool internalTangentialGeometry=false;
    bool internalNormalCurrentGeometry=false;
    for(const auto &r:input.dielectrics)
    {
        if(!(r.thicknessM>0.0) || !(r.widthM>0.0) || !(r.heightM>0.0) || !(r.relativePermittivity>=1.0)) continue;
        bool touchesSurface=false;
        for(const auto &t:input.triangles)
        {
            int face=0;double faceError=0.0;
            if(pointOnDielectricFace(r,centroid(t),face,faceError)){touchesSurface=true;break;}
        }
        if(!touchesSurface) continue;
        if(input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight ||
           input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer ||
        (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel))))
        {
            const Vec3 n=NumericalEM::normalized(r.normalAxis);
            for(const auto &w:input.wire.wires)
            {
                const Vec3 mid=(w.aM+w.bM)*0.5;int side=0;double dist=0.0;
                const Vec3 tangent=NumericalEM::normalized(w.bM-w.aM);
                if(pointInExteriorHalfSpaceOfSlab(r,mid,side,dist) && dist>1e-9)
                {
                    exteriorMutualGeometry=true;
                    if(std::abs(NumericalEM::dot(tangent,n))<=0.15) exteriorTangentialGeometry=true;
                }
                if(input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer ||
        (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel))))
                {
                    double z=0.0;bool strict=false;
                    if(pointInDielectricLayer(r,mid,z,strict) && strict)
                    {
                        if(std::abs(NumericalEM::dot(tangent,n))<=0.15) internalTangentialGeometry=true;
                        else internalNormalCurrentGeometry=true;
                    }
                }
            }
        }
        if(input.dielectricKernelModel==DielectricKernelModel::LayeredSlabQuasiStatic)
        {
            const C epsr=std::max(1.0,r.relativePermittivity)*C{1.0,-std::max(0.0,r.lossTangent)};
            const C refl=(epsr-C{1.0,0.0})/(epsr+C{1.0,0.0});
            maxTerms=std::max(maxTerms,layeredImageTermCount(refl));
        }
        ++regions;
    }
    out.layeredSlabRegionCount=regions;
    if(input.dielectricKernelModel==DielectricKernelModel::LayeredSlabQuasiStatic)
    {
        out.layeredImageSeriesTerms=maxTerms;
        out.layeredSlabQuasiStaticUsed=regions>0;
    }
    else
    {
        out.layeredSlabSommerfeldScalarUsed=regions>0;
        const bool teVector=input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldTeVector ||
                            input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldCrossFace ||
                            input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed ||
                            input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight ||
                            input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer ||
                             (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel)));
        const bool crossFace=input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldCrossFace ||
                             input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed ||
                             input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight ||
                             input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer ||
                             (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel)));
        const bool longitudinalHed=input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed ||
                                   input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight ||
                                   input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer ||
                             (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel)));
        const bool exteriorHeight=input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight ||
                                  input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer ||
                             (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel)));
        const bool vedNormal=input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel));
        const bool internalLayer=input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer || vedNormal;
        out.layeredSlabSommerfeldTeVectorUsed=teVector && regions>0;
        out.layeredSlabSommerfeldCrossFaceUsed=crossFace && regions>0;
        out.layeredSlabSommerfeldLongitudinalHedUsed=longitudinalHed && regions>0;
        out.layeredSlabSommerfeldExteriorHeightUsed=exteriorHeight && regions>0;
        out.layeredSlabSommerfeldInternalLayerUsed=internalLayer && regions>0;
        out.layeredSlabSommerfeldVedNormalUsed=vedNormal && regions>0;
        out.layeredSommerfeldExteriorWireSurfaceVectorUsed=exteriorHeight && regions>0 && exteriorMutualGeometry;
        out.layeredSommerfeldExteriorHedScalarDiagnosticEvaluated=exteriorHeight && regions>0 && exteriorMutualGeometry;
        out.layeredSommerfeldInternalWireSurfaceVectorUsed=internalLayer && regions>0 && internalTangentialGeometry;
        out.layeredSommerfeldWireSurfaceScalarGradientUsed=internalLayer && regions>0 && (exteriorTangentialGeometry || internalTangentialGeometry);
        out.layeredSommerfeldInternalNormalCurrentGuarded=!vedNormal && internalLayer && regions>0 && internalNormalCurrentGeometry;
        out.layeredSommerfeldVedNormalVectorUsed=vedNormal && regions>0 && internalNormalCurrentGeometry;
        out.layeredSommerfeldVedScalarGradientUsed=vedNormal && regions>0 && internalNormalCurrentGeometry;
        out.layeredSommerfeldHedFullGradientUsed=vedNormal && regions>0 && internalNormalCurrentGeometry;
        const bool vedOffDiagonal=(input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel));
        out.layeredSommerfeldVedOffDiagonalGuarded=!vedOffDiagonal && vedNormal && regions>0 && internalNormalCurrentGeometry;
        out.layeredSommerfeldVedOffDiagonalVectorUsed=vedOffDiagonal && regions>0 && internalNormalCurrentGeometry;
        out.layeredSommerfeldVedOffDiagonalReciprocityGuardUsed=vedOffDiagonal && regions>0 && internalNormalCurrentGeometry;
        out.layeredSommerfeldVectorPotentialUsed=teVector && regions>0;
        out.layeredSommerfeldCrossFaceVectorUsed=crossFace && regions>0;
        out.layeredSommerfeldCrossFaceAllPairAssemblyUsed=crossFace && regions>0;
        out.layeredSommerfeldHedScalarDiagnosticEvaluated=teVector && regions>0;
        out.layeredSommerfeldLongitudinalHedScalarUsed=longitudinalHed && regions>0;
        out.layeredSommerfeldCrossFaceHedScalarUsed=longitudinalHed && regions>0;
        out.layeredSommerfeldFullComplexScalarUsed=false;
        out.layeredSommerfeldReactiveProjectionUsed=regions>0;
        if(regions>0)
        {
            out.layeredSommerfeldRadialSamples=SommerfeldRadialSampleCount;
            out.layeredSommerfeldQuadratureOrder=SommerfeldQuadratureOrder;
            out.layeredSommerfeldLimitingLossTangent=SommerfeldLimitingLossTangent;
        }
    }
}

// v5.26 dynamic scalar-potential Sommerfeld correction -----------------------------
//
// For a symmetric air / dielectric / air slab the scalar-potential TM spectrum can
// be written in terms of the vertical propagation constants
//
//   gamma_i = sqrt(k_rho^2-k_i^2),   Re(gamma_i)>=0,
//
// and the potential reflection coefficient at the air->dielectric interface
//
//   r_TM^phi = (gamma_2-eps_r*gamma_1)/(gamma_2+eps_r*gamma_1).
//
// With E=exp(-2 gamma_2 h), the finite-slab coefficients at its two faces are
//
//   R = r(1-E)/(1-r^2 E),
//   T = (1-r^2) exp(-gamma_2 h)/(1-r^2 E).
//
// Their high-k_rho limit reproduces the 5.25 electrostatic image series.  For a
// same-face interaction the zero-distance interface image r_inf is deliberately
// removed before the inverse Hankel transform: the audited Duffy direct singular
// coefficient therefore remains unchanged.  The finite-distance reflected part is
// smooth.  For opposite faces T already decays exponentially and is transformed in
// full.  Version 5.26 uses this as a scalar-only, passivity-guarded transition.
// Version 5.29 reuses the same spectral infrastructure to add the same-face HED TE
// vector-potential contribution while retaining the scalar guard until the remaining
// transmitted/longitudinal mixed-potential terms pass passive-network validation.
struct SommerfeldSpectrum
{
    C gamma1{0.0,0.0};
    C gamma2{0.0,0.0};
    C epsr{1.0,0.0};
    C tmInterfaceReflectionAirToLayer{0.0,0.0};
    C teInterfaceReflectionAirToLayer{0.0,0.0};
    // TM scalar-potential coefficient. For tangential electric-current vector
    // potential this is also the TM projector coefficient; it equals -R_TM(E).
    C reflection{0.0,0.0};
    C transmission{0.0,0.0};
    // Tangential TE vector-potential coefficients.
    C teReflection{0.0,0.0};
    C teTransmission{0.0,0.0};
    C reflectionInfinity{0.0,0.0};
};

C outgoingVerticalGamma(double kRho,C k)
{
    C g=std::sqrt(C{kRho*kRho,0.0}-k*k);
    if(g.real()<0.0 || (std::abs(g.real())<1e-15 && g.imag()<0.0)) g=-g;
    return g;
}

SommerfeldSpectrum sommerfeldSpectrum(const DielectricRegion &slab,double k0,double kRho)
{
    const double er=std::max(1.0,slab.relativePermittivity);
    // The tiny additional dielectric loss implements the limiting-absorption contour
    // for nominally lossless slabs.  It is negligible compared with ordinary PCB loss
    // but prevents branch/pole samples from landing exactly on the real integration axis.
    const double tanD=std::max(0.0,slab.lossTangent)+SommerfeldLimitingLossTangent;
    const C epsr=er*C{1.0,-tanD};
    const C k1=k0*C{1.0,-1e-8};
    const C k2=k0*std::sqrt(epsr);
    const C g1=outgoingVerticalGamma(kRho,k1),g2=outgoingVerticalGamma(kRho,k2);
    const C denInterface=g2+epsr*g1;
    C r{0.0,0.0};
    if(std::abs(denInterface)>1e-30) r=(g2-epsr*g1)/denInterface;
    const double h=std::max(slab.thicknessM,1e-15);
    const C roundTrip=std::exp(-2.0*g2*h);
    C den=C{1.0,0.0}-r*r*roundTrip;
    if(std::abs(den)<1e-18) den+=C{1e-18,1e-18};

    C rTe{0.0,0.0};
    const C denTeInterface=g1+g2;
    if(std::abs(denTeInterface)>1e-30) rTe=(g1-g2)/denTeInterface;
    C denTe=C{1.0,0.0}-rTe*rTe*roundTrip;
    if(std::abs(denTe)<1e-18) denTe+=C{1e-18,1e-18};

    SommerfeldSpectrum out;
    out.gamma1=g1;
    out.gamma2=g2;
    out.epsr=epsr;
    out.tmInterfaceReflectionAirToLayer=r;
    out.teInterfaceReflectionAirToLayer=rTe;
    out.reflection=r*(C{1.0,0.0}-roundTrip)/den;
    out.transmission=(C{1.0,0.0}-r*r)*std::exp(-g2*h)/den;
    out.teReflection=rTe*(C{1.0,0.0}-roundTrip)/denTe;
    out.teTransmission=(C{1.0,0.0}-rTe*rTe)*std::exp(-g2*h)/denTe;
    out.reflectionInfinity=(C{1.0,0.0}-epsr)/(C{1.0,0.0}+epsr);
    return out;
}


// v5.38 guided-mode pole audit ----------------------------------------------------
//
// The same finite dielectric-slab reflection coefficients used by the 5.37
// propagating far-field contain discrete guided-mode poles where
//      D_p(k_rho) = 1 - r_p^2 exp(-2 gamma_2 h) = 0,  p in {TE,TM}.
// We locate those poles in complex transverse wavenumber and report the residue of
// the reflection coefficient.  This is deliberately a *spectral audit only*:
// the application's PEC ground is an explicit RWG sheet and is therefore absent
// from D_p.  Assigning a physical surface-wave power from this bare-slab residue
// would mix two different boundary models.  A grounded-substrate modal
// normalization must be added before P_surface-wave participates in closure.
struct SlabPoleSpectralData
{
    C denominator{1.0,0.0};
    C numerator{0.0,0.0};
};

C outgoingVerticalGammaComplex(C kRho,C k)
{
    C g=std::sqrt(kRho*kRho-k*k);
    if(g.real()<0.0 || (std::abs(g.real())<1e-15 && g.imag()<0.0)) g=-g;
    return g;
}

SlabPoleSpectralData slabPoleSpectralData(const DielectricRegion &slab,double k0,C kRho,bool te)
{
    const double er=std::max(1.0,slab.relativePermittivity);
    const double tanD=std::max(0.0,slab.lossTangent)+SommerfeldLimitingLossTangent;
    const C epsr=er*C{1.0,-tanD};
    const C k1=k0*C{1.0,-1e-8};
    const C k2=k0*std::sqrt(epsr);
    const C g1=outgoingVerticalGammaComplex(kRho,k1);
    const C g2=outgoingVerticalGammaComplex(kRho,k2);
    C r{0.0,0.0};
    if(te)
    {
        const C den=g1+g2;
        if(std::abs(den)>1e-30) r=(g1-g2)/den;
    }
    else
    {
        const C den=g2+epsr*g1;
        if(std::abs(den)>1e-30) r=(g2-epsr*g1)/den;
    }
    const double h=std::max(slab.thicknessM,1e-15);
    const C e=std::exp(-2.0*g2*h);
    return {C{1.0,0.0}-r*r*e,r*(C{1.0,0.0}-e)};
}

C slabPoleDerivative(const DielectricRegion &slab,double k0,C beta,bool te)
{
    const double h=std::max(1e-7*k0,1e-7*std::max(k0,std::abs(beta)));
    const C hp{h,0.0};
    return (slabPoleSpectralData(slab,k0,beta+hp,te).denominator-
            slabPoleSpectralData(slab,k0,beta-hp,te).denominator)/(2.0*h);
}

bool refineSlabPole(const DielectricRegion &slab,double k0,double betaStart,bool te,C &root,double &denMag,double &residueMag)
{
    root=C{betaStart,-std::max(1e-9*k0,0.25*std::max(0.0,slab.lossTangent)*k0)};
    for(int it=0;it<24;++it)
    {
        const auto sp=slabPoleSpectralData(slab,k0,root,te);
        const C der=slabPoleDerivative(slab,k0,root,te);
        if(std::abs(der)<1e-18) break;
        C step=sp.denominator/der;
        const double maxStep=0.15*std::max(k0,std::abs(root));
        if(std::abs(step)>maxStep) step*=maxStep/std::abs(step);
        root-=step;
        if(std::abs(step)<1e-11*std::max(k0,std::abs(root))) break;
    }
    const auto sp=slabPoleSpectralData(slab,k0,root,te);
    const C der=slabPoleDerivative(slab,k0,root,te);
    denMag=std::abs(sp.denominator);
    residueMag=std::abs(der)>1e-18?std::abs(sp.numerator/der):0.0;
    const double er=std::max(1.0,slab.relativePermittivity);
    const double betaMin=0.9995*k0,betaMax=1.002*std::sqrt(er)*k0;
    return std::isfinite(root.real())&&std::isfinite(root.imag())&&std::isfinite(denMag)&&std::isfinite(residueMag)&&
           root.real()>betaMin&&root.real()<betaMax&&root.imag()<=1e-5*k0&&denMag<2e-5;
}

std::vector<LayeredSommerfeldPoleResult> findSlabGuidedPoleCandidates(const DielectricRegion &slab,double k0)
{
    std::vector<LayeredSommerfeldPoleResult> out;
    const double er=std::max(1.0,slab.relativePermittivity);
    if(er<=1.000001||!(slab.thicknessM>0.0)||!(k0>0.0)) return out;
    const double lo=k0*(1.0+1e-5),hi=k0*std::sqrt(er)*(1.0-1e-5);
    if(!(hi>lo)) return out;
    constexpr int samples=3200;
    for(bool te:{true,false})
    {
        std::vector<double> beta(static_cast<std::size_t>(samples+1));
        std::vector<double> mag(static_cast<std::size_t>(samples+1));
        for(int i=0;i<=samples;++i)
        {
            const double x=static_cast<double>(i)/samples;
            // Quadratic clustering resolves the fundamental mode close to the air
            // light line even for electrically thin PCB substrates.
            beta[static_cast<std::size_t>(i)]=lo+(hi-lo)*x*x;
            mag[static_cast<std::size_t>(i)]=std::abs(slabPoleSpectralData(slab,k0,C{beta[static_cast<std::size_t>(i)],0.0},te).denominator);
        }
        for(int i=1;i<samples;++i)
        {
            const double m=mag[static_cast<std::size_t>(i)];
            if(!(m<=mag[static_cast<std::size_t>(i-1)]&&m<=mag[static_cast<std::size_t>(i+1)]&&m<0.12)) continue;
            C root;double denMag=0.0,residueMag=0.0;
            if(!refineSlabPole(slab,k0,beta[static_cast<std::size_t>(i)],te,root,denMag,residueMag)) continue;
            bool duplicate=false;
            for(const auto &q:out)
                if(q.polarization==(te?"TE":"TM")&&std::abs(q.transverseWavenumberPerM-root)<2e-5*std::max(k0,std::abs(root))){duplicate=true;break;}
            if(duplicate) continue;
            LayeredSommerfeldPoleResult q;
            q.polarization=te?"TE":"TM";
            q.transverseWavenumberPerM=root;
            q.betaOverK0=root.real()/k0;
            q.effectiveIndex=q.betaOverK0;
            q.attenuationNpPerM=std::max(0.0,-root.imag());
            q.denominatorMagnitude=denMag;
            q.residueMagnitudePerM=residueMag;
            out.push_back(q);
        }
    }
    std::sort(out.begin(),out.end(),[](const auto &a,const auto &b){
        if(a.betaOverK0!=b.betaOverK0)return a.betaOverK0<b.betaOverK0;
        return a.polarization<b.polarization;
    });
    return out;
}


// v5.39 grounded-PEC modal audit -------------------------------------------------
//
// The microstrip presets use an explicit RWG ground on the negative-normal face of
// the dielectric.  The corresponding infinite grounded-slab reference problem is
// air / dielectric / PEC rather than the symmetric air / dielectric / air slab used
// by the 5.38 spectral audit.  For propagation constant beta and
//   alpha_1 = sqrt(beta^2-k0^2), q_2 = sqrt(k2^2-beta^2),
// the non-magnetic grounded-slab dispersion equations used here are
//   TM : q_2 tan(q_2 h) - eps_r alpha_1 = 0,
//   TE : q_2 cot(q_2 h) + alpha_1 = 0.
// These enforce E_t=0 at the PEC plane and exponential decay in the exterior air.
// The modal normalization below is independent of the MoM excitation: TM uses unit
// H_t at the dielectric/air interface, TE uses unit E_t.  It therefore supplies a
// physically dimensioned W/m normalization but not, by itself, source-coupled power.
struct GroundedPecDispersionData
{
    C denominator{1.0,0.0};
    C alphaAir{0.0,0.0};
    C qLayer{0.0,0.0};
    C epsr{1.0,0.0};
};

C groundedLayerQ(C beta,C k2)
{
    C q=std::sqrt(k2*k2-beta*beta);
    if(q.real()<0.0 || (std::abs(q.real())<1e-15 && q.imag()>0.0)) q=-q;
    return q;
}

GroundedPecDispersionData groundedPecDispersionData(const DielectricRegion &slab,double k0,C beta,bool te)
{
    const double er=std::max(1.0,slab.relativePermittivity);
    const double tanD=std::max(0.0,slab.lossTangent)+SommerfeldLimitingLossTangent;
    const C epsr=er*C{1.0,-tanD};
    const C k1=k0*C{1.0,-1e-8};
    const C k2=k0*std::sqrt(epsr);
    const C alpha=outgoingVerticalGammaComplex(beta,k1);
    const C q=groundedLayerQ(beta,k2);
    const double h=std::max(slab.thicknessM,1e-15);
    C d{1.0,0.0};
    if(te)
    {
        const C t=std::tan(q*h);
        if(std::abs(t)<1e-20) d=C{1e30,0.0};
        else d=q/t+alpha;
    }
    else d=q*std::tan(q*h)-epsr*alpha;
    return {d,alpha,q,epsr};
}

C groundedPecDispersionDerivative(const DielectricRegion &slab,double k0,C beta,bool te)
{
    const double h=1e-7*std::max(k0,std::abs(beta));
    const C hp{std::max(h,1e-9),0.0};
    return (groundedPecDispersionData(slab,k0,beta+hp,te).denominator-
            groundedPecDispersionData(slab,k0,beta-hp,te).denominator)/(2.0*hp.real());
}

double groundedPecCoverageFraction(const Input &input,const DielectricRegion &slab)
{
    const Vec3 n=NumericalEM::normalized(slab.normalAxis);
    const Vec3 u=NumericalEM::normalized(slab.uAxis);
    const Vec3 v=NumericalEM::normalized(slab.vAxis);
    if(NumericalEM::norm(n)<0.5||NumericalEM::norm(u)<0.5||NumericalEM::norm(v)<0.5)return 0.0;
    const Vec3 faceCenter=slab.centerM-n*(0.5*std::max(slab.thicknessM,0.0));
    const double tol=std::max(1e-7,0.03*std::max(slab.thicknessM,1e-6));
    double projectedArea=0.0;
    for(const auto &t:input.triangles)
    {
        const Vec3 c=centroid(t),av=c-faceCenter;
        if(std::abs(NumericalEM::dot(av,n))>tol)continue;
        if(std::abs(NumericalEM::dot(av,u))>0.52*std::max(slab.widthM,0.0) ||
           std::abs(NumericalEM::dot(av,v))>0.52*std::max(slab.heightM,0.0))continue;
        const Vec3 areaVec=cross(t.b-t.a,t.c-t.a)*0.5;
        projectedArea+=std::abs(NumericalEM::dot(areaVec,n));
    }
    const double target=std::max(1e-30,slab.widthM*slab.heightM);
    return std::clamp(projectedArea/target,0.0,1.0);
}

double gaussIntegrateAbs2(const C &q,double h,bool te)
{
    // 16-point symmetric Gauss-Legendre integration on z in [-h,0].
    double sum=0.0;
    const C denom=te?std::sin(q*h):std::cos(q*h);
    if(std::abs(denom)<1e-20)return 0.0;
    for(std::size_t i=0;i<GlX.size();++i)
    {
        for(double sign:{-1.0,+1.0})
        {
            const double x=sign*GlX[i];
            const double z=0.5*h*(x-1.0); // [-h,0]
            const C f=te?std::sin(q*(z+h))/denom:std::cos(q*(z+h))/denom;
            sum+=GlW[i]*std::norm(f);
        }
    }
    return 0.5*h*sum;
}

void populateGroundedModeNormalization(LayeredSommerfeldPoleResult &m,const DielectricRegion &slab,double k0,double omega,bool te)
{
    const auto d=groundedPecDispersionData(slab,k0,m.transverseWavenumberPerM,te);
    const double alpha=std::max(d.alphaAir.real(),1e-12*std::max(1.0,k0));
    const double airIntegral=1.0/(2.0*alpha);
    const double dielIntegral=gaussIntegrateAbs2(d.qLayer,std::max(slab.thicknessM,1e-15),te);
    double pAir=0.0,pDiel=0.0;
    if(te)
    {
        const double coeff=0.5*std::real(m.transverseWavenumberPerM/(omega*NumericalEM::Mu0));
        pAir=std::max(0.0,coeff*airIntegral);
        pDiel=std::max(0.0,coeff*dielIntegral);
    }
    else
    {
        const C eps2=NumericalEM::Epsilon0*d.epsr;
        const double coeffAir=0.5*std::real(m.transverseWavenumberPerM/(omega*NumericalEM::Epsilon0));
        const double coeffDiel=0.5*std::real(m.transverseWavenumberPerM/(omega*eps2));
        pAir=std::max(0.0,coeffAir*airIntegral);
        pDiel=std::max(0.0,coeffDiel*dielIntegral);
    }
    m.groundedPecBoundary=true;
    m.modalPowerNormalizationWPerM=pAir+pDiel;
    m.dielectricPowerFraction=m.modalPowerNormalizationWPerM>0.0?pDiel/m.modalPowerNormalizationWPerM:0.0;
    m.phaseVelocityMps=m.transverseWavenumberPerM.real()>1e-30?omega/m.transverseWavenumberPerM.real():0.0;
}

bool refineGroundedPecMode(const DielectricRegion &slab,double k0,double betaStart,bool te,C &root,double &denMag,double &residueScale)
{
    root=C{betaStart,-std::max(1e-10*k0,0.15*std::max(0.0,slab.lossTangent)*k0)};
    for(int it=0;it<30;++it)
    {
        const auto data=groundedPecDispersionData(slab,k0,root,te);
        const C der=groundedPecDispersionDerivative(slab,k0,root,te);
        if(std::abs(der)<1e-16)break;
        C step=data.denominator/der;
        const double maxStep=0.08*std::max(k0,std::abs(root));
        if(std::abs(step)>maxStep)step*=maxStep/std::abs(step);
        root-=step;
        if(std::abs(step)<1e-12*std::max(k0,std::abs(root)))break;
    }
    const auto data=groundedPecDispersionData(slab,k0,root,te);
    const C der=groundedPecDispersionDerivative(slab,k0,root,te);
    denMag=std::abs(data.denominator)/std::max(k0,1e-30);
    residueScale=std::abs(der)>1e-18?k0/std::abs(der):0.0;
    const double er=std::max(1.0,slab.relativePermittivity);
    return std::isfinite(root.real())&&std::isfinite(root.imag())&&std::isfinite(denMag)&&
           root.real()>0.999999*k0&&root.real()<1.001*std::sqrt(er)*k0&&root.imag()<=1e-5*k0&&denMag<2e-6;
}

std::vector<LayeredSommerfeldPoleResult> findGroundedPecModes(const DielectricRegion &slab,double k0,double omega)
{
    std::vector<LayeredSommerfeldPoleResult> out;
    const double er=std::max(1.0,slab.relativePermittivity);
    if(er<=1.000001||!(slab.thicknessM>0.0)||!(k0>0.0))return out;
    const double lo=k0*(1.0+1e-8),hi=k0*std::sqrt(er)*(1.0-1e-7);
    if(!(hi>lo))return out;
    constexpr int samples=3600;
    for(bool te:{false,true}) // TM first: grounded TM0 exists without the TE cutoff.
    {
        std::vector<double> beta(samples+1),mag(samples+1);
        for(int i=0;i<=samples;++i)
        {
            const double x=double(i)/samples;
            const double clustered=x*x;
            beta[static_cast<std::size_t>(i)]=lo+(hi-lo)*clustered;
            mag[static_cast<std::size_t>(i)]=std::abs(groundedPecDispersionData(slab,k0,C{beta[static_cast<std::size_t>(i)],0.0},te).denominator)/std::max(k0,1e-30);
        }
        for(int i=1;i<samples;++i)
        {
            const double m=mag[static_cast<std::size_t>(i)];
            if(!(m<=mag[static_cast<std::size_t>(i-1)]&&m<=mag[static_cast<std::size_t>(i+1)]&&m<0.15))continue;
            C root;double denMag=0.0,residue=0.0;
            if(!refineGroundedPecMode(slab,k0,beta[static_cast<std::size_t>(i)],te,root,denMag,residue))continue;
            bool duplicate=false;
            for(const auto &q:out)if(q.polarization==(te?"TE":"TM")&&std::abs(q.transverseWavenumberPerM-root)<3e-5*std::max(k0,std::abs(root))){duplicate=true;break;}
            if(duplicate)continue;
            LayeredSommerfeldPoleResult q;
            q.polarization=te?"TE":"TM";q.transverseWavenumberPerM=root;q.betaOverK0=root.real()/k0;
            q.effectiveIndex=q.betaOverK0;q.attenuationNpPerM=std::max(0.0,-root.imag());q.denominatorMagnitude=denMag;q.residueMagnitudePerM=residue;
            populateGroundedModeNormalization(q,slab,k0,omega,te);out.push_back(q);
        }
    }
    std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){if(a.betaOverK0!=b.betaOverK0)return a.betaOverK0<b.betaOverK0;return a.polarization<b.polarization;});
    return out;
}

void populateLayeredGroundedPecModeAudit(Result &out,const Input &input,const DielectricRegion &slab,double k0)
{
    if(!layeredGroundedPecModeAuditMode(input.dielectricKernelModel))return;
    out.layeredSommerfeldGroundedPecModeAuditUsed=true;
    out.layeredSommerfeldGroundCoverageFraction=groundedPecCoverageFraction(input,slab);
    out.layeredSommerfeldGroundedPecBoundaryDetected=out.layeredSommerfeldGroundCoverageFraction>=0.25;
    out.layeredSommerfeldGroundedPecPowerGuarded=true;
    if(out.layeredSommerfeldGroundedPecBoundaryDetected)
        out.layeredSommerfeldGroundedPecModes=findGroundedPecModes(slab,k0,2.0*NumericalEM::Pi*input.wire.frequencyHz);
    out.layeredSommerfeldGroundedPecModeCount=static_cast<int>(out.layeredSommerfeldGroundedPecModes.size());
    // Do not manufacture W from the closure residual.  The infinite grounded-slab
    // modal normalization is now available, but a finite-ground source-to-mode pole
    // overlap still has to be validated against the RWG current distribution.
    out.layeredSommerfeldSurfaceWavePowerResolved=false;
    out.layeredSommerfeldSurfaceWavePowerGuarded=true;
    out.layeredSurfaceWavePowerW=0.0;
}

void populateLayeredSurfaceWavePoleAudit(Result &out,const Input &input,const DielectricRegion &slab,double k0)
{
    if(!layeredSurfaceWavePoleAuditMode(input.dielectricKernelModel)) return;
    if(layeredGroundedPecModeAuditMode(input.dielectricKernelModel))
    {
        // Keep the 5.38 bare-slab roots out of the public 5.39 mode list; they remain
        // available by selecting mode 13 explicitly.
        out.layeredSommerfeldSurfaceWavePoleAuditUsed=false;
        out.layeredSommerfeldSurfaceWavePoleCount=0;
        out.layeredSommerfeldSurfaceWavePoles.clear();
        return;
    }
    out.layeredSommerfeldSurfaceWavePoles=findSlabGuidedPoleCandidates(slab,k0);
    out.layeredSommerfeldSurfaceWavePoleCount=static_cast<int>(out.layeredSommerfeldSurfaceWavePoles.size());
    out.layeredSommerfeldSurfaceWavePoleAuditUsed=true;
    // Guarded by design in 5.38. The residues are those of the dielectric-only slab,
    // whereas the application keeps the ground conductor explicitly in the RWG matrix.
    // A physical P_surface-wave needs a common grounded-stack dispersion relation,
    // modal field normalization and source-overlap integral before it can enter closure.
    out.layeredSommerfeldSurfaceWavePowerResolved=false;
    out.layeredSommerfeldSurfaceWavePowerGuarded=true;
    out.layeredSurfaceWavePowerW=0.0;
}

struct SommerfeldRadialTable
{
    std::vector<double> rhoM;
    std::vector<C> sameFaceReflectedOverEpsilon;
    // Horizontal-electric-dipole scalar-potential kernel used by the paired
    // 5.29 same-face MPIE. Unlike the 5.26 TM-only scalar preview, its spectrum
    // contains the coupled TE/TM charge-potential combination required by the
    // Sommerfeld-potential gauge.
    std::vector<C> sameFaceHedReflectedOverEpsilon;
    std::vector<C> crossFaceLayeredOverEpsilon;
    // 5.31 coupled HED scalar transmission kernel. In the homogeneous limit
    // TE and TM transmissions coincide and this reduces to the direct scalar term.
    std::vector<C> crossFaceHedLayeredOverEpsilon;
    // 5.29 tangential vector-potential data in the local slab plane.
    // Longitudinal/transverse refer to the source-observer in-plane separation.
    std::vector<C> sameFaceVectorLongitudinal;
    std::vector<C> sameFaceVectorTransverse;
    std::vector<C> crossFaceVectorLongitudinal;
    std::vector<C> crossFaceVectorTransverse;
};

struct SommerfeldRadialIntegrals
{
    C scalarSame{0.0,0.0};
    C scalarSameHed{0.0,0.0};
    C scalarCross{0.0,0.0};
    C scalarCrossHed{0.0,0.0};
    C vectorSameLong{0.0,0.0};
    C vectorSameTrans{0.0,0.0};
    C vectorCrossLong{0.0,0.0};
    C vectorCrossTrans{0.0,0.0};
    // 5.34 internal VED/normal-current transition. These are populated only by
    // the medium-2 cavity integrator; exterior/face tables leave them zero.
    C scalarVed{0.0,0.0};
    C vectorNormal{0.0,0.0};
    // 5.35 internal TM mixed components. rhoFromNormal maps a normal source
    // current into a radial/tangential vector response. normalFromRho is the
    // reciprocal partner for a radial tangential source and normal observation.
    C vectorRhoFromNormal{0.0,0.0};
    C vectorNormalFromRho{0.0,0.0};
};

void appendSommerfeldBreak(std::vector<double> &v,double x,double kMax)
{
    if(std::isfinite(x) && x>0.0 && x<kMax) v.push_back(x);
}

SommerfeldRadialIntegrals integrateSommerfeldRadialAll(const DielectricRegion &slab,double k0,double rhoM)
{
    const double h=std::max(slab.thicknessM,1e-12);
    const C epsr=std::max(1.0,slab.relativePermittivity)*
                 C{1.0,-(std::max(0.0,slab.lossTangent)+SommerfeldLimitingLossTangent)};
    const double k2Abs=std::abs(k0*std::sqrt(epsr));
    const double kBranch=std::max({k0,k2Abs,1e-9});
    const double kTailH=24.0/h;
    const double kTailR=120.0/std::max(rhoM,0.02*h);
    const double kMax=std::max(100.0*kBranch,std::min(kTailH,kTailR));

    std::vector<double> breaks{0.0};
    for(double scale:{0.70,0.90,0.97,0.992,1.008,1.03,1.10})
        appendSommerfeldBreak(breaks,scale*k0,kMax);
    for(double scale:{0.70,0.90,0.97,0.992,1.008,1.03,1.10})
        appendSommerfeldBreak(breaks,scale*k2Abs,kMax);
    appendSommerfeldBreak(breaks,1.5*kBranch,kMax);
    appendSommerfeldBreak(breaks,2.0*kBranch,kMax);
    double x=std::max(2.0*kBranch,1e-9);
    while(x<kMax)
    {
        const double next=std::min(kMax,x*1.40);
        if(next<=x*(1.0+1e-12)) break;
        breaks.push_back(next);x=next;
    }
    breaks.push_back(kMax);
    std::sort(breaks.begin(),breaks.end());
    breaks.erase(std::unique(breaks.begin(),breaks.end(),[](double a,double b){return std::abs(a-b)<=1e-12*std::max({1.0,std::abs(a),std::abs(b)});}),breaks.end());

    const double phaseStep=NumericalEM::Pi/2.5;
    SommerfeldRadialIntegrals total;
    for(std::size_t ib=1;ib<breaks.size();++ib)
    {
        const double a=breaks[ib-1],b=breaks[ib];
        if(!(b>a)) continue;
        int subdivisions=1;
        if(rhoM>0.0) subdivisions=std::max(1,static_cast<int>(std::ceil((b-a)*rhoM/phaseStep)));
        subdivisions=std::min(subdivisions,96);
        for(int sub=0;sub<subdivisions;++sub)
        {
            const double aa=a+(b-a)*static_cast<double>(sub)/subdivisions;
            const double bb=a+(b-a)*static_cast<double>(sub+1)/subdivisions;
            const double mid=0.5*(aa+bb),half=0.5*(bb-aa);
            SommerfeldRadialIntegrals panel;
            for(int g=0;g<8;++g) for(int sign:{-1,1})
            {
                const double kp=mid+half*sign*GlX[static_cast<std::size_t>(g)];
                const auto sp=sommerfeldSpectrum(slab,k0,kp);
                if(std::abs(sp.gamma1)<1e-30) continue;
                const double z=kp*rhoM;
                const double j0=std::cyl_bessel_j(0.0,z);
                const double j2=std::cyl_bessel_j(2.0,z);
                const C ratio=kp/sp.gamma1;
                const C tmSame=sp.reflection-sp.reflectionInfinity;
                const C teSame=sp.teReflection;
                const C tmCross=sp.transmission;
                const C teCross=sp.teTransmission;
                // Horizontal-electric-dipole Sommerfeld-potential gauge:
                //   G_A,t uses R_TE only,
                //   G_phi,h uses R_TE + R_q,
                //   R_q = (kz^2/k_rho^2)(R_TE + R_TM).
                // With gamma^2=-kz^2 and sp.reflection=-R_TM, this becomes
                // R_TE - gamma^2/k_rho^2 * (R_TE-sp.reflection).
                const double kp2=std::max(kp*kp,1e-24*std::max(1.0,k0*k0));
                const C hedScalarReflection=teSame-(sp.gamma1*sp.gamma1/kp2)*(teSame-sp.reflection)-sp.reflectionInfinity;
                // The corresponding HED transmission uses the same coupled TE/TM
                // longitudinal combination. When eps_r -> 1, teCross == tmCross and
                // this becomes the ordinary homogeneous transmitted scalar spectrum.
                const C hedScalarTransmission=teCross-(sp.gamma1*sp.gamma1/kp2)*(teCross-tmCross);
                const double wm=GlW[static_cast<std::size_t>(g)];
                panel.scalarSame += wm*ratio*j0*tmSame;
                panel.scalarSameHed += wm*ratio*j0*hedScalarReflection;
                panel.scalarCross += wm*ratio*j0*tmCross;
                panel.scalarCrossHed += wm*ratio*j0*hedScalarTransmission;
                // In this gauge the tangential magnetic vector potential of a HED
                // is isotropic in the interface plane and is governed by TE only.
                panel.vectorSameLong += wm*ratio*(2.0*teSame*j0);
                panel.vectorSameTrans += wm*ratio*(2.0*teSame*j0);
                // 5.30 activates these transmitted tangential components only in
                // the dedicated cross-face mode; 5.29 keeps them diagnostic-only.
                panel.vectorCrossLong += wm*ratio*(tmCross*(j0-j2)+teCross*(j0+j2));
                panel.vectorCrossTrans += wm*ratio*(tmCross*(j0+j2)+teCross*(j0-j2));
            }
            total.scalarSame += half*panel.scalarSame;
            total.scalarSameHed += half*panel.scalarSameHed;
            total.scalarCross += half*panel.scalarCross;
            total.scalarCrossHed += half*panel.scalarCrossHed;
            total.vectorSameLong += half*panel.vectorSameLong;
            total.vectorSameTrans += half*panel.vectorSameTrans;
            total.vectorCrossLong += half*panel.vectorCrossLong;
            total.vectorCrossTrans += half*panel.vectorCrossTrans;
        }
    }
    // Scalar potential G_phi/epsilon carries 1/epsilon0 and the azimuthal angular
    // integration gives 1/(4*pi). The tangential vector dyadic has projector angular
    // integrals pi(J0 +/- J2), giving 1/(8*pi).
    const C scalarScale{1.0/(4.0*NumericalEM::Pi*NumericalEM::Epsilon0),0.0};
    const C vectorScale{1.0/(8.0*NumericalEM::Pi),0.0};
    total.scalarSame*=scalarScale;
    total.scalarSameHed*=scalarScale;
    total.scalarCross*=scalarScale;
    total.scalarCrossHed*=scalarScale;
    total.vectorSameLong*=vectorScale;
    total.vectorSameTrans*=vectorScale;
    total.vectorCrossLong*=vectorScale;
    total.vectorCrossTrans*=vectorScale;
    return total;
}

// Height-propagated exterior Green data used by the guarded 5.32 wire<->RWG
// mutual correction.  The face spectrum is multiplied by exp[-gamma1(d_o+d_s)]
// before the Hankel inversion.  This is exact for the two exterior half-spaces of
// the symmetric air/slab/air stack; it intentionally does not cover source/observer
// points inside the dielectric layer.
SommerfeldRadialIntegrals integrateSommerfeldExteriorRadialAll(const DielectricRegion &slab,double k0,double rhoM,
                                                               double heightSumM,bool sameSide)
{
    const double h=std::max(slab.thicknessM,1e-12);
    const C epsr=std::max(1.0,slab.relativePermittivity)*
                 C{1.0,-(std::max(0.0,slab.lossTangent)+SommerfeldLimitingLossTangent)};
    const double k2Abs=std::abs(k0*std::sqrt(epsr));
    const double kBranch=std::max({k0,k2Abs,1e-9});
    const double kTailH=24.0/std::max(h+heightSumM,1e-12);
    const double kTailR=120.0/std::max(rhoM,0.02*h);
    const double kMax=std::max(100.0*kBranch,std::min(kTailH,kTailR));

    std::vector<double> breaks{0.0};
    for(double scale:{0.70,0.90,0.97,0.992,1.008,1.03,1.10}) appendSommerfeldBreak(breaks,scale*k0,kMax);
    for(double scale:{0.70,0.90,0.97,0.992,1.008,1.03,1.10}) appendSommerfeldBreak(breaks,scale*k2Abs,kMax);
    appendSommerfeldBreak(breaks,1.5*kBranch,kMax);appendSommerfeldBreak(breaks,2.0*kBranch,kMax);
    double x=std::max(2.0*kBranch,1e-9);
    while(x<kMax){const double next=std::min(kMax,x*1.40);if(next<=x*(1.0+1e-12))break;breaks.push_back(next);x=next;}
    breaks.push_back(kMax);
    std::sort(breaks.begin(),breaks.end());
    breaks.erase(std::unique(breaks.begin(),breaks.end(),[](double a,double b){return std::abs(a-b)<=1e-12*std::max({1.0,std::abs(a),std::abs(b)});}),breaks.end());

    const double phaseStep=NumericalEM::Pi/2.5;
    SommerfeldRadialIntegrals total;
    for(std::size_t ib=1;ib<breaks.size();++ib)
    {
        const double a=breaks[ib-1],b=breaks[ib];if(!(b>a))continue;
        int subdivisions=1;if(rhoM>0.0)subdivisions=std::max(1,static_cast<int>(std::ceil((b-a)*rhoM/phaseStep)));subdivisions=std::min(subdivisions,96);
        for(int sub=0;sub<subdivisions;++sub)
        {
            const double aa=a+(b-a)*static_cast<double>(sub)/subdivisions;
            const double bb=a+(b-a)*static_cast<double>(sub+1)/subdivisions;
            const double mid=0.5*(aa+bb),half=0.5*(bb-aa);
            SommerfeldRadialIntegrals panel;
            for(int g=0;g<8;++g) for(int sign:{-1,1})
            {
                const double kp=mid+half*sign*GlX[static_cast<std::size_t>(g)];
                const auto sp=sommerfeldSpectrum(slab,k0,kp);if(std::abs(sp.gamma1)<1e-30)continue;
                const C verticalPhase=std::exp(-sp.gamma1*std::max(0.0,heightSumM));
                const double z=kp*rhoM,j0=std::cyl_bessel_j(0.0,z),j2=std::cyl_bessel_j(2.0,z);
                const C ratio=kp/sp.gamma1;
                const C tm= sameSide ? sp.reflection : sp.transmission;
                const C te= sameSide ? sp.teReflection : sp.teTransmission;
                const double kp2=std::max(kp*kp,1e-24*std::max(1.0,k0*k0));
                C hed=te-(sp.gamma1*sp.gamma1/kp2)*(te-tm);
                // At exactly zero exterior height on a reflected same-face pair, remove
                // the high-k constant just like the face-only Duffy-compatible table.
                if(sameSide && heightSumM<=1e-15) hed-=sp.reflectionInfinity;
                hed*=verticalPhase;const C teP=te*verticalPhase,tmP=tm*verticalPhase;
                const double wm=GlW[static_cast<std::size_t>(g)];
                if(sameSide) panel.scalarSameHed += wm*ratio*j0*hed;
                else panel.scalarCrossHed += wm*ratio*j0*hed;
                if(sameSide)
                {
                    panel.vectorSameLong += wm*ratio*(2.0*teP*j0);
                    panel.vectorSameTrans += wm*ratio*(2.0*teP*j0);
                }
                else
                {
                    panel.vectorCrossLong += wm*ratio*(tmP*(j0-j2)+teP*(j0+j2));
                    panel.vectorCrossTrans += wm*ratio*(tmP*(j0+j2)+teP*(j0-j2));
                }
            }
            if(sameSide)
            {
                total.scalarSameHed+=half*panel.scalarSameHed;
                total.vectorSameLong+=half*panel.vectorSameLong;total.vectorSameTrans+=half*panel.vectorSameTrans;
            }
            else
            {
                total.scalarCrossHed+=half*panel.scalarCrossHed;
                total.vectorCrossLong+=half*panel.vectorCrossLong;total.vectorCrossTrans+=half*panel.vectorCrossTrans;
            }
        }
    }
    const C scalarScale{1.0/(4.0*NumericalEM::Pi*NumericalEM::Epsilon0),0.0};
    const C vectorScale{1.0/(8.0*NumericalEM::Pi),0.0};
    total.scalarSameHed*=scalarScale;total.scalarCrossHed*=scalarScale;
    total.vectorSameLong*=vectorScale;total.vectorSameTrans*=vectorScale;
    total.vectorCrossLong*=vectorScale;total.vectorCrossTrans*=vectorScale;
    return total;
}

struct SommerfeldExteriorRadialTable
{
    std::vector<double> rhoM;
    std::vector<C> hedScalar;
    std::vector<C> vectorLongitudinal;
    std::vector<C> vectorTransverse;
    bool sameSide=true;
    double quantizedHeightSumM=0.0;
};

double quantizeSommerfeldExteriorHeight(const DielectricRegion &slab,double k0,double heightSumM)
{
    const double lambda=k0>1e-12?2.0*NumericalEM::Pi/k0:1.0;
    const double step=std::max({0.25*std::max(slab.thicknessM,1e-9),lambda/1500.0,1e-6});
    return std::max(0.0,std::round(std::max(0.0,heightSumM)/step)*step);
}

SommerfeldExteriorRadialTable buildSommerfeldExteriorRadialTable(const DielectricRegion &slab,double k0,double heightSumM,bool sameSide)
{
    SommerfeldExteriorRadialTable table;table.sameSide=sameSide;table.quantizedHeightSumM=heightSumM;
    table.rhoM.reserve(SommerfeldRadialSampleCount);table.hedScalar.reserve(SommerfeldRadialSampleCount);
    table.vectorLongitudinal.reserve(SommerfeldRadialSampleCount);table.vectorTransverse.reserve(SommerfeldRadialSampleCount);
    const double rhoMax=std::max(std::hypot(std::max(0.0,slab.widthM),std::max(0.0,slab.heightM)),1e-9);
    const double radialScale=std::max({slab.thicknessM,1e-6*rhoMax,1e-12});
    const double ratio=1.0+rhoMax/radialScale;
    for(int i=0;i<SommerfeldRadialSampleCount;++i)
    {
        const double s=static_cast<double>(i)/static_cast<double>(SommerfeldRadialSampleCount-1);
        const double rho=(i==SommerfeldRadialSampleCount-1)?rhoMax:radialScale*(std::pow(ratio,s)-1.0);
        table.rhoM.push_back(rho);const auto v=integrateSommerfeldExteriorRadialAll(slab,k0,rho,heightSumM,sameSide);
        table.hedScalar.push_back(sameSide?v.scalarSameHed:v.scalarCrossHed);
        table.vectorLongitudinal.push_back(sameSide?v.vectorSameLong:v.vectorCrossLong);
        table.vectorTransverse.push_back(sameSide?v.vectorSameTrans:v.vectorCrossTrans);
    }
    return table;
}

const SommerfeldExteriorRadialTable &sommerfeldExteriorRadialTable(const DielectricRegion &slab,double k0,double heightSumM,bool sameSide)
{
    using Key=std::tuple<double,double,double,double,double,double,double,bool>;
    static thread_local BoundedLruCache<Key,SommerfeldExteriorRadialTable> cache(24);
    const double qh=quantizeSommerfeldExteriorHeight(slab,k0,heightSumM);
    const Key key{k0,slab.thicknessM,slab.widthM,slab.heightM,slab.relativePermittivity,slab.lossTangent,qh,sameSide};
    return cache.get(key,[&]{return buildSommerfeldExteriorRadialTable(slab,k0,qh,sameSide);},
                     gSommerfeldCacheCounters.exteriorHits,gSommerfeldCacheCounters.exteriorMisses,
                     gSommerfeldCacheCounters.exteriorBuilds,gSommerfeldCacheCounters.exteriorEvictions,
                     gSommerfeldCacheCounters.exteriorEntries,gSommerfeldCacheCounters.exteriorBuildMs);
}

SommerfeldRadialTable buildSommerfeldRadialTable(const DielectricRegion &slab,double k0)
{
    SommerfeldRadialTable table;
    table.rhoM.reserve(SommerfeldRadialSampleCount);
    table.sameFaceReflectedOverEpsilon.reserve(SommerfeldRadialSampleCount);
    table.sameFaceHedReflectedOverEpsilon.reserve(SommerfeldRadialSampleCount);
    table.crossFaceLayeredOverEpsilon.reserve(SommerfeldRadialSampleCount);
    table.crossFaceHedLayeredOverEpsilon.reserve(SommerfeldRadialSampleCount);
    table.sameFaceVectorLongitudinal.reserve(SommerfeldRadialSampleCount);
    table.sameFaceVectorTransverse.reserve(SommerfeldRadialSampleCount);
    table.crossFaceVectorLongitudinal.reserve(SommerfeldRadialSampleCount);
    table.crossFaceVectorTransverse.reserve(SommerfeldRadialSampleCount);
    const double rhoMax=std::max(std::hypot(std::max(0.0,slab.widthM),std::max(0.0,slab.heightM)),1e-9);
    const double radialScale=std::max({slab.thicknessM,1e-6*rhoMax,1e-12});
    const double ratio=1.0+rhoMax/radialScale;
    for(int i=0;i<SommerfeldRadialSampleCount;++i)
    {
        const double s=static_cast<double>(i)/static_cast<double>(SommerfeldRadialSampleCount-1);
        const double rho=(i==SommerfeldRadialSampleCount-1)?rhoMax:radialScale*(std::pow(ratio,s)-1.0);
        table.rhoM.push_back(rho);
        const auto values=integrateSommerfeldRadialAll(slab,k0,rho);
        table.sameFaceReflectedOverEpsilon.push_back(values.scalarSame);
        table.sameFaceHedReflectedOverEpsilon.push_back(values.scalarSameHed);
        table.crossFaceLayeredOverEpsilon.push_back(values.scalarCross);
        table.crossFaceHedLayeredOverEpsilon.push_back(values.scalarCrossHed);
        table.sameFaceVectorLongitudinal.push_back(values.vectorSameLong);
        table.sameFaceVectorTransverse.push_back(values.vectorSameTrans);
        table.crossFaceVectorLongitudinal.push_back(values.vectorCrossLong);
        table.crossFaceVectorTransverse.push_back(values.vectorCrossTrans);
    }
    return table;
}

const SommerfeldRadialTable &sommerfeldRadialTable(const DielectricRegion &slab,double k0)
{
    using Key=std::tuple<double,double,double,double,double,double>;
    static thread_local BoundedLruCache<Key,SommerfeldRadialTable> cache(32);
    const Key key{k0,slab.thicknessM,slab.widthM,slab.heightM,slab.relativePermittivity,slab.lossTangent};
    return cache.get(key,[&]{return buildSommerfeldRadialTable(slab,k0);},
                     gSommerfeldCacheCounters.faceHits,gSommerfeldCacheCounters.faceMisses,
                     gSommerfeldCacheCounters.faceBuilds,gSommerfeldCacheCounters.faceEvictions,
                     gSommerfeldCacheCounters.faceEntries,gSommerfeldCacheCounters.faceBuildMs);
}

C interpolateSommerfeldTable(const SommerfeldRadialTable &table,double rhoM,bool sameFace)
{
    const auto &values=sameFace?table.sameFaceReflectedOverEpsilon:table.crossFaceLayeredOverEpsilon;
    if(table.rhoM.empty() || values.empty()) return {0.0,0.0};
    if(rhoM<=table.rhoM.front()) return values.front();
    if(rhoM>=table.rhoM.back()) return values.back();
    const auto it=std::lower_bound(table.rhoM.begin(),table.rhoM.end(),rhoM);
    const std::size_t hi=static_cast<std::size_t>(std::distance(table.rhoM.begin(),it));
    const std::size_t lo=hi-1;
    const double span=table.rhoM[hi]-table.rhoM[lo];
    const double t=span>0.0?(rhoM-table.rhoM[lo])/span:0.0;
    return values[lo]*(1.0-t)+values[hi]*t;
}

C sommerfeldScalarGreenOverEpsilon(const DielectricRegion &slab,double k0,double rhoM,bool sameFace)
{
    return interpolateSommerfeldTable(sommerfeldRadialTable(slab,k0),rhoM,sameFace);
}

C interpolateSommerfeldVectorComponent(const SommerfeldRadialTable &table,const std::vector<C> &values,double rhoM)
{
    if(table.rhoM.empty() || values.empty()) return {0.0,0.0};
    if(rhoM<=table.rhoM.front()) return values.front();
    if(rhoM>=table.rhoM.back()) return values.back();
    const auto it=std::lower_bound(table.rhoM.begin(),table.rhoM.end(),rhoM);
    const std::size_t hi=static_cast<std::size_t>(std::distance(table.rhoM.begin(),it));
    const std::size_t lo=hi-1;
    const double span=table.rhoM[hi]-table.rhoM[lo];
    const double f=span>0.0?(rhoM-table.rhoM[lo])/span:0.0;
    return values[lo]*(1.0-f)+values[hi]*f;
}

struct TangentialVectorGreen
{
    C longitudinal{0.0,0.0};
    C transverse{0.0,0.0};
};

C sommerfeldHedScalarGreenOverEpsilon(const DielectricRegion &slab,double k0,double rhoM,bool sameFace)
{
    const auto &table=sommerfeldRadialTable(slab,k0);
    return interpolateSommerfeldVectorComponent(table,
        sameFace?table.sameFaceHedReflectedOverEpsilon:table.crossFaceHedLayeredOverEpsilon,rhoM);
}

C sommerfeldHedSameFaceScalarGreenOverEpsilon(const DielectricRegion &slab,double k0,double rhoM)
{
    return sommerfeldHedScalarGreenOverEpsilon(slab,k0,rhoM,true);
}

TangentialVectorGreen sommerfeldTangentialVectorGreen(const DielectricRegion &slab,double k0,double rhoM,bool sameFace)
{
    const auto &table=sommerfeldRadialTable(slab,k0);
    if(sameFace)
        return {interpolateSommerfeldVectorComponent(table,table.sameFaceVectorLongitudinal,rhoM),
                interpolateSommerfeldVectorComponent(table,table.sameFaceVectorTransverse,rhoM)};
    return {interpolateSommerfeldVectorComponent(table,table.crossFaceVectorLongitudinal,rhoM),
            interpolateSommerfeldVectorComponent(table,table.crossFaceVectorTransverse,rhoM)};
}

C interpolateSommerfeldExteriorComponent(const SommerfeldExteriorRadialTable &table,const std::vector<C> &values,double rhoM)
{
    if(table.rhoM.empty()||values.empty())return {0.0,0.0};
    if(rhoM<=table.rhoM.front()) return values.front();
    if(rhoM>=table.rhoM.back()) return values.back();
    const auto it=std::lower_bound(table.rhoM.begin(),table.rhoM.end(),rhoM);
    const std::size_t hi=static_cast<std::size_t>(std::distance(table.rhoM.begin(),it)),lo=hi-1;
    const double span=table.rhoM[hi]-table.rhoM[lo];const double f=span>0.0?(rhoM-table.rhoM[lo])/span:0.0;
    return values[lo]*(1.0-f)+values[hi]*f;
}

struct ExteriorSommerfeldGreen
{
    C hedScalarOverEpsilon{0.0,0.0};
    TangentialVectorGreen vector{};
};

ExteriorSommerfeldGreen sommerfeldExteriorGreen(const DielectricRegion &slab,double k0,double rhoM,
                                                 double heightSumM,bool sameSide)
{
    const auto &table=sommerfeldExteriorRadialTable(slab,k0,heightSumM,sameSide);
    return {interpolateSommerfeldExteriorComponent(table,table.hedScalar,rhoM),
            {interpolateSommerfeldExteriorComponent(table,table.vectorLongitudinal,rhoM),
             interpolateSommerfeldExteriorComponent(table,table.vectorTransverse,rhoM)}};
}


struct SommerfeldInternalRadialTable
{
    std::vector<double> rhoM;
    std::vector<C> hedScalarResidual;
    std::vector<C> vedScalarResidual;
    std::vector<C> vectorLongitudinalResidual;
    std::vector<C> vectorTransverseResidual;
    std::vector<C> vectorNormalResidual;
    std::vector<C> vectorRhoFromNormalResidual;
    std::vector<C> vectorNormalFromRhoResidual;
    double quantizedObservationZM=0.0;
    double quantizedSourceZM=0.0;
};

struct InternalCavityResidualData
{
    C residual{0.0,0.0};
    // Dimensionless signed vertical factors. They are -1/gamma*d/dz_obs and
    // +1/gamma*d/dz_src of the residual respectively. These are the quantities
    // required by the rho-z / z-rho TM projector after the spectral 1/gamma
    // Green factor has been separated.
    C signedObservation{0.0,0.0};
    C signedSource{0.0,0.0};
};

InternalCavityResidualData internalCavityResidualData(C gamma,C internalReflection,double zObs,double zSrc,double h)
{
    const double zo=std::clamp(zObs,0.0,h),zs=std::clamp(zSrc,0.0,h);
    const double delta=zo-zs;
    const double signDelta=delta>1e-15?1.0:(delta<-1e-15?-1.0:0.0);
    const C direct=std::exp(-gamma*std::abs(delta));
    const C top=internalReflection*std::exp(-gamma*(zo+zs));
    const C bottom=internalReflection*std::exp(-gamma*(2.0*h-zo-zs));
    const C doubleBounce=internalReflection*internalReflection*std::exp(-gamma*(2.0*h-std::abs(delta)));
    const C roundTrip=std::exp(-2.0*gamma*h);
    C den=C{1.0,0.0}-internalReflection*internalReflection*roundTrip;
    if(std::abs(den)<1e-18)den+=C{1e-18,1e-18};
    const C full=(direct+top+bottom+doubleBounce)/den;
    const C dDirectObs=-gamma*signDelta*direct;
    const C dDirectSrc=+gamma*signDelta*direct;
    const C dFullObs=(dDirectObs-gamma*top+gamma*bottom+gamma*signDelta*doubleBounce)/den;
    const C dFullSrc=(dDirectSrc-gamma*top+gamma*bottom-gamma*signDelta*doubleBounce)/den;
    InternalCavityResidualData out;
    out.residual=full-direct;
    if(std::abs(gamma)>1e-30)
    {
        out.signedObservation=-(dFullObs-dDirectObs)/gamma;
        out.signedSource=+(dFullSrc-dDirectSrc)/gamma;
    }
    return out;
}

C internalCavityResidual(C gamma,C internalReflection,double zObs,double zSrc,double h)
{
    return internalCavityResidualData(gamma,internalReflection,zObs,zSrc,h).residual;
}

double quantizeSommerfeldInternalZ(const DielectricRegion &slab,double k0,double zM)
{
    const double h=std::max(slab.thicknessM,1e-12);
    const double lambda=k0>1e-12?2.0*NumericalEM::Pi/k0:1.0;
    const double step=std::max({h/240.0,lambda/2500.0,2e-7});
    return std::clamp(std::round(std::clamp(zM,0.0,h)/step)*step,0.0,h);
}

SommerfeldRadialIntegrals integrateSommerfeldInternalRadialAll(const DielectricRegion &slab,double k0,double rhoM,
                                                               double zObsM,double zSrcM)
{
    const double h=std::max(slab.thicknessM,1e-12);
    const C epsr=std::max(1.0,slab.relativePermittivity)*
                 C{1.0,-(std::max(0.0,slab.lossTangent)+SommerfeldLimitingLossTangent)};
    const double k2Abs=std::abs(k0*std::sqrt(epsr));
    const double kBranch=std::max({k0,k2Abs,1e-9});
    const double imageDistance=std::max(0.015*h,std::min({zObsM+zSrcM,2.0*h-zObsM-zSrcM,h}));
    const double kTailH=28.0/std::max(imageDistance,1e-12);
    const double kTailR=120.0/std::max(rhoM,0.02*h);
    const double kMax=std::max(100.0*kBranch,std::min(kTailH,kTailR));

    std::vector<double> breaks{0.0};
    for(double scale:{0.70,0.90,0.97,0.992,1.008,1.03,1.10})appendSommerfeldBreak(breaks,scale*k0,kMax);
    for(double scale:{0.70,0.90,0.97,0.992,1.008,1.03,1.10})appendSommerfeldBreak(breaks,scale*k2Abs,kMax);
    appendSommerfeldBreak(breaks,1.5*kBranch,kMax);appendSommerfeldBreak(breaks,2.0*kBranch,kMax);
    double x=std::max(2.0*kBranch,1e-9);
    while(x<kMax){const double next=std::min(kMax,x*1.40);if(next<=x*(1.0+1e-12))break;breaks.push_back(next);x=next;}
    breaks.push_back(kMax);
    std::sort(breaks.begin(),breaks.end());
    breaks.erase(std::unique(breaks.begin(),breaks.end(),[](double a,double b){return std::abs(a-b)<=1e-12*std::max({1.0,std::abs(a),std::abs(b)});}),breaks.end());

    const double phaseStep=NumericalEM::Pi/2.5;
    SommerfeldRadialIntegrals total;
    for(std::size_t ib=1;ib<breaks.size();++ib)
    {
        const double a=breaks[ib-1],b=breaks[ib];if(!(b>a))continue;
        int subdivisions=1;if(rhoM>0.0)subdivisions=std::max(1,static_cast<int>(std::ceil((b-a)*rhoM/phaseStep)));subdivisions=std::min(subdivisions,96);
        for(int sub=0;sub<subdivisions;++sub)
        {
            const double aa=a+(b-a)*static_cast<double>(sub)/subdivisions;
            const double bb=a+(b-a)*static_cast<double>(sub+1)/subdivisions;
            const double mid=0.5*(aa+bb),half=0.5*(bb-aa);
            SommerfeldRadialIntegrals panel;
            for(int g=0;g<8;++g)for(int sign:{-1,1})
            {
                const double kp=mid+half*sign*GlX[static_cast<std::size_t>(g)];
                const auto sp=sommerfeldSpectrum(slab,k0,kp);if(std::abs(sp.gamma2)<1e-30)continue;
                // Reflection seen from inside medium 2 is the sign-reversed interface
                // reflection used for excitation from air. This gives the correct
                // homogeneous and electrostatic limits for the symmetric slab.
                const C rTm=-sp.tmInterfaceReflectionAirToLayer;
                const C rTe=-sp.teInterfaceReflectionAirToLayer;
                const auto tmData=internalCavityResidualData(sp.gamma2,rTm,zObsM,zSrcM,h);
                const C tmCorr=tmData.residual;
                const C teCorr=internalCavityResidual(sp.gamma2,rTe,zObsM,zSrcM,h);
                const double z=kp*rhoM,j0=std::cyl_bessel_j(0.0,z),j1=std::cyl_bessel_j(1.0,z),j2=std::cyl_bessel_j(2.0,z);
                const C ratio=kp/sp.gamma2;
                const double kp2=std::max(kp*kp,1e-24*std::max(1.0,k0*k0));
                const C hed=teCorr-(sp.gamma2*sp.gamma2/kp2)*(teCorr-tmCorr);
                const double wm=GlW[static_cast<std::size_t>(g)];
                panel.scalarSameHed+=wm*ratio*j0*hed;
                // Vertical-electric-dipole (VED) transition: a normal electric
                // current excites TM only.  The charge-potential cavity residual
                // therefore follows tmCorr, while the normal magnetic-vector
                // potential uses the same TM round-trip factor.  The direct
                // homogeneous medium-2 contribution is already removed by
                // internalCavityResidual().
                panel.scalarVed+=wm*ratio*j0*tmCorr;
                panel.vectorNormal+=wm*ratio*(2.0*tmCorr*j0);
                // 5.35 reciprocal rho-z / z-rho TM vector components. The angular
                // integral of the mixed TM projector gives J1. After the spectral
                // scalar 1/gamma2 factor is separated, the projector contributes
                // k_rho*gamma2/k2^2; together with the radial measure this becomes
                // k_rho^2/k2^2. signedObservation/source retain the up/down path
                // orientation of the multiple-reflection cavity response.
                const C k2sq=C{k0*k0,0.0}*epsr;
                if(std::abs(k2sq)>1e-30)
                {
                    const C crossWeight=C{kp*kp,0.0}/k2sq;
                    panel.vectorRhoFromNormal+=wm*(C{-2.0,0.0}*crossWeight*j1*tmData.signedObservation);
                    panel.vectorNormalFromRho+=wm*(C{-2.0,0.0}*crossWeight*j1*tmData.signedSource);
                }
                panel.vectorSameLong+=wm*ratio*(tmCorr*(j0-j2)+teCorr*(j0+j2));
                panel.vectorSameTrans+=wm*ratio*(tmCorr*(j0+j2)+teCorr*(j0-j2));
            }
            total.scalarSameHed+=half*panel.scalarSameHed;
            total.scalarVed+=half*panel.scalarVed;
            total.vectorNormal+=half*panel.vectorNormal;
            total.vectorRhoFromNormal+=half*panel.vectorRhoFromNormal;
            total.vectorNormalFromRho+=half*panel.vectorNormalFromRho;
            total.vectorSameLong+=half*panel.vectorSameLong;
            total.vectorSameTrans+=half*panel.vectorSameTrans;
        }
    }
    const C scalarScale=C{1.0/(4.0*NumericalEM::Pi*NumericalEM::Epsilon0),0.0}/epsr;
    const C vectorScale{1.0/(8.0*NumericalEM::Pi),0.0};
    total.scalarSameHed*=scalarScale;
    total.scalarVed*=scalarScale;
    total.vectorNormal*=vectorScale;
    total.vectorRhoFromNormal*=vectorScale;
    total.vectorNormalFromRho*=vectorScale;
    total.vectorSameLong*=vectorScale;total.vectorSameTrans*=vectorScale;
    return total;
}

SommerfeldInternalRadialTable buildSommerfeldInternalRadialTable(const DielectricRegion &slab,double k0,double zObsM,double zSrcM)
{
    SommerfeldInternalRadialTable table;table.quantizedObservationZM=zObsM;table.quantizedSourceZM=zSrcM;
    table.rhoM.reserve(SommerfeldRadialSampleCount);table.hedScalarResidual.reserve(SommerfeldRadialSampleCount);table.vedScalarResidual.reserve(SommerfeldRadialSampleCount);
    table.vectorLongitudinalResidual.reserve(SommerfeldRadialSampleCount);table.vectorTransverseResidual.reserve(SommerfeldRadialSampleCount);table.vectorNormalResidual.reserve(SommerfeldRadialSampleCount);
    table.vectorRhoFromNormalResidual.reserve(SommerfeldRadialSampleCount);table.vectorNormalFromRhoResidual.reserve(SommerfeldRadialSampleCount);
    const double rhoMax=std::max(std::hypot(std::max(0.0,slab.widthM),std::max(0.0,slab.heightM)),1e-9);
    const double radialScale=std::max({slab.thicknessM,1e-6*rhoMax,1e-12});const double ratio=1.0+rhoMax/radialScale;
    for(int i=0;i<SommerfeldRadialSampleCount;++i)
    {
        const double s=static_cast<double>(i)/static_cast<double>(SommerfeldRadialSampleCount-1);
        const double rho=(i==SommerfeldRadialSampleCount-1)?rhoMax:radialScale*(std::pow(ratio,s)-1.0);
        table.rhoM.push_back(rho);const auto v=integrateSommerfeldInternalRadialAll(slab,k0,rho,zObsM,zSrcM);
        table.hedScalarResidual.push_back(v.scalarSameHed);table.vedScalarResidual.push_back(v.scalarVed);
        table.vectorLongitudinalResidual.push_back(v.vectorSameLong);table.vectorTransverseResidual.push_back(v.vectorSameTrans);table.vectorNormalResidual.push_back(v.vectorNormal);
        table.vectorRhoFromNormalResidual.push_back(v.vectorRhoFromNormal);table.vectorNormalFromRhoResidual.push_back(v.vectorNormalFromRho);
    }
    return table;
}

const SommerfeldInternalRadialTable &sommerfeldInternalRadialTable(const DielectricRegion &slab,double k0,double zObsM,double zSrcM)
{
    using Key=std::tuple<double,double,double,double,double,double,double,double>;
    static thread_local BoundedLruCache<Key,SommerfeldInternalRadialTable> cache(32);
    double zo=quantizeSommerfeldInternalZ(slab,k0,zObsM),zs=quantizeSommerfeldInternalZ(slab,k0,zSrcM);
    if(zo>zs)std::swap(zo,zs); // reciprocal cavity kernel
    const Key key{k0,slab.thicknessM,slab.widthM,slab.heightM,slab.relativePermittivity,slab.lossTangent,zo,zs};
    return cache.get(key,[&]{return buildSommerfeldInternalRadialTable(slab,k0,zo,zs);},
                     gSommerfeldCacheCounters.internalHits,gSommerfeldCacheCounters.internalMisses,
                     gSommerfeldCacheCounters.internalBuilds,gSommerfeldCacheCounters.internalEvictions,
                     gSommerfeldCacheCounters.internalEntries,gSommerfeldCacheCounters.internalBuildMs);
}

C interpolateSommerfeldInternalComponent(const SommerfeldInternalRadialTable &table,const std::vector<C> &values,double rhoM)
{
    if(table.rhoM.empty()||values.empty()) return {0.0,0.0};
    if(rhoM<=table.rhoM.front()) return values.front();
    if(rhoM>=table.rhoM.back()) return values.back();
    const auto it=std::lower_bound(table.rhoM.begin(),table.rhoM.end(),rhoM);
    const std::size_t hi=static_cast<std::size_t>(std::distance(table.rhoM.begin(),it)),lo=hi-1;
    const double span=table.rhoM[hi]-table.rhoM[lo];
    const double f=span>0.0?(rhoM-table.rhoM[lo])/span:0.0;
    return values[lo]*(1.0-f)+values[hi]*f;
}

struct InternalSommerfeldGreen
{
    C hedScalarResidualOverEpsilon{0.0,0.0};
    C vedScalarResidualOverEpsilon{0.0,0.0};
    TangentialVectorGreen vectorResidual{};
    C vectorNormalResidual{0.0,0.0};
    C vectorRhoFromNormalResidual{0.0,0.0};
    C vectorNormalFromRhoResidual{0.0,0.0};
};
InternalSommerfeldGreen sommerfeldInternalGreen(const DielectricRegion &slab,double k0,double rhoM,double zObsM,double zSrcM)
{
    const auto &table=sommerfeldInternalRadialTable(slab,k0,zObsM,zSrcM);
    return {interpolateSommerfeldInternalComponent(table,table.hedScalarResidual,rhoM),
            interpolateSommerfeldInternalComponent(table,table.vedScalarResidual,rhoM),
            {interpolateSommerfeldInternalComponent(table,table.vectorLongitudinalResidual,rhoM),
             interpolateSommerfeldInternalComponent(table,table.vectorTransverseResidual,rhoM)},
            interpolateSommerfeldInternalComponent(table,table.vectorNormalResidual,rhoM),
            interpolateSommerfeldInternalComponent(table,table.vectorRhoFromNormalResidual,rhoM),
            interpolateSommerfeldInternalComponent(table,table.vectorNormalFromRhoResidual,rhoM)};
}

ComplexVec3 layeredWireSurfaceVectorPotentialCorrection(const Input &input,const Vec3 &rObs,const Vec3 &rSrc,
                                                        const Vec3 &sourceVector,double regularizationM,
                                                        double omega,double k0)
{
    ComplexVec3 zero{};
    if(input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight &&
       (input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldInternalLayer && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedNormal && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal && !complexLayeredMode(input.dielectricKernelModel))) return zero;

    const auto internalPair=layeredInternalPairFor(input,rObs,rSrc);
    if(internalPair.region)
    {
        const auto &slab=*internalPair.region;const Vec3 n=NumericalEM::normalized(slab.normalAxis),d=rObs-rSrc;
        const double axial=NumericalEM::dot(d,n);const Vec3 rhoVec=d-n*axial;const double rho=NumericalEM::norm(rhoVec);
        const auto ig=sommerfeldInternalGreen(slab,k0,rho,internalPair.observationZM,internalPair.sourceZM);
        const double fsNormal=NumericalEM::dot(sourceVector,n);
        const Vec3 fsT=sourceVector-n*fsNormal;
        const bool vedMode=input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel));
        if(NumericalEM::norm(fsT)<=1e-14 && (!vedMode || std::abs(fsNormal)<=1e-14)) return zero;
        ComplexVec3 layered{};
        if(NumericalEM::norm(fsT)>1e-14)
        {
            if(rho<=1e-12)
            {
                const C iso=0.5*(ig.vectorResidual.longitudinal+ig.vectorResidual.transverse);layered=fsT*iso;
            }
            else
            {
                const Vec3 rhoHat=rhoVec*(1.0/rho),tHat=NumericalEM::normalized(cross(n,rhoHat));
                layered=rhoHat*(ig.vectorResidual.longitudinal*NumericalEM::dot(fsT,rhoHat));
                layered+=tHat*(ig.vectorResidual.transverse*NumericalEM::dot(fsT,tHat));
            }
        }
        // 5.34 guarded VED transition.  The normal A_z cavity residual is TM-only.
        // It is useful for arbitrary/non-coplanar tests and completes the diagonal
        // normal component, while the dominant via<->horizontal-RWG coupling below
        // is supplied by the VED/HED scalar-potential gradients.  Off-diagonal
        // vector dyadic terms remain intentionally guarded.
        if(vedMode && std::abs(fsNormal)>1e-14) layered+=n*(ig.vectorNormalResidual*fsNormal);
        if((input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel)) && rho>1e-12)
        {
            const Vec3 rhoHat=rhoVec*(1.0/rho);
            // G_{rho z}: a normal/VED source excites a radial tangential vector
            // response. G_{z rho}: only the radial component of a tangential HED
            // source contributes to the reciprocal normal response. Both residuals
            // are subsequently real-projected together with the diagonal vector data.
            if(std::abs(fsNormal)>1e-14)
                layered+=rhoHat*(ig.vectorRhoFromNormalResidual*fsNormal);
            const double radialSource=NumericalEM::dot(fsT,rhoHat);
            if(std::abs(radialSource)>1e-14)
                layered+=n*(ig.vectorNormalFromRhoResidual*radialSource);
        }
        // The internal tables already contain only the cavity residual above the
        // homogeneous-medium-2 direct Green term present in the baseline operator.
        if(!complexLayeredMode(input.dielectricKernelModel))
        {
            layered.x=C{layered.x.real(),0.0};layered.y=C{layered.y.real(),0.0};layered.z=C{layered.z.real(),0.0};
        }
        const double strength=std::clamp(1.0-slab.fieldFillFactor,0.0,1.0);
        return layered*(J*omega*NumericalEM::Mu0*strength);
    }

    const auto pair=layeredExteriorPairFor(input,rObs,rSrc);if(!pair.region)return zero;
    const auto &slab=*pair.region;const Vec3 n=NumericalEM::normalized(slab.normalAxis),d=rObs-rSrc;
    const double axial=NumericalEM::dot(d,n);const Vec3 rhoVec=d-n*axial;const double rho=NumericalEM::norm(rhoVec);
    const bool sameSide=pair.observationSide==pair.sourceSide;
    const double heightSum=pair.observationDistanceM+pair.sourceDistanceM;
    const auto eg=sommerfeldExteriorGreen(slab,k0,rho,heightSum,sameSide);
    const Vec3 fsT=sourceVector-n*NumericalEM::dot(sourceVector,n);
    ComplexVec3 layered{};
    if(sameSide || rho<=1e-12)
    {
        const C iso=0.5*(eg.vector.longitudinal+eg.vector.transverse);layered=fsT*iso;
    }
    else
    {
        const Vec3 rhoHat=rhoVec*(1.0/rho),tHat=NumericalEM::normalized(cross(n,rhoHat));
        layered=rhoHat*(eg.vector.longitudinal*NumericalEM::dot(fsT,rhoHat));
        layered+=tHat*(eg.vector.transverse*NumericalEM::dot(fsT,tHat));
    }
    if(!sameSide)
    {
        const auto med=mediumForPair(input,rObs,rSrc,omega,k0);const C g0=green(rObs,rSrc,med.k,regularizationM);
        layered.x-=g0*fsT.x;layered.y-=g0*fsT.y;layered.z-=g0*fsT.z;
    }
    if(!complexLayeredMode(input.dielectricKernelModel))
    {
        layered.x=C{layered.x.real(),0.0};layered.y=C{layered.y.real(),0.0};layered.z=C{layered.z.real(),0.0};
    }
    const double strength=std::clamp(1.0-slab.fieldFillFactor,0.0,1.0);
    return layered*(J*omega*NumericalEM::Mu0*strength);
}

C layeredWireSurfaceVectorPotentialCorrectionDot(const Input &input,const Vec3 &rObs,const Vec3 &testVector,
                                                  const Vec3 &rSrc,const Vec3 &sourceVector,double regularizationM,
                                                  double omega,double k0)
{
    return dotComplex(testVector,layeredWireSurfaceVectorPotentialCorrection(input,rObs,rSrc,sourceVector,regularizationM,omega,k0));
}

// Residual HED scalar Green used by the 5.33 tangential charge-gradient mutual term.
// It is deliberately real-projected. Since the EFIE scalar contribution carries
// 1/(j*omega), this adds only reactive energy until the full complex layered MPIE
// (including normal-current and radiative balance) is validated.
C layeredWireSurfaceHedScalarResidualOverEpsilon(const Input &input,const Vec3 &rObs,const Vec3 &rSrc,
                                                  double regularizationM,double omega,double k0)
{
    if((input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldInternalLayer && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedNormal && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal && !complexLayeredMode(input.dielectricKernelModel)))return {0.0,0.0};
    const auto internalPair=layeredInternalPairFor(input,rObs,rSrc);
    if(internalPair.region)
    {
        const auto &slab=*internalPair.region;const Vec3 n=NumericalEM::normalized(slab.normalAxis),d=rObs-rSrc;
        const Vec3 rhoVec=d-n*NumericalEM::dot(d,n);const double rho=NumericalEM::norm(rhoVec);
        const C residual=sommerfeldInternalGreen(slab,k0,rho,internalPair.observationZM,internalPair.sourceZM).hedScalarResidualOverEpsilon;
        const double strength=std::clamp(1.0-slab.fieldFillFactor,0.0,1.0);
        return complexLayeredMode(input.dielectricKernelModel) ? strength*residual : C{strength*residual.real(),0.0};
    }
    const auto pair=layeredExteriorPairFor(input,rObs,rSrc);if(!pair.region)return {0.0,0.0};
    const auto &slab=*pair.region;const Vec3 n=NumericalEM::normalized(slab.normalAxis),d=rObs-rSrc;
    const Vec3 rhoVec=d-n*NumericalEM::dot(d,n);const double rho=NumericalEM::norm(rhoVec);
    const bool sameSide=pair.observationSide==pair.sourceSide;const double heightSum=pair.observationDistanceM+pair.sourceDistanceM;
    C residual=sommerfeldExteriorGreen(slab,k0,rho,heightSum,sameSide).hedScalarOverEpsilon;
    if(!sameSide)
    {
        const auto med=mediumForPair(input,rObs,rSrc,omega,k0);
        residual-=green(rObs,rSrc,med.k,regularizationM)/med.epsilon;
    }
    const double strength=std::clamp(1.0-slab.fieldFillFactor,0.0,1.0);
    return complexLayeredMode(input.dielectricKernelModel) ? strength*residual : C{strength*residual.real(),0.0};
}

// 5.34 VED scalar residual for a predominantly normal current inside medium 2.
// A VED excites TM only; the direct homogeneous dielectric term is already present
// in the baseline operator, so the internal table contains only the finite-slab
// cavity residual.  The residual is real-projected before the 1/(j*omega) EFIE
// prefactor, preserving the same passivity guard as the HED transition.
C layeredWireSurfaceVedScalarResidualOverEpsilon(const Input &input,const Vec3 &rObs,const Vec3 &rSrc,
                                                  double regularizationM,double omega,double k0)
{
    (void)regularizationM;(void)omega;
    if(input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedNormal && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal && !complexLayeredMode(input.dielectricKernelModel))return {0.0,0.0};
    const auto pair=layeredInternalPairFor(input,rObs,rSrc);if(!pair.region)return {0.0,0.0};
    const auto &slab=*pair.region;const Vec3 n=NumericalEM::normalized(slab.normalAxis),d=rObs-rSrc;
    const Vec3 rhoVec=d-n*NumericalEM::dot(d,n);const double rho=NumericalEM::norm(rhoVec);
    const C residual=sommerfeldInternalGreen(slab,k0,rho,pair.observationZM,pair.sourceZM).vedScalarResidualOverEpsilon;
    const double strength=std::clamp(1.0-slab.fieldFillFactor,0.0,1.0);
    return complexLayeredMode(input.dielectricKernelModel) ? strength*residual : C{strength*residual.real(),0.0};
}

ComplexVec3 layeredWireSurfaceVedScalarTangentialGradient(const Input &input,const Vec3 &rObs,const Vec3 &rSrc,
                                                           double regularizationM,double omega,double k0)
{
    ComplexVec3 zero{};
    if(input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedNormal && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal && !complexLayeredMode(input.dielectricKernelModel))return zero;
    const auto pair=layeredInternalPairFor(input,rObs,rSrc);if(!pair.region)return zero;
    const auto &slab=*pair.region;const Vec3 n=NumericalEM::normalized(slab.normalAxis),d=rObs-rSrc;
    const Vec3 rhoVec=d-n*NumericalEM::dot(d,n);const double rho=NumericalEM::norm(rhoVec);if(rho<=1e-10)return zero;
    const Vec3 rhoHat=rhoVec*(1.0/rho);const double h=std::max(slab.thicknessM,1e-12);
    const double dr=std::max(5e-7,std::min(0.01*h,std::max(2e-5*h,0.002*std::max(rho,h))));
    const C gp=layeredWireSurfaceVedScalarResidualOverEpsilon(input,rObs+rhoHat*dr,rSrc,regularizationM,omega,k0);
    const C gm=layeredWireSurfaceVedScalarResidualOverEpsilon(input,rObs-rhoHat*dr,rSrc,regularizationM,omega,k0);
    return rhoHat*((gp-gm)/(2.0*dr));
}

ComplexVec3 layeredWireSurfaceHedScalarTangentialGradient(const Input &input,const Vec3 &rObs,const Vec3 &rSrc,
                                                           double regularizationM,double omega,double k0)
{
    ComplexVec3 zero{};
    if((input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldInternalLayer && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedNormal && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal && !complexLayeredMode(input.dielectricKernelModel)))return zero;
    const auto ip=layeredInternalPairFor(input,rObs,rSrc);const auto ep=layeredExteriorPairFor(input,rObs,rSrc);
    const DielectricRegion *slab=ip.region?ip.region:ep.region;if(!slab)return zero;
    const Vec3 n=NumericalEM::normalized(slab->normalAxis),d=rObs-rSrc;const Vec3 rhoVec=d-n*NumericalEM::dot(d,n);
    const double rho=NumericalEM::norm(rhoVec);if(rho<=1e-10)return zero;
    const Vec3 rhoHat=rhoVec*(1.0/rho);
    const double h=std::max(slab->thicknessM,1e-12);
    const double dr=std::max(5e-7,std::min(0.01*h,std::max(2e-5*h,0.002*std::max(rho,h))));
    const C gp=layeredWireSurfaceHedScalarResidualOverEpsilon(input,rObs+rhoHat*dr,rSrc,regularizationM,omega,k0);
    const C gm=layeredWireSurfaceHedScalarResidualOverEpsilon(input,rObs-rhoHat*dr,rSrc,regularizationM,omega,k0);
    const C derivative=(gp-gm)/(2.0*dr);
    return rhoHat*derivative;
}


// Surface RWG charge is HED/tangential-source charge.  In 5.34, when the
// observation point is strictly inside the slab, include the normal derivative
// as well as the in-plane derivative so a vertical via test can sense surface
// charge through E_z.  Face observations keep the audited tangential derivative.
ComplexVec3 layeredWireSurfaceHedScalarFullGradient(const Input &input,const Vec3 &rObs,const Vec3 &rSrc,
                                                     double regularizationM,double omega,double k0)
{
    ComplexVec3 grad=layeredWireSurfaceHedScalarTangentialGradient(input,rObs,rSrc,regularizationM,omega,k0);
    if(input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedNormal && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal && !complexLayeredMode(input.dielectricKernelModel))return grad;
    const auto pair=layeredInternalPairFor(input,rObs,rSrc);
    if(!pair.region || !pair.observationStrictInterior)return grad;
    const auto &slab=*pair.region;const Vec3 n=NumericalEM::normalized(slab.normalAxis);const double h=std::max(slab.thicknessM,1e-12);
    const double margin=std::min(pair.observationZM,h-pair.observationZM);
    if(margin<=1e-8)return grad;
    const double dz=std::max(2e-7,std::min({0.005*h,0.20*margin,2e-5}));
    const C gp=layeredWireSurfaceHedScalarResidualOverEpsilon(input,rObs+n*dz,rSrc,regularizationM,omega,k0);
    const C gm=layeredWireSurfaceHedScalarResidualOverEpsilon(input,rObs-n*dz,rSrc,regularizationM,omega,k0);
    grad+=n*((gp-gm)/(2.0*dz));
    return grad;
}

bool layeredVedNormalSourceSupported(const Input &input,const Vec3 &rObs,const Vec3 &rSrc,const Vec3 &sourceTangent)
{
    if(input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedNormal && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal && !complexLayeredMode(input.dielectricKernelModel))return false;
    const auto pair=layeredInternalPairFor(input,rObs,rSrc);if(!pair.region)return false;
    const Vec3 n=NumericalEM::normalized(pair.region->normalAxis),t=NumericalEM::normalized(sourceTangent);
    return std::abs(NumericalEM::dot(t,n))>=0.85;
}


bool layeredHedTangentialSourceSupported(const Input &input,const Vec3 &rObs,const Vec3 &rSrc,const Vec3 &sourceTangent)
{
    if((input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldInternalLayer && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedNormal && input.dielectricKernelModel!=DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal && !complexLayeredMode(input.dielectricKernelModel)))return false;
    const auto ip=layeredInternalPairFor(input,rObs,rSrc);const auto ep=layeredExteriorPairFor(input,rObs,rSrc);
    const DielectricRegion *slab=ip.region?ip.region:ep.region;if(!slab)return false;
    const Vec3 n=NumericalEM::normalized(slab->normalAxis),t=NumericalEM::normalized(sourceTangent);
    return std::abs(NumericalEM::dot(t,n))<=0.15;
}

C layeredScalarGreenOverEpsilon(const Input &input,const Vec3 &rObs,const Vec3 &rSrc,
                                double regularizationM,double omega,double k0,bool *used=nullptr,int *termsUsed=nullptr)
{
    const auto med=mediumForPair(input,rObs,rSrc,omega,k0);
    const C dynamicBaseline=green(rObs,rSrc,med.k,regularizationM)/med.epsilon;
    const auto pair=layeredSurfacePairFor(input,rObs,rSrc);
    if(!pair.region) return dynamicBaseline;

    if(used)*used=true;
    const auto &slab=*pair.region;
    const C epsr=std::max(1.0,slab.relativePermittivity)*C{1.0,-std::max(0.0,slab.lossTangent)};
    const C refl=(epsr-C{1.0,0.0})/(epsr+C{1.0,0.0});
    const C pref=(C{2.0,0.0}/(C{1.0,0.0}+epsr))/NumericalEM::Epsilon0;
    // fieldFillFactor already represents the dielectric loading captured by the
    // historical effective-medium kernel.  Apply the layered correction only to
    // the residual fraction so the two models do not double-count the substrate.
    const double correctionStrength=std::clamp(1.0-slab.fieldFillFactor,0.0,1.0);
    const Vec3 n=NumericalEM::normalized(slab.normalAxis),d=rObs-rSrc;
    const double axial=NumericalEM::dot(d,n);
    const double rho2=std::max(0.0,NumericalEM::dot(d,d)-axial*axial);
    const double rho=std::sqrt(rho2);
    const double h=std::max(slab.thicknessM,1e-12);

    auto quasiStaticValue=[&](bool recordTerms)
    {
        const int count=layeredImageTermCount(refl);
        if(recordTerms && termsUsed)*termsUsed=std::max(*termsUsed,count);
        C layeredStatic{0.0,0.0};
        if(pair.observationFace==pair.sourceFace)
        {
            // Preserve the baseline direct 1/R singular coefficient.  Replacing it
            // with the ideal zero-distance interface image makes the low-order RWG
            // self term dominate mesh refinement.  Only finite-distance images are
            // therefore added, exactly as in the audited 5.25 formulation.
            C rpow=refl;
            for(int image=1;image<=count;++image)
            {
                const double z=2.0*static_cast<double>(image)*h;
                layeredStatic+=(C{1.0,0.0}+refl)*rpow*staticGreenFromDistance(std::sqrt(rho2+z*z));
                rpow*=refl*refl;
            }
            return dynamicBaseline+correctionStrength*pref*layeredStatic;
        }

        C rpow{1.0,0.0};
        for(int image=0;image<count;++image)
        {
            const double z=(2.0*static_cast<double>(image)+1.0)*h;
            layeredStatic+=(C{1.0,0.0}+refl)*rpow*staticGreenFromDistance(std::sqrt(rho2+z*z));
            rpow*=refl*refl;
        }
        layeredStatic*=pref;
        const double directR=std::sqrt(NumericalEM::dot(d,d)+regularizationM*regularizationM);
        const C effectiveStatic=staticGreenFromDistance(directR)/med.epsilon;
        return dynamicBaseline+correctionStrength*(layeredStatic-effectiveStatic);
    };

    if(input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldScalar ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldTeVector ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldCrossFace ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer ||
        (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel))))
    {
        const bool sameFace=pair.observationFace==pair.sourceFace;
        const bool longitudinalHed=input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed ||
                                   input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight ||
                                   input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer ||
                             (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel)));
        const C spectral=longitudinalHed
            ? sommerfeldHedScalarGreenOverEpsilon(slab,k0,rho,sameFace)
            : sommerfeldScalarGreenOverEpsilon(slab,k0,rho,sameFace);
        const C spectralFull=sameFace
            ? dynamicBaseline+correctionStrength*spectral
            : dynamicBaseline+correctionStrength*(spectral-dynamicBaseline);
        const C quasiStaticFull=quasiStaticValue(false);
        if((input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldTeVector ||
            input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldCrossFace ||
            input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed ||
            input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer ||
        (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel)))) && sameFace && !longitudinalHed)
        {
            // 5.29 evaluates the HED TE/TM scalar spectrum as a diagnostic, but a
            // direct full-complex injection is not yet released: a 300 MHz two-plate
            // passivity check still exposes the missing longitudinal/transmitted
            // potential coupling. Keep the audited 5.26 reactive scalar projection
            // while adding the independently well-behaved TE vector-potential term.
            (void)sommerfeldHedSameFaceScalarGreenOverEpsilon(slab,k0,rho);
        }
        // 5.26, the guarded 5.29/5.30 transitions, the 5.31 HED longitudinal mode and
        // the 5.32 exterior-height, 5.33 internal-layer and 5.34 VED/via transitions retain the scalar
        // reactive projection until the remaining off-diagonal/full-complex terms pass the power test.

        // 5.26 compatibility path: scalar-only dynamic increment remains reactively
        // projected because its matching vector-potential dyadic is not active.
        return quasiStaticFull+C{(spectralFull-quasiStaticFull).real(),0.0};
    }

    return quasiStaticValue(true);
}

C surfaceVectorGreen(const Input &input,const Vec3 &rObs,const Vec3 &rSrc,
                     double regularizationM,double omega,double k0)
{
    const auto med=mediumForPair(input,rObs,rSrc,omega,k0);
    return green(rObs,rSrc,med.k,regularizationM);
}

C surfaceVectorGreenDot(const Input &input,const Vec3 &rObs,const Vec3 &rSrc,
                        const Vec3 &fObs,const Vec3 &fSrc,double regularizationM,
                        double omega,double k0)
{
    const auto med=mediumForPair(input,rObs,rSrc,omega,k0);
    const C baseline=green(rObs,rSrc,med.k,regularizationM)*NumericalEM::dot(fObs,fSrc);
    const bool teMode=input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldTeVector ||
                      input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldCrossFace ||
                      input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed ||
                      input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight ||
                      input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer ||
                             (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel)));
    if(!teMode) return baseline;
    const auto pair=layeredSurfacePairFor(input,rObs,rSrc);
    if(!pair.region) return baseline;
    const bool sameFace=pair.observationFace==pair.sourceFace;
    const bool crossFaceMode=input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldCrossFace ||
                             input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed ||
                             input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight ||
                             input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer ||
                             (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel)));
    if(!sameFace && !crossFaceMode) return baseline;
    const auto &slab=*pair.region;
    const Vec3 n=NumericalEM::normalized(slab.normalAxis);
    const Vec3 d=rObs-rSrc;
    const double axial=NumericalEM::dot(d,n);
    const Vec3 rhoVec=d-n*axial;
    const double rho=NumericalEM::norm(rhoVec);
    const Vec3 foT=fObs-n*NumericalEM::dot(fObs,n);
    const Vec3 fsT=fSrc-n*NumericalEM::dot(fSrc,n);
    const auto vg=sommerfeldTangentialVectorGreen(slab,k0,rho,sameFace);
    C dyadicDot{0.0,0.0};
    if(sameFace)
    {
        // In the 5.29 HED Sommerfeld-potential gauge G_A,xx=G_A,yy for tangential
        // same-face currents, governed by R_TE. 5.30 keeps that audited expression.
        const C isotropic=0.5*(vg.longitudinal+vg.transverse);
        dyadicDot=isotropic*NumericalEM::dot(foT,fsT);
    }
    else if(rho>1e-12)
    {
        // Opposite slab faces require the transmitted TE/TM tangential dyadic.
        // Resolve the test/source RWG vectors into the in-plane longitudinal
        // (rho-hat) and transverse (n x rho-hat) projectors before contraction.
        const Vec3 rhoHat=rhoVec*(1.0/rho);
        const Vec3 tHat=NumericalEM::normalized(cross(n,rhoHat));
        dyadicDot=vg.longitudinal*(NumericalEM::dot(foT,rhoHat)*NumericalEM::dot(fsT,rhoHat))+
                   vg.transverse*(NumericalEM::dot(foT,tHat)*NumericalEM::dot(fsT,tHat));
    }
    else
    {
        // At rho=0 the in-plane azimuth is undefined. The dyadic must be isotropic
        // by rotational symmetry, so use the average of the limiting components.
        const C isotropic=0.5*(vg.longitudinal+vg.transverse);
        dyadicDot=isotropic*NumericalEM::dot(foT,fsT);
    }
    const double correctionStrength=std::clamp(1.0-slab.fieldFillFactor,0.0,1.0);
    if(!sameFace && crossFaceMode)
    {
        // The cross-face table represents the complete transmitted layered vector
        // Green dyadic, while `baseline` is already present in the assembled operator.
        // Subtract the homogeneous/effective-medium direct contribution before applying
        // the residual layered correction so transmission is not counted twice.
        dyadicDot-=baseline;
        // A full complex transmitted-vector increment by itself fails the passive
        // 300 MHz two-plate sanity case because the remaining longitudinal/off-interface
        // MPIE terms are not yet present. Keep only Re(Delta G_A^trans); after multiplication
        // by j*omega*mu this contributes a reactive correction without inventing gain/loss.
        dyadicDot=C{dyadicDot.real(),0.0};
    }
    return baseline+correctionStrength*dyadicDot;
}

struct SingularSourceIntegral
{
    C vectorGreen{0,0};
    C scalarGreenOverEpsilon{0,0};
};

SingularSourceIntegral integrateSameTriangleSourceDuffy(const Input &input,
                                                         const BasisData &sourceBasis,
                                                         const BasisSupport &sourceSupport,
                                                         const TriData &sourceTriangle,
                                                         const Vec3 &observationPoint,
                                                         const Vec3 &observationBasisValue,
                                                         double omega,double k0)
{
    // For a fixed observation point inside the source triangle, split the triangle
    // into three sub-triangles sharing that observation point and use a Duffy map
    // r' = r + u[(1-v)a + vb].  The Jacobian contains u, which analytically
    // cancels the 1/R weak singularity of the scalar Green function at u -> 0.
    // This avoids the old equivalent-radius self regularization while keeping the
    // expensive singular quadrature local to true same-triangle RWG interactions.
    constexpr std::array<double,4> gx{{
        -0.8611363115940525752,-0.3399810435848562648,
         0.3399810435848562648, 0.8611363115940525752}};
    constexpr std::array<double,4> gw{{
        0.3478548451374538574,0.6521451548625461426,
        0.6521451548625461426,0.3478548451374538574}};
    const std::array<Vec3,3> v{{sourceTriangle.tri.a,sourceTriangle.tri.b,sourceTriangle.tri.c}};
    SingularSourceIntegral out;
    for(int side=0;side<3;++side)
    {
        const Vec3 a=v[static_cast<std::size_t>(side)]-observationPoint;
        const Vec3 b=v[static_cast<std::size_t>((side+1)%3)]-observationPoint;
        const double crossMag=NumericalEM::norm(cross(a,b));
        if(!(crossMag>1e-30)) continue;
        for(int iu=0;iu<4;++iu)
        {
            const double u=0.5*(gx[static_cast<std::size_t>(iu)]+1.0);
            const double wu=0.5*gw[static_cast<std::size_t>(iu)];
            for(int iv=0;iv<4;++iv)
            {
                const double vv=0.5*(gx[static_cast<std::size_t>(iv)]+1.0);
                const double wv=0.5*gw[static_cast<std::size_t>(iv)];
                const Vec3 radial=a*(1.0-vv)+b*vv;
                const Vec3 sourcePoint=observationPoint+radial*u;
                const double jacobian=crossMag*u;
                const Vec3 fn=rwgValue(sourceBasis,sourceSupport,sourceTriangle,sourcePoint);
                const C GvDot=surfaceVectorGreenDot(input,observationPoint,sourcePoint,observationBasisValue,fn,0.0,omega,k0);
                const C Gs=layeredScalarGreenOverEpsilon(input,observationPoint,sourcePoint,0.0,omega,k0);
                const double weight=wu*wv*jacobian;
                out.vectorGreen += GvDot*weight;
                out.scalarGreenOverEpsilon += Gs*weight;
            }
        }
    }
    return out;
}

ComplexVec3 gradGreen(const Vec3&r,const Vec3&rp,C k,double regularization)
{
    const Vec3 d=r-rp;const double R=std::sqrt(NumericalEM::dot(d,d)+regularization*regularization);
    if(!(R>1e-18))return {};
    const C G=std::exp(-J*k*R)/(4.0*NumericalEM::Pi*R);
    const C scale=G*(-J*k-1.0/R)/R;
    return d*scale;
}

double matrixBlockReciprocityRelative(const std::vector<std::vector<C>> &a,int offset,int count)
{
    double num=0.0,den=0.0;
    for(int i=0;i<count;++i) for(int j=i+1;j<count;++j)
    {
        const C zij=a[static_cast<std::size_t>(offset+i)][static_cast<std::size_t>(offset+j)];
        const C zji=a[static_cast<std::size_t>(offset+j)][static_cast<std::size_t>(offset+i)];
        num+=std::norm(zij-zji);
        den+=0.5*(std::norm(zij)+std::norm(zji));
    }
    return std::sqrt(num/std::max(den,1e-30));
}

void symmetrizeReciprocalMatrixBlock(std::vector<std::vector<C>> &a,int offset,int count)
{
    for(int i=0;i<count;++i) for(int j=i+1;j<count;++j)
    {
        const C avg=0.5*(a[static_cast<std::size_t>(offset+i)][static_cast<std::size_t>(offset+j)]+
                         a[static_cast<std::size_t>(offset+j)][static_cast<std::size_t>(offset+i)]);
        a[static_cast<std::size_t>(offset+i)][static_cast<std::size_t>(offset+j)]=avg;
        a[static_cast<std::size_t>(offset+j)][static_cast<std::size_t>(offset+i)]=avg;
    }
}

C surfaceMatrixEntry(const Input &input,const BasisData&m,const BasisData&n,const std::vector<TriData>&tri,double k0,double omega,double selfFactor)
{
    (void)selfFactor; // 5.23: singular-aware local quadrature supersedes radius-only self regularization.
    C vectorTerm{0,0},scalarTerm{0,0};const std::array<BasisSupport,2>ms{m.plus,m.minus},ns{n.plus,n.minus};
    for(const auto&sm:ms)for(const auto&sn:ns)
    {
        if(sm.triangle<0||sn.triangle<0)continue;
        const TriData&tm=tri[static_cast<std::size_t>(sm.triangle)],&tn=tri[static_cast<std::size_t>(sn.triangle)];
        const double divM=rwgDivergence(m,sm,tm),divN=rwgDivergence(n,sn,tn);
        const bool same=sm.triangle==sn.triangle;
        if(same)
        {
            const auto qm=triangleQuadratureNear(tm);
            for(const auto&po:qm)
            {
                const Vec3 fm=rwgValue(m,sm,tm,po.p);
                const auto inner=integrateSameTriangleSourceDuffy(input,n,sn,tn,po.p,fm,omega,k0);
                vectorTerm += inner.vectorGreen*po.weight;
                scalarTerm += divM*divN*inner.scalarGreenOverEpsilon*po.weight;
            }
            continue;
        }
        if(sharedVertexCount(tm,tn)>0)
        {
            // Adjacent triangles have a weak boundary singularity. One local
            // 1-to-4 subdivision on each triangle concentrates the ordinary
            // 3-point rule near the common edge/vertex without globally exploding
            // the dense O(N^2) assembly cost.
            const auto qm=triangleQuadratureComposite1(tm),qn=triangleQuadratureComposite1(tn);
            for(const auto&po:qm)for(const auto&ps:qn)
            {
                const Vec3 fm=rwgValue(m,sm,tm,po.p),fn=rwgValue(n,sn,tn,ps.p);
                const C GvDot=surfaceVectorGreenDot(input,po.p,ps.p,fm,fn,0.0,omega,k0);
                const C Gs=layeredScalarGreenOverEpsilon(input,po.p,ps.p,0.0,omega,k0);
                const double w=po.weight*ps.weight;
                vectorTerm+=GvDot*w;
                scalarTerm+=divM*divN*Gs*w;
            }
            continue;
        }
        const auto qm=triangleQuadrature(tm),qn=triangleQuadrature(tn);
        for(const auto&po:qm)for(const auto&ps:qn)
        {
            const Vec3 fm=rwgValue(m,sm,tm,po.p),fn=rwgValue(n,sn,tn,ps.p);
            bool useDyadic=false;
            if(input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldCrossFace ||
               input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed ||
               input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer ||
        (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel))))
            {
                const auto layeredPair=layeredSurfacePairFor(input,po.p,ps.p);
                useDyadic=layeredPair.region && layeredPair.observationFace!=layeredPair.sourceFace;
            }
            const C GvDot=useDyadic
                ? surfaceVectorGreenDot(input,po.p,ps.p,fm,fn,0.0,omega,k0)
                : surfaceVectorGreen(input,po.p,ps.p,0.0,omega,k0)*NumericalEM::dot(fm,fn);
            const C Gs=layeredScalarGreenOverEpsilon(input,po.p,ps.p,0.0,omega,k0);
            const double w=po.weight*ps.weight;
            vectorTerm+=GvDot*w;
            scalarTerm+=divM*divN*Gs*w;
        }
    }
    const C efie=J*omega*NumericalEM::Mu0*vectorTerm+(1.0/(J*omega))*scalarTerm;
    const C zs=finiteSheetImpedance(input,omega);
    return efie + zs*surfaceBasisDotIntegral(m,n,tri);
}

ComplexVec3 surfaceElectricFieldForBasis(const Input &input,const BasisData&b,const std::vector<TriData>&tri,const Vec3&r,double k0,double omega,double mutualFactor,double wireRadius)
{
    ComplexVec3 e{};
    for(const auto&s:{b.plus,b.minus})
    {
        if(s.triangle<0)continue;
        const TriData&t=tri[static_cast<std::size_t>(s.triangle)];const auto q=triangleQuadrature(t);
        const double req=std::sqrt(std::max(t.area,1e-30)/NumericalEM::Pi);const double reg=std::max({1e-12,wireRadius,mutualFactor*req});const double div=rwgDivergence(b,s,t);
        for(const auto&qp:q)
        {
            const auto med=mediumForPair(input,r,qp.p,omega,k0);
            const Vec3 f=rwgValue(b,s,t,qp.p);const C G=green(r,qp.p,med.k,reg);e+=f*(J*omega*NumericalEM::Mu0*G*qp.weight);
            e+=layeredWireSurfaceVectorPotentialCorrection(input,r,qp.p,f,reg,omega,k0)*qp.weight;
            const ComplexVec3 gg=gradGreen(r,qp.p,med.k,reg);e+=gg*((-1.0/(J*omega*med.epsilon))*div*qp.weight);
            const ComplexVec3 layeredGrad=(input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel)))
                ? layeredWireSurfaceHedScalarFullGradient(input,r,qp.p,reg,omega,k0)
                : layeredWireSurfaceHedScalarTangentialGradient(input,r,qp.p,reg,omega,k0);
            e+=layeredGrad*((-1.0/(J*omega))*div*qp.weight);
        }
    }
    return e;
}

C surfaceTestOfWirePulse(const Input &input,const BasisData&m,const std::vector<TriData>&tri,const NumericalEM::WireNetworkMeshSegment&src,double k0,double omega)
{
    C z{0,0};const Vec3 mid=(src.p0M+src.p1M)*0.5,half=(src.p1M-src.p0M)*0.5;
    for(const auto&sm:{m.plus,m.minus})
    {
        if(sm.triangle<0)continue;
        const TriData&tm=tri[static_cast<std::size_t>(sm.triangle)];const auto qm=triangleQuadrature(tm);
        for(const auto&po:qm)
        {
            const Vec3 f=rwgValue(m,sm,tm,po.p);C eDotF{0,0};
            for(int g=0;g<8;++g)for(int sgn:{-1,1})
            {
                const Vec3 rs=mid+half*(sgn*GlX[static_cast<std::size_t>(g)]);
                const double a=std::max(src.radiusM,1e-12);
                const C baseProjected=
                    f.x*projectedEfieKernel(input,po.p,{1,0,0},rs,src.tangent,a,k0,omega)+
                    f.y*projectedEfieKernel(input,po.p,{0,1,0},rs,src.tangent,a,k0,omega)+
                    f.z*projectedEfieKernel(input,po.p,{0,0,1},rs,src.tangent,a,k0,omega);
                const C exteriorCorrection=layeredWireSurfaceVectorPotentialCorrectionDot(input,po.p,f,rs,src.tangent,a,omega,k0);
                eDotF+=GlW[static_cast<std::size_t>(g)]*(baseProjected+exteriorCorrection);
            }
            z+=0.5*src.lengthM*eDotF*po.weight;
        }
    }
    return z;
}

bool boundaryEdgeRequested(const Input &input,const Vec3 &a,const Vec3 &b,int surfaceIndex)
{
    if(!input.enableTerminalHalfRwg) return false;
    const Vec3 c=(a+b)*0.5;
    auto requested=[&](const Vec3 &p,int requestedSurface,double tol){
        if(requestedSurface>=0 && surfaceIndex!=requestedSurface) return false;
        const Vec3 ab=b-a; const double d2=NumericalEM::dot(ab,ab); double dist=NumericalEM::norm(p-a);
        if(d2>1e-30){const double tt=std::clamp(NumericalEM::dot(p-a,ab)/d2,0.0,1.0);dist=NumericalEM::norm(p-(a+ab*tt));}
        return dist<=std::max(tol,1e-7) || NumericalEM::norm(p-c)<=std::max(tol,1e-7);
    };
    for(const auto &f:input.surfaceReferencedFeeds) if(requested(f.positionM,f.surfaceIndex,f.mappingToleranceM)) return true;
    for(const auto &f:input.differentialSurfaceFeeds)
    {
        if(requested(f.positivePositionM,f.positiveSurfaceIndex,f.mappingToleranceM)) return true;
        if(requested(f.negativePositionM,f.negativeSurfaceIndex,f.mappingToleranceM)) return true;
    }
    for(const auto &j:input.galvanicJunctions) if(requested(j.positionM,j.surfaceIndex,j.mappingToleranceM)) return true;
    return false;
}

bool buildSurfaceBasis(const Input &solverInput,const std::vector<PecSurfaceMom::Triangle3D>&input,double tol,std::vector<Vec3>&vertices,std::vector<TriData>&tri,std::vector<BasisData>&bases,int&boundary,int&halfCount,std::string&error)
{
    std::map<VertexKey,int>vertexMap;auto vertexId=[&](const Vec3&p){const VertexKey key=keyFor(p,tol);auto it=vertexMap.find(key);if(it!=vertexMap.end())return it->second;const int id=static_cast<int>(vertices.size());vertexMap.emplace(key,id);vertices.push_back(p);return id;};
    tri.reserve(input.size());
    for(const auto&t:input){const double A=triangleArea(t);if(!(A>1e-18)||!std::isfinite(A))continue;TriData td;td.tri=t;td.area=A;td.centroid=centroid(t);td.vertex={vertexId(t.a),vertexId(t.b),vertexId(t.c)};if(td.vertex[0]==td.vertex[1]||td.vertex[1]==td.vertex[2]||td.vertex[2]==td.vertex[0])continue;tri.push_back(td);}
    if(tri.empty()){error="All hybrid PEC triangles are degenerate";return false;}
    std::map<EdgeKey,std::vector<EdgeUse>>uses;
    for(int ti=0;ti<static_cast<int>(tri.size());++ti){const auto&v=tri[static_cast<std::size_t>(ti)].vertex;const std::array<std::array<int,3>,3>e{{{{0,1,2}},{{1,2,0}},{{2,0,1}}}};for(const auto&q:e){int a=v[q[0]],b=v[q[1]];if(a>b)std::swap(a,b);uses[{a,b}].push_back({ti,q[2]});}}
    boundary=0;halfCount=0;
    for(const auto&kv:uses)
    {
        const auto&edge=kv.first;const auto&u=kv.second;
        const Vec3&a=vertices[static_cast<std::size_t>(edge.a)],&b=vertices[static_cast<std::size_t>(edge.b)];const double l=NumericalEM::norm(b-a);if(!(l>1e-15))continue;
        if(u.size()==1)
        {
            ++boundary;
            const int ti=u[0].triangle;const int si=tri[static_cast<std::size_t>(ti)].tri.surfaceIndex;
            if(boundaryEdgeRequested(solverInput,a,b,si))
            {
                BasisData rwg;rwg.edgeV0=edge.a;rwg.edgeV1=edge.b;rwg.edgeLength=l;
                rwg.plus={ti,tri[static_cast<std::size_t>(ti)].vertex[u[0].freeLocal],+1.0};
                rwg.minus={-1,-1,-1.0};bases.push_back(rwg);++halfCount;
            }
            continue;
        }
        if(u.size()!=2){error="Non-manifold PEC surface in hybrid solve";return false;}
        BasisData rwg;rwg.edgeV0=edge.a;rwg.edgeV1=edge.b;rwg.edgeLength=l;rwg.plus={u[0].triangle,tri[static_cast<std::size_t>(u[0].triangle)].vertex[u[0].freeLocal],+1.0};rwg.minus={u[1].triangle,tri[static_cast<std::size_t>(u[1].triangle)].vertex[u[1].freeLocal],-1.0};bases.push_back(rwg);
    }
    if(bases.empty()){error="No RWG/terminal half-RWG edges were found for the hybrid surface mesh";return false;}return true;
}

struct FeedBinding{int e0=-1,e1=-1;double s0=1.0,s1=1.0;};
bool bindFeeds(const NumericalEM::WireNetworkMomInput&input,const std::vector<NumericalEM::WireNetworkMeshSegment>&seg,std::vector<FeedBinding>&bindings,std::vector<C>&rhs,std::string&error)
{
    const double tol=std::max(input.nodeMergeToleranceM*8.0,1e-7);bindings.resize(input.feeds.size());rhs.assign(seg.size(),{0,0});bool any=false;
    auto lexLess=[](const Vec3&a,const Vec3&b){if(a.x!=b.x)return a.x<b.x;if(a.y!=b.y)return a.y<b.y;return a.z<b.z;};
    for(std::size_t fi=0;fi<input.feeds.size();++fi)
    {
        std::vector<int>touch;
        for(int i=0;i<static_cast<int>(seg.size());++i)if(NumericalEM::norm(seg[static_cast<std::size_t>(i)].p0M-input.feeds[fi].positionM)<=tol||NumericalEM::norm(seg[static_cast<std::size_t>(i)].p1M-input.feeds[fi].positionM)<=tol)touch.push_back(i);
        if(touch.size()!=2){error="Hybrid delta-gap feed mapping requires exactly two meshed wire segments. Branched wire junctions are supported, but place the voltage gap on a degree-2 arm rather than directly on a T/Y/X node.";return false;}
        const auto &a=seg[static_cast<std::size_t>(touch[0])],&b=seg[static_cast<std::size_t>(touch[1])];
        const Vec3 p=input.feeds[fi].positionM;
        const bool aAtP0=NumericalEM::norm(a.p0M-p)<=tol,bAtP0=NumericalEM::norm(b.p0M-p)<=tol;
        const Vec3 oa=aAtP0?a.p1M:a.p0M,ob=bAtP0?b.p1M:b.p0M;
        // Define one deterministic virtual path through the gap. s*I is the current in that
        // path direction, making feed impedance invariant to how individual CAD spans were drawn.
        double s0=1.0,s1=1.0;
        if(lexLess(oa,ob))
        {
            s0=aAtP0?-1.0:+1.0; // other A -> feed
            s1=bAtP0?+1.0:-1.0; // feed -> other B
        }
        else
        {
            s1=bAtP0?-1.0:+1.0; // other B -> feed
            s0=aAtP0?+1.0:-1.0; // feed -> other A
        }
        bindings[fi]={touch[0],touch[1],s0,s1};
        rhs[static_cast<std::size_t>(touch[0])]+=0.5*s0*input.feeds[fi].voltageV/std::max(a.lengthM,1e-18);
        rhs[static_cast<std::size_t>(touch[1])]+=0.5*s1*input.feeds[fi].voltageV/std::max(b.lengthM,1e-18);
        any=any||std::abs(input.feeds[fi].voltageV)>1e-18;
    }
    (void)any; return true;
}

int basisSurfaceIndex(const BasisData &b,const std::vector<TriData> &tri)
{
    if(b.plus.triangle>=0) return tri[static_cast<std::size_t>(b.plus.triangle)].tri.surfaceIndex;
    if(b.minus.triangle>=0) return tri[static_cast<std::size_t>(b.minus.triangle)].tri.surfaceIndex;
    return -1;
}

double pointSegmentDistance(const Vec3 &p,const Vec3 &a,const Vec3 &b)
{
    const Vec3 ab=b-a;const double d2=NumericalEM::dot(ab,ab);
    if(!(d2>1e-30)) return NumericalEM::norm(p-a);
    const double t=std::clamp(NumericalEM::dot(p-a,ab)/d2,0.0,1.0);
    return NumericalEM::norm(p-(a+ab*t));
}

double pointTriangleDistance(const Vec3 &p,const TriData &t)
{
    // Closest-point test after Ericson, Real-Time Collision Detection.  Using the
    // triangle itself (rather than an arbitrary RWG edge) makes terminal mapping
    // independent of the local diagonal direction of a rectangular mesh.
    const Vec3 a=t.tri.a,b=t.tri.b,c=t.tri.c;
    const Vec3 ab=b-a,ac=c-a,ap=p-a;
    const double d1=NumericalEM::dot(ab,ap),d2=NumericalEM::dot(ac,ap);
    if(d1<=0.0&&d2<=0.0)return NumericalEM::norm(ap);
    const Vec3 bp=p-b;const double d3=NumericalEM::dot(ab,bp),d4=NumericalEM::dot(ac,bp);
    if(d3>=0.0&&d4<=d3)return NumericalEM::norm(bp);
    const double vc=d1*d4-d3*d2;
    if(vc<=0.0&&d1>=0.0&&d3<=0.0){const double v=d1/std::max(d1-d3,1e-30);return NumericalEM::norm(p-(a+ab*v));}
    const Vec3 cp=p-c;const double d5=NumericalEM::dot(ab,cp),d6=NumericalEM::dot(ac,cp);
    if(d6>=0.0&&d5<=d6)return NumericalEM::norm(cp);
    const double vb=d5*d2-d1*d6;
    if(vb<=0.0&&d2>=0.0&&d6<=0.0){const double w=d2/std::max(d2-d6,1e-30);return NumericalEM::norm(p-(a+ac*w));}
    const double va=d3*d6-d5*d4;
    if(va<=0.0&&(d4-d3)>=0.0&&(d5-d6)>=0.0)
    {
        const Vec3 bc=c-b;const double w=(d4-d3)/std::max((d4-d3)+(d5-d6),1e-30);
        return NumericalEM::norm(p-(b+bc*w));
    }
    const Vec3 n=cross(ab,ac);const double nn=NumericalEM::norm(n);
    return nn>1e-30?std::abs(NumericalEM::dot(p-a,n))/nn:std::min({NumericalEM::norm(ap),NumericalEM::norm(bp),NumericalEM::norm(cp)});
}

// Composite Gauss integration for pulse-wire self and near interactions.  The reactive
// thin-wire kernel changes on the radius scale, so one fixed 16-point panel can under-resolve
// a source segment several radii long.  Far interactions retain one panel; only near terms are
// subdivided.  Keep this numerically identical in spirit to solveWireNetworkMom().
C integrateProjectedWirePulse(const Input &input,const Vec3 &rObs,const Vec3 &tObs,
                              const Vec3 &p0,const Vec3 &p1,const Vec3 &tSrc,
                              double radiusM,double k0,double omega)
{
    const double lengthM=NumericalEM::norm(p1-p0);if(!(lengthM>1e-18))return {0,0};
    const double a=std::max(radiusM,1e-12);const double distanceM=pointSegmentDistance(rObs,p0,p1);
    int panels=1;if(distanceM<std::max(4.0*a,0.75*lengthM))panels=std::clamp(static_cast<int>(std::ceil(lengthM/(1.25*a))),1,32);
    C total{0,0};
    for(int panel=0;panel<panels;++panel)
    {
        const double u0=double(panel)/double(panels),u1=double(panel+1)/double(panels);
        const Vec3 a0=p0+(p1-p0)*u0,a1=p0+(p1-p0)*u1,mid=(a0+a1)*0.5,half=(a1-a0)*0.5;C sum{0,0};
        for(int g=0;g<8;++g)for(int sgn:{-1,1})
        {
            const Vec3 rs=mid+half*(sgn*GlX[static_cast<std::size_t>(g)]);
            sum+=GlW[static_cast<std::size_t>(g)]*projectedEfieKernel(input,rObs,tObs,rs,tSrc,a,k0,omega);
        }
        total+=0.5*NumericalEM::norm(a1-a0)*sum;
    }
    return total;
}

double segmentSegmentDistance(const Vec3 &p0,const Vec3 &p1,const Vec3 &q0,const Vec3 &q1)
{
    const Vec3 d1=p1-p0,d2=q1-q0,r=p0-q0;
    const double aa=NumericalEM::dot(d1,d1),ee=NumericalEM::dot(d2,d2),f=NumericalEM::dot(d2,r);
    double u=0.0,v=0.0;
    if(aa<=1e-30&&ee<=1e-30)return NumericalEM::norm(p0-q0);
    if(aa<=1e-30)v=std::clamp(f/ee,0.0,1.0);
    else
    {
        const double c=NumericalEM::dot(d1,r);
        if(ee<=1e-30)u=std::clamp(-c/aa,0.0,1.0);
        else
        {
            const double b=NumericalEM::dot(d1,d2),den=aa*ee-b*b;
            if(std::abs(den)>1e-30)u=std::clamp((b*f-c*ee)/den,0.0,1.0);
            v=(b*u+f)/ee;
            if(v<0.0){v=0.0;u=std::clamp(-c/aa,0.0,1.0);}
            else if(v>1.0){v=1.0;u=std::clamp((b-c)/aa,0.0,1.0);}
        }
    }
    return NumericalEM::norm((p0+d1*u)-(q0+d2*v));
}

// Two-dimensional composite Galerkin quadrature for rooftop-wire self/near terms.  A fixed
// 4x4 rule is demonstrably insufficient when a support span is only a few wire radii long:
// it can alias the sharply varying reactive Pocklington kernel and make rooftop Zin oscillate
// with the mesh.  Resolve each close support on roughly the radius scale while retaining the
// compact rule for far pairs.  The hybrid dielectric Green function is preserved unchanged.
C integrateProjectedWireRooftopPair(const Input &input,
                                    const NumericalEM::WireNetworkMeshSegment &obs,bool obsPeakAtP0,
                                    const NumericalEM::WireNetworkMeshSegment &src,bool srcPeakAtP0,
                                    double radiusM,double k0,double omega)
{
    constexpr std::array<double,4> gx{-0.8611363115940526,-0.3399810435848563,0.3399810435848563,0.8611363115940526};
    constexpr std::array<double,4> gw{0.3478548451374538,0.6521451548625461,0.6521451548625461,0.3478548451374538};
    const auto shape=[](bool peakAtP0,double xi){const double u=0.5*(xi+1.0);return peakAtP0?(1.0-u):u;};
    const double lo=obs.lengthM,ls=src.lengthM;if(!(lo>1e-18)||!(ls>1e-18))return {0,0};
    const double a=std::max(radiusM,1e-12);
    const double d=segmentSegmentDistance(obs.p0M,obs.p1M,src.p0M,src.p1M);
    int po=1,ps=1;
    if(d<std::max(5.0*a,0.60*std::max(lo,ls)))
    {
        po=std::clamp(static_cast<int>(std::ceil(lo/a))+2,4,48);
        ps=std::clamp(static_cast<int>(std::ceil(ls/a))+2,4,48);
    }
    const Vec3 od=obs.p1M-obs.p0M,sd=src.p1M-src.p0M;C total{0,0};
    for(int io=0;io<po;++io)
    {
        const double u0=double(io)/double(po),u1=double(io+1)/double(po),uc=0.5*(u0+u1),uh=0.5*(u1-u0);
        for(int is=0;is<ps;++is)
        {
            const double v0=double(is)/double(ps),v1=double(is+1)/double(ps),vc=0.5*(v0+v1),vh=0.5*(v1-v0);C pair{0,0};
            for(int go=0;go<4;++go)
            {
                const double u=uc+uh*gx[static_cast<std::size_t>(go)],xo=2.0*u-1.0;
                const Vec3 ro=obs.p0M+od*u;const double fo=shape(obsPeakAtP0,xo);
                for(int gs=0;gs<4;++gs)
                {
                    const double v=vc+vh*gx[static_cast<std::size_t>(gs)],xs=2.0*v-1.0;
                    const Vec3 rs=src.p0M+sd*v;const double fs=shape(srcPeakAtP0,xs);
                    pair+=gw[static_cast<std::size_t>(go)]*gw[static_cast<std::size_t>(gs)]*fo*fs*
                          projectedEfieKernel(input,ro,obs.tangent,rs,src.tangent,a,k0,omega);
                }
            }
            total+=(lo*uh)*(ls*vh)*pair;
        }
    }
    return total;
}

struct WireTouch
{
    int segment=-1;
    double sign=1.0; // current positive away from the mapped junction position
    double distanceM=0.0;
};

std::vector<WireTouch> touchingWireSegments(const std::vector<NumericalEM::WireNetworkMeshSegment> &wire,const Vec3 &p,double tol)
{
    // Treat mapping tolerance as a search radius for the nearest *topological node*,
    // not as a radius that collects every nearby mesh segment. This matters for short
    // probe/via wires: a useful surface-mapping tolerance can be larger than several
    // wire-segment lengths and must still select only the requested endpoint/node.
    struct NodeTouches{Vec3 p{};std::vector<WireTouch> touches;};
    std::vector<NodeTouches> nodes;
    const double nodeMergeTol=1e-9;
    auto addEndpoint=[&](int segment,const Vec3 &ep,double sign)
    {
        for(auto &n:nodes)
        {
            if(NumericalEM::norm(n.p-ep)<=nodeMergeTol)
            {
                n.touches.push_back({segment,sign,NumericalEM::norm(ep-p)});
                return;
            }
        }
        NodeTouches n;n.p=ep;n.touches.push_back({segment,sign,NumericalEM::norm(ep-p)});nodes.push_back(std::move(n));
    };
    for(int i=0;i<static_cast<int>(wire.size());++i)
    {
        const auto &w=wire[static_cast<std::size_t>(i)];
        addEndpoint(i,w.p0M,+1.0);
        addEndpoint(i,w.p1M,-1.0);
    }
    int best=-1;double bestDistance=std::numeric_limits<double>::infinity();
    for(int i=0;i<static_cast<int>(nodes.size());++i)
    {
        const double d=NumericalEM::norm(nodes[static_cast<std::size_t>(i)].p-p);
        if(d<=tol && d<bestDistance){best=i;bestDistance=d;}
    }
    if(best<0)return {};
    auto out=nodes[static_cast<std::size_t>(best)].touches;
    for(auto &t:out)t.distanceM=bestDistance;
    return out;
}


std::vector<std::vector<WireTouch>> branchedWireNodes(const std::vector<NumericalEM::WireNetworkMeshSegment> &wire)
{
    struct NodeTouches{Vec3 p{};std::vector<WireTouch> touches;};
    std::vector<NodeTouches> nodes;
    const double tol=1e-9;
    auto add=[&](int segment,const Vec3&p,double sign)
    {
        for(auto &n:nodes)if(NumericalEM::norm(n.p-p)<=tol){n.touches.push_back({segment,sign,0.0});return;}
        NodeTouches n;n.p=p;n.touches.push_back({segment,sign,0.0});nodes.push_back(std::move(n));
    };
    for(int i=0;i<static_cast<int>(wire.size());++i)
    {
        const auto&w=wire[static_cast<std::size_t>(i)];
        add(i,w.p0M,+1.0); // positive current leaves p0
        add(i,w.p1M,-1.0); // positive current enters p1
    }
    std::vector<std::vector<WireTouch>> out;
    for(auto &n:nodes)if(n.touches.size()>2)out.push_back(std::move(n.touches));
    return out;
}

struct SurfaceMap
{
    int basis=-1;
    int surfaceIndex=-1;
    Vec3 edgeCenter{};
    double distanceM=std::numeric_limits<double>::infinity();
};

SurfaceMap nearestSurfaceBasis(const Vec3 &p,int requestedSurface,const std::vector<Vec3> &vertices,const std::vector<BasisData> &bases,const std::vector<TriData> &tri)
{
    SurfaceMap best;
    for(int i=0;i<static_cast<int>(bases.size());++i)
    {
        const auto &b=bases[static_cast<std::size_t>(i)];const int si=basisSurfaceIndex(b,tri);
        if(requestedSurface>=0 && si!=requestedSurface) continue;
        const Vec3 a=vertices[static_cast<std::size_t>(b.edgeV0)],bb=vertices[static_cast<std::size_t>(b.edgeV1)];
        const double d=pointSegmentDistance(p,a,bb);
        if(d<best.distanceM){best.basis=i;best.surfaceIndex=si;best.edgeCenter=(a+bb)*0.5;best.distanceM=d;}
    }
    return best;
}

struct SurfaceFluxTerm
{
    int basis=-1;
    double integratedDivergenceM=0.0; // integral(div f_n dS) over the local terminal patch
};

struct SurfaceTerminalStencil
{
    SurfaceMap representative;
    std::vector<SurfaceFluxTerm> fluxTerms;
    int triangleCount=0;
    double patchRadiusM=0.0;
    double localCellSizeM=0.0;
    double surfaceDistanceM=std::numeric_limits<double>::infinity();
    Vec3 surfaceNormal{0.0,0.0,1.0};
};

SurfaceTerminalStencil buildSurfaceTerminalStencil(const Vec3 &p,int requestedSurface,
                                                    const std::vector<Vec3> &vertices,
                                                    const std::vector<BasisData> &bases,
                                                    const std::vector<TriData> &tri)
{
    SurfaceTerminalStencil out;out.representative=nearestSurfaceBasis(p,requestedSurface,vertices,bases,tri);
    int nearestTri=-1;double nearestCentroid=std::numeric_limits<double>::infinity(),nearestSurface=std::numeric_limits<double>::infinity();
    double localH=0.0;
    for(int ti=0;ti<static_cast<int>(tri.size());++ti)
    {
        const auto &t=tri[static_cast<std::size_t>(ti)];if(requestedSurface>=0&&t.tri.surfaceIndex!=requestedSurface)continue;
        const double ds=pointTriangleDistance(p,t);nearestSurface=std::min(nearestSurface,ds);
        const double dc=NumericalEM::norm(t.centroid-p);
        if(dc<nearestCentroid){nearestCentroid=dc;nearestTri=ti;localH=std::sqrt(std::max(2.0*t.area,1e-30));}
    }
    out.surfaceDistanceM=nearestSurface;
    if(nearestTri<0)return out;
    {
        const auto &tt=tri[static_cast<std::size_t>(nearestTri)];
        out.surfaceNormal=NumericalEM::normalized(cross(tt.tri.b-tt.tri.a,tt.tri.c-tt.tri.a));
        out.localCellSizeM=std::sqrt(std::max(2.0*tt.area,1e-30));
    }
    // A one-ring-sized patch is intentionally tied to the local mesh scale.  As the
    // RWG mesh is refined the excitation/return footprint shrinks toward the physical
    // wire terminal instead of jumping to whichever single edge happens to be nearest.
    out.patchRadiusM=std::max(1.65*localH,1.10*nearestCentroid);
    std::vector<unsigned char> selected(tri.size(),0);
    for(int ti=0;ti<static_cast<int>(tri.size());++ti)
    {
        const auto &t=tri[static_cast<std::size_t>(ti)];if(requestedSurface>=0&&t.tri.surfaceIndex!=requestedSurface)continue;
        if(NumericalEM::norm(t.centroid-p)<=out.patchRadiusM){selected[static_cast<std::size_t>(ti)]=1;++out.triangleCount;}
    }
    if(out.triangleCount==0){selected[static_cast<std::size_t>(nearestTri)]=1;out.triangleCount=1;}
    for(int bi=0;bi<static_cast<int>(bases.size());++bi)
    {
        const auto &b=bases[static_cast<std::size_t>(bi)];double coeff=0.0;
        for(const auto &sp:{b.plus,b.minus})
        {
            if(sp.triangle<0||!selected[static_cast<std::size_t>(sp.triangle)])continue;
            const auto &t=tri[static_cast<std::size_t>(sp.triangle)];coeff+=rwgDivergence(b,sp,t)*t.area;
        }
        if(std::abs(coeff)>1e-14)out.fluxTerms.push_back({bi,coeff});
    }
    return out;
}

SurfaceTerminalStencil buildSmoothSurfaceTerminalStencil(const Vec3 &p,int requestedSurface,double requestedRadiusM,
                                                          const std::vector<Vec3> &vertices,
                                                          const std::vector<BasisData> &bases,
                                                          const std::vector<TriData> &tri)
{
    // Differential lumped ports represent a finite scalar-potential terminal rather
    // than a galvanic wire/PEC junction.  A binary one-ring triangle patch changes
    // its physical size every time the RWG mesh changes, so its circuit incidence
    // vector is not a convergent port functional.  Use a smooth Gaussian potential
    // footprint and integrate div(f_n)*w(r) over the actual mesh instead.  With a
    // fixed physical sigma this is a standard Galerkin functional whose value tends
    // to the same continuum port as the mesh is refined.
    SurfaceTerminalStencil out=buildSurfaceTerminalStencil(p,requestedSurface,vertices,bases,tri);
    if(out.representative.basis<0 || !(out.localCellSizeM>0.0)) return out;
    const double sigma=requestedRadiusM>0.0?requestedRadiusM:std::max(out.patchRadiusM,1.25*out.localCellSizeM);
    out.patchRadiusM=std::max(sigma,1e-9);
    out.fluxTerms.clear();out.triangleCount=0;
    const Vec3 n=NumericalEM::normalized(out.surfaceNormal);
    std::vector<double> triangleWeight(tri.size(),0.0);
    const double cutoff=4.5*out.patchRadiusM;
    for(int ti=0;ti<static_cast<int>(tri.size());++ti)
    {
        const auto &t=tri[static_cast<std::size_t>(ti)];
        if(requestedSurface>=0 && t.tri.surfaceIndex!=requestedSurface) continue;
        const Vec3 dc=t.centroid-p;
        const double axial=NumericalEM::dot(dc,n);
        const double rho2=std::max(0.0,NumericalEM::dot(dc,dc)-axial*axial);
        const double cell=std::sqrt(std::max(2.0*t.area,1e-30));
        if(std::sqrt(rho2)>cutoff+2.0*cell) continue;
        double wi=0.0;
        for(const auto &qp:triangleQuadratureNear(t))
        {
            const Vec3 d=qp.p-p;const double z=NumericalEM::dot(d,n);
            const double r2=std::max(0.0,NumericalEM::dot(d,d)-z*z);
            wi+=std::exp(-0.5*r2/(out.patchRadiusM*out.patchRadiusM))*qp.weight;
        }
        if(wi>1e-18){triangleWeight[static_cast<std::size_t>(ti)]=wi;++out.triangleCount;}
    }
    for(int bi=0;bi<static_cast<int>(bases.size());++bi)
    {
        const auto &b=bases[static_cast<std::size_t>(bi)];double coeff=0.0;
        for(const auto &sp:{b.plus,b.minus})
        {
            if(sp.triangle<0) continue;
            const double wi=triangleWeight[static_cast<std::size_t>(sp.triangle)];
            if(!(wi>0.0)) continue;
            const auto &t=tri[static_cast<std::size_t>(sp.triangle)];
            coeff+=rwgDivergence(b,sp,t)*wi;
        }
        if(std::abs(coeff)>1e-14) out.fluxTerms.push_back({bi,coeff});
    }
    return out;
}

struct ConstraintBinding
{
    std::string name;
    Vec3 positionM{};
    std::vector<WireTouch> wireTouches;
    SurfaceMap surface; // representative basis retained for UI/backward-facing diagnostics
    std::vector<SurfaceFluxTerm> surfaceFluxTerms;
    int surfacePatchTriangleCount=0;
    double surfacePatchRadiusM=0.0;
    double surfaceLocalCellSizeM=0.0;
    double surfaceMappingDistanceM=std::numeric_limits<double>::infinity();
    Vec3 surfaceNormal{0.0,0.0,1.0};
    double surfaceSign=1.0;
    bool feedConstraint=false;
    C voltageV{0.0,0.0};
    double referenceOhm=50.0;
    PortReferenceModel referenceModel=PortReferenceModel::AntennaPlane;
    double coaxInnerRadiusM=0.0005;
    double coaxOuterRadiusM=0.0017;
    double coaxRelativePermittivity=2.1;
    double coaxLossTangent=0.0;
    double coaxLengthM=0.0;
};

C surfaceTerminalReturnCurrent(const ConstraintBinding &c,const std::vector<C> &surfaceCoefficients)
{
    // integrated divergence over the terminal patch is the net current flowing OUT of
    // that patch.  The current delivered back into the wire terminal is its negative.
    C outwardFlux{0,0};
    for(const auto &term:c.surfaceFluxTerms)
        if(term.basis>=0&&term.basis<static_cast<int>(surfaceCoefficients.size()))
            outwardFlux+=term.integratedDivergenceM*surfaceCoefficients[static_cast<std::size_t>(term.basis)];
    return -c.surfaceSign*outwardFlux;
}

// v5.21 terminal regularization --------------------------------------------------
std::vector<int> pecTerminalImageConstraintByWireEdge(const std::vector<NumericalEM::WireNetworkMeshSegment> &wire,
                                                       const std::vector<ConstraintBinding> &constraints,
                                                       double nodeTol)
{
    const int n=static_cast<int>(wire.size());
    std::vector<int> parent(static_cast<std::size_t>(n));
    for(int i=0;i<n;++i)parent[static_cast<std::size_t>(i)]=i;
    auto root=[&](int x){while(parent[static_cast<std::size_t>(x)]!=x){parent[static_cast<std::size_t>(x)]=parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(x)])];x=parent[static_cast<std::size_t>(x)];}return x;};
    auto unite=[&](int a,int b){a=root(a);b=root(b);if(a!=b)parent[static_cast<std::size_t>(b)]=a;};
    const double tol=std::max(nodeTol,1e-9);
    for(int i=0;i<n;++i)for(int j=i+1;j<n;++j)
    {
        const auto &a=wire[static_cast<std::size_t>(i)],&b=wire[static_cast<std::size_t>(j)];
        if(NumericalEM::norm(a.p0M-b.p0M)<=tol||NumericalEM::norm(a.p0M-b.p1M)<=tol||
           NumericalEM::norm(a.p1M-b.p0M)<=tol||NumericalEM::norm(a.p1M-b.p1M)<=tol)unite(i,j);
    }
    for(int i=0;i<n;++i)parent[static_cast<std::size_t>(i)]=root(i);
    std::map<int,std::vector<int>> byComponent;
    for(int ci=0;ci<static_cast<int>(constraints.size());++ci)
        for(const auto &wt:constraints[static_cast<std::size_t>(ci)].wireTouches)
            if(wt.segment>=0&&wt.segment<n)byComponent[parent[static_cast<std::size_t>(wt.segment)]].push_back(ci);
    std::vector<int> out(static_cast<std::size_t>(n),-1);
    for(auto &kv:byComponent)
    {
        auto &ids=kv.second;std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
        if(ids.size()!=1)continue;
        const int ci=ids.front();const auto &c=constraints[static_cast<std::size_t>(ci)];
        if(c.wireTouches.empty()||!(c.surfaceLocalCellSizeM>0.0))continue;
        const int terminalEdge=c.wireTouches.front().segment;if(terminalEdge<0||terminalEdge>=n)continue;
        const double normality=std::abs(NumericalEM::dot(wire[static_cast<std::size_t>(terminalEdge)].tangent,NumericalEM::normalized(c.surfaceNormal)));
        if(normality<0.35)continue;
        for(int ei=0;ei<n;++ei)if(parent[static_cast<std::size_t>(ei)]==kv.first)out[static_cast<std::size_t>(ei)]=ci;
    }
    return out;
}

NumericalEM::WireNetworkMeshSegment mirroredPecImageSegment(const NumericalEM::WireNetworkMeshSegment &src,
                                                             const ConstraintBinding &terminal)
{
    const Vec3 n=NumericalEM::normalized(terminal.surfaceNormal);
    auto image=src;
    image.p0M=src.p0M-n*(2.0*NumericalEM::dot(src.p0M-terminal.positionM,n));
    image.p1M=src.p1M-n*(2.0*NumericalEM::dot(src.p1M-terminal.positionM,n));
    image.lengthM=NumericalEM::norm(image.p1M-image.p0M);
    image.tangent=NumericalEM::normalized(image.p1M-image.p0M);
    image.centerM=(image.p0M+image.p1M)*0.5;
    return image;
}

double pecImageBlendWeight(const NumericalEM::WireNetworkMeshSegment &src,const ConstraintBinding &terminal)
{
    const Vec3 n=NumericalEM::normalized(terminal.surfaceNormal);
    const double h=std::abs(NumericalEM::dot(src.centerM-terminal.positionM,n));
    const double hc=std::max(terminal.surfaceLocalCellSizeM,1e-12);
    const double x=h/hc;return std::exp(-x*x);
}

ComplexVec3 triangleCurrentAtCentroid(int ti,const std::vector<BasisData>&bases,const std::vector<TriData>&tri,const std::vector<C>&coeff)
{
    ComplexVec3 out{};const auto&t=tri[static_cast<std::size_t>(ti)];
    for(std::size_t bi=0;bi<bases.size();++bi)for(const auto&s:{bases[bi].plus,bases[bi].minus})if(s.triangle==ti)out+=rwgValue(bases[bi],s,t,t.centroid)*coeff[bi];
    return out;
}

ComplexVec3 combinedFarFieldMoment(const Vec3&dirIn,const std::vector<NumericalEM::WireNetworkMeshSegment>&wire,const std::vector<C>&iw,const std::vector<TriData>&tri,const std::vector<ComplexVec3>&jTri,double k)
{
    const Vec3 dir=NumericalEM::normalized(dirIn);ComplexVec3 f{};
    for(std::size_t i=0;i<wire.size();++i)
    {
        const auto&e=wire[i];const Vec3 tr=e.tangent-dir*NumericalEM::dot(dir,e.tangent);const Vec3 mid=(e.p0M+e.p1M)*0.5,half=(e.p1M-e.p0M)*0.5;C phaseIntegral{0,0};
        for(int g=0;g<8;++g)for(int sgn:{-1,1}){const Vec3 r=mid+half*(sgn*GlX[static_cast<std::size_t>(g)]);phaseIntegral+=GlW[static_cast<std::size_t>(g)]*std::exp(J*k*NumericalEM::dot(dir,r));}
        phaseIntegral*=0.5*e.lengthM*iw[i];f+=tr*phaseIntegral;
    }
    for(std::size_t i=0;i<tri.size();++i){const C ph=std::exp(J*k*NumericalEM::dot(dir,tri[i].centroid));f+=jTri[i]*(tri[i].area*ph);}
    return transverse(f,dir);
}

constexpr std::array<double,4> RooftopGx{-0.8611363115940526,-0.3399810435848563,0.3399810435848563,0.8611363115940526};
constexpr std::array<double,4> RooftopGw{0.3478548451374538,0.6521451548625461,0.6521451548625461,0.3478548451374538};

struct HybridRooftopNode
{
    Vec3 p{};
    std::vector<int> edges;
    bool surfaceTerminal=false;
};

struct HybridRooftopSupport
{
    int edgeIndex=-1;
    double endpointCoefficient=0.0; // signed current along p0 -> p1 at the peak node
    bool peakAtP0=true;
};

struct HybridRooftopFunction
{
    int nodeIndex=-1;
    std::vector<HybridRooftopSupport> supports;
};

struct HybridRooftopBasisSet
{
    std::vector<HybridRooftopNode> nodes;
    std::vector<int> edgeP0Node;
    std::vector<int> edgeP1Node;
    std::vector<HybridRooftopFunction> functions;
    int openEndCount=0;
    int ordinaryContinuityCount=0;
    int branchKclCount=0;
    int terminalNodeCount=0;
};

double hybridRooftopShape(bool peakAtP0,double xi)
{
    const double u=0.5*(xi+1.0);
    return peakAtP0?(1.0-u):u;
}

bool hybridVecLexLess(const Vec3&a,const Vec3&b)
{
    if(a.x!=b.x)return a.x<b.x;
    if(a.y!=b.y)return a.y<b.y;
    return a.z<b.z;
}

int findHybridRooftopNode(const HybridRooftopBasisSet &basis,const Vec3 &p,double tol)
{
    int best=-1;double bd=std::numeric_limits<double>::infinity();
    for(int i=0;i<static_cast<int>(basis.nodes.size());++i)
    {
        const double d=NumericalEM::norm(basis.nodes[static_cast<std::size_t>(i)].p-p);
        if(d<=tol&&d<bd){bd=d;best=i;}
    }
    return best;
}

HybridRooftopBasisSet buildHybridRooftopBasis(const std::vector<NumericalEM::WireNetworkMeshSegment> &wire,
                                               const std::vector<ConstraintBinding> &surfaceConstraints,
                                               double nodeTol)
{
    HybridRooftopBasisSet out;
    const double tol=std::max(nodeTol,1e-10);
    auto nodeId=[&](const Vec3&p)
    {
        for(int i=0;i<static_cast<int>(out.nodes.size());++i)
            if(NumericalEM::norm(out.nodes[static_cast<std::size_t>(i)].p-p)<=tol)return i;
        const int id=static_cast<int>(out.nodes.size());out.nodes.push_back({p,{ },false});return id;
    };
    out.edgeP0Node.resize(wire.size(),-1);out.edgeP1Node.resize(wire.size(),-1);
    for(int ei=0;ei<static_cast<int>(wire.size());++ei)
    {
        const auto&e=wire[static_cast<std::size_t>(ei)];
        const int n0=nodeId(e.p0M),n1=nodeId(e.p1M);
        out.edgeP0Node[static_cast<std::size_t>(ei)]=n0;out.edgeP1Node[static_cast<std::size_t>(ei)]=n1;
        out.nodes[static_cast<std::size_t>(n0)].edges.push_back(ei);
        out.nodes[static_cast<std::size_t>(n1)].edges.push_back(ei);
    }
    const double terminalTol=std::max(8.0*tol,1e-7);
    for(const auto &c:surfaceConstraints)
    {
        int ni=-1;double bd=std::numeric_limits<double>::infinity();
        for(int i=0;i<static_cast<int>(out.nodes.size());++i)
        {
            const double d=NumericalEM::norm(out.nodes[static_cast<std::size_t>(i)].p-c.positionM);
            if(d<bd){bd=d;ni=i;}
        }
        if(ni>=0&&bd<=terminalTol)out.nodes[static_cast<std::size_t>(ni)].surfaceTerminal=true;
    }

    struct Arm{int edge=-1;double outwardSign=1.0;int otherNode=-1;bool peakAtP0=true;};
    for(int ni=0;ni<static_cast<int>(out.nodes.size());++ni)
    {
        const auto &node=out.nodes[static_cast<std::size_t>(ni)];
        const int degree=static_cast<int>(node.edges.size());
        if(degree<=0)continue;
        std::vector<Arm>arms;arms.reserve(node.edges.size());
        for(int ei:node.edges)
        {
            const bool atP0=out.edgeP0Node[static_cast<std::size_t>(ei)]==ni;
            const int other=atP0?out.edgeP1Node[static_cast<std::size_t>(ei)]:out.edgeP0Node[static_cast<std::size_t>(ei)];
            arms.push_back({ei,atP0?+1.0:-1.0,other,atP0});
        }
        std::sort(arms.begin(),arms.end(),[&](const Arm&a,const Arm&b){return hybridVecLexLess(out.nodes[static_cast<std::size_t>(a.otherNode)].p,out.nodes[static_cast<std::size_t>(b.otherNode)].p);});
        if(node.surfaceTerminal)
        {
            ++out.terminalNodeCount;
            // A PEC terminal adds one external current arm. Keep the ordinary KCL-safe wire
            // modes, then add one leakage/half-rooftop mode whose net wire current is +1 A per q.
            if(degree>=2)
            {
                const Arm ref=arms.front();
                for(std::size_t k=1;k<arms.size();++k)
                {
                    HybridRooftopFunction f;f.nodeIndex=ni;
                    f.supports.push_back({ref.edge,-ref.outwardSign,ref.peakAtP0});
                    f.supports.push_back({arms[k].edge,+arms[k].outwardSign,arms[k].peakAtP0});
                    out.functions.push_back(std::move(f));
                }
            }
            const Arm ref=arms.front();
            HybridRooftopFunction terminal;terminal.nodeIndex=ni;
            terminal.supports.push_back({ref.edge,ref.outwardSign,ref.peakAtP0});
            out.functions.push_back(std::move(terminal));
            continue;
        }
        if(degree==1){++out.openEndCount;continue;}
        if(degree==2)++out.ordinaryContinuityCount;else ++out.branchKclCount;
        const Arm ref=arms.front();
        for(std::size_t k=1;k<arms.size();++k)
        {
            HybridRooftopFunction f;f.nodeIndex=ni;
            f.supports.push_back({ref.edge,-ref.outwardSign,ref.peakAtP0});
            f.supports.push_back({arms[k].edge,+arms[k].outwardSign,arms[k].peakAtP0});
            out.functions.push_back(std::move(f));
        }
    }
    return out;
}

double rooftopNetOutwardCoefficientAtNode(const HybridRooftopFunction &f,int nodeIndex,
                                           const HybridRooftopBasisSet &basis)
{
    if(f.nodeIndex!=nodeIndex)return 0.0;
    double s=0.0;
    for(const auto &sp:f.supports)
    {
        const double outward=basis.edgeP0Node[static_cast<std::size_t>(sp.edgeIndex)]==nodeIndex?+1.0:-1.0;
        s+=outward*sp.endpointCoefficient;
    }
    return s;
}

C surfaceTestOfWireRooftopSupport(const Input &input,const BasisData&m,const std::vector<TriData>&tri,
                                  const NumericalEM::WireNetworkMeshSegment&src,const HybridRooftopSupport &support,
                                  double k0,double omega,double mutualFactor)
{
    C z{0,0};const Vec3 mid=(src.p0M+src.p1M)*0.5,half=(src.p1M-src.p0M)*0.5;
    for(const auto&sm:{m.plus,m.minus})
    {
        if(sm.triangle<0)continue;
        const TriData&tm=tri[static_cast<std::size_t>(sm.triangle)];const auto qm=triangleQuadrature(tm);
        const double req=std::sqrt(std::max(tm.area,1e-30)/NumericalEM::Pi);
        const double reg=std::max({1e-12,src.radiusM,mutualFactor*req});
        for(const auto&po:qm)
        {
            const Vec3 f=rwgValue(m,sm,tm,po.p);C eDotF{0,0};
            for(int g=0;g<4;++g)
            {
                const double xi=RooftopGx[static_cast<std::size_t>(g)],shape=hybridRooftopShape(support.peakAtP0,xi);
                const Vec3 rs=mid+half*xi;
                const C baseProjected=
                    f.x*projectedEfieKernel(input,po.p,{1,0,0},rs,src.tangent,reg,k0,omega)+
                    f.y*projectedEfieKernel(input,po.p,{0,1,0},rs,src.tangent,reg,k0,omega)+
                    f.z*projectedEfieKernel(input,po.p,{0,0,1},rs,src.tangent,reg,k0,omega);
                const C vectorCorrection=layeredWireSurfaceVectorPotentialCorrectionDot(input,po.p,f,rs,src.tangent,reg,omega,k0);
                C scalarGradientCorrection{0.0,0.0};
                const double dShapeDs=(support.peakAtP0?-1.0:+1.0)/std::max(src.lengthM,1e-12);
                if(layeredHedTangentialSourceSupported(input,po.p,rs,src.tangent))
                {
                    const ComplexVec3 grad=layeredWireSurfaceHedScalarTangentialGradient(input,po.p,rs,reg,omega,k0);
                    scalarGradientCorrection=(-1.0/(J*omega))*dShapeDs*dotComplex(f,grad);
                }
                else if(layeredVedNormalSourceSupported(input,po.p,rs,src.tangent))
                {
                    const ComplexVec3 grad=layeredWireSurfaceVedScalarTangentialGradient(input,po.p,rs,reg,omega,k0);
                    scalarGradientCorrection=(-1.0/(J*omega))*dShapeDs*dotComplex(f,grad);
                }
                eDotF+=RooftopGw[static_cast<std::size_t>(g)]*(shape*(baseProjected+vectorCorrection)+scalarGradientCorrection);
            }
            z+=support.endpointCoefficient*(0.5*src.lengthM)*eDotF*po.weight;
        }
    }
    return z;
}

C rooftopTestOfSurfaceBasisSupport(const Input &input,const HybridRooftopSupport &support,
                                   const NumericalEM::WireNetworkMeshSegment &obs,const BasisData &surfaceBasis,
                                   const std::vector<TriData>&tri,double k0,double omega,double mutualFactor)
{
    C z{0,0};const Vec3 mid=(obs.p0M+obs.p1M)*0.5,half=(obs.p1M-obs.p0M)*0.5;
    for(int g=0;g<4;++g)
    {
        const double xi=RooftopGx[static_cast<std::size_t>(g)],shape=hybridRooftopShape(support.peakAtP0,xi);
        const Vec3 ro=mid+half*xi;
        z+=RooftopGw[static_cast<std::size_t>(g)]*shape*dotComplex(obs.tangent,surfaceElectricFieldForBasis(input,surfaceBasis,tri,ro,k0,omega,mutualFactor,obs.radiusM));
    }
    return support.endpointCoefficient*(0.5*obs.lengthM)*z;
}

ComplexVec3 combinedFarFieldMomentRooftop(const Vec3&dirIn,const std::vector<NumericalEM::WireNetworkMeshSegment>&wire,
                                          const std::vector<C>&iP0,const std::vector<C>&iP1,
                                          const std::vector<TriData>&tri,const std::vector<ComplexVec3>&jTri,double k)
{
    const Vec3 dir=NumericalEM::normalized(dirIn);ComplexVec3 f{};
    for(std::size_t i=0;i<wire.size();++i)
    {
        const auto&e=wire[i];const Vec3 tr=e.tangent-dir*NumericalEM::dot(dir,e.tangent);const Vec3 mid=(e.p0M+e.p1M)*0.5,half=(e.p1M-e.p0M)*0.5;C phaseIntegral{0,0};
        for(int g=0;g<4;++g)
        {
            const double xi=RooftopGx[static_cast<std::size_t>(g)],u=0.5*(xi+1.0);
            const Vec3 r=mid+half*xi;const C ii=(1.0-u)*iP0[i]+u*iP1[i];
            phaseIntegral+=RooftopGw[static_cast<std::size_t>(g)]*ii*std::exp(J*k*NumericalEM::dot(dir,r));
        }
        phaseIntegral*=0.5*e.lengthM;f+=tr*phaseIntegral;
    }
    for(std::size_t i=0;i<tri.size();++i){const C ph=std::exp(J*k*NumericalEM::dot(dir,tri[i].centroid));f+=jTri[i]*(tri[i].area*ph);}
    return transverse(f,dir);
}

int layeredFarFieldEquivalentSourceSide(const Input &input,const DielectricRegion &slab)
{
    const Vec3 n=NumericalEM::normalized(slab.normalAxis);
    auto sideOf=[&](const Vec3 &p){const double z=NumericalEM::dot(p-slab.centerM,n);return z>=0.0?+1:-1;};
    // Prefer an explicitly driven positive surface terminal for differential patch ports;
    // it most closely represents the outward-radiating equivalent-current reference side.
    if(!input.differentialSurfaceFeeds.empty()) return sideOf(input.differentialSurfaceFeeds.front().positivePositionM);
    if(!input.surfaceReferencedFeeds.empty()) return sideOf(input.surfaceReferencedFeeds.front().positionM);
    if(!input.wire.feeds.empty()) return sideOf(input.wire.feeds.front().positionM);
    return +1;
}

const DielectricRegion *dominantLayeredFarFieldSlab(const Input &input)
{
    const DielectricRegion *best=nullptr;double bestArea=0.0;
    for(const auto &slab:input.dielectrics)
    {
        if(!(slab.widthM>0.0&&slab.heightM>0.0&&slab.thicknessM>0.0&&slab.relativePermittivity>=1.0))continue;
        const double area=slab.widthM*slab.heightM;
        if(!best||area>bestArea){best=&slab;bestArea=area;}
    }
    return best;
}

ComplexVec3 layeredPropagatingPlaneWaveMoment(const Input &input,const DielectricRegion &slab,int sourceSide,
                                               const Vec3 &dirIn,const ComplexVec3 &freeMoment,double k0)
{
    (void)input;
    const Vec3 dir=NumericalEM::normalized(dirIn),n=NumericalEM::normalized(slab.normalAxis);
    const double mu=std::clamp(NumericalEM::dot(dir,n),-1.0,1.0);
    const int observationSide=mu>=0.0?+1:-1;
    const double kRho=k0*std::sqrt(std::max(0.0,1.0-mu*mu));
    const auto sp=sommerfeldSpectrum(slab,k0,kRho);
    Vec3 sHat=cross(n,dir);
    if(NumericalEM::norm(sHat)<1e-10)sHat=NumericalEM::normalized(slab.uAxis);
    else sHat=NumericalEM::normalized(sHat);
    Vec3 pHat=cross(sHat,dir);
    if(NumericalEM::norm(pHat)<1e-10)pHat=NumericalEM::normalized(slab.vAxis);
    else pHat=NumericalEM::normalized(pHat);
    const C fTe=dotComplex(sHat,freeMoment),fTm=dotComplex(pHat,freeMoment);
    C aTe{1.0,0.0},aTm{1.0,0.0};
    if(observationSide==sourceSide)
    {
        // Equivalent-current same-side field: direct + finite-slab reflected wave.
        // `sp.reflection` follows the mixed-potential TM sign convention already used
        // by the dyadic assembly; using that convention keeps TE/TM identical at normal
        // incidence in the local outgoing polarization basis.
        aTe=C{1.0,0.0}+sp.teReflection;
        aTm=C{1.0,0.0}+sp.reflection;
    }
    else
    {
        // Both exterior media are air, so no additional wave-impedance power factor is
        // needed after transmission through the complete symmetric finite slab.
        aTe=sp.teTransmission;
        aTm=sp.transmission;
    }
    ComplexVec3 out=sHat*(fTe*aTe);out+=pHat*(fTm*aTm);return transverse(out,dir);
}

template<class FarFieldMoment>
void populateLayeredPropagatingFarFieldAudit(Result &out,const Input &input,FarFieldMoment &&moment,
                                              double requestedCutStepDeg,double requestedIntegrationStepDeg,double k0)
{
    if(!layeredPropagatingFarFieldMode(input.dielectricKernelModel)||!input.computeFarField||!input.useEffectiveDielectricRegions)return;
    const DielectricRegion *slab=dominantLayeredFarFieldSlab(input);if(!slab)return;
    const int sourceSide=layeredFarFieldEquivalentSourceSide(input,*slab);out.layeredSommerfeldFarFieldSourceSide=sourceSide;
    auto layeredMoment=[&](const Vec3 &d){const ComplexVec3 freeF=moment(d);return layeredPropagatingPlaneWaveMoment(input,*slab,sourceSide,d,freeF,k0);};
    auto layered2=[&](const Vec3 &d){return normComplex2(layeredMoment(d));};

    const double cut=std::clamp(requestedCutStepDeg,0.5,30.0);
    const int azIntervals=std::max(8,static_cast<int>(std::ceil(360.0/cut)));
    const int elIntervals=std::max(4,static_cast<int>(std::ceil(180.0/cut)));
    std::vector<double> azRaw,elRaw;azRaw.reserve(static_cast<std::size_t>(azIntervals+1));elRaw.reserve(static_cast<std::size_t>(elIntervals+1));
    for(int i=0;i<=azIntervals;++i){const double ph=2.0*NumericalEM::Pi*double(i)/double(azIntervals);azRaw.push_back(std::sqrt(std::max(0.0,layered2({std::cos(ph),std::sin(ph),0.0}))));}
    for(int i=0;i<=elIntervals;++i){const double th=NumericalEM::Pi*double(i)/double(elIntervals);elRaw.push_back(std::sqrt(std::max(0.0,layered2({std::sin(th),0.0,std::cos(th)}))));}

    const double step=std::clamp(requestedIntegrationStepDeg,1.0,30.0),dth=step*NumericalEM::Pi/180.0,dph=dth;
    double integ=0.0,upper=0.0,lower=0.0,maxF2=0.0;std::vector<double> sphereRaw;
    for(double th=.5*dth;th<NumericalEM::Pi;th+=dth)for(double ph=.5*dph;ph<2.0*NumericalEM::Pi;ph+=dph)
    {
        const Vec3 d{std::sin(th)*std::cos(ph),std::sin(th)*std::sin(ph),std::cos(th)};const double f2=layered2(d);const double w=std::sin(th)*dth*dph;
        sphereRaw.push_back(std::sqrt(std::max(0.0,f2)));integ+=f2*w;
        if(NumericalEM::dot(d,NumericalEM::normalized(slab->normalAxis))>=0.0)upper+=f2*w;else lower+=f2*w;
        if(f2>maxF2){maxF2=f2;out.layeredMaxRadiationThetaDeg=th*180.0/NumericalEM::Pi;out.layeredMaxRadiationPhiDeg=ph*180.0/NumericalEM::Pi;}
    }
    const double globalMax=std::sqrt(std::max(0.0,maxF2));
    out.layeredAzimuthNormalizedFarField.reserve(azRaw.size());for(double v:azRaw)out.layeredAzimuthNormalizedFarField.push_back(globalMax>0.0?v/globalMax:0.0);
    out.layeredElevationNormalizedFarField.reserve(elRaw.size());for(double v:elRaw)out.layeredElevationNormalizedFarField.push_back(globalMax>0.0?v/globalMax:0.0);
    out.layeredFarFieldNormalized.reserve(sphereRaw.size());for(double v:sphereRaw)out.layeredFarFieldNormalized.push_back(globalMax>0.0?v/globalMax:0.0);
    const double eta0=std::sqrt(NumericalEM::Mu0/NumericalEM::Epsilon0),scale=eta0*k0*k0/(32.0*NumericalEM::Pi*NumericalEM::Pi);
    out.layeredRadiatedPowerUpperW=scale*upper;out.layeredRadiatedPowerLowerW=scale*lower;out.layeredRadiatedPowerTotalW=scale*integ;
    if(integ>1e-30&&maxF2>0.0){out.layeredDirectivityLinear=4.0*NumericalEM::Pi*maxF2/integ;out.layeredDirectivityDbi=10.0*std::log10(std::max(1e-30,out.layeredDirectivityLinear));}
    out.layeredPowerClosureResidualW=out.acceptedPowerW-out.layeredRadiatedPowerTotalW-out.conductorLossW;
    out.layeredPowerClosureRelative=std::abs(out.acceptedPowerW)>1e-18?out.layeredPowerClosureResidualW/std::abs(out.acceptedPowerW):0.0;
    const double tol=std::max(1e-12,0.05*std::max(out.acceptedPowerW,0.0));
    out.layeredSommerfeldPropagatingFarFieldPowerConsistent=out.layeredRadiatedPowerTotalW>=-tol&&out.conductorLossW>=-tol&&
        (out.acceptedPowerW<=1e-18||out.layeredRadiatedPowerTotalW+out.conductorLossW<=out.acceptedPowerW+tol);
    out.layeredSommerfeldPropagatingFarFieldUsed=true;
    populateLayeredSurfaceWavePoleAudit(out,input,*slab,k0);
    populateLayeredGroundedPecModeAudit(out,input,*slab,k0);
}

template<class FarFieldSquared>
void populateFarFieldResult(Result &out,FarFieldSquared &&ff2,double requestedCutStepDeg,double requestedIntegrationStepDeg,double k)
{
    // v5.28: all public radiation cuts use the same spherical convention as the 3D grid:
    // theta = 0 deg toward +Z, theta = 90 deg in XY, phi = 0 deg toward +X.
    // Previous hybrid paths stored the vertical cut as elevation -90...+90 deg while the
    // UI/validation layer interpreted it as theta, rotating that cut by 90 degrees.
    const double cut=std::clamp(requestedCutStepDeg,0.5,30.0);
    const int azIntervals=std::max(8,static_cast<int>(std::ceil(360.0/cut)));
    const int elIntervals=std::max(4,static_cast<int>(std::ceil(180.0/cut)));
    std::vector<double> azRaw,elRaw;
    azRaw.reserve(static_cast<std::size_t>(azIntervals+1));
    elRaw.reserve(static_cast<std::size_t>(elIntervals+1));
    for(int i=0;i<=azIntervals;++i)
    {
        const double phiDeg=360.0*double(i)/double(azIntervals);
        const double phi=phiDeg*NumericalEM::Pi/180.0;
        const double v=std::sqrt(std::max(0.0,ff2({std::cos(phi),std::sin(phi),0.0})));
        out.azimuthDeg.push_back(phiDeg);azRaw.push_back(v);
    }
    for(int i=0;i<=elIntervals;++i)
    {
        const double thetaDeg=180.0*double(i)/double(elIntervals);
        const double theta=thetaDeg*NumericalEM::Pi/180.0;
        const double v=std::sqrt(std::max(0.0,ff2({std::sin(theta),0.0,std::cos(theta)})));
        out.elevationDeg.push_back(thetaDeg);elRaw.push_back(v);
    }

    const double step=std::clamp(requestedIntegrationStepDeg,1.0,30.0);
    const double dth=step*NumericalEM::Pi/180.0,dph=step*NumericalEM::Pi/180.0;
    double integ=0.0,maxF2=0.0;
    std::vector<double> sphereRaw;
    for(double th=.5*dth;th<NumericalEM::Pi;th+=dth)
    {
        out.farFieldThetaDeg.push_back(th*180.0/NumericalEM::Pi);
        const bool first=out.farFieldPhiDeg.empty();
        for(double ph=.5*dph;ph<2.0*NumericalEM::Pi;ph+=dph)
        {
            if(first)out.farFieldPhiDeg.push_back(ph*180.0/NumericalEM::Pi);
            const double f2=ff2({std::sin(th)*std::cos(ph),std::sin(th)*std::sin(ph),std::cos(th)});
            sphereRaw.push_back(std::sqrt(std::max(0.0,f2)));
            integ+=f2*std::sin(th)*dth*dph;
            if(f2>maxF2){maxF2=f2;out.maxRadiationThetaDeg=th*180.0/NumericalEM::Pi;out.maxRadiationPhiDeg=ph*180.0/NumericalEM::Pi;}
        }
    }

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

    // One global normalization is shared by 2D cuts and the 3D pattern.  This prevents
    // every cut from falsely reaching 0 dB when the actual maximum lies outside its plane.
    const double globalMax=std::sqrt(std::max(0.0,maxF2));
    for(double v:azRaw)out.azimuthNormalizedFarField.push_back(globalMax>0.0?v/globalMax:0.0);
    for(double v:elRaw)out.elevationNormalizedFarField.push_back(globalMax>0.0?v/globalMax:0.0);
    out.farFieldNormalized.reserve(sphereRaw.size());
    for(double v:sphereRaw)out.farFieldNormalized.push_back(globalMax>0.0?v/globalMax:0.0);
    if(integ>1e-30&&maxF2>0.0)
    {
        out.directivityLinear=4.0*NumericalEM::Pi*maxF2/integ;
        out.directivityDbi=10.0*std::log10(std::max(1e-30,out.directivityLinear));
        const double eta0=std::sqrt(NumericalEM::Mu0/NumericalEM::Epsilon0);
        out.radiatedPowerW=eta0*k*k*integ/(32.0*NumericalEM::Pi*NumericalEM::Pi);
    }
}

C endpointCurrentAtTouch(const WireTouch &wt,const std::vector<C>&iP0,const std::vector<C>&iP1)
{
    if(wt.segment<0||wt.segment>=static_cast<int>(iP0.size()))return {0,0};
    return wt.sign>0.0?iP0[static_cast<std::size_t>(wt.segment)]:iP1[static_cast<std::size_t>(wt.segment)];
}

Result solveRooftopHybridCore(const Input &input,const NumericalEM::WireNetworkMomResult &pulseMeshSeed,
                              const std::vector<NumericalEM::WireNetworkMeshSegment>&wire,
                              const std::vector<Vec3>&vertices,const std::vector<TriData>&tri,const std::vector<BasisData>&bases,
                              int boundary,int halfCount,const std::vector<FeedBinding>&feedBindings,
                              const std::vector<ConstraintBinding>&constraints)
{
    Result out;
    const int nw=static_cast<int>(wire.size()),ns=static_cast<int>(bases.size()),nc=static_cast<int>(constraints.size());
    const double f=input.wire.frequencyHz,omega=2.0*NumericalEM::Pi*f,lambda=NumericalEM::C0/f,k=2.0*NumericalEM::Pi/lambda;
    const double nodeTol=std::max(input.wire.nodeMergeToleranceM,1e-9);
    const auto rooftop=buildHybridRooftopBasis(wire,constraints,nodeTol);
    const auto pecImageByEdge=pecTerminalImageConstraintByWireEdge(wire,constraints,nodeTol);
    std::vector<int> regularizedConstraintIds;for(int ci:pecImageByEdge)if(ci>=0)regularizedConstraintIds.push_back(ci);
    std::sort(regularizedConstraintIds.begin(),regularizedConstraintIds.end());regularizedConstraintIds.erase(std::unique(regularizedConstraintIds.begin(),regularizedConstraintIds.end()),regularizedConstraintIds.end());
    const int nr=static_cast<int>(rooftop.functions.size());
    if(nr<=0){out.error="Hybrid rooftop basis produced no wire current degrees of freedom";return out;}
    if(nr>std::max(1,input.wire.maxUnknowns)){out.error="Hybrid rooftop wire basis exceeds the wire maxUnknowns limit";return out;}
    const int np=nr+ns;
    if(np>std::max(2,input.maxTotalUnknowns)){out.error="Hybrid rooftop/RWG physical system exceeds maxTotalUnknowns; coarsen the meshes or increase the limit";return out;}

    const double mutualFactor=std::clamp(input.mutualRegularizationFactor,0.005,0.5);
    const double selfFactor=std::clamp(input.surfaceSelfRegularizationFactor,0.02,1.0);
    std::vector<std::vector<C>>Aphys(static_cast<std::size_t>(np),std::vector<C>(static_cast<std::size_t>(np),{0,0}));
    std::vector<C>rhsPhys(static_cast<std::size_t>(np),{0,0});

    // Weak delta-gap excitation for ordinary wire feeds. Only rooftop functions whose peak node
    // is the feed node contribute; the deterministic virtual path makes the result invariant to
    // CAD span direction, matching the wire-only rooftop solver.
    const double feedTol=std::max(input.wire.nodeMergeToleranceM*8.0,1e-7);
    for(std::size_t fi=0;fi<input.wire.feeds.size();++fi)
    {
        const int node=findHybridRooftopNode(rooftop,input.wire.feeds[fi].positionM,feedTol);
        if(node<0){out.error="Hybrid rooftop feed could not map to a mesh node";return out;}
        const auto &bind=feedBindings[fi];
        for(int a=0;a<nr;++a)
        {
            const auto &test=rooftop.functions[static_cast<std::size_t>(a)];if(test.nodeIndex!=node)continue;
            double c0=0.0,c1=0.0;
            for(const auto &sp:test.supports){if(sp.edgeIndex==bind.e0)c0+=sp.endpointCoefficient;if(sp.edgeIndex==bind.e1)c1+=sp.endpointCoefficient;}
            rhsPhys[static_cast<std::size_t>(a)]+=input.wire.feeds[fi].voltageV*0.5*(bind.s0*c0+bind.s1*c1);
        }
    }
    // A surface-referenced feed excites the terminal leakage/half-rooftop mode. Internal wire
    // modes have zero net outward current at that node, so they receive no direct source term.
    for(const auto &c:constraints)if(c.feedConstraint)
    {
        const int node=findHybridRooftopNode(rooftop,c.positionM,feedTol);
        if(node<0){out.error="Hybrid rooftop PEC-referenced feed could not map to a terminal wire node";return out;}
        for(int a=0;a<nr;++a)
            rhsPhys[static_cast<std::size_t>(a)]+=c.voltageV*rooftopNetOutwardCoefficientAtNode(rooftop.functions[static_cast<std::size_t>(a)],node,rooftop);
    }

    // Galerkin rooftop-wire block: both observation and source currents are integrated with the
    // same piecewise-linear shape functions. Self/near pairs use radius-aware composite
    // quadrature on BOTH spans; the hybrid dielectric Green-function model is kept.
    for(int a=0;a<nr;++a)
    {
        const auto &test=rooftop.functions[static_cast<std::size_t>(a)];
        for(int b=0;b<nr;++b)
        {
            const auto &trial=rooftop.functions[static_cast<std::size_t>(b)];C zab{0,0};
            for(const auto &so:test.supports)
            {
                const auto &obs=wire[static_cast<std::size_t>(so.edgeIndex)];
                for(const auto &ss:trial.supports)
                {
                    const auto &src=wire[static_cast<std::size_t>(ss.edgeIndex)];
                    const double aEff=0.5*(std::max(obs.radiusM,1e-12)+std::max(src.radiusM,1e-12));
                    const C pair=integrateProjectedWireRooftopPair(input,obs,so.peakAtP0,src,ss.peakAtP0,aEff,k,omega);
                    zab+=so.endpointCoefficient*ss.endpointCoefficient*pair;
                }
            }
            for(const auto &so:test.supports)
            {
                const auto &obs=wire[static_cast<std::size_t>(so.edgeIndex)];
                for(const auto &ss:trial.supports)
                {
                    const int ci=pecImageByEdge[static_cast<std::size_t>(ss.edgeIndex)];if(ci<0)continue;
                    const auto &terminal=constraints[static_cast<std::size_t>(ci)];const auto &src=wire[static_cast<std::size_t>(ss.edgeIndex)];
                    const double w=pecImageBlendWeight(src,terminal);if(w<1e-8)continue;
                    const auto image=mirroredPecImageSegment(src,terminal);
                    const double aEff=0.5*(std::max(obs.radiusM,1e-12)+std::max(src.radiusM,1e-12));
                    const C pair=integrateProjectedWireRooftopPair(input,obs,so.peakAtP0,image,ss.peakAtP0,aEff,k,omega);
                    zab-=w*so.endpointCoefficient*ss.endpointCoefficient*pair;
                }
            }
            Aphys[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)]=zab;
        }
    }

    // RWG Galerkin surface block and the two genuinely integrated rooftop<->RWG mutual blocks.
    for(int m=0;m<ns;++m)
    {
        for(int q=0;q<ns;++q)Aphys[static_cast<std::size_t>(nr+m)][static_cast<std::size_t>(nr+q)]=surfaceMatrixEntry(input,bases[static_cast<std::size_t>(m)],bases[static_cast<std::size_t>(q)],tri,k,omega,selfFactor);
        for(int b=0;b<nr;++b)
        {
            C v{0,0};for(const auto &sp:rooftop.functions[static_cast<std::size_t>(b)].supports)v+=surfaceTestOfWireRooftopSupport(input,bases[static_cast<std::size_t>(m)],tri,wire[static_cast<std::size_t>(sp.edgeIndex)],sp,k,omega,mutualFactor);
            Aphys[static_cast<std::size_t>(nr+m)][static_cast<std::size_t>(b)]=v;
        }
    }
    for(int a=0;a<nr;++a)
        for(int q=0;q<ns;++q)
        {
            C v{0,0};for(const auto &sp:rooftop.functions[static_cast<std::size_t>(a)].supports)v+=rooftopTestOfSurfaceBasisSupport(input,sp,wire[static_cast<std::size_t>(sp.edgeIndex)],bases[static_cast<std::size_t>(q)],tri,k,omega,mutualFactor);
            Aphys[static_cast<std::size_t>(a)][static_cast<std::size_t>(nr+q)]=v;
        }

    out.surfaceReciprocityPreSymmetryRelative=matrixBlockReciprocityRelative(Aphys,nr,ns);
    if(input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldCrossFace ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer ||
        (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel))))
        symmetrizeReciprocalMatrixBlock(Aphys,nr,ns);
    out.surfaceReciprocityRelative=matrixBlockReciprocityRelative(Aphys,nr,ns);

    // Reciprocity metric before row normalization. With identical media and exact integration the
    // two Galerkin mutual blocks are transposes. This diagnostic exposes quadrature/regularization
    // sensitivity without forcing an artificial symmetrization.
    double recipNum=0.0,recipDen=0.0;
    for(int a=0;a<nr;++a)for(int q=0;q<ns;++q)
    {
        const C zws=Aphys[static_cast<std::size_t>(a)][static_cast<std::size_t>(nr+q)],zsw=Aphys[static_cast<std::size_t>(nr+q)][static_cast<std::size_t>(a)];
        recipNum+=std::norm(zws-zsw);recipDen+=0.5*(std::norm(zws)+std::norm(zsw));
    }
    out.mutualReciprocityPreSymmetryRelative=std::sqrt(recipNum/std::max(recipDen,1e-30));
    for(int a=0;a<nr;++a)for(int q=0;q<ns;++q)
    {
        const C avg=0.5*(Aphys[static_cast<std::size_t>(a)][static_cast<std::size_t>(nr+q)]+Aphys[static_cast<std::size_t>(nr+q)][static_cast<std::size_t>(a)]);
        Aphys[static_cast<std::size_t>(a)][static_cast<std::size_t>(nr+q)]=avg;Aphys[static_cast<std::size_t>(nr+q)][static_cast<std::size_t>(a)]=avg;
    }
    double recipPostNum=0.0,recipPostDen=0.0;for(int a=0;a<nr;++a)for(int q=0;q<ns;++q){const C zws=Aphys[static_cast<std::size_t>(a)][static_cast<std::size_t>(nr+q)],zsw=Aphys[static_cast<std::size_t>(nr+q)][static_cast<std::size_t>(a)];recipPostNum+=std::norm(zws-zsw);recipPostDen+=0.5*(std::norm(zws)+std::norm(zsw));}
    out.mutualReciprocityRelative=std::sqrt(recipPostNum/std::max(recipPostDen,1e-30));

    // v5.21: enforce wire<->PEC terminal continuity by eliminating the mixed
    // line/surface current constraint inside the Galerkin basis.  A symmetric
    // Lagrange row is dimensionally awkward here because the wire test integral
    // is in volts while an RWG surface test integral is in volt-metres.  Nullspace
    // reduction instead builds admissible combined rooftop/RWG current functions
    // and keeps the physical variational equations consistently scaled.
    std::vector<std::vector<double>> terminalMatrix(static_cast<std::size_t>(nc),
                                                     std::vector<double>(static_cast<std::size_t>(np),0.0));
    for(int ci=0;ci<nc;++ci)
    {
        const auto &c=constraints[static_cast<std::size_t>(ci)];const int node=findHybridRooftopNode(rooftop,c.positionM,feedTol);
        if(node<0){out.error="Hybrid rooftop wire/PEC constraint could not map to a wire node";return out;}
        for(int a=0;a<nr;++a)
            terminalMatrix[static_cast<std::size_t>(ci)][static_cast<std::size_t>(a)]=
                rooftopNetOutwardCoefficientAtNode(rooftop.functions[static_cast<std::size_t>(a)],node,rooftop);
        for(const auto &term:c.surfaceFluxTerms)
            terminalMatrix[static_cast<std::size_t>(ci)][static_cast<std::size_t>(nr+term.basis)]+=
                c.surfaceSign*term.integratedDivergenceM;
    }
    NullspaceBasis terminalBasis;
    if(!buildConstraintNullspace(terminalMatrix,np,terminalBasis)){out.error="Failed to build the hybrid rooftop/RWG terminal-current basis";return out;}
    const int ncIndependent=terminalBasis.rank;
    const int nt=np-ncIndependent;
    if(nt<=0||nt>std::max(2,input.maxTotalUnknowns)){out.error="Hybrid rooftop/RWG reduced terminal system has an invalid size";return out;}
    std::vector<std::vector<C>>A(static_cast<std::size_t>(nt),std::vector<C>(static_cast<std::size_t>(nt),{0,0}));
    std::vector<C>rhs(static_cast<std::size_t>(nt),{0,0});
    for(int a=0;a<nt;++a)
    {
        for(int i=0;i<np;++i)rhs[static_cast<std::size_t>(a)]+=terminalBasis.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)]*rhsPhys[static_cast<std::size_t>(i)];
        for(int b=0;b<nt;++b)
            for(int i=0;i<np;++i)for(int j=0;j<np;++j)
                A[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)]+=
                    terminalBasis.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)]*
                    Aphys[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)]*
                    terminalBasis.t[static_cast<std::size_t>(j)][static_cast<std::size_t>(b)];
    }

    double mutual2=0.0;std::size_t mutualN=0;
    // Keep the diagnostic on the physical blocks before terminal-basis mixing.
    for(int i=0;i<np;++i)
    {
        double scale=0.0;for(int q=0;q<np;++q)scale=std::max(scale,std::abs(Aphys[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]));scale=std::max(scale,1e-30);
        for(int q=0;q<np;++q)if((i<nr)!=(q<nr)){mutual2+=std::norm(Aphys[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]/scale);++mutualN;}
    }
    out.normalizedMutualCouplingRms=mutualN?std::sqrt(mutual2/double(mutualN)):0.0;
    for(int i=0;i<nt;++i)
    {
        double scale=0.0;for(int q=0;q<nt;++q)scale=std::max(scale,std::abs(A[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]));scale=std::max(scale,1e-30);
        for(int q=0;q<nt;++q)A[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]/=scale;
        rhs[static_cast<std::size_t>(i)]/=scale;
    }
    SolveStats stats;std::vector<C>qReduced;if(!solveDense(A,rhs,qReduced,stats)){out.error="Hybrid rooftop/RWG reduced Galerkin matrix is singular or ill-conditioned";return out;}
    double r2=0.0,b2=0.0;for(int i=0;i<nt;++i){C ax{0,0};for(int q=0;q<nt;++q)ax+=A[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]*qReduced[static_cast<std::size_t>(q)];r2+=std::norm(ax-rhs[static_cast<std::size_t>(i)]);b2+=std::norm(rhs[static_cast<std::size_t>(i)]);}
    std::vector<C>x(static_cast<std::size_t>(np),{0,0});
    for(int i=0;i<np;++i)for(int a=0;a<nt;++a)x[static_cast<std::size_t>(i)]+=terminalBasis.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)]*qReduced[static_cast<std::size_t>(a)];

    std::vector<C>iP0(static_cast<std::size_t>(nw),{0,0}),iP1(static_cast<std::size_t>(nw),{0,0}),iw(static_cast<std::size_t>(nw),{0,0});
    for(int a=0;a<nr;++a)
    {
        const C qa=x[static_cast<std::size_t>(a)];
        for(const auto &sp:rooftop.functions[static_cast<std::size_t>(a)].supports)
        {
            if(sp.peakAtP0)iP0[static_cast<std::size_t>(sp.edgeIndex)]+=sp.endpointCoefficient*qa;
            else iP1[static_cast<std::size_t>(sp.edgeIndex)]+=sp.endpointCoefficient*qa;
        }
    }
    C netWireCharge{0,0};
    out.wireSegments=wire;
    for(int i=0;i<nw;++i)
    {
        iw[static_cast<std::size_t>(i)]=0.5*(iP0[static_cast<std::size_t>(i)]+iP1[static_cast<std::size_t>(i)]);
        auto &dst=out.wireSegments[static_cast<std::size_t>(i)];dst.currentAtP0A=iP0[static_cast<std::size_t>(i)];dst.currentAtP1A=iP1[static_cast<std::size_t>(i)];dst.currentA=iw[static_cast<std::size_t>(i)];
        if(dst.lengthM>1e-18)dst.lineChargeDensityCpm=-(iP1[static_cast<std::size_t>(i)]-iP0[static_cast<std::size_t>(i)])/(J*omega*dst.lengthM);
        netWireCharge+=dst.lineChargeDensityCpm*dst.lengthM;out.maxWireLineChargeCpm=std::max(out.maxWireLineChargeCpm,std::abs(dst.lineChargeDensityCpm));
        out.peakWireCurrentA=std::max({out.peakWireCurrentA,std::abs(iP0[static_cast<std::size_t>(i)]),std::abs(iP1[static_cast<std::size_t>(i)])});
    }
    out.netWireChargeMagnitudeC=std::abs(netWireCharge);
    for(int ni=0;ni<static_cast<int>(rooftop.nodes.size());++ni)
    {
        const auto &node=rooftop.nodes[static_cast<std::size_t>(ni)];if(node.surfaceTerminal||node.edges.size()!=1)continue;
        const int ei=node.edges.front();const C ie=rooftop.edgeP0Node[static_cast<std::size_t>(ei)]==ni?iP0[static_cast<std::size_t>(ei)]:iP1[static_cast<std::size_t>(ei)];out.maxWireOpenEndCurrentA=std::max(out.maxWireOpenEndCurrentA,std::abs(ie));
    }

    std::vector<C>is(x.begin()+nr,x.begin()+nr+ns);
    out.surfaceBases.reserve(bases.size());for(std::size_t i=0;i<bases.size();++i){const auto&b=bases[i];const Vec3 a=vertices[static_cast<std::size_t>(b.edgeV0)],bb=vertices[static_cast<std::size_t>(b.edgeV1)];out.surfaceBases.push_back({b.plus.triangle,b.minus.triangle,a,bb,(a+bb)*0.5,b.edgeLength,is[i],is[i]*b.edgeLength});}
    std::vector<ComplexVec3>jTri;out.triangleCurrents.reserve(tri.size());
    for(int ti=0;ti<static_cast<int>(tri.size());++ti){const auto jj=triangleCurrentAtCentroid(ti,bases,tri,is);jTri.push_back(jj);const double mag=std::sqrt(normComplex2(jj));out.peakSurfaceCurrentApm=std::max(out.peakSurfaceCurrentApm,mag);out.triangleCurrents.push_back({tri[static_cast<std::size_t>(ti)].centroid,tri[static_cast<std::size_t>(ti)].area,jj.x,jj.y,jj.z,mag});}
    out.surfaceImpedanceOhmPerSquare=finiteSheetImpedance(input,omega);
    if(input.useFiniteSurfaceConductivity&&out.surfaceImpedanceOhmPerSquare.real()>0.0)
        for(std::size_t ti=0;ti<tri.size();++ti)out.conductorLossW+=0.5*out.surfaceImpedanceOhmPerSquare.real()*normComplex2(jTri[ti])*tri[ti].area;

    NumericalEM::WireNetworkMomResult rooftopUncoupled;
    if(!input.wire.feeds.empty())
    {
        auto ui=input.wire;ui.computeFarField=false;ui.currentBasisTreatment=NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge;rooftopUncoupled=NumericalEM::solveWireNetworkMom(ui);
    }
    out.feeds.reserve(input.wire.feeds.size()+constraints.size());
    for(std::size_t fi=0;fi<input.wire.feeds.size();++fi)
    {
        const auto &feed=input.wire.feeds[fi];const auto &bind=feedBindings[fi];
        auto endpointAt=[&](int ei){const auto &s=wire[static_cast<std::size_t>(ei)];return NumericalEM::norm(s.p0M-feed.positionM)<=feedTol?iP0[static_cast<std::size_t>(ei)]:iP1[static_cast<std::size_t>(ei)];};
        const C ifeed=0.5*(bind.s0*endpointAt(bind.e0)+bind.s1*endpointAt(bind.e1));FeedResult fr;fr.name=feed.name;fr.positionM=feed.positionM;fr.voltageV=feed.voltageV;fr.currentA=ifeed;fr.referenceOhm=std::max(feed.referenceOhm,1e-9);if(std::abs(ifeed)>1e-18)fr.inputImpedanceOhm=feed.voltageV/ifeed;if(rooftopUncoupled.valid&&fi<rooftopUncoupled.feeds.size())fr.uncoupledInputImpedanceOhm=rooftopUncoupled.feeds[fi].activeImpedanceOhm;const C z0{fr.referenceOhm,0.0},den=fr.inputImpedanceOhm+z0;if(std::abs(den)>1e-18)fr.reflectionCoefficient=(fr.inputImpedanceOhm-z0)/den;const double gm=std::abs(fr.reflectionCoefficient);fr.returnLossDb=gm>1e-15?-20.0*std::log10(gm):300.0;fr.vswr=gm<1.0?(1+gm)/std::max(1e-15,1-gm):std::numeric_limits<double>::infinity();out.acceptedPowerW+=0.5*std::real(feed.voltageV*std::conj(ifeed));out.feeds.push_back(fr);
    }
    for(const auto &c:constraints)
    {
        C wireSum{0,0};for(const auto &wt:c.wireTouches)wireSum+=wt.sign*endpointCurrentAtTouch(wt,iP0,iP1);
        const C surfaceI=surfaceTerminalReturnCurrent(c,is);
        JunctionResult jr;jr.name=c.name;jr.requestedPositionM=c.positionM;jr.mappedSurfaceEdgeCenterM=c.surface.edgeCenter;jr.mappedSurfaceIndex=c.surface.surfaceIndex;jr.mappedSurfaceBasisIndex=c.surface.basis;jr.touchingWireSegmentCount=static_cast<int>(c.wireTouches.size());jr.surfaceMappingDistanceM=c.surfaceMappingDistanceM;for(const auto &wt:c.wireTouches)jr.wireMappingDistanceM=std::max(jr.wireMappingDistanceM,wt.distanceM);jr.wireCurrentSumA=wireSum;jr.surfaceEdgeCurrentA=surfaceI;jr.currentMismatchA=std::abs(wireSum-surfaceI);out.maxJunctionCurrentMismatchA=std::max(out.maxJunctionCurrentMismatchA,jr.currentMismatchA);out.junctions.push_back(jr);
        if(c.feedConstraint)
        {
            FeedResult fr;fr.name=c.name;fr.positionM=c.positionM;fr.voltageV=c.voltageV;fr.currentA=wireSum;fr.referenceOhm=std::max(c.referenceOhm,1e-9);fr.surfaceReferenced=true;fr.mappedSurfaceIndex=c.surface.surfaceIndex;fr.mappedSurfaceBasisIndex=c.surface.basis;fr.mappingDistanceM=c.surfaceMappingDistanceM;if(std::abs(wireSum)>1e-18)fr.antennaPlaneInputImpedanceOhm=c.voltageV/wireSum;fr.inputImpedanceOhm=fr.antennaPlaneInputImpedanceOhm;
            if(c.referenceModel==PortReferenceModel::CoaxialReferencePlane&&c.coaxLengthM>0.0&&c.coaxInnerRadiusM>0.0&&c.coaxOuterRadiusM>c.coaxInnerRadiusM&&c.coaxRelativePermittivity>=1.0)
            {
                const double er=std::max(1.0,c.coaxRelativePermittivity),zc=(60.0/std::sqrt(er))*std::log(c.coaxOuterRadiusM/c.coaxInnerRadiusM),beta=omega*std::sqrt(NumericalEM::Mu0*NumericalEM::Epsilon0*er),alpha=0.5*beta*std::max(0.0,c.coaxLossTangent);const C gamma{alpha,beta},z0c{zc,0.0},tt=std::tanh(gamma*c.coaxLengthM),denLine=z0c+fr.antennaPlaneInputImpedanceOhm*tt;if(std::abs(denLine)>1e-18)fr.inputImpedanceOhm=z0c*(fr.antennaPlaneInputImpedanceOhm+z0c*tt)/denLine;fr.referencePlaneCorrected=true;fr.feedLineZ0Ohm=zc;fr.feedLineElectricalLengthDeg=beta*c.coaxLengthM*180.0/NumericalEM::Pi;
            }
            const C z0{fr.referenceOhm,0.0},den=fr.inputImpedanceOhm+z0;if(std::abs(den)>1e-18)fr.reflectionCoefficient=(fr.inputImpedanceOhm-z0)/den;const double gm=std::abs(fr.reflectionCoefficient);fr.returnLossDb=gm>1e-15?-20.0*std::log10(gm):300.0;fr.vswr=gm<1.0?(1+gm)/std::max(1e-15,1-gm):std::numeric_limits<double>::infinity();out.acceptedPowerW+=0.5*std::real(c.voltageV*std::conj(wireSum));out.feeds.push_back(fr);
        }
    }

    // Internal non-terminal branch KCL is satisfied by the rooftop basis itself. Surface-terminal
    // nodes are excluded here because their wire-current sum is intentionally balanced by RWG current.
    for(int ni=0;ni<static_cast<int>(rooftop.nodes.size());++ni)
    {
        const auto &node=rooftop.nodes[static_cast<std::size_t>(ni)];if(node.surfaceTerminal||node.edges.size()<=2)continue;C kcl{0,0};
        for(int ei:node.edges){const bool p0=rooftop.edgeP0Node[static_cast<std::size_t>(ei)]==ni;kcl+=(p0?+1.0:-1.0)*(p0?iP0[static_cast<std::size_t>(ei)]:iP1[static_cast<std::size_t>(ei)]);}out.maxWireBranchKclResidualA=std::max(out.maxWireBranchKclResidualA,std::abs(kcl));
    }

    if(input.computeFarField)
    {
        auto ffMoment=[&](const Vec3&d){return combinedFarFieldMomentRooftop(d,wire,iP0,iP1,tri,jTri,k);};
        auto ff2=[&](const Vec3&d){return normComplex2(ffMoment(d));};
        populateFarFieldResult(out,ff2,input.farFieldCutStepDeg,input.farFieldIntegrationStepDeg,k);
        populateLayeredPropagatingFarFieldAudit(out,input,ffMoment,input.farFieldCutStepDeg,input.farFieldIntegrationStepDeg,k);
    }

    out.wavelengthM=lambda;out.wireUnknownCount=nw;out.wireSolvedDofCount=nr;out.surfaceUnknownCount=ns;out.totalUnknownCount=nt;out.triangleCount=static_cast<int>(tri.size());out.dielectricRegionCount=input.useEffectiveDielectricRegions?static_cast<int>(input.dielectrics.size()):0;out.junctionConstraintCount=ncIndependent;out.wireBranchConstraintCount=rooftop.branchKclCount;out.redundantWireBranchConstraintCount=0;out.reducedWireJunctionBasisUsed=rooftop.branchKclCount>0;out.wireLinearRooftopBasisUsed=true;out.wireRooftopFallbackUsed=false;out.wireOpenEndConstraintCount=rooftop.openEndCount;out.wireOrdinaryContinuityConstraintCount=rooftop.ordinaryContinuityCount;out.wireTerminalNodeCount=rooftop.terminalNodeCount;out.boundaryEdgeCount=boundary;out.halfRwgUnknownCount=halfCount;out.residualRelative=std::sqrt(r2/std::max(1e-30,b2));out.minPivotAbs=stats.minPivot;out.maxPivotAbs=stats.maxPivot;out.powerBalanceRatio=out.acceptedPowerW>1e-18?out.radiatedPowerW/out.acceptedPowerW:0.0;out.radiationEfficiency=(out.radiatedPowerW+out.conductorLossW)>1e-18?out.radiatedPowerW/(out.radiatedPowerW+out.conductorLossW):0.0;
    out.pecTerminalRegularizedComponentCount=static_cast<int>(regularizedConstraintIds.size());out.pecTerminalChargeRegularizationUsed=!regularizedConstraintIds.empty();for(int ci:regularizedConstraintIds)out.maxPecTerminalTransitionScaleM=std::max(out.maxPecTerminalTransitionScaleM,constraints[static_cast<std::size_t>(ci)].surfaceLocalCellSizeM);
    populateLayeredDiagnostics(input,out);
    out.valid=!out.feeds.empty()&&finiteResultCore(out);if(!out.valid&&out.error.empty())out.error="Hybrid solve produced a non-finite numerical result";
    out.note="Experimental hybrid Galerkin MoM with linear rooftop wire currents and RWG surface currents. The rooftop wire-wire block uses radius-aware composite Galerkin integration on both observation and source spans for self/near support pairs, while the RWG surface block now uses Duffy singularity extraction for same-triangle terms and a local composite rule for adjacent triangles. Both rooftop-to-RWG and RWG-to-rooftop mutual blocks are integrated rather than projected from pulse samples. Open free-space wire ends have no rooftop DOF and therefore satisfy I=0 by construction. A wire node galvanically tied to PEC receives one additional terminal half-rooftop/leakage mode. In 5.21, single-terminal wire components balance current against integrated RWG patch divergence and use a local planar-PEC image singularity extraction below the local surface-cell scale; multi-terminal components deliberately skip that half-space approximation. Internal T/Y/X wire nodes retain d-1 KCL-safe modes. The raw pre-symmetry reciprocity metric compares the two independently integrated mutual blocks; the solved Galerkin mutual block uses their Lorentz-reciprocal average and should still be checked together with mesh convergence. Effective dielectric regions, finite-conductivity surface impedance and coax reference-plane embedding remain engineering approximations; validate critical designs against independent full-wave tools or measurement.";
    (void)pulseMeshSeed;
    return out;
}


Result solveDifferentialSurfacePortOnly(const Input &input)
{
    Result out;
    if(!validateInputNumerics(input,out.error)) return out;
    if(!(input.wire.frequencyHz>0.0)){out.error="Hybrid frequency must be positive";return out;}
    if(input.triangles.empty()){out.error="Differential surface port requires PEC surface triangles";return out;}
    if(input.differentialSurfaceFeeds.empty()){out.error="No differential surface feed was provided";return out;}

    std::vector<Vec3>vertices;std::vector<TriData>tri;std::vector<BasisData>bases;
    int boundary=0,halfCount=0;std::string err;
    if(!buildSurfaceBasis(input,input.triangles,std::max(1e-12,input.surfaceVertexMergeToleranceM),vertices,tri,bases,boundary,halfCount,err))
    {out.error=err;return out;}
    const int ns=static_cast<int>(bases.size());
    if(ns>std::max(1,input.maxSurfaceUnknowns)||ns>std::max(2,input.maxTotalUnknowns))
    {out.error="Differential surface-port RWG unknown count exceeds configured limits";return out;}

    const double f=input.wire.frequencyHz,omega=2.0*NumericalEM::Pi*f,lambda=NumericalEM::C0/f,k=2.0*NumericalEM::Pi/lambda;
    const double selfFactor=std::clamp(input.surfaceSelfRegularizationFactor,0.02,1.0);
    std::vector<std::vector<C>>A(static_cast<std::size_t>(ns),std::vector<C>(static_cast<std::size_t>(ns),{0,0}));
    for(int m=0;m<ns;++m)for(int q=0;q<ns;++q)
        A[static_cast<std::size_t>(m)][static_cast<std::size_t>(q)]=surfaceMatrixEntry(input,bases[static_cast<std::size_t>(m)],bases[static_cast<std::size_t>(q)],tri,k,omega,selfFactor);
    out.surfaceReciprocityPreSymmetryRelative=matrixBlockReciprocityRelative(A,0,ns);
    if(input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldCrossFace ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer ||
        (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel))))
        symmetrizeReciprocalMatrixBlock(A,0,ns);
    out.surfaceReciprocityRelative=matrixBlockReciprocityRelative(A,0,ns);

    struct PortBinding{DifferentialSurfaceFeed def;SurfaceTerminalStencil pos,neg;std::vector<double>b;};
    std::vector<PortBinding>ports;ports.reserve(input.differentialSurfaceFeeds.size());
    std::vector<C>rhs(static_cast<std::size_t>(ns),{0,0});bool any=false;
    for(const auto &feed:input.differentialSurfaceFeeds)
    {
        const double tol=std::max(feed.mappingToleranceM,1e-7);
        PortBinding pb;pb.def=feed;
        pb.pos=buildSmoothSurfaceTerminalStencil(feed.positivePositionM,feed.positiveSurfaceIndex,feed.footprintRadiusM,vertices,bases,tri);
        pb.neg=buildSmoothSurfaceTerminalStencil(feed.negativePositionM,feed.negativeSurfaceIndex,feed.footprintRadiusM,vertices,bases,tri);
        if(pb.pos.representative.basis<0||pb.neg.representative.basis<0||pb.pos.surfaceDistanceM>tol||pb.neg.surfaceDistanceM>tol||pb.pos.fluxTerms.empty()||pb.neg.fluxTerms.empty())
        {out.error="A differential surface feed could not map both terminals to local RWG patches";return out;}
        if(pb.pos.representative.surfaceIndex==pb.neg.representative.surfaceIndex)
        {out.error="Differential surface feed terminals must reference two distinct PEC surfaces";return out;}
        pb.b.assign(static_cast<std::size_t>(ns),0.0);
        // A localized scalar-potential jump excites RWG functions through their integrated
        // divergence. The circuit incidence vector is the symmetric half-difference; using
        // the same functional for excitation and current extraction preserves Galerkin power duality.
        // The requested port voltage is the difference V+ - V-.  Split it
        // symmetrically (+V/2,-V/2); this makes b the power-dual circuit incidence
        // vector and avoids the historical factor-of-four Zin normalization error.
        for(const auto&t:pb.pos.fluxTerms)if(t.basis>=0&&t.basis<ns)pb.b[static_cast<std::size_t>(t.basis)]+=0.5*t.integratedDivergenceM;
        for(const auto&t:pb.neg.fluxTerms)if(t.basis>=0&&t.basis<ns)pb.b[static_cast<std::size_t>(t.basis)]-=0.5*t.integratedDivergenceM;
        for(int i=0;i<ns;++i)rhs[static_cast<std::size_t>(i)]-=feed.voltageV*pb.b[static_cast<std::size_t>(i)];
        any=any||std::abs(feed.voltageV)>1e-18;ports.push_back(std::move(pb));
    }
    if(!any){out.error="Differential surface-port solve requires at least one non-zero feed voltage";return out;}

    // Row scaling preserves the solution while improving the dense pivot range.
    std::vector<std::vector<C>>As=A;std::vector<C>bs=rhs;
    for(int i=0;i<ns;++i){double scale=0.0;for(int q=0;q<ns;++q)scale=std::max(scale,std::abs(As[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]));scale=std::max(scale,1e-30);for(int q=0;q<ns;++q)As[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]/=scale;bs[static_cast<std::size_t>(i)]/=scale;}
    SolveStats stats;std::vector<C>is;if(!solveDense(As,bs,is,stats)){out.error="Differential surface-port RWG matrix is singular or ill-conditioned";return out;}
    double r2=0.0,b2=0.0;for(int i=0;i<ns;++i){C ax{0,0};for(int q=0;q<ns;++q)ax+=As[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]*is[static_cast<std::size_t>(q)];r2+=std::norm(ax-bs[static_cast<std::size_t>(i)]);b2+=std::norm(bs[static_cast<std::size_t>(i)]);}

    out.surfaceBases.reserve(bases.size());for(std::size_t i=0;i<bases.size();++i){const auto&b=bases[i];const Vec3 a=vertices[static_cast<std::size_t>(b.edgeV0)],bb=vertices[static_cast<std::size_t>(b.edgeV1)];out.surfaceBases.push_back({b.plus.triangle,b.minus.triangle,a,bb,(a+bb)*0.5,b.edgeLength,is[i],is[i]*b.edgeLength});}
    std::vector<ComplexVec3>jTri;out.triangleCurrents.reserve(tri.size());
    for(int ti=0;ti<static_cast<int>(tri.size());++ti){const auto jj=triangleCurrentAtCentroid(ti,bases,tri,is);jTri.push_back(jj);const double mag=std::sqrt(normComplex2(jj));out.peakSurfaceCurrentApm=std::max(out.peakSurfaceCurrentApm,mag);out.triangleCurrents.push_back({tri[static_cast<std::size_t>(ti)].centroid,tri[static_cast<std::size_t>(ti)].area,jj.x,jj.y,jj.z,mag});}
    out.surfaceImpedanceOhmPerSquare=finiteSheetImpedance(input,omega);
    if(input.useFiniteSurfaceConductivity&&out.surfaceImpedanceOhmPerSquare.real()>0.0)for(std::size_t ti=0;ti<tri.size();++ti)out.conductorLossW+=0.5*out.surfaceImpedanceOhmPerSquare.real()*normComplex2(jTri[ti])*tri[ti].area;

    for(const auto &pb:ports)
    {
        C ip{0,0},ipos{0,0},ineg{0,0};
        for(const auto&t:pb.pos.fluxTerms)if(t.basis>=0&&t.basis<ns)ipos+=t.integratedDivergenceM*is[static_cast<std::size_t>(t.basis)];
        for(const auto&t:pb.neg.fluxTerms)if(t.basis>=0&&t.basis<ns)ineg+=t.integratedDivergenceM*is[static_cast<std::size_t>(t.basis)];
        // Positive source current is current delivered from the source into the positive sheet.
        ip=-0.5*(ipos-ineg);
        out.maxDifferentialPortCurrentImbalanceA=std::max(out.maxDifferentialPortCurrentImbalanceA,std::abs(ipos+ineg));
        const double rmin=std::min(pb.pos.patchRadiusM,pb.neg.patchRadiusM),rmax=std::max(pb.pos.patchRadiusM,pb.neg.patchRadiusM);
        if(!(out.minDifferentialPortFootprintRadiusM>0.0))out.minDifferentialPortFootprintRadiusM=rmin;
        else out.minDifferentialPortFootprintRadiusM=std::min(out.minDifferentialPortFootprintRadiusM,rmin);
        out.maxDifferentialPortFootprintRadiusM=std::max(out.maxDifferentialPortFootprintRadiusM,rmax);
        FeedResult fr;fr.name=pb.def.name;fr.positionM=(pb.def.positivePositionM+pb.def.negativePositionM)*0.5;fr.voltageV=pb.def.voltageV;fr.currentA=ip;fr.referenceOhm=std::max(pb.def.referenceOhm,1e-9);fr.surfaceReferenced=true;fr.mappedSurfaceIndex=pb.pos.representative.surfaceIndex;fr.mappedSurfaceBasisIndex=pb.pos.representative.basis;fr.mappingDistanceM=std::max(pb.pos.surfaceDistanceM,pb.neg.surfaceDistanceM);
        if(std::abs(ip)>1e-18) fr.inputImpedanceOhm=pb.def.voltageV/ip;
        fr.antennaPlaneInputImpedanceOhm=fr.inputImpedanceOhm;
        const C z0{fr.referenceOhm,0.0},den=fr.inputImpedanceOhm+z0;if(std::abs(den)>1e-18)fr.reflectionCoefficient=(fr.inputImpedanceOhm-z0)/den;const double gm=std::abs(fr.reflectionCoefficient);fr.returnLossDb=gm>1e-15?-20.0*std::log10(gm):300.0;fr.vswr=gm<1.0?(1+gm)/std::max(1e-15,1-gm):std::numeric_limits<double>::infinity();out.acceptedPowerW+=0.5*std::real(pb.def.voltageV*std::conj(ip));out.feeds.push_back(fr);
    }

    if(input.computeFarField)
    {
        const std::vector<NumericalEM::WireNetworkMeshSegment> emptyWire;const std::vector<C> emptyI;
        auto ffMoment=[&](const Vec3&d){return combinedFarFieldMoment(d,emptyWire,emptyI,tri,jTri,k);};
        auto ff2=[&](const Vec3&d){return normComplex2(ffMoment(d));};
        populateFarFieldResult(out,ff2,input.farFieldCutStepDeg,input.farFieldIntegrationStepDeg,k);
        populateLayeredPropagatingFarFieldAudit(out,input,ffMoment,input.farFieldCutStepDeg,input.farFieldIntegrationStepDeg,k);
    }
    out.wavelengthM=lambda;out.surfaceUnknownCount=ns;out.totalUnknownCount=ns;out.triangleCount=static_cast<int>(tri.size());out.dielectricRegionCount=input.useEffectiveDielectricRegions?static_cast<int>(input.dielectrics.size()):0;out.boundaryEdgeCount=boundary;out.halfRwgUnknownCount=halfCount;out.residualRelative=std::sqrt(r2/std::max(1e-30,b2));out.minPivotAbs=stats.minPivot;out.maxPivotAbs=stats.maxPivot;out.differentialSurfacePortUsed=true;out.differentialSurfacePortCount=static_cast<int>(ports.size());out.differentialPortSymmetricVoltageSplitUsed=true;out.powerBalanceRatio=out.acceptedPowerW>1e-18?out.radiatedPowerW/out.acceptedPowerW:0.0;out.radiationEfficiency=(out.radiatedPowerW+out.conductorLossW)>1e-18?out.radiatedPowerW/(out.radiatedPowerW+out.conductorLossW):0.0;
    populateLayeredDiagnostics(input,out);
    out.valid=!out.feeds.empty()&&finiteResultCore(out);if(!out.valid&&out.error.empty())out.error="Hybrid solve produced a non-finite numerical result";
    out.note="Surface-only RWG Galerkin solve with an ideal differential lumped port between two PEC terminal patches. Since 5.24 the terminal potential is a Gaussian physical-footprint functional integrated against RWG divergence instead of a binary mesh one-ring, and the requested differential voltage is split symmetrically as +V/2 and -V/2. Port current is the exact power-dual half-difference of the two weighted surface fluxes. This removes the previous factor-of-four circuit normalization error and allows a fixed physical footprint to be held constant during mesh-convergence studies. Same-triangle RWG Green integrals use Duffy singularity extraction and adjacent triangles use localized composite quadrature. The 5.25 layered mode adds the residual finite-slab quasi-static image/fringing correction. The 5.26 Sommerfeld TM scalar mode evaluates k_rho-dependent finite-slab reflection/transmission coefficients and an inverse Hankel transform with a passivity-preserving reactive projection. The 5.29 mode additionally injects the same-face HED TE magnetic-vector-potential correction for tangential RWG self/near interactions and evaluates the coupled TE/TM HED scalar spectrum diagnostically. The 5.30 mode adds the residual transmitted TE/TM tangential vector dyadic between opposite slab faces, subtracts the already-present baseline vector Green function, reactively projects the transmitted residual, and symmetrizes the RWG block by reciprocal Galerkin averaging. The 5.31 mode additionally replaces the TM-only slab-face scalar transition with the coupled HED TE/TM longitudinal scalar reflection/transmission spectrum while retaining the same reactive projection. The 5.32 mode preserves that surface operator and extends the spectral propagation into the two exterior air half-spaces for guarded wire/RWG mutual vector coupling. The 5.33 mode additionally provides a medium-2 multiple-reflection cavity residual and tangential HED scalar-gradient correction for embedded/exterior rooftop-wire↔RWG mutual terms; surface-only solves still reduce numerically to the 5.31 path. Version 5.34 adds the guarded TM VED/via normal-current transition: a diagonal normal vector-potential residual, a VED scalar-gradient contribution for vertical rooftop charge, and the reciprocal normal derivative of the HED scalar response at internal wire observations. Version 5.35 additionally activates residual TM rho-z / z-rho mixed vector components obtained from signed cavity derivatives and a J1 Sommerfeld transform under the same reactive projection and reciprocal block averaging. Version 5.36 can retain the full-complex HED/VED wire↔RWG mutual residual when a driven passive-port monitor succeeds, with automatic fallback to the 5.35 reactive path when it does not. The surface-only operator and layered far-field remain guarded/incomplete. Explicit PEC sheets remain in the MoM system; no extra PEC image plane is added.";
    return out;
}

} // namespace

namespace
{
bool complexLayeredCandidatePassive(const Result &r)
{
    if(!r.valid || r.feeds.empty() || !std::isfinite(r.acceptedPowerW)) return false;
    const double pTol=1e-10*std::max(1.0,std::abs(r.acceptedPowerW));
    if(r.acceptedPowerW < -pTol) return false;
    if(r.conductorLossW > r.acceptedPowerW + std::max(pTol,1e-12)) return false;
    for(const auto &feed:r.feeds)
    {
        const auto z=feed.inputImpedanceOhm;
        if(!std::isfinite(z.real()) || !std::isfinite(z.imag())) return false;
        const double zTol=1e-8*std::max(1.0,std::abs(z));
        if(z.real() < -zTol) return false;
    }
    return true;
}

void populatePowerClosureAudit(Result &r,bool layeredIncompleteFarField)
{
    r.layeredSommerfeldPowerAuditUsed=true;
    r.layeredSommerfeldPowerAuditFarFieldIncomplete=layeredIncompleteFarField;
    const double accounted=r.radiatedPowerW+r.conductorLossW;
    r.powerClosureResidualW=r.acceptedPowerW-accounted;
    r.powerClosureRelative=std::abs(r.acceptedPowerW)>1e-18?r.powerClosureResidualW/std::abs(r.acceptedPowerW):0.0;
}

bool layeredPropagatingPowerGuardPass(const Result &r)
{
    return r.layeredSommerfeldPropagatingFarFieldUsed &&
           r.layeredSommerfeldPropagatingFarFieldPowerConsistent &&
           std::isfinite(r.layeredRadiatedPowerTotalW) &&
           std::isfinite(r.layeredPowerClosureRelative);
}
}

Result solve(const Input &input)
{
    const auto cacheStart=sommerfeldCacheSnapshot();
    Result out;
    if(!validateInputNumerics(input,out.error)) return out;
    if(!(input.wire.frequencyHz>0.0)){out.error="Hybrid frequency must be positive";return out;}
    if(input.wire.wires.empty())
    {
        if(!input.differentialSurfaceFeeds.empty())
        {
            Result surfaceOnly=solveDifferentialSurfacePortOnly(input);
            applySommerfeldCacheDiagnostics(surfaceOnly,cacheStart);
            return surfaceOnly;
        }
        out.error="Hybrid solve requires wire geometry unless an ideal differential surface feed is provided";return out;
    }
    if(input.wire.feeds.empty() && input.surfaceReferencedFeeds.empty() && input.differentialSurfaceFeeds.empty()){out.error="Hybrid solve requires at least one delta-gap wire feed, surface-referenced endpoint feed or differential surface feed";return out;}
    if(!input.differentialSurfaceFeeds.empty()){out.error="Differential surface feeds currently use the surface-only RWG port path; do not combine them with explicit wire geometry in the same solve";return out;}
    if(input.triangles.empty()){out.error="Hybrid solve requires at least one PEC surface triangle";return out;}

    // Reuse the proven wire mesher/topology validator. Surface-referenced endpoint feeds cannot be
    // passed to the wire solver because a synthetic nonzero source is still required to execute
    // its meshing path when the hybrid model uses only surface-referenced endpoint feeds.
    const bool rooftopRequested=input.wire.currentBasisTreatment==NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge;
    const bool rooftopFallback=false;
    auto seedInput=input.wire;seedInput.computeFarField=false;
    // Reuse the historical pulse path only as a deterministic topology/mesh generator. When
    // rooftop mode is requested, the actual hybrid matrix is assembled below with rooftop
    // Galerkin wire equations and integrated rooftop<->RWG mutual blocks.
    seedInput.currentBasisTreatment=NumericalEM::WireCurrentBasisTreatment::PulsePointMatching;
    if(seedInput.feeds.empty())
    {
        const auto &w=seedInput.wires.front();
        seedInput.feeds.push_back(NumericalEM::WireFeed3D{"__HYBRID_MESH_SEED__",(w.aM+w.bM)*0.5,{1.0,0.0},50.0});
    }
    const auto uncoupled=NumericalEM::solveWireNetworkMom(seedInput);
    if(!uncoupled.valid){out.error="Wire pre-mesh failed: "+uncoupled.error;return out;}
    const auto &wire=uncoupled.meshSegments;const int nw=static_cast<int>(wire.size());

    std::vector<Vec3>vertices;std::vector<TriData>tri;std::vector<BasisData>bases;int boundary=0,halfCount=0;std::string err;
    if(!buildSurfaceBasis(input,input.triangles,std::max(1e-12,input.surfaceVertexMergeToleranceM),vertices,tri,bases,boundary,halfCount,err)){out.error=err;return out;}
    const int ns=static_cast<int>(bases.size());
    if(ns>std::max(1,input.maxSurfaceUnknowns)){out.error="Hybrid RWG unknown count exceeds maxSurfaceUnknowns";return out;}

    const double f=input.wire.frequencyHz,omega=2.0*NumericalEM::Pi*f,lambda=NumericalEM::C0/f,k=2.0*NumericalEM::Pi/lambda;
    std::vector<FeedBinding>feedBindings;std::vector<C>rhsWire(static_cast<std::size_t>(nw),{0,0});
    if(!input.wire.feeds.empty())
    {
        if(!bindFeeds(input.wire,wire,feedBindings,rhsWire,err)){out.error=err;return out;}
    }

    bool anyExcitation=false;
    for(const auto &feed:input.wire.feeds) anyExcitation=anyExcitation||std::abs(feed.voltageV)>1e-18;
    std::vector<ConstraintBinding>constraints;
    constraints.reserve(input.surfaceReferencedFeeds.size()+input.galvanicJunctions.size());

    // A surface-referenced feed is a true two-terminal port in this educational hybrid model:
    // one terminal is a wire endpoint and the return terminal is the nearest RWG edge on the
    // selected PEC surface. Current continuity is imposed by a Lagrange constraint.
    for(const auto &feed:input.surfaceReferencedFeeds)
    {
        const double tol=std::max({feed.mappingToleranceM,input.wire.nodeMergeToleranceM*8.0,1e-7});
        auto touches=touchingWireSegments(wire,feed.positionM,tol);
        if(touches.size()!=1){out.error="A surface-referenced feed must map to exactly one meshed wire endpoint; move the feed to an open wire end or reduce its mapping tolerance";return out;}
        const auto terminal=buildSurfaceTerminalStencil(feed.positionM,feed.surfaceIndex,vertices,bases,tri);
        if(terminal.representative.basis<0 || terminal.surfaceDistanceM>tol || terminal.fluxTerms.empty()){out.error="A surface-referenced feed could not map to a local RWG terminal patch on the requested PEC surface";return out;}
        ConstraintBinding c;c.name=feed.name;c.positionM=feed.positionM;c.wireTouches=touches;c.surface=terminal.representative;c.surfaceFluxTerms=terminal.fluxTerms;c.surfacePatchTriangleCount=terminal.triangleCount;c.surfacePatchRadiusM=terminal.patchRadiusM;c.surfaceLocalCellSizeM=terminal.localCellSizeM;c.surfaceMappingDistanceM=terminal.surfaceDistanceM;c.surfaceNormal=terminal.surfaceNormal;c.surfaceSign=1.0;c.feedConstraint=true;c.voltageV=feed.voltageV;c.referenceOhm=feed.referenceOhm;
        c.referenceModel=feed.referenceModel;c.coaxInnerRadiusM=feed.coaxInnerRadiusM;c.coaxOuterRadiusM=feed.coaxOuterRadiusM;c.coaxRelativePermittivity=feed.coaxRelativePermittivity;c.coaxLossTangent=feed.coaxLossTangent;c.coaxLengthM=feed.coaxLengthM;constraints.push_back(c);
        const auto &wt=touches.front();const auto &seg=wire[static_cast<std::size_t>(wt.segment)];
        rhsWire[static_cast<std::size_t>(wt.segment)]+=wt.sign*feed.voltageV/std::max(seg.lengthM,1e-18);
        anyExcitation=anyExcitation||std::abs(feed.voltageV)>1e-18;
    }

    // Additional solder/probe junctions carry no source voltage; they only impose KCL between
    // the touching wire segment(s) and the mapped RWG edge current.
    for(const auto &junc:input.galvanicJunctions)
    {
        const double tol=std::max({junc.mappingToleranceM,input.wire.nodeMergeToleranceM*8.0,1e-7});
        auto touches=touchingWireSegments(wire,junc.positionM,tol);
        if(touches.empty() || touches.size()>2){out.error="A galvanic wire/PEC junction must map to one open wire end or one degree-2 wire node";return out;}
        const auto terminal=buildSurfaceTerminalStencil(junc.positionM,junc.surfaceIndex,vertices,bases,tri);
        if(terminal.representative.basis<0 || terminal.surfaceDistanceM>tol || terminal.fluxTerms.empty()){out.error="A galvanic wire/PEC junction could not map to a local RWG terminal patch on the requested surface";return out;}
        ConstraintBinding c;c.name=junc.name;c.positionM=junc.positionM;c.wireTouches=touches;c.surface=terminal.representative;c.surfaceFluxTerms=terminal.fluxTerms;c.surfacePatchTriangleCount=terminal.triangleCount;c.surfacePatchRadiusM=terminal.patchRadiusM;c.surfaceLocalCellSizeM=terminal.localCellSizeM;c.surfaceMappingDistanceM=terminal.surfaceDistanceM;c.surfaceNormal=terminal.surfaceNormal;c.surfaceSign=(junc.surfaceCurrentSign>=0.0?1.0:-1.0);constraints.push_back(c);
    }
    if(!anyExcitation){out.error="Hybrid solve requires at least one non-zero feed voltage";return out;}

    if(rooftopRequested)
    {
        Result candidate=solveRooftopHybridCore(input,uncoupled,wire,vertices,tri,bases,boundary,halfCount,feedBindings,constraints);
        if(complexLayeredMode(input.dielectricKernelModel))
        {
            const std::complex<double> candidateZ=!candidate.feeds.empty()?candidate.feeds.front().inputImpedanceOhm:std::complex<double>{0.0,0.0};
            const double candidatePower=candidate.acceptedPowerW;
            const bool passivePortOk=complexLayeredCandidatePassive(candidate);
            const bool layeredPowerOk=!layeredPropagatingFarFieldMode(input.dielectricKernelModel) || layeredPropagatingPowerGuardPass(candidate);
            if(!passivePortOk || !layeredPowerOk)
            {
                Input safe=input;safe.dielectricKernelModel=DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal;
                Result fallback=solve(safe);
                fallback.layeredSommerfeldComplexTransitionAttempted=true;
                fallback.layeredSommerfeldComplexTransitionFallbackUsed=true;
                fallback.layeredSommerfeldComplexCandidateInputImpedanceOhm=candidateZ;
                fallback.layeredSommerfeldComplexCandidateAcceptedPowerW=candidatePower;
                if(layeredPropagatingFarFieldMode(input.dielectricKernelModel))
                {
                    fallback.layeredSommerfeldPropagatingFarFieldUsed=candidate.layeredSommerfeldPropagatingFarFieldUsed;
                    fallback.layeredSommerfeldPropagatingFarFieldPowerConsistent=candidate.layeredSommerfeldPropagatingFarFieldPowerConsistent;
                    fallback.layeredSommerfeldPropagatingFarFieldGuardRejected=!layeredPowerOk;
                    fallback.layeredSommerfeldFarFieldSourceSide=candidate.layeredSommerfeldFarFieldSourceSide;
                    fallback.layeredRadiatedPowerUpperW=candidate.layeredRadiatedPowerUpperW;fallback.layeredRadiatedPowerLowerW=candidate.layeredRadiatedPowerLowerW;fallback.layeredRadiatedPowerTotalW=candidate.layeredRadiatedPowerTotalW;
                    fallback.layeredDirectivityLinear=candidate.layeredDirectivityLinear;fallback.layeredDirectivityDbi=candidate.layeredDirectivityDbi;
                    fallback.layeredPowerClosureResidualW=candidate.layeredPowerClosureResidualW;fallback.layeredPowerClosureRelative=candidate.layeredPowerClosureRelative;
                    fallback.layeredSommerfeldSurfaceWavePoleAuditUsed=candidate.layeredSommerfeldSurfaceWavePoleAuditUsed;
                    fallback.layeredSommerfeldSurfaceWavePowerResolved=candidate.layeredSommerfeldSurfaceWavePowerResolved;
                    fallback.layeredSommerfeldSurfaceWavePowerGuarded=candidate.layeredSommerfeldSurfaceWavePowerGuarded;
                    fallback.layeredSommerfeldSurfaceWavePoleCount=candidate.layeredSommerfeldSurfaceWavePoleCount;
                    fallback.layeredSurfaceWavePowerW=candidate.layeredSurfaceWavePowerW;
                    fallback.layeredSommerfeldSurfaceWavePoles=candidate.layeredSommerfeldSurfaceWavePoles;
                    fallback.layeredSommerfeldGroundedPecModeAuditUsed=candidate.layeredSommerfeldGroundedPecModeAuditUsed;
                    fallback.layeredSommerfeldGroundedPecBoundaryDetected=candidate.layeredSommerfeldGroundedPecBoundaryDetected;
                    fallback.layeredSommerfeldGroundedPecPowerGuarded=candidate.layeredSommerfeldGroundedPecPowerGuarded;
                    fallback.layeredSommerfeldGroundedPecModeCount=candidate.layeredSommerfeldGroundedPecModeCount;
                    fallback.layeredSommerfeldGroundCoverageFraction=candidate.layeredSommerfeldGroundCoverageFraction;
                    fallback.layeredSommerfeldGroundedPecModes=candidate.layeredSommerfeldGroundedPecModes;
                }
                populatePowerClosureAudit(fallback,true);
                fallback.note += !passivePortOk ? " Version 5.36 attempted the full-complex wire/RWG HED/VED residual and rejected this candidate because the driven passive-port guard failed; the returned solution is the audited 5.35 reactive projection." : " Version 5.37 rejected the complex candidate because the propagating TE/TM layered far-field power exceeded the accepted passive-port budget; the returned solution is the audited 5.35 reactive projection.";
                applySommerfeldCacheDiagnostics(fallback,cacheStart);
                return fallback;
            }
            candidate.layeredSommerfeldComplexTransitionAttempted=true;
            candidate.layeredSommerfeldComplexTransitionUsed=true;
            candidate.layeredSommerfeldComplexCandidateInputImpedanceOhm=candidateZ;
            candidate.layeredSommerfeldComplexCandidateAcceptedPowerW=candidatePower;
            populatePowerClosureAudit(candidate,true);
            if(layeredGroundedPecModeAuditMode(input.dielectricKernelModel))
                candidate.note += " Version 5.39 replaces the bare-slab pole list with an air/dielectric/PEC grounded-slab modal audit when sufficient RWG ground coverage is detected. The TM/TE propagation constants and unit-interface-field W/m normalization use the same PEC boundary type as the microstrip ground. Surface-wave excitation power remains guarded until a finite-ground current-to-mode pole overlap is validated; the closure residual is not relabelled as modal power.";
            else if(layeredSurfaceWavePoleAuditMode(input.dielectricKernelModel))
                candidate.note += " Version 5.38 additionally complex-refines the TE/TM guided-pole candidates of the same finite dielectric-slab Sommerfeld denominator used by the 5.37 propagating audit. Pole propagation constants and reflection residues are reported, but P_surface-wave remains guarded because the explicit PEC ground belongs to the RWG matrix and is not part of the analytic slab pole equation; assigning power before a common grounded-stack modal normalization would be inconsistent.";
            else if(layeredPropagatingFarFieldMode(input.dielectricKernelModel))
                candidate.note += " Version 5.37 additionally evaluates the propagating exterior TE/TM slab spectrum in both air hemispheres. The complex candidate passed both the passive-port guard and the one-sided propagating-radiation power budget. Surface-wave pole residues and explicit dielectric-loss power remain unresolved closure channels.";
            else
                candidate.note += " Version 5.36 retains the full-complex wire/RWG HED/VED Sommerfeld residual because the driven passive-port guard remained positive. The radiation closure diagnostic still uses the free-space far-field moment and is therefore advisory until the layered far-field tensor is implemented.";
        }
        applySommerfeldCacheDiagnostics(candidate,cacheStart);
        return candidate;
    }

    const auto wireBranchConstraints=branchedWireNodes(wire);
    const int nb=nw+ns;
    const int ncSurface=static_cast<int>(constraints.size());

    std::vector<std::vector<double>> branchMatrix(wireBranchConstraints.size(),
                                                  std::vector<double>(static_cast<std::size_t>(nw),0.0));
    for(int bi=0;bi<static_cast<int>(wireBranchConstraints.size());++bi)
        for(const auto &wt:wireBranchConstraints[static_cast<std::size_t>(bi)])
            branchMatrix[static_cast<std::size_t>(bi)][static_cast<std::size_t>(wt.segment)] += wt.sign;
    NullspaceBasis junctionBasis;
    if(!buildConstraintNullspace(branchMatrix,nw,junctionBasis)){out.error="Failed to build the hybrid T/Y/X junction-current basis";return out;}
    const int ncBranch=junctionBasis.rank;
    const int redundantBranch=static_cast<int>(wireBranchConstraints.size())-ncBranch;
    const bool reducedJunctionBasis=input.wire.junctionTreatment==NumericalEM::WireJunctionTreatment::ReducedBasis && ncBranch>0;
    const int wireSolveDofs=reducedJunctionBasis?nw-ncBranch:nw;

    // First assemble the physical pulse/RWG system with only wire<->PEC terminal constraints.
    // Internal branch KCL is either eliminated by a null-space current basis below or appended as
    // independent Lagrange rows in the legacy formulation.
    const int baseN=nb+ncSurface;
    std::vector<std::vector<C>>baseA(static_cast<std::size_t>(baseN),std::vector<C>(static_cast<std::size_t>(baseN),{0,0}));
    std::vector<C>baseRhs(static_cast<std::size_t>(baseN),{0,0});
    for(int i=0;i<nw;++i)baseRhs[static_cast<std::size_t>(i)]=rhsWire[static_cast<std::size_t>(i)];

    // Wire-wire block: same pulse/point-matching operator as solveWireNetworkMom(), now with
    // an optional localized effective-medium dielectric Green function.
    const auto pecImageByEdge=pecTerminalImageConstraintByWireEdge(wire,constraints,std::max(input.wire.nodeMergeToleranceM,1e-9));
    std::vector<int> regularizedConstraintIds;for(int ci:pecImageByEdge)if(ci>=0)regularizedConstraintIds.push_back(ci);std::sort(regularizedConstraintIds.begin(),regularizedConstraintIds.end());regularizedConstraintIds.erase(std::unique(regularizedConstraintIds.begin(),regularizedConstraintIds.end()),regularizedConstraintIds.end());
    for(int m=0;m<nw;++m)
    {
        const auto&obs=wire[static_cast<std::size_t>(m)];const Vec3 ro=obs.centerM;
        for(int q=0;q<nw;++q)
        {
            const auto&src=wire[static_cast<std::size_t>(q)];const double aEff=0.5*(std::max(obs.radiusM,1e-12)+std::max(src.radiusM,1e-12));
            C zww=integrateProjectedWirePulse(input,ro,obs.tangent,src.p0M,src.p1M,src.tangent,aEff,k,omega);
            const int ci=pecImageByEdge[static_cast<std::size_t>(q)];if(ci>=0){const auto &terminal=constraints[static_cast<std::size_t>(ci)];const double w=pecImageBlendWeight(src,terminal);if(w>1e-8){const auto image=mirroredPecImageSegment(src,terminal);zww-=w*integrateProjectedWirePulse(input,ro,obs.tangent,image.p0M,image.p1M,image.tangent,aEff,k,omega);}}
            baseA[static_cast<std::size_t>(m)][static_cast<std::size_t>(q)]=zww;
        }
    }

    // Surface-surface block and both mutual blocks.
    const double selfFactor=std::clamp(input.surfaceSelfRegularizationFactor,0.02,1.0);const double mutualFactor=std::clamp(input.mutualRegularizationFactor,0.005,0.5);
    for(int m=0;m<ns;++m)
    {
        for(int q=0;q<ns;++q)baseA[static_cast<std::size_t>(nw+m)][static_cast<std::size_t>(nw+q)]=surfaceMatrixEntry(input,bases[static_cast<std::size_t>(m)],bases[static_cast<std::size_t>(q)],tri,k,omega,selfFactor);
        for(int q=0;q<nw;++q)baseA[static_cast<std::size_t>(nw+m)][static_cast<std::size_t>(q)]=surfaceTestOfWirePulse(input,bases[static_cast<std::size_t>(m)],tri,wire[static_cast<std::size_t>(q)],k,omega);
    }
    for(int m=0;m<nw;++m)
    {
        const auto&obs=wire[static_cast<std::size_t>(m)];
        for(int q=0;q<ns;++q)baseA[static_cast<std::size_t>(m)][static_cast<std::size_t>(nw+q)]=dotComplex(obs.tangent,surfaceElectricFieldForBasis(input,bases[static_cast<std::size_t>(q)],tri,obs.centerM,k,omega,mutualFactor,obs.radiusM));
    }

    out.surfaceReciprocityPreSymmetryRelative=matrixBlockReciprocityRelative(baseA,nw,ns);
    if(input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldCrossFace ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight ||
       input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldInternalLayer ||
        (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedNormal || (input.dielectricKernelModel==DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal || complexLayeredMode(input.dielectricKernelModel))))
        symmetrizeReciprocalMatrixBlock(baseA,nw,ns);
    out.surfaceReciprocityRelative=matrixBlockReciprocityRelative(baseA,nw,ns);

    // Galvanic constraints use Lagrange multipliers even when wire-internal branch KCL is
    // eliminated. Their wire coefficients are transformed together with the wire EFIE block.
    for(int ci=0;ci<ncSurface;++ci)
    {
        const int row=nb+ci;const auto &c=constraints[static_cast<std::size_t>(ci)];
        for(const auto &wt:c.wireTouches)
        {
            baseA[static_cast<std::size_t>(row)][static_cast<std::size_t>(wt.segment)]+=wt.sign;
            baseA[static_cast<std::size_t>(wt.segment)][static_cast<std::size_t>(row)]+=wt.sign;
        }
        for(const auto &term:c.surfaceFluxTerms)
        {
            const double sc=c.surfaceSign*term.integratedDivergenceM;
            const int scol=nw+term.basis;
            baseA[static_cast<std::size_t>(row)][static_cast<std::size_t>(scol)]+=sc;
            baseA[static_cast<std::size_t>(scol)][static_cast<std::size_t>(row)]+=sc;
        }
    }

    std::vector<std::vector<C>>A;
    std::vector<C>rhs;
    int nt=0;
    if(reducedJunctionBasis)
    {
        nt=wireSolveDofs+ns+ncSurface;
        if(nt>std::max(2,input.maxTotalUnknowns)){out.error="Hybrid reduced junction-basis system exceeds maxTotalUnknowns; coarsen the wire/surface meshes or increase the limit";return out;}
        A.assign(static_cast<std::size_t>(nt),std::vector<C>(static_cast<std::size_t>(nt),{0,0}));rhs.assign(static_cast<std::size_t>(nt),{0,0});
        // T^T * wire equations / T * wire trial functions.
        for(int a=0;a<wireSolveDofs;++a)
        {
            for(int i=0;i<nw;++i)rhs[static_cast<std::size_t>(a)]+=junctionBasis.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)]*baseRhs[static_cast<std::size_t>(i)];
            for(int b=0;b<wireSolveDofs;++b)
            {
                C v{0,0};for(int i=0;i<nw;++i){const double ti=junctionBasis.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)];if(std::abs(ti)<1e-18)continue;for(int q=0;q<nw;++q){const double tq=junctionBasis.t[static_cast<std::size_t>(q)][static_cast<std::size_t>(b)];if(std::abs(tq)<1e-18)continue;v+=ti*baseA[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]*tq;}}A[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)]=v;
            }
            for(int q=nw;q<baseN;++q){C v{0,0};for(int i=0;i<nw;++i)v+=junctionBasis.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)]*baseA[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)];A[static_cast<std::size_t>(a)][static_cast<std::size_t>(wireSolveDofs+q-nw)]=v;}
        }
        for(int pidx=nw;pidx<baseN;++pidx)
        {
            const int rp=wireSolveDofs+pidx-nw;rhs[static_cast<std::size_t>(rp)]=baseRhs[static_cast<std::size_t>(pidx)];
            for(int b=0;b<wireSolveDofs;++b){C v{0,0};for(int q=0;q<nw;++q)v+=baseA[static_cast<std::size_t>(pidx)][static_cast<std::size_t>(q)]*junctionBasis.t[static_cast<std::size_t>(q)][static_cast<std::size_t>(b)];A[static_cast<std::size_t>(rp)][static_cast<std::size_t>(b)]=v;}
            for(int q=nw;q<baseN;++q)A[static_cast<std::size_t>(rp)][static_cast<std::size_t>(wireSolveDofs+q-nw)]=baseA[static_cast<std::size_t>(pidx)][static_cast<std::size_t>(q)];
        }
    }
    else
    {
        nt=baseN+ncBranch;
        if(nt>std::max(2,input.maxTotalUnknowns)){out.error="Hybrid total unknown count exceeds maxTotalUnknowns; coarsen wire/surface meshes, reduce branch/junction constraints or increase the limit";return out;}
        A.assign(static_cast<std::size_t>(nt),std::vector<C>(static_cast<std::size_t>(nt),{0,0}));rhs.assign(static_cast<std::size_t>(nt),{0,0});
        for(int i=0;i<baseN;++i){rhs[static_cast<std::size_t>(i)]=baseRhs[static_cast<std::size_t>(i)];for(int q=0;q<baseN;++q)A[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]=baseA[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)];}
        for(int ri=0;ri<ncBranch;++ri)
        {
            const int bi=junctionBasis.independentRows[static_cast<std::size_t>(ri)];const int row=baseN+ri;
            for(const auto &wt:wireBranchConstraints[static_cast<std::size_t>(bi)]){A[static_cast<std::size_t>(row)][static_cast<std::size_t>(wt.segment)]+=wt.sign;A[static_cast<std::size_t>(wt.segment)][static_cast<std::size_t>(row)]+=wt.sign;}
        }
    }

    // The equation families have different test-function units. Normalize each row before
    // Gaussian elimination; this is a pure algebraic scaling and greatly improves conditioning.
    double mutual2=0.0;std::size_t mutualN=0;const int surfaceStart=wireSolveDofs;const int physicalBlock=wireSolveDofs+ns;
    for(int i=0;i<nt;++i)
    {
        double scale=0.0;for(int q=0;q<nt;++q)scale=std::max(scale,std::abs(A[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]));scale=std::max(scale,1e-30);
        for(int q=0;q<nt;++q){A[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]/=scale;if(i<physicalBlock&&q<physicalBlock&&((i<surfaceStart)!=(q<surfaceStart))){mutual2+=std::norm(A[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]);++mutualN;}}
        rhs[static_cast<std::size_t>(i)]/=scale;
    }
    out.normalizedMutualCouplingRms=mutualN?std::sqrt(mutual2/double(mutualN)):0.0;

    SolveStats stats;std::vector<C>x;if(!solveDense(A,rhs,x,stats)){out.error=reducedJunctionBasis?"Hybrid reduced T/Y/X junction-basis matrix is singular or ill-conditioned":"Hybrid wire/RWG dense block matrix is singular or ill-conditioned";return out;}
    double r2=0.0,b2=0.0;for(int i=0;i<nt;++i){C ax{0,0};for(int q=0;q<nt;++q)ax+=A[static_cast<std::size_t>(i)][static_cast<std::size_t>(q)]*x[static_cast<std::size_t>(q)];r2+=std::norm(ax-rhs[static_cast<std::size_t>(i)]);b2+=std::norm(rhs[static_cast<std::size_t>(i)]);}

    std::vector<C>iw(static_cast<std::size_t>(nw),{0,0});
    if(reducedJunctionBasis){for(int i=0;i<nw;++i)for(int a=0;a<wireSolveDofs;++a)iw[static_cast<std::size_t>(i)]+=junctionBasis.t[static_cast<std::size_t>(i)][static_cast<std::size_t>(a)]*x[static_cast<std::size_t>(a)];}
    else std::copy(x.begin(),x.begin()+nw,iw.begin());
    const int solvedSurfaceStart=reducedJunctionBasis?wireSolveDofs:nw;
    std::vector<C>is(x.begin()+solvedSurfaceStart,x.begin()+solvedSurfaceStart+ns);
    out.wireSegments=wire;for(int i=0;i<nw;++i){out.wireSegments[static_cast<std::size_t>(i)].currentA=iw[static_cast<std::size_t>(i)];out.peakWireCurrentA=std::max(out.peakWireCurrentA,std::abs(iw[static_cast<std::size_t>(i)]));}
    for(const auto &branch:wireBranchConstraints)
    {
        C kcl{0,0};for(const auto &wt:branch)kcl+=wt.sign*iw[static_cast<std::size_t>(wt.segment)];
        out.maxWireBranchKclResidualA=std::max(out.maxWireBranchKclResidualA,std::abs(kcl));
    }
    out.surfaceBases.reserve(bases.size());for(std::size_t i=0;i<bases.size();++i){const auto&b=bases[i];const Vec3 a=vertices[static_cast<std::size_t>(b.edgeV0)],bb=vertices[static_cast<std::size_t>(b.edgeV1)];out.surfaceBases.push_back({b.plus.triangle,b.minus.triangle,a,bb,(a+bb)*0.5,b.edgeLength,is[i],is[i]*b.edgeLength});}
    std::vector<ComplexVec3>jTri;out.triangleCurrents.reserve(tri.size());
    for(int ti=0;ti<static_cast<int>(tri.size());++ti){const auto j=triangleCurrentAtCentroid(ti,bases,tri,is);jTri.push_back(j);const double mag=std::sqrt(normComplex2(j));out.peakSurfaceCurrentApm=std::max(out.peakSurfaceCurrentApm,mag);out.triangleCurrents.push_back({tri[static_cast<std::size_t>(ti)].centroid,tri[static_cast<std::size_t>(ti)].area,j.x,j.y,j.z,mag});}
    out.surfaceImpedanceOhmPerSquare=finiteSheetImpedance(input,omega);
    if(input.useFiniteSurfaceConductivity && out.surfaceImpedanceOhmPerSquare.real()>0.0)
        for(std::size_t ti=0;ti<tri.size();++ti) out.conductorLossW += 0.5*out.surfaceImpedanceOhmPerSquare.real()*normComplex2(jTri[ti])*tri[ti].area;

    out.feeds.reserve(input.wire.feeds.size());
    for(std::size_t fi=0;fi<input.wire.feeds.size();++fi)
    {
        const auto&feed=input.wire.feeds[fi];const auto&bind=feedBindings[fi];const C ifeed=0.5*(bind.s0*iw[static_cast<std::size_t>(bind.e0)]+bind.s1*iw[static_cast<std::size_t>(bind.e1)]);FeedResult fr;fr.name=feed.name;fr.positionM=feed.positionM;fr.voltageV=feed.voltageV;fr.currentA=ifeed;fr.referenceOhm=std::max(feed.referenceOhm,1e-9);if(std::abs(ifeed)>1e-18)fr.inputImpedanceOhm=feed.voltageV/ifeed;if(fi<uncoupled.feeds.size())fr.uncoupledInputImpedanceOhm=uncoupled.feeds[fi].activeImpedanceOhm;const C z0{fr.referenceOhm,0.0},den=fr.inputImpedanceOhm+z0;if(std::abs(den)>1e-18)fr.reflectionCoefficient=(fr.inputImpedanceOhm-z0)/den;const double gm=std::abs(fr.reflectionCoefficient);fr.returnLossDb=gm>1e-15?-20.0*std::log10(gm):300.0;fr.vswr=gm<1.0?(1+gm)/std::max(1e-15,1-gm):std::numeric_limits<double>::infinity();out.acceptedPowerW+=0.5*std::real(feed.voltageV*std::conj(ifeed));out.feeds.push_back(fr);
    }

    for(const auto &c:constraints)
    {
        C wireSum{0,0};for(const auto &wt:c.wireTouches)wireSum+=wt.sign*iw[static_cast<std::size_t>(wt.segment)];
        const C surfaceI=surfaceTerminalReturnCurrent(c,is);
        JunctionResult jr;jr.name=c.name;jr.requestedPositionM=c.positionM;jr.mappedSurfaceEdgeCenterM=c.surface.edgeCenter;jr.mappedSurfaceIndex=c.surface.surfaceIndex;jr.mappedSurfaceBasisIndex=c.surface.basis;jr.touchingWireSegmentCount=static_cast<int>(c.wireTouches.size());jr.surfaceMappingDistanceM=c.surfaceMappingDistanceM;jr.wireMappingDistanceM=0.0;for(const auto &wt:c.wireTouches)jr.wireMappingDistanceM=std::max(jr.wireMappingDistanceM,wt.distanceM);jr.wireCurrentSumA=wireSum;jr.surfaceEdgeCurrentA=surfaceI;jr.currentMismatchA=std::abs(wireSum-surfaceI);out.maxJunctionCurrentMismatchA=std::max(out.maxJunctionCurrentMismatchA,jr.currentMismatchA);out.junctions.push_back(jr);
        if(c.feedConstraint)
        {
            FeedResult fr;fr.name=c.name;fr.positionM=c.positionM;fr.voltageV=c.voltageV;fr.currentA=wireSum;fr.referenceOhm=std::max(c.referenceOhm,1e-9);fr.surfaceReferenced=true;fr.mappedSurfaceIndex=c.surface.surfaceIndex;fr.mappedSurfaceBasisIndex=c.surface.basis;fr.mappingDistanceM=c.surfaceMappingDistanceM;
            if(std::abs(wireSum)>1e-18)fr.antennaPlaneInputImpedanceOhm=c.voltageV/wireSum;
            fr.inputImpedanceOhm=fr.antennaPlaneInputImpedanceOhm;
            if(c.referenceModel==PortReferenceModel::CoaxialReferencePlane && c.coaxLengthM>0.0 && c.coaxInnerRadiusM>0.0 && c.coaxOuterRadiusM>c.coaxInnerRadiusM && c.coaxRelativePermittivity>=1.0)
            {
                const double er=std::max(1.0,c.coaxRelativePermittivity);
                const double zc=(60.0/std::sqrt(er))*std::log(c.coaxOuterRadiusM/c.coaxInnerRadiusM);
                const double beta=omega*std::sqrt(NumericalEM::Mu0*NumericalEM::Epsilon0*er);
                const double alpha=0.5*beta*std::max(0.0,c.coaxLossTangent);
                const C gamma{alpha,beta};const C z0c{zc,0.0};const C t=std::tanh(gamma*c.coaxLengthM);const C denLine=z0c+fr.antennaPlaneInputImpedanceOhm*t;
                if(std::abs(denLine)>1e-18)fr.inputImpedanceOhm=z0c*(fr.antennaPlaneInputImpedanceOhm+z0c*t)/denLine;
                fr.referencePlaneCorrected=true;fr.feedLineZ0Ohm=zc;fr.feedLineElectricalLengthDeg=beta*c.coaxLengthM*180.0/NumericalEM::Pi;
            }
            const C z0{fr.referenceOhm,0.0},den=fr.inputImpedanceOhm+z0;if(std::abs(den)>1e-18)fr.reflectionCoefficient=(fr.inputImpedanceOhm-z0)/den;const double gm=std::abs(fr.reflectionCoefficient);fr.returnLossDb=gm>1e-15?-20.0*std::log10(gm):300.0;fr.vswr=gm<1.0?(1+gm)/std::max(1e-15,1-gm):std::numeric_limits<double>::infinity();out.acceptedPowerW+=0.5*std::real(c.voltageV*std::conj(wireSum));out.feeds.push_back(fr);
        }
    }

    if(input.computeFarField)
    {
        auto ffMoment=[&](const Vec3&d){return combinedFarFieldMoment(d,wire,iw,tri,jTri,k);};
        auto ff2=[&](const Vec3&d){return normComplex2(ffMoment(d));};
        populateFarFieldResult(out,ff2,input.farFieldCutStepDeg,input.farFieldIntegrationStepDeg,k);
        populateLayeredPropagatingFarFieldAudit(out,input,ffMoment,input.farFieldCutStepDeg,input.farFieldIntegrationStepDeg,k);
    }

    out.wavelengthM=lambda;out.wireUnknownCount=nw;out.wireSolvedDofCount=wireSolveDofs;out.surfaceUnknownCount=ns;out.totalUnknownCount=nt;out.triangleCount=static_cast<int>(tri.size());out.dielectricRegionCount=input.useEffectiveDielectricRegions?static_cast<int>(input.dielectrics.size()):0;out.junctionConstraintCount=ncSurface;out.wireBranchConstraintCount=ncBranch;out.redundantWireBranchConstraintCount=redundantBranch;out.reducedWireJunctionBasisUsed=reducedJunctionBasis;out.wireRooftopFallbackUsed=rooftopFallback;out.boundaryEdgeCount=boundary;out.halfRwgUnknownCount=halfCount;out.residualRelative=std::sqrt(r2/std::max(1e-30,b2));out.minPivotAbs=stats.minPivot;out.maxPivotAbs=stats.maxPivot;out.powerBalanceRatio=out.acceptedPowerW>1e-18?out.radiatedPowerW/out.acceptedPowerW:0.0;out.radiationEfficiency=(out.radiatedPowerW+out.conductorLossW)>1e-18?out.radiatedPowerW/(out.radiatedPowerW+out.conductorLossW):0.0;
    out.pecTerminalRegularizedComponentCount=static_cast<int>(regularizedConstraintIds.size());out.pecTerminalChargeRegularizationUsed=!regularizedConstraintIds.empty();for(int ci:regularizedConstraintIds)out.maxPecTerminalTransitionScaleM=std::max(out.maxPecTerminalTransitionScaleM,constraints[static_cast<std::size_t>(ci)].surfaceLocalCellSizeM);
    populateLayeredDiagnostics(input,out);
    out.valid=!out.feeds.empty()&&finiteResultCore(out);if(!out.valid&&out.error.empty())out.error="Hybrid solve produced a non-finite numerical result";
    out.note="Experimental educational hybrid dense MoM: 3D thin-wire and triangular RWG surface currents are solved in one coupled block system. The pulse wire-wire block uses the same radius-aware composite Gauss integration as the standalone wire solver for self and near-neighbour interactions. Internal T/Y/X wire junctions can use the same rank-aware reduced current basis as the wire-only solver, while wire/PEC galvanic terminals use explicit constraints. The RWG surface block uses Duffy singularity extraction on same-triangle source integrals and one-level composite quadrature for adjacent triangles. Terminal half-RWG functions, optional finite-conductivity/thickness surface impedance and midpoint/overlap effective-medium dielectric kernels remain available. The 5.25 layered option supplies a residual quasi-static finite-slab image/fringing correction. The 5.26 Sommerfeld mode adds a numerically transformed k_rho-dependent TM scalar correction with a passivity-preserving reactive projection. The 5.29 transition also adds the same-face HED TE magnetic-vector-potential contribution and evaluates the coupled TE/TM HED scalar spectrum diagnostically. The 5.30 mode adds a residual reactively projected transmitted TE/TM tangential dyadic between opposite slab faces and reciprocal RWG symmetrization while retaining the raw pre-symmetry diagnostic. The 5.31 mode activates the coupled HED TE/TM longitudinal scalar reflection/transmission spectrum on slab-face RWG interactions under the same reactive projection. The 5.32 mode additionally propagates the TE/TM spectrum into exterior source/observer heights and injects the residual tangential vector-potential correction into wire↔RWG mutual interactions. The 5.33 mode adds the internal-medium-2 cavity residual for tangential embedded sources and activates the matching tangential HED scalar-gradient term for both exterior and internal rooftop-wire↔RWG pairs, followed by the existing reciprocal mutual-block average. Version 5.34 adds the guarded TM VED/via normal-current transition with diagonal normal vector residual, VED scalar-gradient coupling and reciprocal HED full-gradient response. Version 5.35 activates the residual TM rho-z / z-rho mixed vector components from signed cavity derivatives and a J1 transform while retaining reactive projection and Lorentz-reciprocal mutual-block averaging. Version 5.36 is the first monitored full-complex lift of the HED/VED wire↔RWG mutual residual: passive candidates are retained, while negative-resistance/power candidates are automatically re-solved with the 5.35 reactive operator. The surface-only complex lift and layered far-field radiation still require completion before this can be called a general layered MPIE. The coax option is an external TEM reference-plane transform rather than an exact coax aperture. Use mesh/junction convergence studies and validate critical printed-antenna designs against independent full-wave tools.";
    if(complexLayeredMode(input.dielectricKernelModel))
    {
        const std::complex<double> candidateZ=!out.feeds.empty()?out.feeds.front().inputImpedanceOhm:std::complex<double>{0.0,0.0};
        const double candidatePower=out.acceptedPowerW;
        const bool passivePortOk=complexLayeredCandidatePassive(out);
        const bool layeredPowerOk=!layeredPropagatingFarFieldMode(input.dielectricKernelModel) || layeredPropagatingPowerGuardPass(out);
        if(!passivePortOk || !layeredPowerOk)
        {
            Input safe=input;safe.dielectricKernelModel=DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal;
            Result fallback=solve(safe);
            fallback.layeredSommerfeldComplexTransitionAttempted=true;fallback.layeredSommerfeldComplexTransitionFallbackUsed=true;
            fallback.layeredSommerfeldComplexCandidateInputImpedanceOhm=candidateZ;fallback.layeredSommerfeldComplexCandidateAcceptedPowerW=candidatePower;
            if(layeredPropagatingFarFieldMode(input.dielectricKernelModel))
            {
                fallback.layeredSommerfeldPropagatingFarFieldUsed=out.layeredSommerfeldPropagatingFarFieldUsed;fallback.layeredSommerfeldPropagatingFarFieldPowerConsistent=out.layeredSommerfeldPropagatingFarFieldPowerConsistent;fallback.layeredSommerfeldPropagatingFarFieldGuardRejected=!layeredPowerOk;
                fallback.layeredSommerfeldFarFieldSourceSide=out.layeredSommerfeldFarFieldSourceSide;fallback.layeredRadiatedPowerUpperW=out.layeredRadiatedPowerUpperW;fallback.layeredRadiatedPowerLowerW=out.layeredRadiatedPowerLowerW;fallback.layeredRadiatedPowerTotalW=out.layeredRadiatedPowerTotalW;fallback.layeredDirectivityLinear=out.layeredDirectivityLinear;fallback.layeredDirectivityDbi=out.layeredDirectivityDbi;fallback.layeredPowerClosureResidualW=out.layeredPowerClosureResidualW;fallback.layeredPowerClosureRelative=out.layeredPowerClosureRelative;
                fallback.layeredSommerfeldSurfaceWavePoleAuditUsed=out.layeredSommerfeldSurfaceWavePoleAuditUsed;fallback.layeredSommerfeldSurfaceWavePowerResolved=out.layeredSommerfeldSurfaceWavePowerResolved;fallback.layeredSommerfeldSurfaceWavePowerGuarded=out.layeredSommerfeldSurfaceWavePowerGuarded;fallback.layeredSommerfeldSurfaceWavePoleCount=out.layeredSommerfeldSurfaceWavePoleCount;fallback.layeredSurfaceWavePowerW=out.layeredSurfaceWavePowerW;fallback.layeredSommerfeldSurfaceWavePoles=out.layeredSommerfeldSurfaceWavePoles;
                fallback.layeredSommerfeldGroundedPecModeAuditUsed=out.layeredSommerfeldGroundedPecModeAuditUsed;fallback.layeredSommerfeldGroundedPecBoundaryDetected=out.layeredSommerfeldGroundedPecBoundaryDetected;fallback.layeredSommerfeldGroundedPecPowerGuarded=out.layeredSommerfeldGroundedPecPowerGuarded;fallback.layeredSommerfeldGroundedPecModeCount=out.layeredSommerfeldGroundedPecModeCount;fallback.layeredSommerfeldGroundCoverageFraction=out.layeredSommerfeldGroundCoverageFraction;fallback.layeredSommerfeldGroundedPecModes=out.layeredSommerfeldGroundedPecModes;
            }
            populatePowerClosureAudit(fallback,true);
            fallback.note += !passivePortOk ? " Version 5.36 rejected the full-complex pulse wire/RWG residual on the passive-port guard and returned the audited 5.35 reactive solution." : " Version 5.37 rejected the full-complex pulse candidate on the propagating TE/TM layered-radiation power guard and returned the audited 5.35 reactive solution.";
            applySommerfeldCacheDiagnostics(fallback,cacheStart);
            return fallback;
        }
        out.layeredSommerfeldComplexTransitionAttempted=true;out.layeredSommerfeldComplexTransitionUsed=true;
        out.layeredSommerfeldComplexCandidateInputImpedanceOhm=candidateZ;out.layeredSommerfeldComplexCandidateAcceptedPowerW=candidatePower;
        populatePowerClosureAudit(out,true);
        if(layeredGroundedPecModeAuditMode(input.dielectricKernelModel))
            out.note += " Version 5.39 reports the grounded air/dielectric/PEC TE/TM modal spectrum when explicit RWG coverage is detected on the slab back face, including unit-interface-field modal W/m normalization. Excitation power remains guarded pending a finite-ground source/current overlap validation.";
        else if(layeredSurfaceWavePoleAuditMode(input.dielectricKernelModel))
            out.note += " Version 5.38 reports complex TE/TM guided-pole candidates of the dielectric-only slab denominator. Their power contribution remains guarded until the analytic modal boundary model includes the explicit RWG ground and a consistent modal normalization/source-overlap integral.";
    }
    applySommerfeldCacheDiagnostics(out,cacheStart);
    return out;
}

} // namespace HybridWireSurfaceMom

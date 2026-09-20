#include "hybrid_wire_surface_mom.h"
#include "antenna_surface_geometry.h"

#include <cstdint>
#include <iostream>

namespace
{
HybridWireSurfaceMom::Input makeLayeredPlateCase(double relativePermittivity,double frequencyHz=100e6)
{
    HybridWireSurfaceMom::Input in;
    in.wire.frequencyHz=frequencyHz;
    in.computeFarField=false;
    in.wire.computeFarField=false;

    AntennaSurface::SurfaceSpec lower;
    lower.kind=AntennaSurface::SurfaceKind::Rectangle;
    lower.widthM=0.10;
    lower.heightM=0.10;
    lower.center={0.0,0.0,0.0};
    lower.meshHintM=0.05;
    auto upper=lower;
    upper.center={0.0,0.0,0.01};

    const auto lowerMesh=AntennaSurface::triangulate(lower,16);
    const auto upperMesh=AntennaSurface::triangulate(upper,16);
    for(const auto &t:lowerMesh) in.triangles.push_back({t.a,t.b,t.c,0});
    for(const auto &t:upperMesh) in.triangles.push_back({t.a,t.b,t.c,1});

    HybridWireSurfaceMom::DifferentialSurfaceFeed feed;
    feed.name="P";
    feed.positivePositionM={0.0,0.0,0.01};
    feed.negativePositionM={0.0,0.0,0.0};
    feed.positiveSurfaceIndex=1;
    feed.negativeSurfaceIndex=0;
    feed.voltageV={1.0,0.0};
    feed.referenceOhm=50.0;
    feed.mappingToleranceM=0.05;
    feed.footprintRadiusM=0.025;
    in.differentialSurfaceFeeds.push_back(feed);

    HybridWireSurfaceMom::DielectricRegion slab;
    slab.name="D";
    slab.centerM={0.0,0.0,0.005};
    slab.widthM=0.10;
    slab.heightM=0.10;
    slab.thicknessM=0.01;
    slab.relativePermittivity=relativePermittivity;
    slab.lossTangent=0.0;
    slab.fieldFillFactor=0.6;
    in.dielectrics.push_back(slab);
    in.useEffectiveDielectricRegions=true;
    in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed;
    in.enableTerminalHalfRwg=false;
    in.maxSurfaceUnknowns=100;
    in.maxTotalUnknowns=120;
    return in;
}

bool require(bool condition,const char *message)
{
    if(condition) return true;
    std::cerr<<message<<'\n';
    return false;
}
}

int main(int argc,char **argv)
{
    const bool stress=argc>1 && std::string(argv[1])=="--stress";

    const auto first=HybridWireSurfaceMom::solve(makeLayeredPlateCase(3.0));
    const auto repeated=HybridWireSurfaceMom::solve(makeLayeredPlateCase(3.0));
    const auto materialChanged=HybridWireSurfaceMom::solve(makeLayeredPlateCase(3.1));
    const auto frequencyChanged=HybridWireSurfaceMom::solve(makeLayeredPlateCase(3.1,101e6));

    if(!require(first.valid&&repeated.valid&&materialChanged.valid&&frequencyChanged.valid,"layered cache test solve failed")) return 2;
    if(!require(first.layeredSommerfeldCacheTableBuilds>=1 && first.layeredSommerfeldCacheMisses>=1,"first solve did not build a Sommerfeld table")) return 3;
    if(!require(first.layeredSommerfeldCacheHits>100,"first solve did not reuse the table during matrix assembly")) return 4;
    if(!require(repeated.layeredSommerfeldCacheTableBuilds==0 && repeated.layeredSommerfeldCacheMisses==0,"identical solve did not reuse resident Sommerfeld tables")) return 5;
    if(!require(repeated.layeredSommerfeldCacheHits>100,"identical solve reported no cache hits")) return 6;
    if(!require(materialChanged.layeredSommerfeldCacheTableBuilds>=1 && materialChanged.layeredSommerfeldCacheMisses>=1,"material change did not invalidate the physical cache key")) return 7;
    if(!require(frequencyChanged.layeredSommerfeldCacheTableBuilds>=1 && frequencyChanged.layeredSommerfeldCacheMisses>=1,"frequency change did not invalidate the physical cache key")) return 8;
    if(!require(frequencyChanged.layeredSommerfeldFaceCacheEntries<=32 && frequencyChanged.layeredSommerfeldExteriorCacheEntries<=24 && frequencyChanged.layeredSommerfeldInternalCacheEntries<=32,"Sommerfeld cache exceeded its bounded resident capacity")) return 9;

    std::uint64_t stressEvictions=0;
    if(stress)
    {
        for(int i=0;i<34;++i)
        {
            const auto r=HybridWireSurfaceMom::solve(makeLayeredPlateCase(3.25,120e6+double(i)*1e6));
            if(!require(r.valid,"stress cache solve failed")) return 10;
            stressEvictions+=r.layeredSommerfeldCacheEvictions;
        }
        if(!require(stressEvictions>0,"bounded LRU stress did not exercise eviction")) return 11;
    }

    std::cout<<"5.42-C cache: first hits="<<first.layeredSommerfeldCacheHits
             <<" misses="<<first.layeredSommerfeldCacheMisses
             <<" builds="<<first.layeredSommerfeldCacheTableBuilds
             <<" build_ms="<<first.layeredSommerfeldCacheBuildTimeMs
             <<" | repeated builds="<<repeated.layeredSommerfeldCacheTableBuilds
             <<" | material builds="<<materialChanged.layeredSommerfeldCacheTableBuilds
             <<" | frequency builds="<<frequencyChanged.layeredSommerfeldCacheTableBuilds;
    if(stress) std::cout<<" | stress evictions="<<stressEvictions;
    std::cout<<'\n';
    return 0;
}

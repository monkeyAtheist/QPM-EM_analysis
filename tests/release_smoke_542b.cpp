#include "hybrid_wire_surface_mom.h"
#include "antenna_surface_geometry.h"
#include "numerical_em_solvers.h"
#include "pec_surface_mom.h"
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>

bool finiteC(std::complex<double> z){return std::isfinite(z.real())&&std::isfinite(z.imag());}
HybridWireSurfaceMom::Input plates(double er,double v){
    HybridWireSurfaceMom::Input in; in.wire.frequencyHz=100e6;in.computeFarField=false;in.wire.computeFarField=false;
    AntennaSurface::SurfaceSpec a;a.kind=AntennaSurface::SurfaceKind::Rectangle;a.widthM=.10;a.heightM=.10;a.center={0,0,0};a.meshHintM=.05;auto b=a;b.center={0,0,.01};
    auto am=AntennaSurface::triangulate(a,16);auto bm=AntennaSurface::triangulate(b,16);for(auto&t:am){in.triangles.push_back({t.a,t.b,t.c,0});}for(auto&t:bm){in.triangles.push_back({t.a,t.b,t.c,1});}
    HybridWireSurfaceMom::DifferentialSurfaceFeed p;p.name="P";p.positivePositionM={0,0,.01};p.negativePositionM={0,0,0};p.positiveSurfaceIndex=1;p.negativeSurfaceIndex=0;p.voltageV={v,0};p.referenceOhm=50;p.mappingToleranceM=.05;p.footprintRadiusM=.025;in.differentialSurfaceFeeds.push_back(p);
    HybridWireSurfaceMom::DielectricRegion d;d.name="D";d.centerM={0,0,.005};d.widthM=.10;d.heightM=.10;d.thicknessM=.01;d.relativePermittivity=er;d.lossTangent=0;d.fieldFillFactor=er>1?.6:0;in.dielectrics.push_back(d);in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::SegmentOverlapWeighted;in.enableTerminalHalfRwg=false;in.maxSurfaceUnknowns=100;in.maxTotalUnknowns=120;return in;
}
int main(){
    auto r=HybridWireSurfaceMom::solve(plates(3.0,1.0));auto r2=HybridWireSurfaceMom::solve(plates(3.0,2.0));if(!r.valid||!r2.valid||r.feeds.empty()){std::cerr<<r.error<<"\n";return 2;}const auto &f=r.feeds[0];auto expected=(f.inputImpedanceOhm-std::complex<double>(f.referenceOhm,0))/(f.inputImpedanceOhm+std::complex<double>(f.referenceOhm,0));double gm=std::abs(f.reflectionCoefficient);double rl=gm>1e-15?-20*std::log10(gm):300;double vs=gm<1?(1+gm)/(1-gm):std::numeric_limits<double>::infinity();
    if(!finiteC(f.inputImpedanceOhm)||f.inputImpedanceOhm.real()<=0||r.acceptedPowerW<=0) return 3;
    if(std::abs(expected-f.reflectionCoefficient)>1e-11) return 4;
    if(std::abs(rl-f.returnLossDb)>1e-10) return 5;
    if(std::isfinite(vs)&&std::abs(vs-f.vswr)>1e-10) return 6;
    if(std::abs(r2.feeds[0].inputImpedanceOhm-f.inputImpedanceOhm)>1e-8*std::max(1.0,std::abs(f.inputImpedanceOhm))) return 7;
    if(std::abs(r2.acceptedPowerW/r.acceptedPowerW-4.0)>1e-7) return 8;
    auto bad=plates(std::numeric_limits<double>::quiet_NaN(),1.0);auto rb=HybridWireSurfaceMom::solve(bad);if(rb.valid||rb.error.empty())return 9;
    NumericalEM::WireNetworkMomInput wi;wi.frequencyHz=100e6;wi.wires.push_back({"W",{-0.5,0,0},{0.5,0,0},.001});wi.feeds.push_back({"F",{0,0,0},{1,0},50});wi.wires[0].aM.x=std::numeric_limits<double>::infinity();auto wr=NumericalEM::solveWireNetworkMom(wi);if(wr.valid||wr.error.empty())return 10;
    PecSurfaceMom::Input pi;pi.frequencyHz=100e6;pi.triangles.push_back({{0,0,0},{1,0,0},{0,1,0},0});pi.excitation.propagationDirection.x=std::numeric_limits<double>::quiet_NaN();auto pr=PecSurfaceMom::solve(pi);if(pr.valid||pr.error.empty())return 11;
    std::cout<<"fast RF smoke Zin="<<f.inputImpedanceOhm<<" Gamma="<<f.reflectionCoefficient<<" RL="<<f.returnLossDb<<" VSWR="<<f.vswr<<" P="<<r.acceptedPowerW<<"\n";return 0;
}

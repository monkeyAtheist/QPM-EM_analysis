#include "laplace_rail_model.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>

static bool near(double a,double b,double rel=1e-9){return std::abs(a-b)<=rel*std::max({1.0,std::abs(a),std::abs(b)});}
int main(){
    using namespace LaplaceRail;
    Inputs in; in.mode=Mode::ConstantCurrent; in.magneticFieldT=0.5; in.railSeparationM=0.1; in.fieldAngleDeg=90; in.massKg=0.05; in.currentA=2; in.initialVelocityMps=0; in.durationS=1;
    auto r=solve(in); if(!r.valid){std::cerr<<r.error<<"\n";return 1;} const auto&s=r.states.back();
    // k=.05 N/A, F=.1 N, a=2 m/s2, v(1)=2, x(1)=1
    if(!near(r.couplingNPerA,0.05)||!near(s.forceN,0.1)||!near(s.accelerationMps2,2.0)||!near(s.velocityMps,2.0)||!near(s.positionM,1.0)||!near(s.motionalEmfV,0.1)) return 2;
    in.mode=Mode::VoltageDriven; in.sourceVoltageV=6; in.totalResistanceOhm=2; in.durationS=10;
    r=solve(in); if(!r.valid)return 3; if(!near(r.timeConstantS,40.0,1e-10)||!near(r.terminalVelocityMps,120.0,1e-10))return 4;
    // Energy/power identity at a representative state: Psource = Pjoule + Pmech.
    auto q=stateAt(in,1.0); if(!near(q.sourcePowerW,q.joulePowerW+q.mechanicalPowerW,1e-9)) return 5;
    in.mode=Mode::GeneratorBraking; in.initialVelocityMps=10; in.sourceVoltageV=0; in.durationS=40;
    r=solve(in); if(!r.valid)return 6; const auto&g=r.states.back(); if(!near(g.velocityMps,10.0/std::exp(1.0),1e-8))return 7; if(!(g.forceN<0&&g.currentA<0&&g.mechanicalPowerW<0))return 8;
    in.magneticFieldT=std::numeric_limits<double>::quiet_NaN(); if(solve(in).valid)return 9;
    std::cout<<"QTsignalApp 6.3.0 Laplace rail regression: PASS\n";
    return 0;
}

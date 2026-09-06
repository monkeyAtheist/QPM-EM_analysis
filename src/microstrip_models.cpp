#include "microstrip_models.h"

#include "numerical_em_solvers.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace MicrostripModels
{
namespace
{
constexpr double Pi = NumericalEM::Pi;

bool sane(double widthM, double heightM, double er)
{
    return std::isfinite(widthM) && std::isfinite(heightM) && std::isfinite(er) &&
           widthM > 0.0 && heightM > 0.0 && er >= 1.0;
}
}

double effectivePermittivity(double widthM, double substrateHeightM, double relativePermittivity)
{
    if(!sane(widthM,substrateHeightM,relativePermittivity)) return std::numeric_limits<double>::quiet_NaN();
    const double u=widthM/substrateHeightM;
    double correction=1.0/std::sqrt(1.0+12.0/u);
    if(u<1.0) correction += 0.04*std::pow(1.0-u,2.0);
    return 0.5*(relativePermittivity+1.0)+0.5*(relativePermittivity-1.0)*correction;
}

double characteristicImpedanceOhm(double widthM, double substrateHeightM, double relativePermittivity)
{
    if(!sane(widthM,substrateHeightM,relativePermittivity)) return std::numeric_limits<double>::quiet_NaN();
    const double u=widthM/substrateHeightM;
    const double ee=effectivePermittivity(widthM,substrateHeightM,relativePermittivity);
    if(u<=1.0)
        return (60.0/std::sqrt(ee))*std::log(8.0/u+0.25*u);
    return (120.0*Pi)/(std::sqrt(ee)*(u+1.393+0.667*std::log(u+1.444)));
}

double widthForImpedance(double targetOhm, double substrateHeightM, double relativePermittivity)
{
    if(!(targetOhm>0.0) || !(substrateHeightM>0.0) || !(relativePermittivity>=1.0))
        return std::numeric_limits<double>::quiet_NaN();
    double lo=1e-4*substrateHeightM,hi=1e3*substrateHeightM;
    for(int i=0;i<120;++i)
    {
        const double mid=std::sqrt(lo*hi);
        const double z=characteristicImpedanceOhm(mid,substrateHeightM,relativePermittivity);
        // Z0 decreases monotonically as width increases.
        if(z>targetOhm) lo=mid; else hi=mid;
    }
    return std::sqrt(lo*hi);
}

LineResult analyzeLine(double frequencyHz, double widthM, double substrateHeightM,
                       double relativePermittivity, double lossTangent, double physicalLengthM)
{
    LineResult out;
    out.widthM=widthM;out.heightM=substrateHeightM;out.relativePermittivity=relativePermittivity;
    if(!(frequencyHz>0.0) || !sane(widthM,substrateHeightM,relativePermittivity)) return out;
    out.effectivePermittivity=effectivePermittivity(widthM,substrateHeightM,relativePermittivity);
    out.characteristicImpedanceOhm=characteristicImpedanceOhm(widthM,substrateHeightM,relativePermittivity);
    out.guidedWavelengthM=NumericalEM::C0/(frequencyHz*std::sqrt(out.effectivePermittivity));
    out.electricalLengthDeg=physicalLengthM>0.0?360.0*physicalLengthM/out.guidedWavelengthM:0.0;
    // First-order dielectric attenuation for a quasi-TEM line. Conductor/roughness/radiation loss
    // are intentionally excluded so the displayed number remains interpretable.
    const double beta=2.0*Pi/out.guidedWavelengthM;
    out.dielectricAttenuationNpPerM=0.5*beta*std::max(0.0,lossTangent);
    out.valid=std::isfinite(out.characteristicImpedanceOhm)&&out.characteristicImpedanceOhm>0.0;
    return out;
}

PatchResult rectangularPatch(double frequencyHz, double substrateHeightM, double relativePermittivity)
{
    PatchResult out;out.frequencyHz=frequencyHz;
    if(!(frequencyHz>0.0)||!(substrateHeightM>0.0)||!(relativePermittivity>=1.0)) return out;
    const double lambda0=NumericalEM::C0/frequencyHz;
    out.widthM=0.5*lambda0*std::sqrt(2.0/(relativePermittivity+1.0));
    out.effectivePermittivity=effectivePermittivity(out.widthM,substrateHeightM,relativePermittivity);
    const double wh=out.widthM/substrateHeightM;
    out.fringingExtensionM=0.412*substrateHeightM*((out.effectivePermittivity+0.3)*(wh+0.264))/
                            std::max(1e-15,(out.effectivePermittivity-0.258)*(wh+0.8));
    out.effectiveLengthM=0.5*lambda0/std::sqrt(out.effectivePermittivity);
    out.physicalLengthM=std::max(1e-9,out.effectiveLengthM-2.0*out.fringingExtensionM);
    out.valid=std::isfinite(out.physicalLengthM)&&out.physicalLengthM>0.0;
    return out;
}

InsetResult insetForResistance(double patchLengthM, double edgeResistanceOhm, double targetResistanceOhm)
{
    InsetResult out;out.edgeResistanceOhm=edgeResistanceOhm;out.targetResistanceOhm=targetResistanceOhm;
    if(!(patchLengthM>0.0)||!(edgeResistanceOhm>0.0)||!(targetResistanceOhm>0.0)||targetResistanceOhm>edgeResistanceOhm) return out;
    const double ratio=std::clamp(targetResistanceOhm/edgeResistanceOhm,0.0,1.0);
    out.insetDepthM=(patchLengthM/Pi)*std::acos(std::sqrt(ratio));
    out.insetDepthM=std::clamp(out.insetDepthM,0.0,0.49*patchLengthM);
    out.valid=std::isfinite(out.insetDepthM);
    return out;
}

std::complex<double> transformImpedance(std::complex<double> loadOhm,
                                        std::complex<double> z0Ohm,
                                        std::complex<double> gammaPerM,
                                        double lengthM)
{
    if(!(lengthM>0.0) || std::abs(z0Ohm)<1e-18) return loadOhm;
    const auto t=std::tanh(gammaPerM*lengthM);
    const auto den=z0Ohm+loadOhm*t;
    if(std::abs(den)<1e-18) return {std::numeric_limits<double>::infinity(),0.0};
    return z0Ohm*(loadOhm+z0Ohm*t)/den;
}

} // namespace MicrostripModels

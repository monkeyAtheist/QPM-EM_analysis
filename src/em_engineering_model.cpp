#include "em_engineering_model.h"

#include <algorithm>
#include <limits>

namespace EmEngineering
{
namespace
{
double positive(double x, double fallback)
{
    return std::isfinite(x) && x > 0.0 ? x : fallback;
}

EmVec3 safePolarization(const EmVec3 &k, const EmVec3 &requested)
{
    EmVec3 e = requested - k * emDot(requested, k);
    if (e.norm() < 1e-12)
    {
        const EmVec3 ref = std::abs(k.z) < 0.8 ? EmVec3{0.0, 0.0, 1.0} : EmVec3{0.0, 1.0, 0.0};
        e = emCross(ref, k);
    }
    return emNormalized(e);
}

void reflectionMetrics(double r, double x, double z0, AntennaResult &out)
{
    z0 = positive(z0, 50.0);
    const std::complex<double> z(r, x);
    const std::complex<double> gamma = (z - z0) / (z + z0);
    out.reflectionMagnitude = std::min(0.999999999, std::abs(gamma));
    out.returnLossDb = out.reflectionMagnitude > 1e-15 ? -20.0 * std::log10(out.reflectionMagnitude) : 300.0;
    out.vswr = (1.0 + out.reflectionMagnitude) / (1.0 - out.reflectionMagnitude);
}

}

MagneticMaterialResult magneticMaterial(const MagneticMaterialInput &input, double frequencyHz)
{
    MagneticMaterialResult out;
    if (!input.enabled)
        return out;
    const double f = positive(frequencyHz, 1.0);
    const double muStatic = std::max(1.0, input.lowFrequencyRelativePermeability);
    const double fc = positive(input.relaxationFrequencyHz, 1e30);
    const double x = f / fc;
    // Single-pole relaxation model with mu* = mu' - j mu'' (e^(jwt)).
    // It is an educational approximation; use manufacturer complex-mu data
    // when quantitative ferrite-loss accuracy is required.
    out.muPrime = 1.0 + (muStatic - 1.0) / (1.0 + x*x);
    out.muDoublePrime = (muStatic - 1.0) * x / (1.0 + x*x);
    if (input.additionalLossTangentAtReference > 0.0)
    {
        const double fref = positive(input.referenceFrequencyHz, f);
        const double exponent = std::clamp(input.additionalLossExponent, 0.0, 3.0);
        const double extraTan = input.additionalLossTangentAtReference *
                                std::pow(std::max(f/fref, 1e-12), exponent);
        out.muDoublePrime += out.muPrime * extraTan;
    }
    out.lossTangent = out.muPrime > 1e-18 ? out.muDoublePrime / out.muPrime : 0.0;
    return out;
}

CoreLossResult coreLoss(const CoreLossInput &input)
{
    CoreLossResult out;
    const double f = positive(input.frequencyHz, 1.0);
    const double b = positive(input.fluxDensityPeakT, 1e-12);
    const double volume = std::max(0.0, input.coreVolumeM3);

    if (input.useDatasheetCurve && !input.datasheetCurve.empty())
    {
        struct Candidate { double d2; const CoreLossDatasheetPoint *p; };
        std::vector<Candidate> candidates;
        candidates.reserve(input.datasheetCurve.size());
        const double lf = std::log(std::max(f, 1e-30));
        const double lb = std::log(std::max(b, 1e-30));
        for (const auto &p : input.datasheetCurve)
        {
            if (!(p.frequencyHz > 0.0 && p.fluxDensityPeakT > 0.0 && p.powerDensityWPerM3 >= 0.0))
                continue;
            const double df = std::log(p.frequencyHz) - lf;
            const double db = std::log(p.fluxDensityPeakT) - lb;
            candidates.push_back({df*df + db*db, &p});
        }
        if (!candidates.empty())
        {
            std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b){ return a.d2 < b.d2; });
            const std::size_t n = std::min<std::size_t>(4, candidates.size());
            if (candidates.front().d2 < 1e-20)
                out.powerDensityWPerM3 = candidates.front().p->powerDensityWPerM3;
            else
            {
                double sw = 0.0, sp = 0.0;
                for (std::size_t i=0;i<n;++i)
                {
                    const double w = 1.0 / std::max(candidates[i].d2, 1e-12);
                    sw += w;
                    sp += w * candidates[i].p->powerDensityWPerM3;
                }
                out.powerDensityWPerM3 = sw > 0.0 ? sp/sw : 0.0;
            }
            out.usedDatasheetCurve = true;
            out.valid = true;
            out.note = QStringLiteral("Manufacturer/measured core-loss interpolation in log(f)-log(Bpk) space using the four nearest valid points. Use data covering the intended operating region; this is not a physical extrapolation model.");
        }
    }

    if (!out.valid && input.useSteinmetz)
    {
        const double k = std::max(0.0, input.steinmetzK);
        const double alpha = std::clamp(input.steinmetzAlpha, 0.0, 5.0);
        const double beta = std::clamp(input.steinmetzBeta, 0.0, 8.0);
        out.powerDensityWPerM3 = k * std::pow(f, alpha) * std::pow(b, beta);
        out.valid = std::isfinite(out.powerDensityWPerM3);
        out.note = QStringLiteral("Classical Steinmetz sinusoidal estimate Pv = k f^alpha Bpk^beta with f in Hz, Bpk in tesla and Pv in W/m^3. Coefficients are material-, temperature- and unit-system-specific; use manufacturer coefficients in these exact SI units.");
    }

    if (!out.valid)
    {
        out.note = QStringLiteral("No valid core-loss model selected.");
        return out;
    }

    out.powerDensityWPerM3 = std::max(0.0, out.powerDensityWPerM3);
    out.totalCoreLossW = out.powerDensityWPerM3 * volume;
    const double irms = std::abs(input.currentRmsA);
    out.equivalentSeriesResistanceOhm = irms > 1e-15 ? out.totalCoreLossW/(irms*irms) : 0.0;
    return out;
}

WindingLossResult windingLoss(const WindingLossInput &input)
{
    WindingLossResult out;
    const double f = positive(input.frequencyHz, 1.0);
    const double rho = positive(input.conductorResistivityOhmM, CopperResistivity20C);
    const double a = 0.5 * positive(input.wireDiameterM, 1e-9);
    out.skinDepthM = std::sqrt(rho / (Pi * f * Mu0));
    const double highFreqRatio = a / std::max(2.0*out.skinDepthM, 1e-30);
    out.skinEffectFactor = std::sqrt(1.0 + highFreqRatio*highFreqRatio);
    const double fref = positive(input.proximityReferenceFrequencyHz, f);
    const double exponent = std::clamp(input.proximityExponent, 0.0, 3.0);
    out.proximityEffectFactor = 1.0 + std::max(0.0,input.proximityExtraAtReference) *
                                      std::pow(std::max(f/fref,1e-12), exponent);
    if (input.useDowellMultilayerEstimate)
    {
        const int layers = std::max(1, input.layerCount);
        const double thickness = positive(input.effectiveLayerConductorThicknessM, 2.0*a);
        const double xi = thickness / std::max(out.skinDepthM, 1e-30);
        if (xi > 1e-4 && xi < 40.0)
        {
            const double sh=std::sinh(xi), si=std::sin(xi), ch=std::cosh(xi), co=std::cos(xi);
            const double term1=(sh+si)/std::max(ch-co,1e-30);
            const double term2=(sh-si)/std::max(ch+co,1e-30);
            out.dowellMultilayerFactor = std::max(1.0, 0.5*xi*(term1 + ((2.0*layers*layers-1.0)/3.0)*term2));
        }
        else if (xi >= 40.0)
        {
            out.dowellMultilayerFactor = std::max(1.0, 0.5*xi*(1.0 + (2.0*layers*layers-1.0)/3.0));
        }
    }
    out.acResistanceOhm = std::max(0.0,input.dcResistanceOhm) *
                          out.skinEffectFactor * out.proximityEffectFactor * out.dowellMultilayerFactor;
    return out;
}

PlaneWaveResult planeWave(const PlaneWaveInput &input)
{
    PlaneWaveResult out;
    const double er = positive(input.epsilonR, 1.0);
    const double mr = positive(input.muR, 1.0);
    const double eps = Epsilon0 * er;
    const double mu = Mu0 * mr;
    const double f = positive(input.frequencyHz, 1.0);
    const EmVec3 k = emNormalized(input.propagation);
    const EmVec3 ehat = safePolarization(k, input.polarization);

    out.waveSpeed = 1.0 / std::sqrt(mu * eps);
    out.wavelength = out.waveSpeed / f;
    out.impedanceOhm = std::sqrt(mu / eps);
    out.omegaRadPerSecond = 2.0 * Pi * f;
    out.betaRadPerMeter = 2.0 * Pi / out.wavelength;

    const double phase = out.omegaRadPerSecond * input.timeSeconds
                       - out.betaRadPerMeter * emDot(k, input.point)
                       + input.phaseDeg * Pi / 180.0;
    const double instantaneousE = input.electricAmplitudeVpm * std::cos(phase);
    out.electricField = ehat * instantaneousE;
    out.magneticField = emCross(k, out.electricField) / out.impedanceOhm;
    out.magneticFluxDensity = out.magneticField * mu;
    out.poyntingInstant = emCross(out.electricField, out.magneticField);
    out.poyntingAverage = k * (input.electricAmplitudeVpm * input.electricAmplitudeVpm / (2.0 * out.impedanceOhm));
    out.note = QStringLiteral("Lossless homogeneous-medium plane wave. E amplitude is peak, so <S> = E0²/(2η).");
    return out;
}

HertzianDipoleResult hertzianDipoleFarField(const HertzianDipoleInput &input)
{
    HertzianDipoleResult out;
    const double er = positive(input.epsilonR, 1.0);
    const double mr = positive(input.muR, 1.0);
    const double eps = Epsilon0 * er;
    const double mu = Mu0 * mr;
    const double speed = 1.0 / std::sqrt(mu * eps);
    const double eta = std::sqrt(mu / eps);
    const double f = positive(input.frequencyHz, 1.0);
    const double lambda = speed / f;
    const double beta = 2.0 * Pi / lambda;
    const double omega = 2.0 * Pi * f;
    const EmVec3 axis = emNormalized(input.axis);
    const EmVec3 rvec = input.point - input.center;
    const double r = rvec.norm();
    out.wavelength = lambda;
    out.distanceM = r;
    if (r < 1e-12)
    {
        out.note = QStringLiteral("Observation point coincides with the dipole center.");
        return out;
    }
    const EmVec3 rhat = rvec / r;
    const double cosTheta = std::clamp(emDot(axis, rhat), -1.0, 1.0);
    const double sinTheta = std::sqrt(std::max(0.0, 1.0 - cosTheta*cosTheta));
    out.thetaDeg = std::acos(cosTheta) * 180.0 / Pi;
    out.radiationResistanceOhm = 80.0 * Pi * Pi * std::pow(input.elementLengthM / lambda, 2.0);

    EmVec3 eTheta = rhat * cosTheta - axis;
    if (eTheta.norm() > 1e-12)
        eTheta = emNormalized(eTheta);
    else
        eTheta = {0.0, 0.0, 0.0};

    const double ePeak = eta * beta * input.currentAmplitudeA * input.elementLengthM
                       * sinTheta / (4.0 * Pi * r);
    const double phase = omega * input.timeSeconds - beta * r + input.phaseDeg * Pi / 180.0;
    out.electricFieldFar = eTheta * (ePeak * std::cos(phase));
    out.magneticFieldFar = emCross(rhat, out.electricFieldFar) / eta;
    out.magneticFluxDensityFar = out.magneticFieldFar * mu;
    out.poyntingAverage = rhat * (ePeak * ePeak / (2.0 * eta));

    const double largestDimension = std::max(std::abs(input.elementLengthM), 1e-9);
    const double fraunhofer = 2.0 * largestDimension * largestDimension / lambda;
    out.farFieldLikely = r > std::max(lambda, fraunhofer);
    out.note = out.farFieldLikely
        ? QStringLiteral("Hertzian-dipole radiation-zone model; observation point satisfies a simple far-field check.")
        : QStringLiteral("Hertzian far-field formula used outside its preferred range. Increase observation distance for reliable results.");
    return out;
}

AntennaResult antennaEstimate(const AntennaInput &input)
{
    AntennaResult out;
    const double f = positive(input.frequencyHz, 1.0);
    out.wavelengthM = C0 / f;
    const double lambda = out.wavelengthM;
    const double length = positive(input.lengthM, 1e-6);
    out.electricalLengthLambda = length / lambda;

    switch (input.type)
    {
    case AntennaType::HertzianDipole:
        out.radiationResistanceOhm = 80.0 * Pi * Pi * std::pow(length/lambda, 2.0);
        out.resistanceOhm = out.radiationResistanceOhm;
        out.reactanceDefined = false;
        out.recommendedLengthM = 0.05 * lambda;
        out.validity = QStringLiteral("Uniform-current Hertzian element: radiation resistance only; feed reactance is not estimated.");
        break;
    case AntennaType::ShortCenterFedDipole:
        out.radiationResistanceOhm = 20.0 * Pi * Pi * std::pow(length/lambda, 2.0);
        out.resistanceOhm = out.radiationResistanceOhm;
        out.reactanceDefined = false;
        out.recommendedLengthM = 0.1 * lambda;
        out.validity = QStringLiteral("Center-fed electrically-short dipole with triangular-current approximation; valid for L << λ. Reactance depends strongly on wire geometry and is intentionally omitted.");
        break;
    case AntennaType::ThinHalfWaveDipole:
    {
        out.radiationResistanceOhm = 73.1;
        out.resistanceOhm = 73.1;
        out.reactanceOhm = 1700.0 * (out.electricalLengthLambda - 0.475);
        out.reactanceDefined = out.electricalLengthLambda >= 0.44 && out.electricalLengthLambda <= 0.52;
        out.recommendedLengthM = 0.475 * lambda;
        out.validity = out.reactanceDefined
            ? QStringLiteral("Thin half-wave dipole local engineering interpolation around resonance. At 0.5λ it reproduces approximately 73 + j42.5 Ω; use MoM/NEC for precision.")
            : QStringLiteral("Length is outside the local half-wave interpolation range (0.44…0.52 λ). Use a full-wave solver for impedance.");
        break;
    }
    case AntennaType::QuarterWaveMonopole:
    {
        out.radiationResistanceOhm = 36.55;
        out.resistanceOhm = 36.55;
        out.reactanceOhm = 850.0 * (out.electricalLengthLambda - 0.2375);
        out.reactanceDefined = out.electricalLengthLambda >= 0.22 && out.electricalLengthLambda <= 0.26;
        out.recommendedLengthM = 0.2375 * lambda;
        out.validity = out.reactanceDefined
            ? QStringLiteral("Quarter-wave monopole over an ideal infinite ground plane; local resonance approximation. Real radials/ground alter impedance.")
            : QStringLiteral("Length is outside the local quarter-wave interpolation range (0.22…0.26 λ).");
        break;
    }
    case AntennaType::FoldedHalfWaveDipole:
        out.radiationResistanceOhm = 292.0;
        out.resistanceOhm = 292.0;
        out.reactanceOhm = 4.0 * 1700.0 * (out.electricalLengthLambda - 0.475);
        out.reactanceDefined = out.electricalLengthLambda >= 0.44 && out.electricalLengthLambda <= 0.52;
        out.recommendedLengthM = 0.475 * lambda;
        out.validity = QStringLiteral("Equal-diameter two-wire folded dipole, approximately 4× a simple half-wave dipole. Spacing and conductor diameters affect the transformation ratio.");
        break;
    case AntennaType::SmallCircularLoop:
    {
        const double area = Pi * input.loopRadiusM * input.loopRadiusM;
        const double nA = std::max(1, input.turns) * area;
        out.radiationResistanceOhm = 31200.0 * std::pow(nA/(lambda*lambda), 2.0);
        out.resistanceOhm = out.radiationResistanceOhm;
        const double L = circularLoopInductance(positive(input.loopRadiusM, 1e-4), positive(input.wireRadiusM, 1e-6), std::max(1, input.turns));
        out.reactanceOhm = 2.0 * Pi * f * L;
        out.reactanceDefined = true;
        out.recommendedLengthM = lambda / (20.0 * Pi);
        out.validity = QStringLiteral("Small-loop approximation (circumference << λ). Reactance uses a thin circular-wire inductance estimate and excludes parasitic capacitance.");
        break;
    }
    }

    reflectionMetrics(out.resistanceOhm, out.reactanceDefined ? out.reactanceOhm : 0.0, input.feedLineOhm, out);
    return out;
}

SolenoidResult solenoid(const SolenoidInput &input)
{
    SolenoidResult out;
    const int n = std::max(1, input.turns);
    const double r = positive(input.radiusM, 1e-6);
    const double l = positive(input.lengthM, 1e-6);
    const double muR = positive(input.relativePermeability, 1.0);
    const double area = Pi * r * r;

    out.idealInductanceH = Mu0 * muR * double(n*n) * area / l;

    const double meterToInch = 39.37007874015748;
    const double rIn = r * meterToInch;
    const double lIn = l * meterToInch;
    const double radialBuildIn = std::max(input.radialBuildM, input.wireDiameterM) * meterToInch;
    double wheelerMicroH = 0.0;
    if (radialBuildIn <= 1.5 * positive(input.wireDiameterM, 1e-9) * meterToInch)
        wheelerMicroH = rIn*rIn*double(n*n) / (9.0*rIn + 10.0*lIn);
    else
        wheelerMicroH = 0.8*rIn*rIn*double(n*n) / (6.0*rIn + 9.0*lIn + 10.0*radialBuildIn);
    out.wheelerAirCoreInductanceH = wheelerMicroH * 1e-6;
    out.recommendedInductanceH = muR > 1.05 ? out.idealInductanceH : out.wheelerAirCoreInductanceH;

    const double meanRadius = r + std::max(0.0, input.radialBuildM) * 0.5;
    out.wireLengthM = 2.0 * Pi * meanRadius * n;
    const double wireDiameter = positive(input.wireDiameterM, 1e-6);
    const double wireArea = Pi * wireDiameter * wireDiameter / 4.0;
    out.wireResistanceOhm = positive(input.conductorResistivityOhmM, CopperResistivity20C) * out.wireLengthM / wireArea;
    out.centerFieldT = Mu0 * muR * n * input.currentA / (2.0 * std::sqrt(r*r + 0.25*l*l));
    out.storedEnergyJ = 0.5 * out.recommendedInductanceH * input.currentA * input.currentA;
    out.windingPitchM = l / n;
    out.note = muR > 1.05
        ? QStringLiteral("Core μr is applied to the ideal magnetic-circuit/long-solenoid inductance and center-field estimate. Real cores require effective permeability, air-gap, saturation and fringing data.")
        : QStringLiteral("Recommended L uses Wheeler's air-core formula; the ideal μ0N²A/l value is also shown for comparison.");
    return out;
}

double circularLoopInductance(double loopRadiusM, double wireRadiusM, int turns)
{
    const double r = positive(loopRadiusM, 1e-6);
    const double a = std::min(positive(wireRadiusM, 1e-9), 0.3*r);
    const double single = Mu0 * r * (std::log(8.0*r/a) - 2.0);
    return std::max(0.0, single) * double(std::max(1, turns) * std::max(1, turns));
}

CapacitanceResult capacitance(const CapacitanceInput &input)
{
    CapacitanceResult out;
    const double eps = Epsilon0 * positive(input.epsilonR, 1.0);
    switch (input.geometry)
    {
    case CapacitanceGeometry::ParallelPlates:
        if (input.areaM2 > 0.0 && input.separationM > 0.0)
        {
            out.capacitanceF = eps * input.areaM2 / input.separationM;
            out.valid = true;
            out.formula = QStringLiteral("C = ε A / d");
            out.note = QStringLiteral("Parallel-plate approximation; edge/fringing fields are neglected.");
        }
        break;
    case CapacitanceGeometry::IsolatedSphere:
        if (input.innerRadiusM > 0.0)
        {
            out.capacitanceF = 4.0 * Pi * eps * input.innerRadiusM;
            out.valid = true;
            out.formula = QStringLiteral("C = 4π ε a");
            out.note = QStringLiteral("Isolated conducting sphere referenced to infinity.");
        }
        break;
    case CapacitanceGeometry::ConcentricSpheres:
        if (input.innerRadiusM > 0.0 && input.outerRadiusM > input.innerRadiusM)
        {
            out.capacitanceF = 4.0 * Pi * eps * input.innerRadiusM * input.outerRadiusM
                             / (input.outerRadiusM - input.innerRadiusM);
            out.valid = true;
            out.formula = QStringLiteral("C = 4π ε ab / (b-a)");
            out.note = QStringLiteral("Concentric spherical conductors.");
        }
        break;
    case CapacitanceGeometry::CoaxialCylinders:
        if (input.innerRadiusM > 0.0 && input.outerRadiusM > input.innerRadiusM && input.lengthM > 0.0)
        {
            out.capacitancePerMeterFpm = 2.0 * Pi * eps / std::log(input.outerRadiusM/input.innerRadiusM);
            out.capacitanceF = out.capacitancePerMeterFpm * input.lengthM;
            out.valid = true;
            out.formula = QStringLiteral("C' = 2π ε / ln(b/a)");
            out.note = QStringLiteral("Long coaxial cylinders; end effects neglected.");
        }
        break;
    case CapacitanceGeometry::TwoWireLine:
        if (input.wireRadiusM > 0.0 && input.wireCenterSpacingM > 2.0*input.wireRadiusM && input.lengthM > 0.0)
        {
            out.capacitancePerMeterFpm = Pi * eps / std::acosh(input.wireCenterSpacingM/(2.0*input.wireRadiusM));
            out.capacitanceF = out.capacitancePerMeterFpm * input.lengthM;
            out.valid = true;
            out.formula = QStringLiteral("C' = π ε / acosh(D / 2a)");
            out.note = QStringLiteral("Two identical parallel round wires in a homogeneous dielectric; length is assumed large compared with spacing.");
        }
        break;
    }
    if (!out.valid)
    {
        out.note = QStringLiteral("Invalid geometry: check positive dimensions and required radius/spacing ordering.");
        return out;
    }

    // Homogeneous lossy dielectric small-signal model. With the e^{jωt}
    // convention, ε*=ε'(1-j tanδ) gives Y = ω C tanδ + jωC.
    const double omega = 2.0 * Pi * positive(input.frequencyHz, 1.0);
    const double tanD = std::max(0.0, input.lossTangent);
    out.dielectricConductanceS = omega * out.capacitanceF * tanD;
    out.admittanceS = {out.dielectricConductanceS, omega * out.capacitanceF};
    if (std::abs(out.admittanceS) > 1e-30)
        out.impedanceOhm = 1.0 / out.admittanceS;
    out.dielectricQualityFactor = tanD > 0.0 ? 1.0 / tanD : std::numeric_limits<double>::infinity();
    return out;
}

CoupledCoilsResult coupledCoils(const CoupledCoilsInput &input)
{
    CoupledCoilsResult out;
    double l1 = positive(input.primaryInductanceH, 1e-12);
    double l2 = positive(input.secondaryInductanceH, 1e-12);
    const double k = std::clamp(input.couplingCoefficient, 0.0, 1.0);
    const double f = positive(input.frequencyHz, 1.0);
    const double omega = 2.0 * Pi * f;

    MagneticMaterialResult coreMu;
    if (input.useSharedCore)
    {
        const double ae = positive(input.coreEffectiveAreaM2, 1e-12);
        const double le = positive(input.corePathLengthM, 1e-9);
        double mur = positive(input.coreRelativePermeability, 1.0);
        if (input.useFrequencyDependentCore)
        {
            auto material = input.coreMaterial;
            material.enabled = true;
            if (material.lowFrequencyRelativePermeability <= 1.0)
                material.lowFrequencyRelativePermeability = mur;
            coreMu = magneticMaterial(material, f);
            mur = std::max(1.0, coreMu.muPrime);
            out.coreMuPrime = coreMu.muPrime;
            out.coreMuDoublePrime = coreMu.muDoublePrime;
            out.coreLossTangent = coreMu.lossTangent;
        }
        else
        {
            out.coreMuPrime = mur;
        }
        const double gap = std::max(0.0, input.airGapM);
        out.magneticReluctanceAtPerH = le / (Mu0 * mur * ae) + gap / (Mu0 * ae);
        out.alValueHPerTurn2 = 1.0 / std::max(out.magneticReluctanceAtPerH, 1e-30);
        const double n1 = std::max(1, input.primaryTurns);
        const double n2 = std::max(1, input.secondaryTurns);
        l1 = n1*n1*out.alValueHPerTurn2;
        l2 = n2*n2*out.alValueHPerTurn2;
        const double bsat = positive(input.saturationFluxDensityT, 0.3);
        out.estimatedPrimarySaturationCurrentA = bsat * out.magneticReluctanceAtPerH * ae / n1;
        if (input.useFrequencyDependentCore)
        {
            out.primaryCoreLossSeriesResistanceOhm = omega*l1*out.coreLossTangent;
            out.secondaryCoreLossSeriesResistanceOhm = omega*l2*out.coreLossTangent;
        }
    }

    double r1 = std::max(0.0,input.primaryResistanceOhm);
    double r2 = std::max(0.0,input.secondaryResistanceOhm);
    if (input.useWindingAcLoss)
    {
        WindingLossInput wi;
        wi.frequencyHz=f;wi.dcResistanceOhm=r1;wi.wireDiameterM=input.primaryWireDiameterM;
        wi.proximityExtraAtReference=input.windingProximityExtraAtReference;
        wi.proximityReferenceFrequencyHz=input.windingProximityReferenceFrequencyHz;
        wi.proximityExponent=input.windingProximityExponent;
        wi.useDowellMultilayerEstimate=input.useDowellMultilayerEstimate;
        wi.layerCount=input.primaryWindingLayers;
        r1=windingLoss(wi).acResistanceOhm;
        wi.dcResistanceOhm=r2;wi.wireDiameterM=input.secondaryWireDiameterM;wi.layerCount=input.secondaryWindingLayers;
        r2=windingLoss(wi).acResistanceOhm;
    }
    out.primaryAcResistanceOhm = r1;
    out.secondaryAcResistanceOhm = r2;
    if(input.useOperatingPointCoreLoss)
    {
        auto clIn=input.operatingPointCoreLoss;clIn.frequencyHz=f;
        const auto cl=coreLoss(clIn);
        if(cl.valid){out.operatingPointCoreLossW=cl.totalCoreLossW;out.operatingPointEquivalentPrimaryResistanceOhm=cl.equivalentSeriesResistanceOhm;}
    }
    const double r1eff=r1+out.primaryCoreLossSeriesResistanceOhm+out.operatingPointEquivalentPrimaryResistanceOhm;
    const double r2eff=r2+out.secondaryCoreLossSeriesResistanceOhm;

    out.effectivePrimaryInductanceH = l1;
    out.effectiveSecondaryInductanceH = l2;
    out.mutualInductanceH = k * std::sqrt(l1*l2);
    out.turnsRatioN2OverN1 = input.primaryTurns > 0 ? double(input.secondaryTurns)/double(input.primaryTurns) : 0.0;
    out.primaryLeakageH = l1 * (1.0 - k*k);
    out.secondaryLeakageH = l2 * (1.0 - k*k);

    // Equal winding interpretation retained: L+M (common mode), L-M (differential mode).
    const double disc = std::sqrt((l1-l2)*(l1-l2) + 4.0*out.mutualInductanceH*out.mutualInductanceH);
    out.modalHighInductanceH = 0.5*(l1+l2+disc);
    out.modalLowInductanceH  = 0.5*(l1+l2-disc);

    const std::complex<double> j(0.0, 1.0);
    const double rMode = 0.5*(r1eff+r2eff);
    out.commonModeImpedanceOhm = rMode + j*omega*out.modalHighInductanceH;
    out.differentialModeImpedanceOhm = rMode + j*omega*out.modalLowInductanceH;
    const std::complex<double> secondaryDen = input.loadOhm + r2eff + j*omega*l2;
    if (std::abs(secondaryDen) > 1e-18)
    {
        out.reflectedImpedanceOhm = (omega*omega*out.mutualInductanceH*out.mutualInductanceH) / secondaryDen;
        out.secondaryCurrentPerPrimaryAperA = -j*omega*out.mutualInductanceH / secondaryDen;
    }
    out.inputImpedanceOhm = r1eff + j*omega*l1 + out.reflectedImpedanceOhm;

    if (input.application == CoupledWindingApplication::CommonModeChoke)
        out.note = input.useSharedCore
            ? QStringLiteral("Common-mode choke interpretation with shared-core magnetic circuit. Optional complex-permeability loss adds Rcore≈ωL tanδμ; the winding model can include skin/proximity and a Dowell-like multilayer estimate; an optional Steinmetz/datasheet operating point adds amplitude-dependent core heating as an equivalent primary resistance. Equal windings reduce to LCM=L+M and LDM=L−M. Ferrite presets remain educational; use manufacturer μ′/μ″ and impedance curves for precision work.")
            : QStringLiteral("Common-mode choke interpretation using the entered L1/L2 and k. Optional winding AC loss is available; a physical magnetic core is normally required for useful common-mode impedance.");
    else
        out.note = input.useSharedCore
            ? QStringLiteral("Shared-core transformer/coupled-inductor model with optional frequency-dependent complex permeability, skin/proximity/Dowell-like winding loss and amplitude-dependent Steinmetz or manufacturer core-loss operating point. The single-pole μ model is an engineering approximation; fringing, nonlinear hysteresis and distributed winding capacitance remain outside this lumped model.")
            : QStringLiteral("Linear two-winding coupled-inductor / transformer model using entered L1/L2 and k, with optional first-order winding AC resistance. Saturation/core loss require the shared-core mode.");
    return out;
}

BalunResult balun(const BalunInput &input)
{
    BalunResult out;
    const double f = positive(input.frequencyHz, 1.0);
    const double omega = 2.0*Pi*f;
    const double z0 = positive(input.sourceResistanceOhm, 1e-6);
    double n = positive(input.secondaryToPrimaryTurnsRatio, 1e-9);
    switch(input.topology)
    {
    case BalunTopology::CurrentBalun1to1: n=1.0; break;
    case BalunTopology::Guanella4to1: n=2.0; break;
    case BalunTopology::Ruthroff4to1: n=2.0; break;
    case BalunTopology::TransformerBalunOrUnun: break;
    }
    out.voltageRatioSecondaryOverPrimary = n;
    out.impedanceRatioSecondaryOverPrimary = n*n;
    out.idealReferredLoadOhm = input.loadOhm / (n*n);

    double lm = positive(input.magnetizingInductanceH,1e-12);
    double coreLossR=0.0;
    if(input.useFrequencyDependentCore)
    {
        auto material=input.coreMaterial;material.enabled=true;
        const auto atF=magneticMaterial(material,f);
        const auto atRef=magneticMaterial(material,positive(material.referenceFrequencyHz,f));
        const double scale=atRef.muPrime>1e-18?atF.muPrime/atRef.muPrime:1.0;
        lm*=std::max(1e-6,scale);
        out.coreMuPrime=atF.muPrime;out.coreMuDoublePrime=atF.muDoublePrime;out.coreLossTangent=atF.lossTangent;
        coreLossR=omega*lm*atF.lossTangent;
    }
    out.effectiveMagnetizingInductanceH=lm;
    out.coreLossSeriesResistanceOhm=coreLossR;
    if(input.useOperatingPointCoreLoss)
    {
        auto clIn=input.operatingPointCoreLoss;clIn.frequencyHz=f;
        const auto cl=coreLoss(clIn);
        if(cl.valid){out.operatingPointCoreLossW=cl.totalCoreLossW;out.operatingPointEquivalentSeriesResistanceOhm=cl.equivalentSeriesResistanceOhm;coreLossR += cl.equivalentSeriesResistanceOhm;out.coreLossSeriesResistanceOhm=coreLossR;}
    }

    const double ll = std::max(0.0,input.leakageInductanceH);
    double rw = std::max(0.0,input.windingResistanceOhm);
    if(input.useWindingAcLoss)
    {
        WindingLossInput wi;wi.frequencyHz=f;wi.dcResistanceOhm=rw;wi.wireDiameterM=input.wireDiameterM;
        wi.proximityExtraAtReference=input.windingProximityExtraAtReference;
        wi.proximityReferenceFrequencyHz=input.windingProximityReferenceFrequencyHz;
        wi.proximityExponent=input.windingProximityExponent;
        wi.useDowellMultilayerEstimate=input.useDowellMultilayerEstimate;wi.layerCount=input.windingLayers;
        rw=windingLoss(wi).acResistanceOhm;
    }
    out.windingAcResistanceOhm=rw;
    const double cp = std::max(0.0,input.parasiticCapacitanceF);
    const std::complex<double> j(0.0,1.0);
    std::complex<double> y = std::abs(out.idealReferredLoadOhm)>1e-30 ? 1.0/out.idealReferredLoadOhm : std::complex<double>{0.0,0.0};
    const std::complex<double> zMag=coreLossR+j*omega*lm;
    if(std::abs(zMag)>1e-30)y += 1.0/zMag;
    y += j*omega*cp;
    const std::complex<double> zShunt = std::abs(y)>1e-30 ? 1.0/y : std::complex<double>{1e30,0.0};
    out.inputImpedanceOhm = rw + j*omega*ll + zShunt;
    out.reflectionCoefficient = (out.inputImpedanceOhm-z0)/(out.inputImpedanceOhm+z0);
    const double gm = std::min(0.999999999999,std::abs(out.reflectionCoefficient));
    out.returnLossDb = -20.0*std::log10(std::max(1e-15,gm));
    out.vswr = (1.0+gm)/(1.0-gm);
    out.commonModeChokingImpedanceOhm = rw + coreLossR + j*omega*lm;
    out.approximateSelfResonanceHz = cp>0.0 ? 1.0/(2.0*Pi*std::sqrt(lm*cp)) : 0.0;
    switch(input.topology)
    {
    case BalunTopology::CurrentBalun1to1:
        out.note = QStringLiteral("1:1 current-balun lumped equivalent with optional frequency-dependent ferrite permeability/loss and first-order winding skin/proximity resistance. Distributed transmission-line symmetry remains outside this model."); break;
    case BalunTopology::Guanella4to1:
        out.note = QStringLiteral("4:1 Guanella ideal impedance ratio plus lumped magnetizing/leakage/parasitic model. Optional complex-mu and winding-loss terms improve the core/choke estimate, but real Guanella bandwidth still depends on transmission-line Z0 and electrical length."); break;
    case BalunTopology::Ruthroff4to1:
        out.note = QStringLiteral("4:1 Ruthroff ideal ratio plus a first-order lossy transformer equivalent. Optional ferrite/winding loss is included, while distributed transmission-line behavior remains an approximation."); break;
    case BalunTopology::TransformerBalunOrUnun:
        out.note = QStringLiteral("Arbitrary transformer balun/unun equivalent with optional complex-permeability and winding AC-loss models. n=N2/N1 gives Zsecondary/Zprimary=n²; distributed capacitance and leakage geometry remain lumped."); break;
    }
    return out;
}

InductiveAntennaResult inductiveAntenna(const InductiveAntennaInput &input)
{
    InductiveAntennaResult out;
    const double f=positive(input.frequencyHz,1.0);
    const double omega=2.0*Pi*f;
    const int n=std::max(1,input.turns);
    const double rho=positive(input.conductorResistivityOhmM,CopperResistivity20C);
    out.wavelengthM=C0/f;
    out.reactiveNearFieldScaleM=out.wavelengthM/(2.0*Pi);
    out.skinDepthM=std::sqrt(2.0*rho/(omega*Mu0));

    double effectiveRadius=positive(input.radiusM,1e-5);
    double areaTurns=0.0;
    double conductorAreaDc=0.0;
    double conductorAreaAc=0.0;

    if(input.geometry==InductiveAntennaGeometry::PlanarCircularSpiral)
    {
        const double din=positive(input.innerDiameterM,1e-5);
        const double w=positive(input.traceWidthM,1e-6);
        const double sp=std::max(0.0,input.traceSpacingM);
        const double t=positive(input.copperThicknessM,1e-7);
        out.outerDiameterM=din+2.0*n*w+2.0*std::max(0,n-1)*sp;
        const double davg=0.5*(out.outerDiameterM+din);
        const double fill=std::clamp((out.outerDiameterM-din)/(out.outerDiameterM+din),1e-6,0.999999);
        // Mohan current-sheet approximation for a circular planar spiral.
        out.inductanceH=Mu0*double(n*n)*davg*0.5*(std::log(2.46/fill)+0.20*fill*fill);
        out.wireLengthM=Pi*n*davg;
        effectiveRadius=0.25*(out.outerDiameterM+din);
        areaTurns=n*Pi*effectiveRadius*effectiveRadius;
        conductorAreaDc=w*t;
        const double tEff=std::min(t,2.0*out.skinDepthM);
        conductorAreaAc=w*std::max(tEff,1e-12);
    }
    else if(input.geometry==InductiveAntennaGeometry::SolenoidalCoil)
    {
        SolenoidInput si;
        si.turns=n;si.radiusM=effectiveRadius;si.lengthM=positive(input.solenoidLengthM,1e-6);
        si.wireDiameterM=positive(input.wireDiameterM,1e-6);si.radialBuildM=si.wireDiameterM;
        si.currentA=input.currentA;si.relativePermeability=1.0;si.conductorResistivityOhmM=rho;
        const auto sr=solenoid(si);
        out.inductanceH=sr.recommendedInductanceH;
        out.wireLengthM=sr.wireLengthM;
        out.outerDiameterM=2.0*effectiveRadius+input.wireDiameterM;
        areaTurns=n*Pi*effectiveRadius*effectiveRadius;
        const double a=0.5*positive(input.wireDiameterM,1e-6);
        conductorAreaDc=Pi*a*a;
        conductorAreaAc=out.skinDepthM<a ? 2.0*Pi*a*out.skinDepthM : conductorAreaDc;
        const double z=input.axialObservationM;
        const double halfL=0.5*si.lengthM;
        const double density=double(n)/si.lengthM;
        out.axialFieldT=0.5*Mu0*density*input.currentA*((z+halfL)/std::sqrt(effectiveRadius*effectiveRadius+(z+halfL)*(z+halfL))-(z-halfL)/std::sqrt(effectiveRadius*effectiveRadius+(z-halfL)*(z-halfL)));
    }
    else
    {
        const double wireR=0.5*positive(input.wireDiameterM,1e-6);
        out.outerDiameterM=2.0*(effectiveRadius+wireR);
        out.inductanceH=circularLoopInductance(effectiveRadius,wireR,n);
        out.wireLengthM=2.0*Pi*effectiveRadius*n;
        areaTurns=n*Pi*effectiveRadius*effectiveRadius;
        conductorAreaDc=Pi*wireR*wireR;
        conductorAreaAc=out.skinDepthM<wireR ? 2.0*Pi*wireR*out.skinDepthM : conductorAreaDc;
        const double z=input.axialObservationM;
        out.axialFieldT=Mu0*n*input.currentA*effectiveRadius*effectiveRadius/(2.0*std::pow(effectiveRadius*effectiveRadius+z*z,1.5));
    }

    if(input.geometry==InductiveAntennaGeometry::PlanarCircularSpiral)
    {
        const double z=input.axialObservationM;
        out.axialFieldT=Mu0*n*input.currentA*effectiveRadius*effectiveRadius/(2.0*std::pow(effectiveRadius*effectiveRadius+z*z,1.5));
    }

    out.effectiveAreaM2Turns=areaTurns;
    out.magneticMomentAm2=areaTurns*input.currentA;
    out.circumferenceLambda=2.0*Pi*effectiveRadius/out.wavelengthM;
    out.radiationResistanceOhm=31200.0*std::pow(areaTurns/(out.wavelengthM*out.wavelengthM),2.0);
    out.dcResistanceOhm=rho*out.wireLengthM/std::max(conductorAreaDc,1e-18);
    out.acResistanceOhm=rho*out.wireLengthM/std::max(conductorAreaAc,1e-18);
    const double rSeries=out.acResistanceOhm+out.radiationResistanceOhm;
    const std::complex<double> j(0.0,1.0);
    const std::complex<double> zSeries=rSeries+j*omega*out.inductanceH;
    if(input.parasiticCapacitanceF>0.0)
    {
        const std::complex<double> y=1.0/zSeries+j*omega*input.parasiticCapacitanceF;
        out.inputImpedanceOhm=std::abs(y)>1e-30?1.0/y:zSeries;
        out.selfResonanceHz=1.0/(2.0*Pi*std::sqrt(std::max(1e-30,out.inductanceH*input.parasiticCapacitanceF)));
    }
    else out.inputImpedanceOhm=zSeries;
    out.unloadedQ=rSeries>0.0?omega*out.inductanceH/rSeries:0.0;
    const bool electricallySmall=out.circumferenceLambda<0.1;
    out.note=electricallySmall
        ? QStringLiteral("Electrically-small magnetic-loop / inductive-antenna model. Radiation resistance uses the small-loop approximation; input impedance adds conductor skin loss, inductance and optional lumped parasitic capacitance. Axial B is quasi-static and most useful inside roughly λ/(2π).")
        : QStringLiteral("The coil circumference is not << λ. Small-loop radiation resistance and quasi-static axial-field assumptions are becoming inaccurate; use the thin-wire full-wave antenna solver for quantitative radiation results.");
    return out;
}

TransmissionLineResult transmissionLine(const TransmissionLineInput &input)
{
    TransmissionLineResult out;
    const double f=positive(input.frequencyHz,1.0);
    const double w=positive(input.traceWidthM,1e-8);
    const double t=std::max(0.0,input.copperThicknessM);
    const double h=positive(input.dielectricHeightM,1e-8);
    const double er=positive(input.epsilonR,1.0);
    const double rho=positive(input.conductorResistivityOhmM,CopperResistivity20C);
    double weff=w;
    if(t>1e-12 && w/t>0.5)
        weff=w+(t/Pi)*(1.0+std::log(std::max(1.000001,4.0*Pi*w/t)));
    out.effectiveWidthM=weff;

    if(input.geometry==TransmissionLineGeometry::Microstrip)
    {
        const double u=weff/h;
        out.effectivePermittivity=(er+1.0)/2.0+(er-1.0)/2.0*(1.0/std::sqrt(1.0+12.0/u)+(u<1.0?0.04*std::pow(1.0-u,2.0):0.0));
        if(u<=1.0)
            out.characteristicImpedanceOhm=(60.0/std::sqrt(out.effectivePermittivity))*std::log(8.0/u+0.25*u);
        else
            out.characteristicImpedanceOhm=(120.0*Pi)/(std::sqrt(out.effectivePermittivity)*(u+1.393+0.667*std::log(u+1.444)));
        out.note=QStringLiteral("Hammerstad-style quasi-static microstrip impedance with a simple thickness correction. Frequency dependence here comes from phase/line length, dielectric loss and copper skin effect; full dispersion and roughness require a field solver or manufacturer stack-up model.");
    }
    else
    {
        out.effectivePermittivity=er;
        const double arg=4.0*h/(Pi*std::max(1e-12,0.8*weff+t));
        if(arg<=1.0){out.note=QStringLiteral("Invalid/very wide stripline geometry for the closed-form approximation.");return out;}
        out.characteristicImpedanceOhm=(60.0/std::sqrt(er))*std::log(arg);
        out.note=QStringLiteral("Symmetric-stripline closed-form engineering approximation. For thick copper, solder mask, asymmetric layers or high frequencies, use a 2D/3D field solver.");
    }

    if(!std::isfinite(out.characteristicImpedanceOhm) || out.characteristicImpedanceOhm<=0.0)return out;
    out.phaseVelocityMps=C0/std::sqrt(out.effectivePermittivity);
    out.guidedWavelengthM=out.phaseVelocityMps/f;
    out.phaseConstantRadPerM=2.0*Pi/out.guidedWavelengthM;
    out.electricalLengthDeg=360.0*input.lineLengthM/out.guidedWavelengthM;
    out.skinDepthM=std::sqrt(2.0*rho/(2.0*Pi*f*Mu0));
    out.surfaceResistanceOhm=std::sqrt(Pi*f*Mu0*rho);
    out.conductorLossDbPerM=8.686*out.surfaceResistanceOhm/(std::max(1e-12,out.characteristicImpedanceOhm*weff));
    out.dielectricLossDbPerM=8.686*0.5*out.phaseConstantRadPerM*std::max(0.0,input.lossTangent);
    const double alphaNp=(out.conductorLossDbPerM+out.dielectricLossDbPerM)/8.686;
    out.totalLossDb=(out.conductorLossDbPerM+out.dielectricLossDbPerM)*std::max(0.0,input.lineLengthM);
    const std::complex<double> gamma(alphaNp,out.phaseConstantRadPerM);
    const std::complex<double> th=std::tanh(gamma*std::max(0.0,input.lineLengthM));
    const std::complex<double> z0(out.characteristicImpedanceOhm,0.0);
    const auto zl=input.loadOhm;
    const auto den=z0+zl*th;
    out.inputImpedanceOhm=std::abs(den)>1e-30?z0*(zl+z0*th)/den:z0;
    out.valid=true;
    return out;
}

FilterDesignResult designFilter(const FilterDesignInput &input)
{
    FilterDesignResult out;
    const double targetFrequency=positive(input.centerFrequencyHz,1.0);
    const double q=positive(input.qualityFactor,0.707);
    double naturalFrequency=targetFrequency;
    // For low/high-pass pages the user specifies the desired -3 dB cutoff.
    // Convert it to the denominator natural frequency for the requested Q.
    if(input.topology==FilterTopology::LowPassRlc)
    {
        const double a=-2.0+1.0/(q*q);
        const double y=(-a+std::sqrt(a*a+4.0))/2.0;
        const double x=std::sqrt(std::max(y,1e-18));
        naturalFrequency=targetFrequency/x;
    }
    else if(input.topology==FilterTopology::HighPassRlc)
    {
        const double b=2.0-1.0/(q*q);
        const double y=(-b+std::sqrt(b*b+4.0))/2.0;
        const double x=std::sqrt(std::max(y,1e-18));
        naturalFrequency=targetFrequency/x;
    }
    const double w0=2.0*Pi*naturalFrequency;
    const double r=positive(input.referenceResistanceOhm,50.0);
    if(input.topology==FilterTopology::ShuntSeriesLcTrap)
    {
        out.inductanceH=positive(input.preferredInductanceH,100e-9);
        out.capacitanceF=1.0/(w0*w0*out.inductanceH);
        out.dampingOrLossResistanceOhm=w0*out.inductanceH/q;
        out.topologyDescription=QStringLiteral("Series-LC resonator shunted across the receive line");
        out.note=QStringLiteral("Q sets the equivalent series loss of the trap resonator. Notch depth also depends on source/load impedances; real inductor SRF and capacitor ESR/ESL can dominate at RF.");
    }
    else
    {
        out.inductanceH=q*r/w0;
        out.capacitanceF=1.0/(w0*q*r);
        out.dampingOrLossResistanceOhm=r;
        switch(input.topology){
        case FilterTopology::LowPassRlc: out.topologyDescription=QStringLiteral("Canonical 2nd-order series-RL / shunt-C low-pass");break;
        case FilterTopology::HighPassRlc: out.topologyDescription=QStringLiteral("Canonical 2nd-order series-RC / shunt-L high-pass");break;
        case FilterTopology::BandPassSeriesRlc: out.topologyDescription=QStringLiteral("Series-RLC band-pass, output across R");break;
        case FilterTopology::NotchSeriesRlc: out.topologyDescription=QStringLiteral("Series-RLC notch prototype, output across L+C");break;
        case FilterTopology::ShuntSeriesLcTrap: break;
        }
        out.note=(input.topology==FilterTopology::LowPassRlc || input.topology==FilterTopology::HighPassRlc)
            ? QStringLiteral("For low/high-pass synthesis the requested frequency is treated as the -3 dB cutoff; the internal RLC natural frequency is adjusted for the selected Q. This remains a canonical second-order prototype and excludes component/layout parasitics.")
            : QStringLiteral("Canonical RLC synthesis using f0, Q and the reference/damping resistance. It is intended for design intuition and first-pass component values, not as a substitute for a complete RF matching/network synthesis including source/load parasitics.");
    }
    out.resonantFrequencyHz=1.0/(2.0*Pi*std::sqrt(out.inductanceH*out.capacitanceF));
    out.nominalBandwidthHz=out.resonantFrequencyHz/q;
    out.valid=std::isfinite(out.inductanceH)&&std::isfinite(out.capacitanceF)&&out.inductanceH>0.0&&out.capacitanceF>0.0;
    return out;
}

std::complex<double> filterTransfer(const FilterDesignInput &input,double frequencyHz,FilterDesignResult *design)
{
    const auto d=designFilter(input); if(design)*design=d;if(!d.valid)return {0.0,0.0};
    const double w=2.0*Pi*positive(frequencyHz,1.0);const std::complex<double> j(0.0,1.0);
    const auto zl=j*w*d.inductanceH;const auto zc=1.0/(j*w*d.capacitanceF);
    const double r=positive(input.referenceResistanceOhm,50.0);
    switch(input.topology)
    {
    case FilterTopology::LowPassRlc: return zc/(r+zl+zc);
    case FilterTopology::HighPassRlc: return zl/(r+zl+zc);
    case FilterTopology::BandPassSeriesRlc: return std::complex<double>(r,0.0)/(r+zl+zc);
    case FilterTopology::NotchSeriesRlc: return (zl+zc)/(r+zl+zc);
    case FilterTopology::ShuntSeriesLcTrap:
    {
        const std::complex<double> zbranch=d.dampingOrLossResistanceOhm+zl+zc;
        const std::complex<double> zload(positive(input.loadResistanceOhm,50.0),0.0);
        const double rs=positive(input.sourceResistanceOhm,50.0);
        const std::complex<double> zp=zload*zbranch/(zload+zbranch);
        const std::complex<double> h=zp/(rs+zp);
        const double baseline=positive(input.loadResistanceOhm,50.0)/(rs+positive(input.loadResistanceOhm,50.0));
        return baseline>0.0?h/baseline:h;
    }
    }
    return {0.0,0.0};
}


ReactiveComponentResult reactiveComponent(ReactiveComponentType type,double value,double frequencyHz,const ReactiveComponentParasitics &parasitics)
{
    ReactiveComponentResult out;
    const double f=positive(frequencyHz,1.0);
    const double w=2.0*Pi*f;
    const std::complex<double> j(0.0,1.0);
    value=std::max(0.0,value);

    if(type==ReactiveComponentType::None || value<=0.0)
    {
        out.impedanceOhm={0.0,0.0};
        out.note=QStringLiteral("No reactive component selected.");
        return out;
    }

    if(parasitics.enabled && parasitics.useDatasheetCurve && parasitics.datasheetCurve.size()>=2)
    {
        std::vector<ReactiveDatasheetPoint> pts=parasitics.datasheetCurve;
        std::sort(pts.begin(),pts.end(),[](const auto&a,const auto&b){return a.frequencyHz<b.frequencyHz;});
        auto zOf=[](const ReactiveDatasheetPoint&p){return std::complex<double>(p.resistanceOhm,p.reactanceOhm);};
        if(f<=pts.front().frequencyHz)out.impedanceOhm=zOf(pts.front());
        else if(f>=pts.back().frequencyHz)out.impedanceOhm=zOf(pts.back());
        else
        {
            auto it=std::lower_bound(pts.begin(),pts.end(),f,[](const auto&p,double x){return p.frequencyHz<x;});
            const auto &b=*it,&a=*(it-1);
            const double la=std::log(std::max(a.frequencyHz,1e-30)),lb=std::log(std::max(b.frequencyHz,1e-30));
            const double u=(std::log(f)-la)/std::max(lb-la,1e-30);
            out.impedanceOhm=zOf(a)+(zOf(b)-zOf(a))*u;
        }
        out.usedDatasheetCurve=true;
        out.effectiveSeriesResistanceOhm=std::max(0.0,out.impedanceOhm.real());
        const double re=std::abs(out.impedanceOhm.real());
        out.effectiveQualityFactor=re>1e-18?std::abs(out.impedanceOhm.imag())/re:std::numeric_limits<double>::infinity();
        out.note=QStringLiteral("Datasheet/measured complex-Z interpolation in log frequency. Outside the supplied frequency range the nearest endpoint is held; do not treat that as physical extrapolation.");
        return out;
    }

    if(!parasitics.enabled)
    {
        out.impedanceOhm=(type==ReactiveComponentType::Inductor)?j*w*value:1.0/(j*w*value);
        out.effectiveQualityFactor=std::numeric_limits<double>::infinity();
        out.effectiveInductanceH=type==ReactiveComponentType::Inductor?value:0.0;
        out.note=QStringLiteral("Ideal reactive component.");
        return out;
    }

    const double fref=positive(parasitics.referenceFrequencyHz,f);
    double rseries=std::max(0.0,parasitics.seriesResistanceOhm);
    if(type==ReactiveComponentType::Inductor && parasitics.usePhysicalWindingLoss)
    {
        WindingLossInput wi;wi.frequencyHz=f;wi.dcResistanceOhm=rseries;wi.wireDiameterM=parasitics.wireDiameterM;
        wi.conductorResistivityOhmM=parasitics.conductorResistivityOhmM;
        wi.proximityExtraAtReference=parasitics.proximityExtraAtReference;
        wi.proximityReferenceFrequencyHz=parasitics.proximityReferenceFrequencyHz;
        wi.proximityExponent=parasitics.proximityExponent;
        wi.useDowellMultilayerEstimate=parasitics.useDowellMultilayerEstimate;
        wi.layerCount=parasitics.windingLayers;
        const auto wr=windingLoss(wi);rseries=wr.acResistanceOhm;out.skinDepthM=wr.skinDepthM;out.skinEffectFactor=wr.skinEffectFactor;out.proximityEffectFactor=wr.proximityEffectFactor;out.dowellMultilayerFactor=wr.dowellMultilayerFactor;
    }
    if(parasitics.qualityFactorAtReference>0.0)
    {
        const double xref=(type==ReactiveComponentType::Inductor)
            ? 2.0*Pi*fref*value
            : 1.0/(2.0*Pi*fref*value);
        const double rq=std::abs(xref)/positive(parasitics.qualityFactorAtReference,1e12);
        const double exponent=std::clamp(parasitics.resistanceFrequencyExponent,0.0,2.0);
        rseries += rq*std::pow(std::max(f/fref,1e-12),exponent);
    }

    double effectiveL=value;
    double coreSeriesLoss=0.0;
    if(type==ReactiveComponentType::Inductor && parasitics.useMagneticMaterialModel)
    {
        auto mi=parasitics.magneticMaterial;mi.enabled=true;
        const auto mf=magneticMaterial(mi,f);
        const auto mr=magneticMaterial(mi,positive(mi.referenceFrequencyHz,fref));
        const double scale=mr.muPrime>1e-18?mf.muPrime/mr.muPrime:1.0;
        effectiveL=value*std::max(1e-6,scale);
        out.magneticMuPrime=mf.muPrime;out.magneticMuDoublePrime=mf.muDoublePrime;out.magneticLossTangent=mf.lossTangent;
        coreSeriesLoss=w*effectiveL*mf.lossTangent;
    }
    out.effectiveInductanceH=type==ReactiveComponentType::Inductor?effectiveL:0.0;
    rseries += coreSeriesLoss;
    if(type==ReactiveComponentType::Inductor && parasitics.useOperatingPointCoreLoss)
    {
        auto ci=parasitics.operatingPointCoreLoss;ci.frequencyHz=f;
        const auto cl=coreLoss(ci);
        if(cl.valid){out.operatingPointCoreLossW=cl.totalCoreLossW;rseries += cl.equivalentSeriesResistanceOhm;}
    }
    out.effectiveSeriesResistanceOhm=rseries;

    if(type==ReactiveComponentType::Inductor)
    {
        std::complex<double> z=rseries+j*w*effectiveL;
        std::complex<double> y=std::abs(z)>1e-30?1.0/z:std::complex<double>(1e30,0.0);
        if(parasitics.parasiticCapacitanceF>0.0) y += j*w*parasitics.parasiticCapacitanceF;
        if(parasitics.parallelResistanceOhm>0.0) y += 1.0/parasitics.parallelResistanceOhm;
        out.impedanceOhm=std::abs(y)>1e-30?1.0/y:std::complex<double>(1e30,0.0);
        if(parasitics.parasiticCapacitanceF>0.0)
            out.approximateSelfResonanceHz=1.0/(2.0*Pi*std::sqrt(std::max(effectiveL,1e-30)*parasitics.parasiticCapacitanceF));
        out.note=parasitics.useMagneticMaterialModel||parasitics.usePhysicalWindingLoss
            ? QStringLiteral("Non-ideal inductor with optional round-wire skin/proximity loss, Dowell-like multilayer loss, single-pole complex-permeability core model, amplitude-dependent Steinmetz/datasheet core-loss operating point, Q-derived loss, self-capacitance and parallel loss. Use manufacturer complex-Z/core-loss data for quantitative design near material or SRF limits.")
            : QStringLiteral("Non-ideal inductor: series winding/Q loss with optional self-capacitance and parallel core-loss resistance.");
    }
    else
    {
        const double c=value;
        std::complex<double> yc(j*w*c);
        if(parasitics.lossTangent>0.0) yc += w*c*parasitics.lossTangent;
        const std::complex<double> zc=std::abs(yc)>1e-30?1.0/yc:std::complex<double>(1e30,0.0);
        out.impedanceOhm=rseries+j*w*std::max(0.0,parasitics.parasiticInductanceH)+zc;
        if(parasitics.parasiticInductanceH>0.0)
            out.approximateSelfResonanceHz=1.0/(2.0*Pi*std::sqrt(c*parasitics.parasiticInductanceH));
        out.note=QStringLiteral("Non-ideal capacitor: ESR + ESL with optional dielectric loss tangent.");
    }

    const double re=std::abs(out.impedanceOhm.real());
    out.effectiveQualityFactor=re>1e-18?std::abs(out.impedanceOhm.imag())/re:std::numeric_limits<double>::infinity();
    return out;
}

std::complex<double> reactiveSeriesImpedance(ReactiveComponentType type,double value,double frequencyHz)
{
    const double w=2.0*Pi*positive(frequencyHz,1.0);
    const std::complex<double> j(0.0,1.0);
    value=std::max(0.0,value);
    switch(type)
    {
    case ReactiveComponentType::Inductor: return j*w*value;
    case ReactiveComponentType::Capacitor: return value>0.0?1.0/(j*w*value):std::complex<double>(0.0,0.0);
    case ReactiveComponentType::None: break;
    }
    return {0.0,0.0};
}

std::complex<double> reactiveShuntAdmittance(ReactiveComponentType type,double value,double frequencyHz)
{
    const auto z=reactiveSeriesImpedance(type,value,frequencyHz);
    return std::abs(z)>1e-30?1.0/z:std::complex<double>(0.0,0.0);
}

namespace
{
void assignSeriesComponent(LMatchSolution &s,double frequencyHz)
{
    const double w=2.0*Pi*positive(frequencyHz,1.0);
    if(std::abs(s.seriesReactanceOhm)<1e-12){s.seriesComponent=ReactiveComponentType::None;s.seriesValue=0.0;return;}
    if(s.seriesReactanceOhm>0.0){s.seriesComponent=ReactiveComponentType::Inductor;s.seriesValue=s.seriesReactanceOhm/w;}
    else{s.seriesComponent=ReactiveComponentType::Capacitor;s.seriesValue=-1.0/(w*s.seriesReactanceOhm);}
}

void assignShuntComponent(LMatchSolution &s,double frequencyHz)
{
    const double w=2.0*Pi*positive(frequencyHz,1.0);
    if(std::abs(s.shuntSusceptanceSiemens)<1e-15){s.shuntComponent=ReactiveComponentType::None;s.shuntValue=0.0;return;}
    if(s.shuntSusceptanceSiemens>0.0){s.shuntComponent=ReactiveComponentType::Capacitor;s.shuntValue=s.shuntSusceptanceSiemens/w;}
    else{s.shuntComponent=ReactiveComponentType::Inductor;s.shuntValue=-1.0/(w*s.shuntSusceptanceSiemens);}
}

std::complex<double> applyLMatchWithParasitics(const std::complex<double> &load,const LMatchSolution &solution,double frequencyHz,
                                                   const ReactiveComponentParasitics *inductorParasitics,
                                                   const ReactiveComponentParasitics *capacitorParasitics)
{
    if(!solution.valid)return load;
    auto componentZ=[&](ReactiveComponentType type,double value){
        if(type==ReactiveComponentType::None)return std::complex<double>(0.0,0.0);
        const ReactiveComponentParasitics *p=nullptr;
        if(type==ReactiveComponentType::Inductor)p=inductorParasitics;
        else if(type==ReactiveComponentType::Capacitor)p=capacitorParasitics;
        return p?reactiveComponent(type,value,frequencyHz,*p).impedanceOhm:reactiveSeriesImpedance(type,value,frequencyHz);
    };
    const auto zs=componentZ(solution.seriesComponent,solution.seriesValue);
    const auto zsh=componentZ(solution.shuntComponent,solution.shuntValue);
    const auto yp=(solution.shuntComponent!=ReactiveComponentType::None && std::abs(zsh)>1e-30)?1.0/zsh:std::complex<double>(0.0,0.0);
    if(solution.order==LMatchOrder::SeriesThenShunt)
    {
        const auto yload=std::abs(load)>1e-30?1.0/load:std::complex<double>(1e30,0.0);
        const auto y=yload+yp;
        return zs+(std::abs(y)>1e-30?1.0/y:std::complex<double>(1e30,0.0));
    }
    const auto z=load+zs;
    const auto y=(std::abs(z)>1e-30?1.0/z:std::complex<double>(1e30,0.0))+yp;
    return std::abs(y)>1e-30?1.0/y:std::complex<double>(1e30,0.0);
}

std::complex<double> applyLMatch(const std::complex<double> &load,const LMatchSolution &solution,double frequencyHz)
{
    return applyLMatchWithParasitics(load,solution,frequencyHz,nullptr,nullptr);
}
}

std::vector<LMatchSolution> synthesizeLMatch(const std::complex<double> &loadOhm,double referenceOhm,double frequencyHz)
{
    std::vector<LMatchSolution> out;
    const double r0=positive(referenceOhm,50.0);
    const double f=positive(frequencyHz,1.0);
    const double r=loadOhm.real();
    if(!(std::isfinite(r)&&std::isfinite(loadOhm.imag())) || r<=0.0 || std::abs(loadOhm)<1e-18)return out;

    // Topology 1: source -> series reactance -> shunt susceptance || load.
    // Re{1/(Y_L+jB)} = R0 sets the total shunt susceptance magnitude.
    const auto yl=1.0/loadOhm;
    const double g=yl.real();
    if(g>0.0)
    {
        double rad=g/r0-g*g;
        const double tol=1e-14*std::max(1.0,g/r0);
        if(rad>=-tol)
        {
            rad=std::max(0.0,rad);
            const double root=std::sqrt(rad);
            for(double sign : {-1.0,1.0})
            {
                const double btot=sign*root;
                LMatchSolution s;
                s.valid=true;s.order=LMatchOrder::SeriesThenShunt;
                s.shuntSusceptanceSiemens=btot-yl.imag();
                const auto zp=1.0/(g+std::complex<double>(0.0,btot));
                s.seriesReactanceOhm=-zp.imag();
                assignSeriesComponent(s,f);assignShuntComponent(s,f);
                s.matchedInputOhm=applyLMatch(loadOhm,s,f);
                s.description=QStringLiteral("Source -> series reactive element -> shunt reactive element across load");
                out.push_back(s);
                if(root<1e-15)break;
            }
        }
    }

    // Topology 2: source -> shunt susceptance -> series reactance -> load.
    // Re{1/(R+jX_total)} = 1/R0 sets the total series reactance magnitude.
    double rad2=r*r0-r*r;
    const double tol2=1e-12*std::max(1.0,r*r0);
    if(rad2>=-tol2)
    {
        rad2=std::max(0.0,rad2);
        const double root=std::sqrt(rad2);
        for(double sign : {-1.0,1.0})
        {
            const double xt=sign*root;
            LMatchSolution s;
            s.valid=true;s.order=LMatchOrder::ShuntThenSeries;
            s.seriesReactanceOhm=xt-loadOhm.imag();
            const auto zs=std::complex<double>(r,xt);
            const auto ys=1.0/zs;
            s.shuntSusceptanceSiemens=-ys.imag();
            assignSeriesComponent(s,f);assignShuntComponent(s,f);
            s.matchedInputOhm=applyLMatch(loadOhm,s,f);
            s.description=QStringLiteral("Source -> shunt reactive element -> series reactive element -> load");
            out.push_back(s);
            if(root<1e-15)break;
        }
    }

    // Remove duplicate degenerate solutions and sort the most economical reactance first.
    std::vector<LMatchSolution> unique;
    for(const auto &candidate:out)
    {
        bool duplicate=false;
        for(const auto &u:unique)
            if(candidate.order==u.order && std::abs(candidate.seriesReactanceOhm-u.seriesReactanceOhm)<1e-9 &&
               std::abs(candidate.shuntSusceptanceSiemens-u.shuntSusceptanceSiemens)<1e-12){duplicate=true;break;}
        if(!duplicate)unique.push_back(candidate);
    }
    std::sort(unique.begin(),unique.end(),[](const LMatchSolution&a,const LMatchSolution&b){
        const double ca=std::abs(a.seriesReactanceOhm)+1e4*std::abs(a.shuntSusceptanceSiemens);
        const double cb=std::abs(b.seriesReactanceOhm)+1e4*std::abs(b.shuntSusceptanceSiemens);
        return ca<cb;
    });
    return unique;
}

std::complex<double> shuntSeriesLcTrapImpedance(double inductanceH,double capacitanceF,double qualityFactor,double referenceFrequencyHz,double frequencyHz)
{
    const double l=positive(inductanceH,1e-12);
    const double c=positive(capacitanceF,1e-15);
    const double q=positive(qualityFactor,1e6);
    const double w0=2.0*Pi*positive(referenceFrequencyHz,1.0);
    const double w=2.0*Pi*positive(frequencyHz,1.0);
    const double esr=w0*l/q;
    const std::complex<double> j(0.0,1.0);
    return std::complex<double>(esr,0.0)+j*w*l+1.0/(j*w*c);
}

RfChainResult rfChain(const RfChainInput &input)
{
    RfChainResult out;
    const double f=positive(input.frequencyHz,1.0);
    const double zref=positive(input.referenceOhm,50.0);
    out.rawAntennaImpedanceOhm=input.antennaImpedanceOhm;
    if(!std::isfinite(input.antennaImpedanceOhm.real())||!std::isfinite(input.antennaImpedanceOhm.imag())||std::abs(input.antennaImpedanceOhm)<1e-18)
    {out.note=QStringLiteral("Invalid antenna/load impedance.");return out;}

    auto load=input.antennaImpedanceOhm;
    if(input.trapEnabled)
    {
        std::complex<double> ztrap;
        if(input.nonIdealTrapComponents)
        {
            auto lp=input.trapInductorParasitics; auto cp=input.trapCapacitorParasitics; lp.enabled=true; cp.enabled=true;
            const auto zl=reactiveComponent(ReactiveComponentType::Inductor,input.trapInductanceH,f,lp).impedanceOhm;
            const auto zc=reactiveComponent(ReactiveComponentType::Capacitor,input.trapCapacitanceF,f,cp).impedanceOhm;
            ztrap=zl+zc;
        }
        else ztrap=shuntSeriesLcTrapImpedance(input.trapInductanceH,input.trapCapacitanceF,input.trapQualityFactor,input.trapReferenceFrequencyHz,f);
        const auto y=1.0/load+(std::abs(ztrap)>1e-30?1.0/ztrap:std::complex<double>(1e30,0.0));
        load=std::abs(y)>1e-30?1.0/y:std::complex<double>(1e30,0.0);
    }
    out.antennaWithTrapOhm=load;

    if(input.matchingEnabled && input.matching.valid)
    {
        if(input.nonIdealMatchingComponents)
        {
            auto lp=input.matchingInductorParasitics; auto cp=input.matchingCapacitorParasitics; lp.enabled=true; cp.enabled=true;
            load=applyLMatchWithParasitics(load,input.matching,f,&lp,&cp);
        }
        else load=applyLMatch(load,input.matching,f);
    }
    out.afterMatchingOhm=load;

    if(input.lineEnabled)
    {
        auto line=input.line;
        line.frequencyHz=f;
        line.loadOhm=load;
        const auto lr=transmissionLine(line);
        if(!lr.valid){out.note=QStringLiteral("The PCB/transmission-line section is invalid for the requested geometry.");return out;}
        load=lr.inputImpedanceOhm;
        out.lineCharacteristicImpedanceOhm=lr.characteristicImpedanceOhm;
        out.lineLossDb=lr.totalLossDb;
    }
    out.sourceInputOhm=load;
    const std::complex<double> zr(zref,0.0);
    const auto den=load+zr;
    out.reflectionCoefficient=std::abs(den)>1e-30?(load-zr)/den:std::complex<double>(1.0,0.0);
    const double gm=std::abs(out.reflectionCoefficient);
    out.returnLossDb=gm>1e-15?-20.0*std::log10(gm):300.0;
    out.vswr=gm<1.0?(1.0+gm)/std::max(1.0-gm,1e-15):std::numeric_limits<double>::infinity();
    out.valid=std::isfinite(load.real())&&std::isfinite(load.imag());
    out.note=QStringLiteral("Cascaded educational RF chain: antenna/load, optional shunt series-LC trap, optional L-match, then optional lossy PCB transmission line back to the reference port. Matching can use either ideal reactances or first-order ESR/ESL/Q/SRF component models. Line discontinuities and layout coupling are not included.");
    return out;
}

WidebandMatchOptimizationResult optimizeWidebandLMatch(const WidebandMatchOptimizationInput &input)
{
    WidebandMatchOptimizationResult out;
    out.initialMatching=input.chainTemplate.matching;
    out.optimizedMatching=input.chainTemplate.matching;
    if(!input.chainTemplate.matchingEnabled || !input.chainTemplate.matching.valid || input.samples.empty())
    {
        out.note=QStringLiteral("Wideband optimizer requires a valid enabled L-match and at least one frequency/load sample.");
        return out;
    }

    auto objective=[&](const LMatchSolution &m){
        double sum=0.0,worst=0.0; int count=0;
        for(const auto &sample:input.samples)
        {
            if(!(sample.frequencyHz>0.0) || !std::isfinite(sample.antennaImpedanceOhm.real()) || !std::isfinite(sample.antennaImpedanceOhm.imag()))continue;
            auto ci=input.chainTemplate; ci.frequencyHz=sample.frequencyHz; ci.antennaImpedanceOhm=sample.antennaImpedanceOhm; ci.matching=m;
            const auto r=rfChain(ci); if(!r.valid)continue;
            const double g=std::abs(r.reflectionCoefficient); sum+=g; worst=std::max(worst,g); ++count;
        }
        if(count==0)return std::numeric_limits<double>::infinity();
        return input.objective==WidebandMatchObjective::WorstReflectionMagnitude?worst:sum/double(count);
    };

    LMatchSolution best=input.chainTemplate.matching;
    double bestObj=objective(best); out.initialObjective=bestObj; out.objectiveHistory.push_back(bestObj);
    if(!std::isfinite(bestObj)){out.note=QStringLiteral("No valid RF-chain samples were available for optimization.");return out;}

    const int n=std::clamp(input.samplesPerCoordinate,3,31);
    const int passes=std::clamp(input.passes,1,12);
    double span=std::clamp(input.initialRelativeSpan,0.01,2.0);
    for(int pass=0;pass<passes;++pass)
    {
        for(int coordinate=0;coordinate<2;++coordinate)
        {
            const double base=(coordinate==0)?best.seriesValue:best.shuntValue;
            if(base<=0.0)continue;
            LMatchSolution localBest=best; double localObj=bestObj;
            for(int i=0;i<n;++i)
            {
                const double u=(n==1)?0.0:(2.0*double(i)/(n-1)-1.0);
                const double factor=std::max(0.05,1.0+span*u);
                auto candidate=best;
                if(coordinate==0)candidate.seriesValue=base*factor; else candidate.shuntValue=base*factor;
                const double obj=objective(candidate);
                if(obj<localObj){localObj=obj;localBest=candidate;}
            }
            best=localBest;bestObj=localObj;out.objectiveHistory.push_back(bestObj);
        }
        span*=0.45;
    }
    out.optimizedMatching=best; out.optimizedObjective=bestObj; out.valid=std::isfinite(bestObj);
    out.note=QStringLiteral("Bounded coordinate-search wideband tuning. The L-match topology and component types remain fixed; only the two component values are adjusted and every candidate is evaluated through the complete RF-chain model.");
    return out;
}

} // namespace EmEngineering

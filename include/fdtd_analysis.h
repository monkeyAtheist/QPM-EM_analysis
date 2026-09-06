#pragma once

#include <complex>
#include <cstddef>
#include <string>
#include <vector>

namespace FDTDAnalysis
{
constexpr double Pi = 3.141592653589793238462643383279502884;

struct Spectrum
{
    std::vector<double> frequencyHz;
    std::vector<double> magnitude;
    std::vector<double> magnitudeDb;
    std::vector<double> phaseDeg;
};

std::complex<double> phasorAtFrequency(const std::vector<double> &samples,
                                       double dtS,
                                       double frequencyHz,
                                       std::size_t startIndex = 0);

Spectrum realSpectrum(const std::vector<double> &samples,
                      double dtS,
                      std::size_t maxBins = 512,
                      bool hannWindow = true);

struct PortTimeSeries
{
    // Signs must be chosen so a +x travelling wave obeys E_t = eta * H_t.
    std::vector<double> electricTangentialVPerM;
    std::vector<double> magneticTangentialAPerM;
};

struct SParameterEstimate
{
    bool valid = false;
    std::string error;
    std::complex<double> forwardInputVPerM{};
    std::complex<double> reflectedInputVPerM{};
    std::complex<double> forwardOutputVPerM{};
    std::complex<double> s11{};
    std::complex<double> s21{};
    double s11Db = 0.0;
    double s21Db = 0.0;
    double vswr = 1.0;
};

SParameterEstimate estimateLocalPlaneWaveSParameters(const PortTimeSeries &input,
                                                       const PortTimeSeries &output,
                                                       double dtS,
                                                       double frequencyHz,
                                                       double etaInputOhm,
                                                       double etaOutputOhm,
                                                       bool discardInitialTransient = true);


enum class GuideScalarBoundary
{
    DirichletSine, // scalar field is zero on PEC side walls (TMz Ez)
    NeumannCosine  // normal derivative is zero on PEC side walls (TEz Hz)
};

struct GuideModeInfo
{
    bool propagating = false;
    std::string error;
    int modeIndex = 0;
    double heightM = 0.0;
    double cutoffHz = 0.0;
    double betaRadPerM = 0.0;
    double guideWavelengthM = 0.0;
};

GuideModeInfo guideModeInfo(double frequencyHz,
                            double heightM,
                            int modeIndex,
                            GuideScalarBoundary boundary,
                            double epsilonR = 1.0,
                            double muR = 1.0);

struct GuideModalSParameterEstimate
{
    bool valid = false;
    std::string error;
    GuideModeInfo mode;
    std::complex<double> forwardInput{};
    std::complex<double> reflectedInput{};
    std::complex<double> forwardOutput{};
    std::complex<double> reflectedOutput{};
    std::complex<double> s11{};
    std::complex<double> s21{};
    double s11Db = 0.0;
    double s21Db = 0.0;
    double vswr = 1.0;
    double inputModePurity = 0.0;
    double outputModePurity = 0.0;
};

// Extracts one transverse mode from two neighboring x-planes at each port.
// This is a true transverse modal projection for a uniform 2D PEC parallel-plate
// guide. Input/output apertures must use the same yStart/yEnd and homogeneous medium.
GuideModalSParameterEstimate estimateParallelPlateGuideModeSParameters(
    const std::vector<std::complex<double>> &scalarPhasor,
    int nx,
    int ny,
    double dxM,
    double dyM,
    double frequencyHz,
    int inputX,
    int outputX,
    int yStart,
    int yEnd,
    int modeIndex,
    GuideScalarBoundary boundary,
    int planeSeparationCells = 2,
    double epsilonR = 1.0,
    double muR = 1.0);

struct RfSweepAnalysis
{
    bool valid = false;
    std::string error;
    double referenceImpedanceOhm = 50.0;
    std::vector<double> frequencyHz;
    std::vector<std::complex<double>> s11;
    std::vector<std::complex<double>> s21;
    std::vector<double> s11Db;
    std::vector<double> s21Db;
    std::vector<double> s11PhaseDeg;
    std::vector<double> s21PhaseDeg;
    std::vector<double> groupDelayS;
    std::vector<double> vswr;
    std::vector<std::complex<double>> inputImpedanceOhm;

    std::size_t resonanceIndex = 0;
    double resonanceHz = 0.0;
    double resonanceS11Db = 0.0;
    double resonanceReturnLossDb = 0.0;
    double resonanceVswr = 1.0;
    std::complex<double> resonanceInputImpedanceOhm{};

    std::size_t transmissionPeakIndex = 0;
    double transmissionPeakHz = 0.0;
    double transmissionPeakDb = 0.0;
    bool has3dBBandwidth = false;
    double lower3dBHz = 0.0;
    double upper3dBHz = 0.0;
    double bandwidth3dBHz = 0.0;
    double loadedQ = 0.0;

    bool hasVswr2Bandwidth = false;
    double lowerVswr2Hz = 0.0;
    double upperVswr2Hz = 0.0;
    double bandwidthVswr2Hz = 0.0;

    std::vector<std::size_t> resonanceCandidates;
};

std::complex<double> reflectionToImpedance(std::complex<double> gamma, double referenceImpedanceOhm = 50.0);
RfSweepAnalysis analyzeSParameterSweep(const std::vector<double> &frequencyHz,
                                       const std::vector<std::complex<double>> &s11,
                                       const std::vector<std::complex<double>> &s21,
                                       double referenceImpedanceOhm = 50.0);

class HarmonicFieldAccumulator
{
public:
    void configure(std::size_t cellCount, double frequencyHz, double dtS);
    void reset();
    void add(const std::vector<double> &field, double timeS);
    std::size_t sampleCount() const noexcept { return m_count; }
    double frequencyHz() const noexcept { return m_frequencyHz; }
    std::vector<std::complex<double>> phasorField() const;

private:
    std::vector<std::complex<double>> m_sum;
    double m_frequencyHz = 1.0;
    double m_dtS = 1.0;
    std::size_t m_count = 0;
};

struct FarFieldPattern2D
{
    bool valid = false;
    std::string error;
    std::vector<double> angleDeg;
    std::vector<double> normalizedMagnitude;
    std::vector<double> magnitudeDb;
    double directivity2DLinear = 0.0;
    double directivity2DDbi = 0.0;
};

// Scalar 2D Helmholtz/Huygens contour transform.  The phasor field must be
// sampled in a homogeneous region enclosing all radiators/scatterers and the
// contour must stay outside material discontinuities and inside the absorber.
FarFieldPattern2D scalarHuygensFarField(const std::vector<std::complex<double>> &scalarPhasor,
                                        int nx,
                                        int ny,
                                        double dxM,
                                        double dyM,
                                        double waveNumberRadPerM,
                                        int contourMarginCells,
                                        int angleCount = 361);
}

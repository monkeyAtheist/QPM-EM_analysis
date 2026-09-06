#include "fdtd_analysis.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace FDTDAnalysis
{
namespace
{
double db20(double value)
{
    return value > 1.0e-15 ? 20.0 * std::log10(value) : -300.0;
}

std::complex<double> expj(double angle)
{
    return {std::cos(angle), std::sin(angle)};
}
}

std::complex<double> phasorAtFrequency(const std::vector<double> &samples,
                                       double dtS,
                                       double frequencyHz,
                                       std::size_t startIndex)
{
    if (!(dtS > 0.0) || !(frequencyHz >= 0.0) || startIndex >= samples.size())
        return {};
    const std::size_t n = samples.size() - startIndex;
    if (n < 2) return {};
    std::complex<double> sum{};
    for (std::size_t i = startIndex; i < samples.size(); ++i)
    {
        const double t = double(i) * dtS;
        sum += samples[i] * expj(-2.0 * Pi * frequencyHz * t);
    }
    return (2.0 / double(n)) * sum;
}

Spectrum realSpectrum(const std::vector<double> &samples,
                      double dtS,
                      std::size_t maxBins,
                      bool hannWindow)
{
    Spectrum out;
    if (!(dtS > 0.0) || samples.size() < 4) return out;
    const std::size_t n = samples.size();
    const std::size_t bins = std::min<std::size_t>(n / 2 + 1, std::max<std::size_t>(2, maxBins));
    out.frequencyHz.reserve(bins);
    out.magnitude.reserve(bins);
    out.magnitudeDb.reserve(bins);
    out.phaseDeg.reserve(bins);

    double windowSum = 0.0;
    std::vector<double> window(n, 1.0);
    if (hannWindow && n > 1)
        for (std::size_t i = 0; i < n; ++i)
            window[i] = 0.5 - 0.5 * std::cos(2.0 * Pi * double(i) / double(n - 1));
    for (double w : window) windowSum += w;
    windowSum = std::max(windowSum, 1.0e-30);

    const double sampleRate = 1.0 / dtS;
    for (std::size_t bi = 0; bi < bins; ++bi)
    {
        // If maxBins clips the FFT, spread bins across the positive Nyquist interval.
        const std::size_t k = bins == n / 2 + 1 ? bi :
            static_cast<std::size_t>(std::llround(double(bi) * double(n / 2) / double(bins - 1)));
        std::complex<double> sum{};
        for (std::size_t i = 0; i < n; ++i)
            sum += samples[i] * window[i] * expj(-2.0 * Pi * double(k) * double(i) / double(n));
        const double scale = (k == 0 || (n % 2 == 0 && k == n / 2)) ? 1.0 / windowSum : 2.0 / windowSum;
        const std::complex<double> value = scale * sum;
        const double mag = std::abs(value);
        out.frequencyHz.push_back(double(k) * sampleRate / double(n));
        out.magnitude.push_back(mag);
        out.magnitudeDb.push_back(db20(mag));
        out.phaseDeg.push_back(std::arg(value) * 180.0 / Pi);
    }
    return out;
}

SParameterEstimate estimateLocalPlaneWaveSParameters(const PortTimeSeries &input,
                                                       const PortTimeSeries &output,
                                                       double dtS,
                                                       double frequencyHz,
                                                       double etaInputOhm,
                                                       double etaOutputOhm,
                                                       bool discardInitialTransient)
{
    SParameterEstimate out;
    if (!(dtS > 0.0) || !(frequencyHz > 0.0) || !(etaInputOhm > 0.0) || !(etaOutputOhm > 0.0))
    {
        out.error = "Invalid dt/frequency/wave impedance.";
        return out;
    }
    const std::size_t ni = std::min(input.electricTangentialVPerM.size(), input.magneticTangentialAPerM.size());
    const std::size_t no = std::min(output.electricTangentialVPerM.size(), output.magneticTangentialAPerM.size());
    if (ni < 16 || no < 16)
    {
        out.error = "Need at least 16 synchronized samples at both ports.";
        return out;
    }

    const std::size_t startI = discardInitialTransient ? ni / 3 : 0;
    const std::size_t startO = discardInitialTransient ? no / 3 : 0;
    const auto ei = phasorAtFrequency(input.electricTangentialVPerM, dtS, frequencyHz, startI);
    const auto hi = phasorAtFrequency(input.magneticTangentialAPerM, dtS, frequencyHz, startI);
    const auto eo = phasorAtFrequency(output.electricTangentialVPerM, dtS, frequencyHz, startO);
    const auto ho = phasorAtFrequency(output.magneticTangentialAPerM, dtS, frequencyHz, startO);

    out.forwardInputVPerM = 0.5 * (ei + etaInputOhm * hi);
    out.reflectedInputVPerM = 0.5 * (ei - etaInputOhm * hi);
    out.forwardOutputVPerM = 0.5 * (eo + etaOutputOhm * ho);
    if (std::abs(out.forwardInputVPerM) < 1.0e-15)
    {
        out.error = "Forward input phasor is too small for a stable S-parameter estimate.";
        return out;
    }

    out.s11 = out.reflectedInputVPerM / out.forwardInputVPerM;
    out.s21 = (out.forwardOutputVPerM / out.forwardInputVPerM) * std::sqrt(etaInputOhm / etaOutputOhm);
    out.s11Db = db20(std::abs(out.s11));
    out.s21Db = db20(std::abs(out.s21));
    const double gamma = std::abs(out.s11);
    out.vswr = gamma < 0.999999 ? (1.0 + gamma) / (1.0 - gamma) : std::numeric_limits<double>::infinity();
    out.valid = true;
    return out;
}


GuideModeInfo guideModeInfo(double frequencyHz,
                            double heightM,
                            int modeIndex,
                            GuideScalarBoundary boundary,
                            double epsilonR,
                            double muR)
{
    GuideModeInfo out;
    out.modeIndex = modeIndex;
    out.heightM = heightM;
    if (!(frequencyHz > 0.0) || !(heightM > 0.0) || !(epsilonR > 0.0) || !(muR > 0.0))
    {
        out.error = "Invalid guide frequency, height or material parameters.";
        return out;
    }
    if (boundary == GuideScalarBoundary::DirichletSine && modeIndex < 1)
    {
        out.error = "Dirichlet/sine guide modes require m >= 1.";
        return out;
    }
    if (modeIndex < 0)
    {
        out.error = "Mode index must be non-negative.";
        return out;
    }

    constexpr double c0 = 299792458.0;
    const double n = std::sqrt(epsilonR * muR);
    out.cutoffHz = modeIndex == 0 ? 0.0 : double(modeIndex) * c0 / (2.0 * heightM * n);
    const double k = 2.0 * Pi * frequencyHz * n / c0;
    const double kc = double(modeIndex) * Pi / heightM;
    const double beta2 = k * k - kc * kc;
    if (!(beta2 > 0.0))
    {
        out.error = "Selected guide mode is below cutoff.";
        return out;
    }
    out.betaRadPerM = std::sqrt(beta2);
    out.guideWavelengthM = 2.0 * Pi / out.betaRadPerM;
    out.propagating = true;
    return out;
}

GuideModalSParameterEstimate estimateParallelPlateGuideModeSParameters(
    const std::vector<std::complex<double>> &u,
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
    int planeSeparationCells,
    double epsilonR,
    double muR)
{
    GuideModalSParameterEstimate out;
    if (nx < 8 || ny < 8 || static_cast<std::size_t>(nx * ny) != u.size() || !(dxM > 0.0) || !(dyM > 0.0))
    {
        out.error = "Invalid phasor grid.";
        return out;
    }
    yStart = std::clamp(yStart, 1, ny - 2);
    yEnd = std::clamp(yEnd, 1, ny - 2);
    if (yEnd < yStart) std::swap(yStart, yEnd);
    if (yEnd - yStart < 3)
    {
        out.error = "Guide aperture must span at least four cells.";
        return out;
    }
    planeSeparationCells = std::clamp(planeSeparationCells, 1, std::max(1, nx / 8));
    if (inputX < 1 || inputX + planeSeparationCells >= nx - 1 || outputX < 1 || outputX + planeSeparationCells >= nx - 1)
    {
        out.error = "Port planes or separation leave the FDTD domain.";
        return out;
    }
    if (outputX <= inputX + planeSeparationCells)
    {
        out.error = "Output port must be to the right of the input port.";
        return out;
    }

    // PEC walls are placed approximately half a cell outside the first/last sampled interior cell.
    const double height = double(yEnd - yStart + 2) * dyM;
    out.mode = guideModeInfo(frequencyHz, height, modeIndex, boundary, epsilonR, muR);
    if (!out.mode.propagating)
    {
        out.error = out.mode.error;
        return out;
    }

    const int mode = modeIndex;
    const double beta = out.mode.betaRadPerM;
    const double d = double(planeSeparationCells) * dxM;
    const std::complex<double> em = expj(-beta * d);
    const std::complex<double> ep = expj(+beta * d);
    const std::complex<double> denom = em - ep;
    if (std::abs(denom) < 1.0e-8)
    {
        out.error = "Port-plane separation is ill-conditioned for this guide wavelength; change separation cells.";
        return out;
    }

    auto at = [&](int x, int y) -> const std::complex<double> & {
        return u[static_cast<std::size_t>(y * nx + x)];
    };
    auto project = [&](int x, double &purity) {
        std::complex<double> numerator{};
        double phiEnergy = 0.0;
        double fieldEnergy = 0.0;
        for (int y = yStart; y <= yEnd; ++y)
        {
            const double un = double(y - yStart + 1) / double(yEnd - yStart + 2);
            const double phi = boundary == GuideScalarBoundary::DirichletSine
                ? std::sin(double(mode) * Pi * un)
                : std::cos(double(mode) * Pi * un);
            const auto value = at(x, y);
            numerator += value * phi;
            phiEnergy += phi * phi;
            fieldEnergy += std::norm(value);
        }
        const std::complex<double> coeff = phiEnergy > 1.0e-20 ? numerator / phiEnergy : std::complex<double>{};
        const double projectedEnergy = std::norm(coeff) * phiEnergy;
        purity = fieldEnergy > 1.0e-30 ? std::clamp(projectedEnergy / fieldEnergy, 0.0, 1.0) : 0.0;
        return coeff;
    };

    auto decompose = [&](int x, std::complex<double> &forward, std::complex<double> &backward, double &purity) {
        double p0 = 0.0, p1 = 0.0;
        const auto c0 = project(x, p0);
        const auto c1 = project(x + planeSeparationCells, p1);
        forward = (c1 - c0 * ep) / denom;
        backward = c0 - forward;
        purity = std::min(p0, p1);
    };

    decompose(inputX, out.forwardInput, out.reflectedInput, out.inputModePurity);
    decompose(outputX, out.forwardOutput, out.reflectedOutput, out.outputModePurity);
    if (std::abs(out.forwardInput) < 1.0e-14)
    {
        out.error = "Projected forward input mode is too small.";
        return out;
    }

    out.s11 = out.reflectedInput / out.forwardInput;
    out.s21 = out.forwardOutput / out.forwardInput;
    out.s11Db = db20(std::abs(out.s11));
    out.s21Db = db20(std::abs(out.s21));
    const double gamma = std::abs(out.s11);
    out.vswr = gamma < 0.999999 ? (1.0 + gamma) / (1.0 - gamma) : std::numeric_limits<double>::infinity();
    out.valid = true;
    return out;
}

std::complex<double> reflectionToImpedance(std::complex<double> gamma, double referenceImpedanceOhm)
{
    if (!(referenceImpedanceOhm > 0.0)) return {};
    const std::complex<double> denominator = 1.0 - gamma;
    if (std::abs(denominator) < 1.0e-12)
        return {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    return referenceImpedanceOhm * (1.0 + gamma) / denominator;
}

RfSweepAnalysis analyzeSParameterSweep(const std::vector<double> &frequencyHz,
                                       const std::vector<std::complex<double>> &s11,
                                       const std::vector<std::complex<double>> &s21,
                                       double referenceImpedanceOhm)
{
    RfSweepAnalysis out;
    out.referenceImpedanceOhm = referenceImpedanceOhm;
    const std::size_t n = std::min({frequencyHz.size(), s11.size(), s21.size()});
    if (n < 2 || !(referenceImpedanceOhm > 0.0))
    {
        out.error = "Need at least two S-parameter samples and a positive reference impedance.";
        return out;
    }
    for (std::size_t i = 0; i < n; ++i)
        if (!(frequencyHz[i] > 0.0) || (i > 0 && !(frequencyHz[i] > frequencyHz[i - 1])))
        {
            out.error = "Sweep frequencies must be positive and strictly increasing.";
            return out;
        }

    out.frequencyHz.assign(frequencyHz.begin(), frequencyHz.begin() + static_cast<std::ptrdiff_t>(n));
    out.s11.assign(s11.begin(), s11.begin() + static_cast<std::ptrdiff_t>(n));
    out.s21.assign(s21.begin(), s21.begin() + static_cast<std::ptrdiff_t>(n));
    out.s11Db.resize(n);
    out.s21Db.resize(n);
    out.s11PhaseDeg.resize(n);
    out.s21PhaseDeg.resize(n);
    out.groupDelayS.assign(n, 0.0);
    out.vswr.resize(n);
    out.inputImpedanceOhm.resize(n);

    std::vector<double> phaseRad(n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double g = std::abs(out.s11[i]);
        out.s11Db[i] = db20(g);
        out.s21Db[i] = db20(std::abs(out.s21[i]));
        out.s11PhaseDeg[i] = std::arg(out.s11[i]) * 180.0 / Pi;
        phaseRad[i] = std::arg(out.s21[i]);
        out.vswr[i] = g < 0.999999 ? (1.0 + g) / (1.0 - g) : std::numeric_limits<double>::infinity();
        out.inputImpedanceOhm[i] = reflectionToImpedance(out.s11[i], referenceImpedanceOhm);
    }

    // Unwrap S21 phase before differentiation.
    for (std::size_t i = 1; i < n; ++i)
    {
        double d = phaseRad[i] - phaseRad[i - 1];
        while (d > Pi) { phaseRad[i] -= 2.0 * Pi; d -= 2.0 * Pi; }
        while (d < -Pi) { phaseRad[i] += 2.0 * Pi; d += 2.0 * Pi; }
    }
    for (std::size_t i = 0; i < n; ++i)
        out.s21PhaseDeg[i] = phaseRad[i] * 180.0 / Pi;

    auto groupDelayBetween = [&](std::size_t a, std::size_t b) {
        const double dw = 2.0 * Pi * (out.frequencyHz[b] - out.frequencyHz[a]);
        return std::abs(dw) > 1.0e-30 ? -(phaseRad[b] - phaseRad[a]) / dw : 0.0;
    };
    out.groupDelayS[0] = groupDelayBetween(0, 1);
    for (std::size_t i = 1; i + 1 < n; ++i) out.groupDelayS[i] = groupDelayBetween(i - 1, i + 1);
    out.groupDelayS[n - 1] = groupDelayBetween(n - 2, n - 1);

    out.resonanceIndex = static_cast<std::size_t>(std::distance(out.s11Db.begin(), std::min_element(out.s11Db.begin(), out.s11Db.end())));
    out.resonanceHz = out.frequencyHz[out.resonanceIndex];
    out.resonanceS11Db = out.s11Db[out.resonanceIndex];
    out.resonanceReturnLossDb = -out.resonanceS11Db;
    out.resonanceVswr = out.vswr[out.resonanceIndex];
    out.resonanceInputImpedanceOhm = out.inputImpedanceOhm[out.resonanceIndex];

    // Local minima are useful resonance candidates. Keep the strongest five.
    for (std::size_t i = 1; i + 1 < n; ++i)
        if (out.s11Db[i] <= out.s11Db[i - 1] && out.s11Db[i] <= out.s11Db[i + 1])
            out.resonanceCandidates.push_back(i);
    if (out.resonanceCandidates.empty()) out.resonanceCandidates.push_back(out.resonanceIndex);
    std::stable_sort(out.resonanceCandidates.begin(), out.resonanceCandidates.end(), [&](std::size_t a, std::size_t b) {
        return out.s11Db[a] < out.s11Db[b];
    });
    if (out.resonanceCandidates.size() > 5) out.resonanceCandidates.resize(5);

    out.transmissionPeakIndex = static_cast<std::size_t>(std::distance(out.s21Db.begin(), std::max_element(out.s21Db.begin(), out.s21Db.end())));
    out.transmissionPeakHz = out.frequencyHz[out.transmissionPeakIndex];
    out.transmissionPeakDb = out.s21Db[out.transmissionPeakIndex];

    auto interpolateCrossing = [&](std::size_t i0, std::size_t i1, const std::vector<double> &y, double target) {
        const double y0 = y[i0], y1 = y[i1];
        if (std::abs(y1 - y0) < 1.0e-15) return 0.5 * (out.frequencyHz[i0] + out.frequencyHz[i1]);
        const double t = std::clamp((target - y0) / (y1 - y0), 0.0, 1.0);
        return out.frequencyHz[i0] + t * (out.frequencyHz[i1] - out.frequencyHz[i0]);
    };

    const double threshold3dB = out.transmissionPeakDb - 3.0;
    bool haveLow = false, haveHigh = false;
    for (std::size_t i = out.transmissionPeakIndex; i > 0; --i)
        if (out.s21Db[i - 1] < threshold3dB && out.s21Db[i] >= threshold3dB)
        {
            out.lower3dBHz = interpolateCrossing(i - 1, i, out.s21Db, threshold3dB);
            haveLow = true;
            break;
        }
    for (std::size_t i = out.transmissionPeakIndex; i + 1 < n; ++i)
        if (out.s21Db[i] >= threshold3dB && out.s21Db[i + 1] < threshold3dB)
        {
            out.upper3dBHz = interpolateCrossing(i, i + 1, out.s21Db, threshold3dB);
            haveHigh = true;
            break;
        }
    if (haveLow && haveHigh && out.upper3dBHz > out.lower3dBHz)
    {
        out.has3dBBandwidth = true;
        out.bandwidth3dBHz = out.upper3dBHz - out.lower3dBHz;
        out.loadedQ = out.transmissionPeakHz / out.bandwidth3dBHz;
    }

    // VSWR <= 2 interval around the best matched point.
    const double vswrLimit = 2.0;
    const std::size_t r = out.resonanceIndex;
    if (out.vswr[r] <= vswrLimit)
    {
        std::size_t lo = r, hi = r;
        while (lo > 0 && out.vswr[lo - 1] <= vswrLimit) --lo;
        while (hi + 1 < n && out.vswr[hi + 1] <= vswrLimit) ++hi;
        auto crossVswr = [&](std::size_t a, std::size_t b) {
            return interpolateCrossing(a, b, out.vswr, vswrLimit);
        };
        out.lowerVswr2Hz = lo > 0 ? crossVswr(lo - 1, lo) : out.frequencyHz.front();
        out.upperVswr2Hz = hi + 1 < n ? crossVswr(hi, hi + 1) : out.frequencyHz.back();
        if (out.upperVswr2Hz > out.lowerVswr2Hz)
        {
            out.hasVswr2Bandwidth = true;
            out.bandwidthVswr2Hz = out.upperVswr2Hz - out.lowerVswr2Hz;
        }
    }

    out.valid = true;
    return out;
}

void HarmonicFieldAccumulator::configure(std::size_t cellCount, double frequencyHz, double dtS)
{
    m_sum.assign(cellCount, {});
    m_frequencyHz = std::max(1.0, frequencyHz);
    m_dtS = std::max(1.0e-30, dtS);
    m_count = 0;
}

void HarmonicFieldAccumulator::reset()
{
    std::fill(m_sum.begin(), m_sum.end(), std::complex<double>{});
    m_count = 0;
}

void HarmonicFieldAccumulator::add(const std::vector<double> &field, double timeS)
{
    if (field.size() != m_sum.size() || m_sum.empty()) return;
    const std::complex<double> phase = expj(-2.0 * Pi * m_frequencyHz * timeS);
    for (std::size_t i = 0; i < field.size(); ++i)
        m_sum[i] += field[i] * phase;
    ++m_count;
}

std::vector<std::complex<double>> HarmonicFieldAccumulator::phasorField() const
{
    auto out = m_sum;
    if (m_count == 0) return out;
    const double scale = 2.0 / double(m_count);
    for (auto &v : out) v *= scale;
    return out;
}

FarFieldPattern2D scalarHuygensFarField(const std::vector<std::complex<double>> &u,
                                        int nx,
                                        int ny,
                                        double dxM,
                                        double dyM,
                                        double k,
                                        int margin,
                                        int angleCount)
{
    FarFieldPattern2D out;
    if (nx < 8 || ny < 8 || static_cast<std::size_t>(nx * ny) != u.size() ||
        !(dxM > 0.0) || !(dyM > 0.0) || !(k > 0.0))
    {
        out.error = "Invalid scalar phasor grid or wave number.";
        return out;
    }
    margin = std::clamp(margin, 2, std::min(nx, ny) / 2 - 2);
    angleCount = std::max(37, angleCount);
    const int x0 = margin, x1 = nx - 1 - margin;
    const int y0 = margin, y1 = ny - 1 - margin;
    if (x1 <= x0 + 2 || y1 <= y0 + 2)
    {
        out.error = "Huygens contour is too small.";
        return out;
    }
    auto at = [&](int x, int y) -> const std::complex<double> & { return u[static_cast<std::size_t>(y * nx + x)]; };

    struct Point { int x, y; double nxv, nyv, ds; };
    std::vector<Point> contour;
    for (int y = y0; y <= y1; ++y)
    {
        contour.push_back({x0,y,-1.0,0.0,dyM});
        contour.push_back({x1,y, 1.0,0.0,dyM});
    }
    for (int x = x0 + 1; x < x1; ++x)
    {
        contour.push_back({x,y0,0.0,-1.0,dxM});
        contour.push_back({x,y1,0.0, 1.0,dxM});
    }

    std::vector<double> mag;
    mag.reserve(static_cast<std::size_t>(angleCount));
    out.angleDeg.reserve(static_cast<std::size_t>(angleCount));
    for (int ai = 0; ai < angleCount; ++ai)
    {
        const double phi = 2.0 * Pi * double(ai) / double(angleCount - 1);
        const double rx = std::cos(phi), ry = std::sin(phi);
        std::complex<double> integral{};
        for (const auto &p : contour)
        {
            const auto dudx = (at(p.x + 1, p.y) - at(p.x - 1, p.y)) / (2.0 * dxM);
            const auto dudy = (at(p.x, p.y + 1) - at(p.x, p.y - 1)) / (2.0 * dyM);
            const auto dudn = p.nxv * dudx + p.nyv * dudy;
            const double xM = (double(p.x) - 0.5 * double(nx - 1)) * dxM;
            const double yM = (double(p.y) - 0.5 * double(ny - 1)) * dyM;
            const double ndotr = p.nxv * rx + p.nyv * ry;
            const auto kernel = expj(-k * (rx * xM + ry * yM));
            integral += (dudn - std::complex<double>(0.0, k * ndotr) * at(p.x, p.y)) * kernel * p.ds;
        }
        out.angleDeg.push_back(phi * 180.0 / Pi);
        mag.push_back(std::abs(integral));
    }

    const double maxMag = *std::max_element(mag.begin(), mag.end());
    if (!(maxMag > 1.0e-30) || !std::isfinite(maxMag))
    {
        out.error = "Far-field contour integral is numerically zero.";
        return out;
    }
    out.normalizedMagnitude.resize(mag.size());
    out.magnitudeDb.resize(mag.size());
    double powerIntegral = 0.0;
    for (std::size_t i = 0; i < mag.size(); ++i)
    {
        const double nmag = mag[i] / maxMag;
        out.normalizedMagnitude[i] = nmag;
        out.magnitudeDb[i] = db20(nmag);
        const double weight = (i == 0 || i + 1 == mag.size()) ? 0.5 : 1.0;
        powerIntegral += weight * nmag * nmag;
    }
    const double dphi = 2.0 * Pi / double(angleCount - 1);
    powerIntegral *= dphi;
    out.directivity2DLinear = powerIntegral > 0.0 ? 2.0 * Pi / powerIntegral : 0.0;
    out.directivity2DDbi = out.directivity2DLinear > 0.0 ? 10.0 * std::log10(out.directivity2DLinear) : -300.0;
    out.valid = true;
    return out;
}
}

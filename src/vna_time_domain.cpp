#include "vna_time_domain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace VnaTimeDomain
{
namespace
{
bool finiteComplex(const std::complex<double> &v)
{
    return std::isfinite(v.real()) && std::isfinite(v.imag());
}

double windowValue(Window type, std::size_t i, std::size_t n)
{
    if (n <= 1 || type == Window::Rectangular) return 1.0;
    const double phase = 2.0 * Pi * static_cast<double>(i) / static_cast<double>(n - 1);
    switch (type)
    {
        case Window::Hann:     return 0.5 - 0.5 * std::cos(phase);
        case Window::Hamming:  return 0.54 - 0.46 * std::cos(phase);
        case Window::Blackman: return 0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);
        case Window::Rectangular: default: return 1.0;
    }
}

double insideGateWeight(double t, double start, double stop, double transition, GateEdge edge)
{
    if (stop < start) std::swap(start, stop);
    if (edge == GateEdge::Rectangular || transition <= 0.0)
        return (t >= start && t <= stop) ? 1.0 : 0.0;

    const double half = 0.5 * transition;
    if (t <= start - half || t >= stop + half) return 0.0;
    if (t >= start + half && t <= stop - half) return 1.0;

    if (t < start + half)
    {
        const double u = std::clamp((t - (start - half)) / transition, 0.0, 1.0);
        return 0.5 - 0.5 * std::cos(Pi * u);
    }

    const double u = std::clamp((t - (stop - half)) / transition, 0.0, 1.0);
    return 0.5 + 0.5 * std::cos(Pi * u);
}
}

TransformResult toTimeDomain(const std::vector<double> &frequencyHz,
                             const std::vector<std::complex<double>> &samples,
                             Window windowType,
                             double uniformityTolerance)
{
    TransformResult out;
    const std::size_t n = frequencyHz.size();
    if (n < 2 || samples.size() != n)
    {
        out.error = "Frequency and complex sample arrays must have the same size >= 2.";
        return out;
    }
    if (!std::isfinite(frequencyHz.front()) || !std::isfinite(frequencyHz.back()))
    {
        out.error = "Frequency array contains a non-finite endpoint.";
        return out;
    }
    for (const auto &v : samples)
        if (!finiteComplex(v))
        {
            out.error = "Complex sweep contains a non-finite sample.";
            return out;
        }

    const double df = (frequencyHz.back() - frequencyHz.front()) / static_cast<double>(n - 1);
    if (!(df > 0.0) || !std::isfinite(df))
    {
        out.error = "Frequencies must be strictly increasing.";
        return out;
    }

    const double toleranceHz = std::max(1e-12, std::abs(df) * std::max(0.0, uniformityTolerance));
    for (std::size_t i = 1; i < n; ++i)
    {
        if (!std::isfinite(frequencyHz[i]) || !(frequencyHz[i] > frequencyHz[i-1]))
        {
            out.error = "Frequencies must be finite and strictly increasing.";
            return out;
        }
        const double localDf = frequencyHz[i] - frequencyHz[i-1];
        if (std::abs(localDf - df) > toleranceHz)
        {
            out.error = "Time-domain conversion requires a uniformly spaced frequency sweep.";
            return out;
        }
    }

    out.frequencyHz = frequencyHz;
    out.frequencyStepHz = df;
    out.frequencySpanHz = frequencyHz.back() - frequencyHz.front();
    out.timeStepS = 1.0 / (static_cast<double>(n) * df);
    out.unambiguousTimeS = 1.0 / df;
    out.timeS.resize(n);
    out.window.resize(n);
    out.timeResponse.assign(n, {0.0, 0.0});

    std::vector<std::complex<double>> weighted(n);
    for (std::size_t k = 0; k < n; ++k)
    {
        out.window[k] = windowValue(windowType, k, n);
        weighted[k] = samples[k] * out.window[k];
    }

    // Inverse discrete transform of the uniformly sampled complex frequency response.
    // The absolute start frequency contributes only a carrier rotation; the delay-envelope
    // magnitude and an FFT round-trip use the baseband-index representation used here.
    const double invN = 1.0 / static_cast<double>(n);
    for (std::size_t m = 0; m < n; ++m)
    {
        std::complex<double> sum{0.0, 0.0};
        for (std::size_t k = 0; k < n; ++k)
        {
            const double a = 2.0 * Pi * static_cast<double>(k * m) / static_cast<double>(n);
            sum += weighted[k] * std::complex<double>(std::cos(a), std::sin(a));
        }
        out.timeResponse[m] = sum * invN;
        out.timeS[m] = static_cast<double>(m) * out.timeStepS;
    }

    out.valid = true;
    return out;
}

std::vector<double> gateWeights(const std::vector<double> &timeS, const Gate &gate)
{
    std::vector<double> weights(timeS.size(), 0.0);
    double start = gate.startS, stop = gate.stopS;
    if (stop < start) std::swap(start, stop);
    for (std::size_t i = 0; i < timeS.size(); ++i)
    {
        const double inside = insideGateWeight(timeS[i], start, stop, std::max(0.0, gate.transitionS), gate.edge);
        weights[i] = gate.mode == GateMode::KeepInside ? inside : 1.0 - inside;
    }
    return weights;
}

std::vector<std::complex<double>> applyGate(const std::vector<double> &timeS,
                                            const std::vector<std::complex<double>> &timeResponse,
                                            const Gate &gate)
{
    if (timeS.size() != timeResponse.size()) return {};
    const auto weights = gateWeights(timeS, gate);
    std::vector<std::complex<double>> out(timeResponse.size());
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = timeResponse[i] * weights[i];
    return out;
}

std::vector<std::complex<double>> toFrequencyDomain(const std::vector<std::complex<double>> &timeResponse)
{
    const std::size_t n = timeResponse.size();
    std::vector<std::complex<double>> out(n, {0.0, 0.0});
    if (n == 0) return out;
    for (std::size_t k = 0; k < n; ++k)
    {
        std::complex<double> sum{0.0, 0.0};
        for (std::size_t m = 0; m < n; ++m)
        {
            const double a = -2.0 * Pi * static_cast<double>(k * m) / static_cast<double>(n);
            sum += timeResponse[m] * std::complex<double>(std::cos(a), std::sin(a));
        }
        out[k] = sum;
    }
    return out;
}

std::vector<Peak> findPeaks(const TransformResult &result,
                            double propagationSpeedMps,
                            bool reflection,
                            std::size_t maxPeaks,
                            std::size_t minimumSeparationSamples)
{
    std::vector<Peak> candidates;
    if (!result.valid || result.timeResponse.empty() || !(propagationSpeedMps > 0.0) || maxPeaks == 0) return candidates;
    const std::size_t n = result.timeResponse.size();
    auto mag = [&](std::size_t i) { return std::abs(result.timeResponse[i]); };

    for (std::size_t i = 0; i < n; ++i)
    {
        const double m = mag(i);
        const double left = i == 0 ? -1.0 : mag(i - 1);
        const double right = i + 1 >= n ? -1.0 : mag(i + 1);
        if (m >= left && m >= right && std::isfinite(m))
        {
            Peak p;
            p.index = i;
            p.timeS = result.timeS[i];
            p.distanceM = distanceFromDelay(p.timeS, propagationSpeedMps, reflection);
            p.magnitude = m;
            p.magnitudeDb = 20.0 * std::log10(std::max(1e-15, m));
            candidates.push_back(p);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Peak &a, const Peak &b) { return a.magnitude > b.magnitude; });
    std::vector<Peak> selected;
    for (const auto &candidate : candidates)
    {
        bool separated = true;
        for (const auto &kept : selected)
        {
            const std::size_t d = candidate.index > kept.index ? candidate.index - kept.index : kept.index - candidate.index;
            if (d < std::max<std::size_t>(1, minimumSeparationSamples)) { separated = false; break; }
        }
        if (separated) selected.push_back(candidate);
        if (selected.size() >= maxPeaks) break;
    }
    std::sort(selected.begin(), selected.end(), [](const Peak &a, const Peak &b) { return a.timeS < b.timeS; });
    return selected;
}

double propagationSpeedFromVelocityFactor(double velocityFactor)
{
    return C0 * std::max(0.0, velocityFactor);
}

double propagationSpeedFromEffectiveMedium(double epsilonEffective, double muEffective)
{
    if (!(epsilonEffective > 0.0) || !(muEffective > 0.0)) return std::numeric_limits<double>::quiet_NaN();
    return C0 / std::sqrt(epsilonEffective * muEffective);
}

double distanceFromDelay(double delayS, double propagationSpeedMps, bool reflection)
{
    return delayS * propagationSpeedMps / (reflection ? 2.0 : 1.0);
}

double nominalDistanceResolution(double frequencySpanHz, double propagationSpeedMps, bool reflection)
{
    if (!(frequencySpanHz > 0.0) || !(propagationSpeedMps > 0.0)) return std::numeric_limits<double>::quiet_NaN();
    return propagationSpeedMps / ((reflection ? 2.0 : 1.0) * frequencySpanHz);
}

double unambiguousDistance(double frequencyStepHz, double propagationSpeedMps, bool reflection)
{
    if (!(frequencyStepHz > 0.0) || !(propagationSpeedMps > 0.0)) return std::numeric_limits<double>::quiet_NaN();
    return propagationSpeedMps / ((reflection ? 2.0 : 1.0) * frequencyStepHz);
}

} // namespace VnaTimeDomain

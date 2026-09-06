#include "fdtd_solver.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace FDTD
{
namespace
{
constexpr double pi = 3.141592653589793238462643383279502884;
constexpr double eta0 = 376.730313668;
constexpr double tiny = 1.0e-30;

double clampReflection(double r)
{
    return std::clamp(r, 1.0e-12, 0.1);
}
}

Solver2D::Solver2D()
{
    Source source;
    source.name = "S1";
    source.x = m_nx / 6;
    source.y = m_ny / 2;
    m_sources.push_back(source);
    configure(m_nx, m_ny, m_dxM, m_dyM, m_courant, m_boundary);
}

bool Solver2D::configure(int nx, int ny, double dxM, double dyM,
                         double courant, BoundaryCondition boundary)
{
    if (nx < 12 || ny < 12 || !(dxM > 0.0) || !(dyM > 0.0))
        return false;

    m_nx = nx;
    m_ny = ny;
    m_dxM = dxM;
    m_dyM = dyM;
    m_courant = std::clamp(courant, 0.05, 0.999);
    m_boundary = boundary;
    m_cpmlCells = std::clamp(m_cpmlCells, 2, std::max(2, std::min(m_nx, m_ny) / 3));

    // Positive eps_r/mu_r are enforced by the GUI/model, so vacuum is the
    // fastest supported medium and therefore supplies a conservative CFL dt.
    const double inverseLength = std::sqrt(1.0 / (m_dxM * m_dxM) +
                                           1.0 / (m_dyM * m_dyM));
    m_dtS = m_courant / (c0 * inverseLength);

    const std::size_t n = static_cast<std::size_t>(m_nx) * static_cast<std::size_t>(m_ny);
    m_ex.assign(n, 0.0);
    m_ey.assign(n, 0.0);
    m_ez.assign(n, 0.0);
    m_hx.assign(n, 0.0);
    m_hy.assign(n, 0.0);
    m_hz.assign(n, 0.0);
    m_ceze.assign(n, 1.0);
    m_cezh.assign(n, 0.0);
    m_materials.assign(n, Material{});

    m_psiHxY.assign(n, 0.0);
    m_psiHyX.assign(n, 0.0);
    m_psiEzX.assign(n, 0.0);
    m_psiEzY.assign(n, 0.0);
    m_psiExY.assign(n, 0.0);
    m_psiEyX.assign(n, 0.0);
    m_psiHzX.assign(n, 0.0);
    m_psiHzY.assign(n, 0.0);

    ensurePrimarySource();
    for (auto &source : m_sources)
        source = sanitizedSource(source);
    setGuideModeSource(m_guideModeSource);

    rebuildCpmlProfiles();
    rebuildCoefficients();
    resetFields();
    return true;
}

Material Solver2D::sanitizedMaterial(Material material) const
{
    material.epsilonR = std::max(1.0e-6, material.epsilonR);
    material.muR = std::max(1.0e-6, material.muR);
    material.conductivitySPerM = std::max(0.0, material.conductivitySPerM);
    return material;
}

Source Solver2D::sanitizedSource(Source source) const
{
    source.x = std::clamp(source.x, 1, m_nx - 2);
    source.y = std::clamp(source.y, 1, m_ny - 2);
    source.frequencyHz = std::max(1.0, source.frequencyHz);
    if (!std::isfinite(source.amplitude)) source.amplitude = 0.0;
    if (!std::isfinite(source.phaseDeg)) source.phaseDeg = 0.0;
    if (source.name.empty()) source.name = "Source";
    return source;
}

void Solver2D::ensurePrimarySource()
{
    if (m_sources.empty())
    {
        Source source;
        source.name = "S1";
        source.x = std::max(1, m_nx / 6);
        source.y = m_ny / 2;
        m_sources.push_back(source);
    }
}

void Solver2D::setPolarization(Polarization polarization)
{
    if (m_polarization == polarization) return;
    m_polarization = polarization;
    resetFields();
}

void Solver2D::setBoundaryCondition(BoundaryCondition boundary)
{
    if (m_boundary == boundary) return;
    m_boundary = boundary;
    clearCpmlMemory();
}

void Solver2D::setCpmlCells(int cells)
{
    const int maxCells = std::max(2, std::min(m_nx, m_ny) / 3);
    cells = std::clamp(cells, 2, maxCells);
    if (m_cpmlCells == cells) return;
    m_cpmlCells = cells;
    rebuildCpmlProfiles();
    clearCpmlMemory();
}

void Solver2D::setCpmlTargetReflection(double reflection)
{
    reflection = clampReflection(reflection);
    if (std::abs(m_cpmlTargetReflection - reflection) < 1.0e-16) return;
    m_cpmlTargetReflection = reflection;
    rebuildCpmlProfiles();
    clearCpmlMemory();
}

void Solver2D::clearMaterials()
{
    std::fill(m_materials.begin(), m_materials.end(), Material{});
    rebuildCoefficients();
}

void Solver2D::setMaterialCell(int x, int y, const Material &material)
{
    if (!inside(x, y)) return;
    const auto k = static_cast<std::size_t>(index(x, y));
    m_materials[k] = sanitizedMaterial(material);
    rebuildCoefficientAt(x, y);
    if (m_materials[k].pec)
    {
        m_ex[k] = m_ey[k] = m_ez[k] = 0.0;
    }
}

void Solver2D::setMaterialRect(int x0, int y0, int x1, int y1, const Material &material)
{
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    x0 = std::clamp(x0, 0, m_nx - 1);
    x1 = std::clamp(x1, 0, m_nx - 1);
    y0 = std::clamp(y0, 0, m_ny - 1);
    y1 = std::clamp(y1, 0, m_ny - 1);
    const Material m = sanitizedMaterial(material);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
        {
            const auto k = static_cast<std::size_t>(index(x, y));
            m_materials[k] = m;
            if (m.pec) m_ex[k] = m_ey[k] = m_ez[k] = 0.0;
        }
    rebuildCoefficients();
}

Material Solver2D::materialAt(int x, int y) const
{
    if (!inside(x, y)) return {};
    return m_materials[static_cast<std::size_t>(index(x, y))];
}

void Solver2D::resetFields()
{
    std::fill(m_ex.begin(), m_ex.end(), 0.0);
    std::fill(m_ey.begin(), m_ey.end(), 0.0);
    std::fill(m_ez.begin(), m_ez.end(), 0.0);
    std::fill(m_hx.begin(), m_hx.end(), 0.0);
    std::fill(m_hy.begin(), m_hy.end(), 0.0);
    std::fill(m_hz.begin(), m_hz.end(), 0.0);
    clearCpmlMemory();
    m_timeS = 0.0;
    m_stepCount = 0;
}

Source Solver2D::source(std::size_t index) const
{
    if (index >= m_sources.size()) return {};
    return m_sources[index];
}

std::size_t Solver2D::addSource(const Source &sourceValue)
{
    m_sources.push_back(sanitizedSource(sourceValue));
    return m_sources.size() - 1;
}

bool Solver2D::setSource(std::size_t sourceIndex, const Source &sourceValue)
{
    if (sourceIndex >= m_sources.size()) return false;
    m_sources[sourceIndex] = sanitizedSource(sourceValue);
    return true;
}

bool Solver2D::removeSource(std::size_t sourceIndex)
{
    if (sourceIndex >= m_sources.size()) return false;
    m_sources.erase(m_sources.begin() + static_cast<std::ptrdiff_t>(sourceIndex));
    ensurePrimarySource();
    return true;
}

void Solver2D::clearSources()
{
    m_sources.clear();
    ensurePrimarySource();
}

void Solver2D::setGuideModeSource(const GuideModeSource &sourceValue)
{
    GuideModeSource source = sourceValue;
    source.x = std::clamp(source.x, 1, m_nx - 2);
    source.yStart = std::clamp(source.yStart, 1, m_ny - 2);
    source.yEnd = std::clamp(source.yEnd, 1, m_ny - 2);
    if (source.yEnd < source.yStart) std::swap(source.yStart, source.yEnd);
    source.modeIndex = std::max(0, source.modeIndex);
    source.frequencyHz = std::max(1.0, source.frequencyHz);
    if (!std::isfinite(source.amplitude)) source.amplitude = 0.0;
    if (!std::isfinite(source.phaseDeg)) source.phaseDeg = 0.0;
    m_guideModeSource = source;
}

void Solver2D::clearGuideModeSource()
{
    m_guideModeSource.enabled = false;
}

void Solver2D::setSourceCell(int x, int y)
{
    ensurePrimarySource();
    m_sources[0].x = std::clamp(x, 1, m_nx - 2);
    m_sources[0].y = std::clamp(y, 1, m_ny - 2);
}

int Solver2D::sourceX() const noexcept
{
    return m_sources.empty() ? 1 : m_sources.front().x;
}

int Solver2D::sourceY() const noexcept
{
    return m_sources.empty() ? 1 : m_sources.front().y;
}

void Solver2D::setSourceWaveform(SourceWaveform waveform) noexcept
{
    ensurePrimarySource();
    m_sources[0].waveform = waveform;
}

SourceWaveform Solver2D::sourceWaveform() const noexcept
{
    return m_sources.empty() ? SourceWaveform::ContinuousSine : m_sources.front().waveform;
}

void Solver2D::setSourceFrequencyHz(double frequencyHz) noexcept
{
    ensurePrimarySource();
    m_sources[0].frequencyHz = std::max(1.0, frequencyHz);
}

double Solver2D::sourceFrequencyHz() const noexcept
{
    return m_sources.empty() ? 1.0 : m_sources.front().frequencyHz;
}

void Solver2D::setSourceAmplitudeVPerM(double amplitude) noexcept
{
    ensurePrimarySource();
    m_sources[0].amplitude = std::isfinite(amplitude) ? amplitude : 0.0;
}

double Solver2D::sourceAmplitudeVPerM() const noexcept
{
    return m_sources.empty() ? 0.0 : m_sources.front().amplitude;
}

void Solver2D::rebuildCoefficientAt(int x, int y)
{
    if (!inside(x, y)) return;
    const auto k = static_cast<std::size_t>(index(x, y));
    const Material m = sanitizedMaterial(m_materials[k]);
    const double eps = epsilon0 * m.epsilonR;
    const double loss = m.conductivitySPerM * m_dtS / (2.0 * eps);
    m_ceze[k] = m.pec ? 0.0 : (1.0 - loss) / (1.0 + loss);
    m_cezh[k] = m.pec ? 0.0 : (m_dtS / eps) / (1.0 + loss);
}

void Solver2D::rebuildCoefficients()
{
    for (int y = 0; y < m_ny; ++y)
        for (int x = 0; x < m_nx; ++x)
            rebuildCoefficientAt(x, y);
}

void Solver2D::rebuildCpmlProfiles()
{
    auto buildAxis = [&](int n, double delta,
                         std::vector<double> &kappaE, std::vector<double> &bE, std::vector<double> &cE,
                         std::vector<double> &kappaH, std::vector<double> &bH, std::vector<double> &cH)
    {
        kappaE.assign(static_cast<std::size_t>(n), 1.0);
        bE.assign(static_cast<std::size_t>(n), 1.0);
        cE.assign(static_cast<std::size_t>(n), 0.0);
        kappaH.assign(static_cast<std::size_t>(n), 1.0);
        bH.assign(static_cast<std::size_t>(n), 1.0);
        cH.assign(static_cast<std::size_t>(n), 0.0);

        const int npml = std::clamp(m_cpmlCells, 2, std::max(2, n / 3));
        const double thickness = npml * delta;
        const double reflection = clampReflection(m_cpmlTargetReflection);
        const double sigmaMaxE = -(m_cpmlPolynomialOrder + 1.0) * std::log(reflection) /
                                 (2.0 * eta0 * std::max(thickness, tiny));
        const double alphaMaxE = 0.05 * sigmaMaxE;
        const double magneticScale = mu0 / epsilon0;

        for (int i = 0; i < n; ++i)
        {
            double depth = 0.0;
            if (i < npml)
                depth = double(npml - i) / double(npml);
            else if (i >= n - npml)
                depth = double(i - (n - npml - 1)) / double(npml);
            if (!(depth > 0.0)) continue;

            depth = std::clamp(depth, 0.0, 1.0);
            const double graded = std::pow(depth, m_cpmlPolynomialOrder);
            const double kappa = 1.0 + (m_cpmlKappaMax - 1.0) * graded;
            const double sigmaE = sigmaMaxE * graded;
            const double alphaE = alphaMaxE * (1.0 - depth);
            const double sigmaH = sigmaE * magneticScale;
            const double alphaH = alphaE * magneticScale;

            const auto compute = [&](double sigma, double alpha, double base,
                                     double &b, double &c)
            {
                b = std::exp(-((sigma / kappa) + alpha) * m_dtS / base);
                const double denominator = sigma * kappa + kappa * kappa * alpha;
                c = std::abs(denominator) > tiny ? sigma * (b - 1.0) / denominator : 0.0;
            };

            kappaE[static_cast<std::size_t>(i)] = kappa;
            kappaH[static_cast<std::size_t>(i)] = kappa;
            compute(sigmaE, alphaE, epsilon0,
                    bE[static_cast<std::size_t>(i)], cE[static_cast<std::size_t>(i)]);
            compute(sigmaH, alphaH, mu0,
                    bH[static_cast<std::size_t>(i)], cH[static_cast<std::size_t>(i)]);
        }
    };

    buildAxis(m_nx, m_dxM, m_kappaXE, m_bXE, m_cXE, m_kappaXH, m_bXH, m_cXH);
    buildAxis(m_ny, m_dyM, m_kappaYE, m_bYE, m_cYE, m_kappaYH, m_bYH, m_cYH);
}

void Solver2D::clearCpmlMemory()
{
    auto clear = [](std::vector<double> &v) { std::fill(v.begin(), v.end(), 0.0); };
    clear(m_psiHxY); clear(m_psiHyX); clear(m_psiEzX); clear(m_psiEzY);
    clear(m_psiExY); clear(m_psiEyX); clear(m_psiHzX); clear(m_psiHzY);
}

double Solver2D::sourceValue(const Source &source, double timeS) const noexcept
{
    const double f = std::max(1.0, source.frequencyHz);
    const double phase = source.phaseDeg * pi / 180.0;
    const double w = 2.0 * pi * f;
    if (source.waveform == SourceWaveform::ContinuousSine)
        return source.amplitude * std::sin(w * timeS + phase);

    if (source.waveform == SourceWaveform::GaussianModulatedSine)
    {
        const double t0 = 4.0 / f;
        const double tau = 1.25 / f;
        const double u = (timeS - t0) / tau;
        return source.amplitude * std::exp(-u * u) * std::sin(w * (timeS - t0) + phase);
    }

    const double t0 = 3.0 / f;
    const double a = pi * f * (timeS - t0);
    const double a2 = a * a;
    return source.amplitude * (1.0 - 2.0 * a2) * std::exp(-a2) * std::cos(phase);
}

void Solver2D::injectSources()
{
    const double t = m_timeS + m_dtS;
    for (const auto &sourceRaw : m_sources)
    {
        if (!sourceRaw.enabled) continue;
        const Source source = sanitizedSource(sourceRaw);
        const auto k = static_cast<std::size_t>(index(source.x, source.y));
        if (m_materials[k].pec) continue;
        const double value = sourceValue(source, t);
        double &target = m_polarization == Polarization::TMz ? m_ez[k] : m_hz[k];
        if (source.injection == SourceInjection::Hard) target = value;
        else target += value;
    }

    if (m_guideModeSource.enabled)
    {
        GuideModeSource gs = m_guideModeSource;
        setGuideModeSource(gs);
        gs = m_guideModeSource;
        const int span = std::max(1, gs.yEnd - gs.yStart);
        int mode = gs.modeIndex;
        if (m_polarization == Polarization::TMz) mode = std::max(1, mode);

        Source waveformSource;
        waveformSource.frequencyHz = gs.frequencyHz;
        waveformSource.amplitude = gs.amplitude;
        waveformSource.phaseDeg = gs.phaseDeg;
        waveformSource.waveform = gs.waveform;
        waveformSource.injection = gs.injection;
        const double temporal = sourceValue(waveformSource, t);

        for (int y = gs.yStart; y <= gs.yEnd; ++y)
        {
            const double u = double(y - gs.yStart + 1) / double(span + 2);
            const double profile = m_polarization == Polarization::TMz
                ? std::sin(double(mode) * pi * u)
                : std::cos(double(mode) * pi * u);
            const auto k = static_cast<std::size_t>(index(gs.x, y));
            if (m_materials[k].pec) continue;
            double &target = m_polarization == Polarization::TMz ? m_ez[k] : m_hz[k];
            const double value = temporal * profile;
            if (gs.injection == SourceInjection::Hard) target = value;
            else target += value;
        }
    }
}


void Solver2D::step(int count)
{
    count = std::max(1, count);
    for (int iteration = 0; iteration < count; ++iteration)
    {
        if (m_polarization == Polarization::TMz) stepTMz();
        else stepTEz();
        injectSources();
        enforceInteriorPec();
        m_timeS += m_dtS;
        ++m_stepCount;
    }
}

void Solver2D::stepTMz()
{
    // Hx = Hx - dt/mu * dEz/dy
    for (int y = 0; y < m_ny - 1; ++y)
        for (int x = 0; x < m_nx; ++x)
        {
            const auto k = static_cast<std::size_t>(index(x, y));
            const auto ku = static_cast<std::size_t>(index(x, y + 1));
            const double mu = mu0 * std::max(1.0e-6, 0.5 * (m_materials[k].muR + m_materials[ku].muR));
            const double derivative = (m_ez[ku] - m_ez[k]) / m_dyM;
            double effective = derivative;
            if (m_boundary == BoundaryCondition::Cpml)
            {
                m_psiHxY[k] = m_bYH[static_cast<std::size_t>(y)] * m_psiHxY[k] +
                              m_cYH[static_cast<std::size_t>(y)] * derivative;
                effective = derivative / m_kappaYH[static_cast<std::size_t>(y)] + m_psiHxY[k];
            }
            m_hx[k] -= (m_dtS / mu) * effective;
        }

    // Hy = Hy + dt/mu * dEz/dx
    for (int y = 0; y < m_ny; ++y)
        for (int x = 0; x < m_nx - 1; ++x)
        {
            const auto k = static_cast<std::size_t>(index(x, y));
            const auto kr = static_cast<std::size_t>(index(x + 1, y));
            const double mu = mu0 * std::max(1.0e-6, 0.5 * (m_materials[k].muR + m_materials[kr].muR));
            const double derivative = (m_ez[kr] - m_ez[k]) / m_dxM;
            double effective = derivative;
            if (m_boundary == BoundaryCondition::Cpml)
            {
                m_psiHyX[k] = m_bXH[static_cast<std::size_t>(x)] * m_psiHyX[k] +
                              m_cXH[static_cast<std::size_t>(x)] * derivative;
                effective = derivative / m_kappaXH[static_cast<std::size_t>(x)] + m_psiHyX[k];
            }
            m_hy[k] += (m_dtS / mu) * effective;
        }

    const std::vector<double> oldScalar = m_ez;
    for (int y = 1; y < m_ny - 1; ++y)
        for (int x = 1; x < m_nx - 1; ++x)
        {
            const auto k = static_cast<std::size_t>(index(x, y));
            if (m_materials[k].pec) { m_ez[k] = 0.0; continue; }
            const auto kl = static_cast<std::size_t>(index(x - 1, y));
            const auto kd = static_cast<std::size_t>(index(x, y - 1));
            const double dHydx = (m_hy[k] - m_hy[kl]) / m_dxM;
            const double dHxdy = (m_hx[k] - m_hx[kd]) / m_dyM;
            double effX = dHydx, effY = dHxdy;
            if (m_boundary == BoundaryCondition::Cpml)
            {
                m_psiEzX[k] = m_bXE[static_cast<std::size_t>(x)] * m_psiEzX[k] +
                              m_cXE[static_cast<std::size_t>(x)] * dHydx;
                m_psiEzY[k] = m_bYE[static_cast<std::size_t>(y)] * m_psiEzY[k] +
                              m_cYE[static_cast<std::size_t>(y)] * dHxdy;
                effX = dHydx / m_kappaXE[static_cast<std::size_t>(x)] + m_psiEzX[k];
                effY = dHxdy / m_kappaYE[static_cast<std::size_t>(y)] + m_psiEzY[k];
            }
            m_ez[k] = m_ceze[k] * m_ez[k] + m_cezh[k] * (effX - effY);
        }

    if (m_boundary == BoundaryCondition::MurFirstOrder)
        applyMurBoundary(m_ez, oldScalar);
    else if (m_boundary == BoundaryCondition::Pec)
        applyPecOuterBoundary();
    else
        applyCpmlOuterBoundary();
}

void Solver2D::stepTEz()
{
    // Ex = Ex + dt/eps * dHz/dy
    // Ey = Ey - dt/eps * dHz/dx
    for (int y = 1; y < m_ny; ++y)
        for (int x = 0; x < m_nx; ++x)
        {
            const auto k = static_cast<std::size_t>(index(x, y));
            if (m_materials[k].pec) { m_ex[k] = 0.0; continue; }
            const auto kd = static_cast<std::size_t>(index(x, y - 1));
            const double derivative = (m_hz[k] - m_hz[kd]) / m_dyM;
            double effective = derivative;
            if (m_boundary == BoundaryCondition::Cpml)
            {
                m_psiExY[k] = m_bYE[static_cast<std::size_t>(y)] * m_psiExY[k] +
                              m_cYE[static_cast<std::size_t>(y)] * derivative;
                effective = derivative / m_kappaYE[static_cast<std::size_t>(y)] + m_psiExY[k];
            }
            m_ex[k] = m_ceze[k] * m_ex[k] + m_cezh[k] * effective;
        }

    for (int y = 0; y < m_ny; ++y)
        for (int x = 1; x < m_nx; ++x)
        {
            const auto k = static_cast<std::size_t>(index(x, y));
            if (m_materials[k].pec) { m_ey[k] = 0.0; continue; }
            const auto kl = static_cast<std::size_t>(index(x - 1, y));
            const double derivative = (m_hz[k] - m_hz[kl]) / m_dxM;
            double effective = derivative;
            if (m_boundary == BoundaryCondition::Cpml)
            {
                m_psiEyX[k] = m_bXE[static_cast<std::size_t>(x)] * m_psiEyX[k] +
                              m_cXE[static_cast<std::size_t>(x)] * derivative;
                effective = derivative / m_kappaXE[static_cast<std::size_t>(x)] + m_psiEyX[k];
            }
            m_ey[k] = m_ceze[k] * m_ey[k] - m_cezh[k] * effective;
        }

    const std::vector<double> oldScalar = m_hz;
    for (int y = 0; y < m_ny - 1; ++y)
        for (int x = 0; x < m_nx - 1; ++x)
        {
            const auto k = static_cast<std::size_t>(index(x, y));
            const auto ku = static_cast<std::size_t>(index(x, y + 1));
            const auto kr = static_cast<std::size_t>(index(x + 1, y));
            const double mu = mu0 * std::max(1.0e-6, m_materials[k].muR);
            const double dExdy = (m_ex[ku] - m_ex[k]) / m_dyM;
            const double dEydx = (m_ey[kr] - m_ey[k]) / m_dxM;
            double effY = dExdy, effX = dEydx;
            if (m_boundary == BoundaryCondition::Cpml)
            {
                m_psiHzY[k] = m_bYH[static_cast<std::size_t>(y)] * m_psiHzY[k] +
                              m_cYH[static_cast<std::size_t>(y)] * dExdy;
                m_psiHzX[k] = m_bXH[static_cast<std::size_t>(x)] * m_psiHzX[k] +
                              m_cXH[static_cast<std::size_t>(x)] * dEydx;
                effY = dExdy / m_kappaYH[static_cast<std::size_t>(y)] + m_psiHzY[k];
                effX = dEydx / m_kappaXH[static_cast<std::size_t>(x)] + m_psiHzX[k];
            }
            m_hz[k] += (m_dtS / mu) * (effY - effX);
        }

    if (m_boundary == BoundaryCondition::MurFirstOrder)
        applyMurBoundary(m_hz, oldScalar);
    else if (m_boundary == BoundaryCondition::Pec)
        applyPecOuterBoundary();
    else
        applyCpmlOuterBoundary();
}

void Solver2D::applyMurBoundary(std::vector<double> &scalar, const std::vector<double> &oldScalar)
{
    const double kx = (c0 * m_dtS - m_dxM) / (c0 * m_dtS + m_dxM);
    const double ky = (c0 * m_dtS - m_dyM) / (c0 * m_dtS + m_dyM);

    for (int y = 1; y < m_ny - 1; ++y)
    {
        const auto l = static_cast<std::size_t>(index(0, y));
        const auto l1 = static_cast<std::size_t>(index(1, y));
        const auto r = static_cast<std::size_t>(index(m_nx - 1, y));
        const auto r1 = static_cast<std::size_t>(index(m_nx - 2, y));
        scalar[l] = oldScalar[l1] + kx * (scalar[l1] - oldScalar[l]);
        scalar[r] = oldScalar[r1] + kx * (scalar[r1] - oldScalar[r]);
    }
    for (int x = 1; x < m_nx - 1; ++x)
    {
        const auto b = static_cast<std::size_t>(index(x, 0));
        const auto b1 = static_cast<std::size_t>(index(x, 1));
        const auto t = static_cast<std::size_t>(index(x, m_ny - 1));
        const auto t1 = static_cast<std::size_t>(index(x, m_ny - 2));
        scalar[b] = oldScalar[b1] + ky * (scalar[b1] - oldScalar[b]);
        scalar[t] = oldScalar[t1] + ky * (scalar[t1] - oldScalar[t]);
    }
    scalar[static_cast<std::size_t>(index(0, 0))] = 0.0;
    scalar[static_cast<std::size_t>(index(m_nx - 1, 0))] = 0.0;
    scalar[static_cast<std::size_t>(index(0, m_ny - 1))] = 0.0;
    scalar[static_cast<std::size_t>(index(m_nx - 1, m_ny - 1))] = 0.0;
}

void Solver2D::applyPecOuterBoundary()
{
    if (m_polarization == Polarization::TMz)
    {
        for (int x = 0; x < m_nx; ++x)
        {
            m_ez[static_cast<std::size_t>(index(x, 0))] = 0.0;
            m_ez[static_cast<std::size_t>(index(x, m_ny - 1))] = 0.0;
        }
        for (int y = 0; y < m_ny; ++y)
        {
            m_ez[static_cast<std::size_t>(index(0, y))] = 0.0;
            m_ez[static_cast<std::size_t>(index(m_nx - 1, y))] = 0.0;
        }
    }
    else
    {
        // Tangential electric field is zero at a PEC boundary.
        for (int x = 0; x < m_nx; ++x)
        {
            m_ex[static_cast<std::size_t>(index(x, 0))] = 0.0;
            m_ex[static_cast<std::size_t>(index(x, m_ny - 1))] = 0.0;
            m_ey[static_cast<std::size_t>(index(x, 0))] = 0.0;
            m_ey[static_cast<std::size_t>(index(x, m_ny - 1))] = 0.0;
        }
        for (int y = 0; y < m_ny; ++y)
        {
            m_ex[static_cast<std::size_t>(index(0, y))] = 0.0;
            m_ex[static_cast<std::size_t>(index(m_nx - 1, y))] = 0.0;
            m_ey[static_cast<std::size_t>(index(0, y))] = 0.0;
            m_ey[static_cast<std::size_t>(index(m_nx - 1, y))] = 0.0;
        }
    }
}

void Solver2D::applyCpmlOuterBoundary()
{
    // CPML handles absorption in the boundary strip. The outermost numerical
    // cells are clamped only to close the finite grid behind that absorber.
    if (m_polarization == Polarization::TMz)
    {
        for (int x = 0; x < m_nx; ++x)
        {
            m_ez[static_cast<std::size_t>(index(x, 0))] = 0.0;
            m_ez[static_cast<std::size_t>(index(x, m_ny - 1))] = 0.0;
        }
        for (int y = 0; y < m_ny; ++y)
        {
            m_ez[static_cast<std::size_t>(index(0, y))] = 0.0;
            m_ez[static_cast<std::size_t>(index(m_nx - 1, y))] = 0.0;
        }
    }
    else
    {
        applyPecOuterBoundary();
    }
}

void Solver2D::enforceInteriorPec()
{
    for (std::size_t k = 0; k < m_materials.size(); ++k)
        if (m_materials[k].pec)
        {
            m_ex[k] = 0.0;
            m_ey[k] = 0.0;
            m_ez[k] = 0.0;
        }
}

const std::vector<double> &Solver2D::primaryScalarField() const noexcept
{
    return m_polarization == Polarization::TMz ? m_ez : m_hz;
}

FieldSample Solver2D::sample(int x, int y) const
{
    FieldSample out;
    if (!inside(x, y)) return out;
    const auto k = static_cast<std::size_t>(index(x, y));
    out.exVPerM = m_ex[k];
    out.eyVPerM = m_ey[k];
    out.ezVPerM = m_ez[k];
    out.hxAPerM = m_hx[k];
    out.hyAPerM = m_hy[k];
    out.hzAPerM = m_hz[k];
    out.electricMagnitudeVPerM = std::sqrt(out.exVPerM*out.exVPerM + out.eyVPerM*out.eyVPerM + out.ezVPerM*out.ezVPerM);
    out.magneticMagnitudeAPerM = std::sqrt(out.hxAPerM*out.hxAPerM + out.hyAPerM*out.hyAPerM + out.hzAPerM*out.hzAPerM);
    out.hMagnitudeAPerM = out.magneticMagnitudeAPerM;
    const Material m = m_materials[k];
    out.energyDensityJPerM3 = 0.5 * epsilon0 * m.epsilonR * out.electricMagnitudeVPerM * out.electricMagnitudeVPerM +
                              0.5 * mu0 * m.muR * out.magneticMagnitudeAPerM * out.magneticMagnitudeAPerM;
    return out;
}

double Solver2D::maxAbsPrimaryField() const
{
    double out = 0.0;
    for (double v : primaryScalarField()) out = std::max(out, std::abs(v));
    return out;
}

double Solver2D::maxAbsEz() const
{
    double out = 0.0;
    for (double v : m_ez) out = std::max(out, std::abs(v));
    return out;
}

double Solver2D::totalEnergyPerMeterJ() const
{
    double sum = 0.0;
    for (int y = 0; y < m_ny; ++y)
        for (int x = 0; x < m_nx; ++x)
            sum += sample(x, y).energyDensityJPerM3 * m_dxM * m_dyM;
    return sum;
}

double Solver2D::wavelengthInVacuumM() const noexcept
{
    double frequency = 1.0;
    for (const auto &source : m_sources)
        if (source.enabled) { frequency = std::max(frequency, source.frequencyHz); break; }
    return c0 / frequency;
}

double Solver2D::cellsPerVacuumWavelength() const noexcept
{
    return wavelengthInVacuumM() / std::max(m_dxM, m_dyM);
}

double Solver2D::maxRefractiveIndex() const noexcept
{
    double n = 1.0;
    for (const auto &m : m_materials)
        if (!m.pec)
            n = std::max(n, std::sqrt(std::max(1.0e-12, m.epsilonR * m.muR)));
    return n;
}

double Solver2D::minimumMaterialWavelengthM() const noexcept
{
    return wavelengthInVacuumM() / std::max(1.0, maxRefractiveIndex());
}

double Solver2D::cellsPerMinimumWavelength() const noexcept
{
    return minimumMaterialWavelengthM() / std::max(m_dxM, m_dyM);
}
}

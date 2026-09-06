#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace FDTD
{
constexpr double epsilon0 = 8.8541878128e-12;
constexpr double mu0 = 1.25663706212e-6;
constexpr double c0 = 299792458.0;

struct Material
{
    double epsilonR = 1.0;
    double muR = 1.0;
    double conductivitySPerM = 0.0;
    bool pec = false;
};

enum class Polarization
{
    TMz,
    TEz
};

enum class BoundaryCondition
{
    Cpml,
    MurFirstOrder,
    Pec
};

enum class SourceWaveform
{
    ContinuousSine,
    GaussianModulatedSine,
    Ricker
};

enum class SourceInjection
{
    Soft,
    Hard
};

struct Source
{
    std::string name = "S1";
    int x = 28;
    int y = 60;
    SourceWaveform waveform = SourceWaveform::ContinuousSine;
    SourceInjection injection = SourceInjection::Soft;
    double frequencyHz = 1.0e9;
    double amplitude = 1.0;
    double phaseDeg = 0.0;
    bool enabled = true;
};


struct GuideModeSource
{
    bool enabled = false;
    int x = 24;
    int yStart = 40;
    int yEnd = 80;
    int modeIndex = 1;
    SourceWaveform waveform = SourceWaveform::ContinuousSine;
    SourceInjection injection = SourceInjection::Soft;
    double frequencyHz = 1.0e9;
    double amplitude = 1.0;
    double phaseDeg = 0.0;
};

struct FieldSample
{
    double exVPerM = 0.0;
    double eyVPerM = 0.0;
    double ezVPerM = 0.0;
    double hxAPerM = 0.0;
    double hyAPerM = 0.0;
    double hzAPerM = 0.0;
    double electricMagnitudeVPerM = 0.0;
    double magneticMagnitudeAPerM = 0.0;
    double hMagnitudeAPerM = 0.0; // compatibility alias
    double energyDensityJPerM3 = 0.0;
};

class Solver2D
{
public:
    Solver2D();

    bool configure(int nx, int ny, double dxM, double dyM,
                   double courant = 0.95,
                   BoundaryCondition boundary = BoundaryCondition::Cpml);

    int nx() const noexcept { return m_nx; }
    int ny() const noexcept { return m_ny; }
    double dxM() const noexcept { return m_dxM; }
    double dyM() const noexcept { return m_dyM; }
    double dtS() const noexcept { return m_dtS; }
    double timeS() const noexcept { return m_timeS; }
    double courant() const noexcept { return m_courant; }
    std::size_t stepCount() const noexcept { return m_stepCount; }

    Polarization polarization() const noexcept { return m_polarization; }
    void setPolarization(Polarization polarization);
    BoundaryCondition boundaryCondition() const noexcept { return m_boundary; }
    void setBoundaryCondition(BoundaryCondition boundary);

    int cpmlCells() const noexcept { return m_cpmlCells; }
    void setCpmlCells(int cells);
    double cpmlTargetReflection() const noexcept { return m_cpmlTargetReflection; }
    void setCpmlTargetReflection(double reflection);

    void clearMaterials();
    void setMaterialCell(int x, int y, const Material &material);
    void setMaterialRect(int x0, int y0, int x1, int y1, const Material &material);
    Material materialAt(int x, int y) const;

    void resetFields();

    std::size_t sourceCount() const noexcept { return m_sources.size(); }
    const std::vector<Source> &sources() const noexcept { return m_sources; }
    Source source(std::size_t index) const;
    std::size_t addSource(const Source &source);
    bool setSource(std::size_t index, const Source &source);
    bool removeSource(std::size_t index);
    void clearSources();

    GuideModeSource guideModeSource() const noexcept { return m_guideModeSource; }
    void setGuideModeSource(const GuideModeSource &source);
    void clearGuideModeSource();

    // Compatibility helpers operate on the first source.
    void setSourceCell(int x, int y);
    int sourceX() const noexcept;
    int sourceY() const noexcept;
    void setSourceWaveform(SourceWaveform waveform) noexcept;
    SourceWaveform sourceWaveform() const noexcept;
    void setSourceFrequencyHz(double frequencyHz) noexcept;
    double sourceFrequencyHz() const noexcept;
    void setSourceAmplitudeVPerM(double amplitude) noexcept;
    double sourceAmplitudeVPerM() const noexcept;

    void step(int count = 1);

    const std::vector<double> &ex() const noexcept { return m_ex; }
    const std::vector<double> &ey() const noexcept { return m_ey; }
    const std::vector<double> &ez() const noexcept { return m_ez; }
    const std::vector<double> &hx() const noexcept { return m_hx; }
    const std::vector<double> &hy() const noexcept { return m_hy; }
    const std::vector<double> &hz() const noexcept { return m_hz; }
    const std::vector<Material> &materials() const noexcept { return m_materials; }

    // Scalar component that is out of the simulation plane: Ez for TMz, Hz for TEz.
    const std::vector<double> &primaryScalarField() const noexcept;
    FieldSample sample(int x, int y) const;
    double maxAbsPrimaryField() const;
    double maxAbsEz() const;
    double totalEnergyPerMeterJ() const;
    double wavelengthInVacuumM() const noexcept;
    double cellsPerVacuumWavelength() const noexcept;
    double maxRefractiveIndex() const noexcept;
    double minimumMaterialWavelengthM() const noexcept;
    double cellsPerMinimumWavelength() const noexcept;

private:
    int index(int x, int y) const noexcept { return y * m_nx + x; }
    bool inside(int x, int y) const noexcept { return x >= 0 && y >= 0 && x < m_nx && y < m_ny; }
    void ensurePrimarySource();
    Source sanitizedSource(Source source) const;
    Material sanitizedMaterial(Material material) const;

    void rebuildCoefficients();
    void rebuildCoefficientAt(int x, int y);
    void rebuildCpmlProfiles();
    void clearCpmlMemory();
    double sourceValue(const Source &source, double timeS) const noexcept;
    void injectSources();

    void stepTMz();
    void stepTEz();
    void applyMurBoundary(std::vector<double> &scalar, const std::vector<double> &oldScalar);
    void applyPecOuterBoundary();
    void applyCpmlOuterBoundary();
    void enforceInteriorPec();

    int m_nx = 161;
    int m_ny = 121;
    double m_dxM = 0.005;
    double m_dyM = 0.005;
    double m_courant = 0.95;
    double m_dtS = 0.0;
    double m_timeS = 0.0;
    std::size_t m_stepCount = 0;
    Polarization m_polarization = Polarization::TMz;
    BoundaryCondition m_boundary = BoundaryCondition::Cpml;

    int m_cpmlCells = 12;
    double m_cpmlTargetReflection = 1.0e-7;
    double m_cpmlKappaMax = 5.0;
    int m_cpmlPolynomialOrder = 3;

    std::vector<Source> m_sources;
    GuideModeSource m_guideModeSource;

    std::vector<double> m_ex;
    std::vector<double> m_ey;
    std::vector<double> m_ez;
    std::vector<double> m_hx;
    std::vector<double> m_hy;
    std::vector<double> m_hz;
    std::vector<double> m_ceze;
    std::vector<double> m_cezh;
    std::vector<Material> m_materials;

    // CPML axis profiles. E/H variants use matched electric/magnetic conductivities.
    std::vector<double> m_kappaXE, m_bXE, m_cXE;
    std::vector<double> m_kappaXH, m_bXH, m_cXH;
    std::vector<double> m_kappaYE, m_bYE, m_cYE;
    std::vector<double> m_kappaYH, m_bYH, m_cYH;

    // CPML convolution memories for TMz.
    std::vector<double> m_psiHxY, m_psiHyX, m_psiEzX, m_psiEzY;
    // CPML convolution memories for TEz.
    std::vector<double> m_psiExY, m_psiEyX, m_psiHzX, m_psiHzY;
};

// Historical name kept so projects generated by QTsignalApp 1.7 remain source-compatible.
using Solver2DTMz = Solver2D;
}

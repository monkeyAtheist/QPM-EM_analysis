#pragma once

#include "numerical_em_solvers.h"
#include "pec_surface_mom.h"

#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace HybridWireSurfaceMom
{

struct DielectricRegion
{
    std::string name;
    NumericalEM::Vec3 centerM{};
    NumericalEM::Vec3 uAxis{1.0,0.0,0.0};
    NumericalEM::Vec3 vAxis{0.0,1.0,0.0};
    NumericalEM::Vec3 normalAxis{0.0,0.0,1.0};
    double widthM = 0.10;
    double heightM = 0.10;
    double thicknessM = 0.0016;
    double relativePermittivity = 4.2;
    double lossTangent = 0.02;
    // Fraction of the dielectric contrast applied to pair interactions whose
    // midpoint lies in this region. 1 = homogeneous-filled Green function;
    // ~0.5..0.8 is a useful microstrip/open-field engineering approximation.
    double fieldFillFactor = 0.65;
};

enum class PortReferenceModel
{
    AntennaPlane = 0,
    CoaxialReferencePlane = 1
};

enum class DielectricKernelModel
{
    MidpointFill = 0,
    SegmentOverlapWeighted = 1,
    // Quasi-static layered-slab correction for RWG surface charge interactions.
    // The PEC sheets remain explicit MoM unknowns; this model therefore accounts
    // only for dielectric interfaces/fringing and does not add a PEC image plane.
    LayeredSlabQuasiStatic = 2,
    // Frequency-domain scalar-potential correction obtained from a Sommerfeld/Hankel
    // transform of the finite-slab TM spectral reflection/transmission coefficients.
    // The RWG vector-potential block is intentionally left on the audited dynamic
    // effective-medium kernel until the matching TE/TM vector-potential term is enabled.
    LayeredSlabSommerfeldScalar = 3,
    // 5.29: passivity-guarded Sommerfeld transition for RWG interactions on slab
    // faces. The same-face tangential magnetic-vector-potential correction uses the
    // TE spectrum of the horizontal-electric-dipole (HED) mixed-potential gauge.
    // The corresponding TE/TM HED scalar spectrum is evaluated as a diagnostic, but
    // the released matrix keeps the audited 5.26 reactive scalar projection until
    // the remaining longitudinal/transmitted potential terms pass the power tests.
    LayeredSlabSommerfeldTeVector = 4,
    // 5.30: extends the guarded layered RWG operator with the TE/TM transmitted
    // tangential dyadic between opposite slab faces. Same-face self/near terms retain
    // the audited 5.29 TE correction; far same-face completion remains guarded because
    // the raw quadrature extension degraded the patch mesh-convergence benchmark.
    // The scalar-potential block remains on the passivity-preserving reactive
    // projection pending a complete off-interface MPIE / layered far-field model.
    LayeredSlabSommerfeldCrossFace = 5,
    // 5.31: replaces the TM-only scalar transition on slab-face RWG interactions
    // with the coupled HED TE/TM longitudinal mixed-potential scalar spectrum.
    // Same-face reflection and opposite-face transmission are both evaluated in
    // the HED gauge, while only the real Green-function increment is injected so
    // the 1/(j omega) scalar EFIE contribution remains reactive. The 5.30
    // transmitted tangential vector dyadic and reciprocal RWG projection remain.
    LayeredSlabSommerfeldLongitudinalHed = 6,
    // 5.32: extends the layered spectral infrastructure away from the exact slab
    // faces into the two exterior air half-spaces. Reflection/transmission spectra
    // are propagated by exp[-gamma1(d_obs+d_src)] and are used as a guarded
    // reactive vector-potential correction in wire<->RWG mutual interactions.
    // Points inside the dielectric volume remain on the audited effective-medium
    // path until the internal-layer source/observer dyadics are completed.
    LayeredSlabSommerfeldExteriorHeight = 7,
    // 5.33: extends the guarded spectral transition to source/observer points located
    // inside the dielectric slab. A residual internal-cavity TE/TM tangential vector
    // dyadic and the matching HED tangential scalar-gradient term are enabled for
    // rooftop-wire <-> RWG mutual coupling. Normal-current/via dyadics and full-complex
    // radiative terms remain guarded until the corresponding power tests are complete.
    LayeredSlabSommerfeldInternalLayer = 8,
    // 5.34: first guarded vertical-electric-dipole (VED) / normal-current transition.
    // Internal TM cavity data are used for the normal vector-potential residual and
    // the VED scalar-potential gradient that couples vertical rooftop/via charge to
    // tangential RWG tests. The reciprocal surface-charge -> vertical-wire path uses
    // the full spatial gradient of the HED scalar residual. All new increments remain
    // reactively projected; off-diagonal full-complex VED dyadics remain guarded.
    LayeredSlabSommerfeldVedNormal = 9,
    // 5.35: completes the first pass of the internal VED/HED mixed vector dyadic
    // with reciprocal rho-z and z-rho TM components. The cross terms are obtained
    // from signed vertical derivatives of the medium-2 cavity response and a J1
    // Sommerfeld transform. They remain residual/reactively projected and the
    // existing Lorentz reciprocal wire<->RWG block average remains the final guard.
    LayeredSlabSommerfeldVedOffDiagonal = 10,
    // 5.36: monitored first full-complex lift of the wire<->RWG layered residuals.
    // HED/VED vector and scalar-gradient residuals retain their complex Sommerfeld
    // values instead of the historical real-only Green projection. The solved
    // candidate is accepted only when the driven passive-port checks remain positive;
    // otherwise solve() automatically falls back to the audited 5.35 reactive path.
    // Surface-only RWG layered terms remain on the 5.31/5.35 guarded operator.
    LayeredSlabSommerfeldComplexPowerGuard = 11,
    // 5.37: first propagating-spectrum layered far-field audit. The accepted 5.36
    // complex wire<->RWG candidate is post-processed with the finite-slab TE/TM
    // plane-wave reflection/transmission coefficients in the two exterior air
    // hemispheres. This supplies separate upper/lower propagating radiation powers
    // and a one-sided power guard. Surface-wave poles and explicit dielectric-loss
    // power remain unresolved channels and are reported as such.
    LayeredSlabSommerfeldPropagatingFarField = 12,
    // 5.38: extracts guided-mode pole candidates of the same finite-slab TE/TM
    // Sommerfeld denominators used by the 5.37 propagating far field. Pole roots
    // are refined in complex k_rho and their spectral residues are reported. The
    // explicit PEC ground is still an RWG unknown rather than part of the analytic
    // slab denominator, so pole *power* is intentionally not added to the energy
    // closure yet; doing so from the bare dielectric-slab poles would double-count
    // / mis-normalize grounded-substrate modes.
    LayeredSlabSommerfeldSurfaceWavePoleAudit = 13,
    // 5.39: grounded dielectric-slab modal audit. Unlike the 5.38 bare
    // air/dielectric/air pole scan, this mode enforces a PEC boundary at the
    // negative-normal slab face and an open air half-space at the positive face.
    // The grounded TE/TM dispersion roots and unit-interface-field modal power
    // normalization are reported. Surface-wave *excitation power* remains guarded
    // until a residue/current-overlap operator using the finite RWG ground extent is
    // validated; the closure residual is never relabelled as modal power.
    LayeredSlabSommerfeldGroundedPecModeAudit = 14
};


struct LayeredSommerfeldPoleResult
{
    std::string polarization; // "TE" or "TM"
    std::complex<double> transverseWavenumberPerM{0.0,0.0};
    double betaOverK0 = 0.0;
    double attenuationNpPerM = 0.0;
    double effectiveIndex = 0.0;
    double denominatorMagnitude = 0.0;
    double residueMagnitudePerM = 0.0;
    // 5.39 grounded-mode diagnostics. For TM, the normalization uses unit H_t
    // at the dielectric/air interface; for TE it uses unit E_t. The resulting
    // forward modal power is per metre transverse to the propagation direction.
    bool groundedPecBoundary = false;
    double modalPowerNormalizationWPerM = 0.0;
    double dielectricPowerFraction = 0.0;
    double phaseVelocityMps = 0.0;
};

struct SurfaceReferencedFeed
{
    std::string name;
    NumericalEM::Vec3 positionM{};
    std::complex<double> voltageV{1.0,0.0};
    double referenceOhm = 50.0;
    int surfaceIndex = -1;
    double mappingToleranceM = 0.01;
    // Optional connector/reference-plane embedding. The MoM excitation remains at
    // the antenna/ground-plane terminal; this transforms the extracted impedance
    // through an external coax section without pretending to solve coax fields.
    PortReferenceModel referenceModel = PortReferenceModel::AntennaPlane;
    double coaxInnerRadiusM = 0.0005;
    double coaxOuterRadiusM = 0.0017;
    double coaxRelativePermittivity = 2.1;
    double coaxLossTangent = 0.0;
    double coaxLengthM = 0.0;
};


struct DifferentialSurfaceFeed
{
    std::string name;
    NumericalEM::Vec3 positivePositionM{};
    NumericalEM::Vec3 negativePositionM{};
    std::complex<double> voltageV{1.0,0.0};
    double referenceOhm = 50.0;
    int positiveSurfaceIndex = -1;
    int negativeSurfaceIndex = -1;
    double mappingToleranceM = 0.01;
    // Gaussian terminal-potential footprint radius (1-sigma) used by the
    // differential RWG port. <=0 selects a mesh-local automatic value. A fixed
    // physical value is recommended for mesh-convergence studies.
    double footprintRadiusM = 0.0;
};

struct GalvanicJunction
{
    std::string name;
    NumericalEM::Vec3 positionM{};
    int surfaceIndex = -1;
    double mappingToleranceM = 0.01;
    double surfaceCurrentSign = 1.0;
};

struct Input
{
    NumericalEM::WireNetworkMomInput wire{};
    std::vector<PecSurfaceMom::Triangle3D> triangles;
    std::vector<DielectricRegion> dielectrics;
    std::vector<SurfaceReferencedFeed> surfaceReferencedFeeds;
    // Ideal two-surface lumped port. The voltage is applied through localized RWG
    // divergence stencils on the positive/negative PEC sheets; no thin-wire probe
    // self impedance is introduced. Intended for patch/microstrip feed studies.
    std::vector<DifferentialSurfaceFeed> differentialSurfaceFeeds;
    std::vector<GalvanicJunction> galvanicJunctions;
    int maxTotalUnknowns = 500;
    int maxSurfaceUnknowns = 350;
    double surfaceVertexMergeToleranceM = 1e-8;
    // Legacy pre-5.23 self-radius factor retained for project/API compatibility.
    // Same-triangle RWG terms now use Duffy singularity extraction.
    double surfaceSelfRegularizationFactor = 0.22;
    // Additional regularization only for wire<->surface near interactions.
    // It is expressed as a fraction of the local equivalent triangle radius.
    double mutualRegularizationFactor = 0.08;
    bool computeFarField = true;
    bool useEffectiveDielectricRegions = true;
    DielectricKernelModel dielectricKernelModel = DielectricKernelModel::SegmentOverlapWeighted;
    // Optional finite-conductivity sheet impedance for PEC surfaces. This keeps the
    // geometry as an infinitesimally thin RWG sheet but adds a local surface-impedance
    // boundary term. Copper defaults are useful for printed-antenna loss studies.
    bool useFiniteSurfaceConductivity = false;
    double surfaceConductivitySPerM = 5.8e7;
    double surfaceThicknessM = 35e-6;
    double surfaceRelativePermeability = 1.0;
    // Boundary RWG functions are still excluded globally. When enabled, a half-RWG
    // is created only for a boundary edge lying inside a requested feed/junction
    // mapping tolerance, making edge terminals possible without opening every boundary.
    bool enableTerminalHalfRwg = true;
    double farFieldCutStepDeg = 2.0;
    double farFieldIntegrationStepDeg = 5.0;
};

struct FeedResult
{
    std::string name;
    NumericalEM::Vec3 positionM{};
    std::complex<double> voltageV{0.0,0.0};
    std::complex<double> currentA{0.0,0.0};
    std::complex<double> inputImpedanceOhm{0.0,0.0};
    std::complex<double> antennaPlaneInputImpedanceOhm{0.0,0.0};
    std::complex<double> uncoupledInputImpedanceOhm{0.0,0.0};
    std::complex<double> reflectionCoefficient{0.0,0.0};
    double referenceOhm = 50.0;
    double returnLossDb = 0.0;
    double vswr = 1.0;
    bool surfaceReferenced = false;
    int mappedSurfaceIndex = -1;
    int mappedSurfaceBasisIndex = -1;
    double mappingDistanceM = 0.0;
    bool referencePlaneCorrected = false;
    double feedLineZ0Ohm = 0.0;
    double feedLineElectricalLengthDeg = 0.0;
};

struct JunctionResult
{
    std::string name;
    NumericalEM::Vec3 requestedPositionM{};
    NumericalEM::Vec3 mappedSurfaceEdgeCenterM{};
    int mappedSurfaceIndex = -1;
    int mappedSurfaceBasisIndex = -1;
    int touchingWireSegmentCount = 0;
    double surfaceMappingDistanceM = 0.0;
    double wireMappingDistanceM = 0.0;
    std::complex<double> wireCurrentSumA{0.0,0.0};
    std::complex<double> surfaceEdgeCurrentA{0.0,0.0};
    double currentMismatchA = 0.0;
};

struct Result
{
    bool valid = false;
    std::string error;
    std::string note;
    double wavelengthM = 0.0;
    int wireUnknownCount = 0;
    int wireSolvedDofCount = 0;
    int surfaceUnknownCount = 0;
    int totalUnknownCount = 0;
    int triangleCount = 0;
    int dielectricRegionCount = 0;
    int junctionConstraintCount = 0; // wire <-> PEC constraints / surface-referenced ports
    int wireBranchConstraintCount = 0; // independent internal T/Y/X KCL relations (rank)
    int redundantWireBranchConstraintCount = 0;
    bool reducedWireJunctionBasisUsed = false;
    bool wireLinearRooftopBasisUsed = false;
    bool wireRooftopFallbackUsed = false; // retained for backward-facing UI diagnostics; false in 5.16 rooftop hybrid mode
    int wireOpenEndConstraintCount = 0;
    int wireOrdinaryContinuityConstraintCount = 0;
    int wireTerminalNodeCount = 0;
    int boundaryEdgeCount = 0;
    int halfRwgUnknownCount = 0;
    double residualRelative = 0.0;
    double minPivotAbs = 0.0;
    double maxPivotAbs = 0.0;
    double peakWireCurrentA = 0.0;
    double peakSurfaceCurrentApm = 0.0;
    double acceptedPowerW = 0.0;
    double radiatedPowerW = 0.0;
    double directivityLinear = 0.0;
    double directivityDbi = 0.0;
    double powerBalanceRatio = 0.0;
    double conductorLossW = 0.0;
    double radiationEfficiency = 0.0;
    std::complex<double> surfaceImpedanceOhmPerSquare{0.0,0.0};
    // Engineering measure of how large the mutual blocks are after row normalization.
    double normalizedMutualCouplingRms = 0.0;
    // Reciprocity diagnostic for the Galerkin wire<->RWG mutual blocks. Zero is ideal;
    // finite quadrature and near-interaction regularization produce small non-zero values.
    double mutualReciprocityRelative = 0.0; // solved/post-symmetry Galerkin block
    double mutualReciprocityPreSymmetryRelative = 0.0; // raw independently integrated blocks
    // Reciprocity diagnostic of the assembled RWG surface-surface block before
    // any row scaling. Zero is ideal for reciprocal material models.
    double surfaceReciprocityRelative = 0.0;
    double surfaceReciprocityPreSymmetryRelative = 0.0;
    bool pecTerminalChargeRegularizationUsed = false;
    int pecTerminalRegularizedComponentCount = 0;
    double maxPecTerminalTransitionScaleM = 0.0;
    double maxWireOpenEndCurrentA = 0.0;
    double maxWireLineChargeCpm = 0.0;
    double netWireChargeMagnitudeC = 0.0;
    double maxJunctionCurrentMismatchA = 0.0;
    double maxWireBranchKclResidualA = 0.0;
    bool differentialSurfacePortUsed = false;
    int differentialSurfacePortCount = 0;
    double maxDifferentialPortCurrentImbalanceA = 0.0;
    double minDifferentialPortFootprintRadiusM = 0.0;
    double maxDifferentialPortFootprintRadiusM = 0.0;
    bool differentialPortSymmetricVoltageSplitUsed = false;
    bool layeredSlabQuasiStaticUsed = false;
    bool layeredSlabSommerfeldScalarUsed = false;
    bool layeredSlabSommerfeldTeVectorUsed = false;
    bool layeredSlabSommerfeldCrossFaceUsed = false;
    bool layeredSlabSommerfeldLongitudinalHedUsed = false;
    bool layeredSlabSommerfeldExteriorHeightUsed = false;
    bool layeredSlabSommerfeldInternalLayerUsed = false;
    bool layeredSlabSommerfeldVedNormalUsed = false;
    bool layeredSommerfeldExteriorWireSurfaceVectorUsed = false;
    bool layeredSommerfeldExteriorHedScalarDiagnosticEvaluated = false;
    bool layeredSommerfeldInternalWireSurfaceVectorUsed = false;
    bool layeredSommerfeldWireSurfaceScalarGradientUsed = false;
    bool layeredSommerfeldInternalNormalCurrentGuarded = false;
    bool layeredSommerfeldVedNormalVectorUsed = false;
    bool layeredSommerfeldVedScalarGradientUsed = false;
    bool layeredSommerfeldHedFullGradientUsed = false;
    bool layeredSommerfeldVedOffDiagonalGuarded = false;
    bool layeredSommerfeldVedOffDiagonalVectorUsed = false;
    bool layeredSommerfeldVedOffDiagonalReciprocityGuardUsed = false;
    bool layeredSommerfeldComplexTransitionAttempted = false;
    bool layeredSommerfeldComplexTransitionUsed = false;
    bool layeredSommerfeldComplexTransitionFallbackUsed = false;
    bool layeredSommerfeldPowerAuditUsed = false;
    bool layeredSommerfeldPowerAuditFarFieldIncomplete = false;
    // 5.37 propagating-spectrum far-field audit. This is an exterior plane-wave
    // TE/TM post-processing of the solved coherent current moment, not yet the pole
    // residue / surface-wave part of the Sommerfeld radiation operator.
    bool layeredSommerfeldPropagatingFarFieldUsed = false;
    bool layeredSommerfeldPropagatingFarFieldPowerConsistent = false;
    bool layeredSommerfeldPropagatingFarFieldGuardRejected = false;
    // 5.38 guided-mode pole audit. These are roots of the *analytic dielectric slab*
    // reflection denominators. They are useful spectral diagnostics, but because an
    // explicit PEC ground remains in the RWG matrix they are not yet assigned a
    // surface-wave power contribution.
    bool layeredSommerfeldSurfaceWavePoleAuditUsed = false;
    bool layeredSommerfeldSurfaceWavePowerResolved = false;
    bool layeredSommerfeldSurfaceWavePowerGuarded = false;
    int layeredSommerfeldSurfaceWavePoleCount = 0;
    double layeredSurfaceWavePowerW = 0.0;
    std::vector<LayeredSommerfeldPoleResult> layeredSommerfeldSurfaceWavePoles;
    // 5.39 grounded PEC modal audit. The coverage metric estimates how much of the
    // slab negative-normal face is backed by explicit RWG PEC triangles. A grounded
    // modal spectrum is reported only when a meaningful backing plane is present.
    bool layeredSommerfeldGroundedPecModeAuditUsed = false;
    bool layeredSommerfeldGroundedPecBoundaryDetected = false;
    bool layeredSommerfeldGroundedPecPowerGuarded = false;
    int layeredSommerfeldGroundedPecModeCount = 0;
    double layeredSommerfeldGroundCoverageFraction = 0.0;
    std::vector<LayeredSommerfeldPoleResult> layeredSommerfeldGroundedPecModes;
    int layeredSommerfeldFarFieldSourceSide = 0; // +1 = +normal equivalent side, -1 = -normal
    double layeredRadiatedPowerUpperW = 0.0;
    double layeredRadiatedPowerLowerW = 0.0;
    double layeredRadiatedPowerTotalW = 0.0;
    double layeredDirectivityLinear = 0.0;
    double layeredDirectivityDbi = 0.0;
    double layeredMaxRadiationThetaDeg = 0.0;
    double layeredMaxRadiationPhiDeg = 0.0;
    double layeredPowerClosureResidualW = 0.0;
    double layeredPowerClosureRelative = 0.0;
    std::vector<double> layeredAzimuthNormalizedFarField;
    std::vector<double> layeredElevationNormalizedFarField;
    std::vector<double> layeredFarFieldNormalized;
    std::complex<double> layeredSommerfeldComplexCandidateInputImpedanceOhm{0.0,0.0};
    double layeredSommerfeldComplexCandidateAcceptedPowerW = 0.0;
    double powerClosureResidualW = 0.0;
    double powerClosureRelative = 0.0;
    bool layeredSommerfeldVectorPotentialUsed = false;
    bool layeredSommerfeldCrossFaceVectorUsed = false;
    bool layeredSommerfeldCrossFaceAllPairAssemblyUsed = false;
    bool layeredSommerfeldHedScalarDiagnosticEvaluated = false;
    bool layeredSommerfeldLongitudinalHedScalarUsed = false;
    bool layeredSommerfeldCrossFaceHedScalarUsed = false;
    bool layeredSommerfeldFullComplexScalarUsed = false;
    bool layeredSommerfeldReactiveProjectionUsed = false;
    int layeredSlabRegionCount = 0;
    int layeredImageSeriesTerms = 0;
    int layeredSommerfeldRadialSamples = 0;
    int layeredSommerfeldQuadratureOrder = 0;
    double layeredSommerfeldLimitingLossTangent = 0.0;

    // 5.42-C bounded Sommerfeld-table cache diagnostics. Counters are the activity
    // attributable to this solve call. Tables are keyed by frequency and slab
    // geometry/material parameters; exterior/internal tables also include their
    // quantized vertical coordinates. Mesh-only changes intentionally reuse tables.
    std::uint64_t layeredSommerfeldCacheHits = 0;
    std::uint64_t layeredSommerfeldCacheMisses = 0;
    std::uint64_t layeredSommerfeldCacheTableBuilds = 0;
    std::uint64_t layeredSommerfeldCacheEvictions = 0;
    std::uint64_t layeredSommerfeldFaceCacheHits = 0;
    std::uint64_t layeredSommerfeldExteriorCacheHits = 0;
    std::uint64_t layeredSommerfeldInternalCacheHits = 0;
    int layeredSommerfeldFaceCacheEntries = 0;
    int layeredSommerfeldExteriorCacheEntries = 0;
    int layeredSommerfeldInternalCacheEntries = 0;
    double layeredSommerfeldCacheBuildTimeMs = 0.0;

    std::vector<NumericalEM::WireNetworkMeshSegment> wireSegments;
    std::vector<PecSurfaceMom::RwgBasisResult> surfaceBases;
    std::vector<PecSurfaceMom::TriangleCurrentResult> triangleCurrents;
    std::vector<FeedResult> feeds;
    std::vector<JunctionResult> junctions;

    // Radiation convention shared by wire/hybrid results since 5.28:
    // theta=0 deg -> +Z, theta=90 deg -> XY plane, phi=0 deg -> +X.
    // Both 2D cuts and the full-sphere samples are normalized to the same global peak.
    std::vector<double> azimuthDeg; // phi in degrees at theta=90 deg
    std::vector<double> azimuthNormalizedFarField;
    std::vector<double> elevationDeg; // spherical theta in degrees at phi=0 deg
    std::vector<double> elevationNormalizedFarField;
    std::vector<double> farFieldThetaDeg;
    std::vector<double> farFieldPhiDeg;
    std::vector<double> farFieldNormalized;
    double maxRadiationThetaDeg = 0.0;
    double maxRadiationPhiDeg = 0.0;
};

// Educational dense block-MoM coupling of the generalized thin-wire EFIE and the
// RWG Galerkin PEC-surface EFIE. Pulse/point-matching remains available; the optional
// linear rooftop mode uses a Galerkin wire-wire block and Galerkin rooftop<->RWG mutual
// blocks, including terminal half-rooftop functions where a wire is galvanically tied
// to a PEC surface. Since 5.21, single-terminal wire components use a local planar PEC
// image singularity extraction below the local RWG cell scale and current is balanced
// against integrated surface-patch divergence; multi-terminal components skip that
// half-space approximation. Since 5.22, a surface-only ideal differential lumped
// port can also drive two localized PEC terminal patches directly through the
// integrated RWG-divergence dual stencil. This is useful for separating printed-
// antenna surface/dielectric errors from the self reactance of an explicit short
// thin-wire probe; it is not a volumetric coax/via model. The mutual blocks are
// evaluated with the same free-space Green function used by each native solver.
// Intended for qualitative wire/ground-plane/reflection studies and convergence
// exercises. Optional localized dielectric regions can use the historical effective-medium
// kernels, the 5.25 quasi-static layered-slab image-series correction, the 5.26 dynamic
// Sommerfeld/Hankel TM scalar-potential correction, the 5.29 passivity-guarded TE
// vector-potential transition, the 5.30 cross-face tangential dyadic extension, the
// 5.31 coupled HED longitudinal scalar transition for RWG interactions lying on slab faces,
// the 5.32 exterior-height transition, 5.33 internal-layer HED scalar-gradient coupling,
// 5.34/5.35 VED normal and mixed rho-z/z-rho terms, the monitored 5.36 complex lift,
// the 5.37 propagating TE/TM layered far-field, the 5.38 bare-slab guided-pole audit,
// or the 5.39 grounded-PEC modal audit. Explicit PEC sheets remain in the MoM system.
// Version 5.39 uses the same PEC boundary *type* as a microstrip ground and reports
// grounded-mode normalization, but still guards excitation power until the finite RWG
// ground extent/current-overlap operator is validated.
// Galvanic wire/surface junctions are enforced
// with current-continuity constraints. Internal thin-wire T/Y/X junctions reuse the wire mesh
// topology and can either be eliminated through the rank-aware reduced current basis
// or retained as legacy KCL Lagrange constraints.
Result solve(const Input &input);

} // namespace HybridWireSurfaceMom

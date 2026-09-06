# QTsignalApp application changelog

## 6.2.0 — Calculator / Notes and Calculator → Designer workflow

- Renames the top-level `Calculator` workspace to `Calculator / Notes`.
- Adds a persistent plain-text `Notes` tab with standard undo/redo, cut/copy/paste, select-all, local autosave and `.txt`/`.md` import/export.
- Exposes the natural exponential directly in the calculator UI with `exp(x)` and `e^(x)` insertion buttons; both use the existing tested expression engine.
- Adds `Create in Antenna designer` to the antenna-sizing page. The currently calculated frequency/material/velocity-factor inputs are transferred to the Designer and a starter geometry is built automatically.
- Supports direct starter generation for dipoles, inverted-V, folded dipole, monopoles, ground-plane, loop, J-pole, Slim Jim, axial helix, 3-element Yagi, rectangular patch and parabolic reflector.
- J-pole, Slim Jim, monopole-without-ground and reflector feeds carry explicit starter/feeding warnings instead of being presented as already matched physical ports.
- Keeps `.qta` schema v19 and all validated EM solver kernels unchanged.

## 6.1.0 — top-level engineering calculator

- Added a first-class `Calculator` workspace at the top application level.
- Added bidirectional frequency/wavelength conversion with epsilon_r/mu_r medium parameters, period, omega, phase velocity and wave number.
- Added copyable common-antenna starting dimensions and inverse dimension->frequency calculations for dipoles, monopoles, ground-plane, inverted-V, folded dipole, 5/8-wave, loop, J-pole, Slim Jim, axial helix, Yagi and rectangular patch.
- Added parabolic-reflector gain/HPBW/focal-distance and required-diameter estimates without pretending that dish diameter defines a unique resonant frequency.
- Added a standalone C++17 expression engine with EM constants (`pi`, `eps0`, `mu0`, `c0`, `ke`, `eta0`, `h`, `hbar`, `qe`, `me`), engineering formula templates and copyable results.
- Added `release_calculator_6100.cpp` numerical regression coverage; no validated 6.0.1 antenna/field solver kernel is changed by this feature release.

## 6.0.1 — dipole topology and sweep reporting maintenance

- Added explicit detection of disconnected, collinear, independently fed wire arms so a two-source array cannot silently be interpreted as a canonical centre-fed dipole.
- Added electrical-length guidance: a λ/2 dipole is λ/2 TOTAL length (λ/4 per arm); the existing preset remains 0.475 λ total as a practical resonant starting length.
- Added frequency-sweep excitation modes for all configured feeds versus reported-feed-only excitation. Other disabled sources are zero impressed voltages, not matched N-port terminations.
- Multi-feed sweep plots now report `Active Γ` or `Driving Γ` instead of calling those quantities `S11`; one-port geometries retain the S11 label.
- Added coarse-sampling and unbracketed-resonance warnings to the sweep status.
- dB-family plot axes/readbacks no longer use SI prefixes such as `mdB`.
- Added a regression reproducing the reported 2.629 dBi two-source geometry and comparing it with a correct one-component, one-feed λ/2 dipole.
- Added loadable 100 MHz exact-half-wave and 0.475 λ resonant-start dipole examples.
- EM kernels and `.qta v19` persistence are unchanged from 6.0.0.

## 6.0.0 — Simulation / Antenna Designer 1.0

- Promoted the frozen 5.42 stabilization baseline to the Simulation / Antenna Designer 1.0 milestone.
- Final production-only release rerun passes dipole, loop, finite-ground monopole, printed patch and homogeneous `eps_r -> 1` via gates.
- Added `runProductionCase()` so release regression executes only frozen production paths while keeping legacy/experimental diagnostics visible in the normal validation campaign.
- Added `RELEASE_6_0_0.md`, final Gate A–E acceptance evidence and `tests/verify_6000.py`.
- Application/package version is 6.0.0; project persistence intentionally remains `.qta v19`, so 5.x project compatibility is preserved.
- No new electromagnetic solver/model family is introduced by the 6.0 promotion.

## 5.42.0 — Simulation / Designer 1.0 stabilization

### Release hygiene, equations/scope reference and acceptance matrix (5.42-D)

- Added a permanent Engineering-use notice above the Antenna Designer Results tabs. It states that antenna outputs are model-dependent numerical estimates and that critical designs require an independent 3D full-wave solver and/or measurement before engineering sign-off.
- Added `SIMULATION_ANTENNA_1_0_REFERENCE.md`, freezing the 1.0 coordinate convention, RF reporting equations, directivity/gain/realized-gain definitions, power terminology, production solver families, one-slab layered-media scope, port models, passivity behavior, convergence guidance and explicitly post-1.0 capabilities.
- Documented the common radiation convention used by the 2D, polar and 3D views: `theta` is measured from +Z; `phi` is measured in the XY plane from +X toward +Y; normalized field levels use `20 log10(|E|/|E|max)`.
- Documented the single-reference-impedance RF contract used by the feed results, sweep plots and Smith chart: `Gamma=(Zin-Z0)/(Zin+Z0)`, return loss, S11 and VSWR are all derived from the same `Z0`.
- Documented that Hybrid `eta_rad,cond` is the modeled conductor-loss efficiency `Prad/(Prad+Pcond)` and that `Prad/Paccepted` remains a separate power audit, particularly important when layered dielectric/surface-wave channels are unresolved.
- Added `SIMULATION_DESIGN_V1_ACCEPTANCE.md`, mapping every frozen Gate A–E requirement to its 5.42 evidence and explicitly separating completed implementation work from the target-machine full-regression gate.
- Added `tests/verify_542d.py` to guard the release documentation, visible engineering warning, RF/angle convention labels and the no-6.0.0-before-full-run promotion rule.
- 5.42 implementation work is now closed, but the application remains 5.42.0 until `python3 tests/verify_542b.py --full` passes on the target build machine. No new physical model family was introduced.

### Sommerfeld cache/performance and diagnostic visibility (5.42-C)

- Replaced the three thread-local Sommerfeld table caches that previously cleared their entire contents at the capacity threshold with bounded LRU caches: 32 face tables, 24 exterior-height tables and 32 internal-layer tables. This removes the periodic cache cliff during sweeps and mixed wire/RWG assemblies.
- Added a safe last-key hot path to each bounded cache. The dominant one-slab/one-frequency assembly can therefore reuse the current radial table without an ordered-map lookup on every Green-function query.
- Preserved the validated physical cache keys. Face tables are keyed by frequency plus slab thickness/extent/permittivity/loss; exterior tables additionally key the quantized height sum and side; internal tables key the reciprocal quantized source/observation depths. Mesh-only changes intentionally reuse a table because the table is a slab Green-function lookup, not a mesh-derived object.
- Added per-solve cache diagnostics to Hybrid results: hits, misses, table builds, LRU evictions, face/exterior/internal resident counts and measured table-build time. Recursive 5.36+ passive fallbacks report the aggregate activity of the attempted candidate plus the returned safe solve.
- Exposed the diagnostics in the single-frequency Hybrid result panel and aggregated hit rate/build/eviction/build-time information in the Hybrid broadband sweep status. Existing passivity/far-field fallback messages remain explicit.
- Added `tests/release_cache_542c.cpp` and `tests/verify_542c.py`. The fast contract proves reuse on an identical solve and a cache miss/rebuild after material or frequency changes; optional `--stress` fills the bounded cache and requires an LRU eviction.
- No Sommerfeld integrand, radial sampling rule, spectral quadrature, passivity guard or antenna production path was changed.

### Numerical regression and solver-contract hardening (5.42-B)

- Added solver-level NaN/Inf guards for standalone thin-wire/network MoM, PEC-surface RWG MoM and the coupled Hybrid wire/RWG path. Invalid direct API inputs are rejected with explicit errors before they can enter a matrix assembly.
- Added post-solve finite-result guards so a non-finite impedance, residual, power or radiation result cannot be returned as `valid=true` without an error.
- Added strict validation of Hybrid dielectric regions, PEC geometry, wire/feed data, differential/surface-referenced ports, galvanic junctions, finite-conductivity parameters and coax reference-plane data.
- Added `tests/release_smoke_542b.cpp` and `tests/verify_542b.py`: the fast regression is warning-clean under `-Wall -Wextra -Wpedantic -Werror`, checks RF consistency (`Zin -> Gamma -> return loss -> VSWR`), passive positive resistance/power, voltage scaling and NaN/Inf rejection.
- Added `tests/release_full_542b.cpp` for the final target-platform release run. It requires the frozen 300 MHz dipole/loop/monopole/patch campaign to remain 4/4 PASS and includes a layered via/probe homogeneous-limit power check.
- The validated electrostatic and magnetostatic kernels remain byte-identical and are hash-guarded by the 5.42-B verifier.
- The full canonical release executable is intentionally separated from the fast build smoke because the dense patch/Sommerfeld regressions are expensive; it must be executed on the target build machine before 5.42 is declared complete.

### Persistence hardening (5.42-A)

- Centralized the antenna-project schema contract at `.qta v19` and added an explicit compatibility guard before any geometry/UI state is modified.
- Future schemas are now rejected with a clear error instead of being interpreted by an older loader. Legacy unversioned projects are treated as v1 and older supported schemas are migrated in memory to v19.
- Added explicit legacy migration for `planes -> surfaces`, `sourceOhm -> referenceOhm`, missing SI `units`, and the current schema tag. The source file remains untouched until the user explicitly saves it.
- Added pre-load document validation for finite numeric values, collection types/safety limits, zero-length wires, invalid surface/dielectric dimensions, invalid feed reference impedance, invalid CAD constraint enums/ranges, group member types and malformed individual-director variables. Invalid projects are rejected before replacing the active geometry.
- Antenna project saves now use `QSaveFile` atomic commit semantics so an interrupted/failed write does not replace the last valid `.qta` file.
- Added the 5.42 persistence verification contract and fixtures for current-schema round-trip, unversioned legacy migration, future-schema rejection and malformed-geometry rejection.
- The validated 5.41 electromagnetic solver/validation kernels are intentionally unchanged in this stabilization slice.
- Application version: 5.42.0. Project schema remains `.qta v19`; no new physical model family is introduced.

# QTsignalApp 5.41.0

## 5.41.0 — Frozen 1.0 validation closure

- Closed the small-loop 1.0 gate without hiding the rooftop discrepancy: pulse/point-matching is explicitly the production closed-loop path (8.35% normalized-pattern RMS), while closed-loop rooftop remains visible as a non-blocking experimental diagnostic (~30.9% RMS).
- Closed the finite-ground monopole gate on the rooftop+RWG production path: ΔZ mesh ≈ 8.63%, coarse/fine normalized-pattern RMS ≈ 0.45%, positive passive Zin. Infinite-ground image-theory impedance/directivity remain informative only.
- Replaced the patch production resonance gate based on the broad Gaussian port's min-|X| with a fixed-voltage modal surface-current response estimator, integral(|Js|^2 dS), using a three-point parabolic peak refinement. The original min-|X| remains visible as a port-reactance diagnostic.
- Canonical differential-port patch: modal resonance ≈ 1.08045 f0 (8.05% offset) with ΔZ mesh ≈ 4.35%, satisfying the frozen <=10% / <=7.5% 1.0 gate without changing the layered operator.
- Validation rows now identify production vs diagnostic paths and the status panel reports a compact Simulation/Designer 1.0 production-gate summary. At 300 MHz the frozen canonical campaign reports 4/4 production rows PASS.
- Application version: 5.41.0. Project schema remains `.qta v19`.

# QTsignalApp 5.40.0

## 5.40.0 — Hybrid broadband sweep and shared single-frequency input path

- Replaced the Antenna Designer thin-wire-only default sweep with `Auto — full geometry when needed`.
- Added an explicit sweep solver selector: Auto, Hybrid wire + PEC/RWG + dielectric, or Thin-wire MoM legacy/fast.
- Extracted one `buildHybridInput()` path shared by the single-frequency Hybrid solve and broadband sweep, so ports, surface triangulation, finite conductivity, dielectric regions and the selected Sommerfeld kernel cannot silently diverge between the two workflows.
- Hybrid sweep re-solves the complete coupled system at each sampled frequency with far-field post-processing disabled for speed; it reuses the same PEC triangulation across the band while all frequency-dependent kernels/matrices are rebuilt.
- Hybrid feed results now drive the existing Zin, S11, VSWR and interactive Smith views and the exported `antennaSweepAvailable` data.
- 5.36+ complex/passivity fallback remains active independently at every sweep point; the UI reports the number of fallback points.
- Explicit thin-wire mode remains available and now warns if PEC/dielectric geometry is deliberately ignored.
- Sweep solver selection is persisted as an optional `.qta v19` field; older projects default to Auto.
- Simulation/Designer 1.0 roadmap updated: Gate B broadband architecture blocker is closed; 5.41 validation closure is the next critical milestone.
- Application version: 5.40.0. Project schema remains `.qta v19`.

# QTsignalApp 5.39.0

## 5.39.0 — Grounded-PEC modal reference and Simulation/Designer 1.0 scope freeze

- Added `LayeredSlabSommerfeldGroundedPecModeAudit = 14`.
- Added an air/dielectric/PEC grounded-substrate dispersion audit using the explicit backing-plane orientation of the dielectric region.
- Grounded modal equations are solved for complex TE/TM propagation constants; the thin 10 mm, eps_r=3.2 benchmark correctly retains the fundamental TM mode while no spurious low-frequency TE mode is reported.
- Added an RWG backing-plane coverage diagnostic so the grounded modal audit is only populated when the substrate negative-normal face is meaningfully PEC-backed.
- Added unit-interface-field modal forward-power normalization (W/m), dielectric power fraction and phase-velocity diagnostics.
- Exact finite-ground `Psurface-wave` remains guarded. The 5.39 normalization is physically dimensioned, but source excitation requires a validated finite-RWG current-to-mode pole overlap; the closure residual is never relabelled as modal power.
- Rectangular and inset-fed patch presets now select the 5.39 grounded modal mode.
- Added `SIMULATION_DESIGN_V1_ROADMAP.md`, freezing the finite acceptance gates for Simulation / Antenna Designer 1.0.
- The frozen blockers include: hybrid broadband sweep using the selected production solver, small-loop rooftop validation, canonical patch resonance within 10% of the design frequency, patch mesh delta-Z <= 7.5%, homogeneous-limit/passivity regression and release stabilization.
- Exact finite-ground surface-wave excitation power, general N-layer anisotropic MPIE, volumetric coax apertures, exact finite-substrate edge diffraction and FMM/GPU acceleration are explicitly post-1.0 unless a frozen benchmark proves one necessary.
- Planned final milestone after the finite 5.40–5.42 closure sequence: `QTsignalApp 6.0.0 — Simulation / Antenna Designer 1.0`.
- Application version: 5.39.0. Project schema remains `.qta v19`.

## 5.38.0 — Guided-mode Sommerfeld pole extraction with guarded surface-wave power

### Pole extraction
- Added `LayeredSlabSommerfeldSurfaceWavePoleAudit = 13`.
- Scans the evanescent interval between the air and dielectric light lines for TE/TM minima of `D = 1 - r² exp(-2 gamma2 h)`.
- Uses quadratic clustering near `k0` so fundamental modes of electrically thin PCB slabs remain resolvable.
- Each candidate is refined as a complex transverse-wavenumber root and reports beta/k0, attenuation, denominator residual and reflection-coefficient residue magnitude.

### Power guard
- 5.37 propagating upper/lower far-field power and its one-sided accepted-power guard remain active.
- `Psurface-wave` is deliberately kept at 0 W / guarded in 5.38. The extracted poles belong to the analytic air/dielectric/air slab, while the application's ground plane is an explicit finite RWG PEC surface.
- No closure residual is re-labelled as surface-wave power; a grounded-stack dispersion relation, modal normalization and source-overlap integral are required first.

### UI / validation
- Hybrid diagnostics list every extracted TE/TM pole candidate and its propagation/residue data.
- Rectangular and inset-fed patch presets now select the 5.38 audit mode.
- The homogeneous epsilon-r -> 1 limit produces no guided-pole candidates and retains the 5.37 propagating far field.
- `.qta` remains schema version 19.
- Application version: 5.38.0.

# QTsignalApp 5.37.0

## 5.37.0 — Propagating TE/TM layered far-field and one-sided power guard

### Layered far field
- Added `LayeredSlabSommerfeldPropagatingFarField = 12`.
- The solved coherent wire+RWG far-field moment is decomposed into local TE/TM components relative to the dominant finite slab.
- Same-side radiation uses direct + finite-slab reflection; opposite-side radiation uses finite-slab TE/TM transmission.
- The angular integration reports separate `Pupper`, `Plower`, total propagating power and a propagating-spectrum directivity.
- The homogeneous epsilon-r -> 1 limit reduces to the existing free-space propagating far field.

### Power guard
- The 5.36 passive-port check remains active.
- 5.37 adds a one-sided energy guard: a complex candidate is rejected when propagating layered radiation plus modeled conductor loss exceeds accepted power beyond numerical tolerance.
- A positive residual is not forced to zero because surface-wave pole power and explicit dielectric absorption are not yet evaluated.

### Result views
- When the 5.37 propagating result passes its power guard, the antenna azimuth/elevation, polar and 3D radiation views display the layered propagating pattern instead of the free-space diagnostic.
- Hybrid result diagnostics report upper/lower power, total propagating power, source-side convention, propagating directivity and unresolved closure.

### Scope / limitations
- This is still an equivalent-current propagating-spectrum post-process. Surface-wave pole residues, lateral-wave branch-cut refinements, finite substrate-edge diffraction and explicit dielectric-loss power remain future work.
- `.qta` remains schema version 19.
- Application version: 5.37.0.

# QTsignalApp 5.36.0

## 5.36.0 — Monitored full-complex wire/RWG Sommerfeld transition

### Complex mutual residual
- Added `LayeredSlabSommerfeldComplexPowerGuard = 11`.
- In this mode, the wire↔RWG HED/VED vector-potential residuals and scalar-gradient residuals retain their full complex Sommerfeld values instead of being projected to `Re(G)`.
- The surface-only RWG layered operator intentionally remains on the audited guarded 5.31/5.35 path; this step targets only the mutual wire/surface transition.

### Automatic passive-port fallback
- The full-complex candidate is accepted only when the solved system is finite, accepted real port power is non-negative, modeled conductor loss does not exceed accepted power, and all driven input resistances remain non-negative within numerical tolerance.
- If the candidate fails, the solver automatically re-runs the same problem with the 5.35 reactive VED off-diagonal kernel.
- The rejected candidate `Zin` and accepted power remain available in diagnostics so the failure is visible rather than silently hidden.

### Power audit
- Added `Paccepted - (Prad + Pcond)` and a relative closure residual.
- The audit explicitly marks the radiation term as `free-space/incomplete` for layered cases because the reflected/transmitted far-field tensor, dielectric absorption and surface-wave power are not yet integrated.
- The closure value is therefore advisory and is not used to force a complex candidate to pass or fail.

### Validation behavior
- A vertical VED/via benchmark at 300 MHz retains the full-complex candidate with positive input resistance and accepted power.
- An embedded horizontal HED benchmark produces a negative candidate input resistance and is automatically returned on the 5.35 reactive fallback, demonstrating that the guard is active rather than cosmetic.
- Voltage scaling and homogeneous-medium collapse remain validation requirements.

### Compatibility
- `.qta` schema remains version 19.
- Application version: 5.36.0.

# QTsignalApp 5.35.0

## 5.35.0 — Reciprocal VED mixed rho-z / z-rho vector transition

### Mixed TM dyadic completion
- Added `DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal`.
- The internal medium-2 TM cavity response now exposes signed observation/source vertical derivatives in addition to its scalar residual.
- The mixed TM projector is inverse-transformed with `J1(k_rho*rho)` to produce residual `G_rho,z` and `G_z,rho` components.
- A normal/VED source can therefore excite a radial tangential vector-potential response, while a radial tangential source can excite the reciprocal normal response.

### Numerical guards
- The mixed components are residual-only: the homogeneous medium-2 direct term remains in the audited baseline and is not counted twice.
- New mixed-vector residuals are real-projected before the `j*omega*mu` EFIE prefactor, preserving the reactive/passivity guard used by the 5.29–5.34 transitions.
- The independently integrated wire->RWG and RWG->wire mutual blocks continue to be replaced by their Lorentz-reciprocal average before solution.
- The 5.34 mode remains available as an explicit regression path with the mixed vector components still guarded.

### Compatibility and validation intent
- Surface-only patch behavior is unchanged by construction; the 5.35 terms require an internal normal/tangential wire-surface interaction to be exercised.
- The dedicated via benchmark is moved toward the surface edge to avoid symmetry cancellation and make the mixed term observable.
- Full-complex layered radiative power balance and layered far-field radiation remain future work.
- `.qta` remains schema version 19.
- Application version: 5.35.0.

# QTsignalApp 5.34.0

## 5.34.0 — Guarded VED / via normal-current transition

### Vertical-electric-dipole / normal-current path
- Added `DielectricKernelModel::LayeredSlabSommerfeldVedNormal`.
- Internal predominantly normal rooftop currents now use a TM-only medium-2 cavity residual instead of being left completely on the effective-medium path.
- The internal Sommerfeld radial table now stores a VED scalar-potential residual and a diagonal normal vector-potential residual in addition to the existing HED tangential data.
- The homogeneous dielectric direct term remains supplied by the baseline operator; only the finite-slab multiple-reflection residual is injected.

### Via / RWG scalar coupling
- Vertical rooftop charge now contributes to a horizontal RWG test through the tangential gradient of the VED scalar residual.
- The reciprocal surface-charge → vertical-wire path extends the HED scalar residual from an in-plane gradient to a full spatial gradient when the wire observation point is strictly inside the slab.
- The two independently integrated wire↔RWG blocks remain Lorentz-reciprocal averaged before solution.

### Passivity guard
- New VED vector/scalar residuals remain real-projected before the EFIE prefactors are applied.
- The off-diagonal TM vector dyadic and full-complex layered VED power/radiation terms remain guarded/off.
- This is therefore a controlled normal-current transition, not yet a complete general VED layered-medium MPIE.

### Regression targets
- Surface-only differential patch: `Zin ≈ 9.00491 - j43.1675 ohm`, unchanged from the 5.33/5.31 surface reference.
- Tangential embedded HED case: `Zin ≈ 2.28205 - j77.5816 ohm`, unchanged from 5.33.
- Vertical embedded via benchmark at 300 MHz / x=35 mm: 5.33 path `≈ 13.2896 - j2188.28 ohm`; 5.34 VED path `≈ 13.3259 - j2193.38 ohm`, `|ΔZ| ≈ 5.10 ohm`.
- Doubling the via excitation voltage keeps `Zin` invariant and scales accepted power by four.
- Homogeneous `epsilon_r=1` limit differs from the previous path by only about `0.0059 ohm`.
- `.qta` project schema remains version 19.
- Application version: 5.34.0.

# QTsignalApp 5.33.0

## 5.33.0 — Internal-layer Sommerfeld cavity + tangential scalar gradient

### Internal dielectric source / observation transition
- Added `DielectricKernelModel::LayeredSlabSommerfeldInternalLayer`.
- Tangential rooftop-wire↔RWG pairs may now use source/observation points inside the dielectric slab rather than falling back entirely to the effective-medium operator.
- The internal spectral Green uses the medium-2 multiple-reflection cavity denominator and subtracts the homogeneous dielectric direct term already present in the baseline, so only the finite-slab residual is injected.
- The homogeneous limit (`εr -> 1`) collapses the internal residual to approximately zero.

### HED scalar-gradient activation
- The 5.32 arbitrary-height HED scalar spectrum is no longer diagnostic-only in the 5.33 mode.
- A tangential finite-difference gradient of the real-projected HED scalar residual is included in rooftop-wire↔RWG mutual interactions together with the vector-potential residual.
- Exterior and internal tangential wire/surface interactions therefore now contain both guarded mixed-potential contributions.

### Passivity / reciprocity guards
- Internal vector and scalar increments remain real-projected before their EFIE prefactors are applied; they are therefore reactive corrections at this transition stage.
- Independently integrated wire↔RWG blocks continue to be Lorentz-reciprocal averaged before solution.
- Predominantly normal embedded current (via / vertical probe) is detected and intentionally left on the previous effective-medium path because the VED/normal-current layered dyadic is not yet implemented.

### Validation
- Surface-only differential-port patch remains a strict 5.31 regression at approximately `9.00491 - j43.1675 ohm`.
- A tangential embedded-wire / PEC-surface sanity case at 300 MHz remains passive and voltage-linear. At `z=-2 mm`, the previous exterior-only path gives approximately `3.89994 - j144.032 ohm` while the 5.33 internal-layer path gives `2.28205 - j77.5816 ohm` (`|Delta Z| ~= 66.47 ohm`). At `z=-8 mm`, the corresponding shift falls to about `53.68 ohm`, confirming the expected depth dependence.
- Doubling the embedded-wire source voltage leaves `Zin` invariant and multiplies accepted power by four.
- The `εr=1` internal-layer result agrees with the previous path to about `0.07 ohm`, within the limiting-absorption / radial-table numerical tolerance used by this transition kernel.
- A predominantly normal embedded wire activates the explicit via/normal-current guard; tangential vector and HED scalar-gradient corrections remain disabled for that case until the VED/normal-current layered dyadic is implemented.

### Compatibility
- `.qta` schema remains version 19.
- Application version: 5.33.0.

# QTsignalApp 5.32.0

## 5.32.0 — Exterior-height Sommerfeld transition for wire↔RWG coupling

### Exterior source / observation heights
- Added `DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight`.
- The 5.31 face-only TE/TM spectrum is propagated into the two exterior air half-spaces with `exp[-gamma1(d_obs+d_src)]`.
- Same-half-space interactions use reflected TE/TM spectra; opposite-half-space interactions use transmitted TE/TM spectra.
- At zero exterior height the new spectral construction reduces to the existing slab-face kernels.

### Guarded wire↔surface mutual coupling
- Exterior wire↔RWG mutual interactions now receive the height-propagated tangential vector-potential residual.
- Opposite-half-space transmission subtracts the effective-medium direct vector term already present in the baseline to avoid double counting.
- Only `Re(Delta G_A,ext)` is injected. After multiplication by `j omega mu0`, the new contribution is reactive and therefore retains the existing passivity guard.
- The HED scalar spectrum is evaluated with the same height propagation but remains diagnostic in the wire↔RWG path until the matching arbitrary-height scalar-gradient term is validated.
- Source or observation points inside the dielectric volume remain on the audited effective-medium path; internal-layer dyadics are not claimed as solved in this version.

### Reciprocity and regression
- The existing reciprocal wire↔RWG block averaging remains active for the new mode.
- The canonical surface-only patch benchmark reduces numerically to the 5.31 operator, so its established convergence/passivity reference remains unchanged.
- A dedicated elevated-wire / PEC-surface sanity case confirms that the new correction is active, decays with exterior height, preserves positive accepted power and obeys voltage scaling.

### Compatibility
- Rectangular-patch and inset-fed patch presets now select the 5.32 mode so future exterior wire/probe geometry automatically uses the new transition while surface-only differential-port solves remain a 5.31 regression.
- `.qta` schema remains version 19.
- Application version: 5.32.0.

# QTsignalApp 5.31.0

## 5.31.0 — Coupled HED longitudinal scalar reflection / transmission

### Layered mixed-potential scalar step
- Added `DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed`.
- The new mode retains the audited 5.29 same-face HED TE vector-potential correction, the 5.30 residual transmitted TE/TM tangential vector dyadic and reciprocal RWG projection.
- The slab-face scalar block now uses the coupled horizontal-electric-dipole TE/TM longitudinal spectrum instead of the earlier TM-only scalar transition.
- Same-face reflection uses the HED longitudinal combination `R_TE - (gamma1^2/k_rho^2)(R_TE - R_phi^TM)` with the high-k singular constant removed before inverse Hankel transformation.
- Opposite-face transmission now uses the corresponding coupled expression `T_TE - (gamma1^2/k_rho^2)(T_TE - T_phi^TM)`. In the homogeneous limit `eps_r -> 1`, TE and TM transmissions coincide and the expression reduces to the ordinary direct scalar spectrum.

### Passivity guard
- The HED longitudinal scalar is activated only through the existing real-Green increment / reactive-impedance projection. The full complex scalar increment remains disabled because earlier incomplete MPIE trials produced negative resistance in passive checks.
- Cross-face transmitted vector terms remain reactively projected for the same reason.
- Reciprocal Galerkin symmetrization of the RWG block remains enabled in the 5.31 mode, with the raw pre-symmetry mismatch retained as a quadrature diagnostic.

### Numerical checks
- Canonical 300 MHz patch with fixed Gaussian differential port:
  - coarse mesh: approximately `7.646 - j41.811 ohm`,
  - fine mesh: approximately `9.005 - j43.168 ohm`,
  - mesh delta: approximately `4.35%`.
- At `1.15 f0`, the coarse patch gives approximately `11.300 - j31.879 ohm`; the sampled patch resonance therefore still remains above the design frequency and the benchmark is intentionally not relaxed.
- Passive two-plate dielectric check remains positive-resistance:
  - 30 MHz: approximately `1.944 - j358.934 ohm`,
  - 300 MHz: approximately `0.192 - j30.837 ohm`,
  - 600 MHz: approximately `2.142 - j3.540 ohm`.
- Voltage doubling preserves input impedance and the expected four-times accepted-power scaling.
- Quarter-wave monopole regression remains approximately `62.180 + j40.873 ohm`.

### UI / diagnostics
- Added `Sommerfeld HED longitudinal scalar + transmitted dyadic (5.31)` to the Hybrid dielectric-kernel selector.
- Rectangular and inset-fed patch starters now preselect the 5.31 mode.
- Hybrid metrics explicitly report same-face and cross-face HED longitudinal scalar activation separately from the 5.30 transmitted-vector diagnostics.

### Scope
- 5.31 completes the coupled HED scalar reflection/transmission step only for RWG interactions lying on the slab faces.
- Arbitrary source/observer heights, wire↔surface off-interface dyadics, full complex layered power balance, explicit surface-wave pole extraction and layered far-field radiation remain future work.
- `.qta` remains schema version 19; the new selector value is appended, so existing saved indices remain compatible.
- Application version: 5.31.0.

# QTsignalApp 5.30.0

## 5.30.0 — Transmitted TE/TM tangential dyadic + reciprocity guard

### Layered-media RWG operator
- Added `DielectricKernelModel::LayeredSlabSommerfeldCrossFace`.
- The 5.30 mode retains the 5.26 scalar reactive projection and the audited 5.29 same-face HED TE self/near correction.
- Opposite slab faces now use the transmitted TE/TM tangential vector Green dyadic resolved into longitudinal and transverse in-plane projectors.
- The transmitted layered vector Green function is converted to a residual by subtracting the already-present homogeneous/effective-medium baseline before injection, preventing double counting.
- A full complex transmitted-vector trial was explicitly rejected after the passive 300 MHz two-plate case produced negative input resistance. The released 5.30 path therefore keeps only the real transmitted Green increment; through the `j omega mu` vector EFIE prefactor this contributes a controlled reactive correction.

### Reciprocity guard
- Added raw and post-guard RWG surface reciprocity diagnostics.
- In the dedicated 5.30 mode the reciprocal Galerkin surface block is symmetrized as `Z <- (Z + Z^T)/2` before row scaling.
- The pre-symmetry mismatch remains visible as a quadrature-quality diagnostic instead of being discarded.
- A naive all-pair same-face TE extension was also tested and rejected because patch mesh convergence degraded from about 5% to about 28%; 5.30 therefore keeps the 5.29 same-face activation scope while extending the opposite-face transmitted operator.

### Numerical checks
- Canonical 300 MHz patch with fixed Gaussian differential port:
  - coarse mesh: approximately `7.660 - j41.783 ohm`,
  - fine mesh: approximately `9.015 - j43.139 ohm`,
  - mesh delta: approximately `4.35%`.
- Passive two-plate dielectric check:
  - 30 MHz: approximately `1.944 - j358.775 ohm`,
  - 300 MHz: approximately `0.192 - j28.940 ohm`,
  - 600 MHz: approximately `1.808 - j9.914 ohm`.
- Voltage doubling still leaves input impedance invariant and scales accepted power by four.
- The 5.29 mode remains numerically unchanged (`9.153 - j42.837 ohm` on the fine patch reference), and the monopole regression remains approximately `62.180 + j40.873 ohm`.

### UI / diagnostics
- Added `Sommerfeld transmitted TE/TM + reciprocity guard (5.30)` to the Hybrid dielectric-kernel selector.
- Rectangular and inset-fed patch starters now preselect the 5.30 mode.
- Hybrid metrics report cross-face transmitted-vector activation and RWG reciprocity before/after the reciprocal projection.

### Scope
- 5.30 is still a guarded transition toward a full layered-media MPIE, not a complete general Green dyadic.
- Arbitrary wire/surface off-interface dyadics, full longitudinal mixed-potential completion, complex transmitted power balance, explicit surface-wave pole treatment and layered far-field radiation remain future work.
- `.qta` remains schema version 19.
- Application version: 5.30.0.

# QTsignalApp 5.29.0

## 5.29.0 — Passivity-guarded Sommerfeld HED TE-vector transition

### Layered-medium vector-potential step
- Added `LayeredSlabSommerfeldTeVector`, exposed as `Sommerfeld TE vector + guarded scalar (5.29)`.
- For RWG source/observer interactions lying on the same dielectric-slab face, the tangential magnetic-vector-potential block now receives a finite-slab TE Sommerfeld correction using the horizontal-electric-dipole mixed-potential gauge.
- The spectral engine now evaluates both finite-slab TE and TM reflection/transmission coefficients and caches their inverse-Hankel radial transforms.
- The direct same-triangle weak singularity remains assigned to the existing Duffy quadrature; the layered correction does not introduce a second PEC image plane or replace the explicit RWG ground.

### HED scalar diagnostic and passivity guard
- Added the coupled HED scalar-potential diagnostic based on the TE/TM combination `R_TE + R_q`, with `R_q` carrying the longitudinal spectral coupling.
- A development trial that injected the full complex HED scalar correction before the transmitted/longitudinal potential terms were complete produced negative input resistance in a passive 300 MHz two-plate sanity case. That path is deliberately not released.
- The production 5.29 mode therefore keeps the audited 5.26 scalar reactive projection while adding the separately stable same-face TE vector-potential correction. Result diagnostics explicitly report that full-complex scalar injection is guarded/off.

### Patch / slab checks
- Canonical 300 MHz Gaussian differential-port patch, fine mesh: approximately `9.153 - j42.837 ohm`.
- Coarse/fine impedance change is approximately `5.18%`; the sampled minimum-|X| remains at `1.15 f0`, so the patch benchmark deliberately remains `FAIL`. No resonance tolerance was relaxed.
- Passive two-plate dielectric sanity cases remain positive-resistance with the 5.29 mode: approximately `1.944 - j358.88 ohm` at 30 MHz and `0.192 - j30.87 ohm` at 300 MHz.
- The 30 MHz voltage-scaling check preserves input impedance and the expected quadratic accepted-power scaling.

### Scope / limitations
- Patch and inset-patch starters now preselect the 5.29 guarded mode.
- Cross-face transmitted vector-potential normalization, longitudinal mixed-potential completion, arbitrary off-interface wire/surface layered dyadics and the layered far-field Green tensor are intentionally still outside this stage.
- This version is therefore a controlled transition toward a full layered-media MPIE, not yet a general Sommerfeld dyadic solver.

### Compatibility
- `.qta` remains schema version 19. Existing project files remain loadable.
- Application version: 5.29.0.

# QTsignalApp 5.28.0

## 5.28.0 — Radiation/result audit and convention corrections

### Radiation-coordinate correction
- Fixed a genuine hybrid far-field convention mismatch. Hybrid azimuth/elevation post-processing previously exposed the vertical cut as geometric elevation `-90…+90°`, while the Results UI and validation bench interpreted the same vector as spherical polar angle `theta=0…180°`.
- All wire and hybrid radiation outputs now use one explicit convention:
  - `theta = 0°` toward `+Z`,
  - `theta = 90°` in the `XY` plane,
  - `phi = 0°` toward `+X`, positive toward `+Y`.
- Hybrid vertical cuts are generated directly in spherical `theta`, so 2D, polar, 3D and validation views now describe the same directions.

### Global radiation normalization
- Azimuth, elevation and full-sphere radiation samples now share one global field-amplitude reference.
- Earlier builds normalized every 2D cut independently, so a cut that missed the true main lobe could still display `0 dB`. This is no longer possible.
- The global peak search combines the full-sphere integration grid with the principal azimuth/elevation cuts; directivity uses that refined sampled peak while the power integral remains the solid-angle quadrature.
- Radiation-result display floors are now consistently `-40 dB` in Cartesian, polar and 3D views.

### Directivity, gain and realized gain
- The 3D view no longer labels directivity-only data generically as gain.
- The Results view now distinguishes:
  - `Dmax`: peak directivity,
  - `Gmax = eta_rad Dmax`: gain within the current loss model,
  - `Greal,max = Gmax (1-|Gamma|^2)`: realized gain when a single feed/reference impedance makes that mismatch factor well-defined.
- In the standalone wire solver the conductor model is lossless, so `eta_rad=1` by definition and `Gmax=Dmax`.
- In the hybrid solver `Gmax` currently uses the modeled conductor radiation efficiency. Missing dielectric/full layered losses are not silently invented.
- The 3D color legend is now in absolute model gain `dBi`; hover readout reports theta/phi, normalized field, relative dB level, directivity, gain and realized gain when available.

### Smith charts and result inspection
- Interactive hover readout is now available not only on the Antenna Designer Smith chart but also on:
  - the RF-chain Smith chart,
  - the FDTD/VNA Smith chart.
- Hover readouts expose frequency, complex Gamma/S11, magnitude, normalized impedance, return loss and VSWR.
- `FieldProfileSeries` now supports a separate un-clipped hover value. The antenna VSWR plot can therefore remain visually clipped at 20 while hover reports the true computed VSWR (including infinity when appropriate).

### Frequency-sweep audit
- The Antenna Designer sweep path is explicitly labelled `Thin-wire antenna frequency sweep`.
- Its UI/status/reference documentation now states that PEC/RWG surfaces and dielectric regions are not part of that sweep path yet. Previous documentation incorrectly implied that the complete hybrid geometry was re-solved.
- If PEC or dielectric geometry is present, the completed sweep status now emits an explicit warning instead of allowing the result to be mistaken for a hybrid sweep.
- The sweep status now correctly calls the reported count `algebraic wire-MoM unknowns` rather than `pulse unknowns`, because the linear rooftop basis is also supported.

### Numerical audit checkpoints
- 300 MHz half-wave dipole after the common-peak normalization:
  - pulse/point matching: `D ~= 2.1806 dBi`, pattern RMS error `~=0.42%`,
  - linear rooftop/Galerkin: `D ~= 2.1825 dBi`, pattern RMS error `~=0.44%`.
- The hybrid quarter-wave monopole input impedance remains unchanged (`~62.18 + j40.87 ohm` for the rooftop benchmark), while the corrected theta convention reduces its reported pattern RMS discrepancy from roughly `48.84%` to `31.44%`.
- The small-loop rooftop pattern remains a known weak point (`~30.9%` RMS in the current canonical benchmark) despite a reasonable directivity estimate. This is left visible as a solver/basis limitation rather than hidden by result-view normalization.

### Compatibility
- No antenna project-schema change: `.qta` remains version 19.
- Application version: 5.28.0.

# QTsignalApp 5.27.0

## 5.27.0 — Interactive result views and 3D gain color map

### Result-graph interaction
- `FieldProfilePlot` now enables mouse tracking and draws an interactive readback cursor directly on the chart.
- Hovering a result curve shows the current X coordinate and the nearest value on each visible series.
- Frequency-sweep plots therefore become directly readable without manually estimating values from the grid.

### Antenna Smith chart improvements
- The antenna sweep Smith chart is now interactive. Hovering the trace highlights the nearest sample and displays:
  - frequency,
  - complex reflection coefficient `Γ`,
  - `|Γ|`,
  - normalized impedance `z/Z0`,
  - return loss,
  - VSWR.
- Best-match and `X≈0` markers remain visible as before.

### Polar radiation cut improvements
- Azimuth and elevation polar plots now support direct hover inspection of the nearest angular sample.
- The popup readback shows the cut angle (`φ` or `θ`), normalized field value and relative level in dB.
- Full 360° cuts are now explicitly closed visually when the provided angle span covers a complete revolution.

### 3D radiation view
- The 3D radiation widget now supports a gain-style color map across the surface, from blue for low relative level up to red for the strongest region.
- A color legend is rendered on the right side of the view to relate color to normalized dB level.
- Hovering the 3D pattern now highlights the nearest sampled direction and displays:
  - `θ`,
  - `φ`,
  - normalized field amplitude,
  - relative level in dB,
  - approximate gain referenced to the reported peak directivity.
- The `Results` tab now exposes quick options in the top-right corner to enable/disable the 3D color map and the legend independently.

### Result-view audit status
- No blocking display/calculation regression was intentionally introduced in the existing wire-current, charge, sweep or radiation result pipelines.
- This 5.27 step focuses on readability and inspection of already computed data, while keeping the current solver outputs and `.qta` schema unchanged.

### Compatibility
- `.qta` project schema remains version 19.
- Application version: 5.27.0.

# QTsignalApp 5.26.0

## 5.26.0 — Dynamic Sommerfeld TM scalar transition

### k_rho-dependent finite-slab spectrum
- Added `DielectricKernelModel::LayeredSlabSommerfeldScalar` to the hybrid wire + PEC/RWG solver.
- The new model evaluates the finite air/dielectric/air slab in the transverse spectral domain with `gamma_i = sqrt(k_rho^2-k_i^2)` and the scalar TM potential reflection coefficient `r = (gamma2-epsr*gamma1)/(gamma2+epsr*gamma1)`.
- Finite-thickness same-face reflection and cross-face transmission use the multiple-reflection denominators `1-r^2 exp(-2 gamma2 h)` rather than the frequency-independent electrostatic coefficient used by 5.25.
- Spatial coupling is recovered numerically with a zeroth-order Bessel/Hankel Sommerfeld transform. The current implementation uses 16-point Gauss-Legendre panels and a cached 80-sample radial table for each slab/frequency combination.
- A limiting loss tangent of `1e-5` is added only to regularize branch/pole sampling for nominally lossless slabs.

### Singular treatment and passivity guard
- Same-face spectra subtract their high-`k_rho` limit before inverse transformation. This prevents the new interface term from changing the direct `1/R` singular coefficient already handled by the 5.23 Duffy quadrature.
- The patch and ground remain explicit RWG PEC surfaces; no PEC image plane is added.
- A first validation implementation that injected the complete complex dynamic scalar correction was rejected: without the matching vector-potential TE/TM dyadic, a high-frequency two-plate sanity case could acquire a non-passive negative input resistance.
- The released 5.26 path therefore retains the complete complex 5.25 quasi-static scalar baseline and injects only `Re(G_Sommerfeld-G_QS)`. Because the scalar EFIE block is multiplied by `1/(j omega)`, this contributes a controlled dynamic reactive correction while avoiding a fictitious standalone radiative/loss term.
- This is explicitly a transition toward a layered mixed-potential integral equation (MPIE), not yet the complete Sommerfeld dyadic solver.

### Antenna Designer / diagnostics
- Added `Sommerfeld TM scalar (dynamic, 5.26)` to the Hybrid wire + PEC dielectric-kernel selector.
- Rectangular-patch and inset-fed patch starters now preselect the 5.26 model. Generic hybrid projects retain their existing default for backward compatibility.
- Results report whether the Sommerfeld scalar correction was actually used, the number of active slab regions, radial-table sample count, spectral quadrature order, limiting loss and reactive-projection status.
- `Equations & references` documents the vertical propagation constants, finite-slab reflection/transmission functions, inverse Hankel transform, high-k subtraction and current partial-MPIE limitation.

### Numerical validation
- Canonical 300 MHz differential-port patch, fine mesh: approximately `9.15 - j42.85 ohm`.
- Current-to-fine mesh variation: approximately `5.13%`.
- The minimum-`|X|` point in the existing `0.85 f0 ... 1.15 f0` sweep remains at `1.15 f0`, so the patch deliberately remains `FAIL`; the benchmark tolerance was not widened.
- Two-plate dielectric sanity case at 30 MHz remains passive at approximately `1.944 - j358.9 ohm` and preserves voltage scaling. At low frequency the 5.26 result approaches the 5.25 quasi-static kernel closely; the dynamic reactance shift grows as frequency increases.
- The unrelated hybrid monopole remains approximately `62.18 + j40.87 ohm`, confirming that a geometry without an active dielectric slab is unchanged.

### Compatibility
- 5.23 Duffy RWG self/near quadrature, 5.24 symmetric Gaussian differential port and the complete 5.25 quasi-static kernel remain available unchanged.
- `.qta` remains schema version 19; the dielectric-kernel choice is still saved by the existing backward-compatible index.
- Electrostatic and magnetostatic model files remain unchanged from the supplied 5.25.0 base.
- Application version: 5.26.0.

## 5.25.0 — Finite-slab dielectric image / fringing correction

### Layered dielectric kernel for RWG surfaces
- Added `DielectricKernelModel::LayeredSlabQuasiStatic` to the hybrid wire + PEC/RWG solver.
- The patch and ground plane remain explicit RWG unknowns. No additional PEC image plane is introduced, avoiding double-counting the conducting boundary condition.
- The dielectric slab interfaces are represented by the quasi-static two-face spectral Dirichlet-to-Neumann relation and converted to a short adaptive image series using `r = (epsr* - 1)/(epsr* + 1)`, including the dielectric loss tangent through the complex relative permittivity.
- The image series uses 3 to 6 terms depending on reflection strength. It is intentionally a controlled quasi-static correction, not a full dynamic Sommerfeld TE/TM Green tensor.

### Stable residual formulation
- A direct replacement of the full dynamic RWG scalar kernel by a static image expansion was rejected during validation because it could produce non-passive input resistance and poor mesh convergence.
- Version 5.25 therefore keeps the audited frequency-domain segment-overlap/effective-medium kernel as the dynamic baseline and adds only the residual finite-slab quasi-static correction to the RWG scalar-potential block.
- The residual is weighted by the dielectric loading not already represented by `fieldFillFactor`, preventing the Hammerstad effective-permittivity seed from being counted twice.
- The same-face direct `1/R` singular coefficient remains handled by the 5.23 Duffy/effective-medium term; only finite-distance image contributions are added.
- Result diagnostics expose whether the layered correction was used, how many slab regions contributed, and the maximum number of retained image terms.

### Antenna Designer / references
- `Hybrid wire + PEC block MoM` now offers `Layered slab image / fringing (quasi-static)` in the dielectric interaction-kernel selector.
- Rectangular-patch and inset-patch starters preselect the layered option; the generic workspace keeps `Segment-overlap weighted` as its default for backward compatibility.
- `Equations & references` documents the slab spectral relation, image coefficient, same-face/cross-face series, the residual-correction strategy, and the limitation relative to a full Sommerfeld formulation.
- Existing `.qta` projects remain compatible: the saved combo-box index is backward compatible and the project schema remains version 19.

### Patch validation bench
- The 300 MHz canonical differential-port patch benchmark now uses the 5.25 layered residual correction while retaining the fixed `sigma = 0.08 lambda0` Gaussian port and symmetric power-dual `+V/2 / -V/2` normalization from 5.24.
- Fine-mesh input impedance is approximately `9.21 - j42.61 ohm` in the current benchmark.
- Current-to-fine mesh variation is approximately `4.93%`, versus about `5.29%` in 5.24.
- The minimum-`|X|` scan remains near `1.15 f0`; the case therefore deliberately remains `FAIL`. The test has not been loosened to hide the remaining full-wave layered-medium/fringing discrepancy.

### Regression / compatibility
- 5.23 Duffy RWG self/near quadrature and 5.24 Gaussian differential-port normalization remain intact.
- Wire-only and no-dielectric hybrid cases retain their previous kernels.
- Electrostatic and magnetostatic model files are unchanged from the supplied 5.24.0 source archive.
- Application version: 5.25.0.

## 5.24.0 — Mesh-stable, power-dual differential RWG ports

### Differential-port circuit normalization corrected
- Audited the ideal two-surface PEC lumped port introduced in 5.22 before moving on to the layered-substrate Green function.
- The requested voltage is now represented physically as `+V/2` on the positive terminal and `-V/2` on the negative terminal.
- The RWG circuit incidence vector is therefore `b = (c+ - c-)/2`, where each `c` contains the weighted integral of `div(f_n)` over its terminal footprint.
- The extracted source current uses the same power-dual functional, `Iport = -b^T i`.
- The previous full-difference convention used the factor twice (once in excitation and once in current extraction), which made the reported differential-port impedance four times too small. This is now fixed rather than compensated empirically.

### Smooth physical terminal footprint
- Replaced the binary mesh one-ring terminal used only by the differential surface port with a Gaussian scalar-potential footprint integrated directly over the RWG triangles.
- `Differential port sigma` is now exposed in the Hybrid wire + PEC simulation controls. A value of zero keeps an automatic mesh-local radius for compatibility; a fixed physical radius is recommended for convergence studies.
- The Gaussian support gives a continuous Galerkin port functional instead of changing terminal area abruptly whenever the triangle mesh changes.
- Result diagnostics report the actual minimum/maximum terminal sigma and whether the symmetric power-dual convention was used.

### Patch validation bench
- The canonical patch differential-port row now uses a fixed `sigma = 0.08 lambda0` distributed validation terminal at both current and fine mesh levels. This is deliberately a broad numerical validation port, not a coax-pin model.
- At 300 MHz the differential-port fine-mesh impedance is approximately `9.81 - j42.59 ohm`; the absolute value changes because the old circuit normalization was incorrect.
- More importantly, current-to-fine mesh variation falls to about `5.3%` with a physically fixed smooth footprint, compared with roughly `19.8%` for the 5.23 mesh-sized one-ring terminal.
- The minimum-|X| scan remains at the upper side of the present range (`~1.15 f0`), so the patch intentionally remains FAIL. The next unresolved item is now cleaner: layered substrate / fringing physics rather than RWG self terms, short-wire probe reactance, port normalization, or mesh-dependent terminal area.

### Regression / compatibility
- 5.23 Duffy same-triangle and adjacent RWG quadrature are unchanged.
- Wire-only dipole/loop and the 5.21 monopole terminal treatment remain unchanged.
- Electrostatic and magnetostatic models remain byte-identical to the audited 5.19.1 base.
- No antenna project-schema increment is required: `.qta` remains version 19. The optional `hybridDifferentialPortRadiusMm` field is backward compatible.
- Application version: 5.24.0.

from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]

project = json.loads((ROOT / "QTsignalApp.qtproject.json").read_text(encoding="utf-8"))
main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
ui = (ROOT / "src/antenna_designer_widget.cpp").read_text(encoding="utf-8")
roadmap = (ROOT / "SIMULATION_DESIGN_V1_ROADMAP.md").read_text(encoding="utf-8")
reference = (ROOT / "SIMULATION_ANTENNA_1_0_REFERENCE.md").read_text(encoding="utf-8")
acceptance = (ROOT / "SIMULATION_DESIGN_V1_ACCEPTANCE.md").read_text(encoding="utf-8")
change = (ROOT / "CHANGELOG_APP.md").read_text(encoding="utf-8")
release_readme = (ROOT / "tests/README_RELEASE_REGRESSION.md").read_text(encoding="utf-8")

checks = {
    "version remains 5.42": project.get("version") == "5.42.0" and project.get("packaging", {}).get("productVersion") == "5.42.0" and 'QStringLiteral("5.42.0")' in main,
    "schema stays v19": "AntennaProjectSchemaVersion = 19" in ui,
    "visible engineering notice": all(token in ui for token in [
        "Engineering-use notice",
        "independent 3D full-wave solver and/or measurement",
        "before engineering sign-off",
        "resultWorkspaceLayout->addWidget(engineeringUseNotice)",
    ]),
    "results tab uses notice wrapper": 'workspaceTabs->addTab(resultWorkspace, QStringLiteral("Results"))' in ui,
    "azimuth convention UI": 'Azimuth φ in XY plane' in ui and 'φ = 0° points toward +X; positive angles rotate toward +Y' in ui,
    "elevation convention UI": 'Polar angle θ, φ = 0° cut' in ui and 'θ = 0° points toward +Z; θ = 90° toward +X' in ui,
    "3D spherical mapping": all(token in ui for token in [
        "radius*std::sin(th)*std::cos(ph)",
        "radius*std::sin(th)*std::sin(ph)",
        "radius*std::cos(th)",
    ]),
    "reference RF equations": all(token in reference for token in [
        "Gamma = (Zin - Z0) / (Zin + Z0)",
        "S11[dB] = 20 log10(|Gamma|)",
        "Return loss[dB] = -20 log10(|Gamma|)",
        "VSWR = (1 + |Gamma|) / (1 - |Gamma|)",
        "z = Zin / Z0",
    ]),
    "reference radiation terminology": all(token in reference for token in [
        "D(theta,phi) = 4 pi U(theta,phi) / Prad",
        "eta_rad,cond = Prad / (Prad + Pcond)",
        "Greal,max[dBi] = Gmax[dBi] + 10 log10(1 - |Gamma|^2)",
    ]),
    "reference scope": all(token in reference for token in [
        "one isotropic dielectric slab with an optional explicit PEC backing plane",
        "no general N-layer stack",
        "no full dielectric volume-integral/FEM solve",
        "Exact finite-ground surface-wave excitation power remains guarded",
    ]),
    "reference sign-off warning": "Critical antenna designs must be independently checked with an appropriate 3D full-wave solver and/or measurement before engineering sign-off." in reference,
    "acceptance covers gates A-E": all(f"| {gate} |" in acceptance for gate in "ABCDE"),
    "acceptance blocks 6.0": "READY FOR FINAL TARGET-MACHINE RELEASE GATE; NOT YET 6.0.0" in acceptance and "verify_542b.py --full" in acceptance,
    "roadmap D complete": all(token in roadmap for token in [
        "5.42-D COMPLETE",
        "IMPLEMENTATION COMPLETE / TARGET FULL RUN PENDING",
        "6.0.0 milestone is not released until the target-machine full 5.42-B campaign passes",
        "No new physical model families",
    ]),
    "changelog D": "Release hygiene, equations/scope reference and acceptance matrix (5.42-D)" in change,
    "release README D": "Release-hygiene verification (5.42-D)" in release_readme and "verify_542d.py" in release_readme,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("5.42-D release-hygiene verification FAILED: " + ", ".join(failed))

# Guard against accidentally advertising the unreleased milestone as the package version.
if re.search(r'"version"\s*:\s*"6\.0\.0"', (ROOT / "QTsignalApp.qtproject.json").read_text(encoding="utf-8")):
    raise SystemExit("5.42-D release-hygiene verification FAILED: package was prematurely promoted to 6.0.0")

print("QTsignalApp 5.42-D release-hygiene / Gate A-E documentation verification: PASS")
print("Promotion remains blocked pending: python3 tests/verify_542b.py --full")

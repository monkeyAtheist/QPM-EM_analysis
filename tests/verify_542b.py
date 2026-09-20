from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]

parser = argparse.ArgumentParser(description="QTsignalApp 5.42-B regression and numerical-safety verification")
parser.add_argument("--full", action="store_true", help="run the heavy canonical 4-case + homogeneous-via release campaign")
parser.add_argument("--full-timeout", type=int, default=1800, help="timeout in seconds for the heavy release executable")
args = parser.parse_args()

project = json.loads((ROOT / "QTsignalApp.qtproject.json").read_text(encoding="utf-8"))
main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
roadmap = (ROOT / "SIMULATION_DESIGN_V1_ROADMAP.md").read_text(encoding="utf-8")
change = (ROOT / "CHANGELOG_APP.md").read_text(encoding="utf-8")
wire = (ROOT / "src/numerical_em_solvers.cpp").read_text(encoding="utf-8")
rwg = (ROOT / "src/pec_surface_mom.cpp").read_text(encoding="utf-8")
hybrid = (ROOT / "src/hybrid_wire_surface_mom.cpp").read_text(encoding="utf-8")

checks = {
    "version": project.get("version") == "5.42.0" and project.get("packaging", {}).get("productVersion") == "5.42.0" and 'QStringLiteral("5.42.0")' in main,
    "wire finite guards": all(token in wire for token in ["Wire-network input contains NaN or Inf", "Wire feed contains NaN or Inf", "Wire-network solve produced a non-finite numerical result"]),
    "rwg finite guards": all(token in rwg for token in ["Surface MoM input contains NaN or Inf", "PEC surface geometry contains NaN or Inf", "Surface MoM solve produced a non-finite numerical result"]),
    "hybrid finite guards": all(token in hybrid for token in ["validateInputNumerics", "Hybrid dielectric region contains NaN or Inf", "Hybrid solve produced a non-finite numerical result"]),
    "roadmap": all(token in roadmap for token in ["5.42-A COMPLETE", "5.42-B IMPLEMENTED / TARGET FULL RUN PENDING", "5.42-C COMPLETE", "5.42-D COMPLETE", "6.0.0 milestone is not released"]),
    "changelog": change.startswith("# QTsignalApp 5.42.0") and "Numerical regression and solver-contract hardening (5.42-B)" in change,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("5.42-B static verification FAILED: " + ", ".join(failed))

# The validated electrostatic/magnetostatic kernels are deliberately byte-frozen in 5.42-B.
static_field_hashes = {
    "src/electrostatic_model.cpp": "b3e8b46a60d316895324e3f4a13b6e4ce3579dccdfeb5665b43176e0ed597356",
    "include/electrostatic_model.h": "047cb78f9f0bb89ea0640acbce9e5a866f05ea3e5907ca0e3a5a64f5742beaa3",
    "src/magnetostatic_model.cpp": "6fcf3123641254a1476308b3dacb990c0de470e8d60032bc8e9ef805cadca5af",
    "include/magnetostatic_model.h": "4eee0ae2585072cdeac4884e8cc4c959588a176796537abf0819c94cf76a03e4",
}
for rel, expected in static_field_hashes.items():
    actual = hashlib.sha256((ROOT / rel).read_bytes()).hexdigest()
    if actual != expected:
        raise SystemExit("5.42-B validated static-field kernel regression: " + rel)

for ui in (ROOT / "forms").glob("*.ui"):
    ET.parse(ui)

strict_flags = ["-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I" + str(ROOT / "include")]
solver_sources = [
    ROOT / "src/antenna_validation_bench.cpp",
    ROOT / "src/numerical_em_solvers.cpp",
    ROOT / "src/antenna_surface_geometry.cpp",
    ROOT / "src/pec_surface_mom.cpp",
    ROOT / "src/hybrid_wire_surface_mom.cpp",
    ROOT / "src/microstrip_models.cpp",
]

# The smoke executable below compiles the five production solver translation units with
# the strict flags. The validation bench is not part of that executable, so check it once here.
subprocess.run(["g++", *strict_flags, "-fsyntax-only", str(ROOT / "src/antenna_validation_bench.cpp")], check=True)

with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    smoke_exe = td / "release_smoke_542b"
    smoke_sources = [
        ROOT / "src/numerical_em_solvers.cpp",
        ROOT / "src/antenna_surface_geometry.cpp",
        ROOT / "src/pec_surface_mom.cpp",
        ROOT / "src/hybrid_wire_surface_mom.cpp",
        ROOT / "src/microstrip_models.cpp",
        ROOT / "tests/release_smoke_542b.cpp",
    ]
    subprocess.run(["g++", "-O0", *strict_flags, *map(str, smoke_sources), "-o", str(smoke_exe)], check=True)
    subprocess.run([str(smoke_exe)], check=True, timeout=180)

    # The full harness source is always syntax-checked, but its expensive full link/run
    # is reserved for --full so normal build verification stays practical.
    subprocess.run(["g++", *strict_flags, "-fsyntax-only", str(ROOT / "tests/release_full_542b.cpp")], check=True)
    if args.full:
        full_exe = td / "release_full_542b"
        full_sources = [*solver_sources, ROOT / "tests/release_full_542b.cpp"]
        subprocess.run(["g++", "-O2", *strict_flags, *map(str, full_sources), "-o", str(full_exe)], check=True)
        subprocess.run([str(full_exe)], check=True, timeout=args.full_timeout)

print("QTsignalApp 5.42-B smoke/warning-clean verification: PASS")
if args.full:
    print("QTsignalApp 5.42-B full release campaign: PASS")
else:
    print("Full canonical release harness syntax-checked but not executed. Run: python3 tests/verify_542b.py --full")

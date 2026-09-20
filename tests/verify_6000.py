from pathlib import Path
import argparse
import concurrent.futures
import hashlib
import json
import os
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description="QTsignalApp 6.0.0 Simulation / Antenna Designer 1.0 release verification")
parser.add_argument("--full", action="store_true", help="run the production-only canonical 1.0 release gate")
parser.add_argument("--full-timeout", type=int, default=1800, help="timeout in seconds for the full production release executable")
args = parser.parse_args()

project = json.loads((ROOT / "QTsignalApp.qtproject.json").read_text(encoding="utf-8"))
main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
roadmap = (ROOT / "SIMULATION_DESIGN_V1_ROADMAP.md").read_text(encoding="utf-8")
acceptance = (ROOT / "SIMULATION_DESIGN_V1_ACCEPTANCE.md").read_text(encoding="utf-8")
release = (ROOT / "RELEASE_6_0_0.md").read_text(encoding="utf-8")
change = (ROOT / "CHANGELOG_APP.md").read_text(encoding="utf-8")
bench_h = (ROOT / "include/antenna_validation_bench.h").read_text(encoding="utf-8")
bench_cpp = (ROOT / "src/antenna_validation_bench.cpp").read_text(encoding="utf-8")

checks = {
    "application version": project.get("version") == "6.0.0" and project.get("packaging", {}).get("productVersion") == "6.0.0" and 'QStringLiteral("6.0.0")' in main,
    "schema retained": 'AntennaProjectSchemaVersion = 19' in (ROOT / "src/antenna_designer_widget.cpp").read_text(encoding="utf-8"),
    "released roadmap": "Current baseline — 6.0.0 / Simulation / Antenna Designer 1.0 RELEASED" in roadmap and "FINAL MILESTONE COMPLETE" in roadmap,
    "acceptance released": "QTsignalApp 6.0.0 — Simulation / Antenna Designer 1.0 is released" in acceptance,
    "release notes": "Final production regression" in release and "Pprop/Pfree" in release,
    "changelog": "## 6.0.0 — Simulation / Antenna Designer 1.0" in change,
    "production-only API": "runProductionCase" in bench_h and "CaseReport runProductionCase" in bench_cpp,
    "release harness": (ROOT / "tests/release_6000.cpp").exists(),
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("6.0.0 static release verification FAILED: " + ", ".join(failed))

# Preserve the validated electrostatic/magnetostatic kernels across the 6.0 promotion.
static_field_hashes = {
    "src/electrostatic_model.cpp": "b3e8b46a60d316895324e3f4a13b6e4ce3579dccdfeb5665b43176e0ed597356",
    "include/electrostatic_model.h": "047cb78f9f0bb89ea0640acbce9e5a866f05ea3e5907ca0e3a5a64f5742beaa3",
    "src/magnetostatic_model.cpp": "6fcf3123641254a1476308b3dacb990c0de470e8d60032bc8e9ef805cadca5af",
    "include/magnetostatic_model.h": "4eee0ae2585072cdeac4884e8cc4c959588a176796537abf0819c94cf76a03e4",
}
for rel, expected in static_field_hashes.items():
    actual = hashlib.sha256((ROOT / rel).read_bytes()).hexdigest()
    if actual != expected:
        raise SystemExit("6.0.0 validated static-field kernel regression: " + rel)

for ui in (ROOT / "forms").glob("*.ui"):
    ET.parse(ui)

strict = ["-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I" + str(ROOT / "include")]
production_sources = [
    ROOT / "src/antenna_validation_bench.cpp",
    ROOT / "src/numerical_em_solvers.cpp",
    ROOT / "src/antenna_surface_geometry.cpp",
    ROOT / "src/pec_surface_mom.cpp",
    ROOT / "src/hybrid_wire_surface_mom.cpp",
    ROOT / "src/microstrip_models.cpp",
]

def run_checked(cmd, **kwargs):
    subprocess.run(cmd, check=True, **kwargs)

def syntax_check(src):
    run_checked(["g++", *strict, "-fsyntax-only", str(src)])

# Strict warning-clean syntax verification can run in parallel and is intentionally independent of Qt.
with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(6, os.cpu_count() or 1))) as pool:
    list(pool.map(syntax_check, [*production_sources, ROOT / "tests/release_6000.cpp", ROOT / "tests/release_smoke_542b.cpp"]))

with tempfile.TemporaryDirectory() as td_raw:
    td = Path(td_raw)

    # Build the fast RF/passivity/NaN smoke from strict O0 objects in parallel.
    smoke_sources = [
        ROOT / "src/numerical_em_solvers.cpp",
        ROOT / "src/antenna_surface_geometry.cpp",
        ROOT / "src/pec_surface_mom.cpp",
        ROOT / "src/hybrid_wire_surface_mom.cpp",
        ROOT / "src/microstrip_models.cpp",
        ROOT / "tests/release_smoke_542b.cpp",
    ]
    def compile_smoke(item):
        i, src = item
        obj = td / f"smoke_{i}.o"
        run_checked(["g++", "-O0", *strict, "-c", str(src), "-o", str(obj)])
        return obj
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(6, os.cpu_count() or 1))) as pool:
        smoke_objects = list(pool.map(compile_smoke, enumerate(smoke_sources)))
    smoke_exe = td / "release_smoke_6000"
    run_checked(["g++", *map(str, smoke_objects), "-o", str(smoke_exe)])
    run_checked([str(smoke_exe)], timeout=180)

    if args.full:
        full_sources = [*production_sources, ROOT / "tests/release_6000.cpp"]
        def compile_full(item):
            i, src = item
            obj = td / f"full_{i}.o"
            run_checked(["g++", "-O2", *strict, "-c", str(src), "-o", str(obj)])
            return obj
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(6, os.cpu_count() or 1))) as pool:
            full_objects = list(pool.map(compile_full, enumerate(full_sources)))
        full_exe = td / "release_6000"
        run_checked(["g++", *map(str, full_objects), "-o", str(full_exe)])
        run_checked([str(full_exe)], timeout=args.full_timeout)

print("QTsignalApp 6.0.0 release structure / strict-build / RF smoke: PASS")
if args.full:
    print("QTsignalApp 6.0.0 production-only Simulation / Antenna Designer 1.0 gate: PASS")
else:
    print("Full production gate not executed by this invocation. Run: python3 tests/verify_6000.py --full")

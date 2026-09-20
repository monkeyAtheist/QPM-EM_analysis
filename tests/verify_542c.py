from pathlib import Path
import argparse
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

parser = argparse.ArgumentParser(description="QTsignalApp 5.42-C Sommerfeld cache/performance verification")
parser.add_argument("--stress", action="store_true", help="fill the bounded face-table cache and require LRU eviction")
args = parser.parse_args()

project = json.loads((ROOT / "QTsignalApp.qtproject.json").read_text(encoding="utf-8"))
header = (ROOT / "include/hybrid_wire_surface_mom.h").read_text(encoding="utf-8")
hybrid = (ROOT / "src/hybrid_wire_surface_mom.cpp").read_text(encoding="utf-8")
ui = (ROOT / "src/antenna_designer_widget.cpp").read_text(encoding="utf-8")
roadmap = (ROOT / "SIMULATION_DESIGN_V1_ROADMAP.md").read_text(encoding="utf-8")
change = (ROOT / "CHANGELOG_APP.md").read_text(encoding="utf-8")

checks = {
    "version": project.get("version") == "5.42.0",
    "bounded LRU": "class BoundedLruCache" in hybrid and "cache.clear()" not in hybrid[hybrid.find("sommerfeldExteriorRadialTable"):hybrid.find("C interpolateSommerfeldInternalComponent")],
    "hot-key fast path": "m_lastEntry && key==m_lastKey" in hybrid,
    "face cache": "BoundedLruCache<Key,SommerfeldRadialTable> cache(32)" in hybrid,
    "exterior cache": "BoundedLruCache<Key,SommerfeldExteriorRadialTable> cache(24)" in hybrid,
    "internal cache": "BoundedLruCache<Key,SommerfeldInternalRadialTable> cache(32)" in hybrid,
    "diagnostic contract": all(token in header for token in ["layeredSommerfeldCacheHits", "layeredSommerfeldCacheMisses", "layeredSommerfeldCacheTableBuilds", "layeredSommerfeldCacheEvictions", "layeredSommerfeldCacheBuildTimeMs"]),
    "diagnostic UI": "5.42-C Sommerfeld cache" in ui and "Sommerfeld cache:" in ui,
    "roadmap": "5.42-C COMPLETE" in roadmap and "5.42-D COMPLETE" in roadmap,
    "changelog": "Sommerfeld cache/performance and diagnostic visibility (5.42-C)" in change,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("5.42-C static verification FAILED: " + ", ".join(failed))

strict = ["-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I" + str(ROOT / "include")]
sources = [
    ROOT / "src/numerical_em_solvers.cpp",
    ROOT / "src/antenna_surface_geometry.cpp",
    ROOT / "src/pec_surface_mom.cpp",
    ROOT / "src/hybrid_wire_surface_mom.cpp",
    ROOT / "src/microstrip_models.cpp",
    ROOT / "tests/release_cache_542c.cpp",
]
with tempfile.TemporaryDirectory() as td:
    exe = Path(td) / "release_cache_542c"
    subprocess.run(["g++", "-O0", *strict, *map(str, sources), "-o", str(exe)], check=True)
    cmd = [str(exe)] + (["--stress"] if args.stress else [])
    subprocess.run(cmd, check=True, timeout=180)

print("QTsignalApp 5.42-C bounded-cache/performance verification: PASS")
if not args.stress:
    print("Optional bounded-LRU eviction stress: python3 tests/verify_542c.py --stress")

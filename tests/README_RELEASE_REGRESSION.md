# QTsignalApp 6.2.0 workflow regression

Primary command:

```bash
python3 tests/verify_6200.py
```

This release check verifies the 6.2.0 package/version wiring, Calculator / Notes UI strings, persistent note-editor plumbing, explicit `exp(x)` and `e^(x)` calculator access, and Calculator → Antenna Designer starter-geometry handoff. It compiles `em_calculator_model.cpp` plus `release_calculator_6200.cpp` with strict C++17 warning flags and numerically re-runs the inherited wave/antenna/parser tests plus exponential checks.

The previously validated numerical EM kernels are SHA-256 guarded and must remain unchanged. Full Qt GUI compilation is still performed with the target Qt/QPM toolchain because the lightweight regression environment does not provide the Qt SDK.

Older release scripts remain available for historical validation campaigns.

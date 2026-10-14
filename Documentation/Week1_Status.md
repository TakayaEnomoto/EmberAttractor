# Week 1 — verification and first profiler capture

Verified on 14 October 2026 with Unreal Engine 5.7.4. This is an independent C++/Niagara prototype; it does not depend on Gravity files.

## Milestone evidence

| Assignment requirement | Implemented and checked |
|---|---|
| Two systems running in a test map | `/Game/EmberLab/L_EmberLab`: GPU sprite embers and CPU mesh debris. Reloaded saved assets in a real D3D12 game process. |
| Runtime particle-count control | `ember.GPUCount`, `ember.CPUCount`, and `[` / `]` presets. Debug HUD checks observed GPU 100,000 → 200,000 and CPU 500 → 1,000. |
| First profiler capture | Four completed 1080p captures: baseline, GPU-only, CPU-only, combined; Unreal Insights traces, native CSVs, wall-time CSVs and screenshots retained. |

Additional checks: mode 0 removed all particles; modes 1/2 independently disabled the other system; mode 3 ran both. R resets the bursts. The mouse positions the attraction field; holding left mouse applies attraction. Automatic moving-attractor captures exercise the same position/strength parameters. GPU rules are attraction and drag; CPU adds collision. The prototype has no thermal-state logic, recovery scoring, or particle-to-gameplay export yet.

## Initial measurements

Hardware: Intel Core Ultra 7 265F, NVIDIA GeForce RTX 5070 Ti, approximately 32 GB RAM, Windows 11. Editor Development game, D3D12, 1920×1080, VSync off, fixed camera. Runs were sequential, with real offscreen rendering, a 10-second warmup and a 20-second measured window. Tracing, named events, CSV collection and stat overlays were enabled. These are instrumented workstation measurements, not certification on course lab hardware.

| Mode | Samples | Wall mean (ms) | Wall P95 (ms) | Whole GPU mean (ms) | System GT + RT aggregate (ms) | Niagara GPU simulation (ms) |
|---|---:|---:|---:|---:|---:|---:|
| Baseline | 7,868 | 2.543 | 3.317 | 1.580 | 0.000 | — |
| GPU 100,000 | 6,213 | 3.220 | 3.821 | 2.311 | 0.108 | 0.0196 |
| CPU 500 | 5,835 | 3.428 | 4.287 | 1.728 | 0.442 | — |
| Both: 100,000 + 500 | 5,537 | 3.613 | 4.519 | 2.402 | 0.565 | 0.0199 |

Wall means/P95 retain all positive samples, including screenshot-associated frames. A screenshot is requested at capture start; the first three samples are explicitly flagged. Maximum observed wall frames by mode are 0: 440.8 ms, 1: 513.5 ms, 2: 527.9 ms, 3: 391.1 ms. These stalls must not be presented as hitch-free 60 fps. Excluding only that flagged window gives mean wall times 0: 2.487 ms, 1: 3.138 ms, 2: 3.338 ms, 3: 3.543 ms; the unfiltered data remains available.

`GPUTime` is whole-frame GPU timing. `GPU/NiagaraGPUSimulation` covers the Niagara simulation pass, not sprite/mesh rendering or the whole GPU effect. Native `Particles/Total/<system>` and `Particles/RT/<system>` values are microseconds and are converted to milliseconds above. The GT aggregate includes concurrent and end-of-frame work; it is not game-thread wall time. System costs are added for a preliminary workload check, not a replacement for examining the Insights critical path. The 300-frame ParticlePerf test in each log is a shorter sample; the table uses the full native CSV window.

The measured average system GT+RT aggregate is below the assignment’s 4 ms target for this initial workload. Typical frame timings are below 16.6 ms here, but screenshot stalls, profiler overhead, hardware differences, and the unfinished Week 2 workload prevent a final budget claim. A five-level scaling sweep, breaking point and controlled optimization comparison remain future work.

## Evidence and reproduction

Frozen small evidence files and checksums: [Evidence/Week1/Manifest.json](../Evidence/Week1/Manifest.json). It maps each mode to raw frame CSV, native engine CSV, log, screenshot and its `.utrace` path. Large traces remain under `Saved/Profiling` to avoid duplicate copies; preserve that directory when handing over the captures. Earlier failed/obsolete trials are under `Saved/Profiling/Discarded_InitializationRuns` and are excluded from this report.

Run `Tools/Capture.ps1 -Mode 3 -Seconds 20 -Offscreen` to repeat the combined capture; use modes 0, 1 and 2 separately for the others. Open the `.utrace` files in Unreal Insights. `Tools/MakeWeek1Report.py` regenerates this report and the evidence manifest from completed root-level captures. Never profile with NullRHI; it was used only for asset authoring.

## Before presenting

- Open [Proposal.pdf](Proposal.pdf) and [the Chinese demo guide](Demo_Guide_zh.md). Launch `Play_Lab.cmd` for the interactive prototype, or open the `.uproject` and press Play.
- Ask the instructor to accept the custom ember-attraction direction: the published Track B specifically names boids.
- Personally play/watch at least two reference games and add genuine observations. The proposal lists references but does not claim this student task is complete.
- This is a Week 1 technical prototype, not a final explosion effect. Thermal state, recovery outcomes, verified two-way feedback, full count sweep, optimization, report/video and release are later milestones.
- Bursts have a ten-minute test lifetime. R restarts them; F enables repeated eight-second bursts for demonstration.

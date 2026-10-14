# Ember Attractor

An independent Unreal Engine **5.7.4** / C++ / Niagara laboratory for the first milestone of *Niagara at Scale*. No Gravity assets or code are required. The relationship to Gravity is design inspiration and potential future reuse, not an existing integration.

**Current milestone: End of Week 1.** Two systems run in the test map, particle counts can be changed at runtime, and initial profiler captures are included. This is a proposed custom standalone variation of Track B, subject to instructor approval; it is not the final three-week submission.

## Clone and build on another computer

Prerequisites: Windows, Unreal Engine **5.7** (tested with **5.7.4**), Git, and Visual Studio 2022 with the **Game development with C++** workload, MSVC toolchain and Windows SDK. Sign in to GitHub with an account that has access to this private repository.

```powershell
git clone https://github.com/TakayaEnomoto/EmberAttractor.git
cd EmberAttractor
powershell -NoProfile -File .\Tools\Build.ps1
```

The build script discovers UE 5.7 through Epic's installation manifest or the Windows registry. If it is installed elsewhere, set its installation root before building:

```powershell
$env:UE57_ROOT = 'D:\Games\EpicGames\UE_5.7'
powershell -NoProfile -File .\Tools\Build.ps1
```

After a successful build, double-click `Open_Editor.cmd`, open the startup map and press **Play**, or double-click `Play_Lab.cmd` for the standalone demonstration. The first launch may compile shaders. Niagara assets and the map are committed, so **do not regenerate assets just to open a fresh clone**. No Gravity checkout or external asset pack is needed.

Compiled binaries, intermediate files and shader caches are not versioned. A fresh clone therefore needs the C++ build before either launcher can work. Close the editor/game before pulling updates and rebuilding:

```powershell
git pull --ff-only
powershell -NoProfile -File .\Tools\Build.ps1
```

## Open and play

Double-click `Play_Lab.cmd` to launch the demonstration, or `Open_Editor.cmd` to edit it. The Chinese walkthrough is in [Documentation/Demo_Guide_zh.md](Documentation/Demo_Guide_zh.md). Verified milestone results and the first four captures are documented in [Documentation/Week1_Status.md](Documentation/Week1_Status.md).

Open `EmberAttractor.uproject` with Unreal Engine 5.7. The startup map is `/Game/EmberLab/L_EmberLab`. Press **Play**, then click the game viewport. For consistent full-HD measurements, run `Tools/Play.ps1` in PowerShell instead; it launches the map as a standalone editor game at 1920 x 1080.

| Control | Action |
|---|---|
| Move mouse | Position the attraction point on the chamber's interaction plane |
| Hold left mouse | Apply attraction to the simulated particles |
| 0 / 1 / 2 / 3 | Baseline / GPU only / CPU only / both systems |
| [ / ] | Decrease / increase the selected systems' count presets, resetting their bursts |
| R | Restart the particle burst |
| F | Toggle an eight-second repeat cycle |
| C | Start / stop a frame-time CSV capture |
| Tilde | Open the Unreal console |

The HUD reports **target** counts, not a fabricated live count. Use the Niagara Debugger overview for live simulation information. Turning a system off immediately removes its old particles, so baseline mode does not retain long-lived particles.

## Runtime console controls

Open the Unreal console with the tilde key. The following variables allow graders to reproduce the workload without editing assets:

| Variable | Default | Meaning / accepted range |
|---|---:|---|
| `ember.Mode` | `3` | `0`: no particles; `1`: GPU only; `2`: CPU only; `3`: both |
| `ember.GPUCount` | `100000` | GPU burst target, clamped to 0–1,000,000; changes restart the simulation |
| `ember.CPUCount` | `500` | CPU burst target, clamped to 0–100,000; changes restart the simulation |
| `ember.AttractionStrength` | `350` | Nonnegative attraction strength, applied while the left mouse button is held |

The accepted count limits are input limits, not performance guarantees. The assignment's target workloads are 100,000+ GPU particles and 500–5,000 CPU particles.

```
ember.Mode 3
ember.GPUCount 100000
ember.CPUCount 500
ember.AttractionStrength 350
stat Niagara
stat GPU
fx.Niagara.Debug.Hud Enabled=1 OverviewEnabled=1 OverviewLocation=450,30
fx.ParticlePerfStats.RunTest 300 1 1 1
```

Counts reset the affected simulations on change. GPU presets: 10k, 25k, 50k, 100k, 200k. CPU presets: 500, 1k, 2k, 3.5k, 5k. The default combined case is 100,000 GPU sprites plus 500 CPU meshes.

The GPU simulation uses attraction and drag. The CPU simulation additionally has a collision module. Bursts use a ten-minute laboratory lifetime; press R to restart or F for an eight-second repeat. Thermal state and recovery events are planned for Week 2.

## Rebuild and regenerate assets

Requirements: UE 5.7, Visual Studio 2022 C++ toolchain, Windows SDK. `Tools/Engine.ps1` discovers the installed engine from Epic's installation manifest or the registry; set `UE57_ROOT` if necessary. No engine source modifications are required.

1. Close this project's editor/game before rebuilding.
2. Run `Tools/Build.ps1`.
3. If regenerating the original assets, run `Tools/BuildAssets.ps1`.
4. Open `EmberAttractor.uproject` or run `Tools/Play.ps1`.

**Regeneration replaces this lab's generated Niagara assets and map.** Save authored variations under other names before rerunning the generator. Normal use does not require regeneration. Asset generation uses Unreal's editor APIs; binary assets are never hand-edited.

## Capture protocol

`Tools/Capture.ps1 -Mode 3 -Seconds 20` performs a deterministic moving-attractor test at 1080p. Modes 0, 1, 2 and 3 cover the empty baseline and the two systems separately/combined. `-Offscreen` suppresses the game window while retaining real rendering; do not use `-NullRHI` for performance measurements.

The script enables Unreal Insights CPU/GPU/frame traces, CSV profiling, `stat Niagara`, `stat GPU`, and the Niagara Debugger overlay. A ten-second warmup precedes the lab's frame samples. At capture start, a screenshot is requested and a 300-frame engine particle-performance test begins. The first three screenshot-associated sample frames are flagged, not silently removed. All positive frame times, including long hitches, are retained. The process waits for sufficient frames for the engine test, subject to a 90-second watchdog.

Outputs go to `Saved/Profiling`: `.utrace`, log, screenshot, frame-time CSV, plus engine-generated CSV/performance output directories. Open `.utrace` in Unreal Insights. Per-system particle stats in the log distinguish Niagara cost from whole-frame game/render-thread timing in the lab CSV; these are **not interchangeable**. GPU trace/stat evidence is required for GPU costs. The CSV alone cannot prove the 4 ms Niagara budget.

The repository includes the frozen Week 1 CSVs, logs, screenshots and manifest in `Evidence/Week1`. Large `.utrace` recordings live in the original workstation's ignored `Saved/Profiling` directory and **are not downloaded by cloning this repository**. Generate new traces on the classroom computer with `Tools/Capture.ps1`; run one mode at a time. The manifest's trace paths describe those original local recordings, while its CSV/log/screenshot paths are included in Git.

Budget: 60 fps / 16.6 ms at 1080p on lab machines; Niagara combined game/render-thread cost <= 4 ms. Current workstation samples are preliminary and do not establish lab-machine compliance. See `Documentation/Week1_Status.md` for verified results and remaining work.

## Project contents

- `Content/EmberLab`: two generated Niagara systems and the test map.
- `Source/EmberAttractor`: runtime actor, input/count controls, HUD and capture instrumentation.
- `Source/EmberAttractorEditor`: reproducible Niagara/map asset builder.
- `Documentation/Proposal.pdf`: one-page proposal; Markdown source is alongside it.
- `Documentation/Week1_Status.md`: verified milestone checklist, timings and interpretation limits.
- `Evidence/Week1`: captured CSVs, logs, screenshots and a manifest linking the large Insights traces.
- `Tools`: build, asset generation, play, capture and proposal-rendering helpers.

## Scope after Week 1

The completed assignment still needs end-to-end outcomes driven by particle state, GPU feedback validation, the complete count sweep/breaking point, measured optimization, final report/video and tagged release. The published Track B specifies boids; the custom ember-attraction variation needs instructor acceptance. Official reference sources are in the proposal; the student's required play/watch observations should be added after reviewing the two reference games.

## Assignment submission checklist

| Milestone / deliverable | Status |
|---|---|
| Day 3: one-page proposal with track, mechanic, reference, budget and gameplay data path | [Proposal](Documentation/Proposal.pdf) prepared; custom-track acceptance and the student's two-game review remain to be confirmed |
| End of Week 1: both systems in a test map, runtime count control, first profiler capture | Implemented and recorded in [Week 1 verification](Documentation/Week1_Status.md) |
| End of Week 2: end-to-end mechanic and complete scaling sweep | Planned; at least five particle-count levels per system, extending to the budget failure point |
| Week 3: measured optimization | Planned; preserve comparable before/after measurements |
| Final Git repository with a tagged release and build/console-variable README | Repository and README available; final release tag remains to be created |
| Performance report, maximum two pages | Pending: budget, frame time against count for both systems, optimization comparison and another-week plan |
| Captured video, 60–90 seconds | Pending; include a segment with the stat overlay visible |

The current branch is the Week 1 working version. A final release tag should identify the completed, verified submission after the remaining requirements are met.

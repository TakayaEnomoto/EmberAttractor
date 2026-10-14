"""Build a traceable Week 1 report from the completed, non-discarded captures."""
import csv
import hashlib
import json
import re
import shutil
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAPTURES = ROOT / 'Saved' / 'Profiling'
EVIDENCE = ROOT / 'Evidence' / 'Week1'
EVIDENCE.mkdir(parents=True, exist_ok=True)
NAMES = ['Baseline', 'GPU 100,000', 'CPU 500', 'Both: 100,000 + 500']


def mean(rows, column):
    values = [float(row[column]) for row in rows if row.get(column)]
    return statistics.mean(values) if values else None


def copy_evidence(source, destination):
    target = EVIDENCE / destination
    shutil.copy2(source, target)
    return {
        'path': target.relative_to(ROOT).as_posix(),
        'source': source.relative_to(ROOT).as_posix(),
        'sha256': hashlib.sha256(target.read_bytes()).hexdigest(),
        'bytes': target.stat().st_size,
    }


records = []
for mode in range(4):
    log = sorted(CAPTURES.glob(f'Ember_mode{mode}_*.log'))[-1]
    log_text = log.read_text(encoding='utf-8-sig')
    assert 'Log file closed' in log_text, f'Capture is still running: {log}'
    native_name = re.findall(r'Writing CSV to file : .*?/(Profile\([^\r\n]+\.csv)', log_text)[-1]
    native_path = CAPTURES / 'CSV' / native_name
    app_name = re.findall(r'saved \d+ frame-time samples to .*?/(EmberLab_[^\r\n]+\.csv)', log_text)[-1]
    app_path = CAPTURES / app_name
    with app_path.open(encoding='utf-8-sig', newline='') as stream:
        app_rows = list(csv.DictReader(stream))
    with native_path.open(encoding='utf-8-sig', newline='') as stream:
        native_rows = []
        for row in csv.DictReader(stream):
            try:
                float(row.get('FrameTime', ''))
            except (ValueError, TypeError):
                continue
            if 'CsvProfile STOP' not in row.get('EVENTS', ''):
                native_rows.append(row)
    assert native_rows and app_rows
    wall = sorted(float(row['frame_wall_ms']) for row in app_rows)
    clean_rows = [row for row in app_rows if row['screenshot_request_window'] == '0']
    systems = {}
    for name in ('NS_Embers_GPU', 'NS_Debris_CPU'):
        gt = mean(native_rows, f'Particles/Total/{name}')
        rt = mean(native_rows, f'Particles/RT/{name}')
        if gt is not None:
            systems[name] = {'gt_aggregate_us': gt, 'rt_us': rt}
    combined_cpu = sum(item['gt_aggregate_us'] + (item['rt_us'] or 0) for item in systems.values()) / 1000
    screenshot = sorted(CAPTURES.glob(f'EmberLab_*_mode{mode}.png'))[-1]
    trace = log.with_suffix('.utrace')
    assert trace.stat().st_size > 0
    record = {
        'mode': mode, 'label': NAMES[mode], 'samples': len(app_rows),
        'mean_wall_ms': statistics.mean(wall),
        'p95_wall_ms': wall[min(len(wall)-1, int(len(wall)*0.95))],
        'max_wall_ms': max(wall),
        'mean_wall_excluding_screenshot_window_ms': mean(clean_rows, 'frame_wall_ms'),
        'native_samples': len(native_rows),
        'native_frame_mean_ms': mean(native_rows, 'FrameTime'),
        'whole_gpu_mean_ms': mean(native_rows, 'GPUTime'),
        'niagara_gpu_simulation_mean_ms': mean(native_rows, 'GPU/NiagaraGPUSimulation'),
        'niagara_system_gt_plus_rt_mean_ms': combined_cpu,
        'system_timings': systems,
        'evidence': [copy_evidence(log, f'mode{mode}.log'),
                     copy_evidence(app_path, f'mode{mode}_frames.csv'),
                     copy_evidence(native_path, f'mode{mode}_engine.csv'),
                     copy_evidence(screenshot, f'mode{mode}_capture.png')],
        'trace': {'path': trace.relative_to(ROOT).as_posix(), 'bytes': trace.stat().st_size},
    }
    records.append(record)

asset_hashes = {
    path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
    for path in sorted((ROOT / 'Content' / 'EmberLab').glob('*')) if path.is_file()
}
(EVIDENCE / 'Manifest.json').write_text(json.dumps({'captures': records, 'asset_sha256': asset_hashes}, indent=2), encoding='utf-8')

lines = [
    '# Week 1 — verification and first profiler capture',
    '',
    'Verified on 14 October 2026 with Unreal Engine 5.7.4. This is an independent C++/Niagara prototype; it does not depend on Gravity files.',
    '',
    '## Milestone evidence',
    '',
    '| Assignment requirement | Implemented and checked |',
    '|---|---|',
    '| Two systems running in a test map | `/Game/EmberLab/L_EmberLab`: GPU sprite embers and CPU mesh debris. Reloaded saved assets in a real D3D12 game process. |',
    '| Runtime particle-count control | `ember.GPUCount`, `ember.CPUCount`, and `[` / `]` presets. Debug HUD checks observed GPU 100,000 → 200,000 and CPU 500 → 1,000. |',
    '| First profiler capture | Four completed 1080p captures: baseline, GPU-only, CPU-only, combined; Unreal Insights traces, native CSVs, wall-time CSVs and screenshots retained. |',
    '',
    'Additional checks: mode 0 removed all particles; modes 1/2 independently disabled the other system; mode 3 ran both. R resets the bursts. The mouse positions the attraction field; holding left mouse applies attraction. Automatic moving-attractor captures exercise the same position/strength parameters. GPU rules are attraction and drag; CPU adds collision. The prototype has no thermal-state logic, recovery scoring, or particle-to-gameplay export yet.',
    '',
    '## Initial measurements',
    '',
    'Hardware: Intel Core Ultra 7 265F, NVIDIA GeForce RTX 5070 Ti, approximately 32 GB RAM, Windows 11. Editor Development game, D3D12, 1920×1080, VSync off, fixed camera. Runs were sequential, with real offscreen rendering, a 10-second warmup and a 20-second measured window. Tracing, named events, CSV collection and stat overlays were enabled. These are instrumented workstation measurements, not certification on course lab hardware.',
    '',
    '| Mode | Samples | Wall mean (ms) | Wall P95 (ms) | Whole GPU mean (ms) | System GT + RT aggregate (ms) | Niagara GPU simulation (ms) |',
    '|---|---:|---:|---:|---:|---:|---:|',
]
for item in records:
    simulation = item['niagara_gpu_simulation_mean_ms']
    simulation_text = f'{simulation:.4f}' if simulation is not None else '—'
    lines.append(f"| {item['label']} | {item['samples']:,} | {item['mean_wall_ms']:.3f} | {item['p95_wall_ms']:.3f} | {item['whole_gpu_mean_ms']:.3f} | {item['niagara_system_gt_plus_rt_mean_ms']:.3f} | {simulation_text} |")
lines += [
    '',
    'Wall means/P95 retain all positive samples, including screenshot-associated frames. A screenshot is requested at capture start; the first three samples are explicitly flagged. Maximum observed wall frames by mode are ' + ', '.join(f"{item['mode']}: {item['max_wall_ms']:.1f} ms" for item in records) + '. These stalls must not be presented as hitch-free 60 fps. Excluding only that flagged window gives mean wall times ' + ', '.join(f"{item['mode']}: {item['mean_wall_excluding_screenshot_window_ms']:.3f} ms" for item in records) + '; the unfiltered data remains available.',
    '',
    '`GPUTime` is whole-frame GPU timing. `GPU/NiagaraGPUSimulation` covers the Niagara simulation pass, not sprite/mesh rendering or the whole GPU effect. Native `Particles/Total/<system>` and `Particles/RT/<system>` values are microseconds and are converted to milliseconds above. The GT aggregate includes concurrent and end-of-frame work; it is not game-thread wall time. System costs are added for a preliminary workload check, not a replacement for examining the Insights critical path. The 300-frame ParticlePerf test in each log is a shorter sample; the table uses the full native CSV window.',
    '',
    'The measured average system GT+RT aggregate is below the assignment’s 4 ms target for this initial workload. Typical frame timings are below 16.6 ms here, but screenshot stalls, profiler overhead, hardware differences, and the unfinished Week 2 workload prevent a final budget claim. A five-level scaling sweep, breaking point and controlled optimization comparison remain future work.',
    '',
    '## Evidence and reproduction',
    '',
    'Frozen small evidence files and checksums: [Evidence/Week1/Manifest.json](../Evidence/Week1/Manifest.json). It maps each mode to raw frame CSV, native engine CSV, log, screenshot and its `.utrace` path. Large traces remain under `Saved/Profiling` to avoid duplicate copies; preserve that directory when handing over the captures. Earlier failed/obsolete trials are under `Saved/Profiling/Discarded_InitializationRuns` and are excluded from this report.',
    '',
    'Run `Tools/Capture.ps1 -Mode 3 -Seconds 20 -Offscreen` to repeat the combined capture; use modes 0, 1 and 2 separately for the others. Open the `.utrace` files in Unreal Insights. `Tools/MakeWeek1Report.py` regenerates this report and the evidence manifest from completed root-level captures. Never profile with NullRHI; it was used only for asset authoring.',
    '',
    '## Before presenting',
    '',
    '- Open [Proposal.pdf](Proposal.pdf) and [the Chinese demo guide](Demo_Guide_zh.md). Launch `Play_Lab.cmd` for the interactive prototype, or open the `.uproject` and press Play.',
    '- Ask the instructor to accept the custom ember-attraction direction: the published Track B specifically names boids.',
    '- Personally play/watch at least two reference games and add genuine observations. The proposal lists references but does not claim this student task is complete.',
    '- This is a Week 1 technical prototype, not a final explosion effect. Thermal state, recovery outcomes, verified two-way feedback, full count sweep, optimization, report/video and release are later milestones.',
    '- Bursts have a ten-minute test lifetime. R restarts them; F enables repeated eight-second bursts for demonstration.',
]
(ROOT / 'Documentation' / 'Week1_Status.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print(json.dumps([{key: value for key, value in item.items() if key not in ('evidence', 'trace')} for item in records], indent=2))

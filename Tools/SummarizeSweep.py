"""Summarize the scaling sweep and bounds captures written by Tools/Sweep.ps1.

Writes Evidence/Final (frozen CSVs, gzip-compressed engine CSVs, logs, manifest), Documentation/Sweep.md and the
frame-time chart Documentation/SweepChart.svg. Niagara cost is the per-system
game-thread aggregate plus render-thread time from the engine CSV, never wall time.
"""
import csv
import gzip
import hashlib
import json
import math
import re
import shutil
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAPTURES = ROOT / 'Saved' / 'Profiling'
EVIDENCE = ROOT / 'Evidence' / 'Final'
DOCS = ROOT / 'Documentation'
FRAME_BUDGET_MS = 16.6
NIAGARA_BUDGET_MS = 4.0


def mean(values):
    values = [value for value in values if value is not None]
    return statistics.mean(values) if values else None


def number(row, key):
    try:
        return float(row[key])
    except (KeyError, TypeError, ValueError):
        return None


def latest_logs():
    runs = {}
    for log in sorted(CAPTURES.glob('Ember_*_*.log')):
        match = re.match(r'Ember_(.+)_\d{8}_\d{6}\.log$', log.name)
        if match:
            runs[match.group(1)] = log  # sorted by timestamp, so the newest wins
    return runs


def load_run(label, log):
    text = log.read_text(encoding='utf-8-sig', errors='replace')
    if 'Log file closed' not in text:
        raise RuntimeError(f'{log.name} is incomplete')
    native = Path(re.findall(r'Writing CSV to file : (.+?\.csv)', text)[-1].strip())
    if not native.is_absolute():
        native = (CAPTURES / 'CSV' / native.name)
    lab = Path(re.findall(r'saved \d+ frame-time samples to (.+?\.csv)', text)[-1].strip())
    with lab.open(encoding='utf-8-sig', newline='') as stream:
        lab_rows = list(csv.DictReader(stream))
    with native.open(encoding='utf-8-sig', newline='') as stream:
        native_rows = [row for row in csv.DictReader(stream)
                       if number(row, 'FrameTime') is not None and 'CsvProfile STOP' not in row.get('EVENTS', '')]
    clean = [row for row in lab_rows if row['screenshot_request_window'] == '0']
    wall = sorted(float(row['frame_wall_ms']) for row in clean)
    systems = sorted({key.split('/')[-1] for key in native_rows[0] if key.startswith('Particles/Total/')})
    niagara_ms = 0.0
    per_system = {}
    for system in systems:
        total_us = mean(number(row, f'Particles/Total/{system}') for row in native_rows) or 0.0
        rt_us = mean(number(row, f'Particles/RT/{system}') for row in native_rows) or 0.0
        count = mean(number(row, f'Particles/Count/{system}') for row in native_rows)
        per_system[system] = {'gt_aggregate_ms': total_us / 1000, 'rt_ms': rt_us / 1000, 'mean_live_count': count}
        niagara_ms += (total_us + rt_us) / 1000
    first = lab_rows[0]
    record = {
        'label': label,
        'mode': int(first['mode']),
        'gpu_count': int(first['gpu_target_count']),
        'cpu_count': int(first['cpu_target_count']),
        'dynamic_bounds': 'NS_Debris_CPU_DynamicBounds' in systems,
        'gpu_port': 'NS_Debris_GPUPort' in systems,
        'samples': len(clean),
        'wall_mean_ms': statistics.mean(wall),
        'wall_p95_ms': wall[min(len(wall) - 1, int(len(wall) * 0.95))],
        'frame_mean_ms': mean(number(row, 'FrameTime') for row in native_rows),
        'game_thread_ms': mean(number(row, 'GameThreadTime') for row in native_rows),
        'render_thread_ms': mean(number(row, 'RenderThreadTime') for row in native_rows),
        'gpu_frame_ms': mean(number(row, 'GPUTime') for row in native_rows),
        'niagara_gpu_sim_ms': mean(number(row, 'GPU/NiagaraGPUSimulation') for row in native_rows),
        'niagara_gt_rt_ms': niagara_ms,
        'systems': per_system,
    }
    record['within_budget'] = (record['wall_p95_ms'] <= FRAME_BUDGET_MS
                               and record['niagara_gt_rt_ms'] <= NIAGARA_BUDGET_MS)
    record['evidence'] = []
    # Engine CSVs are several MB each, so they are frozen gzip-compressed.
    for source, suffix in ((log, 'log'), (lab, 'frames.csv'), (native, 'engine.csv.gz')):
        target = EVIDENCE / f'{label}.{suffix}'
        if suffix.endswith('.gz'):
            with source.open('rb') as raw, gzip.open(target, 'wb') as packed:
                shutil.copyfileobj(raw, packed)
        else:
            shutil.copy2(source, target)
        record['evidence'].append({'path': target.relative_to(ROOT).as_posix(),
                                   'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
    return record


def fmt(value, digits=2):
    return '—' if value is None else f'{value:.{digits}f}'


def chart(gpu, cpu, port):
    """Frame time (wall P95) against particle count, log-scaled count axis, as standalone SVG."""
    width, height, left, right, top, bottom = 760, 250, 56, 20, 16, 44
    plot_w, plot_h = width - left - right, height - top - bottom
    series = [('GPU sprites (mode 1)', '#d9531e', gpu), ('CPU meshes (mode 2)', '#2f6db3', cpu),
              ('Meshes ported to GPU (optimization)', '#2e8b57', port)]
    series = [entry for entry in series if entry[2]]
    counts = [run[0] for _, _, runs in series for run in runs]
    lo, hi = math.log10(min(counts)), math.log10(max(counts))
    y_max = max(FRAME_BUDGET_MS * 1.25, max(run[1] for _, _, runs in series for run in runs) * 1.1)
    x = lambda count: left + (math.log10(count) - lo) / (hi - lo) * plot_w
    y = lambda ms: top + plot_h - ms / y_max * plot_h
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" font-family="Segoe UI, Arial, sans-serif" font-size="11">',
             f'<rect width="{width}" height="{height}" fill="#ffffff"/>']
    for tick in range(0, int(y_max) + 1, 4):
        parts.append(f'<line x1="{left}" x2="{width - right}" y1="{y(tick):.1f}" y2="{y(tick):.1f}" stroke="#e6e8ec"/>')
        parts.append(f'<text x="{left - 8}" y="{y(tick) + 4:.1f}" text-anchor="end" fill="#5b6472">{tick}</text>')
    for exponent in range(math.floor(lo), math.ceil(hi) + 1):
        for mantissa in (1, 2, 5):
            count = mantissa * 10 ** exponent
            if 10 ** lo <= count <= 10 ** hi:
                label = (str(count) if count < 1000 else f'{count // 1000}k' if count < 1_000_000
                         else f'{count // 1_000_000}M')
                parts.append(f'<text x="{x(count):.1f}" y="{top + plot_h + 18}" text-anchor="middle" fill="#5b6472">{label}</text>')
    parts.append(f'<line x1="{left}" x2="{width - right}" y1="{y(FRAME_BUDGET_MS):.1f}" y2="{y(FRAME_BUDGET_MS):.1f}" stroke="#b42318" stroke-dasharray="6 4"/>')
    parts.append(f'<text x="{width - right}" y="{y(FRAME_BUDGET_MS) - 6:.1f}" text-anchor="end" fill="#b42318">16.6 ms budget (60 fps)</text>')
    for index, (name, color, runs) in enumerate(series):
        points = ' '.join(f'{x(c):.1f},{y(ms):.1f}' for c, ms in runs)
        parts.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="2.2"/>')
        for c, ms in runs:
            parts.append(f'<circle cx="{x(c):.1f}" cy="{y(ms):.1f}" r="3.5" fill="{color}"/>')
        parts.append(f'<rect x="{left + 12}" y="{top + 8 + index * 18}" width="12" height="3" fill="{color}"/>')
        parts.append(f'<text x="{left + 30}" y="{top + 13 + index * 18}" fill="#253247">{name}</text>')
    parts.append(f'<text x="{left + plot_w / 2}" y="{height - 10}" text-anchor="middle" fill="#253247">Particle count (log scale)</text>')
    parts.append(f'<text transform="translate(16 {top + plot_h / 2}) rotate(-90)" text-anchor="middle" fill="#253247">Frame time, wall P95 (ms)</text>')
    parts.append('</svg>')
    return '\n'.join(parts)


def main():
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    records = [load_run(label, log) for label, log in latest_logs().items()
               if label.startswith(('sweep_', 'bounds_', 'port_'))]
    by_label = {record['label']: record for record in records}
    gpu = sorted((r for r in records if r['label'].startswith('sweep_gpu_')), key=lambda r: r['gpu_count'])
    cpu = sorted((r for r in records if r['label'].startswith('sweep_cpu_')), key=lambda r: r['cpu_count'])
    port = sorted((r for r in records if r['label'].startswith('port_gpu_')), key=lambda r: r['cpu_count'])
    (EVIDENCE / 'Manifest.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
    (DOCS / 'SweepChart.svg').write_text(chart([(r['gpu_count'], r['wall_p95_ms']) for r in gpu],
                                               [(r['cpu_count'], r['wall_p95_ms']) for r in cpu],
                                               [(r['cpu_count'], r['wall_p95_ms']) for r in port]), encoding='utf-8')

    header = ('| Count | Wall mean | Wall P95 | Game thread | Render thread | GPU frame | Niagara GT+RT | Niagara GPU sim | Budget |\n'
              '|---:|---:|---:|---:|---:|---:|---:|---:|:---:|')
    def row(record, count):
        return (f"| {count:,} | {fmt(record['wall_mean_ms'])} | {fmt(record['wall_p95_ms'])} | {fmt(record['game_thread_ms'])} | "
                f"{fmt(record['render_thread_ms'])} | {fmt(record['gpu_frame_ms'])} | {fmt(record['niagara_gt_rt_ms'], 3)} | "
                f"{fmt(record['niagara_gpu_sim_ms'], 3)} | {'pass' if record['within_budget'] else '**fail**'} |")
    lines = ['# Scaling sweep', '',
             'Generated by `Tools/SummarizeSweep.py` from `Tools/Sweep.ps1` captures (lab sandbox, 1080p, auto-moving attractor). All times in ms, means over the '
             'capture window unless noted; P95 excludes the three flagged screenshot frames. Budget = wall P95 <= 16.6 ms '
             'and Niagara GT+RT <= 4 ms.', '']
    baseline = by_label.get('sweep_baseline')
    if baseline:
        lines += ['## Baseline (no particles)', '', header, row(baseline, 0), '']
    lines += ['## System 1: GPU sprites (mode 1)', '', header] + [row(r, r['gpu_count']) for r in gpu] + ['']
    lines += ['## System 2: CPU meshes (mode 2)', '', header] + [row(r, r['cpu_count']) for r in cpu] + ['']
    lines += ['## Optimization: CPU fixed vs dynamic bounds', '',
              '| CPU count | Bounds | Wall mean | Niagara GT+RT | NS_Debris GT aggregate | NS_Debris RT |', '|---:|---|---:|---:|---:|---:|']
    for record in sorted((r for r in records if r['label'].startswith('bounds_dynamic_')), key=lambda r: r['cpu_count']):
        fixed = by_label.get(f"sweep_cpu_{record['cpu_count']}")
        for name, run in (('dynamic (before)', record), ('fixed (after)', fixed)):
            if not run:
                continue
            system = next(iter(run['systems'].values()))
            lines.append(f"| {run['cpu_count']:,} | {name} | {fmt(run['wall_mean_ms'])} | {fmt(run['niagara_gt_rt_ms'], 3)} | "
                         f"{fmt(system['gt_aggregate_ms'], 3)} | {fmt(system['rt_ms'], 3)} |")
    lines += ['', '## Optimization: debris ported from CPU to GPU simulation', '',
              '| Meshes | CPU sim: Niagara GT+RT | GPU port: Niagara GT+RT | CPU sim: wall P95 | GPU port: wall P95 | GPU port: Niagara GPU sim |',
              '|---:|---:|---:|---:|---:|---:|']
    for record in port:
        before = by_label.get(f"sweep_cpu_{record['cpu_count']}")
        if before:
            lines.append(f"| {record['cpu_count']:,} | {fmt(before['niagara_gt_rt_ms'], 3)} | {fmt(record['niagara_gt_rt_ms'], 3)} | "
                         f"{fmt(before['wall_p95_ms'])} | {fmt(record['wall_p95_ms'])} | {fmt(record['niagara_gpu_sim_ms'], 3)} |")
    lines += ['', '![Frame time against particle count](SweepChart.svg)', '']
    (DOCS / 'Sweep.md').write_text('\n'.join(lines), encoding='utf-8')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()

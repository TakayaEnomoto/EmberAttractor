"""Summarize measured frame CSVs; never substitute frame timing for Niagara timing."""
import csv
import json
import statistics
from pathlib import Path

root = Path(__file__).resolve().parents[1]
capture_root = root / 'Saved' / 'Profiling'
records = []
for path in sorted(capture_root.glob('EmberLab_*_mode*.csv')):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        continue
    frame_times = sorted(float(row['frame_wall_ms']) for row in rows)
    def column_mean(key):
        values = [float(row[key]) for row in rows if row.get(key)]
        return round(statistics.mean(values), 4) if values else None
    records.append({
        'file': path.name,
        'mode': int(rows[0]['mode']),
        'gpu_target_count': int(rows[0]['gpu_target_count']),
        'cpu_target_count': int(rows[0]['cpu_target_count']),
        'sample_count': len(rows),
        'mean_frame_ms': round(statistics.mean(frame_times), 4),
        'median_frame_ms': round(statistics.median(frame_times), 4),
        'p95_frame_ms': frame_times[min(len(frame_times)-1, int(len(frame_times)*.95))],
        'max_frame_ms': max(frame_times),
        'whole_game_thread_mean_ms': column_mean('game_thread_ms'),
        'whole_render_thread_mean_ms': column_mean('render_thread_ms'),
        'note': 'Includes screenshot-associated frames and hitches. Whole-frame timings are not Niagara timings.'
    })
destination = root / 'Documentation' / 'CaptureSummary.json'
destination.write_text(json.dumps(records, indent=2), encoding='utf-8')
print(json.dumps(records, indent=2))

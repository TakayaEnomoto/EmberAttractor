"""Render the two-page performance report from Evidence/Final/Manifest.json.

Writes Documentation/PerformanceReport.html and prints it to PerformanceReport.pdf with a
locally installed Edge or Chrome (headless). Run Tools/SummarizeSweep.py first.
"""
import json
import subprocess
import sys
from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / 'Documentation'
records = json.loads((ROOT / 'Evidence' / 'Final' / 'Manifest.json').read_text(encoding='utf-8'))
by_label = {record['label']: record for record in records}
gpu = sorted((r for r in records if r['label'].startswith('sweep_gpu_')), key=lambda r: r['gpu_count'])
cpu = sorted((r for r in records if r['label'].startswith('sweep_cpu_')), key=lambda r: r['cpu_count'])
narrative = json.loads((DOCS / 'ReportText.json').read_text(encoding='utf-8'))


def fmt(value, digits=2):
    return '—' if value is None else f'{value:.{digits}f}'


def count_label(count):
    return f'{count / 1_000_000:g}M' if count >= 1_000_000 else f'{count / 1000:g}k'


def sweep_table(runs, key):
    rows = ''.join(
        f"<tr class='{'' if run['within_budget'] else 'fail'}'><td>{count_label(run[key])}</td>"
        f"<td>{fmt(run['wall_mean_ms'])}</td><td>{fmt(run['wall_p95_ms'])}</td><td>{fmt(run['gpu_frame_ms'])}</td>"
        f"<td>{fmt(run['niagara_gt_rt_ms'], 3)}</td><td>{fmt(run['niagara_gpu_sim_ms'], 3)}</td>"
        f"<td>{'pass' if run['within_budget'] else 'fail'}</td></tr>" for run in runs)
    return ("<table><tr><th>Count</th><th>Wall mean</th><th>Wall P95</th><th>GPU frame</th>"
            "<th>Niagara GT+RT</th><th>Niagara GPU sim</th><th>Budget</th></tr>" + rows + "</table>")


def bounds_table():
    rows = ''
    for dynamic in sorted((r for r in records if r['label'].startswith('bounds_dynamic_')), key=lambda r: r['cpu_count']):
        fixed = by_label.get(f"sweep_cpu_{dynamic['cpu_count']}")
        if not fixed:
            continue
        before = dynamic['niagara_gt_rt_ms']
        after = fixed['niagara_gt_rt_ms']
        rows += (f"<tr><td>{dynamic['cpu_count']:,}</td><td>{fmt(before, 3)}</td><td>{fmt(after, 3)}</td>"
                 f"<td>{(after - before) / before * 100:+.1f}%</td><td>{fmt(dynamic['wall_mean_ms'])} → {fmt(fixed['wall_mean_ms'])}</td></tr>")
    return ("<table><tr><th>CPU meshes</th><th>Dynamic bounds (before)</th><th>Fixed bounds (after)</th>"
            "<th>Change</th><th>Wall mean</th></tr>" + rows + "</table>")


def port_table():
    rows = ''
    for port in sorted((r for r in records if r['label'].startswith('port_gpu_')), key=lambda r: r['cpu_count']):
        cpu_run = by_label.get(f"sweep_cpu_{port['cpu_count']}")
        if cpu_run:
            rows += (f"<tr><td>{port['cpu_count']:,}</td><td>{fmt(cpu_run['niagara_gt_rt_ms'], 3)}</td>"
                     f"<td>{fmt(port['niagara_gt_rt_ms'], 3)}</td><td>{fmt(cpu_run['wall_p95_ms'])}</td>"
                     f"<td>{fmt(port['wall_p95_ms'])}</td><td>{fmt(port['niagara_gpu_sim_ms'], 3)}</td></tr>")
    return ("<table><tr><th>Meshes</th><th>CPU sim GT+RT</th><th>GPU port GT+RT</th><th>CPU sim P95</th>"
            "<th>GPU port P95</th><th>GPU port sim</th></tr>" + rows + "</table>")


def paragraphs(key):
    return ''.join(f'<p>{text}</p>' for text in narrative[key])


baseline = by_label.get('sweep_baseline')
chart = (DOCS / 'SweepChart.svg').read_text(encoding='utf-8')
html = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>Ember Attractor — Performance Report</title>
<style>
@page {{ size: A4; margin: 11mm 13mm; }}
body {{ font-family: 'Segoe UI', Arial, sans-serif; font-size: 8.6pt; line-height: 1.32; color: #1d2735; margin: 0; }}
h1 {{ font-size: 17pt; margin: 0 0 2pt; color: #17283e; }}
.sub {{ color: #5b6472; margin-bottom: 8pt; }}
h2 {{ font-size: 10pt; color: #ad4b18; margin: 7pt 0 2pt; border-bottom: 1px solid #e6e8ec; padding-bottom: 1pt; }}
p {{ margin: 0 0 4pt; }}
table {{ border-collapse: collapse; width: 100%; margin: 2pt 0 4pt; font-size: 7.9pt; }}
th, td {{ border-bottom: 1px solid #e6e8ec; padding: 1.2pt 4pt; text-align: right; }}
th:first-child, td:first-child {{ text-align: left; }}
th {{ background: #f4f5f7; font-weight: 600; }}
tr.fail td {{ color: #b42318; font-weight: 600; }}
.two {{ display: grid; grid-template-columns: 1fr 1fr; gap: 10pt; }}
svg {{ width: 100%; height: auto; }}
.page2 {{ break-before: page; }}
.note {{ color: #5b6472; font-size: 8pt; }}
</style></head><body>
<h1>Ember Attractor — Performance Report</h1>
<div class="sub">Niagara at Scale · UE 5.7.4 · {escape(narrative['hardware'])}</div>
<h2>Budget and method</h2>{paragraphs('method')}
<h2>Scaling: frame time against particle count</h2>
{chart}
<div class="two"><div><b>System 1 — GPU sprites (mode 1)</b>{sweep_table(gpu, 'gpu_count')}</div>
<div><b>System 2 — CPU meshes (mode 2)</b>{sweep_table(cpu, 'cpu_count')}</div></div>
<p class="note">All values in ms. Baseline with no particles: wall mean {fmt(baseline['wall_mean_ms']) if baseline else '—'}, GPU frame {fmt(baseline['gpu_frame_ms']) if baseline else '—'}. Red rows miss the budget (wall P95 &gt; 16.6 ms or Niagara GT+RT &gt; 4 ms).</p>
<div class="page2"></div>
<h2>Where each system breaks</h2>{paragraphs('breaking')}
<h2>Optimization: fixed bounds on the CPU system</h2>{paragraphs('optimization_intro')}{bounds_table()}<p>{narrative['optimization_result'][0]}</p><p>{narrative['optimization_result'][1]}</p>{port_table()}
<h2>Many simple vs few complex</h2>{paragraphs('tradeoff')}
<h2>Gameplay readback</h2>{paragraphs('readback')}
<h2>With another week</h2>{paragraphs('next')}
<p class="note">Evidence: Evidence/Final (CSVs, logs, SHA-256 manifest), Documentation/Sweep.md. Reproduce with Tools/Sweep.ps1 then Tools/SummarizeSweep.py and Tools/MakeReport.py.</p>
</body></html>
"""
target = DOCS / 'PerformanceReport.html'
target.write_text(html, encoding='utf-8')
browsers = [Path(r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'),
            Path(r'C:\Program Files\Google\Chrome\Application\chrome.exe')]
browser = next((path for path in browsers if path.exists()), None)
if browser is None:
    sys.exit('No Edge/Chrome found; open PerformanceReport.html and print it to PDF.')
subprocess.run([str(browser), '--headless', '--disable-gpu', '--no-pdf-header-footer',
                f'--print-to-pdf={DOCS / "PerformanceReport.pdf"}', target.as_uri()], check=True)
print(f'Wrote {target} and PerformanceReport.pdf')

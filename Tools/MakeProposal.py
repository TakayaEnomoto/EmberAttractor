"""Render the proposal as a single-page PDF with a locally installed Edge or Chrome (headless)."""
import re
import subprocess
import sys
from html import escape
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'Documentation' / 'Proposal.md').read_text(encoding='utf-8')
page = root / 'Documentation' / 'Proposal.html'
target = root / 'Documentation' / 'Proposal.pdf'


def markup(text):
    text = escape(text)
    text = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', text)
    text = re.sub(r'\*(.+?)\*', r'<i>\1</i>', text)
    text = re.sub(r'`(.+?)`', r'<code>\1</code>', text)
    return re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', text)


body = []
for block in source.strip().split('\n\n'):
    lines = block.splitlines()
    if block.startswith('# '):
        body.append(f'<h1>{markup(lines[0][2:])}</h1>')
        body += [f'<div class="sub">{markup(line)}</div>' for line in lines[1:]]
    elif block.startswith('## '):
        body.append(f'<h2>{markup(lines[0][3:])}</h2>')
        if len(lines) > 1:
            body.append(f'<p>{markup(" ".join(lines[1:]))}</p>')
    else:
        body.append(f'<p>{markup(block.replace(chr(10), " "))}</p>')

page.write_text(f"""<!doctype html><html lang="en"><head><meta charset="utf-8">
<title>Ember Attractor - Proposal</title><style>
@page {{ size: A4; margin: 12mm 15mm; }}
body {{ font-family: 'Segoe UI', Arial, sans-serif; font-size: 9pt; line-height: 1.36; color: #253247; margin: 0; }}
h1 {{ font-size: 22pt; color: #17283e; margin: 0 0 2pt; }}
.sub {{ color: #5b6472; margin-bottom: 6pt; }}
h2 {{ font-size: 10.5pt; color: #ad4b18; margin: 8pt 0 2pt; }}
p {{ margin: 0 0 4pt; }}
code {{ font-size: 8.4pt; }}
a {{ color: #ad4b18; }}
</style></head><body>{''.join(body)}</body></html>""", encoding='utf-8')

browsers = [Path(r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'),
            Path(r'C:\Program Files\Google\Chrome\Application\chrome.exe')]
browser = next((path for path in browsers if path.exists()), None)
if browser is None:
    sys.exit('No Edge/Chrome found; open Proposal.html and print it to PDF.')
subprocess.run([str(browser), '--headless', '--disable-gpu', '--no-pdf-header-footer',
                f'--print-to-pdf={target}', page.as_uri()], check=True)
pages = len(re.findall(rb'/Type\s*/Page[^s]', target.read_bytes()))
assert pages == 1, f'Proposal must be one page, got {pages}'
print(target)

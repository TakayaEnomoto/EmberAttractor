"""Render the proposal as a single-page PDF, using only local dependencies."""
from pathlib import Path
from html import escape
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_LEFT
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer
from reportlab.lib.pagesizes import A4
import re

root = Path(__file__).resolve().parents[1]
source = (root / 'Documentation' / 'Proposal.md').read_text(encoding='utf-8')
target = root / 'Documentation' / 'Proposal.pdf'
body = ParagraphStyle('Body', fontName='Helvetica', fontSize=9, leading=12,
                      textColor=colors.HexColor('#253247'), spaceAfter=5)
heading = ParagraphStyle('Section', parent=body, fontName='Helvetica-Bold',
                         fontSize=10, leading=13, spaceBefore=5, spaceAfter=3,
                         textColor=colors.HexColor('#ad4b18'))
title = ParagraphStyle('Title', parent=body, fontName='Helvetica-Bold',
                       fontSize=25, leading=29, spaceAfter=6,
                       textColor=colors.HexColor('#17283e'))

def markup(text):
    text = escape(text)
    text = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', text)
    text = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<link href="\2" color="#ad4b18">\1</link>', text)
    return text

story = []
for block in source.strip().split('\n\n'):
    if block.startswith('# '):
        lines = block.splitlines()
        story.append(Paragraph(markup(lines[0][2:]), title))
        for line in lines[1:]:
            story.append(Paragraph(markup(line), body))
    elif block.startswith('## '):
        lines = block.splitlines()
        story.append(Paragraph(markup(lines[0][3:]), heading))
        if len(lines) > 1:
            story.append(Paragraph(markup(' '.join(lines[1:])), body))
    else:
        story.append(Paragraph(markup(block.replace('\n', ' ')), body))

doc = SimpleDocTemplate(str(target), pagesize=A4, rightMargin=43, leftMargin=43,
                        topMargin=34, bottomMargin=34, title='Ember Attractor - Proposal',
                        author='Niagara at Scale')
doc.build(story)
from pypdf import PdfReader
pages = len(PdfReader(str(target)).pages)
assert pages == 1, f'Proposal must be one page, got {pages}'
print(target)

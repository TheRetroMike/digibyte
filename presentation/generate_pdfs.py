#!/usr/bin/env python3
"""Generate high-quality PDFs from markdown files using WeasyPrint."""

import subprocess
import sys
from pathlib import Path

def md_to_html(md_file):
    """Convert markdown to HTML using pandoc."""
    html_file = md_file.replace('.md', '.html')

    cmd = [
        'pandoc',
        md_file,
        '-o', html_file,
        '--standalone',
        '--css=pdf-style-weasy.css',
        '--metadata', f'title={Path(md_file).stem}'
    ]

    subprocess.run(cmd, check=True)
    return html_file

def html_to_pdf(html_file):
    """Convert HTML to PDF using WeasyPrint."""
    pdf_file = html_file.replace('.html', '.pdf')

    cmd = [
        'weasyprint',
        html_file,
        pdf_file
    ]

    subprocess.run(cmd, check=True)
    return pdf_file

def main():
    presentation_dir = Path('/Users/jt/Code/digibyte/presentation')

    md_files = [
        'DIGIDOLLAR_5MIN_PRESENTATION.md',
        'DIGIDOLLAR_DETAILED_PRESENTATION.md',
        'EXECUTIVE_SUMMARY.md'
    ]

    for md_file in md_files:
        md_path = str(presentation_dir / md_file)
        print(f"Converting {md_file}...")

        # Convert MD -> HTML
        html_file = md_to_html(md_path)
        print(f"  Generated HTML: {html_file}")

        # Convert HTML -> PDF
        pdf_file = html_to_pdf(html_file)
        print(f"  Generated PDF: {pdf_file}")

        # Clean up HTML
        Path(html_file).unlink()
        print(f"  Cleaned up HTML")

    print("\nAll PDFs generated successfully!")

if __name__ == '__main__':
    main()

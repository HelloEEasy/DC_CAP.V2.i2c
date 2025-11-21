import sys
import subprocess

if len(sys.argv) < 3:
    print('Usage: python extract_pdf_text.py input.pdf output.txt')
    sys.exit(2)

infile = sys.argv[1]
outfile = sys.argv[2]

try:
    from pdfminer.high_level import extract_text
except Exception:
    print('pdfminer.six not found, installing...')
    subprocess.check_call([sys.executable, '-m', 'pip', 'install', 'pdfminer.six'])
    from pdfminer.high_level import extract_text

print('Extracting text from', infile)
text = extract_text(infile)
with open(outfile, 'w', encoding='utf-8') as f:
    f.write(text)
print('Wrote', len(text), 'characters to', outfile)
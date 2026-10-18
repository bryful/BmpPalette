"""Regression tests for packed 4bpp C/C++ code export (standard library only)."""
import argparse
from pathlib import Path
import re
import struct
import subprocess

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--platform', default='x64', choices=['x64', 'Win32'])
parser.add_argument('--configuration', default='Release', choices=['Debug', 'Release'])
args = parser.parse_args()
exe = root / args.platform / args.configuration / 'BmpPalette.exe'
work = root / 'tests' / 'generated' / 'code'
work.mkdir(parents=True, exist_ok=True)


def run(*args, ok=True):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True)
    assert (result.returncode == 0) == ok, result.stderr
    return result


def bmp(path, width=3, top=False, bits=4, compression=0, count=16):
    # Asymmetric rows, nonzero padding and odd-width unused nibbles.
    rows = [bytes([0x01, 0x2F]), bytes([0x34, 0x5E])]
    if width == 4:
        rows = [bytes([0x01, 0x23]), bytes([0x45, 0x67])]
    stride = ((width * bits + 31) // 32) * 4
    data = b''.join(r + b'\xCC' * (stride-len(r)) for r in (rows if top else rows[::-1]))
    palette = b''.join(bytes([i, 2*i, 3*i, 0]) for i in range(count))
    offset = 54 + len(palette)
    header = struct.pack('<2sIHHI', b'BM', offset+len(data), 0, 0, offset)
    header += struct.pack('<IiiHHIIiiII', 40, width, -2 if top else 2,
                          1, bits, compression, len(data), 0, 0, count, 0)
    path.write_bytes(header + palette + data)


def values(text, suffix):
    body = re.search(r'\b\w+_' + suffix + r'\[.*?\]\s*=\s*\{(.*?)\}', text, re.S)[1]
    return [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]+)', body)]


for top in [False, True]:
    for width in [3, 4]:
        source = work / f'aaa_{top}_{width}.bmp'
        bmp(source, width, top)
        run('-code', source)
        text = source.with_suffix('.cpp').read_text()
        assert '#include <stdint.h>' in text
        assert f'const int {source.stem}_width = {width};' in text
        assert f'const int {source.stem}_height = 2;' in text
        assert values(text, 'palette') == [(3*i << 16) | (2*i << 8) | i for i in range(16)]
        assert values(text, 'data') == ([0x01, 0x20, 0x34, 0x50] if width == 3 else [0x01, 0x23, 0x45, 0x67])
        assert f'static uint8_t {source.stem}_data[]' in text

source = work / 'aaa_False_3.bmp'
explicit = work / 'different.cpp'
run('-code', source, '-o', explicit)
assert 'aaa_False_3_width' in explicit.read_text()
run('-code', source, '-o', source, ok=False)
run('-code', source, '-p', 'ignored.json', ok=False)
run('-code', work / 'missing*.bmp', ok=False)
run('-code', root / 'tests' / 'fixtures' / 'palette16.png', ok=False)

batch = work / 'batch'
run('-code', work / 'aaa_*.bmp', '-o', batch)
assert len(list(batch.glob('aaa_*.cpp'))) == 4
# A wildcard with just one match still treats -o as a directory.
run('-code', work / 'aaa_False_3.bm?', '-o', work / 'one')
assert (work / 'one' / 'aaa_False_3.cpp').exists()
run('-code', work / 'aaa_*.bmp')

named = work / '12-a 日本語.bmp'
bmp(named)
run('-code', named)
assert 'image_12_a_width' in named.with_suffix('.cpp').read_text()

invalid = work / 'bad.bmp'
for options in [dict(bits=8), dict(count=15), dict(compression=2)]:
    bmp(invalid, **options)
    explicit.write_text('keep')
    run('-code', invalid, '-o', explicit, ok=False)
    assert explicit.read_text() == 'keep'
bmp(invalid)
invalid.write_bytes(invalid.read_bytes()[:-1])
run('-code', invalid, '-o', explicit, ok=False)
assert explicit.read_text() == 'keep'

# An invalid member must not prevent valid members being exported.
bmp(work / 'mix_good.bmp')
(work / 'mix_bad.bmp').write_bytes(b'bad')
run('-code', work / 'mix_*.bmp', '-o', work / 'mixed', ok=False)
assert (work / 'mixed' / 'mix_good.cpp').exists()
print('Code export tests passed')

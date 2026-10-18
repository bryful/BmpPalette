"""PNG integration tests; run from the solution root after building Release/x64."""
import binascii
import json
from pathlib import Path
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parent.parent
FIXTURES = ROOT / 'tests' / 'fixtures'
GENERATED = ROOT / 'tests' / 'generated' / 'png'
FIXTURES.mkdir(exist_ok=True)
GENERATED.mkdir(parents=True, exist_ok=True)
EXE = ROOT / 'x64' / 'Release' / 'BmpPalette.exe'
COLORS = [(i * 17, 255 - i * 17, i * 7) for i in range(16)]

def chunk(name, data):
    return struct.pack('>I', len(data)) + name + data + struct.pack('>I', binascii.crc32(name + data))

def png(width, height, kind, rows, palette=None, depth=8, transparency=None):
    result = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, depth, kind, 0, 0, 0))
    if palette is not None:
        result += chunk(b'PLTE', bytes(c for color in palette for c in color))
    if transparency is not None:
        result += chunk(b'tRNS', transparency)
    return result + chunk(b'IDAT', zlib.compress(b''.join(b'\0' + row for row in rows))) + chunk(b'IEND', b'')

def run(*args, ok=True):
    result = subprocess.run([str(EXE), *map(str, args)], capture_output=True, text=True)
    assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
    return result.stdout

indexed = FIXTURES / 'palette16.png'
indexed.write_bytes(png(5, 2, 3, [bytes([0x01, 0x23, 0x40]), bytes([0x56, 0x78, 0x90])], COLORS, depth=4, transparency=bytes([0]+[255]*15)))
# Fixed fixtures also exercise Pal16Edit's shared image loader in --self-test.
bmp_pixels = bytes([0x01, 0x23, 0x40, 0, 0x56, 0x78, 0x90, 0])
bmp = b'BM' + struct.pack('<IHHI', 118+len(bmp_pixels), 0, 0, 118)
bmp += struct.pack('<IiiHHIIiiII', 40, 5, -2, 1, 4, 0, len(bmp_pixels), 0, 0, 16, 0)
bmp += bytes(c for r, g, b in COLORS for c in (b, g, r, 0)) + bmp_pixels
(FIXTURES / 'palette16.bmp').write_bytes(bmp)
(FIXTURES / 'rgb.png').write_bytes(png(1, 1, 2, [bytes([255, 0, 0])]))
rgb = GENERATED / '日本語 RGB.PNG'
rgb.write_bytes(png(5, 2, 2, [bytes(c for color in COLORS[:5] for c in color), bytes(c for color in COLORS[5:10] for c in color)]))
rgba = GENERATED / 'rgba.png'
rgba.write_bytes(png(2, 1, 6, [bytes([255, 0, 0, 0, 0, 255, 0, 128])]))
gray = GENERATED / 'gray.png'
gray.write_bytes(png(3, 1, 0, [bytes([0, 128, 255])]))
assert 'Palette colors: 16' in run('-info', indexed)
assert 'Bits per pixel: 4' in run('-info', indexed)
export = GENERATED / 'indexed.json'
run('-pal', indexed, '-o', export)
assert json.loads(export.read_text()) == [list(c) for c in COLORS]
assert 'Width: 5' in run('-info', rgb)
run('-pal', rgb, ok=False)
for source in (indexed, rgb, rgba, gray):
    palette = GENERATED / (source.stem + '.json')
    run('-makepal', source, '-o', palette)
    values = json.loads(palette.read_text())
    assert len(values) == 16 and values[0] == [255, 0, 255]
    if source == rgba:
        assert [255, 0, 0] in values and [0, 255, 0] in values, 'Alpha must not change RGB'
    outdir = GENERATED / source.stem
    outdir.mkdir(exist_ok=True)
    # Read the actual unique output path through its filename (console encoding may differ).
    previous = set(outdir.iterdir())
    run('-redu', source, '-p', palette, '-o', outdir)
    output, = set(outdir.iterdir()) - previous
    assert output.suffix == '.bmp' and output.read_bytes()[:2] == b'BM'
    data = output.read_bytes()
    width, height = struct.unpack_from('<ii', data, 18)
    assert height < 0
    stride = ((width * 4 + 31) // 32) * 4
    for y in range(-height):
        for x in range(width):
            value = data[118+y*stride+x//2]
            index = value & 15 if x % 2 else value >> 4
            assert index != 0
            if source in (indexed, rgb):
                assert values[index] == list(COLORS[y*5+x]), 'RGB channels, row orientation or palette indices changed'
# Missing extensions must select BMP, even if a PNG with the same stem exists.
base = GENERATED / 'default'
base.with_suffix('.bmp').write_bytes(next((GENERATED / indexed.stem).glob('*.bmp')).read_bytes())
base.with_suffix('.png').write_bytes(indexed.read_bytes())
assert 'DIB header size: 40' in run('-info', base)
assert 'Format: PNG' not in run('-info', base)
missing = GENERATED / 'png-only'
missing.with_suffix('.png').write_bytes(indexed.read_bytes())
run('-info', missing, ok=False)
for contents in (b'not a PNG', indexed.read_bytes()[:45]):
    bad = GENERATED / 'bad.png'
    bad.write_bytes(contents)
    run('-info', bad, ok=False)
    run('-makepal', bad, ok=False)
run('-redu', GENERATED / '*.PNG', '-p', GENERATED / (rgb.stem + '.json'), '-o', GENERATED / 'batch', ok=False) # includes bad.png
print('PNG integration tests passed.')

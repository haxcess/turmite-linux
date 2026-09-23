"""Exercise Q through the real host; independently decode its indexed PNG."""
import binascii
import pathlib
import os
import struct
import subprocess
import tempfile
import time
import zlib


def decode(path):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    chunks = {}
    at = 8
    while at < len(data):
        size, = struct.unpack_from('>I', data, at)
        kind = data[at + 4:at + 8]
        payload = data[at + 8:at + 8 + size]
        crc, = struct.unpack_from('>I', data, at + 8 + size)
        assert binascii.crc32(kind + payload) & 0xffffffff == crc
        chunks[kind] = chunks.get(kind, b'') + payload
        at += size + 12
    width, height, depth, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', chunks[b'IHDR'])
    assert (depth, color, compression, filtering, interlace) == (4, 3, 0, 0, 0)
    assert chunks[b'sRGB'] == b'\x01'
    assert chunks[b'gAMA'] == struct.pack('>I', 45455)
    assert len(chunks[b'cHRM']) == 32 and len(chunks[b'tIME']) == 7
    assert chunks[b'pHYs'] == struct.pack('>IIB', 11811, 11811, 1)
    assert chunks[b'bKGD'] == b'\x00'
    assert len(chunks[b'PLTE']) == 18
    assert b'tRNS' not in chunks
    rows = zlib.decompress(chunks[b'IDAT'])
    stride = (width + 1) // 2
    assert len(rows) == (stride + 1) * height
    pixels = []
    previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        method = rows[start]
        row = bytearray(rows[start + 1:start + 1 + stride])
        for x in range(stride):
            left = row[x - 1] if x else 0
            up = previous[x]
            corner = previous[x - 1] if x else 0
            p = left + up - corner
            distances = [abs(p-left), abs(p-up), abs(p-corner)]
            paeth = [left, up, corner][distances.index(min(distances))]
            row[x] = (row[x] + [0, left, up, (left+up)//2, paeth][method]) & 255
        pixels.extend((row[x//2] >> (4 if x % 2 == 0 else 0)) & 15 for x in range(width))
        previous = row
    return width, height, chunks[b'PLTE'], bytes(pixels)


with tempfile.TemporaryDirectory(prefix='turmite-png-') as tmp:
    palettes = []
    for mode, palette_args in enumerate(([], ['--random'], ['--randomish'])):
        for capacity in (1, 3):
            root = pathlib.Path(tmp) / f'{mode}-{capacity}'
            args = ['./turmite', '-H', '-s', '12345', '-d', str(capacity), '-i', '0.01',
                    '-x', '162', '-y', '122', '-c', '2', '-D', str(root)] + palette_args
            process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True)
            time.sleep(0.15)
            _, errors = process.communicate('Q\n', timeout=10)
            assert process.returncode == 0, errors
            directory, = root.glob('tape-*')
            width, height, palette, pixels = decode(directory / 'universe.png')
            assert (width, height) == (81, 61)  # Odd width checks final half-byte packing.
            metadata = {}
            for line in (directory / 'manifest.txt').read_text().splitlines():
                if '=' in line:
                    key, value = line.split('=', 1)
                    metadata[key.strip()] = value.strip()
            expected_palette = b''.join(bytes.fromhex(metadata[f'palette_{i}']) for i in range(6))
            assert palette == expected_palette
            palettes.append(palette)
            pages = sorted((directory / 'pages').glob('page-*.bin'))
            assert len(pages) == capacity
            assert pixels == pages[-1].read_bytes()
            frames = sorted((directory / 'frames').glob('frame-*.png'))
            assert len(frames) >= 2  # First draining frame plus final stable frame.
            assert [p.name for p in frames] == [f'frame-{i:08d}.png' for i in range(len(frames))]
            for image in frames:
                fw, fh, fp, _ = decode(image)
                assert (fw, fh, fp) == (width, height, palette)
            assert decode(frames[-1])[3] == pixels
            ant_lines = (directory / 'manifest.txt').read_text().split('ANT SNAPSHOTS\n')[1].splitlines()
            header = ant_lines[0].split('\t')
            final_ants = [dict(zip(header, line.split('\t'))) for line in ant_lines[1:] if line]
            final_ants = [a for a in final_ants if int(a['page']) == capacity - 1]
            assert all(a['enabled'] == '0' and float(a['tokens']) < 1 for a in final_ants)
    assert len(set(palettes)) == 3
    sdl_root = pathlib.Path(tmp) / 'sdl'
    sdl = subprocess.run(['./tests/debug_quit_app_test', str(sdl_root)],
                         env={**os.environ, 'SDL_VIDEODRIVER': 'dummy'},
                         capture_output=True, text=True, timeout=10)
    assert sdl.returncode == 0, sdl.stdout + sdl.stderr
    print(sdl.stdout.strip())
    sdl_dir, = sdl_root.glob('tape-*')
    sdl_frames = sorted((sdl_dir / 'frames').glob('*.png'))
    assert len(sdl_frames) >= 3
    assert decode(sdl_frames[-1]) == decode(sdl_dir / 'universe.png')
    bad_root = pathlib.Path(tmp) / 'not-a-directory'
    bad_root.write_text('occupied')
    failed = subprocess.run(['./turmite', '-H', '-d', '1', '-x', '160', '-y', '120',
                             '-D', str(bad_root)], input='Q\n', text=True,
                            capture_output=True, timeout=10)
    assert failed.returncode == 1 and 'dump:' in failed.stderr
print('PNG dump ok: Q, active palettes, final tape/ring wrap, indexed pixels, template metadata, export failure')

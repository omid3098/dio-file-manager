"""Blink Compare (src/ui_compare.c), driven by real Win32 input on the private desktop.
Fixture (made here with Pillow): hero_v07/v08 (2048x1536, v08 differs in one 200x120 box), small/large (800x600 against
2000x1500 of the same picture), photo.jpg against photo.png, 8k_a/8k_b (7680x4320), alpha_a/alpha_b (1024x1024 with a clear hole),
two Persian names, note.txt. Run from the repo:  python tests/use/compare_smoke.py [--light]   (DFM_EXE, DFM_USE_WORK as in input_smoke.py)
Budgets it fails over (docs/design.md, Blink Compare): Space to the other picture drawn within 50 ms; working set with two 8K pictures
open at most 120 MB steady and 160 MB peak; the window answers a message within 50 ms while two 8K pictures decode.
"""
import ctypes as C
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
os.environ.setdefault('DFM_USE_WORK', str(ROOT / '.agent-work/use-checks' / Path(__file__).stem))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import input_smoke as base

WORK = base.WORK
FIX = WORK / 'cmp'
U, K = base.U, base.K
PSAPI = C.WinDLL('psapi', use_last_error=True)
K.OpenProcess.restype = C.c_void_p
K.OpenProcess.argtypes = [C.c_ulong, C.c_int, C.c_ulong]
K.CloseHandle.argtypes = [C.c_void_p]
PSAPI.GetProcessMemoryInfo.argtypes = [C.c_void_p, C.c_void_p, C.c_ulong]
FA = '\u067e\u0627\u0633\u067e\u0648\u0631\u062a \u0627\u0633\u06a9\u0646.png'
FB = '\u067e\u0627\u0633\u067e\u0648\u0631\u062a \u0627\u0633\u06a9\u0646 \u06f2.png'


class PMC(C.Structure):
    _fields_ = [('cb', C.c_ulong), ('PageFaultCount', C.c_ulong), ('PeakWorkingSetSize', C.c_size_t), ('WorkingSetSize', C.c_size_t),
                ('QuotaPeakPagedPoolUsage', C.c_size_t), ('QuotaPagedPoolUsage', C.c_size_t),
                ('QuotaPeakNonPagedPoolUsage', C.c_size_t), ('QuotaNonPagedPoolUsage', C.c_size_t),
                ('PagefileUsage', C.c_size_t), ('PeakPagefileUsage', C.c_size_t)]


def memory(proc):
    """(working set, peak working set) of the app in MB."""
    h = K.OpenProcess(0x1000 | 0x0400, False, proc.pid)   # QUERY_LIMITED_INFORMATION | QUERY_INFORMATION
    pm = PMC()
    pm.cb = C.sizeof(pm)
    ok = PSAPI.GetProcessMemoryInfo(h, C.byref(pm), pm.cb)
    K.CloseHandle(h)
    assert ok, 'GetProcessMemoryInfo'
    return pm.WorkingSetSize / 1048576, pm.PeakWorkingSetSize / 1048576


def qpc():
    v = C.c_longlong()
    K.QueryPerformanceCounter(C.byref(v))
    return v.value


def qpf():
    v = C.c_longlong()
    K.QueryPerformanceFrequency(C.byref(v))
    return v.value


def make_fixture():
    from PIL import Image, ImageDraw
    import random
    import shutil
    shutil.rmtree(FIX, ignore_errors=True)
    FIX.mkdir(parents=True)

    def scene(w, h, seed=3):
        rnd = random.Random(seed)
        im = Image.new('RGB', (w, h), (24, 42, 48))
        d = ImageDraw.Draw(im)
        for i in range(60):
            x, y = rnd.randrange(w), rnd.randrange(h)
            r = rnd.randrange(max(8, w // 40), max(16, w // 8))
            d.rectangle([x, y, x + r, y + r // 2], fill=tuple(rnd.randrange(40, 200) for _ in range(3)), outline=(220, 220, 200), width=max(1, w // 600))
        return im

    def stamp(im, box, colour):
        ImageDraw.Draw(im).rectangle(box, fill=colour)
        return im
    a = scene(2048, 1536)
    a.save(FIX / 'hero_v07.png')
    stamp(a.copy(), (1400, 300, 1600, 420), (215, 166, 91)).save(FIX / 'hero_v08.png')
    big = scene(2000, 1500, 5)
    big.save(FIX / 'large.png')
    big.resize((800, 600), Image.LANCZOS).save(FIX / 'small.png')
    stamp(scene(1600, 1200, 7), (100, 100, 220, 180), (255, 80, 80)).save(FIX / 'photo.png')
    scene(1600, 1200, 7).save(FIX / 'photo.jpg', quality=90)
    for n, hole in (('a', (300, 300, 600, 600)), ('b', (320, 320, 700, 700))):
        im = scene(1024, 1024, 11).convert('RGBA')
        ImageDraw.Draw(im).rectangle(hole, fill=(0, 0, 0, 0))
        im.save(FIX / f'alpha_{n}.png')
    k = scene(7680, 4320, 13)
    k.save(FIX / '8k_a.png', compress_level=1)
    stamp(k, (5000, 2000, 5400, 2300), (215, 166, 91)).save(FIX / '8k_b.png', compress_level=1)
    scene(1200, 900, 17).save(FIX / FA)
    stamp(scene(1200, 900, 17), (50, 50, 200, 120), (250, 250, 250)).save(FIX / FB)
    (FIX / 'note.txt').write_text('x')
    now = time.time()
    for f in FIX.iterdir():
        os.utime(f, (now - 3600, now - 3600))
    for older in ('hero_v07.png', 'small.png', 'photo.jpg', '8k_a.png', 'alpha_a.png', FA):
        os.utime(FIX / older, (now - 7200, now - 7200))


class Cmp(base.App):
    def __init__(self, name, extra=()):
        super().__init__(name, FIX, 'grid', 'light' if '--light' in sys.argv else 'dark', extra=extra)

    def pick(self, *names):
        s = self.snap()
        for i, n in enumerate(names):
            self.cell(s['names'].index(n), (17,) if i else ())
        return self.snap()

    def context_menu(self, name, row=None):
        """Right-click a picture; with row, walk down to that row with the arrow keys and press Enter (Open, Open With greyed out but still a step, Compare Pictures: three steps)."""
        s = self.snap()
        x, y = self.cell_xy(s['names'].index(name))
        self.rclick(x, y)
        s = self.snap()
        assert s['ctx'], 'Context menu did not open'
        if row is not None:
            for _ in range(row):
                self.key(40)
            self.key(13)
            s = self.snap()
        return s

    def open_compare(self, a_name, b_name, wait=True):
        self.pick(a_name, b_name)
        s = self.context_menu(b_name, 3)    # Open, Open With (greyed out), Compare Pictures
        assert s['cmp']['open'] == 1, f'Compare did not open: {s["cmp"]}'
        if wait:
            self.until('Pictures never loaded', lambda: all(x['tex'] for x in self.snap()['cmp']['side']), 20)
        return self.snap()

    def key_down(self, vk):
        U.PostMessageW(self.hwnd, 0x100, vk, 1)

    def key_up(self, vk):
        U.PostMessageW(self.hwnd, 0x101, vk, 0xC0000001)


def diff_box(p1, p2, region):
    """Bounding box of the pixels that differ between two screenshots inside region (x0, y0, x1, y1), or None."""
    from PIL import Image, ImageChops
    a = Image.open(p1).convert('RGB').crop(region)
    b = Image.open(p2).convert('RGB').crop(region)
    box = ImageChops.difference(a, b).point(lambda v: 255 if v > 24 else 0).getbbox()
    return None if box is None else (box[0] + region[0], box[1] + region[1], box[2] + region[0], box[3] + region[1])


def stage_px(s, dpi):
    x, y, w, h = s['cmp']['stage']
    return int(x * dpi), int(y * dpi), int((x + w) * dpi), int((y + h) * dpi)


def inside():
    results = []

    def check(name, fn):
        a = None
        try:
            a = Cmp(name)
            fn(a)
            results.append({'name': name, 'result': 'passed'})
        except BaseException as e:
            results.append({'name': name, 'result': 'failed', 'error': repr(e)})
            if a:
                try:
                    a.snap('fail-' + name)
                except BaseException:
                    pass
        finally:
            if a:
                a.close()
        print(results[-1], flush=True)

    def same_size(a):
        s = a.open_compare('hero_v07.png', 'hero_v08.png')
        c = s['cmp']
        assert c['side'][0]['sw'] == 2048 and c['side'][1]['sw'] == 2048 and c['side'][0]['sh'] == 1536, c
        assert c['side'][0]['tw'] <= c['want'][0] + 1, f'Decoded bigger than the stage: {c}'
        dpi = U.GetDpiForWindow(a.hwnd) / 96
        a.snap('same-split')
        a.key(0x23)    # End: A alone (divider at the far right)
        a.snap('same-A')
        a.key_down(0x20)
        time.sleep(.3)
        s = a.snap('same-B')
        assert s['cmp']['shown'] == 2 and s['cmp']['held'] == 1, s['cmp']
        x0, y0, x1, y1 = stage_px(s, dpi)
        x0, y0, x1, y1 = x0 + int(100 * dpi), y0 + int(60 * dpi), x1 - int(100 * dpi), y1    # leave out the A and B labels in the corners
        box = diff_box(WORK / 'same-A.png', WORK / 'same-B.png', (x0, y0, x1, y1))
        a.key_up(0x20)
        time.sleep(.2)
        assert box, 'A and B look the same'
        frac = (box[2] - box[0]) * (box[3] - box[1]) / ((x1 - x0) * (y1 - y0))
        assert 0.002 < frac < 0.06, f'Changed area is {frac:.4f} of the stage: {box}'
        print('  same-size: differing pixels', box, f'{frac:.4f} of the stage', flush=True)
        assert a.snap()['cmp']['shown'] == 0, 'Releasing Space did not go back'

    def divider(a):
        s = a.open_compare('hero_v07.png', 'hero_v08.png')
        x, y, w, h = s['cmp']['stage']
        p = a.point(x + w * .25, y + h * .5)
        a.msg(0x200, 0, p)
        time.sleep(.05)
        a.msg(0x201, 1, p)
        time.sleep(.05)
        p2 = a.point(x + w * .75, y + h * .5)
        a.msg(0x200, 1, p2)
        time.sleep(.1)
        a.msg(0x202, 0, p2)
        time.sleep(.1)
        sp = a.snap()['cmp']['split']
        assert abs(sp - .75) < .02, f'Divider after a drag from 25% to 75%: {sp}'
        a.snap('divider-75')
        a.key(0x25)
        sp2 = a.snap()['cmp']['split']
        assert abs(sp2 - (sp - .02)) < .005, f'Left arrow: {sp2}'
        a.key(0x24)
        assert a.snap()['cmp']['split'] == 0
        a.key(0x23)
        assert a.snap()['cmp']['split'] == 1

    def escape_keeps_selection(a):
        a.open_compare('hero_v07.png', 'hero_v08.png')
        a.key(27)
        s = a.snap('after-esc')
        assert s['cmp']['open'] == 0 and sorted(s['selection']) == ['hero_v07.png', 'hero_v08.png'], f'After Esc: {s["selection"]}'

    def keys_are_owned(a):
        a.open_compare('hero_v07.png', 'hero_v08.png')
        n = a.snap()['count']
        a.key(46)      # Delete must not trash anything now
        a.key(ord('A'), (17,))
        a.key(ord('Q'))   # a typed letter must not start the search either
        a.msg(0x102, ord('q'))
        s = a.snap()
        assert s['count'] == n and s['cmp']['open'] == 1 and not s['search'], f'Keys leaked: {s["count"]} {s["search"]}'

    def different_sizes(a):
        s = a.open_compare('small.png', 'large.png')
        c = s['cmp']
        assert {c['side'][0]['sw'], c['side'][1]['sw']} == {800, 2000}, c
        a.snap('sizes-differ')
        a.key(0x23)
        a.snap('sizes-A')
        a.key(0x24)
        a.snap('sizes-B')

    def jpg_png(a):
        s = a.open_compare('photo.jpg', 'photo.png')
        assert s['cmp']['side'][0]['sw'] == 1600 and s['cmp']['side'][1]['sw'] == 1600
        a.snap('jpg-png')

    def transparency(a):
        s = a.open_compare('alpha_a.png', 'alpha_b.png')
        assert s['cmp']['side'][0]['alpha'] and s['cmp']['side'][1]['alpha'], s['cmp']
        a.snap('alpha-split')
        a.key_down(0x20)
        time.sleep(.2)
        a.snap('alpha-B')
        a.key_up(0x20)

    def persian(a):
        a.open_compare(FA, FB)
        a.snap('persian')

    def refused(a):
        a.pick('hero_v07.png')
        s = a.context_menu('hero_v07.png')
        a.key(27)
        assert s['cmp']['can'] == 0, 'One picture'
        a.pick('hero_v07.png', 'hero_v08.png')
        s = a.context_menu('hero_v08.png')
        a.key(27)
        assert s['cmp']['can'] == 1, 'Two pictures'
        a.pick('hero_v07.png', 'hero_v08.png', 'photo.png')
        s = a.context_menu('photo.png')
        a.snap('refused-three')
        a.key(27)
        assert s['cmp']['can'] == 0, 'Three pictures'
        a.pick('hero_v07.png', 'note.txt')
        s = a.context_menu('note.txt')
        a.key(27)
        assert s['cmp']['can'] == 0, 'A picture and a text file'

    def blink_button(a):
        s = a.open_compare('hero_v07.png', 'hero_v08.png')
        x, y, w, h = s['cmp']['stage']
        a.click(x + 100, y + h + 28)     # the Blink button, second of the bottom row
        seen = set()
        end = time.monotonic() + 2.5
        while time.monotonic() < end:
            c = a.snap()['cmp']
            seen.add((c['blink'], c['shown']))
            time.sleep(.17)
        assert any(sh == 1 for _, sh in seen) and any(sh == 2 for _, sh in seen), f'Blink never flipped: {seen}'

    def latency_and_memory(a):
        time.sleep(2)     # the folder's own thumbnails of the two 8K pictures load first: they are part of the base
        base_ws, _ = memory(a.proc)
        a.pick('8k_a.png', '8k_b.png')
        s = a.context_menu('8k_b.png')
        for _ in range(3):
            a.key(40)
        t0 = time.monotonic()
        a.key(13)
        worst = 0
        while time.monotonic() - t0 < 30:
            worst = max(worst, a.msg(0))
            s = a.snap()['cmp']
            if all(x['tex'] for x in s['side']) and not any(x['busy'] for x in s['side']):
                break
            time.sleep(.02)
        load = time.monotonic() - t0
        assert all(x['tex'] for x in s['side']), s
        time.sleep(.5)
        ws, peak = memory(a.proc)
        a.snap('8k')
        sd = s['side'][0]
        print(f'  8K: shown after {load:.2f} s, longest message wait {worst * 1000:.0f} ms, WS {ws:.0f} MB (base {base_ws:.0f}), peak {peak:.0f} MB, '
              f'decoded {sd["tw"]}x{sd["th"]} from {sd["sw"]}x{sd["sh"]}', flush=True)
        assert sd['sw'] == 7680 and sd['sh'] == 4320
        assert load < 4, f'Open time {load:.2f} s'
        assert worst < .05, f'Window stopped answering {worst * 1000:.0f} ms while decoding'
        assert ws <= 120 and peak <= 160, f'Memory {ws:.0f} / peak {peak:.0f} MB'
        lat = []
        for _ in range(8):
            a.key_up(0x20)
            time.sleep(.3)
            t = qpc()
            a.key_down(0x20)
            time.sleep(.3)
            c = a.snap()['cmp']
            assert c['shown'] == 2, c
            lat.append((c['b_qpc'] - t) * 1000 / qpf())
            a.key_up(0x20)
            time.sleep(.2)
        print('  Space -> B drawn (ms):', [f'{v:.1f}' for v in lat], flush=True)
        assert max(lat) <= 50 and min(lat) >= 0, f'Latency {lat}'
        a.key(27)
        time.sleep(1.0)
        ws2, _ = memory(a.proc)
        print(f'  after close WS {ws2:.0f} MB (base {base_ws:.0f})', flush=True)
        assert ws2 <= base_ws + 20, f'Memory after close {ws2:.0f} vs {base_ws:.0f}'

    def frame_rows(a):
        return [l.split() for l in Path(str(a.base) + '.frames').read_text(encoding='utf-8').splitlines() if len(l.split()) == 10 and l[0] != 'D']

    def idle_and_frame_time(a):
        s = a.open_compare('hero_v07.png', 'hero_v08.png')
        time.sleep(1.0)
        n0 = len(frame_rows(a))
        time.sleep(2.0)      # nothing asked for a frame meanwhile: no snapshot, no input
        rows = frame_rows(a)
        assert len(rows) == n0, f'{len(rows) - n0} frames drawn in 2 s with the compare open and nothing moving'
        x, y, w, h = s['cmp']['stage']
        a.click(x + 100, y + h + 28)     # Blink
        time.sleep(1.6)                  # the button's own change of look runs its frames first
        n1 = len(frame_rows(a))
        time.sleep(2.0)
        rows = frame_rows(a)[n1:]
        ms = max(float(r[9]) for r in rows)
        print(f'  blink: {len(rows)} frames in 2 s, slowest frame build {ms:.2f} ms', flush=True)
        assert 3 <= len(rows) <= 6, f'Blink drew {len(rows)} frames in 2 s'
        assert ms < 8, f'Frame build {ms:.2f} ms'

    def search_closes_it(a):
        a.open_compare('hero_v07.png', 'hero_v08.png')
        a.click(*a.header_icon(1))       # the header's search button: the list behind would change
        s = a.snap('search-closes')
        assert s['cmp']['open'] == 0, f'Compare stayed open over a search: {s["cmp"]}'

    check('1-same-size-small-difference', same_size)
    check('2-divider-and-arrows', divider)
    check('3-esc-keeps-selection', escape_keeps_selection)
    check('4-keys-are-owned', keys_are_owned)
    check('5-different-sizes', different_sizes)
    check('6-jpg-against-png', jpg_png)
    check('7-transparency', transparency)
    check('8-persian-names', persian)
    check('9-one-three-or-text-selection', refused)
    check('10-blink-button', blink_button)
    check('11-8k-memory-and-latency', latency_and_memory)
    check('12-idle-and-frame-time', idle_and_frame_time)
    check('13-search-closes-it', search_closes_it)
    (WORK / 'compare-results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def main():
    for d in ('tmp', 'appdata'):
        (WORK / d).mkdir(parents=True, exist_ok=True)
    os.environ.update(TEMP=str(WORK / 'tmp'), TMP=str(WORK / 'tmp'))
    if '--inside' in sys.argv:
        return inside()
    make_fixture()
    gaming = r'C:\Users\Omid\.agents\tools\gaming.py'
    rc = subprocess.run([sys.executable, gaming, 'check'], creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if rc == 3:
        subprocess.run([sys.executable, gaming, 'wait', '--cap', '60'], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    elif rc:
        return rc
    sys.path.insert(0, r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    with (WORK / 'compare.log').open('wb') as out, (WORK / 'compare.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside', *(a for a in sys.argv[1:] if a == '--light')], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=600)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

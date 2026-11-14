"""Folder sizes in the Details view, driven by real Win32 input on the shared private-desktop driver.

Fixture (all sizes are bytes of file data, made here):
  a 3,000,000   b 1,000,000 + sub/500,000 = 1,500,000 (a nested total)   c 10,000   d empty   x.txt a file
DFM_TEST_FSZ_DELAY_MS slows each folder walk so the "still measuring" state can be read. Run from the repo:
  python tests/use/foldersize_smoke.py        (DFM_EXE, DFM_USE_WORK as in input_smoke.py)
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
os.environ.setdefault('DFM_USE_WORK', str(ROOT / '.agent-work/use-checks' / Path(__file__).stem))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import input_smoke as base

WORK = base.WORK
FIX = WORK / 'fsz'
PICS = WORK / 'final'
SIZES = {'a': 3_000_000, 'b': 1_500_000, 'c': 10_000, 'd': 0}
TEXT = {'a': '3.0 MB', 'b': '1.5 MB', 'c': '10.0 kB', 'd': '0 bytes'}


def make_fixture():
    shutil.rmtree(FIX, ignore_errors=True)
    for d in ('a', 'b/sub', 'c', 'd'):
        (FIX / d).mkdir(parents=True)
    (FIX / 'a' / 'big.bin').write_bytes(b'\0' * 3_000_000)
    (FIX / 'b' / 'one.bin').write_bytes(b'\0' * 1_000_000)
    (FIX / 'b' / 'sub' / 'two.bin').write_bytes(b'\0' * 500_000)
    (FIX / 'c' / 'small.bin').write_bytes(b'\0' * 10_000)
    (FIX / 'x.txt').write_bytes(b'x')


class Fsz(base.App):
    """The app on the fixture in Details, dark."""
    def __init__(self, name, delay_ms=0):
        if delay_ms:
            os.environ['DFM_TEST_FSZ_DELAY_MS'] = str(delay_ms)
        else:
            os.environ.pop('DFM_TEST_FSZ_DELAY_MS', None)
        super().__init__(name, FIX, 'list', 'dark', extra=('--zoom', '0'))

    def sizes(self, s=None):
        return {n: (k, t) for n, k, t in (s or self.snap())['sizes']}

    def row_xy(self, name, s=None):
        s = s or self.snap()
        x, y, w, h = s['row']
        return x + 80, y + (s['names'].index(name) - s['first']) * h + h / 2

    def size_header(self):
        """The Size column title, right-aligned in its column; the header sits one row above the first row."""
        x, y, w, h = self.snap()['row']
        return x + 380 + 70, y - h / 2

    def folders_in_order(self, s=None):
        sz = self.sizes(s)
        return [n for n in (s or self.snap())['names'] if n in sz]

    def picture(self, label):
        self.snap()
        PICS.mkdir(exist_ok=True)
        shutil.copyfile(str(self.base) + '.png', PICS / (label + '.png'))


def inside():
    results = []

    def check(name, fn, delay_ms=0):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        make_fixture()
        a = None
        try:
            a = Fsz(name, delay_ms)
            fn(a)
            results.append(dict(check=name, result='passed'))
        except Exception as e:
            if a and a.proc.poll() is None:
                a.snap(name + '-failed')
            results.append(dict(check=name, result='FAILED', reason=str(e)))
        finally:
            if a:
                a.close()
        print(json.dumps(results[-1]), flush=True)

    def measuring_then_values(a):
        s = a.snap()
        assert 'sizes' in s, 'The state has no folder sizes'
        first = a.sizes(s)
        assert set(first) == set(SIZES), f'Folders listed: {first}'
        assert s['fsz_open'] > 0, 'Nothing is being measured'
        assert all(k == 2 and t == '' for k, t in first.values()), f'Not all "measuring" at first: {first}'
        a.picture('measuring')
        a.until('Sizes never arrived', lambda: a.snap()['fsz_open'] == 0, 20)
        done = a.sizes()
        assert {n: t for n, (k, t) in done.items()} == TEXT and all(k == 1 for k, t in done.values()), f'Sizes: {done}'
        a.picture('sizes')

    def nested_and_files(a):
        a.until('Sizes never arrived', lambda: a.snap()['fsz_open'] == 0, 20)
        s = a.snap()
        assert 'x.txt' not in a.sizes(s) and s['names'][-1] == 'x.txt', f'File among the folders: {s["names"]}'
        assert a.sizes(s)['b'] == (1, '1.5 MB'), 'A nested folder is not counted in its parent'

    def size_sort(a):
        a.until('Listing never showed the folders', lambda: len(a.sizes()) == 4)
        a.click(*a.size_header())
        a.until('Size sort never applied', lambda: a.snap()['names'][0] in SIZES)
        a.click(*a.row_xy('a'))                       # select the biggest while its size is still unknown
        assert a.snap()['selection'] == ['a']
        early = a.folders_in_order()
        a.until('Sizes never arrived', lambda: a.snap()['fsz_open'] == 0, 20)
        a.until('The one re-sort never happened', lambda: a.folders_in_order() != early)
        s = a.snap()
        order = a.folders_in_order(s)
        assert s['names'][-1] == 'x.txt' and s['names'][:4] == order, f'Files first: {s["names"]}'
        assert order in (['d', 'c', 'b', 'a'], ['a', 'b', 'c', 'd']), f'Folders sorted by name or unsorted: {order}'
        assert s['selection'] == ['a'] and s['names'][s['cursor']] == 'a', f'Re-sort lost the selection: {s["selection"]}, cursor {s["cursor"]}'
        a.picture('sorted')
        time.sleep(.6)
        assert a.folders_in_order() == order, 'Folders moved again after the one re-sort'
        a.click(*a.size_header())                      # the other direction
        a.until('Reverse sort', lambda: a.folders_in_order() == order[::-1])
        assert a.snap()['names'][-1] == 'x.txt'

    def pill(a):
        a.until('Listing never showed the folders', lambda: len(a.sizes()) == 4)
        a.click(*a.row_xy('a'))
        s = a.snap()
        assert s['fsz_open'] > 0 and s['sel_pending'], 'Selected folder not measuring'
        assert s['pill'] == '\u201ca\u201d selected (\u2026)', f'Pill while measuring: {s["pill"]!r}'
        a.click(*a.row_xy('b'), (17,))
        s = a.snap()
        # the sum of what is in so far, then the dots: b has a nested folder and finishes after a
        assert s['pill'].startswith('2 items selected (') and s['pill'].endswith('\u2026)'), f'Pill for two while measuring: {s["pill"]!r}'
        a.until('Sizes never arrived', lambda: a.snap()['fsz_open'] == 0, 20)
        a.until('Pill never showed the total', lambda: a.snap()['pill'] == '2 items selected (4.5 MB)')
        a.click(*a.row_xy('a'))
        a.until('Pill for one', lambda: a.snap()['pill'] == '“a” selected (3.0 MB)')

    def changes(a):
        a.until('Sizes never arrived', lambda: a.snap()['fsz_open'] == 0, 20)
        assert a.sizes()['c'] == (1, '10.0 kB')
        with (FIX / 'c' / 'small.bin').open('ab') as f:
            f.write(b'\0' * 2_000_000)
        time.sleep(1.5)
        assert a.sizes()['c'] == (1, '10.0 kB'), 'A folder changed while it stays open must not update by itself'
        time.sleep(6)                                  # longer than the few seconds a measurement counts as recent
        a.click(*a.row_xy('a')); a.click(*a.row_xy('a'))
        assert a.wait_ready()['path'].endswith('\\a'), 'Double click did not open a'
        a.key(37, (18,))                               # back: the folders inside are measured again
        assert a.wait_ready()['path'].endswith('fsz'), 'Back'
        a.until('The changed folder never showed its new size', lambda: a.sizes()['c'] == (1, '2.0 MB'), 12)

    check('1-measuring-then-values', measuring_then_values, 1200)
    check('2-nested-and-files', nested_and_files)
    check('3-size-sort-one-resort', size_sort, 1200)
    check('4-pill', pill, 1200)
    check('5-change-shows-when-entered-again', changes)
    (WORK / 'foldersize-results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def main():
    for d in ('tmp', 'appdata', 'final'):
        (WORK / d).mkdir(parents=True, exist_ok=True)
    os.environ.update(TEMP=str(WORK / 'tmp'), TMP=str(WORK / 'tmp'))
    if '--inside' in sys.argv:
        return inside()
    gaming = r'C:\Users\Omid\.agents\tools\gaming.py'
    rc = subprocess.run([sys.executable, gaming, 'check'], creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if rc == 3:
        subprocess.run([sys.executable, gaming, 'wait', '--cap', '60'], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    elif rc:
        return rc
    sys.path.insert(0, r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    with (WORK / 'foldersize.log').open('wb') as out, (WORK / 'foldersize.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=300)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

"""The disk tree (Ctrl+3), driven by real Win32 input on the shared private-desktop driver.

Fixture (sizes are bytes of file data, made here):
  huge 20,000,000 + 200 files of 1,000   فارسی_پوشه 4,000,000 (Persian names)   mid 5,000,000 + sub/2,000,000
  empty (no files)   denied (no access)   video.mp4 8,000,000   notes.txt 100,000
Checks: layout (area follows size, rectangles tile the pane), nested children of a big folder, Persian names, select / open /
keys, leaving the tree, an empty folder, a folder the user cannot read, a path over 260 characters, the window answering while
a big folder is measured. Pictures of the tree in light and dark go to <work>/final.
Run from the repo:  python tests/use/tree_smoke.py        (DFM_EXE, DFM_USE_WORK as in input_smoke.py)
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
FIX = WORK / 'tree'
PICS = WORK / 'final'
PERSIAN = '\u0641\u0627\u0631\u0633\u06cc_\u067e\u0648\u0634\u0647'
USER = os.environ.get('USERNAME', '')


def make_fixture(deny=True):
    drop_fixture()
    for d in ('huge', PERSIAN, 'mid/sub', 'empty', 'denied'):
        (FIX / d).mkdir(parents=True)
    (FIX / 'huge' / 'big.bin').write_bytes(b'\0' * 20_000_000)
    for i in range(200):
        (FIX / 'huge' / f'tiny{i:03d}.txt').write_bytes(b'x' * 1000)
    (FIX / PERSIAN / '\u06af\u0632\u0627\u0631\u0634 \u0633\u0627\u0644\u0627\u0646\u0647.pdf').write_bytes(b'\0' * 3_000_000)
    (FIX / PERSIAN / '\u0639\u06a9\u0633.jpg').write_bytes(b'\0' * 1_000_000)
    (FIX / 'mid' / 'a.bin').write_bytes(b'\0' * 5_000_000)
    (FIX / 'mid' / 'sub' / 'b.bin').write_bytes(b'\0' * 2_000_000)
    (FIX / 'denied' / 'secret.bin').write_bytes(b'\0' * 1_000_000)
    (FIX / 'video.mp4').write_bytes(b'\0' * 8_000_000)
    (FIX / 'notes.txt').write_bytes(b'\0' * 100_000)
    if deny and USER:
        subprocess.run(['icacls', str(FIX / 'denied'), '/deny', f'{USER}:(OI)(CI)(F)'], capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)


def drop_fixture():
    if FIX.exists():
        subprocess.run(['icacls', str(FIX / 'denied'), '/remove:d', USER], capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
        shutil.rmtree('\\\\?\\' + str(FIX), ignore_errors=True)


class Tree(base.App):
    def __init__(self, name, path=FIX, theme='dark', view='tree', extra=('--zoom', '0'), delay_ms=0):
        if delay_ms:
            os.environ['DFM_TEST_FSZ_DELAY_MS'] = str(delay_ms)   # slows each folder walk so "still measuring" can be seen
        else:
            os.environ.pop('DFM_TEST_FSZ_DELAY_MS', None)
        super().__init__(name, path, view, theme, extra=extra)

    def t(self, s=None):
        return (s or self.snap())['tree']

    def settled(self, seconds=25):
        """Every folder measured and the nested listings read."""
        def done():
            s = self.snap()
            t = s['tree']
            return t['shown'] and t['open'] == 0 and t['n'] > 0
        self.until('Tree never settled', done, seconds)
        time.sleep(.4)   # the nested listings land one frame after the sizes
        return self.snap()

    def center(self, name):
        t = self.t()
        for i in t['items']:
            if i['k'] == 0 and i['n'] == name:
                return t['ox'] + i['x'] + i['w'] / 2, t['oy'] + i['y'] + min(i['h'] / 2, 14), i
        raise AssertionError(f'No rectangle named {name}: {[i["n"] for i in t["items"]]}')

    def picture(self, label):
        self.snap()
        PICS.mkdir(exist_ok=True)
        shutil.copyfile(str(self.base) + '.png', PICS / (label + '.png'))


def check_tiling(t, total, tol=0.02):
    """Rectangles sit inside the pane, never overlap, fill it, and each area is its share of the total."""
    its = t['items']
    pane = t['w'] - 20, t['h'] - 32 - 10
    area = pane[0] * pane[1]
    covered = 0.0
    for i in its:
        assert i['w'] > 0 and i['h'] > 0, f'Empty rectangle {i}'
        assert i['x'] >= 9.9 and i['y'] >= 31.9 and i['x'] + i['w'] <= t['w'] - 9.9 and i['y'] + i['h'] <= t['h'] - 9.9, f'Outside the pane: {i}'
        covered += i['w'] * i['h']
        share = i['size'] / total
        assert abs(i['w'] * i['h'] / area - share) < tol, f'Area of {i["n"]!r} is {i["w"] * i["h"] / area:.3f} of the pane, size share {share:.3f}'
    assert abs(covered / area - 1) < 0.001, f'Rectangles cover {covered / area:.4f} of the pane'
    for a in range(len(its)):
        for b in range(a + 1, len(its)):
            p, q = its[a], its[b]
            ox = min(p['x'] + p['w'], q['x'] + q['w']) - max(p['x'], q['x'])
            oy = min(p['y'] + p['h'], q['y'] + q['h']) - max(p['y'], q['y'])
            assert not (ox > 0.01 and oy > 0.01), f'Overlap {p["n"]!r} / {q["n"]!r}'


def inside():
    results = []

    def check(name, fn, **kw):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        a = None
        try:
            a = Tree(name, **kw)
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

    def layout(a):
        s = a.settled()
        t = a.t(s)
        assert t['on'] and t['shown'], f'Tree not shown: {t["on"]} {t["shown"]}'
        names = {i['n'] for i in t['items'] if i['k'] == 0}
        assert {'huge', PERSIAN, 'mid', 'video.mp4'} <= names, f'Rectangles: {names}'
        total = 20_200_000 + 4_000_000 + 7_000_000 + 8_000_000 + 100_000
        # denied (no access) and empty count 0, so the head says so
        assert 'items' in t['head'] or "can't be read" in t['head'], t['head']
        check_tiling(t, t['total'])
        big = {i['n']: i['size'] for i in t['items'] if i['k'] == 0}
        assert big['huge'] == 20_200_000 and big[PERSIAN] == 4_000_000 and big['mid'] == 7_000_000 and big['video.mp4'] == 8_000_000, big
        assert t['total'] == total, f'Total {t["total"]} not {total}'
        a.picture('tree-' + a.theme)

    def nested(a):
        s = a.settled()
        t = a.t(s)
        huge = next(i for i in t['items'] if i['n'] == 'huge')
        assert huge['nest'] >= 2, f'huge shows {huge["nest"]} nested blocks'
        assert t['subs'] >= 2, f'Nested listings read: {t["subs"]}'

    def hidden_share(a):
        """A folder of a visible 1,000,000-byte file and a hidden 9,000,000-byte one: the visible file is a tenth of the
        folder's rectangle, not all of it; Ctrl+H then shows both files."""
        s = a.settled()
        t = a.t(s)
        hid = next(i for i in t['items'] if i['n'] == 'hid')
        assert hid['nest'] >= 1, f'no nested blocks in {hid}'
        files = lambda it: [r for r in it['nr'] if r[5] >= 0]
        assert len(files(hid)) == 1, f'one visible file expected: {hid["nr"]}'
        share = files(hid)[0][2] * files(hid)[0][3] / (hid['w'] * hid['h'])
        assert 0.04 < share < 0.2, f'the visible file (10 % of the folder) fills {share:.2f} of its rectangle'
        a.key(72, (17,))   # Ctrl+H: hidden files shown
        def both():
            tt = a.t()
            h = next(i for i in tt['items'] if i['n'] == 'hid')
            return len(files(h)) == 2
        a.until('Ctrl+H did not show the hidden file in the nested blocks', both, 15)
        a.key(72, (17,))
        def one():
            tt = a.t()
            h = next(i for i in tt['items'] if i['n'] == 'hid')
            return len(files(h)) == 1
        a.until('Ctrl+H again did not hide the hidden file in the nested blocks', one, 15)

    def many_with_remainder(a):
        """64 visible files and one hidden file as big as all of them: the nested blocks are full (kept items, small items), and the
        unlisted half is still laid out as empty space, so the visible files fill about half of the rectangle."""
        s = a.settled()
        t = a.t(s)
        many = next(i for i in t['items'] if i['n'] == 'many')
        nr = many['nr']
        assert any(r[5] == -2 for r in nr), f'the unlisted remainder is missing from {len(nr)} nested rectangles'
        listed = sum(r[2] * r[3] for r in nr if r[5] != -2)
        whole = sum(r[2] * r[3] for r in nr)
        assert 0.4 < listed / whole < 0.6, f'the listed files fill {listed / whole:.2f} of the folder, not half'

    def select_open(a):
        a.settled()
        x, y, i = a.center('video.mp4')
        a.click(x, y)
        s = a.snap()
        assert s['selection'] == ['video.mp4'] and s['names'][s['cursor']] == 'video.mp4', f'Selection {s["selection"]}'
        a.click(*a.center('mid')[:2])
        assert a.snap()['selection'] == ['mid']
        a.click(*a.center(PERSIAN)[:2], (17,))
        assert set(a.snap()['selection']) == {'mid', PERSIAN}, f'Ctrl click: {a.snap()["selection"]}'
        a.click(*a.center('mid')[:2])
        a.key(13)
        a.until('Enter did not open mid', lambda: a.snap()['path'].endswith('\\mid'))

    def open_by_double(a):
        a.settled()
        x, y, _ = a.center('mid')
        a.click(x, y); a.click(x, y)
        a.until('Double click did not open mid', lambda: a.snap()['path'].endswith('\\mid'))
        s = a.settled()
        t = a.t(s)
        assert t['shown'] and {i['n'] for i in t['items']} >= {'a.bin', 'sub'}, f'Inside mid: {[i["n"] for i in t["items"]]}'
        a.key(37, (18,))
        a.until('Back did not leave mid', lambda: a.snap()['path'].endswith('tree'))
        assert a.settled()['tree']['shown'], 'Tree gone after back'

    def keys_and_leave(a):
        a.settled()
        a.key(ord('3'), (17,))
        s = a.snap()
        assert not s['tree']['on'] and not s['tree']['shown'], 'Ctrl+3 did not leave the tree'
        a.key(ord('3'), (17,))
        assert a.snap()['tree']['on']
        a.settled()
        a.key(ord('1'), (17,))
        s = a.snap()
        assert not s['tree']['on'], 'Ctrl+1 did not leave the tree'
        a.key(ord('3'), (17,))
        a.settled()
        a.key(ord('2'), (17,))
        assert not a.snap()['tree']['on'], 'Ctrl+2 did not leave the tree'

    def arrows(a):
        a.settled()
        a.click(*a.center('huge')[:2])
        first = a.snap()['selection'][0]
        moved = set()
        for vk in (39, 40, 37, 38):
            a.key(vk)
            moved.add(a.snap()['selection'][0])
        assert len(moved | {first}) >= 2, f'Arrow keys never moved the selection: {moved}'

    def rename_and_tooltip(a):
        a.settled()
        x, y, _ = a.center('video.mp4')
        a.click(x, y)
        a.msg(0x200, 0, a.point(x + 30, y + 30))
        a.until('No tooltip for a rectangle', lambda: a.snap() and True)
        time.sleep(1.0)
        a.picture('tooltip-' + a.theme)
        a.key(113)                                  # F2
        a.until('F2 opened no rename box', lambda: a.snap()['act'] & 1 and a.snap()['typing'])
        a.picture('rename-' + a.theme)
        a.key(27)
        a.until('Esc left the rename box open', lambda: not a.snap()['act'] & 1)
        assert a.snap()['tree']['shown'], 'Tree gone after the rename box'

    def leave_while_measuring(a):
        a.until('Measuring never started', lambda: a.snap()['fsz_open'] > 0)
        a.key(ord('1'), (17,))
        a.until('Measuring went on after the tree was left', lambda: a.snap()['fsz_open'] == 0 or a.snap()['tree']['on'] is False and a.snap()['fsz_open'] <= 0, 20)
        s = a.snap()
        assert not s['tree']['on'] and s['tree']['subs'] == 0, f'Tree state after leaving: {s["tree"]}'

    def empty_folder(a):
        s = a.snap()
        assert s['count'] == 0 or s['names'] == [], f'Not empty: {s["names"]}'
        a.picture('empty-' + a.theme)

    def denied_header(a):
        s = a.settled()
        t = a.t(s)
        assert "can't be read" in t['head'] or 'folder' in t['head'], f'Head: {t["head"]}'
        a.picture('denied')

    def long_path(a):
        s = a.settled()
        t = a.t(s)
        assert t['n'] >= 1 and t['total'] > 0, f'Nothing for the long path: {t}'

    def responsive(a):
        # a big tree being measured: the window answers each message at once
        worst = 0.0
        end = time.monotonic() + 8
        while time.monotonic() < end:
            worst = max(worst, a.msg(0))
            time.sleep(.02)
        print(json.dumps(dict(worst_message_ms=round(worst * 1000, 1))), flush=True)
        assert worst < .25, f'The window took {worst * 1000:.0f} ms to answer'

    make_fixture()
    check('1-layout-dark', layout)
    check('1-layout-light', layout, theme='light')
    check('2-nested', nested)
    hid = WORK / 'tree2'
    (hid / 'hid').mkdir(parents=True, exist_ok=True)
    (hid / 'hid' / 'vis.bin').write_bytes(b'\0' * 1_000_000)
    (hid / 'hid' / 'hid.bin').write_bytes(b'\0' * 9_000_000)
    subprocess.run(['attrib', '+h', str(hid / 'hid' / 'hid.bin')], capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
    check('2b-hidden-share-and-ctrl-h', hidden_share, path=hid)
    many = WORK / 'tree3'
    (many / 'many').mkdir(parents=True, exist_ok=True)
    for i in range(64):
        (many / 'many' / f'f{i:02d}.bin').write_bytes(bytes(1000))
    (many / 'many' / 'hidden.bin').write_bytes(bytes(64000))
    subprocess.run(['attrib', '+h', str(many / 'many' / 'hidden.bin')], capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
    check('2c-many-files-keep-the-remainder', many_with_remainder, path=many)
    check('3-select-open-keys', select_open)
    check('4-double-click-opens-then-back', open_by_double)
    check('5-ctrl-3-and-leave', keys_and_leave)
    check('6-arrow-keys', arrows)
    check('6b-rename-box-and-tooltip', rename_and_tooltip)
    check('6c-leave-while-measuring', leave_while_measuring, delay_ms=1500)
    check('7-empty-folder', empty_folder, path=FIX / 'empty')
    check('8-denied-header', denied_header, path=FIX)
    # a path over 260 characters: the folder below it is 300 characters deep
    deep = FIX / 'L'
    long_dir = deep / ('d' * 120) / ('e' * 120) / ('f' * 120)
    os.makedirs('\\\\?\\' + str(long_dir))
    with open('\\\\?\\' + str(long_dir / 'f.bin'), 'wb') as f:
        f.write(b'\0' * 100_000)
    with open('\\\\?\\' + str(deep / 'x.bin'), 'wb') as f:
        f.write(b'\0' * 50_000)
    check('9-long-path', long_path, path=deep)
    (WORK / 'tree-results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    drop_fixture()
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
    with (WORK / 'tree.log').open('wb') as out, (WORK / 'tree.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=420)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

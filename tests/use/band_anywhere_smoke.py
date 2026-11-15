"""The selection box starts from an item's blank part, not only from the empty background (real input, private desktop).
A press on an item's icon or name text stays an item press; the rest of a row or a cell starts the box once the pointer moves."""
import ctypes as C
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
os.environ.setdefault('DFM_USE_WORK', str(ROOT / '.agent-work/use-checks' / Path(__file__).stem))
import input_smoke as base
from select_smoke import press, move, release
from view_smoke import set_stop, cell_xy

WORK = base.WORK
U = base.U
NAMES = lambda idx: sorted(f'item-{i:04}.txt' for i in idx)
PERSIAN = 'ا-گزارش-کوتاه-سالانه.txt'
PERSIAN_LONG = 'ب-گزارش-سالانهٔ-بسیار-طولانی-برای-بررسی-نام-فارسی-در-فهرست-پوشه-های-بزرگ.txt'


def drag(a, start, end, mods=(), steps=4):
    press(a, *start, mods)
    for n in range(1, steps + 1):
        move(a, start[0] + (end[0] - start[0]) * n / steps, start[1] + (end[1] - start[1]) * n / steps)
    return a.snap()


def details(a, theme):
    dpi = U.GetDpiForWindow(a.hwnd) / 96
    U.SetWindowPos(a.hwnd, None, 0, 0, int(1100 * dpi), int(1000 * dpi), 0x16)   # tall enough for rows 3 to 20 without the edge scroll
    time.sleep(.5)
    s = set_stop(a, 0)
    x, y, w, h = s['row']
    assert w > 520, f'Row too narrow for the Size cell: {w}'
    ry = lambda i: y + (i - s['first']) * 36 + 18
    # Row 3's Size cell to row 20: 18 rows.
    s = drag(a, (x + 420, ry(2)), (x + 560, ry(19)))
    assert s['band'], 'No box from a Size cell'
    assert sorted(s['selection']) == NAMES(range(2, 20)), ('size cell', len(s['selection']), s['selection'][:3])
    shutil.copyfile(str(a.base) + '.png', WORK / 'final' / f'details-size-cell-{theme}.png')
    release(a, x + 560, ry(19))
    # Beside the name text, and the Modified and Type cells.
    for dx, label in ((300, 'beside name'), (520, 'modified'), (660, 'type')):
        if dx + 20 > w:
            continue
        s = drag(a, (x + dx, ry(4)), (x + dx, ry(8)))
        assert s['band'] and sorted(s['selection']) == NAMES(range(4, 9)), (label, s['selection'])
        release(a, x + dx, ry(8))
    # A click with no drag on the blank part selects the row; Ctrl+click toggles; no box.
    a.click(x + 300, ry(6))
    s = a.snap()
    assert not s['band'] and s['selection'] == NAMES([6]), ('click', s['selection'])
    a.click(x + 300, ry(7), (17,))
    assert a.snap()['selection'] == NAMES([6, 7]), 'Ctrl+click on a blank part'
    # Ctrl+drag adds to the saved selection.
    a.click(x + 300, ry(1))
    s = drag(a, (x + 420, ry(5)), (x + 420, ry(8)), (17,))
    assert s['band'] and sorted(s['selection']) == NAMES([1, 5, 6, 7, 8]), ('ctrl', s['selection'])
    release(a, x + 420, ry(8))
    # A press on the name text, then a drag: an item press, no box.
    a.click(x + 300, ry(0))
    px, py = x + 60, ry(3)
    press(a, px, py)
    move(a, px + 10, py + 30)
    s = a.snap()
    assert not s['band'], 'A press on a name started a box'
    release(a, px + 10, py + 30)
    # Auto-scroll past the bottom edge.
    sx, sy = x + 420, ry(3)
    press(a, sx, sy)
    move(a, sx, sy + 100)
    end = (sx, a.snap()['chrome']['client'][3] - a.snap()['chrome']['client'][1] + 30)
    U.PostMessageW(a.hwnd, 0x200, 1, a.point(*end))
    a.until('Edge did not scroll', lambda: a.snap()['first'] > 3, 8)
    s = a.snap()
    assert s['band'] and 'item-0003.txt' in s['selection'] and len(s['selection']) > 12, ('autoscroll', s['first'], len(s['selection']))
    release(a, *end)
    time.sleep(.3)


def grid(a, stop, view_name):
    s = set_stop(a, stop)
    x, y, w, h = s['row']
    if stop == 1:
        rows, first = s['column_rows'], s['first']
        px = lambda c: x + 16 + (c - first) * 240
        # Blank part of a List row, right of the name text, to another column.
        start = (px(0) + 200, y + 1 * 32 + 16)
        end = (px(1) + 100, y + 3 * 32 + 16)
        expected = [r + c * rows for c in (0, 1) for r in (1, 2, 3)]
    else:
        cols = s['cols']
        cw = (w - 32) / cols
        assert cols >= 3, f'Too few columns: {cols}'
        # The gap between two icons: just inside the padding of the second cell.
        start = (x + 16 + cw + 4, y + 40)
        end = (x + 16 + 2.5 * cw, y + h + 40)
        expected = [1, 2, cols + 1, cols + 2]
    s = drag(a, start, end)
    assert s['band'], f'{view_name}: no box from blank padding'
    assert sorted(s['selection']) == NAMES(expected), (view_name, NAMES(expected), s['selection'])
    shutil.copyfile(str(a.base) + '.png', WORK / 'final' / f'{view_name}.png')
    release(a, *end)
    # Ctrl+drag adds; the press on the icon itself is an item press (no box).
    a.click(*cell_xy(a, 0))
    s = drag(a, start, end, (17,))
    assert s['band'] and sorted(s['selection']) == NAMES(sorted(set(expected + [0]))), (view_name, 'ctrl', s['selection'])
    release(a, *end)
    ix, iy = cell_xy(a, 0)
    press(a, ix, iy)
    move(a, ix + 12, iy + 12)
    assert not a.snap()['band'], f'{view_name}: a press on an icon started a box'
    release(a, ix + 12, iy + 12)
    # A click with no drag on blank padding selects that item.
    a.click(*start)
    s = a.snap()
    assert not s['band'] and len(s['selection']) == 1, (view_name, 'click', s['selection'])


def small_window(a, stop):
    s = set_stop(a, stop)
    dpi = U.GetDpiForWindow(a.hwnd) / 96
    U.SetWindowPos(a.hwnd, None, 0, 0, int(360 * dpi), int(280 * dpi), 0x16)
    time.sleep(.5)
    s = a.snap()
    x, y, w, h = s['row']
    if stop == 0:
        ry = lambda i: y + (i - s['first']) * 36 + 18
        start, end = (x + min(w - 60, 200), ry(1)), (x + min(w - 60, 200), ry(3))
        expected = [1, 2, 3]
    else:
        cols = s['cols']
        cw = (w - 32) / cols
        start = (x + 16 + 4, y + 6)
        end = (x + 16 + cols * cw - 4, y + h * .5 + 4)
        expected = list(range(cols))
    s = drag(a, start, end)
    assert s['band'], f'360x280 stop {stop}: no box'
    assert sorted(s['selection'])[:len(expected)] == NAMES(expected) or set(NAMES(expected)) <= set(s['selection']), (stop, s['selection'])
    shutil.copyfile(str(a.base) + '.png', WORK / 'final' / f'small-window-{stop}.png')
    release(a, *end)


def persian(a):
    s = set_stop(a, 0)
    x, y, w, h = s['row']
    ry = lambda i: y + (i - s['first']) * 36 + 18
    assert s['count'] == 4, s['count']
    # Index 2 is the short Persian name: its blank part (right of its text) starts the box; its text does not.
    start = (x + 340, ry(2))
    s = drag(a, start, (x + 340, ry(0)))
    assert s['band'] and sorted(s['selection']) == sorted(['a.txt', 'b.txt', PERSIAN]), ('persian blank part', s['selection'])
    shutil.copyfile(str(a.base) + '.png', WORK / 'final' / 'persian-details.png')
    release(a, x + 340, ry(0))
    a.click(x + 300, ry(1))
    press(a, x + 100, ry(2))
    move(a, x + 110, ry(2) - 30)
    assert not a.snap()['band'], 'A press on the Persian name started a box'
    release(a, x + 110, ry(2) - 30)
    # Index 3: a name longer than the column has no blank part; the whole cell is its text.
    press(a, x + 340, ry(3))
    move(a, x + 350, ry(3) - 40)
    assert not a.snap()['band'], 'A press on a long Persian name started a box'
    release(a, x + 350, ry(3) - 40)
    # Its row's Size cell is still blank.
    s = drag(a, (x + 420, ry(3)), (x + 420, ry(0)))
    assert s['band'] and s['selected'] == 4, ('persian size cell', s['selection'])
    shutil.copyfile(str(a.base) + '.png', WORK / 'final' / 'persian-long-details.png')
    release(a, x + 420, ry(3))


def open_blank(a, stop):
    s = set_stop(a, stop)
    x, y, w, h = s['row']
    if stop == 0:
        pt = (x + 300, y + 18)
    else:
        pt = (x + 16 + 4, y + 6)   # a Medium tile's padding
    a.click(*pt)
    assert a.snap()['selection'] == ['sub'], ('click on blank part', a.snap()['selection'])
    a.click(*pt)
    a.click(*pt)
    a.until('Double click on a blank part did not open the folder', lambda: a.snap()['path'].endswith('sub'))


def inside():
    results = []

    def check(name, folder, fn, view='grid', theme='light'):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        a = None
        try:
            a = base.App(name, folder, view, theme)
            fn(a)
            results.append(dict(check=name, result='passed'))
        except Exception as e:
            if a and a.proc.poll() is None:
                a.snap(name + '-failed')
            results.append(dict(check=name, result='FAILED', reason=repr(e)))
        finally:
            if a:
                a.close()
        print(json.dumps(results[-1], ensure_ascii=False), flush=True)

    check('1-details-dark', WORK / 'many', lambda a: details(a, 'dark'), theme='dark')
    check('1-details-light', WORK / 'many', lambda a: details(a, 'light'))
    check('2-list', WORK / 'many', lambda a: grid(a, 1, 'list-light'))
    check('3-icons-medium', WORK / 'many', lambda a: grid(a, 3, 'icons-light'))
    check('4-icons-small', WORK / 'many', lambda a: grid(a, 2, 'small-icons-light'))
    check('5-icons-dark', WORK / 'many', lambda a: grid(a, 4, 'icons-large-dark'), theme='dark')
    check('6-persian', WORK / 'persian', persian)
    check('7-small-details', WORK / 'many', lambda a: small_window(a, 0))
    check('8-small-icons', WORK / 'many', lambda a: small_window(a, 3))
    check('9-open-blank-details', WORK / 'dir', lambda a: open_blank(a, 0))
    check('9-open-blank-icons', WORK / 'dir', lambda a: open_blank(a, 3))
    print(json.dumps(results, ensure_ascii=False))
    return int(any(r['result'] != 'passed' for r in results))


def main():
    for folder in ('tmp', 'final', 'appdata', 'many', 'persian', 'dir', 'dir/sub'):
        (WORK / folder).mkdir(parents=True, exist_ok=True)
    os.environ.update(TEMP=str(WORK / 'tmp'), TMP=str(WORK / 'tmp'))
    if '--inside' in sys.argv:
        return inside()
    for i in range(5200):
        (WORK / 'many' / f'item-{i:04}.txt').touch(exist_ok=True)
    for n in ('a.txt', 'b.txt', PERSIAN, PERSIAN_LONG):
        (WORK / 'persian' / n).touch(exist_ok=True)
    gaming = r'C:\Users\Omid\.agents\tools\gaming.py'
    rc = subprocess.run([sys.executable, gaming, 'check'], creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if rc == 3:
        subprocess.run([sys.executable, gaming, 'wait', '--cap', '60'], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    elif rc:
        return rc
    sys.path.insert(0, r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    with (WORK / 'band.log').open('wb') as out, (WORK / 'band.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=900)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

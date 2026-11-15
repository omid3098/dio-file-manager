"""Drag and drop on the private desktop (docs/design.md, "Drag and drop").
Drag out: real mouse messages press an item's icon and move past the drag distance; in test mode the window then reads the
data its drag would carry (CF_HDROP, Preferred DropEffect) instead of starting OLE, which needs a real pointer.
Drop in: test-only WM_COPYDATA commands hand a real IDataObject (CF_HDROP of sandbox files) to the window's own IDropTarget at
client points and keys, through the same code OLE calls; the jobs and the files it makes are checked.
Not covered here: a real drag between two programs (a browser upload, Explorer); the first real use checks it.
Needs a free drive letter for `subst` (the second "drive" is a sandbox folder). Never touches files outside DFM_USE_WORK.
"""
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

WORK = base.WORK
U = base.U
SAND = WORK / 'dnd'
PERSIAN = 'گزارش-نهایی-بسیار-طولانی-سال-۱۴۰۵-با-نام-خیلی-بلند-برای-آزمایش-نمایش.txt'
COPY, MOVE = 1, 2
LONG = '\\\\?\\'   # the extended prefix: Python itself needs it to make, read and remove a path past 260 characters
SHIFT, CTRL = 4, 8


class CopyData(C.Structure):
    _fields_ = [('dwData', C.c_size_t), ('cbData', C.c_ulong), ('lpData', C.c_void_p)]


def send(a, *fields):
    data = '\t'.join(str(f) for f in fields).encode('utf-8')
    buf = C.create_string_buffer(data, len(data))
    cd = CopyData(0x444E44, len(data), C.cast(buf, C.c_void_p))
    r = C.c_size_t()
    assert U.SendMessageTimeoutW(a.hwnd, 0x4A, 0, C.addressof(cd), 2, 8000, C.byref(r)), 'The window did not answer a drag command'


def px(a, x, y):
    dpi = U.GetDpiForWindow(a.hwnd) / 96
    return int(x * dpi), int(y * dpi)


def enter(a, x, y, paths, keys=0, allowed=COPY | MOVE):
    X, Y = px(a, x, y)
    send(a, 'enter', X, Y, keys, allowed, *[str(p) for p in paths])
    return a.snap()['dnd']


def over(a, x, y, keys=0, allowed=COPY | MOVE):
    X, Y = px(a, x, y)
    send(a, 'over', X, Y, keys, allowed)
    return a.snap()['dnd']


def drop(a, x, y, keys=0, allowed=COPY | MOVE):
    X, Y = px(a, x, y)
    send(a, 'drop', X, Y, keys, allowed)
    return a.snap()['dnd']


def leave(a):
    send(a, 'leave')


def jobs_done(a, seconds=60):
    a.until('Jobs did not finish', lambda: not a.snap()['jobs'] and not a.snap()['act'] & 8, seconds)


def item_xy(a, name):
    s = a.snap()
    i = s['names'].index(name)
    if s['view'] == 1 and s['zoom'] == 0:   # Details
        x, y, w, h = s['row']
        return x + 30, y + (i - s['first']) * 36 + 18
    return a.cell_xy(i)


def bg_xy(a):
    d = a.snap()['dnd']['content']
    return d[0] + d[2] - 40, d[1] + d[3] - 30   # the empty corner below the items


def row_xy(a, target):
    for x, y, w, h, t in a.snap()['dnd']['rows']:
        if t.lower() == str(target).lower():
            return x + w / 2, y + h / 2
    raise AssertionError(f'No sidebar row for {target}: {a.snap()["dnd"]["rows"]}')


def fresh():
    for child in SAND.glob('*') if SAND.exists() else []:   # the sandbox drive's own folder stays: a drive letter is mapped to it
        if child.name != 'drive':
            shutil.rmtree(LONG + str(child), ignore_errors=True)
    for child in (SAND / 'drive').glob('*') if (SAND / 'drive').exists() else []:
        shutil.rmtree(LONG + str(child), ignore_errors=True) if child.is_dir() else child.unlink()
    for d in ('src', 'ext', 'dst', 'dst/sub', 'dst/sub/deep', 'drive', 'many'):
        (SAND / d).mkdir(parents=True, exist_ok=True)
    for n in ('a.txt', 'b.txt', PERSIAN):
        (SAND / 'src' / n).write_text('x ' + n, encoding='utf-8')
    (SAND / 'src' / 'folder').mkdir()
    (SAND / 'src' / 'folder' / 'inner.txt').write_text('inner', encoding='utf-8')
    for i in range(300):
        (SAND / 'many' / f'file-{i:03}.txt').write_text(str(i), encoding='utf-8')
    for n in ('one.txt', 'two.txt', 'three.txt'):
        (SAND / 'ext' / n).write_text(n, encoding='utf-8')
    (SAND / 'dst' / 'keep.txt').write_text('keep', encoding='utf-8')


def long_dir(base):
    """base plus five folders of 100 Persian letters each: a path of about 590 characters (over 1040 bytes in UTF-8)."""
    d = Path(base)
    for part in ('گ', 'ر', 'م', 'ن', 'ی'):
        d = d / (part * 100)
    os.makedirs(LONG + str(d), exist_ok=True)
    return d


def long_file(folder, name, text):
    p = Path(folder) / name
    with open(LONG + str(p), 'w', encoding='utf-8') as f:
        f.write(text)
    assert len(str(p)) > 400 and len(str(p).encode('utf-8')) > 800, len(str(p))
    return p


def long_read(p):
    try:
        with open(LONG + str(p), encoding='utf-8') as f:
            return f.read()
    except OSError:
        return None


def free_letter():
    for letter in 'QRSTUVWXYZ':
        if not Path(f'{letter}:\\').exists():
            return letter
    raise AssertionError('No free drive letter for the sandbox drive')


def press_and_move(a, x, y, dx, dy, mods=()):
    a.keys(mods)
    p = a.point(x, y)
    a.msg(0x200, 0, p)
    time.sleep(.05)
    a.msg(0x201, 1, p)
    time.sleep(.05)
    q = a.point(x + dx, y + dy)
    a.msg(0x200, 1, q)
    time.sleep(.15)
    a.msg(0x202, 0, q)
    a.keys(())
    time.sleep(.1)


def inside():
    results = []
    letter = free_letter()
    drive = f'{letter}:\\'
    shutil.rmtree(SAND, ignore_errors=True)
    (SAND / 'drive').mkdir(parents=True)

    def check(name, fn, folder='dst', view='grid', theme='light', extra=()):
        if os.environ.get('USE_ONLY') and not name.split('-')[0] in os.environ['USE_ONLY'].split(','):
            return
        app = None
        try:
            fresh()
            app = base.App(name, SAND / folder, view, theme, extra)
            fn(app)
            app.snap(name + '-passed')
            results.append(dict(check=name, result='passed'))
        except Exception as e:
            if app:
                try:
                    app.snap(name + '-failed')
                except Exception as capture_error:
                    e = RuntimeError(f'{e}; capture failed: {capture_error}')
            results.append(dict(check=name, result='FAILED', reason=str(e)))
        finally:
            if app:
                app.close()
        print(json.dumps(results[-1], ensure_ascii=False), flush=True)

    # ---- out ----
    def out_file(a):
        x, y = item_xy(a, 'a.txt')
        press_and_move(a, x, y, 3, 2)
        assert a.snap()['dnd']['drags'] == 0, 'A press that barely moved must not start a drag'
        press_and_move(a, x, y, 40, 10)
        s = a.snap()
        assert s['dnd']['drags'] == 1, s['dnd']
        assert s['selection'] == ['a.txt'], f'A press on an unselected item selects it first: {s["selection"]}'
        want = str(SAND / 'src' / 'a.txt')
        assert s['dnd']['drag'] == [want] and s['dnd']['drag_data'] == [want], s['dnd']
        assert s['dnd']['preferred'] == COPY, s['dnd']
        assert s['loc'] == 0 and s['path'] == str(SAND / 'src')

    def out_folder_persian(a):
        x, y = item_xy(a, 'folder')
        press_and_move(a, x, y, 40, 10)
        assert a.snap()['dnd']['drag_data'] == [str(SAND / 'src' / 'folder')], a.snap()['dnd']
        x, y = item_xy(a, PERSIAN)
        press_and_move(a, x, y, 40, 10)
        d = a.snap()['dnd']
        assert d['drags'] == 2 and d['drag_data'] == [str(SAND / 'src' / PERSIAN)], d
        a.snap('out-persian-name')

    def out_selection(a):
        for n in ('a.txt', 'b.txt'):
            x, y = item_xy(a, n)
            a.click(x, y, (17,))
        x, y = item_xy(a, 'b.txt')
        press_and_move(a, x, y, 40, 10)
        d = a.snap()['dnd']
        assert sorted(Path(p).name for p in d['drag_data']) == ['a.txt', 'b.txt'], d
        x, y = item_xy(a, 'folder')   # an unselected item: only it is dragged
        press_and_move(a, x, y, 40, 10)
        assert a.snap()['selection'] == ['folder'] and a.snap()['dnd']['drag_data'] == [str(SAND / 'src' / 'folder')]

    def out_300(a):
        a.key(ord('A'), (17,))
        assert a.snap()['selected'] == 300
        x, y = item_xy(a, 'file-000.txt')
        t0 = time.monotonic()
        press_and_move(a, x, y, 40, 10)
        d = a.snap()['dnd']
        assert len(d['drag_data']) == 300 and sorted(d['drag_data']) == sorted(str(SAND / 'many' / f'file-{i:03}.txt') for i in range(300)), len(d['drag_data'])
        print(json.dumps({'drag_of_300_seconds': round(time.monotonic() - t0, 2)}), flush=True)

    def out_details(a):
        x, y = item_xy(a, 'b.txt')
        press_and_move(a, x, y, 40, 4)
        d = a.snap()['dnd']
        assert d['drags'] == 1 and d['drag_data'] == [str(SAND / 'src' / 'b.txt')], d
        s = a.snap()
        x, y, w, h = s['row']
        press_and_move(a, x + w - 20, y + 18, 40, 4)   # the blank end of a row is for clicks and the selection box, not a drag
        assert a.snap()['dnd']['drags'] == 1, 'A press far from the icon and name must not drag'

    # ---- in ----
    def in_background(a):
        x, y = bg_xy(a)
        d = enter(a, x, y, [SAND / 'ext' / 'one.txt'])
        assert (d['kind'], d['effect'], d['dest']) == (1, MOVE, str(SAND / 'dst')), d   # the same drive moves
        a.snap('drop-over-background')
        d = drop(a, x, y)
        assert d['last_effect'] == MOVE and d['last_n'] == 1 and d['last_dest'] == str(SAND / 'dst'), d
        jobs_done(a)
        assert (SAND / 'dst' / 'one.txt').read_text() == 'one.txt' and not (SAND / 'ext' / 'one.txt').exists()
        a.until('The moved file was not listed', lambda: 'one.txt' in a.snap()['names'])
        # Ctrl copies
        d = enter(a, x, y, [SAND / 'ext' / 'two.txt'], CTRL)
        assert d['effect'] == COPY, d
        drop(a, x, y, CTRL)
        jobs_done(a)
        assert (SAND / 'dst' / 'two.txt').exists() and (SAND / 'ext' / 'two.txt').exists()
        # a source that offers copy only
        d = enter(a, x, y, [SAND / 'ext' / 'three.txt'], 0, COPY)
        assert d['effect'] == COPY, d
        drop(a, x, y, 0, COPY)
        jobs_done(a)
        assert (SAND / 'dst' / 'three.txt').exists() and (SAND / 'ext' / 'three.txt').exists()

    def in_folder_item(a):
        x, y = item_xy(a, 'sub')
        d = enter(a, x, y, [SAND / 'ext' / 'one.txt', SAND / 'ext' / 'two.txt'])
        assert (d['kind'], d['effect'], d['dest'], d['item']) == (2, MOVE, str(SAND / 'dst' / 'sub'), a.snap()['names'].index('sub')), d
        a.snap('drop-over-folder')
        drop(a, x, y)
        jobs_done(a)
        assert sorted(p.name for p in (SAND / 'dst' / 'sub').glob('*.txt')) == ['one.txt', 'two.txt']
        # a file under the pointer is not a drop target: the drop goes into the open folder
        x, y = item_xy(a, 'keep.txt')
        d = enter(a, x, y, [SAND / 'ext' / 'three.txt'])
        assert (d['kind'], d['dest']) == (1, str(SAND / 'dst')), d
        leave(a)

    def in_refused(a):
        x, y = item_xy(a, 'sub')
        d = enter(a, x, y, [SAND / 'dst' / 'sub'])   # a folder onto itself
        assert d['effect'] == 0 and d['kind'] == 0, d
        drop(a, x, y)
        time.sleep(.5)
        assert a.snap()['dnd']['drops'] == 0 and not a.snap()['jobs'] and (SAND / 'dst' / 'sub' / 'deep').exists()
        d = enter(a, x, y, [SAND / 'dst' / 'keep.txt'])   # a move into the folder it already sits in (the background)
        bx, by = bg_xy(a)
        d = over(a, bx, by)
        assert d['effect'] == 0, d
        d = over(a, bx, by, CTRL)   # a copy there is allowed: Keep Both makes the second name
        assert d['effect'] == COPY, d
        leave(a)
        d = enter(a, bx, by, [SAND / 'dst' / 'sub'], CTRL)
        assert d['effect'] == COPY and d['kind'] == 1, d   # a folder into its own parent as a copy is fine
        leave(a)

    def in_own_subfolder(a):
        bx, by = bg_xy(a)
        d = enter(a, bx, by, [SAND / 'dst' / 'sub'])   # the open folder is inside the dragged folder
        assert d['effect'] == 0 and d['kind'] == 0, d
        d = over(a, bx, by, CTRL)
        assert d['effect'] == 0, d
        drop(a, bx, by)
        assert a.snap()['dnd']['drops'] == 0

    def in_conflict(a):
        (SAND / 'ext' / 'keep.txt').write_text('other', encoding='utf-8')
        bx, by = bg_xy(a)
        enter(a, bx, by, [SAND / 'ext' / 'keep.txt'], CTRL)
        drop(a, bx, by, CTRL)
        a.until('The conflict dialog did not open', lambda: a.snap()['act'] & 8)
        a.snap('drop-conflict')
        a.click(*center(a.snap()['conflict_keep']))
        jobs_done(a)
        assert (SAND / 'dst' / 'keep.txt').read_text() == 'keep' and len(list((SAND / 'dst').glob('keep*.txt'))) == 2

    def in_drive_row(a):
        x, y = row_xy(a, drive)
        d = enter(a, x, y, [SAND / 'ext' / 'one.txt'])
        assert (d['kind'], d['effect']) == (3, COPY) and d['dest'].lower() == drive.lower(), d   # another drive copies
        a.snap('drop-over-drive')
        drop(a, x, y)
        jobs_done(a)
        assert (SAND / 'drive' / 'one.txt').exists() and (SAND / 'ext' / 'one.txt').exists()
        d = enter(a, x, y, [SAND / 'ext' / 'two.txt'], SHIFT)
        assert d['effect'] == MOVE, d
        drop(a, x, y, SHIFT)
        jobs_done(a)
        assert (SAND / 'drive' / 'two.txt').exists() and not (SAND / 'ext' / 'two.txt').exists()
        d = enter(a, x, y, [SAND / 'ext' / 'three.txt'], SHIFT | CTRL)
        assert d['effect'] == 0, d
        leave(a)

    def in_trash(a):
        x, y = row_xy(a, '@3')
        d = enter(a, x, y, [SAND / 'ext' / 'one.txt'])
        assert (d['kind'], d['effect']) == (4, MOVE), d
        d = over(a, x, y, CTRL)
        assert d['effect'] == MOVE, d   # the Recycle Bin only takes
        a.snap('drop-over-trash')
        drop(a, x, y)
        jobs_done(a)
        assert not (SAND / 'ext' / 'one.txt').exists()
        a.until('The bin count did not change', lambda: a.snap()['trash'] > 0)

    def in_added_folder_row(a):
        # a folder added with Add to Sidebar (Ctrl+D) takes a drop as the built-in places do; a missing one takes none
        target = SAND / 'dst' / 'sub'
        a.path(target)
        a.key(ord('D'), (17,))
        a.path(SAND / 'ext')
        a.until('the added folder has no sidebar row', lambda: any(t.lower() == str(target).lower() for *_, t in a.snap()['dnd']['rows']), 4)
        x, y = row_xy(a, target)
        d = enter(a, x, y, [SAND / 'ext' / 'one.txt'])
        assert (d['kind'], d['dest'].lower()) == (3, str(target).lower()), d
        a.snap('drop-over-added-folder')
        drop(a, x, y)
        jobs_done(a)
        assert (target / 'one.txt').exists() and not (SAND / 'ext' / 'one.txt').exists()
        shutil.rmtree(target)
        a.until('the row of a deleted folder still takes a drop', lambda: (a.msg(0), not any(t.lower() == str(target).lower() for *_, t in a.snap()['dnd']['rows']))[1], 6)
        x, y = [r for r in a.snap()['sbrows'] if r['key'].lower() == str(target).lower()][0]['r'][:2]
        d = enter(a, x + 40, y + 18, [SAND / 'ext' / 'two.txt'])
        assert d['kind'] == 0 and d['effect'] == 0, d
        leave(a)
        assert (SAND / 'ext' / 'two.txt').exists()

    def in_busy(a):
        big = SAND / 'ext' / 'big.bin'
        with big.open('wb') as f:
            f.write(b'B' * (24 << 20))
        other = SAND / 'ext' / 'big2.bin'
        with other.open('wb') as f:
            f.write(b'C' * (24 << 20))
        bx, by = bg_xy(a)
        for p in (big, other):
            enter(a, bx, by, [p], CTRL)
            drop(a, bx, by, CTRL)
        a.until('Two jobs did not run at once', lambda: len(a.snap()['jobs']) == 2)
        s = a.snap('drop-while-copying')
        jobs_done(a, 120)
        assert (SAND / 'dst' / 'big.bin').stat().st_size == 24 << 20 and (SAND / 'dst' / 'big2.bin').stat().st_size == 24 << 20

    def in_scroll(a):
        s = a.snap()
        d = s['dnd']
        x = d['content'][0] + d['content'][2] / 2
        bottom = d['content'][1] + d['content'][3] - 8
        enter(a, x, bottom, [SAND / 'ext' / 'one.txt'])
        before = a.snap()['dnd']['scroll_y']
        for _ in range(12):
            over(a, x, bottom)
            time.sleep(.05)
        after = a.snap()['dnd']['scroll_y']
        assert after > before + 20, (before, after)
        top = d['content'][1] + (40 if s['zoom'] == 0 else 0) + 8
        for _ in range(40):
            over(a, x, top)
            time.sleep(.05)
        assert a.snap()['dnd']['scroll_y'] < after, 'The top edge did not scroll back'
        leave(a)

    def in_small_window(a):
        x, y = item_xy(a, 'sub')
        d = enter(a, x, y, [SAND / 'ext' / 'one.txt'])
        assert d['kind'] == 2, d
        a.snap('drop-small-window')
        leave(a)
        assert not a.snap()['dnd']['session']

    def in_leave(a):
        x, y = item_xy(a, 'sub')
        enter(a, x, y, [SAND / 'ext' / 'one.txt'])
        leave(a)
        d = a.snap()['dnd']
        assert d['session'] == 0 and d['effect'] == 0 and d['drops'] == 0
        assert (SAND / 'ext' / 'one.txt').exists()

    def in_details_background(a):
        # over a folder, then into blank space: the drop goes into the open folder, not the folder the pointer left
        x, y = item_xy(a, 'sub')
        d = enter(a, x, y, [SAND / 'ext' / 'one.txt'])
        assert d['kind'] == 2, d
        bx, by = bg_xy(a)
        d = over(a, bx, by)
        assert (d['kind'], d['dest'], d['item']) == (1, str(SAND / 'dst'), -1), d
        drop(a, bx, by)
        jobs_done(a)
        assert (SAND / 'dst' / 'one.txt').exists() and not (SAND / 'dst' / 'sub' / 'one.txt').exists()

    def in_effect_choice(a):
        # a Shift or Ctrl the source does not allow is refused; with no key the one effect it offers is taken
        bx, by = bg_xy(a)
        one = SAND / 'ext' / 'one.txt'
        assert enter(a, bx, by, [one], CTRL, MOVE)['effect'] == 0
        assert over(a, bx, by, SHIFT, COPY)['effect'] == 0
        assert over(a, bx, by, 0, COPY)['effect'] == COPY
        assert over(a, bx, by, 0, MOVE)['effect'] == MOVE
        assert over(a, bx, by, CTRL, COPY | MOVE)['effect'] == COPY
        over(a, bx, by, CTRL, MOVE)
        drop(a, bx, by, CTRL, MOVE)
        time.sleep(.5)
        assert a.snap()['dnd']['drops'] == 0 and one.exists() and not (SAND / 'dst' / 'one.txt').exists()

    def in_long_path(a):
        # a source path of 590 characters (over 1040 bytes in UTF-8) reaches the job and the file arrives, by copy and by move
        top = SAND / 'ext' / ('گ' * 100)
        try:
            deep = long_dir(SAND / 'ext')
            one = long_file(deep, 'long-one.txt', 'long one')
            two = long_file(deep, 'long-two.txt', 'long two')
            assert len(str(one)) > 560, len(str(one))   # the 590-character path of the request
            bx, by = bg_xy(a)
            d = enter(a, bx, by, [SAND / 'ext' / 'one.txt', one], CTRL)
            assert d['effect'] == COPY, d
            d = drop(a, bx, by, CTRL)
            assert d['last_n'] == 2, d
            jobs_done(a)
            assert (SAND / 'dst' / 'one.txt').exists()
            assert long_read(SAND / 'dst' / 'long-one.txt') == 'long one', 'The long-path file did not arrive in the target folder'
            assert long_read(one) == 'long one', 'A copy took the source away'
            d = enter(a, bx, by, [two])   # the same drive moves
            assert d['effect'] == MOVE, d
            drop(a, bx, by)
            jobs_done(a)
            assert long_read(SAND / 'dst' / 'long-two.txt') == 'long two', 'The moved long-path file did not arrive in the target folder'
            assert long_read(two) is None, 'The moved long-path file stayed at its source'
        finally:
            shutil.rmtree(LONG + str(top), ignore_errors=True)

    def long_parent(base):
        """A folder the window can open (its path under 240 characters) holding a folder, so that the folder inside has a path of over 260."""
        pad = 230 - len(str(base)) - 1
        parent = Path(base) / ('گ' * pad)
        child = parent / ('ر' * 120)
        os.makedirs(LONG + str(child), exist_ok=True)
        assert len(str(child)) > 330, len(str(child))
        return parent, child

    def in_long_target(a):
        # a folder item whose own path is over 260 characters takes a drop: a short file and a 590-character one copy, then a short one moves
        parent, child = long_parent(SAND / 'dst')
        try:
            far = long_file(long_dir(SAND / 'ext' / 'from'), 'far.txt', 'far')
            a.path(parent)
            a.until('the long folder did not open', lambda: a.snap()['path'] == str(parent), 6)
            x, y = item_xy(a, child.name)
            d = enter(a, x, y, [SAND / 'ext' / 'one.txt', far], CTRL)
            assert (d['kind'], d['dest']) == (2, str(child)), d
            a.snap('drop-over-long-folder')
            d = drop(a, x, y, CTRL)
            assert d['last_dest'] == str(child) and d['last_n'] == 2, d
            jobs_done(a)
            assert long_read(child / 'one.txt') == 'one.txt' and long_read(child / 'far.txt') == 'far', 'A file did not arrive in the long target folder'
            assert long_read(far) == 'far'
            enter(a, x, y, [SAND / 'ext' / 'two.txt'])
            drop(a, x, y)
            jobs_done(a)
            assert long_read(child / 'two.txt') == 'two.txt' and not (SAND / 'ext' / 'two.txt').exists(), 'A move into the long target folder did not arrive'
        finally:
            shutil.rmtree(LONG + str(parent), ignore_errors=True)
            shutil.rmtree(LONG + str(SAND / 'ext' / 'from'), ignore_errors=True)

    def in_long_other_drive(a):
        # long sources onto the other drive's row (copy, then Shift moves), and short ones onto a folder item with a long path on it
        try:
            src = long_dir(SAND / 'ext' / 'from')
            one = long_file(src, 'across-one.txt', 'across one')
            two = long_file(src, 'across-two.txt', 'across two')
            x, y = row_xy(a, drive)
            d = enter(a, x, y, [one])
            assert (d['kind'], d['effect']) == (3, COPY), d
            drop(a, x, y)
            jobs_done(a)
            assert long_read(Path(drive) / 'across-one.txt') == 'across one', 'The long-path file did not arrive on the other drive'
            enter(a, x, y, [two], SHIFT)
            drop(a, x, y, SHIFT)
            jobs_done(a)
            assert long_read(Path(drive) / 'across-two.txt') == 'across two' and long_read(two) is None, 'The moved long-path file did not arrive on the other drive'
            parent, child = long_parent(Path(drive) / 'in')
            a.path(parent)
            a.until('the long folder on the other drive did not open', lambda: a.snap()['path'].lower() == str(parent).lower(), 6)
            x, y = item_xy(a, child.name)
            d = enter(a, x, y, [SAND / 'ext' / 'one.txt', SAND / 'ext' / 'three.txt'])
            assert (d['kind'], d['effect']) == (2, COPY), d   # another drive copies
            drop(a, x, y)
            jobs_done(a)
            assert long_read(child / 'one.txt') == 'one.txt' and long_read(child / 'three.txt') == 'three.txt', 'A copy into the long folder on the other drive did not arrive'
            enter(a, x, y, [SAND / 'ext' / 'two.txt'], SHIFT)
            drop(a, x, y, SHIFT)
            jobs_done(a)
            assert long_read(child / 'two.txt') == 'two.txt' and not (SAND / 'ext' / 'two.txt').exists(), 'A move into the long folder on the other drive did not arrive'
        finally:
            shutil.rmtree(LONG + str(SAND / 'ext' / 'from'), ignore_errors=True)

    def in_many_rows(a):
        # 70 folders added to the sidebar: the last row takes a drop like the first (the list of drop rows has no fixed size)
        many = SAND / 'rows70'
        for i in range(70):
            (many / f'f{i:02}').mkdir(parents=True, exist_ok=True)
        for i in range(70):
            a.path(many / f'f{i:02}')
            a.key(ord('D'), (17,))
            time.sleep(.08)
        a.path(SAND / 'ext')
        a.until('70 added rows did not show', lambda: sum(1 for *_, t in a.snap()['dnd']['rows'] if t.lower().startswith(str(many).lower())) == 70, 10)
        for i in (0, 56, 63, 64, 69):
            target = many / f'f{i:02}'
            x, y = row_xy(a, target)
            d = enter(a, x, y, [SAND / 'ext' / 'one.txt'], CTRL)
            assert (d['kind'], d['dest'].lower()) == (3, str(target).lower()), (i, d)
            leave(a)
        x, y = row_xy(a, many / 'f69')
        enter(a, x, y, [SAND / 'ext' / 'one.txt'])
        a.snap('drop-over-70th-row')
        drop(a, x, y)
        jobs_done(a)
        assert (many / 'f69' / 'one.txt').exists() and not (SAND / 'ext' / 'one.txt').exists()

    def in_source_follows(a):
        # a file leaves the open folder from another process (what Explorer does after an outgoing move): the list drops it
        assert 'keep.txt' in a.snap()['names']
        shutil.move(str(SAND / 'dst' / 'keep.txt'), str(SAND / 'ext' / 'keep.txt'))
        a.until('The moved-out file stayed listed', lambda: 'keep.txt' not in a.snap()['names'], 15)

    subprocess.run(['subst', f'{letter}:', str(SAND / 'drive')], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        # subst must exist before the apps start so that the sidebar lists the drive
        check('1-out-file-grid', out_file, 'src')
        check('2-out-folder-persian', out_folder_persian, 'src')
        check('3-out-selection', out_selection, 'src')
        check('4-out-300', out_300, 'many')
        check('5-out-details', out_details, 'src', 'list')
        check('6-in-background', in_background)
        check('7-in-folder-item', in_folder_item)
        check('8-in-refused', in_refused)
        check('9-in-own-subfolder', in_own_subfolder, 'dst/sub/deep')
        check('10-in-conflict', in_conflict)
        check('11-in-drive-row', in_drive_row)
        check('12-in-trash', in_trash)
        os.environ['DFM_TEST_COPY_DELAY_MS'] = '100'
        check('13-in-while-copying', in_busy)
        os.environ.pop('DFM_TEST_COPY_DELAY_MS')
        check('14-in-edge-scroll', in_scroll, 'many')
        check('15-in-edge-scroll-details', in_scroll, 'many', 'list')
        check('16-in-small-window-dark', in_small_window, 'dst', 'grid', 'dark', ('--size', '360x280'))
        check('17-in-folder-dark-details', in_folder_item, 'dst', 'list', 'dark')
        check('18-in-leave', in_leave)
        check('19-in-details-background-after-folder', in_details_background, 'dst', 'list')
        check('20-in-effect-not-allowed', in_effect_choice)
        check('21-in-long-path', in_long_path)
        check('22-source-list-follows-move-out', in_source_follows)
        check('23-in-added-folder-row', in_added_folder_row, 'ext')
        (WORK / 'appdata' / 'dio-file-manager' / 'sidebar.txt').unlink(missing_ok=True)
        check('24-in-long-target-folder', in_long_target)
        check('25-in-long-path-other-drive', in_long_other_drive, 'ext')
        check('26-in-70-sidebar-rows', in_many_rows, 'ext')
        (WORK / 'appdata' / 'dio-file-manager' / 'sidebar.txt').unlink(missing_ok=True)   # the added rows were these checks' own
    finally:
        subprocess.run(['subst', f'{letter}:', '/d'], creationflags=subprocess.CREATE_NO_WINDOW)
    (WORK / 'dnd-results.json').write_text(json.dumps(results, indent=2, ensure_ascii=False), encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def center(r):
    x, y, w, h = r
    return x + w / 2, y + h / 2


def main():
    for folder in (WORK, WORK / 'tmp', WORK / 'appdata'):
        folder.mkdir(parents=True, exist_ok=True)
    os.environ.update(TEMP=str(WORK / 'tmp'), TMP=str(WORK / 'tmp'))
    if '--inside' in sys.argv:
        return inside()
    gaming = r'C:\Users\Omid\.agents\tools\gaming.py'
    code = subprocess.run([sys.executable, gaming, 'check'], creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if code == 3:
        subprocess.run([sys.executable, gaming, 'wait', '--cap', '60'], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    elif code:
        return code
    sys.path.insert(0, r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    with (WORK / 'dnd.log').open('wb') as out, (WORK / 'dnd.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=1500)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

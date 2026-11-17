"""Adding and removing sidebar rows, through the real window on the private desktop.
Right click a folder > Add to Sidebar, Ctrl+D, a row's right-click menu (Remove from Sidebar, Show Hidden Places), the settings
file (sidebar.txt in the check's own APPDATA) across a restart, a missing folder dimmed, the 360x280 window with the sidebar sheet.
Pictures of the whole window go to WORK/shots. Run from the repo: python tests/use/sidebar_smoke.py
(DFM_USE_WORK and DFM_EXE select isolated artifacts and the executable; USE_ONLY=<prefix> runs one check.)
"""
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
os.environ.setdefault('DFM_USE_WORK', str(ROOT / '.agent-work/use-checks' / Path(__file__).stem))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import input_smoke as base

WORK = base.WORK
SHOTS = Path(os.environ.get('DFM_SHOTS', WORK / 'shots'))
SETTINGS = WORK / 'appdata' / 'dio-file-manager' / 'sidebar.txt'
FA = 'گزارش نهایی سالانه شرکت و برنامه‌ی بلندمدت توسعه‌ی فروش'   # a long Persian name with spaces
BS = chr(92)
DEFAULT_KEYS = ['recent', 'starred', 'trash', 'other']


def shot(a, label):
    SHOTS.mkdir(parents=True, exist_ok=True)
    a.snap()
    shutil.copyfile(str(a.base) + '.png', SHOTS / (label + '.png'))


def rows(a):
    return {r['key']: r for r in a.snap()['sbrows']}


def centre(r):
    return r['r'][0] + r['r'][2] / 2, r['r'][1] + r['r'][3] / 2


def row_menu(a, key, item):
    """Right click a sidebar row and click an item of its menu ('remove' or 'show')."""
    r = rows(a)[key]
    a.rclick(*centre(r))
    a.until('the row menu did not open', lambda: a.snap()['ctx'], 3)
    m = a.snap()['sbmenu'][item]
    assert m[2] > 0, f'The menu has no {item} item: {a.snap()["sbmenu"]}'
    a.click(m[0] + m[2] / 2, m[1] + m[3] / 2)
    a.until('the row menu did not close', lambda: not a.snap()['ctx'], 3)
    time.sleep(.2)


def add_by_menu(a, index):
    """Right click the folder at `index` in the grid and pick Add to Sidebar (the menu is Open, Open With, Cut, Copy, Rename, Trash, Star, Add)."""
    a.cell(index)
    a.rclick(*a.cell_xy(index))
    a.until('the item menu did not open', lambda: a.snap()['ctx'], 3)
    for _ in range(9):   # one more since "Suggest names" sits before Move to Trash
        a.key(0x28)
    a.key(13)
    a.until('the item menu did not close', lambda: not a.snap()['ctx'], 3)
    time.sleep(.2)


def wheel_at(a, x, y):
    """The lParam of a wheel message: the pointer in screen pixels."""
    from ctypes import wintypes as W
    import ctypes as C
    dpi = base.U.GetDpiForWindow(a.hwnd) / 96
    pt = W.POINT(int(x * dpi), int(y * dpi))
    base.U.ClientToScreen.argtypes = [W.HWND, C.POINTER(W.POINT)]
    base.U.ClientToScreen(a.hwnd, C.byref(pt))
    return (pt.x & 0xFFFF) | ((pt.y & 0xFFFF) << 16)


def paths(a):
    return [k for k in rows(a) if k not in DEFAULT_KEYS and not k.startswith(('place', 'drive:'))]


def saved():
    return SETTINGS.read_text(encoding='utf-8').splitlines() if SETTINGS.exists() else []


def inside():
    results = []
    fx = WORK / 'fixture'
    shutil.rmtree(fx, ignore_errors=True)
    fx.mkdir(parents=True)
    for n in (FA, 'Alpha', 'Beta'):
        (fx / n).mkdir()
    (fx / 'file.txt').write_text('x', encoding='utf-8')
    many = WORK / 'many'
    shutil.rmtree(many, ignore_errors=True)
    many.mkdir()
    for i in range(30):
        (many / f'folder {i + 1:02d}').mkdir()
    # the grid shows folders first, by name: Alpha, Beta, then the Persian name
    ALPHA, BETA, PERSIAN = 0, 1, 2

    def check(name, fn, theme='light', size='1280x800', where=fx, fresh=True, seed=None):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        if fresh:
            SETTINGS.chmod(stat.S_IREAD | stat.S_IWRITE) if SETTINGS.exists() else None
            SETTINGS.unlink(missing_ok=True)
        if seed is not None:   # what sidebar.txt holds when the window starts
            SETTINGS.parent.mkdir(parents=True, exist_ok=True)
            SETTINGS.write_bytes(seed)
        a = None
        try:
            a = base.App(name, where, 'grid', theme, extra=('--size', size))
            fn(a, name)
            results.append(dict(check=name, result='passed'))
        except Exception as e:
            if a and a.proc.poll() is None:
                a.snap(name + '-failed')
            results.append(dict(check=name, result='FAILED', reason=str(e)))
        finally:
            if a:
                a.close()
        print(json.dumps(results[-1], ensure_ascii=False), flush=True)

    def base_rows(a):
        k = set(rows(a))
        assert {'recent', 'starred', 'place0', 'trash', 'other'} <= k, f'Default rows: {sorted(k)}'
        assert any(x.startswith('drive:') for x in k), f'No drive row: {sorted(k)}'

    def long_persian(a, name):
        base_rows(a)
        add_by_menu(a, PERSIAN)
        added = paths(a)
        assert added == [str(fx / FA)], f'Added rows: {added}'
        r = rows(a)[added[0]]
        assert r['r'][3] >= 30, f'Row geometry: {r}'
        order = list(rows(a))
        assert order.index(added[0]) == order.index('trash') + 1, f'The added row is not right after Trash: {order}'
        assert order.index(added[0]) < next(i for i, k in enumerate(order) if k.startswith('drive:')), 'The row is not before the drives'
        assert saved() == [f'add {fx / FA}'], f'Settings file: {saved()}'
        shot(a, name)
        a.click(*centre(r))   # the row opens the folder
        a.until('the row did not open the folder', lambda: a.snap()['path'] == str(fx / FA), 4)
        shot(a, name + '-opened')

    def twice(a, name):
        base_rows(a)
        add_by_menu(a, ALPHA)
        assert paths(a) == [str(fx / 'Alpha')]
        a.click(*centre(rows(a)[str(fx / 'Alpha')]))
        a.until('not in Alpha', lambda: a.snap()['path'] == str(fx / 'Alpha'), 4)
        a.key(ord('D'), (17,))   # already there: nothing is added
        time.sleep(.4)
        assert paths(a) == [str(fx / 'Alpha')], f'A duplicate row: {paths(a)}'
        # the item menu of an added folder no longer offers Add to Sidebar: its Enter on the 8th row is Copy Path instead of a new row
        a.path(fx)
        a.key(ord('D'), (17,))   # the open folder (Fixture) is added once
        time.sleep(.4)
        a.key(ord('D'), (17,))
        time.sleep(.4)
        assert sorted(paths(a)) == sorted([str(fx), str(fx / 'Alpha')]), f'Rows: {paths(a)}'
        assert len(saved()) == 2, f'Settings file: {saved()}'
        shot(a, name)

    def thirty(a, name):
        for i in range(30):
            a.path(many / f'folder {i + 1:02d}')
            a.key(ord('D'), (17,))
            time.sleep(.1)
        a.path(fx)
        a.until('30 rows', lambda: len(paths(a)) == 30, 5)
        assert paths(a) == [str(many / f'folder {i + 1:02d}') for i in range(30)], 'The rows are not in the order they were added'
        vh = 800
        o = rows(a)['other']['r']
        assert o[1] > vh, f'30 rows do not overflow the sidebar: Other Locations at {o}'   # the sidebar column scrolls; the harness's wheel messages do not move it on main either
        shot(a, name)
        assert len(saved()) == 30

    def remove_home(a, name):
        base_rows(a)
        row_menu(a, 'place0', 'remove')
        assert 'place0' not in rows(a), 'Home is still there'
        assert saved() == ['hide place0'], f'Settings: {saved()}'
        shot(a, name + '-removed')
        row_menu(a, 'recent', 'show')
        assert 'place0' in rows(a), 'Show Hidden Places did not bring Home back'
        assert saved() == [], f'Settings: {saved()}'
        shot(a, name)

    def remove_drive(a, name):
        base_rows(a)
        drive = next(k for k in rows(a) if k.startswith('drive:'))
        letters = [k for k in rows(a) if k.startswith('drive:')]
        row_menu(a, drive, 'remove')
        assert drive not in rows(a), 'The drive row is still there'
        assert f'hide {drive}' in saved()
        shot(a, name + '-removed')
        # remove every other row too: the menu on Other Locations still brings them back
        for k in [k for k in rows(a) if k not in ('other',)]:
            row_menu(a, k, 'remove')
        assert list(rows(a)) == ['other'], f'Rows left: {list(rows(a))}'
        shot(a, name + '-empty')
        row_menu(a, 'other', 'show')
        assert set(letters) <= set(rows(a)) and 'recent' in rows(a), 'Show Hidden Places did not bring everything back'
        assert saved() == []

    def restart(a, name):
        add_by_menu(a, BETA)
        row_menu(a, 'place1', 'remove')
        row_menu(a, 'recent', 'remove')
        want = sorted(saved())
        assert len(want) == 3, want
        a.close()
        b = base.App(name + '-again', fx, 'grid', 'light', extra=('--size', '1280x800'))
        try:
            k = rows(b)
            assert 'place1' not in k and 'recent' not in k, f'Removed rows came back: {sorted(k)}'
            assert str(fx / 'Beta') in k, 'The added folder did not come back'
            shot(b, name)
        finally:
            b.close()

    def recent_and_search(a, name):
        base_rows(a)
        a.click(*centre(rows(a)['recent']))
        a.until('not in Recent', lambda: a.snap()['loc'] == 1, 4)
        a.key(ord('D'), (17,))
        time.sleep(.4)
        assert paths(a) == [], f'Ctrl+D in Recent added {paths(a)}'
        a.click(*centre(rows(a)['place0']))
        a.path(fx)
        a.key(ord('F'), (17,))
        a.until('search field', lambda: a.snap()['typing'], 3)
        for ch in 'Alp':
            a.msg(0x102, ord(ch))
            time.sleep(.05)
        a.until('searching', lambda: a.snap()['searching'], 8)
        a.key(ord('D'), (17,))
        time.sleep(.4)
        assert paths(a) == [], f'Ctrl+D in a search added {paths(a)}'
        assert saved() == []
        # a search result that is a folder offers Add to Sidebar in its menu
        a.until('result', lambda: a.snap()['count'] >= 1, 8)
        add_by_menu_search(a)
        assert paths(a) == [str(fx / 'Alpha')], f'The search result was not added: {paths(a)}'
        shot(a, name)

    def add_by_menu_search(a):
        a.cell(0)
        a.rclick(*a.cell_xy(0))
        a.until('the item menu did not open', lambda: a.snap()['ctx'], 3)
        for _ in range(7):   # Open, Open With, Open Item Location, Copy, Move to Trash, Star, Add
            a.key(0x28)
        a.key(13)
        a.until('the item menu did not close', lambda: not a.snap()['ctx'], 3)
        time.sleep(.3)

    def gone(a, name):
        victim = WORK / 'victim folder'
        victim.mkdir(exist_ok=True)
        a.path(victim)
        a.key(ord('D'), (17,))
        a.path(fx)
        a.until('row', lambda: str(victim) in rows(a), 3)
        assert not rows(a)[str(victim)]['dim'], 'A present folder is dimmed'
        shot(a, name + '-present')
        shutil.rmtree(victim)
        a.until('the row of a deleted folder is not dimmed', lambda: (a.msg(0), rows(a)[str(victim)]['dim'])[1], 6)
        shot(a, name)
        a.click(*centre(rows(a)[str(victim)]))
        time.sleep(.5)
        assert a.snap()['path'] == str(fx), 'A click on a missing folder moved the window'   # the existing "That folder does not exist" toast
        shot(a, name + '-clicked')
        victim.mkdir()
        a.path(fx)
        a.until('the row is still dimmed after the folder came back', lambda: (a.msg(0), not rows(a)[str(victim)]['dim'])[1], 6)
        shutil.rmtree(victim)

    def small_sheet(a, name):
        assert a.snap()['narrow'], 'Not a narrow window'
        a.click(26, 23)
        a.until('the sidebar sheet did not open', lambda: a.snap()['overlay'], 3)
        time.sleep(.4)
        base_rows(a)
        shot(a, name + '-sheet')
        row_menu(a, 'place0', 'remove')
        assert a.snap()['overlay'], 'The sheet closed with the menu'
        assert 'place0' not in rows(a)
        shot(a, name + '-removed')
        row_menu(a, 'recent', 'show')
        assert 'place0' in rows(a)
        shot(a, name + '-back')

    def restore_duplicate(a, name):
        drive = next(k for k in rows(a) if k.startswith('drive:'))
        root = drive[len('drive:'):] + ':' + chr(92)
        row_menu(a, drive, 'remove')
        a.path(root)
        a.key(ord('D'), (17,))
        a.until('the folder was not added', lambda: paths(a), 3)
        row_menu(a, 'recent', 'show')
        k = rows(a)
        assert drive in k, 'The drive row did not come back'
        assert paths(a) == [], f'The restored drive shows twice: {paths(a)}'
        assert not [l for l in saved() if l.startswith('add ')], f'Settings file: {saved()}'

    def resized_menu(a, name):
        dpi = base.U.GetDpiForWindow(a.hwnd) / 96
        assert base.U.SetWindowPos(a.hwnd, None, 0, 0, int(600 * dpi), int(500 * dpi), 0x16), 'The window did not resize'
        a.until('the window is not narrow', lambda: a.snap()['narrow'], 3)
        a.click(26, 23)
        a.until('the sidebar sheet did not open', lambda: a.snap()['overlay'], 3)
        time.sleep(.4)
        a.rclick(*centre(rows(a)['recent']))
        a.until('the row menu did not open', lambda: a.snap()['ctx'], 3)
        a.key(0x28)
        a.key(13)
        time.sleep(.3)
        assert 'recent' not in rows(a), 'Down and Enter did not remove Recent'
        assert 'hide recent' in saved(), f'Settings file: {saved()}'

    def clipped_rows(a, name):
        a.click(26, 23)
        a.until('the sidebar sheet did not open', lambda: a.snap()['overlay'], 3)
        time.sleep(.4)
        a.rclick(60, 265)   # the sheet's bottom padding: the list is clipped here
        assert not a.snap()['ctx'], 'A right click outside the visible list opened a row menu'

    def overlong_line(a, name):
        """One 5,000-character line is one broken record: nothing of it is a row, and the lines after it are read."""
        assert paths(a) == [str(fx / 'Alpha'), str(fx / FA)], f'Rows: {paths(a)}'   # not the junk path, not Beta from inside the long line
        a.path(fx)
        a.key(ord('D'), (17,))
        a.until('the open folder was not added', lambda: len(paths(a)) == 3, 3)
        assert not any(len(l) > 400 for l in saved()), 'The long line was written back'
        assert str(fx / 'Beta') not in paths(a)

    spellings = [str(fx / FA), str(fx / FA) + BS, str(fx) + BS + '.' + BS + FA, str(fx) + BS + 'x' + BS + '..' + BS + FA,
                 str(fx).lower() + BS + 'alpha', str(fx) + BS + 'Alpha' + BS]

    def same_folder_spellings(a, name):
        """The long Persian folder and Alpha, each written several ways in the file, are two rows; adding them again in other spellings adds none."""
        assert paths(a) == [str(fx / FA), str(fx).lower() + BS + 'alpha'], f'Rows: {paths(a)}'
        for sp in (str(fx / FA) + BS, str(fx) + BS + '.' + BS + FA, str(fx) + BS + 'Alpha' + BS + '.'):
            a.path(sp)
            a.key(ord('D'), (17,))
            time.sleep(.4)
            assert len(paths(a)) == 2, f'{sp!r} gave a new row: {paths(a)}'
        assert saved() == [f'add {p}' for p in spellings], f'Settings file: {saved()}'   # nothing new was added, so nothing was written

    def retry_save(a, name):
        """A save that fails (sidebar.txt is read-only) shows the row for this run; the next Add saves it."""
        SETTINGS.chmod(stat.S_IREAD)
        try:
            a.path(fx / 'Alpha')
            a.key(ord('D'), (17,))
            a.until('the row did not show', lambda: paths(a) == [str(fx / 'Alpha')], 3)
            time.sleep(.3)
            assert saved() == [], f'The read-only file changed: {saved()}'
            a.key(ord('D'), (17,))   # still read-only: another try, another failure, still one row
            time.sleep(.4)
            assert paths(a) == [str(fx / 'Alpha')] and saved() == [], f'{paths(a)} {saved()}'
        finally:
            SETTINGS.chmod(stat.S_IREAD | stat.S_IWRITE)
        a.key(ord('D'), (17,))
        a.until('the next Add did not save', lambda: saved() == [f'add {fx / "Alpha"}'], 3)
        assert paths(a) == [str(fx / 'Alpha')], f'A second row: {paths(a)}'

    def sheet_spellings(a, name):
        a.click(26, 23)
        a.until('the sidebar sheet did not open', lambda: a.snap()['overlay'], 3)
        time.sleep(.4)
        assert paths(a) == [str(fx / FA), str(fx).lower() + BS + 'alpha'], f'Rows: {paths(a)}'
        shot(a, name)

    check('1-long-persian-name-light', long_persian, 'light')
    check('2-long-persian-name-dark', long_persian, 'dark')
    check('3-same-folder-twice', twice, 'light')
    check('4-thirty-folders', thirty, 'light', size='1280x800')
    check('5-remove-home-show-hidden', remove_home, 'light')
    check('6-remove-drive-and-all', remove_drive, 'dark')
    check('7-restart', restart, 'light')
    check('8-recent-and-search', recent_and_search, 'light')
    check('9-deleted-folder-dimmed', gone, 'light')
    check('10-small-window-sheet-light', small_sheet, 'light', size='360x280')
    check('11-small-window-sheet-dark', small_sheet, 'dark', size='360x280')
    check('12-wide-narrow-480', long_persian, 'dark', size='683x500')
    check('13-restore-no-duplicate', restore_duplicate, 'light')
    check('14-resized-sheet-menu-keys', resized_menu, 'light')
    check('15-clipped-row-no-menu', clipped_rows, 'light', size='360x280')
    def seed_of(*lines):
        return ''.join(f'add {l}\n' for l in lines).encode('utf-8')

    check('16-overlong-line', overlong_line, 'light', fresh=False,
          seed=seed_of('X' * 1043 + 'add ' + str(fx / 'Beta'), 'Y' * 5000, str(fx / 'Alpha'), str(fx / FA)))
    check('17-same-folder-spellings', same_folder_spellings, 'light', seed=seed_of(*spellings), fresh=False)
    check('18-failed-save-tried-again', retry_save, 'light', seed=b'')
    check('19-spellings-sheet-360-dark', sheet_spellings, 'dark', size='360x280', seed=seed_of(*spellings), fresh=False)
    (WORK / 'sidebar-results.json').write_text(json.dumps(results, indent=2, ensure_ascii=False), encoding='utf-8')
    return 1 if any(r['result'] != 'passed' for r in results) else 0


def main():
    for d in ('tmp', 'appdata'):
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
    with (WORK / 'sidebar.log').open('wb') as out, (WORK / 'sidebar.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=15 * 60)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

"""Rubber-band selection through real input on the private desktop."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

os.environ.setdefault('DFM_SELECT_ID', str(time.time_ns()))

ROOT = Path(__file__).resolve().parents[2]
os.environ.setdefault('DFM_USE_WORK', r'E:\w\dio-file-manager\.agent-work\rubberband-v1')
import input_smoke as base
from view_smoke import set_stop, cell_xy

WORK = base.WORK


def press(a, x, y, mods=()):
    a.keys(mods)
    a.msg(0x200, 0, a.point(x, y))
    time.sleep(.05)
    a.msg(0x201, 1, a.point(x, y))
    time.sleep(.06)


def move(a, x, y):
    a.msg(0x200, 1, a.point(x, y))
    time.sleep(.1)


def release(a, x, y):
    a.msg(0x202, 0, a.point(x, y))
    a.keys(())
    time.sleep(.1)
    assert not a.snap()['band'], 'Box remained after release'


def band_case(a, stop, picture=None, prefix='item-', finish=False, items=None):
    s = set_stop(a, stop)
    x, y, w, h = s['row']
    if stop == 0:
        start = (x+100, y+8*36+12)
        end = (x+240, y+5*36+18)
        indices = (5, 6, 7)
        keep = 0
    elif stop == 1:
        start = (x+20, y-8)
        end = (x+16+300, y+48)
        indices = (0, 1, s['column_rows'], s['column_rows']+1)
        keep = s['count']-1
    else:
        cw = (w-32)/s['cols']
        start = (x+8, max(50, y+6))
        end = (x+16+1.5*cw, y+1.5*h)
        indices = (0, 1, s['cols'], s['cols']+1)
        keep = s['count']-1
    expected = sorted((items[i] if items else f'{prefix}{i:03}.txt') for i in indices if i < s['count'])
    # Starting with everything selected proves a plain band replaces it.
    a.key(65, (17,))
    if finish:
        press(a, *start)
        move(a, *end)
        assert a.snap()['band'] and sorted(a.snap()['selection']) == expected
        if picture:
            shutil.copyfile(str(a.base)+'.png', WORK/'final'/picture)
        release(a, *end)
        assert sorted(a.snap()['selection']) == expected
        return expected
    for mods in ((), (17,), (16,)):
        if mods:
            a.key(36)  # reset scroll after selecting an offscreen item
            a.key(27)
            # Ctrl+click a visible item outside the rectangle.
            keep = 0 if stop == 0 else 2
            a.click(*cell_xy(a, keep))
            time.sleep(.5)
            assert a.snap()['selection'] == [f'item-{keep:03}.txt'], ('seed', stop, a.snap()['selection'])
        press(a, *start, mods)
        move(a, *end)
        s = a.snap()
        names = sorted(set(expected + ([f'item-{keep:03}.txt'] if mods else [])))
        assert s['band'] and sorted(s['selection']) == names, (stop, mods, names, s['selection'])
        if picture and not mods:
            shutil.copyfile(str(a.base)+'.png', WORK/'final'/picture)
        # Shrinking back must remove items no longer inside the rectangle.
        move(a, start[0]+1, start[1]+1)
        s = a.snap()
        assert s['selection'] == ([f'item-{keep:03}.txt'] if mods else []), ('shrink', stop, s['selection'])
        move(a, *end)
        release(a, *end)
        assert sorted(a.snap()['selection']) == names
    a.click(*start)
    assert a.snap()['selected'] == 0, 'Empty click did not clear selection'
    a.rclick(*start)
    assert a.snap()['ctx'], 'Background menu missing'
    a.key(27)
    px, py = cell_xy(a, 0)
    press(a, px, py)
    move(a, px+8, py+8)
    assert not a.snap()['band'], 'Item press started a box'
    release(a, px+8, py+8)


def scrolling(a, stop):
    s = set_stop(a, stop)
    x, y, w, h = s['row']
    start = (x+20, y-8) if stop == 1 else (x+8, y+6)
    end = (1070, y+80) if stop == 1 else (x+w/2, 714)
    press(a, *start)
    move(a, *end)
    a.until('Edge did not scroll', lambda: a.snap()['first'] > 1)
    s = a.snap()
    assert s['band'] and 'item-000.txt' in s['selection'], 'Anchor moved with scroll'
    threshold = s['column_rows']*4 if stop == 1 else int(674/h)*s['cols']
    assert any(int(n[5:8]) > threshold for n in s['selection']), 'No selection beyond first screen'
    release(a, *end)
    before = a.snap()['first']
    time.sleep(.4)
    assert a.snap()['first'] <= before+2, 'Autoscroll continued after release'


def inside():
    results = []

    def check(name, folder, fn, view='grid', theme='light'):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        a = None
        try:
            a = base.App(name, folder, view, 'dark' if name == '1-band-0' else theme)
            fn(a)
            results.append(dict(check=name, result='passed'))
        except Exception as e:
            if a and a.proc.poll() is None:
                a.snap(name+'-failed')
            results.append(dict(check=name, result='FAILED', reason=str(e)))
        finally:
            if a:
                a.close()
        print(json.dumps(results[-1]), flush=True)

    for stop in range(6):
        pic = 'grid-light.png' if stop == 3 else 'details-dark.png' if stop == 0 else None
        check(f'1-band-{stop}', WORK/('few' if stop == 0 else 'many'),
              lambda a, stop=stop, pic=pic: band_case(a, stop, pic))
    for stop in (3, 1):
        check(f'2-edge-scroll-{stop}', WORK/'many', lambda a, stop=stop: scrolling(a, stop))

    def search(a):
        a.key(70, (17,))
        for c in 'item-00':
            a.msg(0x102, ord(c))
            time.sleep(.01)
        a.until('Search not ready', lambda: a.snap()['searching'] and a.snap()['count'] == 8)
        for stop in range(6):
            band_case(a, stop)

    check('3-search-all-stops', WORK/'few', search)

    def tint(a, stop, theme):
        from PIL import Image
        s = set_stop(a, stop)
        x, y, w, h = s['row']
        px, py = (x+180, y+12) if stop <= 1 else (x+16+(w-32)/s['cols']/2, y+5)
        surface = (16, 19, 28) if theme == 'dark' else (247, 248, 252)
        # CUI mixes in OKLab, rather than interpolating sRGB channels.
        expected = (33, 41, 58) if theme == 'dark' else (219, 225, 251)

        def color(expected):
            a.snap()
            scale = base.U.GetDpiForWindow(a.hwnd)/96
            with Image.open(str(a.base)+'.png') as image:
                actual = image.convert('RGB').getpixel((round(px*scale), round(py*scale)))
            assert all(abs(c-e) <= 2 for c,e in zip(actual, expected)), (theme, stop, actual, expected)

        # Hover is neutral; the same pixel becomes accent tinted by all three selection paths.
        a.msg(0x200, 0, a.point(*cell_xy(a, 0))); time.sleep(.3)
        fg = (238, 241, 250) if theme == 'dark' else (14, 20, 36)
        color(tuple(round(b*.94+c*.06) for b,c in zip(surface, fg)))
        a.click(*cell_xy(a, 0)); color(expected)
        a.key(65, (17,)); color(expected)
        name = ('details', 'list', 'grid')[0 if stop == 0 else 1 if stop == 1 else 2]+'-'+theme
        a.msg(0x200, 0, a.point(700, 23)); time.sleep(.3)
        a.snap()
        shutil.copyfile(str(a.base)+'.png', WORK/'final'/(name+'-selected.png'))
        band_case(a, stop, name+'-band.png', finish=True)
        if stop == 0:  # Details selects rows 5..7 rather than the first row.
            py += 5*36
        color(expected)

    for theme in ('dark', 'light'):
        for stop in (0, 1, 3):
            check(f'5-tint-{theme}-{stop}', WORK/('few' if stop == 0 else 'many'),
                  lambda a, stop=stop, theme=theme: tint(a, stop, theme), theme=theme)

    def special(a, place, stop):
        prefix = f'dio-band-{os.environ["DFM_SELECT_ID"]}-{place}-{stop}-'
        files = [a.folder/f'{prefix}{i:03}.txt' for i in range(8)]
        saved = WORK/'appdata/dio-file-manager/starred.txt'
        saved.parent.mkdir(parents=True, exist_ok=True)
        saved.write_text('', encoding='utf-8')
        if place == 'recent':
            shortcuts = a.folder/'shortcuts'
            shortcuts.mkdir(exist_ok=True)
            # Real IShellLink files, parsed by recent_list, confined to this task.
            script = "$w=New-Object -ComObject WScript.Shell; Get-ChildItem -LiteralPath $env:DFM_RECENT_FILES -Filter '*.txt' | ForEach-Object { $s=$w.CreateShortcut((Join-Path $env:DFM_TEST_RECENT ($_.Name+'.lnk'))); $s.TargetPath=$_.FullName; $s.Save() }"
            os.environ['DFM_TEST_RECENT'] = str(shortcuts)
            subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-Command', script],
                           env=dict(os.environ, DFM_RECENT_FILES=str(a.folder)), check=True,
                           creationflags=subprocess.CREATE_NO_WINDOW)
        a.restart()
        if place == 'starred':
            a.key(65, (17,))
            a.rclick(*cell_xy(a, 0))
            for _ in range(7):
                a.key(40)
            a.key(13)
            a.until('Star did not save all fixtures', lambda: saved.exists() and len(saved.read_text(encoding='utf-8').splitlines()) == 8)
        elif place == 'trash':
            a.key(65, (17,))
            assert sorted(a.snap()['selection']) == sorted(p.name for p in files)
            a.key(46)
            a.until('Fixtures not moved to Trash', lambda: all(not p.exists() for p in files))
            a.until('Trash job did not finish', lambda: not a.snap()['act'] & 2)
        a.click(90, {'recent': 62, 'starred': 96, 'trash': 368}[place])
        a.until('Special place not loaded', lambda: a.snap()['loc'] == {'recent': 1, 'starred': 2, 'trash': 3}[place] and a.snap()['state'] == 1)
        # Filter the real bin to our fixtures before any selection or action.
        a.key(70, (17,))
        for c in prefix:
            a.msg(0x102, ord(c))
            time.sleep(.01)
        a.until('Special fixtures not listed', lambda: a.snap()['count'] == 8)
        set_stop(a, stop)  # Ctrl+A must reach the files, not the filter field.
        a.key(65, (17,))
        items = a.snap()['selection']
        selected = band_case(a, stop, prefix=prefix, finish=True, items=items)
        if place == 'recent':
            a.key(46)
            assert all(p.exists() for p in files), 'Recent Delete modified a file'
            assert not a.snap()['act'] and not a.snap()['views'], 'Recent Delete started an action'
        a.rclick(*cell_xy(a, items.index(selected[0])))
        assert sorted(a.snap()['selection']) == selected, 'Context click replaced band selection'
        # Recent: Star. Starred: Unstar. Trash: Restore.
        for _ in range(5 if place == 'recent' else 6 if place == 'starred' else 1):
            a.key(40)
        a.key(13)
        if place == 'trash':
            a.until('Restore did not match band', lambda: sorted(p.name for p in files if p.exists()) == selected)
            a.until('Restore still running', lambda: not a.snap()['views'])
            # Reloading a special place closes its local filter.
            a.key(70, (17,))
            for c in prefix:
                a.msg(0x102, ord(c))
                time.sleep(.01)
            a.until('Trash kept restored fixtures', lambda: a.snap()['count'] == 8-len(selected))
            set_stop(a, stop)  # move keyboard focus from the filter back to files
            # Restore the remainder too; never empty the owner's bin.
            a.key(65, (17,))
            a.rclick(*cell_xy(a, 0)); a.key(40); a.key(13)
            a.until('Remaining fixtures not restored', lambda: all(p.exists() for p in files))
        else:
            expected = selected if place == 'recent' else sorted(p.name for p in files if p.name not in selected)
            a.until('Star action did not match band', lambda: sorted(Path(p).name for p in saved.read_text(encoding='utf-8').splitlines()) == expected)
            assert all(p.exists() for p in files), 'Star action modified a file'
        assert all(p.read_text(encoding='utf-8') == 'band fixture' for p in files), 'Fixture content changed'
        os.environ.pop('DFM_TEST_RECENT', None)

    for place in ('recent', 'starred', 'trash'):
        for stop in (0, 1, 3):
            folder = WORK/f'{place}-{stop}'
            check(f'4-{place}-{stop}', folder, lambda a, place=place, stop=stop: special(a, place, stop))
    (WORK/'select-results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def main():
    for folder in ('tmp', 'final', 'appdata', 'few', 'many'):
        (WORK/folder).mkdir(parents=True, exist_ok=True)
    os.environ.update(TEMP=str(WORK/'tmp'), TMP=str(WORK/'tmp'))
    if '--inside' in sys.argv:
        return inside()
    for folder, count in (('few', 8), ('many', 200)):
        for i in range(count):
            (WORK/folder/f'item-{i:03}.txt').touch(exist_ok=True)
    for place in ('recent', 'starred', 'trash'):
        for stop in (0, 1, 3):
            folder = WORK/f'{place}-{stop}'
            folder.mkdir(exist_ok=True)
            for fixture in folder.glob('dio-band-*.txt'):
                fixture.unlink()
            for i in range(8):
                (folder/f'dio-band-{os.environ["DFM_SELECT_ID"]}-{place}-{stop}-{i:03}.txt').write_text('band fixture', encoding='utf-8')
    gaming = r'C:\Users\Omid\.agents\tools\gaming.py'
    rc = subprocess.run([sys.executable, gaming, 'check'], creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if rc == 3:
        subprocess.run([sys.executable, gaming, 'wait', '--cap', '60'], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    elif rc:
        return rc
    sys.path.insert(0, r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    with (WORK/'select.log').open('wb') as out, (WORK/'select.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=360)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

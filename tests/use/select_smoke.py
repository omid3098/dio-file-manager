"""Rubber-band selection through real input on the private desktop."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

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


def band_case(a, stop, picture=None):
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
    expected = sorted(f'item-{i:03}.txt' for i in indices if i < s['count'])
    # Starting with everything selected proves a plain band replaces it.
    a.key(65, (17,))
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

    def check(name, folder, fn, view='grid'):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        a = None
        try:
            a = base.App(name, folder, view, 'dark' if name == '1-band-0' else 'light')
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

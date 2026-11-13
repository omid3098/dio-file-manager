"""Six-stop views driven by real Win32 input on the shared private-desktop driver."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
os.environ.setdefault('DFM_USE_WORK', r'E:\w\dio-file-manager\.agent-work\viewzoom-v1')
sys.path.insert(0, str(Path(__file__).resolve().parent))
import input_smoke as base

WORK = base.WORK
FINAL = WORK / 'final'
NAMES = ('details', 'list', 'small', 'medium', 'large', 'extra-large')


def wheel(a, delta, ctrl=False):
    a.keys((17,) if ctrl else ())
    a.msg(0x200, 0, a.point(700, 400))
    elapsed = a.msg(0x20A, ((delta & 0xffff) << 16) | (8 if ctrl else 0))
    a.keys(())
    return elapsed


def set_stop(a, stop):
    a.click(1048, 23)
    s = a.snap()
    x, y, w, h = s['slider']
    assert w > 100 and h > 0, f'No slider track: {s["slider"]}'
    # Drag from the current thumb to the requested stop, including the end stops.
    x0 = x + 10 + (w - 20) * s['zoom'] / 5
    x1 = x + 10 + (w - 20) * stop / 5
    a.msg(0x200, 0, a.point(x0, y + h / 2))
    time.sleep(.04)
    a.msg(0x201, 1, a.point(x0, y + h / 2))
    time.sleep(.05)
    for n in range(1, 7):
        a.msg(0x200, 1, a.point(x0 + (x1-x0)*n/6, y+h/2))
        time.sleep(.025)
    a.msg(0x202, 0, a.point(x1, y+h/2))
    time.sleep(.2)
    s = a.snap()
    assert s['zoom'] == stop, f'Drag to {stop}: {s["zoom"]}'
    a.key(27)
    time.sleep(.2)
    return a.snap()


def cell_xy(a, index):
    s = a.snap()
    x, y, w, h = s['row']
    if s['zoom'] == 1:
        rows = s['column_rows']
        return x + 16 + (index//rows - s['first'])*240 + 60, y + (index % rows)*32 + 16
    if s['zoom'] == 0:
        return x + 80, y + (index-s['first'])*36 + 18
    return x + 16 + (index % s['cols']+.5)*(w-32)/s['cols'], y + (index//s['cols']-s['first'])*h + min(h/2, 40)


def picture(a, name):
    a.snap()
    shutil.copyfile(str(a.base)+'.png', FINAL / (name+'.png'))


def inside():
    results = []

    def check(name, folder, fn, view='grid'):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        a = None
        try:
            a = base.App(name, folder, view)
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

    def stops(a):
        for stop, name in enumerate(NAMES):
            s = set_stop(a, stop)
            assert s['view'] == (1 if stop == 0 else 0)
            if stop == 1:
                assert s['column_rows'] >= 10 and s['row'][3] == 32
            elif stop == 2:
                assert s['row'][3] == 48
            elif stop >= 3:
                assert s['row'][3] == (128, 160, 208)[stop-3]
            picture(a, 'light-'+name)
            x,y = cell_xy(a, 0)
            a.click(x,y)
            assert a.snap()['selection'] == ['A'], f'Click in {name}'
            time.sleep(.5)
            a.click(x,y); a.click(x,y)
            a.until(f'Open in {name}', lambda: a.snap()['path'] == str(WORK/'views'/'A'))
            a.key(37, (18,))
            assert a.wait_ready()['path'] == str(WORK/'views')
        a.click(1048, 23)
        picture(a, 'popover')
        a.key(27)

    def keys(a):
        set_stop(a, 0)
        for stop in range(1,6):
            a.key(187, (17,16))
            assert a.snap()['zoom'] == stop, f'Ctrl Plus to {stop}'
        for stop in range(4,-1,-1):
            a.key(189, (17,))
            assert a.snap()['zoom'] == stop, f'Ctrl Minus to {stop}'
        a.key(189, (17,)); assert a.snap()['zoom'] == 0
        for stop in range(1,6):
            wheel(a, 120, True); time.sleep(.18)
            assert a.snap()['zoom'] == stop, f'Ctrl wheel up to {stop}'
        wheel(a, 120, True); time.sleep(.18); assert a.snap()['zoom'] == 5
        for stop in range(4,-1,-1):
            wheel(a, -120, True); time.sleep(.18)
            assert a.snap()['zoom'] == stop, f'Ctrl wheel down to {stop}'
        a.key(48,(17,)); assert a.snap()['zoom'] == 3, 'Ctrl 0'
        a.key(107,(17,)); assert a.snap()['zoom'] == 4, 'Keypad plus'
        a.key(109,(17,)); assert a.snap()['zoom'] == 3, 'Keypad minus'
        set_stop(a, 4)
        a.click(1012,23); assert a.snap()['zoom'] == 0, 'Header Details'
        a.click(976,23); assert a.snap()['zoom'] == 4, 'Header last icon'
        a.key(50,(17,)); a.key(49,(17,))
        assert a.snap()['zoom'] == 4, 'Keyboard last icon'

    def persistence(a):
        for stop in range(6):
            set_stop(a, stop)
            a.view = 'saved'
            a.restart()
            assert a.snap()['zoom'] == stop, f'Restart lost {NAMES[stop]}'
        a.key(50,(17,)); a.restart(); a.key(49,(17,))
        assert a.snap()['zoom'] == 5, 'Last icon lost on restart'

    def columns(a):
        set_stop(a, 1)
        rows = a.snap()['column_rows']
        x,y=cell_xy(a, rows)
        a.click(x,y); assert a.snap()['cursor'] == rows, 'Second column starts after all rows'
        a.key(36); a.key(40); assert a.snap()['cursor'] == 1, 'List down'
        a.key(39); assert a.snap()['cursor'] == rows+1, 'List right'
        for _ in range(120):
            wheel(a,-120)
            time.sleep(.01)
        time.sleep(.4)
        s = a.snap()
        assert s['last'] == (s['count']+rows-1)//rows, 'Wheel did not reach last column'
        x,y=cell_xy(a,s['count']-1)
        assert 240 <= x < 1080 and 46 < y < 720, f'Last item offscreen: {x,y}'
        a.click(x,y)
        assert a.snap()['selection'] == ['file-00199.txt'], 'Last item not clickable'
        a.key(36); time.sleep(.3)
        x,y=cell_xy(a,0); a.click(x,y)
        assert a.snap()['cursor'] == 0, 'Home did not reveal first column'

    def performance(a, stop):
        set_stop(a, stop)
        assert a.wait_ready()['count'] == 20000
        before = a.snap()
        delays = []
        for _ in range(80):
            delays.append(wheel(a,-120))
            delays.append(a.msg(0))
            time.sleep(.012)
        time.sleep(.3)
        after = a.snap()
        assert after['first'] > before['first'], f'No scroll in {NAMES[stop]}'
        ms = max(delays)*1000
        print(json.dumps(dict(stop=NAMES[stop], max_message_ms=round(ms,3), samples=len(delays))),flush=True)
        assert ms <= 50, f'Message delayed {ms:.1f} ms in {NAMES[stop]}'

    def special(a):
        for stop in range(6):
            set_stop(a, stop)
            for y,loc in ((62,1),(96,2),(368,3)):
                a.click(90,y)
                assert a.wait_ready()['loc'] == loc
                assert a.snap()['zoom'] == stop, 'Special place changed stop'
            for _ in range(3):
                a.key(37,(18,))
            assert a.wait_ready()['path'] == str(WORK/'views'), 'History did not return to the fixture'
            a.key(70,(17,))
            for char in 'file-0000':
                a.msg(0x102,ord(char)); time.sleep(.01)
            a.until('Search not ready', lambda: a.snap()['searching'] and a.snap()['count'] == 10)
            assert a.snap()['zoom'] == stop
            picture(a, 'search-'+NAMES[stop])
            a.key(27)
            a.until('Search did not close', lambda: not a.snap()['searching'])

    check('1-stops-open', WORK/'views', stops)
    check('2-keys-wheel', WORK/'views', keys)
    check('3-restart', WORK/'views', persistence)
    check('4-column-order-scroll', WORK/'views', columns)
    check('5-special-search', WORK/'views', special)
    for stop in range(6):
        check('6-performance-'+NAMES[stop], WORK/'big', lambda a, stop=stop: performance(a,stop))
    (WORK/'view-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def main():
    for d in ('tmp','appdata','final','views/A','big'):
        (WORK/d).mkdir(parents=True, exist_ok=True)
    os.environ.update(TEMP=str(WORK/'tmp'), TMP=str(WORK/'tmp'))
    if '--inside' in sys.argv:
        return inside()
    gaming = r'C:\Users\Omid\.agents\tools\gaming.py'
    rc = subprocess.run([sys.executable,gaming,'check'],creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if rc == 3:
        subprocess.run([sys.executable,gaming,'wait','--cap','60'],check=True,creationflags=subprocess.CREATE_NO_WINDOW)
    elif rc:
        return rc
    for i in range(200):
        (WORK/'views'/f'file-{i:05}.txt').touch(exist_ok=True)
    for i in range(20000):
        (WORK/'big'/f'file-{i:05}.txt').touch(exist_ok=True)
    sys.path.insert(0,r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    with (WORK/'view.log').open('wb') as out, (WORK/'view.err').open('wb') as err:
        run = popen([sys.executable,str(Path(__file__).resolve()),'--inside'], ROOT,dict(os.environ),b'',out,err)
        try:
            return run.wait(timeout=360)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

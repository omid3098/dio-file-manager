"""Six view modes on a continuous size slider, driven by real Win32 input on the shared private-desktop driver."""
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
FINAL = WORK / 'final'
ICON_FILES = ('a.pdf', 'b.png', 'c.txt', 'd.zip', 'e.mp3', 'f.mp4', 'g.json', 'h.xlsx', 'i.exe', 'j.bin')
ICON_DX = 12   # left edge of a Details row's icon, from the row's left edge
NAMES = ('details', 'list', 'small', 'medium', 'large', 'extra-large')


def wheel(a, delta, ctrl=False):
    a.keys((17,) if ctrl else ())
    a.msg(0x200, 0, a.point(700, 400))
    elapsed = a.msg(0x20A, ((delta & 0xffff) << 16) | (8 if ctrl else 0))
    a.keys(())
    return elapsed


def set_stop(a, stop):
    a.click(*a.header_icon(0))
    s = a.snap()
    x, y, w, h = s['slider']
    assert w > 100 and h > 0, f'No slider track: {s["slider"]}'
    # Drag from the current thumb to the requested stop, including the end stops.
    x0 = x + 9 + (w - 18) * s['zv'] / 5
    x1 = x + 9 + (w - 18) * stop / 5
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
    assert s['zoom'] == stop and abs(s['zv'] - stop) < .1, f'Drag to {stop}: {s["zoom"]} at {s["zv"]}'
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


def expected_icon_and_row(zv):
    """Icon edge and row height for a slider value, worked out from the product's promise, not read from the app:
    icons are 32, 64, 96 and 144 px at Small to Extra Large, linear between; Small rows are the icon plus 16 px;
    tiles (Medium and up) are icon + 12 + 8 + two text lines of 17 + 10."""
    px = (32, 64, 96, 144)
    v = min(max(zv, 2), 5)
    i = min(int(v) - 2, 2)
    icon = px[i] + (px[i+1] - px[i]) * (v - (i + 2))
    return icon, icon + (16 if zv < 2.5 else 12 + 8 + 2 * 17 + 10)


def picture(a, name):
    a.snap()
    shutil.copyfile(str(a.base)+'.png', FINAL / (name+'.png'))


def _lum(c):
    def f(v):
        v /= 255
        return v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4
    return .2126 * f(c[0]) + .7152 * f(c[1]) + .0722 * f(c[2])


def contrast(a, b):
    x, y = sorted((_lum(a), _lum(b)), reverse=True)
    return (x + .05) / (y + .05)


def icon_contrast(image, box, bg):
    """(best contrast, pixels reaching 3:1) inside box=(x0, y0, x1, y1) against the row background."""
    px = image.convert('RGB').load()
    best, strong = 1.0, 0
    for y in range(box[1], box[3]):
        for x in range(box[0], box[2]):
            c = contrast(px[x, y], bg)
            best = max(best, c)
            strong += c >= 3.0
    return best, strong


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
            # The slider is continuous: a thumb near a stop gives that stop's row height within a few pixels.
            if stop == 1:
                assert s['column_rows'] >= 10 and s['row'][3] == 32
            elif stop >= 2:
                icon, row = expected_icon_and_row(s['zv'])
                assert abs(s['icon_px'] - icon) < .05, f'Icon {s["icon_px"]} px at {s["zv"]}, expected {icon:.2f} in {name}'
                assert abs(s['row'][3] - row) < .1, f'Row height {s["row"][3]} at {s["zv"]}, expected {row:.2f} in {name}'
            picture(a, 'light-'+name)
            x,y = cell_xy(a, 0)
            a.click(x,y)
            assert a.snap()['selection'] == ['A'], f'Click in {name}'
            time.sleep(.5)
            a.click(x,y); a.click(x,y)
            a.until(f'Open in {name}', lambda: a.snap()['path'] == str(WORK/'views'/'A'))
            a.key(37, (18,))
            assert a.wait_ready()['path'] == str(WORK/'views')
        a.click(*a.header_icon(0))
        picture(a, 'popover')
        a.key(27)

    def popup_tooltips(a):
        for name, x in (('view-options', a.header_icon(0)[0]), ('main-menu', 212)):
            a.msg(0x200, 0, a.point(x, 23))
            a.until(name + ': hover tooltip never appeared', lambda: a.snap()['tooltip'])
            a.click(x, 23)
            s = a.snap()
            assert s['popups'] and not s['tooltip'], f'{name}: tooltip survived click: {s}'
            time.sleep(.8)
            assert not a.snap()['tooltip'], name + ': tooltip returned over popup'
            a.msg(0x200, 0, a.point(700, 400))
            a.msg(0x200, 0, a.point(x, 23))
            time.sleep(.8)
            s = a.snap()
            assert s['popups'] and not s['tooltip'], name + ': rehover showed tooltip'
            picture(a, 'dark-' + name + '-hover')
            a.key(27)
            a.until(name + ': Esc left popup open', lambda: not a.snap()['popups'])
            a.until(name + ': tooltip did not return after closing', lambda: a.snap()['tooltip'])
            a.msg(0x200, 0, a.point(700, 400))
            time.sleep(.4)


    def icons(a):
        """File-type icons in Details and List keep 3:1 contrast against their row, in this check's theme."""
        from PIL import Image
        kinds = len(ICON_FILES)
        for stop in (0, 1):
            s = set_stop(a, stop)
            assert s['count'] == kinds + 1, s['names']
            picture(a, f'{a.theme}-icons-{NAMES[stop]}')
            image = Image.open(str(a.base) + '.png')
            image.load()
            for i, name in enumerate(s['names']):
                cx, cy = cell_xy(a, i)
                x0 = int(cx - 48) if stop == 1 else int(s['row'][0] + ICON_DX)
                box = (x0, int(cy - 8), x0 + 16, int(cy + 8))
                bg = image.convert('RGB').getpixel((box[0] - 3, int(cy)))
                best, strong = icon_contrast(image, box, bg)
                print(json.dumps({'icon': name, 'view': NAMES[stop], 'theme': a.theme, 'best': round(best, 2), 'pixels_3to1': strong}), flush=True)
                need = 60 if stop == 1 else 15   # the old pale List outline reached 3:1 on only 9 to 10 pixels
                assert best >= 3.0 and strong >= need, f'{name} icon in {NAMES[stop]} ({a.theme}): best {best:.2f}:1, {strong} pixels reach 3:1'
            image.close()

    def keys(a):
        set_stop(a, 0)
        for stop in range(1,6):
            a.key(187, (17,16))
            a.until(f'Ctrl Plus to {stop}', lambda: abs(a.snap()['zv'] - stop) < .001)
        for stop in range(4,-1,-1):
            a.key(189, (17,))
            a.until(f'Ctrl Minus to {stop}', lambda: abs(a.snap()['zv'] - stop) < .001)
        a.key(189, (17,)); a.until('Ctrl Minus at Details', lambda: abs(a.snap()['zv']) < .001)
        for stop in range(1,6):
            wheel(a, 120, True); time.sleep(.18)
            a.until(f'Ctrl wheel up to {stop}', lambda: abs(a.snap()['zv'] - stop) < .001)
        wheel(a, 120, True); time.sleep(.18); a.until('stop 5', lambda: abs(a.snap()['zv'] - 5) < .001)
        for stop in range(4,-1,-1):
            wheel(a, -120, True); time.sleep(.18)
            a.until(f'Ctrl wheel down to {stop}', lambda: abs(a.snap()['zv'] - stop) < .001)
        a.key(48,(17,)); a.until('Ctrl 0', lambda: abs(a.snap()['zv'] - 3) < .001)
        a.key(107,(17,)); a.until('Keypad plus', lambda: abs(a.snap()['zv'] - 4) < .001)
        a.key(109,(17,)); a.until('Keypad minus', lambda: abs(a.snap()['zv'] - 3) < .001)
        set_stop(a, 4)
        a.click(*a.header_icon(0)); a.until('View options open', lambda: a.snap()['popups'] >= 1); time.sleep(.3)
        x, y, w, h = a.snap()['view_mode']
        assert w > 40 and h > 20, f'No grid/list toggles in the view options menu: {(x, y, w, h)}'
        a.click(x + w * .75, y + h / 2); a.until('Menu list toggle: Details', lambda: abs(a.snap()['zv'] - 0) < .001)
        assert a.snap()['popups'] >= 1, 'The view options menu closed when the list toggle was clicked'
        a.click(x + w * .25, y + h / 2); a.until('Menu grid toggle: last icon', lambda: abs(a.snap()['zv'] - 4) < .12)  # returns to the value the thumb was left on
        assert a.snap()['popups'] >= 1, 'The view options menu closed when the grid toggle was clicked'
        a.key(27); time.sleep(.2)
        a.key(50,(17,)); a.key(49,(17,))
        assert a.snap()['zoom'] == 4, 'Keyboard last icon'

    def persistence(a):
        for stop in range(6):
            set_stop(a, stop)
            a.view = 'saved'
            a.restart()
            z = a.snap(); assert z['zoom'] == stop and abs(z['zv'] - stop) < .12, f'Restart lost {NAMES[stop]}: {z["zv"]}'
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
        # The bottom bar remains available before hover and supports direct dragging.
        a.msg(0x200, 0, a.point(1000, 714))
        a.msg(0x201, 1, a.point(1000, 714))
        time.sleep(.08)
        a.msg(0x200, 1, a.point(250, 714))
        time.sleep(.08)
        a.msg(0x202, 0, a.point(250, 714))
        a.until('Horizontal scrollbar did not reveal first column', lambda: a.snap()['first'] == 0)
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
            assert a.snap()['typing'], 'Reopened search did not take the keyboard'
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
    check('7-popup-tooltips', WORK/'views', popup_tooltips, theme='dark')
    check('8-icon-contrast-dark', WORK/'icons', icons, theme='dark')
    check('8-icon-contrast-light', WORK/'icons', icons, theme='light')
    for stop in range(6):
        check('6-performance-'+NAMES[stop], WORK/'big', lambda a, stop=stop: performance(a,stop))
    (WORK/'view-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def main():
    for d in ('tmp','appdata','final','views/A','big','icons/z-folder'):
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
    for name in ICON_FILES:
        (WORK/'icons'/name).touch(exist_ok=True)
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

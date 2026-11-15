"""Drive usage bars in the sidebar, through the real window on the private desktop.
A sandbox drive list (DFM_TEST_DRIVES, test mode only: "L type label|total bytes|free bytes") gives the drives sizes. Each drive with a size
shows a 3 px bar under its label: accent up to 90% used, danger above, a muted track for the rest; no bar without a size; the label
stays whole (or is cut with an ellipsis) and the hover tooltip reads "199.5 GB of 238.2 GB used (84%)". Pictures of the whole window go
to WORK/shots. Run from the repo: python tests/use/sidebar_bars_smoke.py
(DFM_USE_WORK and DFM_EXE select isolated artifacts and the executable.)
"""
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
SHOTS = Path(os.environ.get('DFM_SHOTS', WORK / 'shots'))
GB = 10 ** 9
LONG = 'A very long drive label that cannot fit'
# letter, type (2 removable, 3 fixed), label, total, free
DRIVES = [
    ('C', 3, 'Local Disk', 238200 * 10 ** 6, 38700 * 10 ** 6),      # 84% used
    ('D', 3, 'Almost Full', 512 * GB, 20 * GB),                      # 96%: danger
    ('E', 3, LONG, 500 * GB, 250 * GB),                              # long label
    ('F', 3, 'Empty Drive', 100 * GB, 100 * GB),                     # 0% used
    ('G', 2, 'USB Stick', 64 * GB, 44 * GB),                         # removable, eject button
    ('H', 3, 'No Size', 0, 0),                                       # unknown size: no bar
]
TIPS = {'C': '199.5 GB of 238.2 GB used (84%)', 'D': '492.0 GB of 512.0 GB used (96%)', 'E': '250.0 GB of 500.0 GB used (50%)',
        'F': '0.0 GB of 100.0 GB used (0%)', 'G': '20.0 GB of 64.0 GB used (31%)'}
USED = {'C': 199.5 / 238.2, 'D': 492 / 512, 'E': .5, 'F': 0.0, 'G': 20 / 64}


def shot(a, label):
    SHOTS.mkdir(parents=True, exist_ok=True)
    a.snap()
    shutil.copyfile(str(a.base) + '.png', SHOTS / (label + '.png'))


def image(a):
    from PIL import Image
    a.snap()
    return Image.open(str(a.base) + '.png').convert('RGB')


def far(c, d):
    return sum(abs(c[k] - d[k]) for k in range(3))


def find_bars(img, x_probe=60, y_from=380, x_right=240):
    """The 3 px bars of the sidebar, top to bottom: three rows that hold one colour along x_probe..x_probe+20 (a run no text makes),
    different from the pane and from the rows above and below. Each is (y, x_left, x_right, fill_end, fill_colour, track_colour);
    the track is the colour at the bar's right end, the fill the colour at its left end (the same when nothing is used: fill_end is None)."""
    pane = img.getpixel((3, y_from))
    bars, y = [], y_from
    while y < img.height - 4:
        c = img.getpixel((x_probe, y))
        if (all(far(img.getpixel((x, y + k)), c) < 8 for x in range(x_probe, x_probe + 20) for k in range(3)) and far(c, pane) > 60
                and far(img.getpixel((x_probe, y - 1)), c) > 30 and far(img.getpixel((x_probe, y + 3)), c) > 30):
            row = [img.getpixel((x, y + 1)) for x in range(x_right)]
            fill = c
            end = max(x for x in range(x_probe, x_right) if far(row[x], fill) < 12 and all(far(row[k], fill) < 12 for k in range(x_probe, x + 1)))
            track_colour = row[min(end + 3, x_right - 1)]
            rest = False
            if far(track_colour, fill) >= 12 and far(track_colour, img.getpixel((min(end + 3, x_right - 1), y - 1))) > 20:   # something is left after the fill: the track runs on while its colour holds
                rest = True
                xr = end + 3
                while xr + 1 < x_right and far(row[xr + 1], track_colour) < 12:
                    xr += 1
            else:
                track_colour = fill
                xr = end
            xl = min(x for x in range(40 if x_probe < 80 else 70, x_probe) if far(row[x], fill) < 12 or far(row[x], track_colour) < 12)
            bars.append((y, xl, xr, end if rest else None, fill, track_colour))
            y += 8
        else:
            y += 1
    return bars


def sandbox():
    f = WORK / 'drives.txt'
    f.write_text(''.join(f'{l} {t} {n}|{tot}|{free}\n' if tot else f'{l} {t} {n}\n' for l, t, n, tot, free in DRIVES), encoding='utf-8')
    return f


def hover(a, x, y, wait=.9):
    a.msg(0x200, 0, a.point(x, y))
    time.sleep(wait)


def check_bars(a, name):
    """Five bars (the drive without a size has none), 3 px high, from the label's left edge to the row's right padding (to the
    eject button on the removable drive), filled to the used share, danger red only above 90%; the picture goes to WORK/shots."""
    img = image(a)
    bars = find_bars(img)
    assert len(bars) == 5, f'{name}: expected 5 bars (C, D, E, F, G), found {len(bars)}: {[b[0] for b in bars]}'
    for letter, (y, xl, xr, fr, fill, track) in zip('CDEFG', bars):
        assert abs(xl - 44) <= 1, f'{name} {letter}: bar starts at x={xl}, the label at 44'
        want_r = 186 if letter == 'G' else 222
        assert abs(xr - want_r) <= 2, f'{name} {letter}: bar ends at x={xr}, expected {want_r}'
        share = 0.0 if fr is None else (fr - xl + 1) / (xr - xl + 1)
        assert abs(share - USED[letter]) <= .04, f'{name} {letter}: fill is {share:.2f} of the bar, used {USED[letter]:.2f}'
        red = fill[0] - fill[2] > 60 and fill[0] > fill[1] + 60
        assert red == (letter == 'D'), f'{name} {letter}: fill colour {fill}, danger only above 90%'
        if fr is not None:
            assert far(fill, track) > 60, f'{name} {letter}: the track {track} cannot be told from the fill {fill}'
    shot(a, name)
    return bars


def tip_of(a, x, y):
    hover(a, x, y, .1)
    a.until('tooltip never appeared', lambda: a.snap().get('tooltip'), 5)
    return a.snap()['tip']['text']


def inside():
    results = []
    shutil.rmtree(SHOTS, ignore_errors=True)
    drives = sandbox()
    os.environ['DFM_TEST_DRIVES'] = str(drives)
    os.environ['DFM_TEST_EJECT_LOG'] = str(WORK / 'eject.log')
    folder = WORK / 'folder'
    folder.mkdir(parents=True, exist_ok=True)

    def check(name, fn, theme='light', size='1280x800', where=folder):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        a = None
        try:
            a = base.App(name, where, 'grid', theme, extra=('--size', size))
            fn(a, name)
            results.append(dict(check=name, result='passed'))
        except Exception as e:
            if a and a.proc.poll() is None:
                try:
                    a.snap(name + '-failed')
                except Exception:
                    pass
            results.append(dict(check=name, result='FAILED', reason=str(e)))
        finally:
            if a and a.proc.poll() is None:
                a.proc.kill()

    def hovered(a, n):
        bars = check_bars(a, n + '-before')
        for letter, bar in zip('CDEFG', bars):
            tip = tip_of(a, 120, bar[0] - 12)
            assert tip == TIPS[letter], f'{n} {letter}: tooltip {tip!r}, expected {TIPS[letter]!r}'
        hover(a, 120, bars[0][0] - 12, .3)
        check_bars(a, n)   # the hovered row keeps its bar
        hover(a, 120, bars[4][0] + 34, 1.2)   # the drive without a size: no bar and no tooltip
        assert not a.snap().get('tooltip'), 'a tooltip on the drive without a size'

    def other(a, n):
        bars = find_bars(image(a))
        a.click(100, bars[4][0] + 68)   # Other Locations, the row after the drive without a size
        a.wait_ready()
        assert a.snap()['loc'] == 4, f'{n}: Other Locations did not open'
        check_bars(a, n)   # the sidebar keeps its bars beside the page's own meters

    def narrow(a, n):
        a.click(26, 23)   # the sidebar button opens the sheet
        time.sleep(.8)
        img = image(a)
        bars = find_bars(img, x_probe=78, y_from=300, x_right=300)   # the sheet's labels start at x=76; the sheet is the sidebar's 240 px, so the 25% fill of the USB drive ends near x=100
        assert len(bars) == 5, f'{n}: the sheet shows {len(bars)} bars: {[b[0] for b in bars]}'
        assert all(abs(b[1] - 76) <= 1 for b in bars), f'{n}: bars start at {[b[1] for b in bars]}, the labels at 76'
        assert [b[4][0] - b[4][2] > 60 for b in bars] == [False, True, False, False, False], f'{n}: only the drive above 90% is danger red'
        shot(a, n)

    for theme in ('light', 'dark'):
        check(f'plain-{theme}', lambda a, n: check_bars(a, n), theme)
        check(f'selected-{theme}', lambda a, n: check_bars(a, n), theme, where=Path('C:/'))
        check(f'hover-{theme}', hovered, theme)
        check(f'other-{theme}', other, theme)
    check('narrow-360', narrow, 'light', size='360x700')
    for r in results:
        print(r)
    return 0 if all(r['result'] == 'passed' for r in results) else 1


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
    with (WORK / 'run.log').open('wb') as out, (WORK / 'run.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=10 * 60)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

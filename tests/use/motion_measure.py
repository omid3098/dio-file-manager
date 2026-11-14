"""Latency and frame-time numbers for the slider drag and the resize glide, plus frame strips, with 1,000 files.

Run: python tests/use/motion_measure.py [--strips]   (DFM_EXE, DFM_USE_WORK, CUI_RENDERER as for the other use suites)
Writes <work>/measure.json. Message latency is the time SendMessage takes to be answered while the motion runs;
frame time is the app's own time to build one frame (the `ms` column of the .frames log).
With --strips it also saves frame pictures of a slowed resize glide and of a slider drag under <work>/strips/.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
os.environ.setdefault('DFM_USE_WORK', str(ROOT / '.agent-work/use-checks' / Path(__file__).stem))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import input_smoke as base
import motion_smoke as ms

WORK = base.WORK


def stats(values):
    values = sorted(values)
    return dict(n=len(values), max=round(values[-1], 3), p99=round(values[int(len(values) * .99)], 3), mean=round(sum(values) / len(values), 3))


def probe_while(a, seconds, action=None):
    """Send a no-op message every few ms for `seconds`; returns the answer times in ms."""
    delays = []
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        delays.append(a.msg(0) * 1000)
        time.sleep(.004)
    return delays


def inside():
    folder = WORK / 'files1000'
    out = {}
    app = base.App('measure', folder, 'grid', 'light')
    try:
        app.wait_ready()
        # Slider drag across 2.2 to 4.6: small steps, a message answered after each one.
        box = ms.slider(app)
        mark = ms.mark(app)
        delays = []
        y = box[1] + box[3] / 2
        x0, x1 = ms.thumb_x(box, app.snap()['zv']), ms.thumb_x(box, 4.6)
        delays.append(app.msg(0x200, 0, app.point(x0, y)) * 1000); time.sleep(.04)
        delays.append(app.msg(0x201, 1, app.point(x0, y)) * 1000); time.sleep(.05)
        for n in range(1, 61):
            delays.append(app.msg(0x200, 1, app.point(x0 + (x1 - x0) * n / 60, y)) * 1000)
            delays.append(app.msg(0) * 1000)
            time.sleep(.012)
        delays.append(app.msg(0x202, 0, app.point(x1, y)) * 1000)
        delays += probe_while(app, .4)
        rows = ms.frames(app)[mark:]
        out['slider_drag'] = dict(latency_ms=stats(delays), frame_build_ms=stats([r['ms'] for r in rows]), frames=len(rows),
                                  frame_gap_ms=stats([(b['t'] - a_['t']) * 1000 for a_, b in zip(rows, rows[1:]) if b['t'] - a_['t'] < .2]))
        app.key(27)
        # Resize glide: 1080 -> 900 -> 1080 -> 700, probing while each glide runs.
        s = app.snap()
        for width in (900, 1080, 700, 1080):
            mark = ms.mark(app)
            ms.resize(app, width)
            delays = probe_while(app, .5)
            rows = [r for r in ms.frames(app)[mark:]]
            glide = [r for r in rows if r['glide']]
            out.setdefault('resize_glide', []).append(dict(width=width, latency_ms=stats(delays), glide_frames=len(glide),
                                                           frame_build_ms=stats([r['ms'] for r in rows]),
                                                           glide_ms=round((glide[-1]['t'] - glide[0]['t']) * 1000) if glide else 0))
    finally:
        app.close()
    (WORK / 'measure.json').write_text(json.dumps(out, indent=2), encoding='utf-8')
    print(json.dumps(out), flush=True)

    if '--strips' in sys.argv:
        for theme in ('dark', 'light'):
            for name, drive, env in (('glide', resize_glide_strip, {'DFM_TEST_GLIDE_MS': '1500', 'DFM_TEST_STRIP_EVERY': '7'}),
                                     ('cross25', cross25_strip, {'DFM_TEST_STRIP_EVERY': '1'}),
                                     ('drag', drag_strip, {'DFM_TEST_STRIP_EVERY': '4'})):
                d = WORK / 'strips' / f'{name}-{theme}'
                d.mkdir(parents=True, exist_ok=True)
                for f in list(d.glob('strip-*.png')) + [d / 'arm']:
                    f.unlink(missing_ok=True)
                os.environ.update(env, DFM_TEST_STRIP=str(d))
                (WORK / 'appdata' / 'dio-file-manager' / 'view.txt').unlink(missing_ok=True)
                a = base.App(f'strip-{name}-{theme}', folder, 'grid', theme)
                try:
                    a.wait_ready()
                    drive(a, d)
                finally:
                    a.close()
                    for k in list(env) + ['DFM_TEST_STRIP']:
                        os.environ.pop(k, None)
                montage(d, WORK / 'strips' / f'{name}-{theme}.png')
    return 0


def montage(folder, out, columns=3, scale=.5):
    """The frames of one motion, whole window each, in reading order with their numbers."""
    from PIL import Image, ImageDraw
    files = sorted(folder.glob('strip-*.png'))
    assert files, f'No frames in {folder}'
    frames = [Image.open(f).convert('RGB') for f in files]
    frames = [f.resize((int(f.width * scale), int(f.height * scale)), Image.LANCZOS) for f in frames]
    w, h = frames[0].size
    rows = (len(frames) + columns - 1) // columns
    sheet = Image.new('RGB', (columns * w + (columns + 1) * 8, rows * (h + 22) + 8), (128, 128, 128))
    draw = ImageDraw.Draw(sheet)
    for n, f in enumerate(frames):
        x, y = 8 + (n % columns) * (w + 8), 8 + (n // columns) * (h + 22)
        draw.text((x, y + 4), f'frame {n + 1} of {len(frames)}', fill=(255, 255, 255))
        sheet.paste(f, (x, y + 20))
    sheet.save(out)
    print(json.dumps(dict(montage=str(out), frames=len(frames), size=sheet.size)), flush=True)


def arm(folder):
    (folder / 'arm').write_text('go')


def resize_glide_strip(a, folder):
    """1080 to 880 wide, slowed to 1.5 s: one picture in twelve."""
    time.sleep(.5)
    arm(folder)
    ms.resize(a, 880)
    time.sleep(2.2)


def cross25_strip(a, folder):
    """Slow drag from 2.2 across 2.5 to 3.0: compact rows turn into tiles with one fade, pictures from just before 2.5."""
    ms.set_value(a, 2.2)
    time.sleep(.5)
    box = ms.slider(a)
    y = box[1] + box[3] / 2
    x0 = ms.thumb_x(box, a.snap()['zv'])
    a.msg(0x200, 0, a.point(x0, y)); time.sleep(.04)
    a.msg(0x201, 1, a.point(x0, y)); time.sleep(.05)
    for v in (2.3, 2.4, 2.46):
        a.msg(0x200, 1, a.point(ms.thumb_x(box, v), y)); time.sleep(.05)
    arm(folder)
    for v in (2.5, 2.54, 2.58, 2.62, 2.7, 2.8, 2.9, 3.0):
        a.msg(0x200, 1, a.point(ms.thumb_x(box, v), y)); time.sleep(.04)
    a.msg(0x202, 0, a.point(ms.thumb_x(box, 3.0), y))
    time.sleep(.6)


def drag_strip(a, folder):
    box = ms.slider(a)
    arm(folder)
    ms.drag(a, box, a.snap()['zv'], 4.6, 40, .05)
    time.sleep(.5)


def main():
    (WORK / 'files1000').mkdir(parents=True, exist_ok=True)
    (WORK / 'tmp').mkdir(parents=True, exist_ok=True)
    (WORK / 'appdata').mkdir(parents=True, exist_ok=True)   # the app's settings folder lives under it
    os.environ.update(TEMP=str(WORK / 'tmp'), TMP=str(WORK / 'tmp'))
    if '--inside' in sys.argv:
        return inside()
    for i in range(1000):
        (WORK / 'files1000' / f'file-{i:05}.txt').touch(exist_ok=True)
    gaming = r'C:\Users\Omid\.agents\tools\gaming.py'
    rc = subprocess.run([sys.executable, gaming, 'check'], creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if rc == 3:
        subprocess.run([sys.executable, gaming, 'wait', '--cap', '60'], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    elif rc:
        return rc
    sys.path.insert(0, r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    with (WORK / 'measure.log').open('wb') as out, (WORK / 'measure.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'] + sys.argv[1:], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=300)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

"""The sidebar's width (drag its edge, double click resets, saved) and its slide away below 683 px, with real Win32 input on a private desktop.
Run from the repo: python tests/use/sidebar_layout_smoke.py   (DFM_EXE, DFM_USE_WORK as for the other use suites)
The app's own state dump gives the sidebar width drawn now (sidebar_px), the saved width (sidebar_w) and the frame log of the slide.
Pictures: <work>/pictures/ (whole windows, light and dark, and a frame strip of a slowed slide).
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
os.environ.setdefault('CUI_RENDERER', 'software')   # the hidden desktop's default path is emulated and slow; see motion_smoke.py
os.environ.setdefault('DFM_USE_WORK', str(ROOT / '.agent-work/use-checks' / Path(__file__).stem))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import input_smoke as base

WORK, U = base.WORK, base.U
SETTINGS = WORK / 'appdata' / 'dio-file-manager' / 'view.txt'
PICS = WORK / 'pictures'


def resize(a, width, height=720):
    import motion_smoke as ms
    ms.resize(a, width, height)


def width_of(a):
    s = a.snap()
    return round(s['content_w'] + s['sidebar_px'], 1)


def settled(a, seconds=3):
    """Wait until the sidebar is at rest (docked or gone) and return the state."""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        s = a.snap()
        if s['docked'] or s['narrow']:
            return s
        time.sleep(.03)
    raise AssertionError('The sidebar never came to rest')


def drag(a, x0, x1, y=300, steps=8):
    a.msg(0x200, 0, a.point(x0, y)); time.sleep(.05)
    a.msg(0x201, 1, a.point(x0, y)); time.sleep(.05)
    for i in range(1, steps + 1):
        a.msg(0x200, 1, a.point(x0 + (x1 - x0) * i / steps, y)); time.sleep(.03)
    a.msg(0x202, 0, a.point(x1, y)); time.sleep(.15)


def saved_width():
    """The third number of view.txt, once the writer has run (300 ms after the last change, at once on exit)."""
    for _ in range(40):
        if SETTINGS.exists():
            parts = SETTINGS.read_text(encoding='utf-8').split()
            if len(parts) >= 4:
                return int(parts[3])
        time.sleep(.05)
    return None


def inside():
    results = []
    folder = WORK / 'fixture'

    def check(name, fn, theme='light', env=None, size=None, width=None, view='grid'):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        if width:
            SETTINGS.write_text('view-v2 3.000 3.000 %d\n' % width, encoding='utf-8')
        app = None
        saved = {k: os.environ.get(k) for k in (env or {})}
        os.environ.update(env or {})
        try:
            app = base.App(name, folder, view, theme, extra=('--size', size) if size else ())
            fn(app)
            app.snap(name + '-passed')
            results.append({'check': name, 'result': 'passed'})
        except Exception as e:
            if app:
                app.snap(name + '-failed')
            results.append({'check': name, 'result': 'FAILED', 'reason': str(e)})
        finally:
            if app:
                app.close()
            for k, v in saved.items():
                os.environ.pop(k, None) if v is None else os.environ.__setitem__(k, v)
        print(json.dumps(results[-1]), flush=True)

    def width_drag(a):
        s = a.snap()
        assert s['sidebar_px'] == 240 and s['sidebar_w'] == 240, f'Start width {s["sidebar_px"]}/{s["sidebar_w"]}'
        drag(a, 240, 300)
        s = a.snap()
        assert 295 <= s['sidebar_px'] <= 305 and s['sidebar_w'] == s['sidebar_px'], f'Dragged to 300: {s["sidebar_px"]}/{s["sidebar_w"]}'
        drag(a, s['sidebar_px'], 40)
        assert a.snap()['sidebar_px'] == 180, 'Below 180 stops at 180'
        drag(a, 180, 900)
        s = a.snap()
        assert s['sidebar_px'] == 400 and s['sidebar_w'] == 400, 'Above 400 stops at 400'
        assert s['content_w'] == 1080 - 400, f'The content takes the rest: {s["content_w"]}'
        assert saved_width() == 400, 'The width is saved'

    def width_restart(a):
        s = a.snap()   # a new process: it read what the first one saved on exit
        assert s['sidebar_w'] == 400 and s['sidebar_px'] == 400, f'The saved width comes back after a restart: {s["sidebar_w"]}'

    def width_reset(a):
        assert a.snap()['sidebar_px'] == 400, 'Start at the saved 400'
        for _ in range(2):   # a double click on the line
            a.msg(0x200, 0, a.point(400, 300)); time.sleep(.03)
            a.msg(0x201, 1, a.point(400, 300)); time.sleep(.03)
            a.msg(0x202, 0, a.point(400, 300)); time.sleep(.05)
        time.sleep(.2)
        s = a.snap()
        assert s['sidebar_w'] == 240 and s['sidebar_px'] == 240, f'Double click resets: {s["sidebar_px"]}/{s["sidebar_w"]}'
        assert saved_width() == 240

    def content_minimum(a):
        s = a.snap()
        assert s['sidebar_w'] == 400, 'Saved 400'
        resize(a, 700)
        s = settled(a)
        assert s['content_w'] >= 440 and s['sidebar_w'] == 400 and s['sidebar_px'] < 400, f'Drawn narrower: {s["sidebar_px"]} of {s["sidebar_w"]}, content {s["content_w"]}'
        a.snap('minimum-700')
        resize(a, 1080)
        assert settled(a)['sidebar_px'] == 400, 'The saved width is back when there is room'
        assert saved_width() == 400, 'A narrow window never changes the saved width'

    def content_side_drag(a):
        """The content-side half of the strip in Details view resizes, it does not start the rubber band."""
        s = a.snap()
        assert s['sidebar_px'] == 240 and s['zoom'] >= 0, 'Start width 240'
        a.msg(0x200, 0, a.point(241, 400)); time.sleep(.05)
        a.msg(0x201, 1, a.point(241, 400)); time.sleep(.05)
        for i in range(1, 9):
            a.msg(0x200, 1, a.point(241 + 59 * i / 8, 400)); time.sleep(.03)
            assert not a.snap()['band'], 'The press on the strip started the band'
        a.msg(0x202, 0, a.point(300, 400)); time.sleep(.15)
        s = a.snap()
        assert 295 <= s['sidebar_px'] <= 305 and not s['band'], f'Dragged to 300 from the content side: {s["sidebar_px"]}, band {s["band"]}'

    def click_keeps_saved(a):
        """A click on a constrained divider (drawn narrower than saved) moves nothing and saves nothing."""
        resize(a, 700)
        s = settled(a)
        assert s['sidebar_px'] < 400 and s['sidebar_w'] == 400, f'Constrained: {s["sidebar_px"]} of {s["sidebar_w"]}'
        x = round(s['sidebar_px'])
        a.msg(0x200, 0, a.point(x, 300)); time.sleep(.05)
        a.msg(0x201, 1, a.point(x, 300)); time.sleep(.1)
        a.msg(0x202, 0, a.point(x, 300)); time.sleep(.2)
        assert a.snap()['sidebar_w'] == 400, 'The saved width stays 400 after a click'
        time.sleep(.6)   # the writer runs 300 ms after a change
        assert saved_width() == 400, 'A click on the divider never changes the saved width'

    def slide_log(a):
        """The sidebar's own frames (<state>.sidebar: time, width, sliding), one slide per run of sliding lines; each is closed by the
        first frame after it. Returns (slide time in ms, frames) for each slide so far. No picture is taken while it slides."""
        out, run = [], []
        for line in Path(str(a.base) + '.sidebar').read_text(encoding='utf-8').splitlines():
            t, w, sliding = line.split()
            if int(sliding):
                run.append(float(t))
            elif run:
                out.append((round((float(t) - run[0]) * 1000, 1), len(run)))
                run = []
        return out

    def slide_fast(a):
        """Default 150 ms: crossing 683 both ways, timed from the app's own frames (the first sliding frame to the first frame after it)
        while the test only waits: no picture, no state dump during the slide."""
        time.sleep(.4)
        resize(a, 600)
        time.sleep(.6)
        s = settled(a)
        assert s['narrow'] and s['sidebar_px'] == 0, 'Slid out'
        resize(a, 900)
        time.sleep(.6)
        s = settled(a)
        assert s['docked'] and s['sidebar_px'] == 240 and not s['narrow'], 'Slid back in'
        slides = slide_log(a)
        assert len(slides) == 2, f'Expected a slide out and a slide in: {slides}'
        for (ms_, n), way in zip(slides, ('out', 'in')):
            assert n >= 3, f'Slide {way} drew {n} frames'
            assert 150 <= ms_ <= 170, f'Slide {way} took {ms_:.0f} ms over {n} frames (150 ms planned, one frame late at most)'
        print(json.dumps({'slide_out_ms': slides[0][0], 'slide_in_ms': slides[1][0], 'frames': [slides[0][1], slides[1][1]]}), flush=True)

    def slide_slow(a):
        """Slowed to 1.5 s: monotonic, the narrow chrome only at the end, a crossing back in the middle turns round from where it is."""
        a.snap()
        time.sleep(.3)
        resize(a, 640)
        seen = []
        end = time.monotonic() + 3
        while time.monotonic() < end:
            s = a.snap()
            seen.append((s['sidebar_px'], s['narrow'], s['content_w']))
            if s['narrow']:
                break
        px = [p for p, _, _ in seen]
        assert all(b <= a_ + .01 for a_, b in zip(px, px[1:])), f'Not monotonic: {px}'
        assert len(px) > 4, f'Too few steps to see a slide: {px}'
        assert all(not n for _, n, _ in seen[:-1]) and seen[-1][1], 'The narrow chrome shows only when the slide is over'
        assert all(c >= 440 for _, _, c in seen), f'The content under 440: {seen}'
        # back out and in again: turn round part way
        resize(a, 900)
        time.sleep(.5)
        resize(a, 640)
        s0 = a.snap()['sidebar_px']
        time.sleep(.45)
        s1 = a.snap()
        assert 0 < s1['sidebar_px'] < s0 or s1['narrow'], f'Turned round from where it was: {s0} -> {s1["sidebar_px"]}'
        resize(a, 900)
        time.sleep(.2)
        s2 = a.snap()
        assert s2['sidebar_px'] >= s1['sidebar_px'] - 1 or s1['narrow'], f'Reversal jumped: {s1["sidebar_px"]} -> {s2["sidebar_px"]}'
        s3 = settled(a, 4)
        assert s3['docked'] and s3['sidebar_px'] == 240, 'Back at rest, docked'

    def snap_jump(a):
        """Win+Left: one big jump from 1080 to 540. The content never goes under 440 on the way."""
        a.snap()
        resize(a, 540)
        seen = []
        end = time.monotonic() + 3
        while time.monotonic() < end:
            s = a.snap()
            seen.append(s['content_w'])
            if s['narrow']:
                break
            time.sleep(.01)
        assert seen[-1] <= 540 and min(seen) >= 440, f'Snap jump: {seen}'

    def instant(a):
        a.snap()
        resize(a, 600)
        time.sleep(.08)
        s = a.snap()
        assert s['narrow'] and s['sidebar_px'] == 0, f'Animations off: out at once, got {s["sidebar_px"]}'
        resize(a, 900)
        time.sleep(.08)
        s = a.snap()
        assert s['docked'] and s['sidebar_px'] == 240, f'Animations off: in at once, got {s["sidebar_px"]}'

    def tiny(a):
        s = a.snap()
        assert s['narrow'] and s['sidebar_px'] == 0, 'Narrow at start'
        a.click(24, 23); time.sleep(.5)
        assert a.snap('tiny-sheet')['overlay'], 'The sheet opens in a 360 x 280 window'

    def pictures(a):
        PICS.mkdir(parents=True, exist_ok=True)
        a.snap('wide')
        import shutil
        shutil.copyfile(str(a.base) + '.png', PICS / f'wide-{a.theme}.png')
        resize(a, 600)
        settled(a)
        a.snap('narrow')
        shutil.copyfile(str(a.base) + '.png', PICS / f'narrow-{a.theme}.png')

    def strip(inward, theme):
        d = PICS / ('slide-%s-%s' % ('in' if inward else 'out', theme))
        d.mkdir(parents=True, exist_ok=True)
        for f in list(d.glob('strip-*.png')) + [d / 'arm']:
            f.unlink(missing_ok=True)
        def run(a):
            """The slide slowed to 1.5 s, one picture in twelve: out from 1080 to 600 wide, or in from 600 to 1080."""
            a.snap()
            time.sleep(.3)
            if inward:
                resize(a, 600); settled(a, 4); time.sleep(.3)
            (d / 'arm').write_text('go')
            resize(a, 1080 if inward else 600)
            time.sleep(2.4)
            import motion_measure as mm
            mm.montage(d, PICS / (d.name + '.png'))
        return run, {'DFM_TEST_GLIDE_MS': '1500', 'DFM_TEST_STRIP_EVERY': '12', 'DFM_TEST_STRIP': str(d)}

    SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    SETTINGS.unlink(missing_ok=True)
    check('1-drag-and-limits', width_drag)
    check('2-restart-keeps-width', width_restart)
    check('3-double-click-reset', width_reset)
    SETTINGS.write_text('view-v2 3.000 3.000 400\n', encoding='utf-8')
    check('4-content-minimum', content_minimum)
    check('4b-content-side-drag-details', content_side_drag, width=240, view='list')
    SETTINGS.write_text('view-v2 3.000 3.000 400\n', encoding='utf-8')
    check('4c-click-keeps-saved-width', click_keeps_saved)
    check('5-slide-150ms', slide_fast, width=240)
    check('6-slide-slow-and-reverse', slide_slow, env={'DFM_TEST_GLIDE_MS': '1500'}, width=240)
    check('7-snap-jump', snap_jump, width=240)
    check('8-animations-off', instant, env={'DFM_TEST_ANIMATIONS': 'off'}, width=240)
    check('9-tiny-window', tiny, size='360x280', width=400)
    check('10-pictures-light', pictures, 'light', width=240)
    check('11-pictures-dark', pictures, 'dark', width=240)
    for n, inward, theme in ((12, False, 'light'), (13, True, 'dark')):
        run, env = strip(inward, theme)
        check('%d-strip-%s' % (n, 'in' if inward else 'out'), run, theme, env=env, width=240)
    return 1 if any(r['result'] != 'passed' for r in results) else 0


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / 'tmp').mkdir(exist_ok=True)
    (WORK / 'appdata').mkdir(exist_ok=True)
    os.environ.update(TEMP=str(WORK / 'tmp'), TMP=str(WORK / 'tmp'))
    if '--inside' in sys.argv:
        return inside()
    gaming = r'C:\Users\Omid\.agents\tools\gaming.py'
    rc = subprocess.run([sys.executable, gaming, 'check'], creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if rc == 3:
        subprocess.run([sys.executable, gaming, 'wait', '--cap', '60'], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    elif rc:
        return rc
    (WORK / 'fixture').mkdir(exist_ok=True)
    for name in ('1.txt', '2.txt', '3.txt', 'A', 'B'):
        (WORK / 'fixture' / name).touch()
    sys.path.insert(0, r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    with (WORK / 'sidebar.log').open('wb') as out, (WORK / 'sidebar.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=600)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

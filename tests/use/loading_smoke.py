"""Loading placeholders and the unreadable-folder page, through the real window on the private desktop.
A slow listing (DFM_TEST_LIST_DELAY_MS, test mode only) shows placeholder shapes after 200 ms in the form of the view that will
replace them; a quick one never shows them; the frames stop once the items are there. "Unable to access" offers Try Again.
Pictures of the whole window go to WORK/shots. Run from the repo: python tests/use/loading_smoke.py
(DFM_USE_WORK and DFM_EXE select isolated artifacts and the executable.)
"""
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
SHOTS = Path(os.environ.get('DFM_SHOTS', WORK / 'shots'))
FA = 'گزارش_نهایی_سالانه_گزارش_نهایی_سالانه_گزارش_نهایی_سالانه'


class Slow(base.App):
    """An app that does not wait for its listing, so the placeholders can be looked at while it runs."""
    def wait_ready(self):
        return self.snap()


def frames(a):
    return [l for l in Path(str(a.base) + '.frames').read_text(encoding='utf-8').splitlines() if l and l[0] != 'D']


def shot(a, label):
    SHOTS.mkdir(parents=True, exist_ok=True)
    a.snap()
    shutil.copyfile(str(a.base) + '.png', SHOTS / (label + '.png'))


def ink_below(a, rows_from=.5, rows_to=.85):
    """Pixels in the lower middle of the content pane that are not its background: placeholders that really reach there (a frame
    counter alone cannot tell shapes that are drawn from shapes that are not)."""
    from PIL import Image
    img = Image.open(str(a.base) + '.png').convert('RGB')
    w, h = img.size
    left = 250 if w >= 900 else 8
    box = img.crop((left, int(h * rows_from), w - 8, int(h * rows_to)))
    bg = box.getpixel((box.width - 1, box.height - 1))
    return sum(1 for px in box.getdata() if px != bg)


def inside():
    results = []
    folder = WORK / 'folder'
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    for i in range(24):
        (folder / f'report {i + 1}.txt').write_text('x', encoding='utf-8')
    for n in ('Pictures', 'Projects', FA):
        (folder / n).mkdir()
    empty = WORK / 'empty'
    shutil.rmtree(empty, ignore_errors=True)
    empty.mkdir(parents=True)
    denied = Path(os.environ.get('DFM_DENIED', r'C:\System Volume Information'))

    def check(name, fn, theme='light', view='grid', size='1280x800', where=folder, env=None, extra=()):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        keep = {k: os.environ.get(k) for k in (env or {})}
        os.environ.update(env or {})
        a = None
        try:
            a = Slow(name, where, view, theme, extra=('--size', size, *extra))
            fn(a, name)
            results.append(dict(check=name, result='passed'))
        except Exception as e:
            if a and a.proc.poll() is None:
                a.snap(name + '-failed')
            results.append(dict(check=name, result='FAILED', reason=str(e)))
        finally:
            if a:
                a.close()
            for k, v in keep.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v
        print(json.dumps(results[-1]), flush=True)

    def slow(a, name):
        a.until('no placeholders after 200 ms of a running listing', lambda: a.snap()['skel'] > 0, 6)
        s = a.snap()
        assert s['state'] == 0 and s['count'] == 0, f'The listing was already done: {s["state"]} {s["count"]}'
        shot(a, name)
        ink = ink_below(a)
        assert ink > 800, f'The placeholders do not reach the lower half of the pane: {ink} pixels there'
        a.until('the listing never finished', lambda: a.snap()['state'] != 0, 12)
        s = a.snap()
        assert s['listed'] == 27, f'Items after loading: {s["listed"]}'
        time.sleep(.5)
        n = a.snap()['skel']
        time.sleep(.6)
        assert a.snap()['skel'] == n, 'Placeholders were still drawn after the items arrived'
        shot(a, name + '-done')
        idle(a)

    def idle(a):
        time.sleep(.5)
        n1 = len(frames(a))
        time.sleep(1.5)
        late = frames(a)[n1:]
        assert not late, f'{len(late)} frames drawn while idle after loading'

    def quick(a, name):
        s = a.snap()
        a.until('listing never finished', lambda: a.snap()['state'] != 0, 6)
        assert a.snap()['skel'] == 0, 'A quick listing drew placeholders'
        shot(a, name)

    def still(a, name):
        """Animations off: the shapes stay on screen and do not move, and no frame is drawn faster than the 20 ms wake."""
        a.until('no placeholders', lambda: a.snap()['skel'] > 0, 6)
        shot(a, name)
        a.until('the listing never finished', lambda: a.snap()['state'] != 0, 12)
        idle(a)

    def denied_page(a, name):
        a.until('no failed state', lambda: a.snap()['state'] == 2, 6)
        s = a.snap()
        shot(a, name)
        assert s['skel'] == 0, 'Placeholders drawn for a failed listing'

    def try_again(a, name):
        """The button under the reason asks for the listing again (what F5 does): the listing runs, placeholders show, and it fails again."""
        a.until('no failed state', lambda: a.snap()['state'] == 2, 8)
        shot(a, name)
        a.click(760, 519)   # the button's place at 1280x800 (see the picture)
        a.until('Try Again did not ask for the listing again', lambda: a.snap()['state'] == 0, 3)
        a.until('no placeholders in the second listing', lambda: a.snap()['skel'] > 0, 6)
        a.until('the second listing never failed', lambda: a.snap()['state'] == 2, 8)

    big = WORK / 'big'
    if not big.exists():
        big.mkdir(parents=True)
        for i in range(5200):
            (big / f'file {i:05d}.txt').write_bytes(b'x')
    persian = WORK / (FA + '_' + FA)   # a long Persian folder name that cannot be read (its own owner is denied List Folder)

    def listing_big(a, name):
        a.until('listing never finished', lambda: a.snap()['state'] != 0, 10)
        s = a.snap()
        assert s['listed'] == 5200, f'Items: {s["listed"]}'
        print(f'  5200 files: placeholder frames drawn {s["skel"]}', flush=True)
        shot(a, name)
        idle(a)

    def no_results(a, name):
        a.until('listing never finished', lambda: a.snap()['state'] != 0, 6)
        a.key(ord('F'), (17,))
        a.until('Search field did not take the keyboard', lambda: a.snap()['typing'])
        for ch in 'zzzzqq':
            a.msg(0x102, ord(ch))
            time.sleep(.05)
        a.until('search shows no results', lambda: a.snap()['searching'] and a.snap()['count'] == 0 and a.snap()['search'], 8)
        time.sleep(.8)
        assert a.snap()['skel'] == 0, 'Placeholders drawn for a search with no results'
        shot(a, name)

    d = {'DFM_TEST_LIST_DELAY_MS': '4000'}
    check('1-grid-light', slow, 'light', 'grid', env=d)
    check('2-grid-dark', slow, 'dark', 'grid', env=d)
    check('3-details-light', slow, 'light', 'list', env=d)
    check('4-details-dark', slow, 'dark', 'list', env=d)
    check('5-grid-dark-360', slow, 'dark', 'grid', '360x800', env=d)
    check('6-details-light-600', slow, 'light', 'list', '600x800', env=d)
    check('6a-list-columns-light', slow, 'light', 'grid', env=d, extra=('--zoom', '1'))
    check('6b-small-tiles-dark', slow, 'dark', 'grid', env=d, extra=('--zoom', '2'))
    check('7-grid-animations-off', still, 'light', 'grid', env=dict(d, DFM_TEST_ANIMATIONS='off'))
    check('8-details-animations-off', still, 'dark', 'list', env=dict(d, DFM_TEST_ANIMATIONS='off'))
    check('9-quick-no-placeholders', quick, 'light', 'grid')
    check('10-short-delay-no-placeholders', quick, 'light', 'list', env={'DFM_TEST_LIST_DELAY_MS': '100'})
    check('11-empty-folder', quick, 'light', 'grid', where=empty)
    if denied.exists():
        check('12-denied-light', denied_page, 'light', 'grid', where=denied)
        check('13-denied-dark-360', denied_page, 'dark', 'grid', '360x800', where=denied)
    if denied.exists():
        check('19-try-again', try_again, 'light', 'grid', where=denied, env={'DFM_TEST_LIST_DELAY_MS': '1500'})
    if denied.exists():
        check('20-denied-short-window', denied_page, 'dark', 'grid', '360x280', where=denied)
    spaced = WORK / 'Annual project planning documents and quarterly financial reports for the engineering and research teams before the final board meeting'
    spaced.mkdir(exist_ok=True)
    subprocess.run(['icacls', str(spaced), '/deny', f"{os.environ.get('USERNAME', 'Everyone')}:(RD)"], check=True, creationflags=subprocess.CREATE_NO_WINDOW, capture_output=True)
    try:
        check('21-denied-long-spaced-name-short-window', denied_page, 'dark', 'grid', '360x280', where=spaced)
    finally:
        subprocess.run(['icacls', str(spaced), '/remove:d', os.environ.get('USERNAME', 'Everyone')], creationflags=subprocess.CREATE_NO_WINDOW, capture_output=True)
    check('14-5200-files', listing_big, 'light', 'grid', where=big)
    check('15-5200-files-details', listing_big, 'dark', 'list', where=big)
    check('16-search-no-results', no_results, 'light', 'grid')
    persian.mkdir(exist_ok=True)
    user = os.environ.get('USERNAME', 'Everyone')
    subprocess.run(['icacls', str(persian), '/deny', f'{user}:(RD)'], check=True, creationflags=subprocess.CREATE_NO_WINDOW, capture_output=True)
    try:
        check('17-denied-persian-long-name', denied_page, 'light', 'grid', '700x800', where=persian)
        check('18-denied-persian-long-name-360', denied_page, 'dark', 'grid', '360x800', where=persian)
    finally:
        subprocess.run(['icacls', str(persian), '/remove:d', user], creationflags=subprocess.CREATE_NO_WINDOW, capture_output=True)
        subprocess.run(['icacls', str(persian), '/q'], creationflags=subprocess.CREATE_NO_WINDOW, capture_output=True)
    (WORK / 'loading-results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
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
    with (WORK / 'loading.log').open('wb') as out, (WORK / 'loading.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=10 * 60)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

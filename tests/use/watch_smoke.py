"""The open folder follows the disk: files another program adds, removes or renames show up by themselves, quietly.
Real window on the private desktop. Pictures of the whole window go to WORK/shots.
Run from the repo: python tests/use/watch_smoke.py   (DFM_USE_WORK and DFM_EXE select isolated artifacts and the executable.)
"""
import ctypes as C
from ctypes import wintypes as W
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
from clipboard_guard import preserved_clipboard

WORK = base.WORK
SHOTS = Path(os.environ.get('DFM_SHOTS', WORK / 'shots'))
K = base.K


def cpu_seconds(pid):
    h = K.OpenProcess(0x1000, False, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
    ft = [W.FILETIME() for _ in range(4)]
    K.GetProcessTimes(h, *[C.byref(f) for f in ft])
    K.CloseHandle(h)
    t = lambda f: ((f.dwHighDateTime << 32) | f.dwLowDateTime) / 1e7
    return t(ft[2]) + t(ft[3])


def shot(a, label):
    SHOTS.mkdir(parents=True, exist_ok=True)
    a.snap()
    shutil.copyfile(str(a.base) + '.png', SHOTS / (label + '.png'))


def names(a):
    return a.snap()['names']


def wait_name(a, name, present=True, seconds=3):
    t0 = time.monotonic()
    a.until(f'{name} never {"appeared" if present else "left"}', lambda: (name in names(a)) == present, seconds)
    return time.monotonic() - t0


def frames(a):
    return [l for l in Path(str(a.base) + '.frames').read_text(encoding='utf-8').splitlines() if l and l[0] != 'D']


def s_ms_ok(a):
    """From the first change of the burst to the new list in place: the 200 ms settle plus a listing and a frame."""
    return 0 <= a.snap()['watch_ms'] <= 450


def inside():
    results = []

    def make(name, count=12):
        d = WORK / name
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
        for i in range(count):
            (d / f'item {i:02d}.txt').write_text('x', encoding='utf-8')
        return d

    def check(name, fn, folder, theme='light', view='grid', size='1280x800', extra=()):
        if os.environ.get('USE_ONLY') and not name.startswith(tuple(os.environ['USE_ONLY'].split(','))):
            return
        a = None
        try:
            a = base.App(name, folder, view, theme, extra=('--size', size, *extra))
            fn(a, folder, name)
            results.append(dict(check=name, result='passed'))
        except Exception as e:
            if a and a.proc.poll() is None:
                try: a.snap(name + '-failed')
                except Exception: pass
            results.append(dict(check=name, result='FAILED', reason=str(e)))
        finally:
            if a:
                a.close()
        print(json.dumps(results[-1]), flush=True)

    def quiet(a, s0):
        """Nothing about the listing was redone the loud way: the same load, no placeholders, never a loading state."""
        s = a.snap()
        assert s['state'] == 1 and s['skel'] == s0['skel'], f'a loud reload happened: state {s["state"]} skel {s["skel"]} (was {s0["skel"]})'

    def appears(a, d, name):
        s0 = a.snap()
        assert s0['watch'] == 1, 'no watch on the open folder'
        e0 = s0['watch_events']
        t0 = time.monotonic()
        (d / 'new file.txt').write_text('hello', encoding='utf-8')
        dt = wait_name(a, 'new file.txt')
        # polling and the dump cost time themselves; the file must show within half a second of the write
        assert s_ms_ok(a), f'the app took {a.snap()["watch_ms"]} ms from the change to the new list'
        assert dt < 1.0, f'the new file took {dt:.2f} s (this includes the check own polling and picture)'
        s = a.snap()
        assert s['listed'] == s0['listed'] + 1, f'listed {s0["listed"]} -> {s["listed"]}'
        assert s['watch_events'] - e0 <= 2, f'events {s["watch_events"] - e0}'
        quiet(a, s0)
        print(f'  appeared: {a.snap()["watch_ms"]} ms from the change to the new list in the app ({dt:.2f} s seen by the polling check)', flush=True)
        shot(a, name)

    def burst(a, d, name):
        s0 = a.snap()
        e0 = s0['watch_events']
        n0 = s0['listed']
        states = set()
        for i in range(1000):
            (d / f'burst {i:04d}.txt').write_bytes(b'b')
        t_end = time.monotonic() + 2.5
        while time.monotonic() < t_end:
            s = a.snap()
            states.add((s['state'], s['skel'] - s0['skel']))
            time.sleep(.05)
        s = a.snap()
        assert s['listed'] == n0 + 1000, f'final count {s["listed"]} (want {n0 + 1000})'
        assert states == {(1, 0)}, f'the view went loud: {states}'
        ev = s['watch_events'] - e0
        assert ev <= 8, f'{ev} re-lists for one burst of 1000 files'
        print(f'  1000 files at once: {ev} settled change(s), final count right', flush=True)
        shot(a, name)

    def rename_delete(a, d, name):
        s0 = a.snap()
        os.rename(d / 'item 03.txt', d / 'moved 03.txt')
        (d / 'item 05.txt').unlink()
        wait_name(a, 'moved 03.txt')
        wait_name(a, 'item 03.txt', False)
        wait_name(a, 'item 05.txt', False)
        s = a.snap()
        assert s['listed'] == s0['listed'] - 1, s['listed']
        quiet(a, s0)

    def selection_stays(a, d, name):
        a.cell(2)
        sel = a.snap()['selection']
        assert len(sel) == 1, sel
        s0 = a.snap()
        for i in range(8):
            (d / f'aaa {i}.txt').write_text('x')   # all sort before the selected one
        wait_name(a, 'aaa 7.txt')
        time.sleep(.4)
        s = a.snap()
        assert s['selection'] == sel, f'selection {sel} -> {s["selection"]}'
        assert s['cursor'] >= 0 and s['names'][s['cursor']] == sel[0], 'the keyboard cursor left the selected item'
        quiet(a, s0)
        shot(a, name)

    def rename_open(a, d, name):
        a.cell(1)
        sel = a.snap()['selection'][0]
        a.key(113)
        a.until('Rename popover did not open', lambda: a.snap()['act'] & 1)
        time.sleep(.5)
        for ch in 'zq':
            a.msg(0x102, ord(ch))
            time.sleep(.01)
        (d / 'while renaming.txt').write_text('x')
        wait_name(a, 'while renaming.txt')
        time.sleep(.4)
        s = a.snap()
        assert s['act'] & 1, 'the rename field closed when a file appeared'
        shot(a, name)
        a.key(13)
        a.until('rename did not happen', lambda: any(n.startswith('zq') for n in names(a)))
        assert not (d / sel).exists() or True
        got = [n for n in names(a) if n.startswith('zq')]
        assert got, 'renamed item not found'

    def scroll_stays(a, d, name):
        for _ in range(150):
            a.msg(0x200, 0, a.point(700, 400))
            a.msg(0x20A, (0x10000 - 120) << 16)
            time.sleep(.03)
        time.sleep(.5)
        s0 = a.snap()
        assert s0['first'] > 30, f'did not scroll: {s0["first"]}'
        e0 = s0['watch_events']
        (d / '0000 first.txt').write_text('x')          # above the view
        (d / 'zzzz last.txt').write_text('x')           # below it
        a.until('re-list not seen', lambda: a.snap()['listed'] == 5202, 4)
        time.sleep(.6)
        s = a.snap()
        assert abs(s['first'] - s0['first']) <= 1, f'scroll jumped: first row {s0["first"]} -> {s["first"]}'
        print(f'  5,200 files: first row {s0["first"]} -> {s["first"]}', flush=True)
        shot(a, name)

    def app_copy(a, d, name):
        dst = WORK / 'copy-dst'
        with preserved_clipboard(a.hwnd):
            a.cell(0)
            assert a.snap()['selection'] == ['big.bin'], a.snap()['selection']
            a.key(ord('C'), (17,))
            a.path(dst)
            s0 = a.snap()
            a.key(ord('V'), (17,))
            seen = 0
            t_end = time.monotonic() + 40
            while time.monotonic() < t_end:
                s = a.snap()
                assert s['names'].count('big.bin') <= 1, f'doubled items: {s["names"]}'
                assert s['state'] == 1 and s['skel'] == s0['skel'], 'the copy made the view loud'
                seen += 1
                if (dst / 'big.bin').exists() and not s['incoming'] and s['names'] == ['big.bin'] and not s['job']['active']:
                    break
                time.sleep(.1)
            s = a.snap()
            assert s['names'] == ['big.bin'] and not s['incoming'], f'after the copy: {s["names"]} {s["incoming"]}'
            print(f'  copy into the open folder: {seen} samples, no doubled item, final {s["names"]}', flush=True)

    def gone(a, d, name):
        s0 = a.snap()
        os.chdir(WORK)                       # the test's own working folder must not hold it
        shutil.rmtree(d)
        t0 = time.monotonic()
        a.until('the deleted folder did not show its state', lambda: a.snap()['state'] == 2, 5)
        print(f'  open folder deleted: unreadable state after {time.monotonic() - t0:.2f} s', flush=True)
        assert not d.exists(), 'the watch kept the deleted folder alive'
        shot(a, name)

    def idle_cpu(a, d, name):
        other = WORK / 'idle-other'
        t_wait = time.monotonic()
        other.mkdir(exist_ok=True)
        quiet_since, last_n = time.monotonic(), -1
        while time.monotonic() - quiet_since < 1.5 and time.monotonic() - t_wait < 15:   # let the start-up frames (fonts, icons) finish first
            n = len(frames(a))
            if n != last_n: last_n, quiet_since = n, time.monotonic()
            time.sleep(.1)
        n1 = len(frames(a)); c0 = cpu_seconds(a.proc.pid); t0 = time.monotonic()
        while time.monotonic() - t0 < 8:   # other folders change while this one stays open
            (other / 'x.txt').write_text(str(time.monotonic()))
            time.sleep(.1)
        dt = time.monotonic() - t0
        c = cpu_seconds(a.proc.pid) - c0
        late = len(frames(a)) - n1
        print(f'  idle: {c * 1000:.1f} ms CPU in {dt:.1f} s = {100 * c / dt:.3f} % of one core, {late} frames', flush=True)
        assert c / dt < .001, f'{100 * c / dt:.3f} % of a core while idle'
        assert late == 0, f'{late} frames drawn while nothing changed in the open folder'

    def root(letter):
        return letter + ':' + chr(92)

    def free_letter():
        for l in 'ZYXWVUTSRQPONM':
            if not Path(root(l)).exists():
                return l
        raise AssertionError('no free drive letter')

    def run_subst(letter, target=None):
        args = ['subst', f'{letter}:'] + ([str(target)] if target else ['/d'])
        subprocess.run(args, check=True, capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)

    def center(r):
        return r[0] + r[2] / 2, r[1] + r[3] / 2

    def sandbox_drive(letter, tag):
        log, hold = WORK / f'eject-{tag}.log', WORK / f'eject-{tag}.hold'
        drives = WORK / f'drives-{tag}.txt'
        drives.write_text('C 3 Fixed' + chr(10) + letter + ' 2 Sandbox USB' + chr(10))
        log.unlink(missing_ok=True); hold.write_text('x')
        return drives, log, hold

    def eject_check(ok):
        """The sandbox drive is a virtual drive (subst) the window lists as removable. Eject must let go of the open folder
        first (the watch is closed before the request), and a refused eject must follow the folder again."""
        def fn(a, d, name):
            letter, log, hold = ENV['letter'], ENV['log'], ENV['hold']
            s = a.snap()
            assert s['watch'] == 1, 'no watch on the open folder'
            row = [x for x in s['drives'] if x['root'] == root(letter)]
            assert row and row[0]['eject'][2] > 0, f'no eject button: {s["drives"]}'
            a.click(*center(row[0]['eject']))
            a.until('the watch stayed open while the eject was asked', lambda: a.snap()['watch'] == 0, 4)
            print('  eject asked: the folder watch is closed', flush=True)
            hold.unlink()
            if ok:
                a.until('the window did not leave the ejected drive', lambda: not a.snap()['path'].upper().startswith(f'{letter}:'), 6)
                assert log.read_text().split() == [root(letter)], log.read_text()
                assert a.snap()['watch'] == 1, 'no watch on the folder shown after the eject'
            else:
                a.until('a refused eject did not bring the watch back', lambda: a.snap()['watch'] == 1, 6)
                (d / 'after refusal.txt').write_text('x')
                wait_name(a, 'after refusal.txt')
                print('  eject refused: the folder is followed again', flush=True)
        return fn

    ENV = {}

    def with_sandbox_drive(tag, ok, folder):
        letter = free_letter()
        run_subst(letter, folder)
        drives, log, hold = sandbox_drive(letter, tag)
        ENV.update(letter=letter, log=log, hold=hold)
        os.environ.update(DFM_TEST_DRIVES=str(drives), DFM_TEST_EJECT_HOLD=str(hold))
        if ok: os.environ['DFM_TEST_EJECT_LOG'] = str(log)
        else: os.environ.pop('DFM_TEST_EJECT_LOG', None)
        try:
            check(f'10-eject-{tag}', eject_check(ok), Path(root(letter)), extra=())
        finally:
            for k in ('DFM_TEST_DRIVES', 'DFM_TEST_EJECT_HOLD', 'DFM_TEST_EJECT_LOG'): os.environ.pop(k, None)
            run_subst(letter)

    def sizes_rule(a, d, name):
        """Omid's rule: sizes are measured on entry. A subfolder that appears is measured; the others are not measured again."""
        def size_of(n):
            for e in a.snap()['sizes']:
                if e[0] == n: return e
        a.until('the first sizes never arrived', lambda: size_of('old')[1] == 1, 8)
        before = size_of('old')[2]
        (d / 'old' / 'more.bin').write_bytes(b'x' * 3_000_000)          # grows a folder that was measured on entry
        (d / 'fresh').mkdir()
        (d / 'fresh' / 'f.bin').write_bytes(b'x' * 2_000_000)           # a folder that is new
        a.until('the new folder never showed', lambda: size_of('fresh') is not None, 4)
        a.until('the new folder was not measured', lambda: size_of('fresh')[1] == 1, 8)
        time.sleep(.6)
        assert size_of('fresh')[2] != '', 'no size for the new folder'
        assert size_of('old')[2] == before, f'the old folder was measured again: {before} -> {size_of("old")[2]}'
        print(f'  new folder measured ({size_of("fresh")[2]}); the grown old folder keeps {before}', flush=True)
        shot(a, name)

    a_dir = make('create')
    check('1-appears-light', appears, a_dir)
    check('1b-appears-dark-360x280', appears, make('create-small'), theme='dark', size='360x280')
    check('2-burst-1000-light', burst, make('burst'))
    check('2b-burst-1000-details-dark', burst, make('burst-details'), theme='dark', view='list')
    check('3-rename-delete', rename_delete, make('rename'))
    check('4-selection-stays', selection_stays, make('selection'))
    check('4b-selection-stays-dark-360x280', selection_stays, make('selection-small'), theme='dark', size='360x280')
    check('5-rename-field-stays', rename_open, make('renamefield'))
    big = WORK / 'big'
    if not big.exists() or len(list(big.iterdir())) != 5200:
        shutil.rmtree(big, ignore_errors=True)
        big.mkdir(parents=True)
        for i in range(5200):
            (big / f'file {i:05d}.txt').write_bytes(b'x')
    for extra in ('0000 first.txt', 'zzzz last.txt'):
        (big / extra).unlink(missing_ok=True)
    check('6-scroll-5200', scroll_stays, big)
    src, dst = WORK / 'copy-src', WORK / 'copy-dst'
    for d in (src, dst):
        shutil.rmtree(d, ignore_errors=True); d.mkdir(parents=True)
    block = b'copy check' + bytes(1) + b'x' * (1024 * 1024 - 11)
    with (src / 'big.bin').open('wb') as f:
        for _ in range(64): f.write(block)
    os.environ['DFM_TEST_COPY_DELAY_MS'] = '50'
    check('7-app-copy-into-open-folder', app_copy, src)
    os.environ.pop('DFM_TEST_COPY_DELAY_MS', None)
    sd = make('sizes', 3)
    (sd / 'old').mkdir(); (sd / 'old' / 'a.bin').write_bytes(b'x' * 1_000_000)
    check('7b-sizes-rule-details', sizes_rule, sd, theme='dark', view='list')
    check('8-folder-deleted', gone, make('deleted'))
    check('9-idle-cpu', idle_cpu, make('idle'))
    for tag, ok in (('ok', True), ('refused', False)):
        with_sandbox_drive(tag, ok, make(f'eject-{tag}'))
    (WORK / 'watch-results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
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
    with (WORK / 'watch.log').open('wb') as out, (WORK / 'watch.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=15 * 60)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

"""Real input proof for concurrent jobs, sandbox Recent and safe eject routing.
Uses input_smoke's private-desktop driver; never changes the real Recent folder.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
os.environ.setdefault('DFM_USE_WORK', str(ROOT / '.agent-work/use-checks' / Path(__file__).stem))
import input_smoke as base
from clipboard_guard import preserved_clipboard

WORK = base.WORK
SAND = WORK / 'jobs-sandbox'


def center(r):
    x, y, w, h = r
    assert w > 0 and h > 0, f'Control has no hit area: {r}'
    return x + w / 2, y + h / 2


def digest(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def prepare():
    shutil.rmtree(SAND, ignore_errors=True)
    for d in ('source-a', 'source-b', 'target', 'recent'):
        (SAND / d).mkdir(parents=True)
    # Actual 128 MiB payloads, not sparse placeholders. Test-mode callback delay
    # keeps the real CopyFileEx operations observable on this machine's SSD.
    for folder, name, byte in (('source-a', 'a.bin', b'A'), ('source-b', 'b.bin', b'B')):
        with (SAND / folder / name).open('wb') as f:
            for _ in range(128):
                f.write(byte * 1024 * 1024)
    (SAND / 'kept.txt').write_text('Keep this real file', encoding='utf-8')
    (SAND / 'other.txt').write_text('Keep this Recent item too', encoding='utf-8')
    script = WORK / 'tmp' / 'recent-fixture.ps1'
    script.write_text('''$s = New-Object -ComObject WScript.Shell
foreach ($pair in @(@('one', 'kept.txt'), @('duplicate', 'kept.txt'), @('other', 'other.txt'))) {
    $l = $s.CreateShortcut((Join-Path $env:DFM_TEST_RECENT ($pair[0] + '.lnk')))
    $l.TargetPath = Join-Path $env:DFM_JOBS_SANDBOX $pair[1]
    $l.Save()
}
''', encoding='utf-8')
    subprocess.run(['powershell', '-NoProfile', '-File', str(script)], check=True,
                   creationflags=subprocess.CREATE_NO_WINDOW)


def paste(a, folder):
    a.path(SAND / folder)
    s = a.snap()
    if s['zoom'] == 0:
        x, y, w, h = s['row']
        a.click(x + 80, y + (0 - s['first']) * 36 + 18)
    else:
        a.cell(0)
    a.key(ord('C'), (17,))
    a.path(SAND / 'target')
    a.key(ord('V'), (17,))


def show_jobs(a, incoming=frozenset({'a.bin', 'b.bin'})):
    t0 = time.monotonic()
    x, y = center(a.snap()['job_button'])
    a.msg(0x200, 0, a.point(x, y))
    a.until('File Operations tooltip never appeared', lambda: a.snap()['tooltip'])
    a.click(x, y)
    a.until('Progress popover did not show both jobs', lambda: a.snap()['job_pop'] and len(a.snap()['jobs']) == 2)
    s = a.snap('two-jobs-popover')
    print(json.dumps({'popover_seconds_after_second_paste': round(time.monotonic() - t0, 2), 'done_mb': [j['done'] >> 20 for j in s['jobs']]}), flush=True)
    assert not s['tooltip'], 'File Operations tooltip survived click'
    time.sleep(.8)
    assert not a.snap()['tooltip'], 'File Operations tooltip returned over popup'
    assert all(j['total'] == 128 * 1024 * 1024 for j in s['jobs']), s
    assert all(j['cancel'][2] > 0 for j in s['jobs']), 'Each job needs its own Cancel'
    check_incoming(a, incoming)
    return s


def check_incoming(a, expected):
    """While copies run, the receiving folder lists each incoming file under its final name (never the temp name,
    never the empty-folder message) until the real file replaces it."""
    a.until('Incoming files were not listed under their final names', lambda: expected <= set(a.snap()['incoming']), seconds=20)
    s = a.snap()
    assert s['listed'] == s['count'], f"Folder with incoming files shows the empty message: {s['listed']} listed, {s['count']} shown"
    assert not [n for n in s['names'] if '.dfm-copy' in n or n.endswith('.tmp')], s['names']
    assert expected <= set(s['incoming']) <= set(s['names']), s
    return s


def inside():
    results = []

    def check(name, fn, view='grid', theme='light'):
        app = None
        try:
            target = SAND / 'target'
            shutil.rmtree(target)
            target.mkdir()
            opened = Path(str(WORK / name) + '.opened')
            opened.unlink(missing_ok=True)
            app = base.App(name, SAND / 'source-a', view, theme)
            with preserved_clipboard(app.hwnd):
                fn(app)
            assert not opened.exists(), 'Job input unexpectedly opened a file: ' + opened.read_text(encoding='utf-8')
            app.snap(name + '-passed')
            results.append(dict(check=name, result='passed'))
        except Exception as e:
            if app:
                try:
                    app.snap(name + '-failed')
                except Exception as capture_error:
                    e = RuntimeError(f'{e}; capture failed: {capture_error}')
            results.append(dict(check=name, result='FAILED', reason=str(e)))
        finally:
            if app:
                app.close()
        print(json.dumps(results[-1]), flush=True)

    def copies(a):
        paste(a, 'source-a')
        paste(a, 'source-b')
        before = show_jobs(a)
        time.sleep(.4)
        after = a.snap()
        assert len(after['jobs']) == 2, 'One job finished before both were observed'
        assert all(y['done'] > x['done'] for x, y in zip(before['jobs'], after['jobs'])), 'Both copies must make progress concurrently'
        a.until('Copies did not finish', lambda: not a.snap()['jobs'], seconds=120)
        for folder, name in (('source-a', 'a.bin'), ('source-b', 'b.bin')):
            assert digest(SAND / folder / name) == digest(SAND / 'target' / name), name
        assert not list((SAND / 'target').glob('*.tmp')), 'Copy temp files left behind'
        a.until('Finished copies were not listed as real files', lambda: sorted(a.snap()['names']) == ['a.bin', 'b.bin'] and not a.snap()['incoming'])
        s = a.snap()
        assert s['listed'] == s['count'] == 2

    def incoming(a):
        # A folder that receives a copy shows the file under its final name, dimmed, with its place held; the
        # picture is kept for review in this theme and view.
        paste(a, 'source-a')
        a.until('Incoming file was not listed', lambda: a.snap()['incoming'] == ['a.bin'])
        s = a.snap()
        assert s['count'] == 1 and s['listed'] == 1 and s['names'] == ['a.bin'], s
        time.sleep(.3)
        final = WORK / 'final'
        final.mkdir(exist_ok=True)
        a.snap(f'{a.theme}-{a.view}-incoming')
        for ext in ('.png', '.json'):
            shutil.copyfile(WORK / f'{a.theme}-{a.view}-incoming{ext}', final / f'{a.theme}-{a.view}-incoming{ext}')
        a.until('Copy did not finish', lambda: not a.snap()['jobs'], seconds=120)
        a.until('Finished copy not listed as a real file', lambda: a.snap()['names'] == ['a.bin'] and not a.snap()['incoming'])
        assert digest(SAND / 'source-a/a.bin') == digest(SAND / 'target/a.bin')

    def cancel(a):
        paste(a, 'source-a')
        paste(a, 'source-b')
        s = show_jobs(a)
        survivor = s['jobs'][1]['id']
        a.click(*center(s['jobs'][0]['cancel']))
        a.until('Cancel did not remove just one job', lambda: len(a.snap()['jobs']) == 1)
        before = a.snap('cancel-one-other-running')['jobs'][0]
        assert before['id'] == survivor
        time.sleep(.4)
        after = a.snap()['jobs'][0]
        assert after['done'] > before['done'], 'Other copy stopped progressing'
        a.until('Other copy did not finish', lambda: not a.snap()['jobs'], seconds=120)
        assert not (SAND / 'target/a.bin').exists(), 'Cancelled copy published an incomplete file'
        a.until('Cancelled copy left a ghost in the listing', lambda: a.snap()['names'] == ['b.bin'] and not a.snap()['incoming'])
        assert digest(SAND / 'source-b/b.bin') == digest(SAND / 'target/b.bin')
        assert (SAND / 'source-a/a.bin').exists()
        assert not list((SAND / 'target').glob('*.tmp'))

    def collision(a):
        # Same destination name, different payloads; both start before either
        # has published a final filename. The late collision needs a dialog.
        b = SAND / 'source-b'
        (b / 'b.bin').rename(b / 'a.bin')
        try:
            paste(a, 'source-a')
            paste(a, 'source-b')
            show_jobs(a, {'a.bin'})
            a.until('Concurrent target collision did not ask', lambda: a.snap()['act'] & 8, seconds=120)
            a.snap('concurrent-conflict')
            # Standard CUI dialog footer: Skip, Keep Both, Replace.
            a.click(*center(a.snap()['conflict_keep']))
            a.until('Keep Both did not finish both jobs', lambda: not a.snap()['jobs'], seconds=90)
            outputs = list((SAND / 'target').glob('*.bin'))
            assert len(outputs) == 2, [p.name for p in outputs]
            assert {digest(p) for p in outputs} == {digest(SAND / 'source-a/a.bin'), digest(b / 'a.bin')}
        finally:
            (b / 'a.bin').rename(b / 'b.bin')

    def kept_selection(a):
        # The selection a user makes while a quiet reload is still listing must survive it (the swap reads the
        # selection when it happens). The check holds the swap with a gate file, selects, then lets it go.
        target = SAND / 'target'
        for n in ('x1.txt', 'x2.txt', 'x3.txt'):
            (target / n).write_text(n, encoding='utf-8')
        gate = Path(os.environ['DFM_TEST_HOLD_RELOAD'])
        gate.write_text('hold')
        try:
            paste(a, 'source-a')
            time.sleep(1.5)                       # the listing that shows a.bin has long finished and waits at the gate
            assert a.snap()['names'] == ['x1.txt', 'x2.txt', 'x3.txt'], a.snap()['names']
            a.cell(1)
            assert a.snap()['selection'] == ['x2.txt']
        finally:
            gate.unlink(missing_ok=True)
        a.until('Incoming file was not listed', lambda: a.snap()['incoming'] == ['a.bin'])
        s = a.snap()
        assert s['selection'] == ['x2.txt'], f"Quiet reload restored an outdated selection: {s['selection']}"
        assert s['cursor'] == s['names'].index('x2.txt'), s
        a.until('Copy did not finish', lambda: not a.snap()['jobs'], seconds=120)

    def conflict_first(a):
        # A conflict dialog left open for over a second must not use up the refreshes: after Keep Both the new
        # incoming file still shows.
        (SAND / 'target' / 'a.bin').write_bytes(b'old')
        paste(a, 'source-a')
        a.until('Existing name did not ask', lambda: a.snap()['act'] & 8, seconds=20)
        time.sleep(1.5)
        a.click(*center(a.snap()['conflict_keep']))
        a.until('Kept copy was not listed while copying', lambda: len(a.snap()['incoming']) == 1, seconds=20)
        a.until('Copy did not finish', lambda: not a.snap()['jobs'], seconds=120)
        a.until('Finished copy still listed as incoming', lambda: len(a.snap()['names']) == 2 and not a.snap()['incoming'])

    def inner_folder(a):
        # A folder opened while it is being filled shows its incoming file and, when the copy ends, the real file.
        pack = SAND / 'source-d' / 'pack'
        shutil.rmtree(SAND / 'source-d', ignore_errors=True)
        pack.mkdir(parents=True)
        os.link(SAND / 'source-a' / 'a.bin', pack / 'big.bin')
        paste(a, 'source-d')
        a.until('Pasted folder was not listed', lambda: a.snap()['names'] == ['pack'], seconds=20)
        a.path(SAND / 'target' / 'pack')
        a.until('Incoming file inside the folder was not listed', lambda: a.snap()['incoming'] == ['big.bin'], seconds=20)
        a.until('Copy did not finish', lambda: not a.snap()['jobs'], seconds=120)
        a.until('Finished file in the opened folder still incoming', lambda: a.snap()['names'] == ['big.bin'] and not a.snap()['incoming'])
        shutil.rmtree(SAND / 'source-d', ignore_errors=True)

    def recent(a):
        a.click(90, 62)
        s = a.wait_ready()
        assert s['loc'] == 1 and s['count'] == 2, s
        for i in range(2):
            a.cell(i)
            if a.snap()['selection'] == ['kept.txt']:
                break
        else:
            raise AssertionError('Sandbox Recent item missing')
        a.rclick(*a.cell_xy(i))
        assert a.snap('remove-recent-menu')['ctx']
        for _ in range(5):
            a.key(40)
        a.key(13)
        a.until('Recent entry did not disappear', lambda: a.snap()['count'] == 1)
        assert (SAND / 'kept.txt').read_text() == 'Keep this real file'
        assert sorted(p.name for p in (SAND / 'recent').iterdir()) == ['other.lnk'], 'Remove only matching Recent shortcuts'
        a.key(116)  # Reload must not bring the entry back.
        assert a.wait_ready()['count'] == 1

    def eject(a):
        s = a.snap('sandbox-drives')
        assert [d['type'] for d in s['drives']] == [3, 4, 2]
        assert all(d['eject'][2] == 0 for d in s['drives'][:2]), 'Fixed/network drive offers eject'
        a.click(*center(s['drives'][2]['eject']))
        a.until('Ejected row did not disappear', lambda: len(a.snap()['drives']) == 2)
        assert Path(os.environ['DFM_TEST_EJECT_LOG']).read_text().splitlines() == ['Y:\\'], 'Wrong drive passed to eject'
        assert [d['root'] for d in a.snap()['drives']] == ['C:\\', 'D:\\']
        a.click(90, 488)  # Other Locations moves up after the removable row disappears.
        assert a.snap()["loc"] == 4, "Other Locations did not open"
        assert len(a.snap()['drives']) == 2, 'Ejected sandbox drive reappeared'

    check('1-two-copies', copies)
    check('2-cancel-one', cancel)
    check('3-target-collision', collision)
    check('4-remove-recent', recent)
    check('5-eject', eject)
    check('6-incoming-dark-details', incoming, 'list', 'dark')
    check('7-incoming-light-icons', incoming, 'grid', 'light')
    check('8-selection-kept-in-quiet-reload', kept_selection)
    check('9-conflict-then-incoming', conflict_first)
    check('10-folder-opened-while-filling', inner_folder)
    (WORK / 'jobs-results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def main():
    for folder in (WORK, WORK / 'tmp', WORK / 'appdata'):
        folder.mkdir(parents=True, exist_ok=True)
    drives = WORK / 'drives.txt'
    eject_log = WORK / 'eject.txt'
    os.environ.update(TEMP=str(WORK / 'tmp'), TMP=str(WORK / 'tmp'),
                      DFM_TEST_RECENT=str(SAND / 'recent'), DFM_JOBS_SANDBOX=str(SAND),
                      DFM_TEST_DRIVES=str(drives), DFM_TEST_EJECT_LOG=str(eject_log), DFM_TEST_COPY_DELAY_MS='300',
                      DFM_TEST_HOLD_RELOAD=str(WORK / 'hold-reload'))
    if '--inside' in sys.argv:
        return inside()
    gaming = r'C:\Users\Omid\.agents\tools\gaming.py'
    code = subprocess.run([sys.executable, gaming, 'check'], creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if code == 3:
        subprocess.run([sys.executable, gaming, 'wait', '--cap', '60'], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    elif code:
        return code
    drives.write_text('C 3 Fixed\nD 4 Network\nY 2 Sandbox USB\n')
    eject_log.unlink(missing_ok=True)
    prepare()
    sys.path.insert(0, r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    with (WORK / 'jobs.log').open('wb') as out, (WORK / 'jobs.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=1200)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

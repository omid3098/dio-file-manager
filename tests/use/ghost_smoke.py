"""Agent names (Ghost names, the agent picker, F2 suggestion), on the shared private-desktop driver with a stand-in agent.
The stand-in (stub_agent.py) answers like a real agent; DFM_STUB_MODE picks ok / broken / slow / collide / illegal.
Fixture: 20 files with machine-made names (IMG_4821.jpg ..., Screenshot ... .png, a8f3c2e0.pdf, one .txt with Persian content),
existing.jpg and report-final.docx.
Run from the repo:  python tests/use/ghost_smoke.py   (DFM_EXE, DFM_USE_WORK as in input_smoke.py; USE_ONLY=<prefix> runs one check)"""
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
FIX = WORK / 'fix'
APPDATA = WORK / 'appdata'
STUB = Path(__file__).resolve().parent / 'stub_agent.py'
MODE = WORK / 'stub_mode.txt'
PID = WORK / 'stub_pid.txt'
VK_TAB, VK_ESC, VK_F2, VK_RET = 9, 27, 0x71, 13
EXTRA = ['existing.jpg', 'report-final.docx']


def make_fixture():
    shutil.rmtree(FIX, ignore_errors=True)
    FIX.mkdir(parents=True)
    names = ['IMG_%d.jpg' % (4821 + i) for i in range(7)] + ['Screenshot 2026-11-17 09301%d.png' % i for i in range(6)] \
        + ['a8f3c2e%d.pdf' % i for i in range(5)] + ['a1b2c3d4e5f6.txt', 'IMG_9999.heic']
    for n in names:
        (FIX / n).write_bytes(b'x' * 100)
    (FIX / 'a1b2c3d4e5f6.txt').write_text('گزارش فروش ماه گذشته. مجموع فروش ده درصد بیشتر شد.', encoding='utf-8')
    (FIX / 'existing.jpg').write_bytes(b'e')
    (FIX / 'report-final.docx').write_bytes(b'r')
    return names


def set_mode(mode):
    MODE.write_text(mode)
    PID.unlink(missing_ok=True)


def choose(agent='test'):
    (APPDATA / 'dio-file-manager').mkdir(parents=True, exist_ok=True)
    (APPDATA / 'dio-file-manager' / 'agent.txt').write_text('agent=%s\nmodel=\n' % agent)


def alive(pid):
    out = subprocess.run(['tasklist', '/FI', 'PID eq %d' % pid], capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW).stdout
    return str(pid) in out


class Gh(base.App):
    def __init__(self, name, view='grid', stub=True, timeout=None):
        if stub:
            os.environ['DFM_TEST_AGENT'] = 'python -I "%s"' % STUB
            os.environ['DFM_TEST_NO_AGENTS'] = '1'   # the real agents installed on this PC stay hidden; the stand-in stays visible
        else:
            os.environ.pop('DFM_TEST_AGENT', None)
            os.environ['DFM_TEST_NO_AGENTS'] = '1'
        os.environ['DFM_STUB_MODE'] = str(MODE)
        os.environ['DFM_STUB_PID'] = str(PID)
        if timeout:
            os.environ['DFM_TEST_AGENT_TIMEOUT_MS'] = str(timeout)
        else:
            os.environ.pop('DFM_TEST_AGENT_TIMEOUT_MS', None)
        super().__init__(name, FIX, view, 'light', extra=('--zoom', '3') if view == 'grid' else ('--zoom', '0'))
        self.wait_ready()

    def gh(self):
        return self.snap()['ghost']

    def select_all(self):
        self.key(ord('A'), (17,))

    def suggest(self):
        self.key(ord('R'), (17, 16))

    def pairs_ready(self, n=None, seconds=20):
        self.until('suggestions did not arrive', lambda: self.gh()['pairs'] and (n is None or len(self.gh()['pairs']) == n), seconds)
        return self.gh()['pairs']

    def names(self):
        return sorted(p.name for p in FIX.iterdir())

    def row_xy(self, name):
        s = self.snap()
        i = s['names'].index(name)
        x, y, w, h = s['row']
        if self.view == 'list':
            return x + 80, y + (i - s['first']) * h + h / 2
        return self.cell_xy(i)


def inside():
    results = []

    def check(name, fn, chosen=True, **kw):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        names = make_fixture()
        shutil.rmtree(APPDATA, ignore_errors=True)
        APPDATA.mkdir(parents=True)   # the real APPDATA always exists; the app makes its own folder in it
        set_mode('ok')
        if chosen:
            choose()   # before the app starts: it reads the choice once
        a = None
        try:
            a = Gh(name, **kw)
            fn(a, names)
            results.append(dict(check=name, result='passed'))
        except Exception as e:
            if a and a.proc.poll() is None:
                a.snap(name + '-failed')
            results.append(dict(check=name, result='FAILED', reason=str(e)))
        finally:
            if a:
                a.close()
        print(json.dumps(results[-1]), flush=True)

    def untouched(a, names):
        assert a.names() == sorted(names + EXTRA), 'files changed'

    def no_agent(a, names):
        a.select_all(); a.suggest()
        a.until('picker did not open', lambda: a.gh()['pop'] == 1)
        g = a.gh()
        assert g['found'] == [] and g['runs'] == 0 and g['pairs'] == [], g
        a.snap('1-no-agent-picker')
        a.key(VK_ESC)
        untouched(a, names)

    def first_use_picks_once(a, names):
        a.select_all(); a.suggest()
        a.until('picker did not open', lambda: a.gh()['pop'] == 1)
        g = a.gh()
        assert g['found'] == ['test'] and g['choice'] == '', g
        x, y, w, h = g['rects'][4]
        a.snap('2-first-use-picker')
        a.click(x + w / 2, y + h / 2)
        a.until('choice saved', lambda: a.gh()['choice'] == 'Test agent \u00b7 Default')
        assert 'agent=test' in (APPDATA / 'dio-file-manager' / 'agent.txt').read_text()
        pairs = a.pairs_ready(len(names) + 2)       # the run started by itself after the choice (all files, existing.jpg and report-final.docx included)
        a.snap('2-first-use-ghosts-grid')
        a.key(VK_ESC)
        a.select_all(); a.suggest()     # chosen once: no picker the second time, the run starts
        a.until('second run', lambda: a.gh()['runs'] == 2)
        assert a.gh()['pop'] == 0 and not a.gh()['picking']

    def accept_and_undo(a, names):
        a.select_all(); a.suggest()
        pairs = a.pairs_ready()
        assert all(p[1].rsplit('.', 1)[-1] == p[0].rsplit('.', 1)[-1] for p in pairs), 'extension changed'
        assert any('خلاصه' in p[1] for p in pairs if p[0].endswith('.txt')), 'Persian content did not get a Persian name'
        a.key(VK_TAB)
        a.until('names on disk', lambda: any(n.startswith('Sample') for n in a.names()))
        a.until('undo toast', lambda: a.gh()['undo'])
        assert not any(n.startswith('IMG_') for n in a.names()), a.names()
        a.snap('3-accepted')
        a.key(ord('Z'), (17,))
        a.until('undone', lambda: a.names() == sorted(names + EXTRA), 10)

    def esc_drops(a, names):
        a.select_all(); a.suggest(); a.pairs_ready()
        a.key(VK_ESC)
        a.until('dropped', lambda: a.gh()['pairs'] == [])
        untouched(a, names)

    def collisions(a, names):
        set_mode('collide')
        a.select_all(); a.suggest()
        new = [p[1] for p in a.pairs_ready()]
        assert len(set(n.lower() for n in new)) == len(new), 'two suggestions share a name: %s' % new
        assert 'existing.jpg' not in [n.lower() for n in new], 'a suggestion would overwrite existing.jpg'
        assert any(n.startswith('existing 2') for n in new), new

    def illegal(a, names):
        set_mode('illegal')
        a.select_all(); a.suggest()
        for o, n in a.pairs_ready():
            assert not any(c in n for c in '<>:"/\\|?*'), (o, n)
            assert n.rsplit('.', 1)[0].upper() != 'CON' and not n.rsplit('.', 1)[0].endswith(' '), n
        a.snap('6-illegal-names')

    def broken(a, names):
        set_mode('broken')
        a.select_all(); a.suggest()
        a.until('answer judged', lambda: a.gh()['result'] == 'broken')
        assert a.gh()['pairs'] == []
        untouched(a, names)
        a.snap('7-broken-answer')

    def timeout(a, names):
        set_mode('slow')
        a.select_all(); a.suggest()
        a.until('running', lambda: a.gh()['running'])
        a.until('timed out', lambda: a.gh()['result'] == 'timeout', 15)
        time.sleep(1)
        assert not alive(int(PID.read_text())), 'the agent is still running'

    def cancel(a, names):
        set_mode('slow')
        a.select_all(); a.suggest()
        a.until('running', lambda: a.gh()['running'] and PID.exists())
        a.snap('9-working-bar')
        x, y, w, h = a.gh()['rects'][3]
        assert w > 0, 'the Cancel button is not on screen'
        a.click(x + w / 2, y + h / 2)
        a.until('cancelled', lambda: a.gh()['result'] == 'cancelled' and not a.gh()['running'])
        time.sleep(1)
        assert not alive(int(PID.read_text())), 'the agent is still running'
        assert a.gh()['pairs'] == []

    def f2_machine_name(a, names):
        a.click(*a.row_xy('IMG_4821.jpg'))
        a.key(VK_F2)
        a.until('suggestion in the rename box', lambda: a.gh()['f2']['text'])
        a.snap('10-f2-suggestion')
        s = a.gh()['f2']['text']
        a.key(VK_TAB)
        a.key(VK_RET)
        a.until('renamed', lambda: s in a.names() and 'IMG_4821.jpg' not in a.names())

    def f2_normal_name(a, names):
        a.click(*a.row_xy('report-final.docx'))
        a.key(VK_F2)
        time.sleep(1.0)
        g = a.gh()
        assert g['runs'] == 0 and g['f2']['text'] == '' and not g['f2']['busy'], g
        a.snap('11-f2-normal')

    def f2_never_picker(a, names):
        a.click(*a.row_xy('IMG_4821.jpg'))
        a.key(VK_F2)
        time.sleep(.6)
        g = a.gh()
        assert not g['picking'] and g['pop'] == 0 and g['runs'] == 0, g

    def list_view(a, names):
        a.select_all(); a.suggest(); a.pairs_ready()
        a.snap('13-ghosts-list')

    def accept_by_click(a, names):
        a.select_all(); a.suggest(); a.pairs_ready()
        x, y, w, h = a.gh()['rects'][1]
        a.click(x + w / 2, y + h / 2)
        a.until('names on disk', lambda: any(n.startswith('Sample') for n in a.names()))
        a.until('undo toast', lambda: a.gh()['undo'])

    def budgets(a, names):
        """Numbers, not looks: idle CPU with suggestions on screen and with a run going (cpu_budget.py, five 30 s windows each),
        the working set, and how long the window takes to answer a message while a run goes (the run never blocks the window)."""
        import psutil
        tool = r'C:\Users\Omid\.agents\tools\cpu_budget.py'
        out = {}
        a.select_all(); a.suggest(); a.pairs_ready()
        time.sleep(1)
        r = subprocess.run([sys.executable, tool, '--pid', str(a.proc.pid), '--max', '0.5'], capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
        out['idle_with_suggestions'] = r.stdout[-700:]
        assert r.returncode == 0, 'idle CPU with suggestions: ' + r.stdout[-400:]
        a.key(VK_ESC)
        set_mode('slow')
        a.select_all(); a.suggest()
        a.until('run started', lambda: a.gh()['running'])
        r = subprocess.run([sys.executable, tool, '--pid', str(a.proc.pid), '--max', '0.5'], capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
        out['idle_while_running'] = r.stdout[-700:]
        assert r.returncode == 0, 'idle CPU while a run goes: ' + r.stdout[-400:]
        lat = []
        for _ in range(60):
            t = time.perf_counter(); a.msg(0); lat.append((time.perf_counter() - t) * 1000)
        lat.sort()
        out['window_answer_ms_p50_p95_max'] = [round(lat[30], 1), round(lat[56], 1), round(lat[-1], 1)]
        out['working_set_mb'] = round(psutil.Process(a.proc.pid).memory_info().rss / 1048576, 1)
        (WORK / 'ghost-budgets.json').write_text(json.dumps(out, indent=2), encoding='utf-8')
        assert lat[56] < 50, 'the window answers slowly while a run goes: %s' % lat[56]

    def replaced_file(a, names):
        """A file replaced after the suggestion came is another file: Accept leaves it alone and renames the rest."""
        a.select_all(); a.suggest(); a.pairs_ready()
        (FIX / 'IMG_4821.jpg').write_bytes(b'a different picture, written after the suggestion')
        a.key(VK_TAB)
        a.until('the others renamed', lambda: any(n.startswith('Sample') for n in a.names()))
        a.until('worker done', lambda: not a.gh()['batch'])
        assert 'IMG_4821.jpg' in a.names(), 'the replaced file was renamed: %s' % a.names()
        assert not any(n.startswith('IMG_4822') for n in a.names()), 'the untouched files were not renamed'

    check('1-no-agent', no_agent, stub=False, chosen=False)
    check('2-first-use-picks-once', first_use_picks_once, chosen=False)
    check('3-accept-and-undo', accept_and_undo)
    check('4-esc-drops', esc_drops)
    check('5-collisions', collisions)
    check('6-illegal', illegal)
    check('7-broken-json', broken)
    check('8-timeout', timeout, timeout=2500)
    check('9-cancel', cancel)
    check('10-f2-machine-name', f2_machine_name)
    check('11-f2-normal-name', f2_normal_name)
    check('12-f2-never-opens-picker', f2_never_picker, chosen=False)
    check('13-list-view', list_view, view='list')
    check('14-accept-by-click', accept_by_click)
    check('16-replaced-file', replaced_file)
    if os.environ.get('DFM_BUDGET') or (os.environ.get('USE_ONLY') or '').startswith('15'):   # about 5 minutes
        check('15-budgets', budgets)
    (WORK / 'ghost-results.json').write_text(json.dumps(results, indent=2, ensure_ascii=False), encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def main():
    for d in ('tmp', 'appdata', 'final'):
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
    with (WORK / 'ghost.log').open('wb') as out, (WORK / 'ghost.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=900)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

"""The disk tree on real folders (a user folder, a whole drive): budgets and pictures, on the hidden desktop.

Budgets (fail over them; docs/design.md "Disk tree"): while the folders are measured the window answers every message in under 50 ms;
working set at most 175 MB at every sample and at the end; idle CPU below 0.1 % of one core over 20 s once the tree is settled.
Usage: python tests/use/tree_budget.py PATH [PATH ...]      (DFM_EXE, DFM_USE_WORK as in input_smoke.py)
Writes <work>/tree-budget.json and a picture per path and theme in <work>/final.
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

WORK = base.WORK
PICS = WORK / 'final'
K = C.WinDLL('kernel32', use_last_error=True)
P = C.WinDLL('psapi', use_last_error=True)


class MEM(C.Structure):
    _fields_ = [('cb', W.DWORD), ('PageFaultCount', W.DWORD), ('PeakWorkingSetSize', C.c_size_t), ('WorkingSetSize', C.c_size_t),
                ('QuotaPeakPagedPoolUsage', C.c_size_t), ('QuotaPagedPoolUsage', C.c_size_t),
                ('QuotaPeakNonPagedPoolUsage', C.c_size_t), ('QuotaNonPagedPoolUsage', C.c_size_t),
                ('PagefileUsage', C.c_size_t), ('PeakPagefileUsage', C.c_size_t)]


K.OpenProcess.restype = W.HANDLE
K.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
P.GetProcessMemoryInfo.argtypes = [W.HANDLE, C.POINTER(MEM), W.DWORD]
K.GetProcessTimes.argtypes = [W.HANDLE, C.POINTER(W.FILETIME), C.POINTER(W.FILETIME), C.POINTER(W.FILETIME), C.POINTER(W.FILETIME)]


def proc_handle(pid):
    return K.OpenProcess(0x1000 | 0x0400, False, pid)   # query limited + read


def mem_mb(h):
    m = MEM(); m.cb = C.sizeof(m)
    assert P.GetProcessMemoryInfo(h, C.byref(m), m.cb)
    return m.WorkingSetSize / 1048576, m.PeakWorkingSetSize / 1048576


def cpu_s(h):
    c, e, k, u = (W.FILETIME() for _ in range(4))
    assert K.GetProcessTimes(h, C.byref(c), C.byref(e), C.byref(k), C.byref(u))
    t = lambda f: ((f.dwHighDateTime << 32) | f.dwLowDateTime) / 1e7
    return t(k) + t(u)


def run_one(path, theme):
    name = ('real-' + Path(path).name.replace(':', '').replace('\\', '') + '-' + theme).replace('--', '-')
    view = os.environ.get('DFM_TB_VIEW', 'tree')   # 'list' measures the Details view of the same folder, the baseline
    a = base.App(name + view, path, view, theme, extra=('--zoom', '0'))
    out = dict(path=str(path), theme=theme)
    try:
        h = proc_handle(a.proc.pid)
        t0 = time.monotonic()
        worst, peak = 0.0, 0.0
        first_pic = None
        while True:
            worst = max(worst, a.msg(0))
            ws, pk = mem_mb(h)
            peak = max(peak, pk)
            s = a.snap()
            t = s['tree']
            if first_pic is None and (t['n'] or view != 'tree'):
                first_pic = time.monotonic() - t0
            if s['fsz_open'] == 0 and (view != 'tree' or t['n']) or time.monotonic() - t0 > 600:
                break
            time.sleep(.05)
        out['seconds_to_all_in'] = round(time.monotonic() - t0, 1)
        out['seconds_to_first_rectangles'] = round(first_pic or -1, 1)
        # nested listings land after the sizes and may ask for sizes the cache dropped: wait until the engine is quiet for 2 s in a row
        t1, quiet = time.monotonic(), 0
        while quiet < 2 and time.monotonic() - t1 < 180:
            worst = max(worst, a.msg(0))
            ws, pk = mem_mb(h)
            peak = max(peak, pk)
            quiet = quiet + 1 if a.snap()['fsz_open'] == 0 else 0
            time.sleep(1.0)
        out['seconds_nested_settle'] = round(time.monotonic() - t1, 1)
        s = a.snap(f'{name}-state')
        shutil.copyfile(str(a.base) + '.png', PICS / (name + view + '.png'))
        out['head'] = s['tree']['head']
        out['rects'] = s['tree']['n']
        out['subs'] = s['tree']['subs']
        # messages while the tree is idle, and the CPU over the window
        time.sleep(1.0)
        c0, w0 = cpu_s(h), time.monotonic()
        idle_worst = 0.0
        time.sleep(20)   # no message sent: the window is left alone
        out['idle_cpu_pct_of_one_core_over_20s'] = round(100 * (cpu_s(h) - c0) / (time.monotonic() - w0), 3)
        ws, pk = mem_mb(h)
        out.update(max_message_ms_during_walk=round(worst * 1000, 1), max_message_ms_idle=round(idle_worst * 1000, 1),
                   working_set_mb=round(ws, 1), peak_working_set_mb=round(max(peak, pk), 1))
    finally:
        a.close()
    return out


def inside():
    results = []
    for p in sys.argv[sys.argv.index('--inside') + 1:]:
        for theme in os.environ.get('DFM_TB_THEMES', 'dark,light').split(','):
            r = run_one(p, theme)
            results.append(r)
            print(json.dumps(r), flush=True)
    (WORK / 'tree-budget.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    fails = []
    for r in results:
        tag = f"{r['path']} {r['theme']}"
        if r['max_message_ms_during_walk'] >= 50: fails.append(f"{tag}: message delay {r['max_message_ms_during_walk']} ms")
        if r['peak_working_set_mb'] > 175: fails.append(f"{tag}: working set {r['peak_working_set_mb']} MB")
        if r['idle_cpu_pct_of_one_core_over_20s'] >= 0.1: fails.append(f"{tag}: idle CPU {r['idle_cpu_pct_of_one_core_over_20s']} %")
    print('FAIL ' + '; '.join(fails) if fails else 'budgets met', flush=True)
    return 1 if fails else 0


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
    with (WORK / 'tree-budget.log').open('wb') as out, (WORK / 'tree-budget.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside', *sys.argv[1:]], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=3000)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

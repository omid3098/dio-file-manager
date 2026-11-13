"""Guarded end-to-end check of "Use as Default File Manager" against the REAL registry (not a *_smoke.py: run it by hand).
Run from the repo: python tests/use/default_fm_real.py
DFM_EXE selects the executable, DFM_USE_WORK the folder for artifacts.

What it does, on a private desktop so that no window reaches the user's screen:
  1. gaming.py check (and wait, if a game is on);
  2. backs up HKCU\\Software\\Classes\\Folder, the Win+E class key and HKCU\\Software\\Dio File Manager (reg export text
     and a value-by-value dump, with types);
  3. turns the switch on with --test-state (so an opened place is only written to a log, and no app window opens);
  4. at most 10 seconds later, from the private desktop, calls ShellExecuteEx with the "open" verb on a test folder,
     on a drive and on Win+E's target, and reads the log to see what arrived and where;
  5. in a finally block turns the switch off, compares the keys byte for byte with the backup, and on a difference
     imports the backup and tries again. The result line says RESTORED or NOT RESTORED.
The outer process then looks at the real keys once more, from outside the private desktop.
"""
import ctypes
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import winreg

ROOT = Path(__file__).resolve().parents[2]
WORK = Path(os.environ.get('DFM_USE_WORK', ROOT / '.agent-work/use-checks' / Path(__file__).stem))
EXE = Path(os.environ.get('DFM_EXE', ROOT / 'build/MinSizeRel/dio-file-manager.exe'))
WIN_E = '{52205fd8-5dfb-447d-801a-d0b52f2e83e1}'
KEYS = [r'Software\Classes\Folder', r'Software\Classes\CLSID\%s' % WIN_E, r'Software\Dio File Manager']
BASE = WORK / 'real'
OPENED = Path(str(BASE) + '.opened')
BACKUP = WORK / 'backup'
NO_WINDOW = subprocess.CREATE_NO_WINDOW
LAUNCHER_DIR = Path(os.environ['LOCALAPPDATA']) / 'dio-file-manager'
MAX_REGISTERED = 10.0


def dump(path, rel=''):
    out = []
    try:
        k = winreg.OpenKey(winreg.HKEY_CURRENT_USER, path)
    except OSError:
        return out
    with k:
        out.append([rel, None, None, None])
        i = 0
        while True:
            try:
                name, data, typ = winreg.EnumValue(k, i)
            except OSError:
                break
            out.append([rel, name, typ, repr(data)])
            i += 1
        i = 0
        while True:
            try:
                sub = winreg.EnumKey(k, i)
            except OSError:
                break
            out += dump(path + '\\' + sub, rel + '\\' + sub)
            i += 1
    return sorted(out, key=lambda r: [('' if x is None else str(x)) for x in r])


def export_text(key, target):
    """The reg export text of a key, or 'absent'."""
    target.unlink(missing_ok=True)
    r = subprocess.run(['reg', 'export', 'HKCU\\' + key, str(target), '/y'], capture_output=True, creationflags=NO_WINDOW)
    return target.read_bytes() if r.returncode == 0 and target.exists() else b'absent'


def snapshot(tag):
    d = WORK / tag
    d.mkdir(exist_ok=True)
    return {'dump': json.loads(json.dumps([dump(k, k) for k in KEYS])),
            'export': [export_text(k, d / ('k%d.reg' % i)).hex() for i, k in enumerate(KEYS)]}


def app(*args, base=True):
    env = {k: v for k, v in os.environ.items() if not k.startswith('DFM_TEST')}
    return subprocess.run([str(EXE), *args, *(['--test-state', str(BASE)] if base else [])], env=env, cwd=ROOT,
                          timeout=20, creationflags=NO_WINDOW).returncode


def lines():
    return OPENED.read_text(encoding='utf-8', errors='replace').splitlines() if OPENED.exists() else []


def kill_started():
    for line in lines():
        if line.startswith('pid\t'):
            subprocess.run(['taskkill', '/F', '/PID', line.split('\t')[1]], capture_output=True, creationflags=NO_WINDOW)


class SEI(ctypes.Structure):
    _fields_ = [('cbSize', W.DWORD), ('fMask', ctypes.c_ulong), ('hwnd', W.HWND), ('lpVerb', W.LPCWSTR), ('lpFile', W.LPCWSTR),
                ('lpParameters', W.LPCWSTR), ('lpDirectory', W.LPCWSTR), ('nShow', ctypes.c_int), ('hInstApp', W.HINSTANCE),
                ('lpIDList', ctypes.c_void_p), ('lpClass', W.LPCWSTR), ('hkeyClass', W.HKEY), ('dwHotKey', W.DWORD),
                ('hIcon', W.HANDLE), ('hProcess', W.HANDLE)]


def shell_execute(verb, target):
    ctypes.windll.ole32.CoInitializeEx(None, 2)
    s = SEI()
    s.cbSize = ctypes.sizeof(SEI)
    s.fMask = 0x100 | 0x400   # SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI
    s.lpVerb, s.lpFile, s.nShow = verb, target, 1
    ok = ctypes.windll.shell32.ShellExecuteExW(ctypes.byref(s))
    return bool(ok), ctypes.get_last_error() if not ok else 0


def resolved_command(cls, verb):
    """The command Windows would run for this class and verb (the merged HKCU+HKCR view), without running it."""
    buf = ctypes.create_unicode_buffer(2048)
    n = W.DWORD(2048)
    hr = ctypes.windll.shlwapi.AssocQueryStringW(0x8, 1, cls, verb, buf, ctypes.byref(n))   # ASSOCF_INIT_DEFAULTTOFOLDER, ASSOCSTR_COMMAND
    return buf.value if hr == 0 else 'error %#x' % (hr & 0xFFFFFFFF)


def case(label, verb, target, seconds, expect, cls=None):
    """One ShellExecuteEx; True when the expected line arrived. Gives up after the seconds given.
    Before the call the class's command is read: when it is not the launcher, the call is not made at all,
    because it would start the real Explorer."""
    kill_started()
    OPENED.unlink(missing_ok=True)
    if cls:
        cmd = resolved_command(cls, verb)
        if 'dio-file-manager-launcher.exe' not in cmd:
            print(json.dumps({'case': label, 'class': cls, 'resolves_to': cmd, 'call': 'not made: not our launcher',
                              'result': 'FAILED'}), flush=True)
            return False
    ok, err = shell_execute(verb, target)
    deadline = time.monotonic() + seconds
    got = []
    while time.monotonic() < deadline:
        got = [l for l in lines() if l.startswith('arrive-')]
        if got:
            break
        time.sleep(.05)
    kill_started()
    passed = bool(got) and got[0] == expect
    print(json.dumps({'case': label, 'verb': verb, 'target': target, 'call_ok': ok, 'error': err, 'arrived': got, 'expected': expect,
                      'result': 'passed' if passed else 'FAILED'}), flush=True)
    return passed


def inside():
    WORK.mkdir(parents=True, exist_ok=True)
    folder = WORK / 'opened folder'
    folder.mkdir(exist_ok=True)
    OPENED.unlink(missing_ok=True)
    before = snapshot('before')
    (WORK / 'before.json').write_text(json.dumps(before), encoding='utf-8')
    launcher_before = (LAUNCHER_DIR / 'dio-file-manager-launcher.exe').exists()
    assert app('--default-fm', 'status', base=False) == 10, 'The switch is already on: nothing to test against'
    results = []
    started = None
    restored = False
    try:
        assert app('--default-fm', 'on') == 0, 'Turning the switch on failed'
        started = time.monotonic()
        time.sleep(.2)
        results.append(case('folder', 'open', str(folder), 2.5, 'arrive-folder\t%s\t' % folder, 'Directory'))
        results.append(case('drive', 'open', 'C:\\', 2.5, 'arrive-folder\tC:\\\t', 'Drive'))
        win_e = case('win+e class name', 'opennewwindow', 'shell:::' + WIN_E, 2.0, 'arrive-home\t\t')
        if not win_e and time.monotonic() - started < 6:
            win_e = case('win+e class name (open)', 'open', '::' + WIN_E, 2.0, 'arrive-home\t\t')
        results.append(win_e)
    finally:
        window = time.monotonic() - started if started else 0.0
        for attempt in range(3):
            app('--default-fm', 'off')
            after = snapshot('after')
            if after == before:
                restored = True
                break
            print(json.dumps({'restore_attempt': attempt + 1, 'same': False}), flush=True)
            for i, k in enumerate(KEYS):   # put the backup back, then try the switch once more
                text = bytes.fromhex(before['export'][i])
                if text != b'absent':
                    subprocess.run(['reg', 'import', str(WORK / 'before' / ('k%d.reg' % i))], capture_output=True, creationflags=NO_WINDOW)
        kill_started()
        print(json.dumps({'registered_seconds': round(window, 2), 'restored': restored, 'launcher_left': (LAUNCHER_DIR / 'dio-file-manager-launcher.exe').exists(),
                          'launcher_before': launcher_before}), flush=True)
    assert restored, 'NOT RESTORED'
    assert window <= MAX_REGISTERED, 'The keys stayed registered for %.1f seconds' % window
    return int(not all(results))


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / 'tmp').mkdir(exist_ok=True)
    os.environ.update(TEMP=str(WORK / 'tmp'), TMP=str(WORK / 'tmp'))
    if '--inside' in sys.argv:
        return inside()
    gaming = Path(r'C:\Users\Omid\.agents\tools\gaming.py')
    code = subprocess.run([sys.executable, str(gaming), 'check'], creationflags=NO_WINDOW).returncode
    if code == 3:
        subprocess.run([sys.executable, str(gaming), 'wait', '--cap', '60'], check=True, creationflags=NO_WINDOW)
    elif code:
        return code
    sys.path.insert(0, r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    rc = 1
    try:
        with (WORK / 'real.log').open('wb') as out, (WORK / 'real.err').open('wb') as err:
            run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
            try:
                rc = run.wait(timeout=90)
            finally:
                run.close()
    finally:   # from outside the private desktop: nothing may stay registered, whatever happened inside
        status = subprocess.run([str(EXE), '--default-fm', 'status'], env={k: v for k, v in os.environ.items() if not k.startswith('DFM_TEST')},
                                creationflags=NO_WINDOW).returncode
        if status != 10:
            subprocess.run([str(EXE), '--default-fm', 'off'], env={k: v for k, v in os.environ.items() if not k.startswith('DFM_TEST')},
                           creationflags=NO_WINDOW)
            print('The switch was still on after the run; it was turned off from outside', file=sys.stderr)
            rc = rc or 2
    return rc


if __name__ == '__main__':
    raise SystemExit(main())

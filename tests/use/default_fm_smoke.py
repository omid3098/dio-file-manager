"""Checks for "Use as Default File Manager" with the registry root redirected to a test key.
Run from the repo: python tests/use/default_fm_smoke.py
DFM_EXE selects the executable, DFM_USE_WORK the folder for artifacts.

Nothing here touches the real Classes or the real Dio File Manager key. The exe runs with --test-state, which lets
DFM_TEST_REGROOT (a key under HKCU), DFM_TEST_LOCAL (the launcher folder) and DFM_TEST_EXPLORER (a stand-in for
Explorer) take effect. The whole run is on a private desktop; every app it starts is ended by the pid it logs.
"""
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid
import winreg

ROOT = Path(__file__).resolve().parents[2]
WORK = Path(os.environ.get('DFM_USE_WORK', ROOT / '.agent-work/use-checks' / Path(__file__).stem))
EXE = Path(os.environ.get('DFM_EXE', ROOT / 'build/MinSizeRel/dio-file-manager.exe'))
sys.path.insert(0, str(Path(__file__).resolve().parent))

WIN_E = '{52205fd8-5dfb-447d-801a-d0b52f2e83e1}'
HIVE = winreg.HKEY_CURRENT_USER
TEST_KEY = r'Software\DioFmTest\%s' % uuid.uuid4().hex[:10]
CLASSES = TEST_KEY + r'\Classes'
STATE = TEST_KEY + r'\State'
APP_DIR = WORK / 'app'
LOCAL = WORK / 'local'
BASE = WORK / 'default-fm'
OPENED = Path(str(BASE) + '.opened')
RESIDENT = Path(str(BASE) + '.resident')   # what the Win+E process logs: its pid, hotkey-ok / hotkey-busy, hotkey-open
ADV = TEST_KEY + r'\Advanced'              # stands in for Explorer\Advanced (DisabledHotkeys)
RUN = TEST_KEY + r'\Run'                   # stands in for the Run key
RUN_NAME = 'Dio File Manager'
RUNONCE = TEST_KEY + r'\RunOnce'           # stands in for RunOnce: the launcher cleanup after a turn-off
CLEANUP_NAME = 'Dio File Manager launcher cleanup'
STAND_LOG = WORK / 'standin.log'
STAND_PY = WORK / 'standin.py'
NO_WINDOW = subprocess.CREATE_NO_WINDOW

STAND_IN = '''import os, subprocess, sys
with open(os.environ["STANDIN_LOG"], "a", encoding="utf-8") as f:
    f.write("\\t".join(sys.argv[1:]) + "\\n")
    n = len(open(os.environ["STANDIN_LOG"], encoding="utf-8").read().splitlines())
if os.environ.get("STANDIN_RELAUNCH") and n < 5:   # a stand-in that calls the app back, like a loop would
    subprocess.run([os.environ["STANDIN_RELAUNCH"], "--test-state", os.environ["STANDIN_BASE"]] + sys.argv[1:],
                   creationflags=0x08000000)
'''


# ---- registry helpers ----

def dump(path=TEST_KEY, rel=''):
    """Every key and value below path, with value types, as a sorted list."""
    out = []
    try:
        k = winreg.OpenKey(HIVE, path)
    except OSError:
        return out
    with k:
        out.append((rel, None, None, None))
        i = 0
        while True:
            try:
                name, data, typ = winreg.EnumValue(k, i)
            except OSError:
                break
            out.append((rel, name, typ, repr(data)))
            i += 1
        i = 0
        while True:
            try:
                sub = winreg.EnumKey(k, i)
            except OSError:
                break
            out += dump(path + '\\' + sub, rel + '\\' + sub)
            i += 1
    return sorted(out, key=lambda r: tuple('' if x is None else str(x) for x in r))


def put(rel, name, value, typ=winreg.REG_SZ):
    with winreg.CreateKeyEx(HIVE, CLASSES + ('\\' + rel if rel else ''), 0, winreg.KEY_WRITE) as k:
        if name is not None:
            winreg.SetValueEx(k, name, 0, typ, value)


def wipe():
    try:
        subprocess.run(['reg', 'delete', 'HKCU\\' + TEST_KEY, '/f'], capture_output=True, creationflags=NO_WINDOW)
    except OSError:
        pass


def ensure_keys():
    """Explorer/Advanced and Run exist on every Windows; the redirected copies exist before a check starts."""
    for key in (ADV, RUN, RUNONCE):
        winreg.CreateKeyEx(HIVE, key, 0, winreg.KEY_WRITE).Close()


def adv_value():
    try:
        with winreg.OpenKey(HIVE, ADV) as k:
            return winreg.QueryValueEx(k, 'DisabledHotkeys')
    except OSError:
        return None


def set_adv(value):
    with winreg.CreateKeyEx(HIVE, ADV, 0, winreg.KEY_WRITE) as k:
        if value is None:
            try:
                winreg.DeleteValue(k, 'DisabledHotkeys')
            except OSError:
                pass
        else:
            winreg.SetValueEx(k, 'DisabledHotkeys', 0, value[1], value[0])


def run_value():
    try:
        with winreg.OpenKey(HIVE, RUN) as k:
            return winreg.QueryValueEx(k, RUN_NAME)[0]
    except OSError:
        return None


def runonce_value():
    try:
        with winreg.OpenKey(HIVE, RUNONCE) as k:
            return winreg.QueryValueEx(k, CLEANUP_NAME)[0]
    except OSError:
        return None


def seed_rich():
    """Values that exist before: three types, an existing Folder\\shell with a value, another CLSID, no explore key."""
    wipe()
    put('', None, None)
    put('Folder\\shell', '', 'none')
    put('Folder\\shell\\open', 'MUIVerb', 'Open it')
    put('Folder\\shell\\open\\command', '', '%SystemRoot%\\explorer.exe /idlist,%I,%L', winreg.REG_EXPAND_SZ)
    put('Folder\\shell\\open\\command', 'DelegateExecute', '{11dbb47c-a525-400b-9e80-a54615a090c0}')
    put('Folder\\shell\\open\\command', 'Extra', bytes([1, 2, 3, 0, 255]), winreg.REG_BINARY)
    put('Folder\\shell\\open\\command', 'Count', 7, winreg.REG_DWORD)
    put('CLSID\\{00000000-0000-0000-0000-000000000042}', 'Name', 'someone else')
    ensure_keys()


def seed_clean():
    wipe()
    put('', None, None)
    ensure_keys()


def state_value(name):
    try:
        with winreg.OpenKey(HIVE, STATE) as k:
            return winreg.QueryValueEx(k, name)[0]
    except OSError:
        return None


def command_of(rel):
    try:
        with winreg.OpenKey(HIVE, CLASSES + '\\' + rel) as k:
            return winreg.QueryValueEx(k, '')[0]
    except OSError:
        return None


FOLDER_OPEN = 'Folder\\shell\\open\\command'
FOLDER_EXPLORE = 'Folder\\shell\\explore\\command'
WIN_E_CMD = 'CLSID\\%s\\shell\\opennewwindow\\command' % WIN_E
WIN_E_OPEN = 'CLSID\\%s\\shell\\open\\command' % WIN_E   # the verb a launch without a verb gets; Windows' own copy sends it to Explorer


# ---- running the exe ----

def env():
    e = dict(os.environ, DFM_TEST_REGROOT=TEST_KEY, DFM_TEST_LOCAL=str(LOCAL), APPDATA=str(WORK / 'appdata'),
             DFM_TEST_EXPLORER='"%s" "%s"' % (sys.executable, STAND_PY), STANDIN_LOG=str(STAND_LOG))
    e.pop('STANDIN_RELAUNCH', None)
    return e


def exe_run(args, exe=None, extra_env=None, timeout=15):
    e = env()
    e.update(extra_env or {})
    return subprocess.run([str(exe or APP_EXE), *args], env=e, cwd=ROOT, timeout=timeout, creationflags=NO_WINDOW).returncode


def registered_prefix():
    """The registered Win+E command as the shell would run it: the launcher and what it carries."""
    return command_of(WIN_E_CMD)


def lines():
    try:
        return OPENED.read_text(encoding='utf-8', errors='replace').splitlines() if OPENED.exists() else []
    except PermissionError:   # the app is writing it right now: the next look sees it
        return []


def standin_lines():
    return STAND_LOG.read_text(encoding='utf-8').splitlines() if STAND_LOG.exists() else []


def kill_apps():
    pids = [int(l.split('\t')[1]) for l in lines() if l.startswith('pid\t')]
    for pid in pids:
        subprocess.run(['taskkill', '/F', '/PID', str(pid)], capture_output=True, creationflags=NO_WINDOW)
    for pid in pids:   # an exe that is still running cannot be written
        for _ in range(100):
            if not pid_alive(pid):
                break
            time.sleep(.05)


def res_lines():
    try:
        return RESIDENT.read_text(encoding='utf-8', errors='replace').splitlines() if RESIDENT.exists() else []
    except PermissionError:
        return []


def res_pids():
    return [int(l.split('\t')[1]) for l in res_lines() if l.startswith('pid\t')]


def pid_alive(pid):
    h = ctypes.windll.kernel32.OpenProcess(0x00100000, False, pid)   # SYNCHRONIZE
    if not h:
        return False
    alive = ctypes.windll.kernel32.WaitForSingleObject(h, 0) == 258
    ctypes.windll.kernel32.CloseHandle(h)
    return alive


def kill_resident():
    for pid in res_pids():
        if pid_alive(pid):
            subprocess.run(['taskkill', '/F', '/PID', str(pid)], capture_output=True, creationflags=NO_WINDOW)
    for pid in res_pids():
        for _ in range(100):
            if not pid_alive(pid):
                break
            time.sleep(.05)


def kill_started():
    kill_apps()
    kill_resident()


def has_line(prefix):
    return any(l.startswith(prefix) for l in res_lines())


def resident_window():
    user32 = ctypes.windll.user32
    user32.FindWindowW.restype = ctypes.c_void_p
    return user32.FindWindowW('DioFileManagerResident', None)


def post_hotkey():
    """What Windows posts to the window that owns a hotkey: WM_HOTKEY with the id the resident registered."""
    w = resident_window()
    assert w, 'The resident process has no window'
    ctypes.windll.user32.PostMessageW(ctypes.c_void_p(w), 0x0312, 1, 0)


def reset_case():
    kill_started()
    for f in (OPENED, RESIDENT, STAND_LOG, LOCAL / 'fallback.txt'):
        for _ in range(60):   # a process that was just ended may still hold the file for a moment
            try:
                f.unlink(missing_ok=True)
                break
            except PermissionError:
                time.sleep(.05)
        else:
            f.unlink(missing_ok=True)


def wait_for(what, cond, seconds=8):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        v = cond()
        if v:
            return v
        time.sleep(.05)
    raise AssertionError(what)


def fire(command_text):
    """Start a command line text the way the shell starts a registered command; do not wait."""
    return subprocess.Popen(command_text, env=env(), cwd=ROOT, creationflags=NO_WINDOW)


def arrival():
    wait_for('Nothing arrived: ' + repr(lines()), lambda: any(l.startswith(('arrive-', 'explorer', 'refused')) for l in lines()), seconds=20)
    time.sleep(.15)
    return [l for l in lines() if not l.startswith('pid\t')]


def known_folder(guid):
    class G(ctypes.Structure):
        _fields_ = [('a', ctypes.c_ulong), ('b', ctypes.c_ushort), ('c', ctypes.c_ushort), ('d', ctypes.c_ubyte * 8)]
    g = G()
    ctypes.windll.ole32.CLSIDFromString(guid, ctypes.byref(g))
    out = ctypes.c_wchar_p()
    assert ctypes.windll.shell32.SHGetKnownFolderPath(ctypes.byref(g), 0, None, ctypes.byref(out)) == 0
    return out.value


# ---- the checks ----

APP_EXE = APP_DIR / 'dio-file-manager.exe'


def inside():
    results = []
    sandbox = WORK / 'a folder with space'
    shutil.rmtree(sandbox, ignore_errors=True)
    sandbox.mkdir(parents=True)
    (sandbox / 'notes file.txt').write_text('x', encoding='utf-8')
    shutil.rmtree(APP_DIR, ignore_errors=True)
    shutil.rmtree(LOCAL, ignore_errors=True)
    APP_DIR.mkdir(parents=True)
    LOCAL.mkdir(parents=True)
    (WORK / 'appdata').mkdir(exist_ok=True)
    shutil.copyfile(EXE, APP_EXE)
    STAND_PY.write_text(STAND_IN, encoding='utf-8')
    OPENED.unlink(missing_ok=True)
    os.environ.update(env())   # every child, also the ones other helpers start, runs against the test key

    def real_hot():
        """Read-only: the real DisabledHotkeys and Run value must stay as they are."""
        out = []
        for path, name in ((r'Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced', 'DisabledHotkeys'),
                           (r'Software\Microsoft\Windows\CurrentVersion\Run', RUN_NAME),
                           (r'Software\Microsoft\Windows\CurrentVersion\RunOnce', CLEANUP_NAME)):
            try:
                with winreg.OpenKey(HIVE, path) as k:
                    out.append(winreg.QueryValueEx(k, name))
            except OSError:
                out.append(None)
        return out

    def real_keys():
        return [dump(p, p) for p in (r'Software\Classes\Folder', r'Software\Classes\CLSID\%s' % WIN_E, r'Software\Dio File Manager')] + [real_hot()]

    def check(name, fn):
        if os.environ.get('USE_ONLY') and not name.startswith(tuple(os.environ['USE_ONLY'].split(','))):
            return
        real = real_keys()
        try:
            reset_case()
            fn()
            assert real_keys() == real, 'The real registry keys changed'
            results.append({'check': name, 'result': 'passed'})
        except Exception as e:
            results.append({'check': name, 'result': 'FAILED', 'reason': '%s: %s' % (type(e).__name__, e)})
        finally:
            reset_case()
            if real_keys() != real:   # never leave the real registry changed: undo it with the app's own switch, no redirect
                subprocess.run([str(APP_EXE), '--default-fm', 'off'], env={k: v for k, v in os.environ.items() if not k.startswith('DFM_TEST')},
                               creationflags=NO_WINDOW)
                results.append({'check': name + ' (real registry guard)', 'result': 'FAILED', 'reason': 'the real keys were changed'})
        print(json.dumps(results[-1]), flush=True)

    on = lambda: exe_run(['--default-fm', 'on', '--test-state', str(BASE)])
    off_raw = lambda: exe_run(['--default-fm', 'off', '--test-state', str(BASE)])

    def explorer_restarts():
        """What Explorer does when it starts: tell the windows "TaskbarCreated". Posted to the process's own window, so no other
        program is bothered."""
        user32 = ctypes.windll.user32
        user32.RegisterWindowMessageW.restype = ctypes.c_uint
        w = resident_window()
        if w:
            user32.PostMessageW(ctypes.c_void_p(w), user32.RegisterWindowMessageW('TaskbarCreated'), 0, 0)

    def fake_explorer():
        """A stand-in for the Explorer that is running at turn-off: a process that owns a window of Explorer's taskbar class. The
        bridge watches the process behind that window, and lets go of Win+E when it ends."""
        code = ('''import ctypes, time
u = ctypes.windll.user32
class C(ctypes.Structure):
    _fields_ = [('style', ctypes.c_uint), ('proc', ctypes.c_void_p), ('cls', ctypes.c_int), ('wnd', ctypes.c_int), ('inst', ctypes.c_void_p),
                ('icon', ctypes.c_void_p), ('cursor', ctypes.c_void_p), ('brush', ctypes.c_void_p), ('menu', ctypes.c_wchar_p), ('name', ctypes.c_wchar_p)]
c = C()
c.proc = ctypes.cast(u.DefWindowProcW, ctypes.c_void_p).value
c.name = 'Shell_TrayWnd'
u.RegisterClassW(ctypes.byref(c))
u.CreateWindowExW.restype = ctypes.c_void_p
assert u.CreateWindowExW(0, 'Shell_TrayWnd', 'taskbar', 0x80000000, 0, 0, 0, 0, None, None, None, None)
print('ready', flush=True)
time.sleep(600)
''')
        p = subprocess.Popen([sys.executable, '-c', code], stdout=subprocess.PIPE, env=env(), creationflags=NO_WINDOW)
        assert p.stdout.readline().strip() == b'ready', 'The stand-in Explorer did not start'
        return p

    def sign_in():
        """What Windows does at the next sign-in: it deletes the RunOnce value and runs its command. The real launcher is never
        touched: the command carries --test-state, so it works on the redirected folder."""
        cmd = runonce_value()
        if not cmd:
            return
        with winreg.OpenKey(HIVE, RUNONCE, 0, winreg.KEY_SET_VALUE) as k:
            winreg.DeleteValue(k, CLEANUP_NAME)
        subprocess.run(cmd, env=env(), cwd=ROOT, creationflags=NO_WINDOW, timeout=15)

    def settle():
        for pid in [p for p in res_pids() if pid_alive(p)]:
            explorer_restarts()
            wait_for('The process did not end after TaskbarCreated: %r' % res_lines(), lambda: not pid_alive(pid))
        sign_in()

    def off():
        """Turn off, then let the rest of the day happen (Explorer restarts, the user signs in again), so a check that looks for
        the keys, the process and the launcher after a turn-off sees the end state. The bridge itself is checked in 23."""
        rc = off_raw()
        if rc == 0:
            settle()
        return rc
    status = lambda: exe_run(['--default-fm', 'status', '--test-state', str(BASE)])

    def on_off(seed, label):
        def run():
            seed()
            before = dump()
            assert status() == 10, 'Reports on before anything is registered'
            assert on() == 0, 'Turning on failed'
            assert status() == 0, 'Not reported on after turning on'
            after_on = dump()
            assert after_on != before, 'Turning on changed nothing'
            launcher = str(LOCAL / 'dio-file-manager-launcher.exe')
            assert (LOCAL / 'dio-file-manager-launcher.exe').exists(), 'No launcher copy'
            for rel, tail in ((FOLDER_OPEN, ' "%1"'), (FOLDER_EXPLORE, ' "%1"'), (WIN_E_CMD, ''), (WIN_E_OPEN, '')):
                want = '"%s" --test-state "%s"%s' % (launcher, BASE, tail)
                assert command_of(rel) == want, f'{rel}: {command_of(rel)!r} is not {want!r}'
                with winreg.OpenKey(HIVE, CLASSES + '\\' + rel) as k:
                    assert winreg.QueryValueEx(k, 'DelegateExecute') == ('', winreg.REG_SZ), 'DelegateExecute is not an empty string'
            assert state_value('App') == str(APP_EXE) and state_value('Launcher') == launcher, 'State App/Launcher wrong'
            assert on() == 0 and dump() == after_on, 'Turning on twice changed the keys'
            assert off() == 0, 'Turning off failed'
            assert status() == 10, 'Still reported on after turning off'
            assert dump() == before, 'Keys differ from before:\n%s\nvs\n%s' % (
                [r for r in dump() if r not in before], [r for r in before if r not in dump()])
            assert state_value('App') is None, 'State key left behind'
            assert not (LOCAL / 'dio-file-manager-launcher.exe').exists(), 'The launcher copy was left behind'
            assert off() == 0 and dump() == before, 'Turning off twice changed the keys'
            assert on() == 0 and off() == 0 and dump() == before, 'A second on/off cycle left a difference'
        return run

    def forms():
        seed_rich()
        before = dump()
        assert on() == 0
        folder_cmd = command_of(FOLDER_OPEN)
        prefix = registered_prefix()
        downloads = known_folder('{374DE290-123F-4565-9164-39C4925E467B}')
        win_e_cases = ['', 'shell:::' + WIN_E, '::' + WIN_E]

        def launcher_call(text):
            reset_case()
            fire(prefix + (' ' + text if text else ''))
            try:
                return arrival()
            except AssertionError as e:
                raise AssertionError('%s [for %r]' % (e, text))

        def folder_call(path):
            reset_case()
            fire(folder_cmd.replace('%1', path))
            try:
                return arrival()
            except AssertionError as e:
                raise AssertionError('%s [for folder %r]' % (e, path))

        def expect(got, *want):
            assert got == list(want), f'got {got!r}, want {list(want)!r}'

        spaced = str(sandbox)
        expect(folder_call(spaced), 'arrive-folder\t%s\t' % spaced)
        expect(folder_call('C:\\'), 'arrive-folder\tC:\\\t')     # the shell's own form: "C:\" swallows its closing quote
        expect(launcher_call('C:'),'arrive-folder\tC:\\\t')
        expect(launcher_call('C:\\'), 'arrive-folder\tC:\\\t')
        file = sandbox / 'notes file.txt'
        expect(launcher_call('/select,"%s"' % file), 'arrive-folder\t%s\tnotes file.txt' % spaced)
        expect(launcher_call('/select, "%s"' % file), 'arrive-folder\t%s\tnotes file.txt' % spaced)
        expect(launcher_call('/e,/select,"%s"' % file), 'arrive-folder\t%s\tnotes file.txt' % spaced)
        expect(launcher_call('/select,"C:\\Windows"'), 'arrive-folder\tC:\\\tWindows')
        expect(launcher_call('/select,C:\\'), 'arrive-folder\tC:\\\t')
        for text in win_e_cases:
            expect(launcher_call(text), 'arrive-home\t\t')
        expect(launcher_call('::{20D04FE0-3AEA-1069-A2D8-08002B30309D}'), 'arrive-place\t4\t')
        expect(launcher_call('shell:MyComputerFolder'), 'arrive-place\t4\t')
        expect(launcher_call('::{645FF040-5081-101B-9F08-00AA002F954E}'), 'arrive-place\t3\t')
        expect(launcher_call('shell:RecycleBinFolder'), 'arrive-place\t3\t')
        expect(launcher_call('shell:Recent'), 'arrive-place\t1\t')
        expect(launcher_call('shell:Downloads'), 'arrive-folder\t%s\t' % downloads)
        for text, argv in (('shell:ControlPanelFolder', 'shell:ControlPanelFolder'),
                           ('::{26EE0668-A00A-44D7-9371-BEB064C98683}', '::{26EE0668-A00A-44D7-9371-BEB064C98683}'),
                           ('ms-settings:', 'ms-settings:'),
                           ('"C:\\no\\such folder"', 'C:\\no\\such folder')):
            got = launcher_call(text)
            assert len(got) == 1 and got[0].startswith('explorer\t'), f'{text}: {got!r}'
            wait_for('The Explorer stand-in was not called for ' + text, lambda: standin_lines())
            assert standin_lines() == [argv], f'{text}: the stand-in got {standin_lines()!r}'
        reset_case()
        assert off() == 0 and dump() == before, 'Keys differ after the forms'

    def loop_guard():
        seed_rich()
        assert on() == 0
        prefix = registered_prefix()
        launcher = str(LOCAL / 'dio-file-manager-launcher.exe')
        reset_case()
        # a stand-in that calls the registered command back with the same arguments, five times at most
        e = dict(env(), STANDIN_RELAUNCH=launcher, STANDIN_BASE=str(BASE))
        subprocess.Popen(prefix + ' shell:ControlPanelFolder', env=e, cwd=ROOT, creationflags=NO_WINDOW)
        wait_for('The loop was not refused: %r' % lines(), lambda: any(l.startswith('refused') for l in lines()))
        time.sleep(2.5)
        assert len(standin_lines()) == 1, 'The stand-in was called %d times' % len(standin_lines())
        assert len([l for l in lines() if l.startswith('explorer')]) == 1, 'Explorer was handed the place more than once'
        # the same request twice by hand: the second one is refused with exit 4
        reset_case()
        assert exe_run(['shell:ControlPanelFolder', '--test-state', str(BASE)]) == 0
        assert exe_run(['shell:ControlPanelFolder', '--test-state', str(BASE)]) == 4, 'The second request was not refused'
        time.sleep(2.2)
        assert exe_run(['shell:ControlPanelFolder', '--test-state', str(BASE)]) == 0, 'A request two seconds later was refused'
        assert off() == 0

    def exe_gone():
        seed_rich()
        before = dump()
        gone = APP_DIR / 'gone.exe'
        shutil.copyfile(EXE, gone)
        assert exe_run(['--default-fm', 'on', '--test-state', str(BASE)], exe=gone) == 0
        assert state_value('App') == str(gone)
        prefix = registered_prefix()
        gone.unlink()
        reset_case()
        p = fire(prefix + ' shell:Downloads')
        assert p.wait(timeout=15) == 0, 'The launcher failed without the app'
        got = arrival()
        assert any(l.startswith('restored') for l in got), f'No restore logged: {got!r}'
        assert any(l.startswith('explorer\t') for l in got), f'Explorer was not handed the request: {got!r}'
        wait_for('The stand-in was not called', lambda: standin_lines())
        assert standin_lines() == ['shell:Downloads'], standin_lines()
        assert dump() == before, 'Keys not restored exactly:\n%s' % ([r for r in dump() if r not in before] + [r for r in before if r not in dump()])
        assert state_value('App') is None and status() == 10

    def start_app(exe):
        """Start the app on the sandbox like a normal start and wait until its start-up work, the sync included, is done."""
        frames = Path(str(BASE) + '.frames')
        for _ in range(60):
            try:
                frames.unlink(missing_ok=True)
                break
            except PermissionError:
                time.sleep(.05)
        p = subprocess.Popen([str(exe), '--path', str(sandbox), '--test-state', str(BASE)], env=env(), cwd=ROOT, creationflags=NO_WINDOW)
        try:
            wait_for('The app did not finish starting', lambda: frames.exists(), seconds=15)
        except BaseException:
            p.kill(); p.wait()
            raise
        time.sleep(.3)
        return p

    def stale_copy_of(exe):
        """The launcher of another build with the same size: one byte of the exe is different."""
        data = bytearray(exe.read_bytes())
        data[-1] ^= 0xFF
        return bytes(data)

    launcher_file = LOCAL / 'dio-file-manager-launcher.exe'

    def exe_moved():
        """The registered exe is gone and the same build starts from another folder: nothing follows it. Turning the
        switch on from the new place registers it and brings the launcher along."""
        seed_rich()
        before = dump()
        first, second = APP_DIR / 'old' / 'dio-file-manager.exe', APP_DIR / 'new' / 'dio-file-manager.exe'
        first.parent.mkdir(exist_ok=True); second.parent.mkdir(exist_ok=True)
        shutil.copyfile(EXE, first); shutil.copyfile(EXE, second)
        assert exe_run(['--default-fm', 'on', '--test-state', str(BASE)], exe=first) == 0
        assert state_value('App') == str(first)
        kill_resident()   # a running exe cannot be written
        launcher_file.write_bytes(b'stale')   # a launcher of another version
        registered = dump()
        first.unlink()
        start_app(second).kill()
        assert state_value('App') == str(first), 'A copy at another path took over the registration'
        assert launcher_file.read_bytes() == b'stale' and dump() == registered, 'A copy at another path changed the launcher or the keys'
        assert exe_run(['--default-fm', 'on', '--test-state', str(BASE)], exe=second) == 0
        assert state_value('App') == str(second) and launcher_file.read_bytes() == second.read_bytes()
        reset_case()
        fire(registered_prefix() + ' "%s"' % sandbox)
        assert arrival() == ['arrive-folder	%s	' % sandbox], 'The launcher did not start the moved app'
        reset_case()
        assert exe_run(['--default-fm', 'off', '--test-state', str(BASE)], exe=second) == 0 and dump() == before

    def registered_build_updates_launcher():
        """A build of the same size that differs in content reaches the launcher when it starts from the registered
        path, also when that path is spelled another way."""
        seed_rich()
        reg = APP_DIR / 'reg' / 'dio-file-manager.exe'
        reg.parent.mkdir(exist_ok=True)
        shutil.copyfile(EXE, reg)
        assert exe_run(['--default-fm', 'on', '--test-state', str(BASE)], exe=reg) == 0
        stale = stale_copy_of(reg)
        kill_resident()
        launcher_file.write_bytes(stale)
        assert len(stale) == reg.stat().st_size and stale != reg.read_bytes()
        spelled = Path(str(APP_DIR / 'old' / '..' / 'reg' / 'DIO-FILE-MANAGER.EXE'))
        (APP_DIR / 'old').mkdir(exist_ok=True)
        p = start_app(spelled)
        try:
            wait_for('The same-size launcher was not refreshed', lambda: launcher_file.read_bytes() == reg.read_bytes(), seconds=5)
        finally:
            p.kill(); p.wait()
        assert not (LOCAL / 'dio-file-manager-launcher.exe.new').exists(), 'The temporary copy was left behind'
        assert off() == 0

    def other_copy_leaves_launcher_alone():
        """A build started from any other path, and the launcher itself, leave the launcher and every key as they are."""
        seed_rich()
        reg, other = APP_DIR / 'reg' / 'dio-file-manager.exe', APP_DIR / 'other' / 'dio-file-manager.exe'
        reg.parent.mkdir(exist_ok=True); other.parent.mkdir(exist_ok=True)
        shutil.copyfile(EXE, reg)
        assert exe_run(['--default-fm', 'on', '--test-state', str(BASE)], exe=reg) == 0
        other.write_bytes(reg.read_bytes() + bytes(1024))   # a larger build, like an unfinished feature
        kill_resident()
        launcher_file.write_bytes(b'launcher of the installed app')
        registered = dump()
        start_app(other).kill()
        assert launcher_file.read_bytes() == b'launcher of the installed app', 'A build at another path replaced the launcher'
        assert state_value('App') == str(reg) and dump() == registered, 'A build at another path changed the keys or the record'
        assert off() == 0

    def upgrade_from_three_targets():
        """A registration made before the open verb was added: opening the new version, or turning the switch on again,
        saves the open verb as it was and registers it, and turning off puts everything back."""
        seed_rich()
        before = dump()

        def make_old():
            assert exe_run(['--default-fm', 'on', '--test-state', str(BASE)]) == 0
            for key in (CLASSES + '\\CLSID\\' + WIN_E + '\\shell\\open', STATE + '\\Saved\\3'):
                subprocess.run(['reg', 'delete', 'HKCU\\' + key, '/f'], capture_output=True, creationflags=NO_WINDOW)
            assert command_of(WIN_E_OPEN) is None, 'The test could not make the old registration'

        def is_ours(rel):
            return 'launcher' in (command_of(rel) or '')

        make_old()
        p = subprocess.Popen([str(APP_EXE), '--path', str(sandbox), '--test-state', str(BASE)], env=env(), cwd=ROOT, creationflags=NO_WINDOW)
        try:
            wait_for('The open verb was not added when the app started', lambda: is_ours(WIN_E_OPEN))
        finally:
            p.kill(); p.wait()
        assert off() == 0 and dump() == before, 'Off after the start did not restore'
        make_old()
        assert exe_run(['--default-fm', 'on', '--test-state', str(BASE)]) == 0 and is_ours(WIN_E_OPEN), 'Turning on did not add the open verb'
        assert off() == 0 and dump() == before, 'Off after turning on again did not restore'

    def switch_in_menu():
        import input_smoke as base
        base.EXE = APP_EXE
        seed_rich()
        before = dump()
        a = base.App('default-fm-ui', sandbox, 'grid', 'light')
        try:
            s = a.snap()
            assert s['default_fm'] == 0, 'The switch starts on'
            bx, by, bw, bh = s['menu_button']
            assert bw > 0, 'No main menu button'

            def toggle(want):
                a.click(bx + bw / 2, by + bh / 2)
                time.sleep(.5)
                x, y, w, h = a.snap('menu-open-%d' % want)['dfm_item']
                assert w > 0 and h > 0, 'The menu item was not laid out'
                a.click(x + w / 2, y + h / 2)
                a.until('The switch did not go to %d' % want, lambda: a.snap()['default_fm'] == want)

            toggle(1)
            assert command_of(FOLDER_OPEN) and 'dio-file-manager-launcher.exe' in command_of(FOLDER_OPEN), 'The click changed no registry key'
            assert command_of(WIN_E_CMD) and (LOCAL / 'dio-file-manager-launcher.exe').exists()
            toggle(0)
            settle()
            assert dump() == before, 'The second click did not restore the keys exactly'
            assert state_value('App') is None
        finally:
            a.close()
            exe_run(['--default-fm', 'off', '--test-state', str(BASE)])

    def acl_of(key, right):
        """(deny, allow): run PowerShell that denies one right on a key under HKCU to the current user, and takes the denial away."""
        acl = ("$k=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('%s','ReadWriteSubTree','ChangePermissions,ReadPermissions'); "
               "$a=$k.GetAccessControl(); $r=New-Object System.Security.AccessControl.RegistryAccessRule("
               "[System.Security.Principal.WindowsIdentity]::GetCurrent().Name,'%s','Deny'); " % (key, right))
        ps = lambda tail: subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', acl + tail],
                                         capture_output=True, timeout=60, creationflags=NO_WINDOW).returncode
        return (lambda: ps('$a.AddAccessRule($r); $k.SetAccessControl($a); $k.Close()'),
                lambda: ps('$a.RemoveAccessRule($r) | Out-Null; $k.SetAccessControl($a); $k.Close()'))

    def failed_restore_keeps_recovery():
        """A key that cannot be written back must keep the record and the launcher, and turning off must say so."""
        seed_rich()
        before = dump()
        assert on() == 0
        launcher = LOCAL / 'dio-file-manager-launcher.exe'
        deny, allow = acl_of(CLASSES + '\\' + FOLDER_OPEN, 'SetValue')
        assert deny() == 0, 'Could not deny writes to the command key'
        try:
            assert off() != 0, 'Turning off said it worked while a key could not be restored'
            assert run_value() is not None and adv_value() is not None, 'The Win+E keys went back while the switch stays on'
            assert launcher.exists(), 'The launcher the commands still point to was deleted'
            assert state_value('App') is not None, 'The record of the old values was deleted'
            assert status() == 0 and 'launcher' in command_of(FOLDER_OPEN), 'The command no longer points at the launcher'
        finally:
            allow()
        assert off() == 0, 'Turning off failed once the key could be written again'
        assert dump() == before, 'Keys differ from before after the retry'
        assert not launcher.exists() and state_value('App') is None, 'The launcher or the record was left behind'

    def unreadable_record_is_a_failure():
        """A saved value the registry will not let us read must fail the turn-off and keep everything, not count as 'no record'."""
        seed_rich()
        put(WIN_E_CMD, '', 'original-explorer.exe')   # Win+E had a command before: it is what the unreadable record holds
        before = dump()
        assert on() == 0
        launcher = LOCAL / 'dio-file-manager-launcher.exe'
        after_on = dump()
        deny, allow = acl_of(STATE + '\\Saved\\2', 'QueryValues')   # the saved Win+E record
        assert deny() == 0, 'Could not deny reads of the saved record'
        try:
            assert off() != 0, 'Turning off said it worked while a saved value could not be read'
            assert launcher.exists(), 'The launcher was deleted while Win+E still points to it'
            assert state_value('App') is not None, 'The record was deleted while one value could not be read'
            assert 'launcher' in (command_of(WIN_E_CMD) or ''), 'Win+E was changed without its saved value'
            assert status() == 0, 'The switch reads as off while Win+E is still ours'
        finally:
            allow()
        assert off() == 0, 'Turning off failed once the record could be read again'
        assert dump() == before, 'Keys differ from before after the retry'
        assert not launcher.exists() and state_value('App') is None, 'The launcher or the record was left behind'

    def failed_win_e_restore_then_retry():
        """Win+E cannot be written back while Folder can: the switch stays on, the next click retries, the record is never replaced."""
        seed_rich()
        put(WIN_E_CMD, '', 'original-explorer.exe')   # a Win+E command that has to be written back
        before = dump()
        assert on() == 0
        launcher = LOCAL / 'dio-file-manager-launcher.exe'
        saved = dump(STATE)
        deny, allow = acl_of(CLASSES + '\\' + WIN_E_CMD, 'SetValue')
        assert deny() == 0, 'Could not deny writes to the Win+E key'
        try:
            assert off() != 0, 'Turning off said it worked while Win+E could not be restored'
            assert 'launcher' not in (command_of(FOLDER_OPEN) or ''), 'The test needs Folder to be restored'
            assert 'launcher' in (command_of(WIN_E_CMD) or ''), 'The test needs Win+E to still be ours'
            assert status() == 0, 'The switch reads as off while Win+E still points to the launcher'
            assert launcher.exists(), 'The launcher Win+E points to was deleted'
            assert dump(STATE) == saved, 'The record of the old values changed'
            on()   # a turn-on on top of the waiting one (it cannot write Win+E either) must not replace the record
            assert dump(STATE) == saved, 'A turn-on replaced the record of the old values'
            assert off() != 0, 'Turning off said it worked while Win+E could still not be restored'
            assert dump(STATE) == saved and launcher.exists(), 'The second failed turn-off lost the record or the launcher'
        finally:
            allow()
        assert off() == 0, 'The retry failed once the key could be written again'
        assert dump() == before, 'Keys differ from before after the retry'
        assert status() == 10 and not launcher.exists() and state_value('App') is None, 'The launcher or the record was left behind'
        assert on() == 0 and off() == 0 and dump() == before, 'A later on/off cycle left a difference'

    def other_programs_entries_survive():
        """A class another program registers while the switch is on stays when it goes off."""
        seed_clean()
        before = dump()
        assert on() == 0
        other = CLASSES + r'\CLSID\{00000000-0000-0000-0000-000000000099}'
        with winreg.CreateKeyEx(HIVE, other, 0, winreg.KEY_WRITE) as k:
            winreg.SetValueEx(k, 'Name', 0, winreg.REG_SZ, 'registered later')
        assert off() == 0, 'Turning off failed'
        assert command_of(WIN_E_CMD) is None and not [r for r in dump() if WIN_E in r[0]], 'The Win+E keys were left behind'
        with winreg.OpenKey(HIVE, other) as k:
            assert winreg.QueryValueEx(k, 'Name')[0] == 'registered later', 'The other program\'s class was erased'
        winreg.DeleteKey(HIVE, other)
        winreg.DeleteKey(HIVE, CLASSES + r'\CLSID')
        assert dump() == before, 'Keys differ from before'

    def long_names():
        """Unicode names that fit Windows' limits open the folder, and a long file name is selected in the window."""
        import input_smoke as base
        deep = sandbox / ('目' * 150)
        deep.mkdir(exist_ok=True)
        longname = '文' * 90 + '.txt'
        (sandbox / longname).write_text('x', encoding='utf-8')
        seed_rich()
        assert on() == 0
        reset_case()
        fire(registered_prefix() + ' "%s"' % deep)
        got = arrival()
        assert len(got) == 1 and got[0].startswith('arrive-folder\t' + str(sandbox)), \
            f'The long folder did not arrive as a folder: {got!r}'   # the log is ANSI: its CJK letters are "?"
        assert off() == 0
        real = base.subprocess.Popen
        base.EXE = APP_EXE
        base.subprocess.Popen = lambda cmd, **kw: real([cmd[0], '--view', cmd[4], '--test-state', cmd[6], '--theme', cmd[8],
                                                         '/select,' + str(sandbox / longname)], **kw)
        try:
            a = base.App('default-fm-long', sandbox, 'grid', 'light')
        finally:
            base.subprocess.Popen = real
        try:
            a.until('The long file name was not selected', lambda: a.snap()['selected'] == 1, seconds=10)
        finally:
            a.close()


    # ---- Win+E: the resident process, DisabledHotkeys and the Run entry ----

    def want_run():
        return '"%s" --resident --test-state "%s"' % (launcher_file, BASE)

    def resident_up():
        wait_for('The resident process did not take Win+E: %r' % res_lines(), lambda: has_line('hotkey-ok'))
        return res_pids()[-1]

    def working_set(pid):
        import psutil
        return psutil.Process(pid).memory_info().wset

    def resident_keys_and_process():
        """Turning on starts the process, writes the Run entry and the hotkey value; a posted WM_HOTKEY opens a window; off undoes all."""
        seed_rich()
        before = dump()
        assert on() == 0
        pid = resident_up()
        assert run_value() == want_run(), 'Run entry: %r' % run_value()
        assert adv_value() == ('E', winreg.REG_SZ), 'DisabledHotkeys: %r' % (adv_value(),)
        assert pid_alive(pid), 'The resident process is not running'
        time.sleep(.5)
        assert working_set(pid) <= 5 * 1024 * 1024, 'Working set %d bytes' % working_set(pid)
        post_hotkey()
        got = arrival()
        assert got == ['arrive-home\t\t'], 'A posted Win+E did not open Home: %r' % got
        assert working_set(pid) <= 5 * 1024 * 1024, 'Working set after a hotkey %d bytes' % working_set(pid)
        kill_apps()
        assert on() == 0 and pid_alive(pid) and res_pids() == [pid], 'Turning on twice started a second process'
        assert off() == 0
        for _ in range(100):
            if not pid_alive(pid):
                break
            time.sleep(.05)
        assert not pid_alive(pid), 'The process still runs after turning off'
        assert run_value() is None and adv_value() is None, 'Run entry or DisabledHotkeys left: %r %r' % (run_value(), adv_value())
        assert dump() == before, 'Keys differ from before:\n%s' % ([r for r in dump() if r not in before] + [r for r in before if r not in dump()])
        assert not launcher_file.exists(), 'The launcher was left behind while its process was running'

    def disabled_hotkeys_values():
        """E is added to what is there and the old value, its type or its absence comes back exactly."""
        for old, label in ((None, 'absent'), (('XR', winreg.REG_SZ), 'letters'), (('Xr', winreg.REG_EXPAND_SZ), 'expand type'),
                           (('ZeX', winreg.REG_SZ), 'already has E')):
            seed_rich()
            set_adv(old)
            before = dump()
            assert on() == 0, label
            want = ('E', winreg.REG_SZ) if old is None else (old[0] if 'E' in old[0].upper() else old[0] + 'E', old[1])
            assert adv_value() == want, '%s: %r is not %r' % (label, adv_value(), want)
            after_on = dump()
            assert on() == 0 and dump() == after_on, label + ': turning on twice changed the keys'
            assert off() == 0, label
            assert adv_value() == old, '%s: back as %r, not %r' % (label, adv_value(), old)
            assert dump() == before, label + ': keys differ from before'
            reset_case()

    def hotkey_busy_then_taken():
        """While Explorer holds Win+E the process says so (1409) and waits; when Explorer restarts (TaskbarCreated) it takes it."""
        import threading
        user32 = ctypes.windll.user32
        seed_rich()
        ready, stop, held = threading.Event(), threading.Event(), []

        def holder():
            held.append(bool(user32.RegisterHotKey(None, 77, 0x4000 | 0xF, 0x86)))   # Win+Ctrl+Alt+Shift+F23: the key a check uses instead of Win+E
            ready.set()
            stop.wait()
            user32.UnregisterHotKey(None, 77)

        t = threading.Thread(target=holder)
        t.start()
        try:
            assert ready.wait(5) and held == [True], 'The test could not hold Win+E'
            assert on() == 0
            wait_for('No refusal logged: %r' % res_lines(), lambda: has_line('hotkey-busy'))
            assert 'hotkey-busy\t1409' in res_lines() and not has_line('hotkey-ok'), res_lines()
            stop.set(); t.join()
            user32.RegisterWindowMessageW.restype = ctypes.c_uint
            msg = user32.RegisterWindowMessageW('TaskbarCreated')
            user32.SendNotifyMessageW(ctypes.c_void_p(0xFFFF), msg, 0, 0)   # as Explorer does, once
            wait_for('The hotkey was not taken after TaskbarCreated: %r' % res_lines(), lambda: has_line('hotkey-ok'), seconds=5)
        finally:
            stop.set(); t.join()
        assert off() == 0

    def old_install_gets_resident():
        """An install made before the Win+E process existed gets the keys and the process when its app starts."""
        seed_rich()
        before = dump()
        assert on() == 0
        resident_up()
        kill_resident()
        subprocess.run(['reg', 'delete', 'HKCU\\' + RUN, '/v', RUN_NAME, '/f'], capture_output=True, creationflags=NO_WINDOW)
        subprocess.run(['reg', 'delete', 'HKCU\\' + STATE + '\\Saved\\DisabledHotkeys', '/f'], capture_output=True, creationflags=NO_WINDOW)
        set_adv(None)
        RESIDENT.unlink(missing_ok=True)
        assert run_value() is None and adv_value() is None
        p = start_app(APP_EXE)
        try:
            wait_for('The old install did not get its keys', lambda: run_value() == want_run() and adv_value() == ('E', winreg.REG_SZ))
            resident_up()
        finally:
            p.kill(); p.wait()
        assert off() == 0 and dump() == before, 'Off after the upgrade did not restore'

    def update_while_resident():
        """The hard case: the app is replaced by a new build while the process runs from the launcher. The launcher follows,
        the process restarts from the new launcher, and a turn-on from a newer build works the same way."""
        seed_rich()
        before = dump()
        reg = APP_DIR / 'upd' / 'dio-file-manager.exe'
        reg.parent.mkdir(exist_ok=True)
        shutil.copyfile(EXE, reg)
        assert exe_run(['--default-fm', 'on', '--test-state', str(BASE)], exe=reg) == 0
        first = resident_up()
        reg.write_bytes(EXE.read_bytes() + bytes(16))   # the installer writes the new build over the app
        assert launcher_file.read_bytes() != reg.read_bytes()
        p = start_app(reg)
        try:
            wait_for('The launcher did not become the new build', lambda: launcher_file.read_bytes() == reg.read_bytes(), seconds=10)
            wait_for('The process did not restart from the new launcher', lambda: len(res_pids()) >= 2 and pid_alive(res_pids()[-1]), seconds=10)
        finally:
            p.kill(); p.wait()
        assert not pid_alive(first), 'The old process still runs'
        second = res_pids()[-1]
        assert not (LOCAL / 'dio-file-manager-launcher.exe.new').exists(), 'The temporary copy was left behind'
        assert launcher_file.read_bytes() == reg.read_bytes() and run_value() == want_run()
        wait_for('The new process did not take Win+E', lambda: res_lines()[-1].startswith('hotkey-ok'))
        post_hotkey()
        assert arrival() == ['arrive-home\t\t'], 'The new process did not open a window'
        kill_apps()
        reg.write_bytes(reg.read_bytes() + bytes(16))
        assert exe_run(['--default-fm', 'on', '--test-state', str(BASE)], exe=reg) == 0, 'A turn-on from a newer build failed while the process runs'
        assert launcher_file.read_bytes() == reg.read_bytes() and not pid_alive(second) and pid_alive(res_pids()[-1]), 'The newer turn-on left the old process or launcher'
        assert off() == 0 and dump() == before and not launcher_file.exists()

    def app_gone_while_resident():
        """Win+E with the app deleted: the keys go back, Explorer is handed the request and the process ends."""
        seed_rich()
        before = dump()
        gone = APP_DIR / 'gone2.exe'
        shutil.copyfile(EXE, gone)
        assert exe_run(['--default-fm', 'on', '--test-state', str(BASE)], exe=gone) == 0
        pid = resident_up()
        gone.unlink()
        post_hotkey()
        wait_for('The process did not notice the app is gone: %r' % res_lines(), lambda: has_line('hotkey-gone'))
        wait_for('The stand-in was not called', lambda: standin_lines())
        assert dump() == before, 'Keys not restored:\n%s' % ([r for r in dump() if r not in before] + [r for r in before if r not in dump()])
        assert pid_alive(pid), 'The process ended with the keys back, so Win+E does nothing until Explorer restarts'
        post_hotkey()
        wait_for('The second press did not open Explorer', lambda: len(standin_lines()) == 2)
        explorer_restarts()
        wait_for('The process did not end at the Explorer restart', lambda: not pid_alive(pid))
        assert has_line('bridge-exit')

    def bridge_after_turn_off():
        """The case a user meets: Explorer already restarted with E disabled, then the switch is turned off. Win+E must still open
        something. The process stays on, opens Windows Explorer on Win+E, and lets go of the hotkey and ends when Explorer
        restarts; the launcher goes at the next sign-in."""
        seed_rich()
        before = dump()
        assert on() == 0
        pid = resident_up()
        old_explorer = fake_explorer()
        try:
            bridge_after_turn_off_body(pid, before, old_explorer)
        finally:
            old_explorer.kill()

    def bridge_after_turn_off_body(pid, before, old_explorer):
        assert off_raw() == 0
        assert pid_alive(pid), 'The process ended with the switch: Win+E does nothing until Explorer restarts'
        assert run_value() is None and adv_value() is None, 'The keys are not back'
        assert launcher_file.exists(), 'The launcher was deleted while the process runs from it'
        want = '"%s" --cleanup-launcher --test-state "%s"' % (APP_EXE, BASE)
        assert runonce_value() == want, 'Sign-in cleanup entry: %r' % runonce_value()
        wait_for('The process did not become a bridge: %r' % res_lines(), lambda: has_line('bridge'))
        post_hotkey()
        wait_for('Win+E did not open Explorer: %r' % res_lines(), lambda: len(standin_lines()) == 1)
        assert standin_lines() == [''], 'Explorer was started with arguments: %r' % standin_lines()
        assert not [l for l in lines() if l.startswith('arrive-')], 'The app opened although the switch is off: %r' % lines()
        post_hotkey()
        wait_for('A second Win+E did not open a second window', lambda: len(standin_lines()) == 2)
        assert pid_alive(pid)
        explorer_restarts()   # the same taskbar owner: a display-scale change sends the message too, Explorer did not restart
        wait_for('TaskbarCreated did not arrive', lambda: has_line('taskbar-created'))
        time.sleep(.3)
        assert pid_alive(pid) and not has_line('bridge-exit'), 'The bridge ended although Explorer did not restart: %r' % res_lines()
        post_hotkey()
        wait_for('Win+E did not open Explorer after a TaskbarCreated of the same Explorer', lambda: len(standin_lines()) == 3)
        old_explorer.kill()   # Explorer ends; the new one registers its hotkeys while it starts, before it says TaskbarCreated
        wait_for('The process kept Win+E after Explorer ended: the new Explorer would find it taken and not try again', lambda: not pid_alive(pid))
        assert has_line('bridge-exit')
        user32 = ctypes.windll.user32
        assert user32.RegisterHotKey(None, 78, 0x4000 | 0xF, 0x86), 'The hotkey is still held after the process ended'
        user32.UnregisterHotKey(None, 78)
        assert launcher_file.exists(), 'The launcher went before the sign-in'
        sign_in()
        assert not launcher_file.exists() and runonce_value() is None, 'The sign-in cleanup left the launcher'
        assert dump() == before, 'Keys differ from before:\n%s' % ([r for r in dump() if r not in before] + [r for r in before if r not in dump()])

    def turn_on_after_turn_off():
        """Turning the switch on again while the bridge runs: the pending launcher cleanup is dropped, the bridge is replaced by a
        process that opens the app, and a cleanup that still fires finds the switch on and leaves the launcher alone."""
        seed_rich()
        before = dump()
        assert on() == 0
        first = resident_up()
        assert off_raw() == 0 and pid_alive(first)
        cmd = runonce_value()
        assert cmd
        assert on() == 0
        assert runonce_value() is None, 'The cleanup entry is still there with the switch on'
        wait_for('The bridge did not end', lambda: not pid_alive(first))
        wait_for('The new process did not take Win+E', lambda: sum(l.startswith('hotkey-ok') for l in res_lines()) >= 2)
        second = res_pids()[-1]
        assert second != first and pid_alive(second)
        subprocess.run(cmd, env=env(), cwd=ROOT, creationflags=NO_WINDOW, timeout=15)   # a cleanup that fires anyway
        assert launcher_file.exists(), 'The cleanup deleted the launcher of a switch that is on'
        post_hotkey()
        assert arrival() == ['arrive-home\t\t'], 'Win+E did not open the app after turning on again'
        assert not standin_lines(), 'Explorer was started although the switch is on'
        kill_apps()
        assert off() == 0 and dump() == before and not launcher_file.exists()

    def bridge_watches_late_explorer():
        """A turn-off while no Explorer runs: the first TaskbarCreated names the Explorer to watch. It already lost Win+E to the
        bridge, so the bridge keeps serving it until that Explorer ends, and then lets go."""
        seed_rich()
        assert on() == 0
        pid = resident_up()
        assert off_raw() == 0
        wait_for('The process did not become a bridge: %r' % res_lines(), lambda: has_line('bridge'))
        late = fake_explorer()
        try:
            explorer_restarts()
            wait_for('TaskbarCreated did not arrive', lambda: has_line('taskbar-created'))
            time.sleep(.3)
            assert pid_alive(pid) and not has_line('bridge-exit'), 'The bridge ended at the restart that had already lost Win+E to it: %r' % res_lines()
            post_hotkey()
            wait_for('Win+E did not open Explorer', lambda: len(standin_lines()) == 1)
            late.kill()
            wait_for('The process kept Win+E after that Explorer ended', lambda: not pid_alive(pid))
        finally:
            late.kill()
        sign_in()

    def app_gone_with_failed_restore():
        """Win+E with the app deleted and a command that cannot be put back: the process keeps Win+E, E and the Run entry stay,
        this press opens Explorer, and the next press, once the key can be written, puts everything back."""
        seed_rich()
        before = dump()
        gone = APP_DIR / 'gone3.exe'
        shutil.copyfile(EXE, gone)
        assert exe_run(['--default-fm', 'on', '--test-state', str(BASE)], exe=gone) == 0
        pid = resident_up()
        gone.unlink()
        deny, allow = acl_of(CLASSES + '\\' + FOLDER_OPEN, 'SetValue')
        assert deny() == 0, 'Could not deny writes to the command key'
        try:
            post_hotkey()
            wait_for('The process did not notice the app is gone: %r' % res_lines(), lambda: has_line('hotkey-gone'))
            wait_for('The stand-in was not called', lambda: standin_lines())
            time.sleep(.3)
            assert pid_alive(pid), 'The process ended with the keys still in place: Win+E does nothing'
            assert run_value() is not None and adv_value() is not None, 'The Win+E keys went back while a command did not'
        finally:
            allow()
        post_hotkey()
        wait_for('The second press did not open Explorer', lambda: len(standin_lines()) == 2)
        wait_for('The keys did not go back once they could', lambda: run_value() is None and adv_value() is None)
        assert pid_alive(pid)
        assert dump() == before, 'Keys differ from before:\n%s' % ([r for r in dump() if r not in before] + [r for r in before if r not in dump()])
        settle()

    def resident_start_cost():
        """Budget: a posted Win+E shows the window at most 50 ms later than a plain launcher start (median of 7); idle CPU is 0."""
        import statistics
        seed_rich()
        assert on() == 0
        resident_up()
        prefix = registered_prefix()

        def wait_home(t0):
            deadline = time.perf_counter() + 20
            while time.perf_counter() < deadline:
                if any(l.startswith('arrive-home') for l in lines()):
                    return (time.perf_counter() - t0) * 1000
                time.sleep(.002)
            raise AssertionError('No window: %r' % lines())

        plain, hot = [], []
        for _ in range(7):
            kill_apps(); OPENED.unlink(missing_ok=True)
            t0 = time.perf_counter()
            fire(prefix)
            plain.append(wait_home(t0))
            kill_apps(); OPENED.unlink(missing_ok=True)
            time.sleep(.3)
            t0 = time.perf_counter()
            post_hotkey()
            hot.append(wait_home(t0))
        kill_apps()
        mp, mh = statistics.median(plain), statistics.median(hot)
        print(json.dumps({'budget-numbers': {'plain launcher start ms': round(mp, 1), 'posted Win+E ms': round(mh, 1)}}), flush=True)
        assert mh <= mp + 50, 'Posted Win+E %.1f ms vs plain launcher start %.1f ms' % (mh, mp)
        pid = res_pids()[-1]
        import psutil
        pr = psutil.Process(pid)
        c0 = sum(pr.cpu_times()[:2]); time.sleep(3); c1 = sum(pr.cpu_times()[:2])
        assert c1 - c0 == 0, 'The idle process used %.3f s of CPU in 3 s' % (c1 - c0)
        print(json.dumps({'budget-numbers': {'resident working set KB': working_set(pid) // 1024, 'idle CPU seconds in 3 s': c1 - c0}}), flush=True)
        assert working_set(pid) <= 5 * 1024 * 1024
        if os.environ.get('DFM_CPU_BUDGET'):   # the machine's own budget tool: five 30-second windows, all must be at 0 %
            tool = subprocess.run([sys.executable, r'C:\Users\Omid\.agents\tools\cpu_budget.py', '--pid', str(pid), '--max', '0'],
                                  capture_output=True, text=True, creationflags=NO_WINDOW, timeout=300)
            (WORK / 'cpu_budget.out').write_text(tool.stdout + tool.stderr, encoding='utf-8')
            assert tool.returncode == 0, 'cpu_budget.py: %s' % tool.stdout[-400:]
        assert off() == 0

    try:
        check('1-on-off-restores-values', on_off(seed_rich, 'rich'))
        check('2-on-off-on-clean-keys', on_off(seed_clean, 'clean'))
        check('3-every-command-line-form', forms)
        check('4-loop-guard', loop_guard)
        check('5-app-gone-restores-and-hands-over', exe_gone)
        check('6-exe-moved-is-not-followed', exe_moved)
        check('7-switch-in-main-menu', switch_in_menu)
        check('8-failed-restore-keeps-recovery', failed_restore_keeps_recovery)
        check('9-other-programs-entries-survive', other_programs_entries_survive)
        check('10-long-unicode-names', long_names)
        check('11-unreadable-saved-value-keeps-recovery', unreadable_record_is_a_failure)
        check('12-failed-win-e-restore-retries-and-keeps-record', failed_win_e_restore_then_retry)
        check('13-upgrade-from-three-targets', upgrade_from_three_targets)
        check('14-registered-build-updates-same-size-launcher', registered_build_updates_launcher)
        check('15-other-copy-leaves-launcher-and-keys', other_copy_leaves_launcher_alone)
        check('16-win-e-process-keys-and-hotkey', resident_keys_and_process)
        check('17-disabled-hotkeys-kept-and-restored-exactly', disabled_hotkeys_values)
        check('18-hotkey-busy-then-taken-after-explorer-restart', hotkey_busy_then_taken)
        check('19-old-install-gets-the-process', old_install_gets_resident)
        check('20-update-while-the-process-runs', update_while_resident)
        check('21-app-gone-while-the-process-runs', app_gone_while_resident)
        check('22-win-e-start-cost-and-idle-budget', resident_start_cost)
        check('23-win-e-still-works-after-turn-off-then-ends-at-explorer-restart', bridge_after_turn_off)
        check('24-turn-on-again-replaces-the-bridge', turn_on_after_turn_off)
        check('25-bridge-watches-an-explorer-that-starts-after-the-turn-off', bridge_watches_late_explorer)
        check('26-app-gone-with-a-failed-restore-keeps-win-e', app_gone_with_failed_restore)
    finally:
        exe_run(['--default-fm', 'off', '--test-state', str(BASE)])
        wipe()
        (WORK / 'default-fm-results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    assert not dump(), 'The test key is still there'
    return int(any(r['result'] != 'passed' for r in results))


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
    with (WORK / 'default-fm.log').open('wb') as out, (WORK / 'default-fm.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=240)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

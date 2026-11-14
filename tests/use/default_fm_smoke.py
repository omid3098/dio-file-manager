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


def seed_clean():
    wipe()
    put('', None, None)


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


def kill_started():
    for line in lines():
        if line.startswith('pid\t'):
            subprocess.run(['taskkill', '/F', '/PID', line.split('\t')[1]], capture_output=True, creationflags=NO_WINDOW)


def reset_case():
    kill_started()
    for f in (OPENED, STAND_LOG, LOCAL / 'fallback.txt'):
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

    def real_keys():
        return [dump(p, p) for p in (r'Software\Classes\Folder', r'Software\Classes\CLSID\%s' % WIN_E, r'Software\Dio File Manager')]

    def check(name, fn):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
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
    off = lambda: exe_run(['--default-fm', 'off', '--test-state', str(BASE)])
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

    def exe_moved():
        seed_rich()
        before = dump()
        first, second = APP_DIR / 'old' / 'dio-file-manager.exe', APP_DIR / 'new' / 'dio-file-manager.exe'
        first.parent.mkdir(exist_ok=True); second.parent.mkdir(exist_ok=True)
        shutil.copyfile(EXE, first); shutil.copyfile(EXE, second)
        assert exe_run(['--default-fm', 'on', '--test-state', str(BASE)], exe=first) == 0
        assert state_value('App') == str(first)
        (LOCAL / 'dio-file-manager-launcher.exe').write_bytes(b'stale')   # a launcher of another version
        first.unlink()
        p = subprocess.Popen([str(second), '--path', str(sandbox), '--test-state', str(BASE)], env=env(), cwd=ROOT, creationflags=NO_WINDOW)
        try:
            wait_for('The app did not follow the moved exe', lambda: state_value('App') == str(second))
            wait_for('The launcher copy was not refreshed',
                     lambda: (LOCAL / 'dio-file-manager-launcher.exe').stat().st_size == second.stat().st_size)
        finally:
            p.kill(); p.wait()
        reset_case()
        fire(registered_prefix() + ' "%s"' % sandbox)
        assert arrival() == ['arrive-folder\t%s\t' % sandbox], 'The launcher did not start the moved app'
        reset_case()
        assert exe_run(['--default-fm', 'off', '--test-state', str(BASE)], exe=second) == 0 and dump() == before

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
        assert exe_run(['--default-fm', 'off', '--test-state', str(BASE)]) == 0 and dump() == before, 'Off after the start did not restore'
        make_old()
        assert exe_run(['--default-fm', 'on', '--test-state', str(BASE)]) == 0 and is_ours(WIN_E_OPEN), 'Turning on did not add the open verb'
        assert exe_run(['--default-fm', 'off', '--test-state', str(BASE)]) == 0 and dump() == before, 'Off after turning on again did not restore'

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
            f'The long folder did not arrive as a folder: {got!r}'   # the log is ANSI: it ends at the first CJK letter
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

    try:
        check('1-on-off-restores-values', on_off(seed_rich, 'rich'))
        check('2-on-off-on-clean-keys', on_off(seed_clean, 'clean'))
        check('3-every-command-line-form', forms)
        check('4-loop-guard', loop_guard)
        check('5-app-gone-restores-and-hands-over', exe_gone)
        check('6-exe-moved-is-followed', exe_moved)
        check('7-switch-in-main-menu', switch_in_menu)
        check('8-failed-restore-keeps-recovery', failed_restore_keeps_recovery)
        check('9-other-programs-entries-survive', other_programs_entries_survive)
        check('10-long-unicode-names', long_names)
        check('11-unreadable-saved-value-keeps-recovery', unreadable_record_is_a_failure)
        check('12-failed-win-e-restore-retries-and-keeps-record', failed_win_e_restore_then_retry)
        check('13-upgrade-from-three-targets', upgrade_from_three_targets)
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

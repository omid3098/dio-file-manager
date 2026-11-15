"""Open File Location on a Windows shortcut, through Win32 input on correct/hidden.py's desktop.

Shortcuts are made in the sandbox (and a few real Start menu ones are copied, read only). The check never
touches the clipboard. Run from the repo: python tests/use/shortcut_smoke.py
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
from input_smoke import App, WORK, SAND, U  # noqa: E402

LINKS, FAR, SMALL = SAND/'links', SAND/'far', SAND/'small'
SCROLLED, DEEP = SAND/'scrolled', SAND/'deep'   # a long shortcut folder, and a long folder its shortcuts point into
FOLDERS = FAR/'folders'
PERSIAN = 'گزارش بسیار طولانی با نام فارسی برای آزمایش باز کردن محل فایل هدف در پوشه.txt'
START = Path(r'C:\ProgramData\Microsoft\Windows\Start Menu\Programs')
USER_START = Path(os.environ.get('APPDATA', '')) / r'Microsoft\Windows\Start Menu\Programs'
OFFLINE = r'\\nonexistent\share\x.txt'
OFFLINE_WAIT = 90   # seconds an unreachable share may take to answer


def make_links(pairs):
    """Write each shortcut through IShellLinkW and IPersistFile (Unicode paths; WScript.Shell cannot store a Persian one)."""
    import ctypes as C, uuid
    ole = C.OleDLL('ole32')
    ole.CoInitialize(None)
    ole.CoCreateInstance.argtypes = [C.c_void_p, C.c_void_p, C.c_ulong, C.c_void_p, C.c_void_p]
    def guid(text):
        return (C.c_ubyte * 16)(*uuid.UUID(text).bytes_le)
    CLSID, ISHELL, IPERSIST = guid('00021401-0000-0000-C000-000000000046'), guid('000214F9-0000-0000-C000-000000000046'), guid('0000010B-0000-0000-C000-000000000046')
    def call(obj, index, *types_and_args):
        vtbl = C.cast(obj, C.POINTER(C.POINTER(C.c_void_p)))[0]
        proto = C.WINFUNCTYPE(C.HRESULT, C.c_void_p, *[t for t, _ in types_and_args])
        return proto(vtbl[index])(obj, *[v for _, v in types_and_args])
    for link, target in pairs:
        shell, persist = C.c_void_p(), C.c_void_p()
        ole.CoCreateInstance(C.addressof(CLSID), None, 1,
                             C.addressof(ISHELL), C.addressof(shell))
        call(shell, 20, (C.c_wchar_p, str(target)))   # SetPath
        call(shell, 0, (C.c_void_p, C.addressof(IPERSIST)), (C.c_void_p, C.addressof(persist)))   # QueryInterface
        call(persist, 6, (C.c_wchar_p, str(link)), (C.c_int, 1))   # Save
        C.WINFUNCTYPE(C.c_ulong, C.c_void_p)(C.cast(persist, C.POINTER(C.POINTER(C.c_void_p)))[0][2])(persist)
        C.WINFUNCTYPE(C.c_ulong, C.c_void_p)(C.cast(shell, C.POINTER(C.POINTER(C.c_void_p)))[0][2])(shell)


def prepare():
    shutil.rmtree(SAND, ignore_errors=True)
    LINKS.mkdir(parents=True); FOLDERS.mkdir(parents=True); SMALL.mkdir()
    for i in range(150):
        (FAR/('f%03d.txt' % i)).write_text('x', encoding='utf-8')
        (FOLDERS/('d%03d' % i)).mkdir()
    (FAR/PERSIAN).write_text('x', encoding='utf-8')
    SCROLLED.mkdir(); DEEP.mkdir()
    for i in range(250):
        (SCROLLED/('f%03d.txt' % i)).write_text('x', encoding='utf-8')
        (DEEP/('f%03d.txt' % i)).write_text('x', encoding='utf-8')
    make_links([(SCROLLED/'f100.lnk', DEEP/'f100.txt'), (SCROLLED/'a.lnk', DEEP/'f200.txt')])
    gone = SAND/'gone.txt'
    gone.write_text('x', encoding='utf-8')
    (LINKS/'plain.txt').write_text('x', encoding='utf-8')
    make_links([(LINKS/'to-file.lnk', FAR/'f099.txt'), (LINKS/'to-folder.lnk', FOLDERS/'d099'),
                (LINKS/'to-persian.lnk', FAR/PERSIAN), (LINKS/'to-gone.lnk', gone),
                (LINKS/'to-offline.lnk', OFFLINE), (LINKS/'to-sibling.lnk', LINKS/'plain.txt'),
                (LINKS/'to-root.lnk', 'C:\\'), (SMALL/'to-file.lnk', FAR/'f099.txt')])
    gone.unlink()
    # Real shortcuts, copied byte for byte: no file path (a shell item) and an installed program.
    for src, name in ((USER_START/'File Explorer.lnk', 'store-like.lnk'), (USER_START/'System Tools'/'Control Panel.lnk', 'control-panel.lnk'),
                      (START/'Google Chrome.lnk', 'start-chrome.lnk'), (START/'Microsoft Edge.lnk', 'start-edge.lnk')):
        if src.exists():
            shutil.copyfile(src, LINKS/name)


def typed(a, text):
    for ch in text:
        a.msg(0x102, ord(ch)); time.sleep(.01)


def names(a):
    return a.snap()['names']


def open_menu(a, name, label=None):
    """Right click the named item. Its cell is brought into view first with the keyboard."""
    s = a.snap()
    listing = names(a)
    idx = listing.index(name)
    cols = s['cols']
    if not (s['first'] * cols <= idx < (s['last'] + 1) * cols):
        a.cell(0)
        a.key(36)   # Home
        for _ in range(idx // cols):
            a.key(40)
        for _ in range(idx % cols):
            a.key(39)
        time.sleep(.3)
    s = a.snap()
    x, y, w, h = s['row']
    col, row = idx % s['cols'], idx // s['cols'] - s['first']
    if a.view == "list":
        a.rclick(x + 100, y + row*h + h/2)
    else:
        a.rclick(x + 16 + (col + .5)*(w - 32)/s["cols"], y + row*h + 40)
    s = a.snap(label)
    assert s['ctx'], 'Context menu did not open on ' + name
    assert s['selected'] == 1 and s['selection'] == [name], (name, s['selected'], s['selection'])
    return s


def close_menu(a):
    a.key(27)
    a.until('Esc did not close the menu', lambda: not a.snap()['ctx'])


def choose(a, row):
    for _ in range(row + 1):   # the first Down lands on the first row
        a.key(40)
    a.key(13)


def inside():
    results = []
    prepare()

    def check(name, fn, view='grid', theme='light', folder=LINKS, extra=()):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        app = None
        try:
            app = App(name, folder, view, theme, extra)
            fn(app)
            app.snap(name + '-passed')
            results.append(dict(check=name, result='passed'))
        except Exception as e:
            if app:
                try:
                    app.snap(name + '-failed')
                except Exception:
                    pass
            results.append(dict(check=name, result='FAILED', reason=repr(e)))
        finally:
            if app:
                app.close()
        print(json.dumps(results[-1]), flush=True)

    def lands(a, link, folder, leaf, picture=None):
        """Choose Open File Location on `link`: the window shows `folder` with `leaf` selected and in view; Back returns."""
        start = a.snap()['path']
        s = open_menu(a, link, picture and picture + '-menu')
        assert s['lnk']['item'] == 1, 'No Open File Location for ' + link
        choose(a, 2)
        a.until('Did not open the target folder', lambda: a.snap()['path'].lower() == str(folder).lower() and a.snap()['state'] != 0, 15)
        a.until('Target not selected', lambda: a.snap()['selection'] == [leaf], 10)
        s = a.snap(picture)
        assert s['lnk']['last'] == 'opened', s['lnk']
        idx = names(a).index(leaf)
        assert s['first'] * s['cols'] <= idx < (s['last'] + 1) * s['cols'], ('Target not scrolled into view', idx, s['first'], s['last'])
        a.key(37, (18,))   # Alt+Left: one history step
        a.until('Back did not return to the shortcut folder', lambda: a.snap()['path'].lower() == start.lower(), 10)

    def file_target(a):
        lands(a, 'to-file.lnk', FAR, 'f099.txt', 'lnk-file')

    def folder_target(a):
        lands(a, 'to-folder.lnk', FOLDERS, 'd099', 'lnk-folder')

    def persian_target(a):
        lands(a, 'to-persian.lnk', FAR, PERSIAN, 'lnk-persian')

    def sibling_target(a):
        a.snap('lnk-sibling-before')
        open_menu(a, 'to-sibling.lnk'); choose(a, 2)
        a.until('Sibling target not selected', lambda: a.snap()['selection'] == ['plain.txt'], 10)

    def drive_root(a):
        open_menu(a, 'to-root.lnk'); choose(a, 2)
        a.until('Drive root did not open', lambda: a.snap()['path'].upper().rstrip('\\') == 'C:' and a.snap()['state'] != 0, 15)

    def start_menu(a):
        for link, folder, leaf in (('start-chrome.lnk', r'C:\Program Files\Google\Chrome\Application', 'chrome.exe'),
                                   ('start-edge.lnk', r'C:\Program Files (x86)\Microsoft\Edge\Application', 'msedge.exe')):
            if (LINKS/link).exists():
                lands(a, link, folder, leaf, 'lnk-' + link[:-4])

    def real_start_menu(a):
        """The real folder: right click the real Google Chrome shortcut, read only."""
        a.path(START)
        s = open_menu(a, 'Google Chrome.lnk', 'lnk-real-start-menu')
        assert s['lnk']['item'] == 1
        choose(a, 2)
        a.until('Chrome folder did not open', lambda: a.snap()['path'].lower().endswith(r'chrome\application') and a.snap()['state'] != 0, 15)
        a.until('chrome.exe not selected', lambda: a.snap()['selection'] == ['chrome.exe'], 10)

    def gone_target(a):
        open_menu(a, 'to-gone.lnk')
        assert a.snap()['lnk']['item'] == 1
        choose(a, 2)
        a.until('No answer for a deleted target', lambda: a.snap()['lnk']['last'] == 'missing', 10)
        time.sleep(.3)
        s = a.snap('lnk-gone-toast')
        assert s['path'].lower() == str(LINKS).lower(), ('The window moved', s['path'])

    def no_path(a):
        for name in ('store-like.lnk', 'control-panel.lnk'):
            if (LINKS/name).exists():
                s = open_menu(a, name, 'lnk-no-path-' + name[:-4])
                assert s['lnk']['item'] == 0, name + ' offers Open File Location'
                close_menu(a)
        s = open_menu(a, 'plain.txt')
        assert s['lnk']['item'] == 0, 'A text file offers Open File Location'
        close_menu(a)

    def two_selected(a):
        a.cell(names(a).index('to-file.lnk'))
        a.cell(names(a).index('to-folder.lnk'), (17,))
        assert a.snap()['selected'] == 2
        s = a.snap()
        listing = names(a); idx = listing.index('to-folder.lnk')
        x, y, w, h = s['row']
        a.rclick(x + 16 + (idx % s['cols'] + .5)*(w - 32)/s['cols'], y + (idx // s['cols'] - s['first'])*h + 40)
        s = a.snap('lnk-two-selected')
        assert s['ctx'] and s['selected'] == 2 and s['lnk']['item'] == 0, s['lnk']

    def offline(a):
        """An unreachable network target: the window keeps answering while the check runs, then the toast."""
        t0 = time.monotonic()
        s = open_menu(a, 'to-offline.lnk', 'lnk-offline-menu')
        menu_ms = (time.monotonic() - t0) * 1000
        assert s['lnk']['item'] == 1, 'The offline shortcut has a file path'
        choose(a, 2)
        worst, t0 = 0, time.monotonic()
        while a.snap()['lnk']['last'] == '' and time.monotonic() - t0 < OFFLINE_WAIT:
            t1 = time.monotonic()
            a.snap(); worst = max(worst, time.monotonic() - t1)
            time.sleep(.2)
        took = time.monotonic() - t0
        s = a.snap('lnk-offline-toast')
        assert s['lnk']['last'] == 'missing', ('No answer for an offline target', s['lnk'], took)
        assert s['path'].lower() == str(LINKS).lower()
        print(json.dumps(dict(offline_wait_s=round(took, 1), worst_state_dump_ms=round(worst*1000), menu_open_ms=round(menu_ms))), flush=True)
        assert worst < .5, ('The window froze while the target was checked', worst)

    def search_results(a):
        a.key(ord('F'), (17,))
        a.until('Search field did not take the keyboard', lambda: a.snap()['typing'])
        typed(a, 'to-file')
        a.until('Search did not show the shortcut', lambda: a.snap()['searching'] and a.snap()['count'] == 1, 8)
        s = open_menu(a, 'to-file.lnk', 'lnk-search')
        assert s['lnk']['item'] == 1
        choose(a, 2)
        a.until('Search result did not open the target', lambda: a.snap()['path'].lower() == str(FAR).lower() and not a.snap()['searching'] and a.snap()['selection'] == ['f099.txt'], 15)

    def small_window(a):
        dpi = U.GetDpiForWindow(a.hwnd) / 96
        U.SetWindowPos(a.hwnd, None, 0, 0, int(360*dpi), int(280*dpi), 0x16)
        time.sleep(.5)
        s = open_menu(a, 'to-file.lnk', 'lnk-small')
        assert s['lnk']['item'] == 1
        choose(a, 2)
        a.until('Small window did not open the target', lambda: a.snap()['selection'] == ['f099.txt'], 15)
        s = a.snap('lnk-small-after')
        idx = names(a).index('f099.txt')
        assert s['first'] * s['cols'] <= idx < (s['last'] + 1) * s['cols'], ('Target not in view', idx, s['first'], s['last'])

    def list_view(a):
        s = open_menu(a, 'to-file.lnk')
        assert s['lnk']['item'] == 1
        choose(a, 2)
        a.until('List view did not open the target', lambda: a.snap()['selection'] == ['f099.txt'], 15)

    def far_target(link, leaf, scroll_first):
        """A far target (index 100 or 200 of 250) from a long, scrolled shortcut folder: it ends up selected and in view in the new listing."""
        def run(a):
            if scroll_first:   # the shortcut folder is scrolled down when the shortcut is chosen: the old rows must not decide
                a.cell(0)
                a.key(36)
                for _ in range(14):
                    a.key(40)
                time.sleep(.6)
                assert a.snap()['first'] > 0, a.snap()['first']
            type_w = a.snap()['type_w']
            if a.view == 'grid' and a.snap()['zoom'] == 1:   # the column view: the first cell is the shortcut
                x, y, w, h = a.snap()['row']
                a.rclick(x + 80, y + h/2)
                s = a.snap()
                assert s['selection'] == [link], s['selection']
            else:
                s = open_menu(a, link)
            assert s['lnk']['item'] == 1
            choose(a, 2)
            a.until('Far target not selected', lambda: a.snap()['path'].lower() == str(DEEP).lower() and a.snap()['selection'] == [leaf], 15)
            idx = names(a).index(leaf)
            def in_view():
                s = a.snap()
                if a.view == 'grid' and s['zoom'] == 1:   # the column view: the target's column is among the built ones
                    return s['first'] <= idx // s['column_rows'] < s['last']
                return s['first'] * s['cols'] <= idx < (s['last'] + 1) * s['cols']
            a.until('Far target not scrolled into view', in_view, 6)
            time.sleep(.5)
            s = a.snap('lnk-far-' + leaf)
            assert in_view(), ('Far target left out of view', idx, s['first'], s['last'], s['cols'], s['column_rows'])
            assert s['type_w'] == type_w, ('The Type column changed width', type_w, s['type_w'])
        return run

    check('1-file-target', file_target)
    check('2-folder-target', folder_target)
    check('3-persian-target', persian_target)
    check('4-sibling-target', sibling_target)
    check('5-drive-root', drive_root)
    check('6-start-menu-copies', start_menu)
    check('7-real-start-menu', real_start_menu, folder=START)
    check('8-deleted-target', gone_target)
    check('9-no-file-path', no_path)
    check('10-two-selected', two_selected)
    check('11-offline-target', offline)
    check('12-search-results', search_results)
    check('13-small-window', small_window, folder=SMALL)
    check('14-list-view', list_view, 'list')
    check('15-dark-menu', lambda a: (open_menu(a, 'to-file.lnk', 'lnk-dark-menu'), None)[1], theme='dark')
    check('16-far-target-icons', far_target('f100.lnk', 'f100.txt', True), folder=SCROLLED, extra=('--zoom', '3'))
    check('17-far-target-columns', far_target('a.lnk', 'f200.txt', False), folder=SCROLLED, extra=('--zoom', '1'))
    check('18-far-target-details', far_target('a.lnk', 'f200.txt', False), 'list', folder=SCROLLED, extra=('--zoom', '0'))
    (WORK/'shortcut-results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK/'tmp').mkdir(exist_ok=True); (WORK/'appdata').mkdir(exist_ok=True)
    os.environ.update(TEMP=str(WORK/'tmp'), TMP=str(WORK/'tmp'))
    if '--inside' in sys.argv:
        return inside()
    gaming = r'C:\Users\Omid\.agents\tools\gaming.py'
    code = subprocess.run([sys.executable, gaming, 'check'], creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if code == 3:
        subprocess.run([sys.executable, gaming, 'wait', '--cap', '60'], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    elif code:
        return code
    sys.path.insert(0, r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    with (WORK/'shortcut.log').open('wb') as out, (WORK/'shortcut.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=20*60)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

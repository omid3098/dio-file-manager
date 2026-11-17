"""Real Win32 input checks for search and the context menu, on a private desktop.
Run from the repo: python tests/use/search_smoke.py
DFM_EXE selects the executable, DFM_USE_WORK the folder for artifacts and the sandbox.
It reuses the driver of input_smoke.py (imported, not copied). Requires the shared correct/hidden.py.
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
import input_smoke as base  # noqa: E402

WORK = base.WORK
FIND = WORK / 'find'


def make_sandbox():
    # Only this folder is ever emptied: it holds the files these checks look at.
    shutil.rmtree(FIND, ignore_errors=True)
    for d in ('sub/deeper', 'Documents', 'Music'):
        (FIND / d).mkdir(parents=True)
    for f in ('sub/deeper/quarterly-report.txt', 'Documents/quarterly-plan.txt', 'notes.txt', 'budget.txt', 'sub/readme.md'):
        (FIND / f).write_text('x', encoding='utf-8')


def typed(a, text):
    for ch in text:
        a.msg(0x102, ord(ch))
        time.sleep(.01)


def inside():
    results = []

    def check(name, fn, view='grid', extra=()):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        app = None
        try:
            make_sandbox()
            for f in extra:
                (FIND / f).write_text('x', encoding='utf-8')
            app = base.App(name, FIND, view)
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
        print(json.dumps(results[-1]), flush=True)

    def searched(a, count):
        def done():
            s = a.snap()
            return s['searching'] and s['count'] == count
        a.until(f'Search did not show {count} results', done, seconds=8)

    def ctrl_f(a):
        a.key(ord('F'), (17,))
        a.until('Search field did not take the keyboard', lambda: a.snap()['typing'])
        typed(a, 'quarterly')
        searched(a, 2)
        s = a.snap('search-results')
        assert s['query'] == 'quarterly', f'Query is {s["query"]!r}'
        a.key(27)
        a.until('Esc did not return to the folder', lambda: not a.snap()['search'])
        s = a.wait_ready()
        assert not s['searching'] and s['count'] == 5, f'Folder not back: {s["count"]} items'

    def one_letter(a):
        typed(a, 'q')
        a.until('A letter in the view did not open search', lambda: a.snap()['search'])
        time.sleep(1)
        s = a.snap('one-letter')
        assert s['query'] == 'q', f'Query is {s["query"]!r}, not one letter'

    def path_field_keeps_letters(a):
        a.key(ord('L'), (17,))
        typed(a, 'abc')   # at once, before the path field has shown that it holds the keyboard
        time.sleep(.6)
        s = a.snap('path-typing')
        assert not s['search'] and not s['searching'], 'Letters typed into the path field started a search'
        assert s['typing'], 'Path field lost the keyboard'

    def background_menu(a):
        a.rclick(700, 420)   # far below the one row of cells
        s = a.snap('menu-bg')
        assert s['ctx'] and s['selected'] == 0, 'No background menu below the grid rows'
        a.key(27)
        a.until('Esc did not close the menu', lambda: not a.snap()['ctx'])

    def result_opens(a):
        a.key(ord('F'), (17,))
        a.until('Search field did not take the keyboard', lambda: a.snap()['typing'])
        typed(a, 'sub')
        searched(a, 1)
        x, y = a.cell_xy(0)
        a.rclick(x, y)
        s = a.snap('result-menu')
        assert s['ctx'] and s['selected'] == 1, 'A result takes no item menu'
        a.key(27)
        a.until('Esc did not close the menu', lambda: not a.snap()['ctx'])
        a.click(x, y)
        a.click(x, y)
        a.until('Double click did not open the result', lambda: a.snap()['path'] == str(FIND / 'sub') and not a.snap()['searching'])

    def menu_keys(a):
        a.cell(0)
        x, y = a.cell_xy(0)
        a.rclick(x, y)
        assert a.snap('menu-keys-open')['ctx'], 'Item menu did not open'
        a.key(27)
        a.until('Esc did not close the menu', lambda: not a.snap()['ctx'])
        a.rclick(x, y)
        assert a.snap()['ctx'], 'Item menu did not open again'
        # Open, Open With, Cut, Copy, then Rename: the first Down lands on the first row.
        for _ in range(6):
            a.key(40)
        a.key(38)   # Up goes back one row: Rename again
        a.snap('menu-keys-down')
        a.key(13)
        a.until('Down and Enter did not choose Rename', lambda: a.snap()['act'] & 1)
        assert not a.snap()['ctx'], 'Menu stayed open after Enter'
        assert a.snap()['cursor'] == 0, 'The grid cursor moved while the menu had the keys'
        time.sleep(.5)
        a.snap('rename-popover')

    def list_menus(a):
        a.rclick(500, 46 + 36 + 18)   # the first row
        s = a.snap('menu-list-item')
        assert s['ctx'] and s['selected'] == 1, 'List view: no item menu'
        a.key(27)
        a.until('Esc did not close the menu', lambda: not a.snap()['ctx'])
        a.rclick(500, 560)   # empty space below the rows
        assert a.snap('menu-list-bg')['ctx'], 'List view: no background menu'
        a.key(27)
        a.until('Esc did not close the menu', lambda: not a.snap()['ctx'])
        a.rclick(1070, 700)   # the bottom right corner: the menu must stay inside the window
        a.snap('menu-edge')
        a.key(27)
        a.path(FIND / 'Music')
        a.rclick(500, 300)   # an empty folder
        assert a.snap('menu-list-empty')['ctx'], 'Empty folder: no menu'

    def trash_search(a):
        target = FIND / 'quarterly-root.txt'
        target.write_text('search trash check', encoding='utf-8')
        a.key(ord('F'), (17,))
        a.until('Search field did not take the keyboard', lambda: a.snap()['typing'])
        typed(a, 'quarterly')
        searched(a, 3)
        for i in range(3):
            a.cell(i)
            if a.snap()['selection'] == [target.name]:
                break
        else:
            raise AssertionError('Root search result missing')
        a.key(46)
        try:
            a.until('Search result was not trashed', lambda: not target.exists())
            a.until('Trash action still running', lambda: not a.snap()['act'] & 2)
            s = a.snap('search-after-trash')
            assert s['search'] and s['searching'] and s['query'] == 'quarterly', 'Trash left the search'
            assert s['count'] == 2, 'Trashed result still listed, or another result lost'
            time.sleep(.5)  # a new row at the deleted row's index is not a double click
            for i in range(2):
                a.cell(i)
                assert a.snap()['selection'] != [target.name], 'Deleted item still selectable'
        finally:
            if not target.exists():
                a.key(ord('Z'), (17,))
                a.until('Undo did not restore the test file', target.exists, seconds=10)
        searched(a, 3)
        assert target.read_text(encoding='utf-8') == 'search trash check'
        a.key(27)
        a.until('Esc did not leave search', lambda: not a.snap()['searching'])
        assert a.wait_ready()['count'] == 6, 'Folder listing stale after trash and Undo'

    # Either Keyboard Search. Names in the sandbox: a Persian name, one with a zero-width non-joiner, one with Arabic letters,
    # one with Latin digits, and sub/readme.md. The Persian-layout letters are the standard Windows Persian layout.
    def open_search(a):
        a.key(ord('F'), (17,))
        a.until('Search field did not take the keyboard', lambda: a.snap()['typing'])

    def found(a, text, names, label):
        a.key(ord('A'), (17,))
        typed(a, text)
        want = names   # only how many results to expect

        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            s = a.snap()
            if s['searching'] and s['query'] == text and s['count'] == len(want):
                time.sleep(.6)   # nothing more may arrive after the result
                s = a.snap(label)
                assert s['count'] == len(want), f'{text!r}: {s["count"]} results after a pause, wanted {len(want)}'
                return s
            time.sleep(.1)
        if not want:
            time.sleep(1.5)
            s = a.snap(label)
            assert s['query'] == text and s['count'] == 0, f'{text!r} found {s["count"]} results, wanted none'
            return s
        raise AssertionError(f'{text!r} did not show {len(want)} results')

    def english_keys_persian_name(a):
        open_search(a)
        s = found(a, 'sghl', ['x'], 'kb-sghl')
        assert s['chip'], 'No "Also read as" pill for a result found by the other keyboard'
        a.snap('kb-sghl-shown')
        found(a, 'sghl', ['x'], 'kb-sghl-again')

    def persian_keys_english_name(a):
        open_search(a)
        s = found(a, 'قثشیپث', ['x'], 'kb-readme')   # q-letters of "readme" on the Persian layout
        assert s['chip'], 'No pill for the Persian-layout readme'

    def zwnj_names(a):
        open_search(a)
        found(a, 'میخواهم', ['x'], 'kb-zwnj-without')
        found(a, 'می‌خواهم', ['x'], 'kb-zwnj-with')

    def arabic_letters(a):
        open_search(a)
        found(a, 'کیان', ['x'], 'kb-arabic-from-persian')
        s = a.snap()
        assert not s['chip'], 'The pill showed for a result the typed text found'
        found(a, 'كيان', ['x'], 'kb-persian-from-arabic')

    def digits(a):
        open_search(a)
        found(a, '۱۲۳', ['x'], 'kb-persian-digits')
        found(a, '123', ['x'], 'kb-latin-digits')

    def no_false_hits(a):
        open_search(a)
        found(a, 'zzqx', [], 'kb-nothing')
        found(a, 'ژژژ', [], 'kb-nothing-persian')
        assert not a.snap()['chip'], 'A pill with no result'

    kb = ('سلام.txt', 'می‌خواهم.txt', 'كيان.pdf', 'report-123.txt')
    check('9-kb-english-keys-persian-name', english_keys_persian_name, extra=kb)
    check('10-kb-persian-keys-english-name', persian_keys_english_name, extra=kb)
    check('11-kb-zwnj', zwnj_names, extra=kb)
    check('12-kb-arabic-letters', arabic_letters, extra=kb)
    check('13-kb-digits', digits, extra=kb)
    check('14-kb-no-false-hits', no_false_hits, extra=kb)
    check('1-ctrl-f-and-esc', ctrl_f)
    check('2-one-letter', one_letter)
    check('3-path-field-keeps-letters', path_field_keeps_letters)
    check('4-background-menu', background_menu)
    check('5-result-opens-and-menu', result_opens)
    check('6-menu-keys', menu_keys)
    check('7-list-menus', list_menus, 'list')
    check('8-trash-search', trash_search)
    (WORK / 'search-results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return int(any(r['result'] != 'passed' for r in results))


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / 'tmp').mkdir(exist_ok=True)
    (WORK / 'appdata').mkdir(exist_ok=True)
    os.environ.update(TEMP=str(WORK / 'tmp'), TMP=str(WORK / 'tmp'))
    if '--inside' in sys.argv:
        return inside()
    gaming = Path(r'C:\Users\Omid\.agents\tools\gaming.py')
    code = subprocess.run([sys.executable, str(gaming), 'check'], creationflags=subprocess.CREATE_NO_WINDOW).returncode
    if code == 3:
        subprocess.run([sys.executable, str(gaming), 'wait', '--cap', '60'], check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    elif code:
        return code
    sys.path.insert(0, r'C:\Users\Omid\.agents\source\correct')
    from hidden import popen
    with (WORK / 'search.log').open('wb') as out, (WORK / 'search.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=180)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

"""The second name box, address bar and search box of one window take the keyboard like the first.
Run from the repo: python tests/use/second_box_smoke.py
DFM_EXE selects the executable, DFM_USE_WORK the folder for artifacts and the sandbox.
Each check opens a box, closes it (Esc or Enter), opens it again in the same window and types there: Persian in the two name boxes, ASCII in the address bar and search box.
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
BOX = WORK / 'box'
SALAM = 'سلام'
ESC, ENTER, F2, CTRL, SHIFT = 27, 13, 113, 17, 16


def make_sandbox():
    shutil.rmtree(BOX, ignore_errors=True)
    BOX.mkdir(parents=True)
    for f in ('a.txt', 'b.txt', 'c.txt'):
        (BOX / f).write_text('x', encoding='utf-8')


def typed(a, text):
    for ch in text:
        a.msg(0x102, ord(ch))
        time.sleep(.02)
    time.sleep(.3)


def inside():
    results = []

    def check(name, fn):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        app = None
        try:
            make_sandbox()
            app = base.App(name, BOX, 'grid')
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
        print(json.dumps(results[-1], ensure_ascii=False), flush=True)

    def taken(a, what, nth):
        a.until(f'The {what} did not take the keyboard the {nth} time', lambda: a.snap()['typing'], seconds=3)
        time.sleep(.3)   # a name box selects its text on its second frame

    def left(a, what):
        a.until(f'Esc or Enter did not close the {what}', lambda: not a.snap()['act'] & 1)

    def rename(a):
        a.cell(0)
        assert a.snap()['selection'] == ['a.txt'], 'Select a.txt'
        a.key(F2)
        a.until('Rename did not open', lambda: a.snap()['act'] & 1)
        taken(a, 'rename box', 'first')
        a.key(ESC)
        left(a, 'rename box')
        a.key(F2)   # the same window, at once: the second box
        a.until('Rename did not open the second time', lambda: a.snap()['act'] & 1)
        taken(a, 'rename box', 'second')
        typed(a, SALAM)
        a.snap('rename-second')
        a.key(ENTER)
        a.until('The second rename box did not rename', lambda: (BOX / f'{SALAM}.txt').exists())
        left(a, 'rename box')
        a.until('The new name is not selected', lambda: a.snap()['selection'] == [f'{SALAM}.txt'])
        a.key(F2)   # after Enter: a third box
        a.until('Rename did not open the third time', lambda: a.snap()['act'] & 1)
        taken(a, 'rename box', 'third')
        typed(a, 'zz')
        a.key(ENTER)
        a.until('The third rename box did not rename', lambda: (BOX / 'zz.txt').exists())

    def new_folder(a):
        a.key(ord('N'), (CTRL, SHIFT))
        a.until('New folder did not open', lambda: a.snap()['act'] & 1)
        taken(a, 'new-folder box', 'first')
        a.key(ESC)
        left(a, 'new-folder box')
        a.key(ord('N'), (CTRL, SHIFT))
        a.until('New folder did not open the second time', lambda: a.snap()['act'] & 1)
        taken(a, 'new-folder box', 'second')
        typed(a, SALAM)
        a.snap('new-folder-second')
        a.key(ENTER)
        a.until('The second new-folder box did not make its folder', lambda: (BOX / SALAM).is_dir())
        left(a, 'new-folder box')
        a.key(ord('N'), (CTRL, SHIFT))
        a.until('New folder did not open the third time', lambda: a.snap()['act'] & 1)
        taken(a, 'new-folder box', 'third')
        typed(a, 'zz')
        a.key(ENTER)
        a.until('The third new-folder box did not make its folder', lambda: (BOX / 'zz').is_dir())

    def address(a):
        a.key(ord('L'), (CTRL,))
        taken(a, 'address bar', 'first')
        a.key(ESC)
        a.until('Esc did not leave the address bar', lambda: not a.snap()['typing'])
        a.key(ord('L'), (CTRL,))
        taken(a, 'address bar', 'second')
        a.key(ord('A'), (CTRL,))
        typed(a, str(BOX / 'x'))
        s = a.snap('address-second')
        assert s['typing'], 'The address bar lost the keyboard'
        a.key(ESC)

    def search(a):
        a.key(ord('F'), (CTRL,))
        taken(a, 'search box', 'first')
        a.key(ESC)
        a.until('Esc did not leave search', lambda: not a.snap()['search'])
        a.key(ord('F'), (CTRL,))
        taken(a, 'search box', 'second')
        typed(a, 'a')
        a.until('The second search did not take the letter', lambda: a.snap()['query'] == 'a')
        a.key(ESC)
        a.until('Esc did not leave search', lambda: not a.snap()['search'])

    for name, fn in (('rename', rename), ('new-folder', new_folder), ('address', address), ('search', search)):
        check(name, fn)
    (WORK / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
    return int(any(r['result'] == 'FAILED' for r in results))


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / 'tmp').mkdir(exist_ok=True)
    (WORK / 'appdata').mkdir(exist_ok=True)
    os.environ.update(TEMP=str(WORK / 'tmp'), TMP=str(WORK / 'tmp'), PYTHONIOENCODING='utf-8')
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
    with (WORK / 'second_box.log').open('wb') as out, (WORK / 'second_box.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            code = run.wait(timeout=180)
        finally:
            run.close()
    print((WORK / 'second_box.log').read_text(encoding='utf-8', errors='replace') + (WORK / 'second_box.err').read_text(encoding='utf-8', errors='replace'))
    return code


if __name__ == '__main__':
    raise SystemExit(main())

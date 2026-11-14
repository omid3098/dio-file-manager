"""Long file names in dialogs, through Win32 input, on correct/hidden.py's desktop.

A file with a 200-character name and no spaces, in a folder deep enough that its path passes 260 characters, is
sent to the Trash: the Recycle Bin cannot take it, so the app asks "Cannot move file to trash, do you want to delete
immediately?" and names the file. In each theme and window width, and for an English and a Persian name, the name
must be cut in the middle, keep its real last letters and ".txt" (no U+FFFD in the text the dialog shows), and no pixel of the dialog's text may lie outside the dialog (the scrim around it is
one flat colour). DFM_DIALOG_SHOTS=<folder> also saves each picture there.
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
from input_smoke import App, WORK, SAND, U

LONG_EN = 'long-name-' + 'x' * 186 + '.txt'
LONG_FA = ('گزارش_نهایی_سالانه_' * 11)[:190] + '.txt'
CASES = [  # name, file, theme, client width in pixels
    ('dark-1280', LONG_EN, 'dark', 1280), ('light-1280', LONG_EN, 'light', 1280), ('dark-360', LONG_EN, 'dark', 360),
    ('persian-dark-1280', LONG_FA, 'dark', 1280), ('persian-dark-360', LONG_FA, 'dark', 360),
]


def long_dir(tag):
    return SAND / tag


def make(path, text='x'):
    p = '\\\\?\\' + str(path)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    Path(p).write_text(text, encoding='utf-8')


def band_outside(png, panel, band):
    """Pixels in the band, left and right of the panel, that differ from the flat scrim beside them."""
    from PIL import Image
    im = Image.open(png).convert('RGB')
    w, h = im.size
    left_from = 0 if w < 683 else 260
    bad = []
    for y in range(band[0], min(band[1], h)):
        for x0, x1 in ((left_from, panel[0] - 2), (panel[1] + 2, w)):
            if x1 <= x0:
                continue
            row = [im.getpixel((x, y)) for x in range(x0, x1)]
            ref = sorted(row)[len(row) // 2]
            bad += [(x0 + i, y) for i, p in enumerate(row) if max(abs(p[k] - ref[k]) for k in range(3)) > 24]
    return bad


def inside():
    results = []
    shots = os.environ.get('DFM_DIALOG_SHOTS')
    for name, fname, theme, cw in CASES:
        app = None
        try:
            shutil.rmtree('\\\\?\\' + str(SAND), ignore_errors=True)
            folder = long_dir(name)
            make(folder / fname)
            assert len(str(folder / fname)) > 260
            app = App(name, folder, theme=theme)
            frame = W.RECT(); client = W.RECT()
            U.GetWindowRect(app.hwnd, C.byref(frame)); U.GetClientRect(app.hwnd, C.byref(client))
            extra_w = frame.right - frame.left - client.right
            U.SetWindowPos(app.hwnd, None, 40, 40, cw + extra_w, 800 + frame.bottom - frame.top - client.bottom, 0x14)
            app.wait_ready(); time.sleep(.4)
            app.cell(0); app.key(46)
            app.until('The trash question did not open', lambda: app.snap()['act'] & 8, 15)
            time.sleep(.8)   # the open animation is done
            s = app.snap(name + '-open')
            U.GetClientRect(app.hwnd, C.byref(client))
            scale = U.GetDpiForWindow(app.hwnd) / 96
            W_, H_ = client.right, client.bottom
            panel_w = min(440, W_ / scale - 32) * scale
            panel = (int((W_ - panel_w) / 2), int((W_ + panel_w) / 2))
            buttons = s['action_rects'][2]
            band = (int(180 * scale), int(buttons[1] * scale if buttons[1] < H_ / 2 else buttons[1]) - 8)
            text = s.get('dialog_text', '')   # the dialog's own text: whole characters, middle cut, the name's real end
            assert text and '�' not in text, 'broken text in the dialog: %r' % text
            assert '…' in text and fname[-24:] in text, 'the name lost its end: %r' % text
            label = s['labels'][0][-1]   # the grid label under it: its last line keeps the extension, whole letters
            assert '…' in label and label.endswith('.txt') and '�' not in label, 'the grid label lost its end: %r' % label
            bad = band_outside(WORK / (name + '-open.png'), panel, band)
            assert not bad, 'text outside the dialog: %d pixels, first at %s (panel x %s)' % (len(bad), bad[0], panel)
            if shots:
                Path(shots).mkdir(parents=True, exist_ok=True)
                shutil.copyfile(WORK / (name + '-open.png'), Path(shots) / (name + '.png'))
            results.append(dict(check=name, result='passed'))
        except Exception as e:
            if app:
                try:
                    app.snap(name + '-failed')
                    if shots:
                        Path(shots).mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(WORK / (name + '-failed.png'), Path(shots) / (name + '-failed.png'))
                except Exception:
                    pass
            results.append(dict(check=name, result='FAILED', reason=repr(e)))
        finally:
            if app:
                app.close()
        print(json.dumps(results[-1], ensure_ascii=False), flush=True)
    # clean what this check made, with the long-path prefix
    shutil.rmtree('\\\\?\\' + str(SAND), ignore_errors=True)
    return 1 if any(r['result'] != 'passed' for r in results) else 0


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / 'tmp').mkdir(exist_ok=True); (WORK / 'appdata').mkdir(exist_ok=True)
    os.environ.update(TEMP=str(WORK / 'tmp'), TMP=str(WORK / 'tmp'))
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
    with (WORK / 'dialog.log').open('wb') as out, (WORK / 'dialog.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=20 * 60)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

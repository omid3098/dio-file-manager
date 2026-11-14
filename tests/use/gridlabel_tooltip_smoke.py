"""A long name in the grid keeps its extension (GNOME Files 47: 3 lines, ellipsis in the middle), and its hover tooltip
holds the whole name inside its box, under the name and clear of the header. Real Win32 input on the private desktop.
Pictures of the whole window go to WORK/shots."""
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
SHOTS = Path(os.environ.get('DFM_SHOTS', WORK / 'shots'))
FA = 'گزارش_نهایی_سالانه_گزارش_نهایی_سالانه_گزارش_نهایی_سالانه.txt'
EN = 'quarterly financial statement final reviewed by everybody version 12 2026.docx'
UNBROKEN = 'a_very_long_name_with_no_spaces_at_all_just_underscores_between_every_single_word_of_it_2026.xlsx'
LONG = (FA, EN, UNBROKEN)
HEADER_H = 46   # the header bar: controls sit above this line
TIP_PAD = 9
WIDE = 'W' * 96 + '.txt'   # five tooltip lines; a short window leaves room under its label for exactly those lines


def lines_of(s, idx):
    return s['labels'][idx]


def check_labels(s):
    """Every long name's last label line shows its extension; at most 3 lines; the middle is what the ellipsis takes."""
    names = s['names']
    for name in LONG:
        lines = lines_of(s, names.index(name))
        ext = name[name.rindex('.'):]
        assert 1 < len(lines) <= 3, f'{name}: {len(lines)} lines: {lines}'
        assert lines[-1].endswith(ext), f'Label of {name!r} loses its extension: {lines}'
        assert not any('…' in l for l in lines[:-1]), f'Ellipsis not on the last line only: {lines}'
        if '…' not in lines[-1]:
            assert ''.join(lines).replace(' ', '') == name.replace(' ', ''), f'Label of {name!r} lost words without an ellipsis: {lines}'
        assert lines[0].startswith(name[:3]), f'Label of {name!r} loses its start: {lines}'


def cell_xy(s, index):
    x, y, w, h = s['row']
    return x + 16 + (index % s['cols'] + .5) * (w - 32) / s['cols'], y + (index // s['cols'] - s['first']) * h + 24


def hover_tip(a, name, theme):
    s = a.snap()
    x, y = cell_xy(s, s['names'].index(name))
    a.msg(0x200, 0, a.point(x - 6, y))
    a.msg(0x200, 0, a.point(x, y))
    a.until(f'{name!r}: tooltip never appeared', lambda: a.snap()['tooltip'], 5)
    time.sleep(.5)
    s = a.snap()
    assert s['tooltip'], 'Tooltip went away'
    return s['tip'], s


def check_tip(tip, name, s, vw, vh=800):
    bx, by, bw, bh = tip['box']
    assert tip['text'].replace('\n', '') == name or tip['text'].replace('\n', ' ') == name, f'Tooltip text {tip["text"]!r} is not {name!r}'
    assert tip['text_w'] <= bw - 2 * TIP_PAD + .5, f'{name!r}: text {tip["text_w"]:.1f} px wide in a box {bw:.1f} wide (room {bw - 2 * TIP_PAD:.1f}): the end is clipped'
    tx, ty, tw_, th_ = tip['text_rect']
    assert tx >= bx - .5 and tx + tw_ <= bx + bw + .5 and ty >= by - .5 and ty + th_ <= by + bh + .5, f'Text box {tip["text_rect"]} sticks out of the tooltip {tip["box"]}'
    assert by >= HEADER_H, f'{name!r}: tooltip at y={by:.1f} covers the header (controls end at {HEADER_H})'
    assert bx >= 0 and bx + bw <= vw + .5, f'Tooltip box sticks out of the window: {tip["box"]}'
    assert by + bh <= vh, f'Tooltip below the window: {tip["box"]}'


def shot(a, name):
    SHOTS.mkdir(parents=True, exist_ok=True)
    a.snap()
    shutil.copyfile(str(a.base) + '.png', SHOTS / (name + '.png'))


def inside():
    results = []
    folder = WORK / 'names'
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    for n in LONG + ('short.txt', 'notes.md'):
        (folder / n).write_text('x', encoding='utf-8')

    wide = WORK / 'wide'
    shutil.rmtree(wide, ignore_errors=True)
    wide.mkdir(parents=True)
    for n in (WIDE, 'short.txt'):
        (wide / n).write_text('x', encoding='utf-8')

    def check(name, fn, theme='light', size='1280x800', where=folder):
        if os.environ.get('USE_ONLY') and not name.startswith(os.environ['USE_ONLY']):
            return
        a = None
        try:
            a = base.App(name, where, 'grid', theme, extra=('--size', size))
            fn(a, theme, int(size.split('x')[0]))
            results.append(dict(check=name, result='passed'))
        except Exception as e:
            if a and a.proc.poll() is None:
                a.snap(name + '-failed')
            results.append(dict(check=name, result='FAILED', reason=str(e)))
        finally:
            if a:
                a.close()
        print(json.dumps(results[-1]), flush=True)

    def labels(a, theme, vw):
        s = a.snap()
        check_labels(s)
        shot(a, f'a-grid-{theme}-{vw}')

    def tooltips(a, theme, vw):
        s = a.snap()
        assert any('…' in l[-1] for l in s['labels'][:8]), 'No label is cut: nothing to hover'
        for name in LONG:
            if '…' not in s['labels'][s['names'].index(name)][-1]:
                continue   # GNOME Files: the tooltip only when the label is ellipsized
            tip, s = hover_tip(a, name, theme)
            check_tip(tip, name, s, vw)
            if name == FA or (name == EN and vw < 400):
                shot(a, f'b-tooltip-{theme}-{vw}' + ('' if name == FA else '-en'))
            a.msg(0x200, 0, a.point(vw - 60, 500))   # off the cell: the tooltip leaves
            a.until('tooltip stayed', lambda: not a.snap()['tooltip'], 5)
            time.sleep(.5)

    def short_window(a, theme, vw):
        """The tooltip of a 96-W name is five lines; at 1280x280 it fits under the label, so it sits there, not over the header."""
        vh = 280
        s = a.snap()
        assert '…' in s['labels'][s['names'].index(WIDE)][-1], 'The 96-W name is not cut: nothing to hover'
        tip, s = hover_tip(a, WIDE, theme)
        check_tip(tip, WIDE, s, vw, vh)
        assert tip['lines'] == 5, f'Expected five tooltip lines, drawn {tip["lines"]}: {tip["text"]!r}'
        assert tip['box'][1] + tip['box'][3] <= vh - 8 + .5, f'Tooltip box {tip["box"]} leaves less than the 8 px edge margin'
        shot(a, f'c-tooltip-{theme}-{vw}x{vh}')

    check('1-labels-light', labels, 'light')
    check('2-labels-dark', labels, 'dark')
    check('3-labels-dark-360', labels, 'dark', '360x800')
    check('4-tooltip-light', tooltips, 'light')
    check('5-tooltip-dark', tooltips, 'dark')
    check('6-tooltip-dark-360', tooltips, 'dark', '360x800')
    check('7-tooltip-dark-1280x280', short_window, 'dark', '1280x280', wide)
    return 1 if any(r['result'] != 'passed' for r in results) else 0


def main():
    for d in ('tmp', 'appdata'):
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
    with (WORK / 'run.log').open('wb') as out, (WORK / 'run.err').open('wb') as err:
        run = popen([sys.executable, str(Path(__file__).resolve()), '--inside'], ROOT, dict(os.environ), b'', out, err)
        try:
            return run.wait(timeout=240)
        finally:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())

"""The use checks must never move or hold the real pointer. Console test: it reads the sources, opens no window.

The pointer belongs to the whole window station, not to one desktop. So Windows' own move and size loops move it for real
even when a check runs on the hidden desktop: the system-menu commands for a move or a size, and a left-button press in the
non-client area on the caption or a frame edge (the loop starts at once). Setting the pointer position, injecting input and
clipping the pointer act on it directly.

A guard on this machine ends any python run of a script under a tests folder whose text contains one of the names below,
comments included. So no file in tests/use may contain them, and this file (also run as a script) builds them at run time.

Run: python -m unittest discover -s tests -p test_pointer_safe.py
"""
import ast
from pathlib import Path
import re
import unittest

TESTS = Path(__file__).resolve().parent
USE = TESTS / 'use'


def j(*parts):
    return ''.join(parts)


# Win32 calls that move or hold the pointer, or start a loop that does (as an attribute, a name or a string for getattr).
POINTER_CALLS = {j('SetCursor', 'Pos'), j('Send', 'Input'), j('mouse', '_event'), j('keybd', '_event'), j('Clip', 'Cursor'),
                 'DragDetect', 'BlockInput'}
SIZE_MOVE_NAMES = {j('SC', '_MOVE'), j('SC', '_SIZE')}
PRESS_NAMES = {j('WM_NCLBUTTON', 'DOWN'), 'WM_NCPRESS'}   # the second is the name the checks use for the press
PRESS = 161                                                # its message number
# The app answers a press on its own maximize button itself (chrome_win.c) and never hands it to Windows' tracking loop.
SAFE_FRAME_PARTS = {'HTMAXBUTTON': 9}
# The system-menu commands for a size (61440..61448) and a move (61456..61458); Windows ignores the low four bits.
SYSTEM_SIZE_MOVE = range(61440, 61472)
# What the guard on this machine matches, anywhere in a script's text.
GUARD = re.compile('|'.join([r'0x[Ff]0[01][0-9A-Fa-f]\b', r'\b' + j('SC', '_MOVE') + r'\b', r'\b' + j('SC', '_SIZE') + r'\b',
                             j('SetCursor', 'Pos'), j('Send', 'Input'), j('mouse', '_event'), j('keybd', '_event'),
                             j('Clip', 'Cursor'), j('WM_NCLBUTTON', 'DOWN'), r'0x[Aa]1\b', j('HTBOTTOM', 'RIGHT')]))


def _int(node):
    return node.value if isinstance(node, ast.Constant) and type(node.value) is int else None


def violations(source, name='<source>'):
    """Every place in the Python source that can start a system move or size loop or touch the pointer."""
    tree = ast.parse(source)
    found = []
    definitions = set()   # integer constants that only define a name, such as a message number assigned to its name
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign):
            definitions.update(id(n) for n in ast.walk(node.value) if isinstance(n, ast.Constant))
    safe_press = set()    # the press operand of a call whose next operand is a safe frame part
    for node in ast.walk(tree):
        if isinstance(node, ast.Call):
            args = node.args
            for i, arg in enumerate(args[:-1]):
                if isinstance(arg, ast.Name) and arg.id in PRESS_NAMES or _int(arg) == PRESS:
                    nxt = args[i + 1]
                    if (isinstance(nxt, ast.Name) and nxt.id in SAFE_FRAME_PARTS) or _int(nxt) in SAFE_FRAME_PARTS.values():
                        safe_press.add(id(arg))
    for node in ast.walk(tree):
        what = None
        if isinstance(node, ast.Attribute) and node.attr in POINTER_CALLS:
            what = node.attr
        elif isinstance(node, ast.Name) and node.id in POINTER_CALLS | SIZE_MOVE_NAMES:
            what = node.id
        elif isinstance(node, ast.Constant) and isinstance(node.value, str) and node.value in POINTER_CALLS:
            what = node.value
        elif isinstance(node, ast.Name) and isinstance(node.ctx, ast.Load) and node.id in PRESS_NAMES and id(node) not in safe_press:
            what = f'{node.id} outside a press on the maximize button'
        elif _int(node) is not None:
            value = node.value
            if value in SYSTEM_SIZE_MOVE:
                what = f'a system size or move command {value:#x}'
            elif value == PRESS and id(node) not in definitions and id(node) not in safe_press:
                what = f'the non-client press message ({value}) outside a press on the maximize button'
        if what:
            found.append(f'{name}:{node.lineno}: {what}')
    return found


class PointerSafeTest(unittest.TestCase):
    def test_use_checks_never_move_or_hold_the_pointer(self):
        files = sorted(USE.glob('*.py'))
        self.assertTrue(files, f'No use checks found in {USE}')
        found = [v for f in files for v in violations(f.read_text(encoding='utf-8'), f.name)]
        self.assertEqual(found, [], 'These use checks can move the real pointer:\n' + '\n'.join(found))

    def test_no_python_file_under_tests_matches_the_machine_guard(self):
        # The guard ends the run of any such script, comments included, so a match means the check never runs.
        found = [f'{f.relative_to(TESTS)}:{n}: {line.strip()[:80]}'
                 for f in sorted(TESTS.rglob('*.py')) if '__pycache__' not in f.parts
                 for n, line in enumerate(f.read_text(encoding='utf-8').splitlines(), 1) if GUARD.search(line)]
        self.assertEqual(found, [], 'These lines match the guard on the pointer:\n' + '\n'.join(found))

    def test_the_old_checks_are_caught(self):
        # The lines that moved Omid's pointer on 2026-11-17, as they were.
        down = j('WM_NCLBUTTON', 'DOWN')
        old_drag = f'assert U.PostMessageW(a.hwnd, 0x112, {61456:#x}, 0)\n'
        old_resize = f'assert U.PostMessageW(a.hwnd, {PRESS:#x}, 17, x | (y << 16))\n'
        old_named = f'{down} = {PRESS:#x}\nsend(a, {down}, HTCAPTION, 0)\n'
        for source in (old_drag, old_resize, old_named):
            self.assertTrue(violations(source), source)
            self.assertTrue(GUARD.search(source), source)   # the machine guard would end a run of them too
        for call in (f'U.{j("SetCursor", "Pos")}(1, 2)', f'U.{j("Send", "Input")}(1, 2, 3)', f'U.{j("mouse", "_event")}(0, 0, 0, 0, 0)',
                     f'U.{j("Clip", "Cursor")}(None)', f"getattr(U, '{j('SetCursor', 'Pos')}')", 'U.DragDetect(h, 1)',
                     f'send(a, 0x112, {j("SC", "_SIZE")}, 0)', 'send(a, 0x112, 61448, 0)', 'send(a, 0x112, 61456, 0)',
                     'send(a, 161, 2, 0)', 'send(a, WM_NCPRESS, HTCAPTION, 0)'):
            self.assertTrue(violations(call), call)

    def test_what_stays_allowed(self):
        for source in ('send(a, WM_NCPRESS, HTMAXBUTTON, 0)', 'U.PostMessageW(h, 161, 9, 0)', 'send(a, 0x112, 0xF030, 0)',
                       'send(a, 0x112, 0xF120, 0)', 'send(a, WM_NCLBUTTONDBLCLK, HTCAPTION, 0)', 'WM_NCPRESS = 161'):
            self.assertEqual(violations(source), [], source)


if __name__ == '__main__':
    unittest.main()

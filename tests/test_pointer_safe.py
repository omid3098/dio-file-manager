"""The use checks must never move or hold the real pointer. Console test: it reads the sources, opens no window.

The pointer belongs to the whole window station, not to one desktop. So Windows' own move and size loops move it for real
even when a check runs on the hidden desktop: SC_MOVE / SC_SIZE sent as WM_SYSCOMMAND, and WM_NCLBUTTONDOWN on the caption or a
frame edge (the loop starts at once). SetCursorPos, SendInput, mouse_event, ClipCursor and DragDetect act on it directly.

Run: python -m unittest discover -s tests -p test_pointer_safe.py
"""
import ast
from pathlib import Path
import unittest

USE = Path(__file__).resolve().parent / 'use'
# Win32 calls that move or hold the pointer, or start a loop that does (as an attribute, a name or a string for getattr).
FORBIDDEN_CALLS = {'SetCursorPos', 'SendInput', 'mouse_event', 'ClipCursor', 'DragDetect', 'BlockInput'}
FORBIDDEN_NAMES = {'SC_MOVE', 'SC_SIZE'}
WM_NCLBUTTONDOWN = 0xA1
# The app answers a press on its own maximize button itself (chrome_win.c) and never hands it to Windows' tracking loop.
SAFE_FRAME_PARTS = {'HTMAXBUTTON': 9}


def _int(node):
    return node.value if isinstance(node, ast.Constant) and type(node.value) is int else None


def violations(source, name='<source>'):
    """Every place in the Python source that can start a system move or size loop or touch the pointer."""
    tree = ast.parse(source)
    found = []
    definitions = set()   # integer constants that only define a name, such as WM_NCLBUTTONDOWN = 0xA1
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign):
            definitions.update(id(n) for n in ast.walk(node.value) if isinstance(n, ast.Constant))
    safe_press = set()    # the WM_NCLBUTTONDOWN operand of a call whose next operand is a safe frame part
    for node in ast.walk(tree):
        if isinstance(node, ast.Call):
            args = node.args
            for i, arg in enumerate(args[:-1]):
                if isinstance(arg, ast.Name) and arg.id == 'WM_NCLBUTTONDOWN' or _int(arg) == WM_NCLBUTTONDOWN:
                    nxt = args[i + 1]
                    if (isinstance(nxt, ast.Name) and nxt.id in SAFE_FRAME_PARTS) or _int(nxt) in SAFE_FRAME_PARTS.values():
                        safe_press.add(id(arg))
    for node in ast.walk(tree):
        what = None
        if isinstance(node, ast.Attribute) and node.attr in FORBIDDEN_CALLS:
            what = node.attr
        elif isinstance(node, ast.Name) and node.id in FORBIDDEN_CALLS | FORBIDDEN_NAMES:
            what = node.id
        elif isinstance(node, ast.Constant) and isinstance(node.value, str) and node.value in FORBIDDEN_CALLS:
            what = node.value
        elif isinstance(node, ast.Name) and isinstance(node.ctx, ast.Load) and node.id == 'WM_NCLBUTTONDOWN' and id(node) not in safe_press:
            what = 'WM_NCLBUTTONDOWN outside a press on the maximize button'
        elif _int(node) is not None:
            value = node.value
            if 0xF000 <= value <= 0xF01F:   # SC_SIZE 0xF000..0xF008 and SC_MOVE 0xF010..0xF012; Windows ignores the low four bits
                what = f'SC_SIZE or SC_MOVE {value:#x}'
            elif value == WM_NCLBUTTONDOWN and id(node) not in definitions and id(node) not in safe_press:
                what = 'WM_NCLBUTTONDOWN (0xA1) outside a press on the maximize button'
        if what:
            found.append(f'{name}:{node.lineno}: {what}')
    return found


class PointerSafeTest(unittest.TestCase):
    def test_use_checks_never_move_or_hold_the_pointer(self):
        files = sorted(USE.glob('*.py'))
        self.assertTrue(files, f'No use checks found in {USE}')
        found = [v for f in files for v in violations(f.read_text(encoding='utf-8'), f.name)]
        self.assertEqual(found, [], 'These use checks can move the real pointer:\n' + '\n'.join(found))

    def test_the_old_checks_are_caught(self):
        # The lines that moved Omid's pointer on 2026-11-17, as they were.
        old_drag = 'assert U.PostMessageW(a.hwnd, 0x112, 0xF010, 0)   # SC_MOVE\n'
        old_resize = 'assert U.PostMessageW(a.hwnd, 0xA1, 17, x | (y << 16))  # HTBOTTOMRIGHT\n'
        old_named = 'WM_NCLBUTTONDOWN = 0xA1\nsend(a, WM_NCLBUTTONDOWN, HTCAPTION, 0)\n'
        for source in (old_drag, old_resize, old_named):
            self.assertTrue(violations(source), source)
        for call in ('U.SetCursorPos(1, 2)', 'U.SendInput(1, 2, 3)', 'U.mouse_event(0, 0, 0, 0, 0)', 'U.ClipCursor(None)',
                     "getattr(U, 'SetCursorPos')", 'U.DragDetect(h, 1)', 'send(a, 0x112, SC_SIZE, 0)', 'send(a, 0x112, 0xF008, 0)'):
            self.assertTrue(violations(call), call)

    def test_what_stays_allowed(self):
        for source in ('send(a, WM_NCLBUTTONDOWN, HTMAXBUTTON, 0)', 'U.PostMessageW(h, 0xA1, 9, 0)', 'send(a, 0x112, 0xF030, 0)',
                       'send(a, 0x112, 0xF120, 0)', 'send(a, WM_NCLBUTTONDBLCLK, HTCAPTION, 0)', 'WM_NCLBUTTONDOWN = 0xA1'):
            self.assertEqual(violations(source), [], source)


if __name__ == '__main__':
    unittest.main()

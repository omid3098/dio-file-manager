"""Exercise byte-for-byte clipboard restoration, including the failed-check path.
Run only on the hidden desktop, with a live app window as the clipboard owner.
"""
import ctypes as C
from ctypes import wintypes as W
import struct
import threading
from clipboard_guard import U, K, open_clipboard
from input_smoke import preserved_clipboard

K.GlobalAlloc.argtypes = [W.UINT, C.c_size_t]
K.GlobalAlloc.restype = W.HGLOBAL
K.GlobalSize.argtypes = [W.HGLOBAL]
K.GlobalSize.restype = C.c_size_t
U.RegisterClipboardFormatW.argtypes = [W.LPCWSTR]


def roundtrip(hwnd):
    def contend(expected):
        outcomes = []
        def other_thread():
            mutex = K.CreateMutexW(None, False, r'Local\DioFileManagerClipboardTest')
            assert mutex
            try:
                result = K.WaitForSingleObject(mutex, 0)
                outcomes.append(result)
                if result in (0, 0x80):
                    K.ReleaseMutex(mutex)
            finally:
                K.CloseHandle(mutex)
        thread = threading.Thread(target=other_thread)
        thread.start(); thread.join(timeout=3)
        assert outcomes == [expected], f'Clipboard mutex ownership: {outcomes}'
    binary = U.RegisterClipboardFormatW('Dio File Manager clipboard preservation test')
    fixture = {
        13: 'clipboard sentinel فارسی\0'.encode('utf-16-le'),
        binary: bytes(range(256)),
        15: struct.pack('<IiiII', 20, 0, 0, 0, 1) + 'C:\\sandbox-only.txt\0\0'.encode('utf-16-le'),
    }

    def put(values):
        open_clipboard(hwnd)
        try:
            assert U.EmptyClipboard()
            for fmt, data in values.items():
                h = K.GlobalAlloc(2, len(data))
                assert h
                p = K.GlobalLock(h)
                assert p
                C.memmove(p, data, len(data))
                K.GlobalUnlock(h)
                assert U.SetClipboardData(fmt, h)
        finally:
            U.CloseClipboard()

    def verify():
        open_clipboard(hwnd)
        try:
            for fmt, expected in fixture.items():
                h = U.GetClipboardData(fmt)
                assert h
                p = K.GlobalLock(h)
                assert p
                try:
                    assert C.string_at(p, len(expected)) == expected, f'Clipboard format {fmt} changed'
                finally:
                    K.GlobalUnlock(h)
        finally:
            U.CloseClipboard()

    with preserved_clipboard(hwnd):  # restore the user's original formats last
        contend(258)  # Another worker cannot enter until save/use/restore has finished.
        put(fixture)
        with preserved_clipboard(hwnd):
            put({13: 'changed\0'.encode('utf-16-le')})
        verify()
        try:
            with preserved_clipboard(hwnd):
                put({13: 'failed check\0'.encode('utf-16-le')})
                raise ValueError('simulated check failure')
        except ValueError:
            pass
        verify()

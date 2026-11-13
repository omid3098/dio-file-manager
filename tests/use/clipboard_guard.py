"""Keep all clipboard formats in memory while a real-input check copies files."""
import ctypes as C
from ctypes import wintypes as W
from contextlib import contextmanager
import time

U = C.WinDLL('user32', use_last_error=True)
O = C.WinDLL('ole32', use_last_error=True)
K = C.WinDLL('kernel32', use_last_error=True)
G = C.WinDLL('gdi32', use_last_error=True)
U.OpenClipboard.argtypes = [W.HWND]
U.EnumClipboardFormats.argtypes = [W.UINT]
U.GetClipboardData.argtypes = [W.UINT]
U.GetClipboardData.restype = W.HANDLE
U.SetClipboardData.argtypes = [W.UINT, W.HANDLE]
U.SetClipboardData.restype = W.HANDLE
O.OleDuplicateData.argtypes = [W.HANDLE, W.WORD, W.UINT]
O.OleDuplicateData.restype = W.HANDLE
G.CopyEnhMetaFileW.argtypes = [W.HANDLE, W.LPCWSTR]
G.CopyEnhMetaFileW.restype = W.HANDLE
G.DeleteEnhMetaFile.argtypes = [W.HANDLE]
G.DeleteMetaFile.argtypes = [W.HANDLE]
G.DeleteObject.argtypes = [W.HANDLE]
K.GlobalLock.argtypes = [W.HGLOBAL]
K.GlobalLock.restype = C.c_void_p
K.GlobalUnlock.argtypes = [W.HGLOBAL]
K.GlobalFree.argtypes = [W.HGLOBAL]
K.CreateMutexW.argtypes = [C.c_void_p, W.BOOL, W.LPCWSTR]
K.CreateMutexW.restype = W.HANDLE
K.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
K.ReleaseMutex.argtypes = [W.HANDLE]
K.CloseHandle.argtypes = [W.HANDLE]


@contextmanager
def clipboard_test_lock():
    mutex = K.CreateMutexW(None, False, r'Local\DioFileManagerClipboardTest')
    if not mutex:
        raise C.WinError(C.get_last_error())
    owned = False
    try:
        result = K.WaitForSingleObject(mutex, 15 * 60 * 1000)
        owned = result in (0, 0x80)  # acquired, or abandoned by an exited worker
        if not owned:
            if result == 0x102:
                raise TimeoutError('Clipboard test lock was busy for 15 minutes')
            raise C.WinError(C.get_last_error())
        yield
    finally:
        if owned:
            K.ReleaseMutex(mutex)
        K.CloseHandle(mutex)


@contextmanager
def preserved_clipboard(hwnd):
    with clipboard_test_lock():
        with _preserved_clipboard(hwnd):
            yield


def open_clipboard(hwnd):
    deadline = time.monotonic() + 3
    while not U.OpenClipboard(hwnd):
        if time.monotonic() >= deadline:
            raise C.WinError(C.get_last_error())
        time.sleep(.02)


def free_copy(fmt, handle):
    if fmt in (2, 9):  # bitmap or palette
        G.DeleteObject(handle)
    elif fmt == 14:
        G.DeleteEnhMetaFile(handle)
    else:
        if fmt == 3:
            class Metafile(C.Structure):
                _fields_ = [('mapping', C.c_int), ('x', C.c_int), ('y', C.c_int), ('handle', W.HANDLE)]
            p = K.GlobalLock(handle)
            if p:
                G.DeleteMetaFile(Metafile.from_address(p).handle)
                K.GlobalUnlock(handle)
        K.GlobalFree(handle)


@contextmanager
def _preserved_clipboard(hwnd):
    copies = {}
    try:
        open_clipboard(hwnd)
        try:
            fmt = 0
            while True:
                C.set_last_error(0)
                fmt = U.EnumClipboardFormats(fmt)
                if not fmt:
                    if C.get_last_error():
                        raise C.WinError(C.get_last_error())
                    break
                source = U.GetClipboardData(fmt)
                duplicate = G.CopyEnhMetaFileW(source, None) if fmt == 14 else O.OleDuplicateData(source, fmt, 0)
                if not duplicate:
                    raise RuntimeError(f'Cannot preserve clipboard format {fmt}; check refused before copying')
                copies[fmt] = duplicate
        finally:
            U.CloseClipboard()
        try:
            yield
        finally:
            open_clipboard(hwnd)
            try:
                if not U.EmptyClipboard():
                    raise C.WinError(C.get_last_error())
                for fmt in list(copies):
                    if not U.SetClipboardData(fmt, copies[fmt]):
                        raise C.WinError(C.get_last_error())
                    del copies[fmt]  # Windows owns the restored handle.
            finally:
                U.CloseClipboard()
    finally:
        for fmt, handle in copies.items():
            free_copy(fmt, handle)

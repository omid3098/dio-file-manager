"""Remove from the real Recycle Bin exactly the items a use check sent there, when the check ends.

An item belongs to a check when the original path in its $I file lies under the check's work folder and the
item was deleted after the check started. Nothing else is touched. input_smoke installs this for every suite,
in the outer process and in the hidden-desktop process, so a failed or killed run is cleaned up too.
Command line: python recycle_guard.py WORKDIR [--since SECONDS_AGO]  (removes the matching items now).
"""
import atexit
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import time

EPOCH_DIFF = 11644473600  # seconds from 1601 to 1970


def read_original(info):
    """(original path, deletion time as unix seconds) from a $I file's bytes, or None."""
    if len(info) < 28:
        return None
    version, _size, filetime = struct.unpack_from('<qqq', info, 0)
    if version == 2:
        (chars,) = struct.unpack_from('<i', info, 24)
        raw = info[28:28 + 2 * chars]
    elif version == 1:
        raw = info[24:24 + 520]
    else:
        return None
    path = raw.decode('utf-16-le', 'replace').split('\x00')[0]
    return path, filetime / 1e7 - EPOCH_DIFF


def norm(p):
    return os.path.normcase(os.path.normpath(str(p))).rstrip('\\') + '\\'


def purge(bin_dirs, work, since):
    """Delete $I/$R pairs under each recycle folder whose original path is inside `work` and that were deleted at
    or after unix time `since`. Returns the original paths removed."""
    prefix, removed = norm(work), []
    for folder in bin_dirs:
        try:
            infos = [p for p in Path(folder).iterdir() if p.name.startswith('$I')]
        except OSError:
            continue
        for info in infos:
            try:
                item = read_original(info.read_bytes())
            except OSError:
                continue
            if not item or not norm(item[0]).startswith(prefix) or item[1] < since:
                continue
            data = info.with_name('$R' + info.name[2:])
            try:
                if data.is_dir() and not data.is_symlink():
                    shutil.rmtree(data, onerror=lambda f, p, e: (os.chmod(p, 0o666), f(p)))
                elif data.exists() or data.is_symlink():
                    os.chmod(data, 0o666)
                    data.unlink()
                info.unlink()
                removed.append(item[0])
            except OSError as e:
                print(json.dumps({'recycle_guard': 'could not remove', 'item': item[0], 'error': str(e)}), flush=True)
    return removed


def bin_dirs_for(work):
    drive = os.path.splitdrive(os.path.abspath(work))[0]
    try:
        return [p for p in Path(drive + '\\$Recycle.Bin').iterdir() if p.is_dir()]
    except OSError:
        return []


_installed = set()


def install(work):
    """Clean up this check's own Recycle Bin items when the process exits, however it ends."""
    work = os.path.abspath(work)
    if work in _installed:
        return
    _installed.add(work)
    since = time.time()

    def clean():
        removed = purge(bin_dirs_for(work), work, since)
        if removed:
            print(json.dumps({'recycle_guard_removed': len(removed)}), flush=True)
    atexit.register(clean)


if __name__ == '__main__':
    args = sys.argv[1:]
    ago = float(args[args.index('--since') + 1]) if '--since' in args else 86400.0
    done = purge(bin_dirs_for(args[0]), args[0], time.time() - ago)
    print(json.dumps({'removed': len(done)}))
